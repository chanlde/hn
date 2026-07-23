/**
 * @file util_crc.h
 * @brief CRC16 (Modbus/IBM poly 0x8005, init 0xFFFF) — 与地面站保持一致时可替换算法。
 */
#ifndef UTIL_CRC_H
#define UTIL_CRC_H

#include <stdint.h>

uint16_t Util_Crc16Calculate(const uint8_t *data, uint32_t len);

#endif
