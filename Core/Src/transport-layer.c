#include "transport-layer.h"

typedef enum TL_State_TypeDef {
    State_PacketType,
	State_PacketCommand,
	State_PacketData,
    State_PacketCRC,
} TL_State_TypeDef;

static TL_State_TypeDef state = State_PacketType;
static uint8_t packet_data_byte_encapsulated = 0;
static bool waiting_for_ack = false;

static TL_Packet_TypeDef packet_buffer[PACKET_BUFFER_LENGTH];
static uint32_t packet_read_index = 0;
static uint32_t packet_write_index = 0;
static uint32_t packet_buffer_mask = PACKET_BUFFER_LENGTH - 1;

extern UART_HandleTypeDef huart2;

static TL_Packet_TypeDef temp_packet = { 
    .PacketType = 0, 
    .PacketCommand = 0,
    .PacketData = {0},
    .PacketCRC = 0 
};

static TL_Packet_TypeDef retx_packet = { 
	.PacketType = 0, 
	.PacketCommand = 0,
	.PacketData = {0}, 
	.PacketCRC = 0 
};

static TL_Packet_TypeDef ack_packet = { 
	.PacketType = 0, 
	.PacketCommand = 0,
	.PacketData = {0}, 
	.PacketCRC = 0 
};

static TL_Packet_TypeDef last_transmitted_packet = { 
	.PacketType = 0, 
	.PacketCommand = 0,
	.PacketData = {0}, 
	.PacketCRC = 0 
};

bool TL_Verify_RETX_Packet(const TL_Packet_TypeDef* pPacket) 
{
	if (pPacket->PacketType != PACKET_RTX) 
	{
        return false;
    }

	if (pPacket->PacketCommand != 0xFF) 
	{
        return false;
    }

    for (uint8_t i = 0; i < PACKET_DATA_BYTE_SIZE; i++) 
	{
        if (pPacket->PacketData[i] != 0xFF) 
		{
            return false;
        }
    }

    return true;
}

bool TL_Verify_ACK_Packet(const TL_Packet_TypeDef* pPacket) 
{
	if (pPacket->PacketType != PACKET_ACK) 
	{
        return false;
    }
    
	if (pPacket->PacketCommand != 0xFF) 
	{
        return false;
    }

    for (uint8_t i = 0; i < PACKET_DATA_BYTE_SIZE; i++) 
	{
        if (pPacket->PacketData[i] != 0xFF) 
		{
            return false;
        }
    }

    return true;
}

bool TL_Verify_Command_Packet(const TL_Packet_TypeDef* pPacket, const uint8_t Command) 
{
	if (pPacket->PacketType != 0x00) 
	{
		return false;
	}
	
	if (pPacket->PacketCommand != Command) 
	{
		return false;
	}

	if (pPacket->PacketCRC != TL_Compute_CRC((const TL_Packet_TypeDef*)pPacket)) 
	{
		return false;
	}

	return true;
}

void TL_Create_RTX_Packet(TL_Packet_TypeDef* pPacket) 
{
    memset(pPacket, 0xFF, sizeof(TL_Packet_TypeDef));
    pPacket->PacketType = PACKET_RTX;
    pPacket->PacketCRC = TL_Compute_CRC((const TL_Packet_TypeDef*)pPacket);
}

void TL_Create_ACK_Packet(TL_Packet_TypeDef* pPacket) 
{
    memset(pPacket, 0xFF, sizeof(TL_Packet_TypeDef));
    pPacket->PacketType = PACKET_ACK;
    pPacket->PacketCRC = TL_Compute_CRC((const TL_Packet_TypeDef*)pPacket);
}

void TL_Create_Command_Packet(TL_Packet_TypeDef* pPacket, uint8_t Command) 
{
    memset(pPacket, 0xFF, sizeof(TL_Packet_TypeDef));
	pPacket->PacketType = 0x00;
    pPacket->PacketCommand = Command;
    pPacket->PacketCRC = TL_Compute_CRC((const TL_Packet_TypeDef*)pPacket);
}

