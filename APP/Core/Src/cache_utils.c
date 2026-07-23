#include "cache_utils.h"

#define CACHE_LINE_SIZE 32U

/**
 * @brief 地址向下对齐到Cache line边界
 */
static inline uint32_t cache_align_down(uint32_t addr)
{
    return addr & ~(CACHE_LINE_SIZE - 1U);
}

/**
 * @brief 大小向上对齐到Cache line边界
 */
static inline uint32_t cache_align_up(uint32_t size)
{
    return (size + CACHE_LINE_SIZE - 1U) & ~(CACHE_LINE_SIZE - 1U);
}

void Cache_CleanDCache_by_Addr(void *addr, uint32_t size)
{
    // DTCM区域不需要Cache维护
    if (Cache_IsFullyInDTCM(addr, size))
    {
        return;
    }
    
    if (size == 0)
    {
        return;
    }
    
    // 计算对齐后的起始地址和长度
    uint32_t start_addr = cache_align_down((uint32_t)addr);
    uint32_t offset = (uint32_t)addr - start_addr;
    uint32_t aligned_size = cache_align_up(size + offset);
    
    // 调用CMSIS函数 - 必须传入对齐后的地址
    SCB_CleanDCache_by_Addr((uint32_t *)start_addr, aligned_size);
}

void Cache_InvalidateDCache_by_Addr(void *addr, uint32_t size)
{
    // DTCM区域不需要Cache维护
    if (Cache_IsFullyInDTCM(addr, size))
    {
        return;
    }
    
    if (size == 0)
    {
        return;
    }
    
    // 计算对齐后的起始地址和长度
    uint32_t start_addr = cache_align_down((uint32_t)addr);
    uint32_t offset = (uint32_t)addr - start_addr;
    uint32_t aligned_size = cache_align_up(size + offset);
    
    // 🔥 修正：必须传入对齐后的地址，不是原始addr
    SCB_InvalidateDCache_by_Addr((void *)start_addr, aligned_size);
}

void Cache_CleanInvalidateDCache_by_Addr(void *addr, uint32_t size)
{
    // DTCM区域不需要Cache维护
    if (Cache_IsFullyInDTCM(addr, size))
    {
        return;
    }
    
    if (size == 0)
    {
        return;
    }
    
    // 计算对齐后的起始地址和长度
    uint32_t start_addr = cache_align_down((uint32_t)addr);
    uint32_t offset = (uint32_t)addr - start_addr;
    uint32_t aligned_size = cache_align_up(size + offset);
    
    // 调用CMSIS函数 - 必须传入对齐后的地址
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)start_addr, aligned_size);
}
