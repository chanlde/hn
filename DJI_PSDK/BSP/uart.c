/**
 ******************************************************************************
 * @file    uart.c
 * @version V1.0.0
 * @date    2017/11/10
 * @brief   The file define UART interface driver functions.
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

/* Includes ------------------------------------------------------------------*/
#include "uart.h"
#include "stm32h7xx_hal.h"
#include "dji_ringbuffer.h"
#include "FreeRTOS.h"
#include "task.h"
#include "osal.h"
#include <stdint.h>
#include <string.h>

/* Private typedef -----------------------------------------------------------*/

/* Private define ------------------------------------------------------------*/
//uart uart buffer size define
#define UART1_READ_BUF_SIZE      512
#define UART1_WRITE_BUF_SIZE     4096
#define UART2_READ_BUF_SIZE      2048
#define UART2_WRITE_BUF_SIZE     2048
#define UART6_READ_BUF_SIZE      2048
#define UART6_WRITE_BUF_SIZE     512
#define UART7_READ_BUF_SIZE      8192
#define UART7_WRITE_BUF_SIZE     2048
#define UART7_RX_DMA_BUF_SIZE    8192U
#define UART7_RX_DMA_STREAM      DMA1_Stream0
#define UART7_RX_DMA_IRQn        DMA1_Stream0_IRQn
#define UART7_RX_DMA_REQUEST     DMA_REQUEST_UART7_RX
#define UART7_DMA_CACHE_LINE_SIZE 32U
#define UART7_DMA_BUFFER_ATTR    __attribute__((section(".dma_buffer"), aligned(UART7_DMA_CACHE_LINE_SIZE)))

/* Private macro -------------------------------------------------------------*/

/* Private variables ---------------------------------------------------------*/
#ifdef USING_UART_PORT_1
//UART1 read ring buffer structure
static T_RingBuffer s_uart1ReadRingBuffer;
//USART1 read buffer state
static T_UartBufferState s_uart1ReadBufferState;
//UART1 write ring buffer structure
static T_RingBuffer s_uart1WriteRingBuffer;
//USART1 write buffer state
static T_UartBufferState s_uart1WriteBufferState;
//UART1 read buffer
static uint8_t s_uart1ReadBuf[UART1_READ_BUF_SIZE];
//UART1 write buffer
static uint8_t s_uart1WriteBuf[UART1_WRITE_BUF_SIZE];

//UART1 mutex
static T_DjiMutexHandle s_uart1Mutex;
//USART1 handle
static UART_HandleTypeDef s_uart1Handle;
#endif

#ifdef USING_UART_PORT_2
static T_RingBuffer s_uart2ReadRingBuffer;
static T_UartBufferState s_uart2ReadBufferState;
static T_RingBuffer s_uart2WriteRingBuffer;
static T_UartBufferState s_uart2WriteBufferState;
static uint8_t s_uart2ReadBuf[UART2_READ_BUF_SIZE];
static uint8_t s_uart2WriteBuf[UART2_WRITE_BUF_SIZE];

static T_DjiMutexHandle s_uart2Mutex;
static UART_HandleTypeDef s_uart2Handle;
static volatile uint32_t s_uart2RxByteCount;
static volatile uint32_t s_uart2LineErrorClears;
#endif

#ifdef USING_UART_PORT_6
static T_RingBuffer s_uart6ReadRingBuffer;
static T_UartBufferState s_uart6ReadBufferState;
static T_RingBuffer s_uart6WriteRingBuffer;
static T_UartBufferState s_uart6WriteBufferState;
static uint8_t s_uart6ReadBuf[UART6_READ_BUF_SIZE];
static uint8_t s_uart6WriteBuf[UART6_WRITE_BUF_SIZE];

static T_DjiMutexHandle s_uart6Mutex;
static UART_HandleTypeDef s_uart6Handle;
static volatile uint32_t s_uart6RxByteCount;
static volatile uint32_t s_uart6LineErrorClears;
#endif

#ifdef USING_UART_PORT_7
static T_RingBuffer s_uart7ReadRingBuffer;
static T_UartBufferState s_uart7ReadBufferState;
static T_RingBuffer s_uart7WriteRingBuffer;
static T_UartBufferState s_uart7WriteBufferState;
static uint8_t s_uart7ReadBuf[UART7_READ_BUF_SIZE];
static uint8_t s_uart7WriteBuf[UART7_WRITE_BUF_SIZE];
static UART7_DMA_BUFFER_ATTR uint8_t s_uart7RxDmaBuf[UART7_RX_DMA_BUF_SIZE];

static T_DjiMutexHandle s_uart7Mutex;
static UART_HandleTypeDef s_uart7Handle;
static DMA_HandleTypeDef s_uart7RxDmaHandle;
static volatile uint16_t s_uart7RxDmaLastPos;
static volatile uint8_t s_uart7RxDmaActive;
static volatile uint32_t s_uart7DmaIrqCount;
static volatile uint32_t s_uart7IdleIrqCount;
static volatile uint32_t s_uart7DmaDrainCount;
static volatile uint32_t s_uart7DmaDrainBytes;
static volatile uint16_t s_uart7DmaMaxDrainBytes;
static volatile uint32_t s_uart7DmaErrorCount;
/** ORE/FE/NE 在 ISR 中清除次数（供调试/心跳统计） */
static volatile uint32_t s_uart7LineErrorClears;
#endif

