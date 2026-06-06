#ifndef DATA_LINK_LAYER_H
#define DATA_LINK_LAYER_H

#include <stdint.h>
#include <stdbool.h>

extern bool debug_mode;

bool DLL_Open(const char* port);
void DLL_Close(void);
int  DLL_Read(uint8_t* buffer, uint32_t size);
void DLL_Write(const uint8_t* data, uint32_t size);
uint64_t DLL_Get_Time_MS(void);
void DLL_Sleep_MS(uint32_t ms);

#endif // DATA_LINK_LAYER_H
