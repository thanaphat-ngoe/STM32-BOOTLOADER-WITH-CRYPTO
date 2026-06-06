#ifndef TRANSPORT_LAYER_H
#define TRANSPORT_LAYER_H

#include <stdint.h>
#include <stdbool.h>

#define PACKET_LENGTH              7
#define PACKET_DATA_BYTE_SIZE      4
#define PACKET_BUFFER_LENGTH       16
#define PACKET_BUFFER_MASK         (PACKET_BUFFER_LENGTH - 1)

#define PACKET_RTX                 0xFF
#define PACKET_ACK                 0xEF
#define PACKET_CMD                 0x00

#pragma pack(push, 1)
typedef struct {
    uint8_t PacketType;
    uint8_t PacketCommand;
    uint8_t PacketData[PACKET_DATA_BYTE_SIZE];
    uint8_t PacketCRC;
} __attribute__((packed)) TL_Packet_TypeDef;
#pragma pack(pop)

void TL_Init(void);
bool TL_Send_Command(uint8_t cmd, const uint8_t* data, uint32_t timeout_ms);
bool TL_Wait_For_Command(uint32_t timeout_ms, uint8_t* out_cmd, uint8_t* out_data);

#endif // TRANSPORT_LAYER_H
