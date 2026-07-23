#include "stm32h743xx.h"

#if !defined(HSE_VALUE)
#define HSE_VALUE 25000000UL
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE 64000000UL
#endif

uint32_t SystemCoreClock = HSI_VALUE;
uint32_t SystemD2Clock = HSI_VALUE;
const uint8_t D1CorePrescTable[16] = {0, 0, 0, 0, 1, 2, 3, 4, 1, 2, 3, 4, 6, 7, 8, 9};

void SystemInit(void)
{
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << (10U * 2U)) | (3UL << (11U * 2U)));
#endif

    SCB->VTOR = 0x08000000UL;
    __DSB();
    __ISB();
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = HSI_VALUE;
    SystemD2Clock = HSI_VALUE;
}
