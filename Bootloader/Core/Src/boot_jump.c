#include "boot_jump.h"
#include "boot_partition.h"

#include "stm32h743xx.h"

typedef void (*BootAppEntry)(void);

static int is_stack_pointer_valid(uint32_t sp)
{
    if (sp >= BOOT_SRAM_BASE && sp < BOOT_SRAM_END)
        return 1;
    if (sp >= BOOT_AXI_SRAM_BASE && sp < BOOT_AXI_SRAM_END)
        return 1;
    return 0;
}

int BootJump_IsAppVectorValid(uint32_t appBase)
{
    uint32_t sp = *(const uint32_t *)appBase;
    uint32_t reset = *(const uint32_t *)(appBase + 4UL);
    uint32_t resetAddr = reset & ~1UL;
    uint32_t appEnd = (appBase == APP_SLOT_B_BASE) ? APP_SLOT_B_END : APP_SLOT_A_END;

    if (!is_stack_pointer_valid(sp))
        return 0;
    if (resetAddr < appBase || resetAddr > appEnd)
        return 0;
    if ((reset & 0x1UL) == 0UL)
        return 0;
    return 1;
}

void BootJump_JumpToApp(uint32_t appBase)
{
    uint32_t appStack = *(const uint32_t *)appBase;
    uint32_t appReset = *(const uint32_t *)(appBase + 4UL);
    BootAppEntry appEntry = (BootAppEntry)appReset;

    __disable_irq();

    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;

    for (uint32_t i = 0; i < 8UL; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFUL;
        NVIC->ICPR[i] = 0xFFFFFFFFUL;
    }

    SCB->VTOR = appBase;
    __DSB();
    __ISB();

    __set_MSP(appStack);
    appEntry();
}