void TL_Create_Data_Command_Packet(TL_Packet_TypeDef* pPacket, uint8_t Command, uint8_t* pData, uint8_t DataSize) 
{
    memset(pPacket, 0xFF, sizeof(TL_Packet_TypeDef));
	pPacket->PacketType = 0x00;
    pPacket->PacketCommand = Command;
	memcpy(pPacket->PacketData, pData, DataSize);
    pPacket->PacketCRC = TL_Compute_CRC((const TL_Packet_TypeDef*)pPacket);
}

uint8_t TL_Compute_CRC(const TL_Packet_TypeDef* pPacket) 
{
	return crc8((uint8_t*)pPacket, PACKET_LENGTH - PACKET_CRC_BYTE_SIZE);
}

bool TL_Is_Packet_Available(void) {
    return packet_read_index != packet_write_index;
}

void TL_Init(void) 
{
    TL_Create_RTX_Packet(&retx_packet);
    TL_Create_ACK_Packet(&ack_packet);
}

void TL_Write(TL_Packet_TypeDef* pPacket, bool IsNeedingAck) 
{
    HAL_UART_Transmit(&huart2, (uint8_t*)pPacket, PACKET_LENGTH, 500);
    memcpy(&last_transmitted_packet, pPacket, sizeof(TL_Packet_TypeDef));
	if (IsNeedingAck) {
		waiting_for_ack = true;
	}
}

void TL_Read(TL_Packet_TypeDef* pPacket) {
    memcpy(pPacket, &packet_buffer[packet_read_index], sizeof(TL_Packet_TypeDef));
    packet_read_index = (packet_read_index + 1) & packet_buffer_mask;
}

bool TL_Update(RB_TypeDef* pRingBuffer) 
{    
	// SYNCHRONIZE OUR SOFTWARE WriteIndex TO MATCH THE HARDWARE
    RING_BUFFER_Sync_Write_Index(pRingBuffer);
	
    while (!RING_BUFFER_Is_Empty(pRingBuffer)) 
	{
        switch (state) 
		{
			case State_PacketType: 
			{
				RING_BUFFER_Read(pRingBuffer, &temp_packet.PacketType);
				state = State_PacketCommand;
				continue;
			} break;

			case State_PacketCommand: 
			{
				RING_BUFFER_Read(pRingBuffer, &temp_packet.PacketCommand);
				state = State_PacketData;
				continue;
			} break;

			case State_PacketData: 
			{
				if (packet_data_byte_encapsulated == PACKET_DATA_BYTE_SIZE) 
				{
					state = State_PacketCRC;
					packet_data_byte_encapsulated = 0;
					continue;
				}
				RING_BUFFER_Read(pRingBuffer, &temp_packet.PacketData[packet_data_byte_encapsulated]);
				packet_data_byte_encapsulated++;
				continue;
			} break;

			case State_PacketCRC: 
			{
				state = State_PacketType;
				RING_BUFFER_Read(pRingBuffer, &temp_packet.PacketCRC);
				if (temp_packet.PacketCRC != TL_Compute_CRC((const TL_Packet_TypeDef*)&temp_packet)) 
				{
                    TL_Write(&retx_packet, false);
                    return false;
                }

				if (TL_Verify_RETX_Packet(&temp_packet)) 
				{
                    TL_Write(&last_transmitted_packet, false);
                    return false;
                }

				if (waiting_for_ack) 
				{
					if (TL_Verify_ACK_Packet(&temp_packet)) {
						waiting_for_ack = false;
						return true;
					}
					else
					{
						return false;
					} 
				}
				else 
				{
					uint32_t next_write_index = (packet_write_index + 1) & packet_buffer_mask;
					if (next_write_index == packet_read_index) 
					{
						__asm__("BKPT #0");
					}
                
					memcpy(&packet_buffer[packet_write_index], &temp_packet, sizeof(TL_Packet_TypeDef));
					packet_write_index = next_write_index;
					TL_Write(&ack_packet, false);
				}
			} break;

            default: {
                state = State_PacketType;
            }
        }
    }

	if (waiting_for_ack) 
	{
		return false;
	}
	else
	{
		return true;
	}
}