/* Exported variables --------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
extern void Error_Handler(void);

/* Private functions ---------------------------------------------------------*/
#ifdef USING_UART_PORT_7
static void uart7_dma_invalidate(const void *addr, uint32_t len)
{
#if (__DCACHE_PRESENT == 1U)
    uintptr_t start = ((uintptr_t)addr) & ~(uintptr_t)(UART7_DMA_CACHE_LINE_SIZE - 1U);
    uintptr_t end = ((uintptr_t)addr + len + UART7_DMA_CACHE_LINE_SIZE - 1U) &
                    ~(uintptr_t)(UART7_DMA_CACHE_LINE_SIZE - 1U);

    if (len > 0U && (SCB->CCR & SCB_CCR_DC_Msk) != 0U) {
        SCB_InvalidateDCache_by_Addr((uint32_t *)start, (int32_t)(end - start));
    }
#else
    (void)addr;
    (void)len;
#endif
}

static void uart7_dma_reset_stats(void)
{
    s_uart7RxDmaLastPos = 0U;
    s_uart7RxDmaActive = 0U;
    s_uart7DmaIrqCount = 0U;
    s_uart7IdleIrqCount = 0U;
    s_uart7DmaDrainCount = 0U;
    s_uart7DmaDrainBytes = 0U;
    s_uart7DmaMaxDrainBytes = 0U;
    s_uart7DmaErrorCount = 0U;
    s_uart7LineErrorClears = 0U;
}

static void uart7_dma_put_span(const uint8_t *data, uint16_t len)
{
    uint16_t written;
    uint16_t usedCapacityOfBuffer;

    if (len == 0U) {
        return;
    }

    uart7_dma_invalidate(data, len);
    written = RingBuf_Put(&s_uart7ReadRingBuffer, data, len);
    usedCapacityOfBuffer = UART7_READ_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart7ReadRingBuffer);
    if (usedCapacityOfBuffer > s_uart7ReadBufferState.maxUsedCapacityOfBuffer) {
        s_uart7ReadBufferState.maxUsedCapacityOfBuffer = usedCapacityOfBuffer;
    }
    s_uart7ReadBufferState.countOfLostData += (uint32_t)(len - written);
}

static void uart7_dma_drain_locked(void)
{
    uint16_t pos;
    uint16_t last;
    uint16_t drained = 0U;
    uint16_t len;
    uint16_t len0;
    uint16_t len1;

    if (s_uart7RxDmaActive == 0U || s_uart7RxDmaHandle.Instance == NULL) {
        return;
    }

    pos = (uint16_t)(UART7_RX_DMA_BUF_SIZE - __HAL_DMA_GET_COUNTER(&s_uart7RxDmaHandle));
    if (pos >= UART7_RX_DMA_BUF_SIZE) {
        pos = 0U;
    }

    last = s_uart7RxDmaLastPos;
    if (pos == last) {
        return;
    }

    if (pos > last) {
        len = (uint16_t)(pos - last);
        uart7_dma_put_span(&s_uart7RxDmaBuf[last], len);
        drained = len;
    } else {
        len0 = (uint16_t)(UART7_RX_DMA_BUF_SIZE - last);
        len1 = pos;
        uart7_dma_put_span(&s_uart7RxDmaBuf[last], len0);
        uart7_dma_put_span(&s_uart7RxDmaBuf[0], len1);
        drained = (uint16_t)(len0 + len1);
    }

    s_uart7RxDmaLastPos = pos;
    s_uart7DmaDrainCount++;
    s_uart7DmaDrainBytes += drained;
    if (drained > s_uart7DmaMaxDrainBytes) {
        s_uart7DmaMaxDrainBytes = drained;
    }
}

static void uart7_dma_drain(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    uart7_dma_drain_locked();
    if (primask == 0U) {
        __enable_irq();
    }
}

static void uart7_dma_stop(void)
{
    s_uart7RxDmaActive = 0U;
    __HAL_UART_DISABLE_IT(&s_uart7Handle, UART_IT_IDLE);
    __HAL_UART_DISABLE_IT(&s_uart7Handle, UART_IT_ERR);
    (void)HAL_UART_DMAStop(&s_uart7Handle);
    (void)HAL_DMA_DeInit(&s_uart7RxDmaHandle);
    HAL_NVIC_DisableIRQ(UART7_RX_DMA_IRQn);
}

