#ifndef __CACHE_UTILS_H
#define __CACHE_UTILS_H

#include "stm32h7xx.h"

/**
 * @file cache_utils.h
 * @brief Cache维护工具函数 - 专用于DMA传输
 * 
 * PSDK Cache Rules:
 * 1. All DMA buffers must be cache-maintained or placed in DTCM.
 * 2. AXI SRAM buffers require explicit Clean/Invalidate.
 * 3. Never access DMA buffers without cache maintenance.
 * 4. DTCM (0x20000000-0x2001FFFF) buffers do NOT need cache maintenance.
 * 
 * Usage:
 * - TX (CPU → DMA): Cache_CleanDCache_by_Addr() before DMA start
 * - RX (DMA → CPU): Cache_InvalidateDCache_by_Addr() after DMA complete
 * - Bidirectional: Cache_CleanInvalidateDCache_by_Addr()
 */

/**
 * @brief 检查buffer是否完全在DTCM区域（不需要Cache维护）
 * @param addr: 缓冲区起始地址
 * @param size: 缓冲区大小（字节）
 * @return 1: 完全在DTCM，0: 不在DTCM或跨区
 */
static inline int Cache_IsFullyInDTCM(void *addr, uint32_t size)
{
    uint32_t start = (uint32_t)addr;
    uint32_t end   = start + size;
    return (start >= 0x20000000UL) && (end <= 0x20020000UL);
}

/**
 * @brief 清理D-Cache（DMA发送前调用）
 * @note 用于CPU写入数据后，DMA读取前的场景
 * @param addr: 缓冲区地址（任意对齐）
 * @param size: 缓冲区大小（字节）
 * @warning 函数内部会处理32字节对齐，但建议buffer本身32字节对齐以获得最佳性能
 * 
 * @example
 *   uint8_t tx_buf[256];
 *   // ... 填充数据 ...
 *   Cache_CleanDCache_by_Addr(tx_buf, sizeof(tx_buf));
 *   HAL_UART_Transmit_DMA(&huart1, tx_buf, sizeof(tx_buf));
 */
void Cache_CleanDCache_by_Addr(void *addr, uint32_t size);

/**
 * @brief 使D-Cache失效（DMA接收后调用）
 * @note 用于DMA写入数据后，CPU读取前的场景
 * @param addr: 缓冲区地址（任意对齐）
 * @param size: 缓冲区大小（字节）
 * 
 * @example
 *   uint8_t rx_buf[256];
 *   HAL_UART_Receive_DMA(&huart1, rx_buf, sizeof(rx_buf));
 *   // ... 等待接收完成 ...
 *   Cache_InvalidateDCache_by_Addr(rx_buf, sizeof(rx_buf));
 *   // ... 使用数据 ...
 */
void Cache_InvalidateDCache_by_Addr(void *addr, uint32_t size);

/**
 * @brief 清理并使D-Cache失效（双向DMA传输）
 * @note 用于双向DMA传输场景
 * @param addr: 缓冲区地址（任意对齐）
 * @param size: 缓冲区大小（字节）
 */
void Cache_CleanInvalidateDCache_by_Addr(void *addr, uint32_t size);

#endif /* __CACHE_UTILS_H */
