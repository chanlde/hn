/**
 ******************************************************************************
 * @file    uart.h
 * @version V1.0.0
 * @date    2017/11/10
 * @brief   This is the header file for "uart.c".
 *
 * @copyright (c) 2021 DJI. All rights reserved.
 *
 * All information contained herein is, and remains, the property of DJI.
 * The intellectual and technical concepts contained herein are proprietary
 * to DJI and may be covered by U.S. and foreign patents, patents in process,
 * and protected by trade secret or copyright law.  Dissemination of this
 * information, including but not limited to data and other proprietary
 * material(s) incorporated within the information, in any form, is strictly
 * prohibited without the express written consent of DJI.
 *
 * If you receive this source code without DJI’s authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 *
 ******************************************************************************
 */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __DJI_PSDK_BSP_UART_H__
#define __DJI_PSDK_BSP_UART_H__

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx.h"

/* Exported constants --------------------------------------------------------*/
#define USING_UART_PORT_1
#define USING_UART_PORT_2
#define USING_UART_PORT_6
#define USING_UART_PORT_7

#define UART_ERROR      (-1)

/* Exported macros -----------------------------------------------------------*/
#define DJI_CONSOLE_UART_NUM             UART_NUM_1
#define DJI_CONSOLE_UART_BAUD            115200

/* Exported types ------------------------------------------------------------*/
typedef enum {
    UART_NUM_1 = 1,
    UART_NUM_2 = 2,
    UART_NUM_6 = 6,
    UART_NUM_7 = 7,
} E_UartNum;

typedef struct {
    uint32_t countOfLostData; /*!< Count of data lost, unit: byte. */
    uint16_t maxUsedCapacityOfBuffer; /*!< Max capacity of buffer that have been used, unit: byte. */
} T_UartBufferState;

typedef struct {
    uint8_t rxDmaActive;
    uint32_t dmaIrqCount;
    uint32_t idleIrqCount;
    uint32_t drainCount;
    uint32_t drainBytes;
    uint16_t maxDrainBytes;
    uint32_t dmaErrorCount;
} T_UartDmaPerfStats;

/* Exported variables --------------------------------------------------------*/
/* Exported functions --------------------------------------------------------*/
void UART_Init(E_UartNum uartNum, uint32_t baudRate);
void UART_InitSbus(E_UartNum uartNum, uint8_t rxInvert);
int UART_SetBaudRate(E_UartNum uartNum, uint32_t baudRate);
int UART_Read(E_UartNum uartNum, uint8_t *buf, uint16_t readSize);
int UART_Write(E_UartNum uartNum, const uint8_t *buf, uint16_t writeSize);
void UART_GetBufferState(E_UartNum uartNum, T_UartBufferState *readBufferState, T_UartBufferState *writeBufferState);

/** UART7（Air780E）IRQ 内 ORE/FE/NE 清除累计次数 */
uint32_t UART_GetUart7LineErrorClears(void);
void UART_GetUart7DmaPerfStats(T_UartDmaPerfStats *stats);
uint32_t UART_GetUart2RxByteCount(void);
uint32_t UART_GetUart2LineErrorClears(void);
uint32_t UART_GetUart6RxByteCount(void);
uint32_t UART_GetUart6LineErrorClears(void);
void UART_PollUart6Rx(void);

/* Private constants ---------------------------------------------------------*/
/* Private macros ------------------------------------------------------------*/
/* Private types -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

#endif /* __DJI_PSDK_BSP_UART_H__ */
