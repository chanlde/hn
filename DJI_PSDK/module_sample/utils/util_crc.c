/**
 * @file util_crc.c
 * @brief CRC-16 (Modbus): poly 0x8005 reflected, init 0xFFFF.
 */
#include "util_crc.h"

uint16_t Util_Crc16Calculate(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;

    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ 0xA001U;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}
