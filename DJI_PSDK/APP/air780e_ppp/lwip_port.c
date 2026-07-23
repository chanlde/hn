#include "lwip/sys.h"

#include "cmsis_os.h"

u32_t sys_now(void)
{
    return (u32_t)osKernelGetTickCount();
}

u32_t sys_jiffies(void)
{
    return (u32_t)osKernelGetTickCount();
}
