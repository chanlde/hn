/**
 ********************************************************************
 * @file    servo_current.h
 * @brief   PA4 / ADC1 CH18 舵机电流采样（底层读接口）
 ********************************************************************
 */

#ifndef SERVO_CURRENT_H
#define SERVO_CURRENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

void ServoCurrent_Init(void);
uint16_t ServoCurrent_ReadRaw(void);
uint32_t ServoCurrent_ReadMv(void);

#ifdef __cplusplus
}
#endif

#endif /* SERVO_CURRENT_H */
