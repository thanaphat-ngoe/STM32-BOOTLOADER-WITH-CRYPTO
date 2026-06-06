#include "crc8.h"

volatile int x = 0;

uint8_t crc8(uint8_t* pData, uint32_t Length) {
    uint8_t crc = 0;

    for (uint32_t i = 0; i < Length; i++) {
        crc ^= pData[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
        x++;
    }

    return crc;
}
