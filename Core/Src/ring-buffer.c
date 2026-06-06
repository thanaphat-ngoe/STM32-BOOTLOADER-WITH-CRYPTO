#include "ring-buffer.h"

extern UART_HandleTypeDef huart2;

void RING_BUFFER_Init(RB_TypeDef* pRingBuffer, uint8_t* pBuffer, uint32_t Size) {
    pRingBuffer->Buffer = pBuffer;
	pRingBuffer->Mask = Size - 1;
    pRingBuffer->ReadIndex = 0;
    pRingBuffer->WriteIndex = 0;
    
}

bool RING_BUFFER_Is_Empty(RB_TypeDef* pRingBuffer) {
    return pRingBuffer->ReadIndex == pRingBuffer->WriteIndex;
}

bool RING_BUFFER_Read(RB_TypeDef* pRingBuffer, uint8_t* pData) {
    uint32_t local_read_index = pRingBuffer->ReadIndex;
    uint32_t local_write_index = pRingBuffer->WriteIndex;

    if (local_read_index == local_write_index) {
        return false;
    }

    *pData = pRingBuffer->Buffer[local_read_index];
	// Round value back to zero if variable went to the end
    local_read_index = (local_read_index + 1) & pRingBuffer->Mask;
    pRingBuffer->ReadIndex = local_read_index;
    
    return true;
}

void RING_BUFFER_Sync_Write_Index(RB_TypeDef* pRingBuffer) {
    uint32_t size = pRingBuffer->Mask + 1; 
    pRingBuffer->WriteIndex = size - __HAL_DMA_GET_COUNTER(huart2.hdmarx);
    pRingBuffer->WriteIndex &= pRingBuffer->Mask; 
}
