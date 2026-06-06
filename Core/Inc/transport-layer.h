#ifndef INC_TL_H
#define INC_TL_H

#include "main.h"
#include "ring-buffer.h"
#include "crc8.h"

#define PACKET_TYPE_BYTE_SIZE      (1) // 1 Byte
#define PACKET_COMMAND_BYTE_SIZE   (1) // 1 Byte
#define PACKET_DATA_BYTE_SIZE      (4) // 4 Bytes
#define PACKET_CRC_BYTE_SIZE       (1) // 1 Byte

#define PACKET_LENGTH (        \
    PACKET_TYPE_BYTE_SIZE    + \
    PACKET_COMMAND_BYTE_SIZE + \
    PACKET_DATA_BYTE_SIZE    + \
    PACKET_CRC_BYTE_SIZE       \
) // 11 Bytes

#define PACKET_RTX                 (0xFF)
#define PACKET_ACK                 (0xEF)

#define PACKET_BUFFER_LENGTH       (8)

typedef struct __attribute__((packed)) TL_Packet_TypeDef {
    uint8_t PacketType;
	uint8_t PacketCommand;
    uint8_t PacketData[PACKET_DATA_BYTE_SIZE];
    uint8_t PacketCRC;
} TL_Packet_TypeDef;

bool TL_Verify_RETX_Packet(const TL_Packet_TypeDef* pPacket);
bool TL_Verify_ACK_Packet(const TL_Packet_TypeDef* pPacket);
bool TL_Verify_Command_Packet(const TL_Packet_TypeDef* pPacket, const uint8_t Command);
uint8_t TL_Compute_CRC(const TL_Packet_TypeDef* pPacket);

void TL_Create_RETX_Packet(TL_Packet_TypeDef* pPacket);
void TL_Create_ACK_Packet(TL_Packet_TypeDef* pPacket);
void TL_Create_Command_Packet(TL_Packet_TypeDef* pPacket, uint8_t Command);
void TL_Create_Data_Command_Packet(TL_Packet_TypeDef* pPacket, uint8_t Command, uint8_t* pData, uint8_t DataSize);

bool TL_Is_Packet_Available(void);
void TL_Init(void);
void TL_Write(TL_Packet_TypeDef* pPacket, bool IsNeedingAck);
void TL_Read(TL_Packet_TypeDef* pPacket);
bool TL_Update(RB_TypeDef* pRingBuffer);

#endif