static void uart7_dma_start(void)
{
    __HAL_RCC_DMA1_CLK_ENABLE();

    s_uart7RxDmaHandle.Instance = UART7_RX_DMA_STREAM;
    s_uart7RxDmaHandle.Init.Request = UART7_RX_DMA_REQUEST;
    s_uart7RxDmaHandle.Init.Direction = DMA_PERIPH_TO_MEMORY;
    s_uart7RxDmaHandle.Init.PeriphInc = DMA_PINC_DISABLE;
    s_uart7RxDmaHandle.Init.MemInc = DMA_MINC_ENABLE;
    s_uart7RxDmaHandle.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    s_uart7RxDmaHandle.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    s_uart7RxDmaHandle.Init.Mode = DMA_CIRCULAR;
    s_uart7RxDmaHandle.Init.Priority = DMA_PRIORITY_HIGH;
    s_uart7RxDmaHandle.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    s_uart7RxDmaHandle.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_FULL;
    s_uart7RxDmaHandle.Init.MemBurst = DMA_MBURST_SINGLE;
    s_uart7RxDmaHandle.Init.PeriphBurst = DMA_PBURST_SINGLE;

    if (HAL_DMA_Init(&s_uart7RxDmaHandle) != HAL_OK) {
        Error_Handler();
    }
    __HAL_LINKDMA(&s_uart7Handle, hdmarx, s_uart7RxDmaHandle);

    s_uart7RxDmaLastPos = 0U;
    uart7_dma_invalidate(s_uart7RxDmaBuf, UART7_RX_DMA_BUF_SIZE);
    if (HAL_UART_Receive_DMA(&s_uart7Handle, s_uart7RxDmaBuf,
                             (uint16_t)UART7_RX_DMA_BUF_SIZE) != HAL_OK) {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(UART7_RX_DMA_IRQn, 6, 0);
    HAL_NVIC_ClearPendingIRQ(UART7_RX_DMA_IRQn);
    HAL_NVIC_EnableIRQ(UART7_RX_DMA_IRQn);

    __HAL_UART_CLEAR_IDLEFLAG(&s_uart7Handle);
    __HAL_UART_ENABLE_IT(&s_uart7Handle, UART_IT_IDLE);
    __HAL_UART_ENABLE_IT(&s_uart7Handle, UART_IT_ERR);
    s_uart7RxDmaActive = 1U;
}
#endif

/* Exported functions --------------------------------------------------------*/

/**
 * @brief UART initialization function.
 * @param uartNum UART number to be initialized.
 * @param baudRate UART baudrate.
 * @return None.
 */
void UART_Init(E_UartNum uartNum, uint32_t baudRate)
{
    switch (uartNum) {

#ifdef USING_UART_PORT_1
        case UART_NUM_1: {
            RingBuf_Init(&s_uart1ReadRingBuffer, s_uart1ReadBuf, UART1_READ_BUF_SIZE);
            RingBuf_Init(&s_uart1WriteRingBuffer, s_uart1WriteBuf, UART1_WRITE_BUF_SIZE);

            s_uart1Handle.Instance = USART1;
            s_uart1Handle.Init.BaudRate = baudRate;
            s_uart1Handle.Init.WordLength = UART_WORDLENGTH_8B;
            s_uart1Handle.Init.StopBits = UART_STOPBITS_1;
            s_uart1Handle.Init.Parity = UART_PARITY_NONE;
            s_uart1Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
            s_uart1Handle.Init.Mode = UART_MODE_TX_RX;
            s_uart1Handle.Init.OverSampling = UART_OVERSAMPLING_16;
            HAL_UART_Init(&s_uart1Handle);
            __HAL_UART_ENABLE_IT(&s_uart1Handle, UART_IT_RXNE);
			HAL_NVIC_EnableIRQ(USART1_IRQn);

            Osal_MutexCreate(&s_uart1Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_2
        case UART_NUM_2: {
            RingBuf_Init(&s_uart2ReadRingBuffer, s_uart2ReadBuf, UART2_READ_BUF_SIZE);
            RingBuf_Init(&s_uart2WriteRingBuffer, s_uart2WriteBuf, UART2_WRITE_BUF_SIZE);
            s_uart2RxByteCount = 0U;
            s_uart2LineErrorClears = 0U;

            s_uart2Handle.Instance = USART2;
            s_uart2Handle.Init.BaudRate = baudRate;
            s_uart2Handle.Init.WordLength = UART_WORDLENGTH_8B;
            s_uart2Handle.Init.StopBits = UART_STOPBITS_1;
            s_uart2Handle.Init.Parity = UART_PARITY_NONE;
            s_uart2Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
            s_uart2Handle.Init.Mode = UART_MODE_TX_RX;
            s_uart2Handle.Init.OverSampling = UART_OVERSAMPLING_16;
            HAL_UART_Init(&s_uart2Handle);
            /* H743：与 UART7 一致，关闭 FIFO，避免 TX/RX 与中断行为异常 */
            (void)HAL_UARTEx_SetTxFifoThreshold(&s_uart2Handle, UART_TXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_SetRxFifoThreshold(&s_uart2Handle, UART_RXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_DisableFifoMode(&s_uart2Handle);
            __HAL_UART_ENABLE_IT(&s_uart2Handle, UART_IT_RXNE);
            HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
            HAL_NVIC_EnableIRQ(USART2_IRQn);

            Osal_MutexCreate(&s_uart2Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_6
        case UART_NUM_6: {
            RingBuf_Init(&s_uart6ReadRingBuffer, s_uart6ReadBuf, UART6_READ_BUF_SIZE);
            RingBuf_Init(&s_uart6WriteRingBuffer, s_uart6WriteBuf, UART6_WRITE_BUF_SIZE);

            s_uart6Handle.Instance = USART6;
            s_uart6Handle.Init.BaudRate = baudRate;
            s_uart6Handle.Init.WordLength = UART_WORDLENGTH_8B;
            s_uart6Handle.Init.StopBits = UART_STOPBITS_1;
            s_uart6Handle.Init.Parity = UART_PARITY_NONE;
            s_uart6Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
            s_uart6Handle.Init.Mode = UART_MODE_TX_RX;
            s_uart6Handle.Init.OverSampling = UART_OVERSAMPLING_16;
            s_uart6Handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
            s_uart6Handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
            s_uart6Handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
            HAL_UART_Init(&s_uart6Handle);
            (void)HAL_UARTEx_SetTxFifoThreshold(&s_uart6Handle, UART_TXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_SetRxFifoThreshold(&s_uart6Handle, UART_RXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_DisableFifoMode(&s_uart6Handle);
            __HAL_UART_ENABLE_IT(&s_uart6Handle, UART_IT_RXNE);
            HAL_NVIC_SetPriority(USART6_IRQn, 6, 0);
            HAL_NVIC_EnableIRQ(USART6_IRQn);

            Osal_MutexCreate(&s_uart6Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_7
        case UART_NUM_7: {
            RingBuf_Init(&s_uart7ReadRingBuffer, s_uart7ReadBuf, UART7_READ_BUF_SIZE);
            RingBuf_Init(&s_uart7WriteRingBuffer, s_uart7WriteBuf, UART7_WRITE_BUF_SIZE);
            memset(&s_uart7ReadBufferState, 0, sizeof(s_uart7ReadBufferState));
            memset(&s_uart7WriteBufferState, 0, sizeof(s_uart7WriteBufferState));
            uart7_dma_reset_stats();

            s_uart7Handle.Instance = UART7;
            s_uart7Handle.Init.BaudRate = baudRate;
            s_uart7Handle.Init.WordLength = UART_WORDLENGTH_8B;
            s_uart7Handle.Init.StopBits = UART_STOPBITS_1;
            s_uart7Handle.Init.Parity = UART_PARITY_NONE;
            s_uart7Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
            s_uart7Handle.Init.Mode = UART_MODE_TX_RX;
            s_uart7Handle.Init.OverSampling = UART_OVERSAMPLING_16;
            s_uart7Handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
            s_uart7Handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
            s_uart7Handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
            HAL_UART_Init(&s_uart7Handle);
            /* H743：关闭 FIFO，避免 TX/RX 与中断行为异常 */
            (void)HAL_UARTEx_SetTxFifoThreshold(&s_uart7Handle, UART_TXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_SetRxFifoThreshold(&s_uart7Handle, UART_RXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_EnableFifoMode(&s_uart7Handle);
            uart7_dma_start();
            HAL_NVIC_SetPriority(UART7_IRQn, 6, 0);
            HAL_NVIC_ClearPendingIRQ(UART7_IRQn);
            HAL_NVIC_EnableIRQ(UART7_IRQn);

            Osal_MutexCreate(&s_uart7Mutex);
        }
            break;
#endif

        default:
            break;
    }
}

void UART_InitSbus(E_UartNum uartNum, uint8_t rxInvert)
{
    if (uartNum != UART_NUM_2 && uartNum != UART_NUM_6) {
        UART_Init(uartNum, 100000U);
        return;
    }

    if (uartNum == UART_NUM_2) {
#ifdef USING_UART_PORT_2
        RingBuf_Init(&s_uart2ReadRingBuffer, s_uart2ReadBuf, UART2_READ_BUF_SIZE);
        RingBuf_Init(&s_uart2WriteRingBuffer, s_uart2WriteBuf, UART2_WRITE_BUF_SIZE);
        memset(&s_uart2ReadBufferState, 0, sizeof(s_uart2ReadBufferState));
        memset(&s_uart2WriteBufferState, 0, sizeof(s_uart2WriteBufferState));
        s_uart2RxByteCount = 0U;
        s_uart2LineErrorClears = 0U;

        s_uart2Handle.Instance = USART2;
        s_uart2Handle.Init.BaudRate = 100000U;
        s_uart2Handle.Init.WordLength = UART_WORDLENGTH_9B;
        s_uart2Handle.Init.StopBits = UART_STOPBITS_2;
        s_uart2Handle.Init.Parity = UART_PARITY_EVEN;
        s_uart2Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
        s_uart2Handle.Init.Mode = UART_MODE_TX_RX;
        s_uart2Handle.Init.OverSampling = UART_OVERSAMPLING_16;
        s_uart2Handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
        s_uart2Handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
        s_uart2Handle.AdvancedInit.AdvFeatureInit = rxInvert ? UART_ADVFEATURE_RXINVERT_INIT : UART_ADVFEATURE_NO_INIT;
        s_uart2Handle.AdvancedInit.RxPinLevelInvert = rxInvert ? UART_ADVFEATURE_RXINV_ENABLE : UART_ADVFEATURE_RXINV_DISABLE;
        HAL_UART_Init(&s_uart2Handle);
        (void)HAL_UARTEx_SetTxFifoThreshold(&s_uart2Handle, UART_TXFIFO_THRESHOLD_1_8);
        (void)HAL_UARTEx_SetRxFifoThreshold(&s_uart2Handle, UART_RXFIFO_THRESHOLD_1_8);
        (void)HAL_UARTEx_DisableFifoMode(&s_uart2Handle);
        __HAL_UART_ENABLE_IT(&s_uart2Handle, UART_IT_RXNE);
        HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);

        if (s_uart2Mutex == NULL) {
            Osal_MutexCreate(&s_uart2Mutex);
        }
#endif
        return;
    }

#ifdef USING_UART_PORT_6
    RingBuf_Init(&s_uart6ReadRingBuffer, s_uart6ReadBuf, UART6_READ_BUF_SIZE);
    RingBuf_Init(&s_uart6WriteRingBuffer, s_uart6WriteBuf, UART6_WRITE_BUF_SIZE);
    memset(&s_uart6ReadBufferState, 0, sizeof(s_uart6ReadBufferState));
    memset(&s_uart6WriteBufferState, 0, sizeof(s_uart6WriteBufferState));
    s_uart6RxByteCount = 0U;
    s_uart6LineErrorClears = 0U;

    s_uart6Handle.Instance = USART6;
    s_uart6Handle.Init.BaudRate = 100000U;
    s_uart6Handle.Init.WordLength = UART_WORDLENGTH_9B;
    s_uart6Handle.Init.StopBits = UART_STOPBITS_2;
    s_uart6Handle.Init.Parity = UART_PARITY_EVEN;
    s_uart6Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    s_uart6Handle.Init.Mode = UART_MODE_TX_RX;
    s_uart6Handle.Init.OverSampling = UART_OVERSAMPLING_16;
    s_uart6Handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    s_uart6Handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    s_uart6Handle.AdvancedInit.AdvFeatureInit = rxInvert ? UART_ADVFEATURE_RXINVERT_INIT : UART_ADVFEATURE_NO_INIT;
    s_uart6Handle.AdvancedInit.RxPinLevelInvert = rxInvert ? UART_ADVFEATURE_RXINV_ENABLE : UART_ADVFEATURE_RXINV_DISABLE;
    HAL_UART_Init(&s_uart6Handle);
    (void)HAL_UARTEx_SetTxFifoThreshold(&s_uart6Handle, UART_TXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_SetRxFifoThreshold(&s_uart6Handle, UART_RXFIFO_THRESHOLD_1_8);
    (void)HAL_UARTEx_DisableFifoMode(&s_uart6Handle);
    __HAL_UART_ENABLE_IT(&s_uart6Handle, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART6_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART6_IRQn);

    if (s_uart6Mutex == NULL) {
        Osal_MutexCreate(&s_uart6Mutex);
    }
#endif
}

int UART_SetBaudRate(E_UartNum uartNum, uint32_t baudRate)
{
    switch (uartNum) {
#ifdef USING_UART_PORT_7
        case UART_NUM_7: {
            Osal_MutexLock(s_uart7Mutex);

            uart7_dma_stop();
            (void)HAL_UART_Abort(&s_uart7Handle);
            (void)HAL_UART_DeInit(&s_uart7Handle);

            RingBuf_Init(&s_uart7ReadRingBuffer, s_uart7ReadBuf, UART7_READ_BUF_SIZE);
            RingBuf_Init(&s_uart7WriteRingBuffer, s_uart7WriteBuf, UART7_WRITE_BUF_SIZE);
            memset(&s_uart7ReadBufferState, 0, sizeof(s_uart7ReadBufferState));
            memset(&s_uart7WriteBufferState, 0, sizeof(s_uart7WriteBufferState));
            uart7_dma_reset_stats();

            s_uart7Handle.Instance = UART7;
            s_uart7Handle.Init.BaudRate = baudRate;
            s_uart7Handle.Init.WordLength = UART_WORDLENGTH_8B;
            s_uart7Handle.Init.StopBits = UART_STOPBITS_1;
            s_uart7Handle.Init.Parity = UART_PARITY_NONE;
            s_uart7Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
            s_uart7Handle.Init.Mode = UART_MODE_TX_RX;
            s_uart7Handle.Init.OverSampling = UART_OVERSAMPLING_16;
            s_uart7Handle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
            s_uart7Handle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
            s_uart7Handle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
            if (HAL_UART_Init(&s_uart7Handle) != HAL_OK) {
                Osal_MutexUnlock(s_uart7Mutex);
                return UART_ERROR;
            }
            (void)HAL_UARTEx_SetTxFifoThreshold(&s_uart7Handle, UART_TXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_SetRxFifoThreshold(&s_uart7Handle, UART_RXFIFO_THRESHOLD_1_8);
            (void)HAL_UARTEx_EnableFifoMode(&s_uart7Handle);
            uart7_dma_start();
            HAL_NVIC_ClearPendingIRQ(UART7_IRQn);
            HAL_NVIC_EnableIRQ(UART7_IRQn);

            Osal_MutexUnlock(s_uart7Mutex);
            return 0;
        }
#endif
        default:
            return UART_ERROR;
    }
}

/**
 * @brief Read UART data.
 * @param uartNum UART number.
 * @param buf Pointer to buffer used to store data.
 * @param readSize Size of data to be read.
 * @return Size of data read actually.
 */
int UART_Read(E_UartNum uartNum, uint8_t *buf, uint16_t readSize)
{
    uint16_t readRealSize;

    switch (uartNum) {

#ifdef USING_UART_PORT_1
        case UART_NUM_1: {
            Osal_MutexLock(s_uart1Mutex);
            readRealSize = RingBuf_Get(&s_uart1ReadRingBuffer, buf, readSize);
            Osal_MutexUnlock(s_uart1Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_2
        case UART_NUM_2: {
            Osal_MutexLock(s_uart2Mutex);
            readRealSize = RingBuf_Get(&s_uart2ReadRingBuffer, buf, readSize);
            Osal_MutexUnlock(s_uart2Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_6
        case UART_NUM_6: {
            Osal_MutexLock(s_uart6Mutex);
            readRealSize = RingBuf_Get(&s_uart6ReadRingBuffer, buf, readSize);
            Osal_MutexUnlock(s_uart6Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_7
        case UART_NUM_7: {
            uint32_t primask;
            uart7_dma_drain();
            primask = __get_PRIMASK();
            __disable_irq();
            readRealSize = RingBuf_Get(&s_uart7ReadRingBuffer, buf, readSize);
            if (primask == 0U) {
                __enable_irq();
            }
        }
            break;
#endif

        default:
            return UART_ERROR;
    }

    return readRealSize;
}

/**
 * @brief Write UART data.
 * @param uartNum UART number.
 * @param buf Pointer to buffer used to store data.
 * @param writeSize Size of data to be write.
 * @return Size of data wrote actually.
 */
int UART_Write(E_UartNum uartNum, const uint8_t *buf, uint16_t writeSize)
{
    int writeRealLen;
    uint16_t usedCapacityOfBuffer = 0;

    switch (uartNum) {

#ifdef USING_UART_PORT_1
        case UART_NUM_1: {
            Osal_MutexLock(s_uart1Mutex);
            writeRealLen = RingBuf_Put(&s_uart1WriteRingBuffer, buf, writeSize);
            __HAL_UART_ENABLE_IT(&s_uart1Handle, UART_IT_TXE);
            usedCapacityOfBuffer = UART1_WRITE_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart1WriteRingBuffer);
            s_uart1WriteBufferState.maxUsedCapacityOfBuffer =
                usedCapacityOfBuffer > s_uart1WriteBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                       : s_uart1WriteBufferState.maxUsedCapacityOfBuffer;
            s_uart1WriteBufferState.countOfLostData += writeSize - writeRealLen;
            Osal_MutexUnlock(s_uart1Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_2
        case UART_NUM_2: {
            Osal_MutexLock(s_uart2Mutex);
            writeRealLen = RingBuf_Put(&s_uart2WriteRingBuffer, buf, writeSize);
            __HAL_UART_ENABLE_IT(&s_uart2Handle, UART_IT_TXE);
            usedCapacityOfBuffer = UART2_WRITE_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart2WriteRingBuffer);
            s_uart2WriteBufferState.maxUsedCapacityOfBuffer =
                usedCapacityOfBuffer > s_uart2WriteBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                       : s_uart2WriteBufferState.maxUsedCapacityOfBuffer;
            s_uart2WriteBufferState.countOfLostData += writeSize - writeRealLen;
            Osal_MutexUnlock(s_uart2Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_6
        case UART_NUM_6: {
            Osal_MutexLock(s_uart6Mutex);
            writeRealLen = RingBuf_Put(&s_uart6WriteRingBuffer, buf, writeSize);
            __HAL_UART_ENABLE_IT(&s_uart6Handle, UART_IT_TXE);
            usedCapacityOfBuffer = UART6_WRITE_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart6WriteRingBuffer);
            s_uart6WriteBufferState.maxUsedCapacityOfBuffer =
                usedCapacityOfBuffer > s_uart6WriteBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                       : s_uart6WriteBufferState.maxUsedCapacityOfBuffer;
            s_uart6WriteBufferState.countOfLostData += writeSize - writeRealLen;
            Osal_MutexUnlock(s_uart6Mutex);
        }
            break;
#endif

#ifdef USING_UART_PORT_7
        case UART_NUM_7: {
            Osal_MutexLock(s_uart7Mutex);
            writeRealLen = RingBuf_Put(&s_uart7WriteRingBuffer, buf, writeSize);
            __HAL_UART_ENABLE_IT(&s_uart7Handle, UART_IT_TXE);
            usedCapacityOfBuffer = UART7_WRITE_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart7WriteRingBuffer);
            s_uart7WriteBufferState.maxUsedCapacityOfBuffer =
                usedCapacityOfBuffer > s_uart7WriteBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                       : s_uart7WriteBufferState.maxUsedCapacityOfBuffer;
            s_uart7WriteBufferState.countOfLostData += writeSize - writeRealLen;
            Osal_MutexUnlock(s_uart7Mutex);
        }
            break;
#endif

        default:
            return UART_ERROR;
    }

    return writeRealLen;
}

void UART_GetBufferState(E_UartNum uartNum, T_UartBufferState *readBufferState, T_UartBufferState *writeBufferState)
{
    switch (uartNum) {
#ifdef USING_UART_PORT_1
        case UART_NUM_1:
            memcpy(readBufferState, &s_uart1ReadBufferState, sizeof(T_UartBufferState));
            memcpy(writeBufferState, &s_uart1WriteBufferState, sizeof(T_UartBufferState));
            break;
#endif
#ifdef USING_UART_PORT_2
        case UART_NUM_2:
            memcpy(readBufferState, &s_uart2ReadBufferState, sizeof(T_UartBufferState));
            memcpy(writeBufferState, &s_uart2WriteBufferState, sizeof(T_UartBufferState));
            break;
#endif
#ifdef USING_UART_PORT_6
        case UART_NUM_6:
            memcpy(readBufferState, &s_uart6ReadBufferState, sizeof(T_UartBufferState));
            memcpy(writeBufferState, &s_uart6WriteBufferState, sizeof(T_UartBufferState));
            break;
#endif
#ifdef USING_UART_PORT_7
        case UART_NUM_7:
            uart7_dma_drain();
            memcpy(readBufferState, &s_uart7ReadBufferState, sizeof(T_UartBufferState));
            memcpy(writeBufferState, &s_uart7WriteBufferState, sizeof(T_UartBufferState));
            break;
#endif
        default:
            break;
    }
}

/**
 * @brief UART1 interrupt request handler fucntion.
 */
#ifdef USING_UART_PORT_1

void USART1_IRQHandler(void)
{
    uint8_t data;
    uint16_t usedCapacityOfBuffer = 0;
    uint16_t realCountPutBuffer = 0;

    if (__HAL_UART_GET_IT_SOURCE(&s_uart1Handle, UART_IT_RXNE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart1Handle, UART_FLAG_RXNE) != RESET) {
        data = (uint8_t) ((uint16_t) (s_uart1Handle.Instance->RDR & (uint16_t) 0x01FF) & (uint16_t) 0x00FF);
        realCountPutBuffer = RingBuf_Put(&s_uart1ReadRingBuffer, &data, 1);
        usedCapacityOfBuffer = UART1_READ_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart1ReadRingBuffer);
        s_uart1ReadBufferState.maxUsedCapacityOfBuffer =
            usedCapacityOfBuffer > s_uart1ReadBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                  : s_uart1ReadBufferState.maxUsedCapacityOfBuffer;
        s_uart1ReadBufferState.countOfLostData += 1 - realCountPutBuffer;
    }

    if (__HAL_UART_GET_IT_SOURCE(&s_uart1Handle, UART_IT_TXE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart1Handle, UART_FLAG_TXE) != RESET) {
        if (RingBuf_Get(&s_uart1WriteRingBuffer, &data, 1)) {
            /* Transmit Data */
            s_uart1Handle.Instance->TDR = ((uint16_t) data & (uint16_t) 0x01FF);
        } else {
            __HAL_UART_DISABLE_IT(&s_uart1Handle, UART_IT_TXE);
        }
    }
}

#endif

/**
 * @brief UART2 interrupt request handler fucntion.
 */
#ifdef USING_UART_PORT_2

void USART2_IRQHandler(void)
{
    uint8_t data;
    uint16_t usedCapacityOfBuffer = 0;
    uint16_t realCountPutBuffer = 0;

    /* H7: ORE/FE/NE 未清时 ORE 会锁死 RDR 新数据，须先清 ICR 并读 RDR 丢脏字节 */
    if (s_uart2Handle.Instance->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        s_uart2Handle.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        (void)s_uart2Handle.Instance->RDR;
        s_uart2LineErrorClears++;
    }

    if (__HAL_UART_GET_IT_SOURCE(&s_uart2Handle, UART_IT_RXNE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart2Handle, UART_FLAG_RXNE) != RESET) {
        data = (uint8_t) ((uint16_t) (s_uart2Handle.Instance->RDR & (uint16_t) 0x01FF) & (uint16_t) 0x00FF);
        realCountPutBuffer = RingBuf_Put(&s_uart2ReadRingBuffer, &data, 1);
        usedCapacityOfBuffer = UART2_READ_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart2ReadRingBuffer);
        s_uart2ReadBufferState.maxUsedCapacityOfBuffer =
            usedCapacityOfBuffer > s_uart2ReadBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                  : s_uart2ReadBufferState.maxUsedCapacityOfBuffer;
        s_uart2ReadBufferState.countOfLostData += 1 - realCountPutBuffer;
        s_uart2RxByteCount++;
    }

    if (__HAL_UART_GET_IT_SOURCE(&s_uart2Handle, UART_IT_TXE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart2Handle, UART_FLAG_TXE) != RESET) {
        if (RingBuf_Get(&s_uart2WriteRingBuffer, &data, 1)) {
            /* Transmit Data */
            s_uart2Handle.Instance->TDR = ((uint16_t) data & (uint16_t) 0x01FF);
        } else {
            __HAL_UART_DISABLE_IT(&s_uart2Handle, UART_IT_TXE);
        }
    }
}

#endif

/**
 * @brief UART6 interrupt request handler function.
 */
#ifdef USING_UART_PORT_6

void USART6_IRQHandler(void)
{
    uint8_t data;
    uint16_t usedCapacityOfBuffer = 0;
    uint16_t realCountPutBuffer = 0;

    if (s_uart6Handle.Instance->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        s_uart6Handle.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        (void)s_uart6Handle.Instance->RDR;
        s_uart6LineErrorClears++;
    }

    if (__HAL_UART_GET_IT_SOURCE(&s_uart6Handle, UART_IT_RXNE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart6Handle, UART_FLAG_RXNE) != RESET) {
        data = (uint8_t) ((uint16_t) (s_uart6Handle.Instance->RDR & (uint16_t) 0x01FF) & (uint16_t) 0x00FF);
        realCountPutBuffer = RingBuf_Put(&s_uart6ReadRingBuffer, &data, 1);
        usedCapacityOfBuffer = UART6_READ_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart6ReadRingBuffer);
        s_uart6ReadBufferState.maxUsedCapacityOfBuffer =
            usedCapacityOfBuffer > s_uart6ReadBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                  : s_uart6ReadBufferState.maxUsedCapacityOfBuffer;
        s_uart6ReadBufferState.countOfLostData += 1 - realCountPutBuffer;
        s_uart6RxByteCount++;
    }

    if (__HAL_UART_GET_IT_SOURCE(&s_uart6Handle, UART_IT_TXE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart6Handle, UART_FLAG_TXE) != RESET) {
        if (RingBuf_Get(&s_uart6WriteRingBuffer, &data, 1)) {
            s_uart6Handle.Instance->TDR = ((uint16_t) data & (uint16_t) 0x01FF);
        } else {
            __HAL_UART_DISABLE_IT(&s_uart6Handle, UART_IT_TXE);
        }
    }
}

#endif

/**
 * @brief UART7 interrupt request handler (UART_NUM_7 / Air780E 4G).
 */
#ifdef USING_UART_PORT_7

void UART7_IRQHandler(void)
{
    uint8_t data;

    /* H7: 与 USART2 一致，先清 ORE/FE/NE，避免 RDR 锁死 */
    if (s_uart7Handle.Instance->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
        s_uart7Handle.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
        (void)s_uart7Handle.Instance->RDR;
        s_uart7LineErrorClears++;
    }

    if (__HAL_UART_GET_IT_SOURCE(&s_uart7Handle, UART_IT_IDLE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart7Handle, UART_FLAG_IDLE) != RESET) {
        __HAL_UART_CLEAR_IDLEFLAG(&s_uart7Handle);
        s_uart7IdleIrqCount++;
        uart7_dma_drain_locked();
    }

    if (__HAL_UART_GET_IT_SOURCE(&s_uart7Handle, UART_IT_TXE) != RESET &&
        __HAL_UART_GET_FLAG(&s_uart7Handle, UART_FLAG_TXE) != RESET) {
        if (RingBuf_Get(&s_uart7WriteRingBuffer, &data, 1)) {
            /* Transmit Data */
            s_uart7Handle.Instance->TDR = ((uint16_t) data & (uint16_t) 0x01FF);
        } else {
            __HAL_UART_DISABLE_IT(&s_uart7Handle, UART_IT_TXE);
        }
    }
}

#endif

uint32_t UART_GetUart7LineErrorClears(void)
{
#ifdef USING_UART_PORT_7
    return s_uart7LineErrorClears;
#else
    return 0U;
#endif
}

void UART_GetUart7DmaPerfStats(T_UartDmaPerfStats *stats)
{
#ifdef USING_UART_PORT_7
    if (stats == NULL) {
        return;
    }

    uart7_dma_drain();
    stats->rxDmaActive = s_uart7RxDmaActive;
    stats->dmaIrqCount = s_uart7DmaIrqCount;
    stats->idleIrqCount = s_uart7IdleIrqCount;
    stats->drainCount = s_uart7DmaDrainCount;
    stats->drainBytes = s_uart7DmaDrainBytes;
    stats->maxDrainBytes = s_uart7DmaMaxDrainBytes;
    stats->dmaErrorCount = s_uart7DmaErrorCount;
#else
    (void)stats;
#endif
}

void DMA1_Stream0_IRQHandler(void)
{
#ifdef USING_UART_PORT_7
    s_uart7DmaIrqCount++;
    HAL_DMA_IRQHandler(&s_uart7RxDmaHandle);
    if (HAL_DMA_GetError(&s_uart7RxDmaHandle) != HAL_DMA_ERROR_NONE) {
        s_uart7DmaErrorCount++;
    }
    uart7_dma_drain_locked();
#endif
}

uint32_t UART_GetUart2RxByteCount(void)
{
#ifdef USING_UART_PORT_2
    return s_uart2RxByteCount;
#else
    return 0U;
#endif
}

uint32_t UART_GetUart2LineErrorClears(void)
{
#ifdef USING_UART_PORT_2
    return s_uart2LineErrorClears;
#else
    return 0U;
#endif
}

uint32_t UART_GetUart6RxByteCount(void)
{
#ifdef USING_UART_PORT_6
    return s_uart6RxByteCount;
#else
    return 0U;
#endif
}

uint32_t UART_GetUart6LineErrorClears(void)
{
#ifdef USING_UART_PORT_6
    return s_uart6LineErrorClears;
#else
    return 0U;
#endif
}

void UART_PollUart6Rx(void)
{
#ifdef USING_UART_PORT_6
    uint8_t data;
    uint16_t usedCapacityOfBuffer;
    uint16_t realCountPutBuffer;

    while ((s_uart6Handle.Instance->ISR & (USART_ISR_RXNE_RXFNE | USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0U) {
        if (s_uart6Handle.Instance->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) {
            s_uart6Handle.Instance->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
            (void)s_uart6Handle.Instance->RDR;
            s_uart6LineErrorClears++;
            continue;
        }

        data = (uint8_t)((uint16_t)(s_uart6Handle.Instance->RDR & (uint16_t)0x01FF) & (uint16_t)0x00FF);
        Osal_MutexLock(s_uart6Mutex);
        realCountPutBuffer = RingBuf_Put(&s_uart6ReadRingBuffer, &data, 1);
        usedCapacityOfBuffer = UART6_READ_BUF_SIZE - RingBuf_GetUnusedSize(&s_uart6ReadRingBuffer);
        s_uart6ReadBufferState.maxUsedCapacityOfBuffer =
            usedCapacityOfBuffer > s_uart6ReadBufferState.maxUsedCapacityOfBuffer ? usedCapacityOfBuffer
                                                                                  : s_uart6ReadBufferState.maxUsedCapacityOfBuffer;
        s_uart6ReadBufferState.countOfLostData += 1 - realCountPutBuffer;
        Osal_MutexUnlock(s_uart6Mutex);
        s_uart6RxByteCount++;
    }
#endif
}

#ifdef __CC_ARM
int fputc(int ch,FILE *f)
{
    if (DJI_CONSOLE_UART_NUM == UART_NUM_1) {
        HAL_UART_Transmit(&s_uart1Handle, (uint8_t *) &ch, 1, 0xFFFF);
    } else if (DJI_CONSOLE_UART_NUM == UART_NUM_2) {
        HAL_UART_Transmit(&s_uart2Handle, (uint8_t *) &ch, 1, 0xFFFF);
    } else if (DJI_CONSOLE_UART_NUM == UART_NUM_7) {
        HAL_UART_Transmit(&s_uart7Handle, (uint8_t *) &ch, 1, 0xFFFF);
    }

    return ch;
}
#else
#ifdef __GNUC__
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif

PUTCHAR_PROTOTYPE
{
    if (DJI_CONSOLE_UART_NUM == UART_NUM_1) {
        HAL_UART_Transmit(&s_uart1Handle, (uint8_t *) &ch, 1, 0xFFFF);
    } else if (DJI_CONSOLE_UART_NUM == UART_NUM_2) {
        HAL_UART_Transmit(&s_uart2Handle, (uint8_t *) &ch, 1, 0xFFFF);
    } else if (DJI_CONSOLE_UART_NUM == UART_NUM_7) {
        HAL_UART_Transmit(&s_uart7Handle, (uint8_t *) &ch, 1, 0xFFFF);
    }

    return ch;
}

#endif
