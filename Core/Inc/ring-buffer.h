#ifndef INC_RINGBUFFER_H
#define INC_RINGBUFFER_H

#include "main.h"

typedef struct __attribute__((packed)) RB_TypeDef {
    uint8_t* Buffer;
    uint32_t Mask;
    uint32_t ReadIndex;
    uint32_t WriteIndex;
} RB_TypeDef;

void RING_BUFFER_Init(RB_TypeDef* pRingBuffer, uint8_t* pBuffer, uint32_t Size);
bool RING_BUFFER_Is_Empty(RB_TypeDef* pRingBuffer);
bool RING_BUFFER_Read(RB_TypeDef* pRingBuffer, uint8_t* pData);
void RING_BUFFER_Sync_Write_Index(RB_TypeDef* pRingBuffer);

#endif
