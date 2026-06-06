#include "transport-layer.h"
#include "data-link-layer.h"
#include <string.h>
#include <stdio.h>

typedef enum {
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

static TL_Packet_TypeDef temp_packet;
static TL_Packet_TypeDef retx_packet;
static TL_Packet_TypeDef ack_packet;
static TL_Packet_TypeDef last_transmitted_packet;

static uint8_t calculate_crc8(const uint8_t* pData, uint32_t Length) {
    uint8_t crc = 0x00;
    for (uint32_t i = 0; i < Length; i++) {
        crc ^= pData[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) crc = (crc << 1) ^ 0x07;
            else crc = (crc << 1);
        }
    }
    return crc;
}

static void print_hex(const char* prefix, uint8_t* data, int len) {
    if (!debug_mode) return;
    printf("     %s ", prefix);
    for(int i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\n");
}

static void TL_Write(TL_Packet_TypeDef* pPacket, bool IsNeedingAck) {
    DLL_Write((uint8_t*)pPacket, PACKET_LENGTH);
    memcpy(&last_transmitted_packet, pPacket, sizeof(TL_Packet_TypeDef));
    
    if (IsNeedingAck) waiting_for_ack = true;

    if (pPacket->PacketType == PACKET_CMD) print_hex("[TX CMD]", (uint8_t*)pPacket, PACKET_LENGTH);
    else if (pPacket->PacketType == PACKET_ACK) print_hex("[TX ACK]", (uint8_t*)pPacket, PACKET_LENGTH);
    else if (pPacket->PacketType == PACKET_RTX) print_hex("[TX RTX]", (uint8_t*)pPacket, PACKET_LENGTH);
}

void TL_Init(void) {
    memset(&retx_packet, 0xFF, sizeof(TL_Packet_TypeDef));
    retx_packet.PacketType = PACKET_RTX;
    retx_packet.PacketCRC = calculate_crc8((uint8_t*)&retx_packet, PACKET_LENGTH - 1);

    memset(&ack_packet, 0xFF, sizeof(TL_Packet_TypeDef));
    ack_packet.PacketType = PACKET_ACK;
    ack_packet.PacketCRC = calculate_crc8((uint8_t*)&ack_packet, PACKET_LENGTH - 1);
}

static void TL_Update(void) {
    uint8_t rx_buf[128];
    int n = DLL_Read(rx_buf, sizeof(rx_buf));
    
    for(int i = 0; i < n; i++) {
        uint8_t b = rx_buf[i];
        
        switch (state) {
            case State_PacketType:
                temp_packet.PacketType = b;
                state = State_PacketCommand;
                break;

            case State_PacketCommand:
                temp_packet.PacketCommand = b;
                state = State_PacketData;
                packet_data_byte_encapsulated = 0;
                break;

            case State_PacketData:
                temp_packet.PacketData[packet_data_byte_encapsulated++] = b;
                if (packet_data_byte_encapsulated == PACKET_DATA_BYTE_SIZE) {
                    state = State_PacketCRC;
                }
                break;

            case State_PacketCRC:
                temp_packet.PacketCRC = b;
                state = State_PacketType;

                if (temp_packet.PacketCRC != calculate_crc8((uint8_t*)&temp_packet, 6)) {
                    TL_Write(&retx_packet, false); // สั่งขอข้อมูลใหม่
                    break;
                }

                if (temp_packet.PacketType == PACKET_RTX && temp_packet.PacketCommand == 0xFF) {
                    TL_Write(&last_transmitted_packet, false);
                    break;
                }

                if (waiting_for_ack) {
                    if (temp_packet.PacketType == PACKET_ACK && temp_packet.PacketCommand == 0xFF) {
                        waiting_for_ack = false;
                        print_hex("[RX RES]", (uint8_t*)&temp_packet, PACKET_LENGTH);
                    }
                    break;
                }

                if (temp_packet.PacketType == PACKET_CMD) {
                    print_hex("[RX CMD]", (uint8_t*)&temp_packet, PACKET_LENGTH);
                    packet_buffer[packet_write_index] = temp_packet;
                    packet_write_index = (packet_write_index + 1) & PACKET_BUFFER_MASK;
                    TL_Write(&ack_packet, false);
                }
                break;
        }
    }
}

// --- Application API ---
bool TL_Send_Command(uint8_t cmd, const uint8_t* data, uint32_t timeout_ms) {
    TL_Packet_TypeDef pkt;
    memset(&pkt, 0xFF, sizeof(TL_Packet_TypeDef));
    pkt.PacketType = PACKET_CMD;
    pkt.PacketCommand = cmd;
    if (data != NULL) memcpy(pkt.PacketData, data, PACKET_DATA_BYTE_SIZE);
    pkt.PacketCRC = calculate_crc8((uint8_t*)&pkt, PACKET_LENGTH - 1);

    for (int attempt = 0; attempt < 5; attempt++) {
        TL_Write(&pkt, true);
        uint64_t start = DLL_Get_Time_MS();
        
        while (DLL_Get_Time_MS() - start < timeout_ms) {
            TL_Update();
            if (!waiting_for_ack) return true;
            DLL_Sleep_MS(1);
        }
        if(debug_mode) printf("[TL] Timeout waiting for ACK (Attempt %d/5)\n", attempt + 1);
    }
    if(debug_mode) printf("[TL] Error: Failed to receive ACK.\n");
    return false;
}

bool TL_Wait_For_Command(uint32_t timeout_ms, uint8_t* out_cmd, uint8_t* out_data) {
    uint64_t start = DLL_Get_Time_MS();
    while (DLL_Get_Time_MS() - start < timeout_ms) {
        TL_Update();
        
        if (packet_read_index != packet_write_index) {
            TL_Packet_TypeDef pkt;
            memcpy(&pkt, &packet_buffer[packet_read_index], sizeof(TL_Packet_TypeDef));
            packet_read_index = (packet_read_index + 1) & PACKET_BUFFER_MASK;
            
            *out_cmd = pkt.PacketCommand;
            if (out_data) memcpy(out_data, pkt.PacketData, PACKET_DATA_BYTE_SIZE);
            return true;
        }
        DLL_Sleep_MS(1);
    }
    return false;
}
