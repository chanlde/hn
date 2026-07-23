#include "boot_jump.h"
#include "boot_log.h"
#include "boot_ota_state.h"
#include "boot_partition.h"

#include "stm32h743xx.h"

int main(void)
{
    T_OtaInfo info;
    uint32_t bootBase = APP_SLOT_A_BASE;

    BootLog_Init();
    BootLog_Line("");
    BootLog_Line("[BOOT] start");

    if (BootOtaState_Load(&info) == 0) {
        BootLog_Write("[BOOT] state=");
        BootLog_WriteDec(info.otaState);
        BootLog_Write(" current=");
        BootLog_WriteDec(info.currentSlot);
        BootLog_Write(" target=");
        BootLog_WriteDec(info.targetSlot);
        BootLog_Write(" size=");
        BootLog_WriteDec(info.firmwareSize);
        BootLog_Line("");
        (void)BootOtaState_PrepareBoot(&info, &bootBase);
    } else {
        BootLog_Line("[BOOT] no valid ota state");
    }

    if (bootBase != 0UL && BootJump_IsAppVectorValid(bootBase)) {
        BootLog_Write("[BOOT] jump ");
        BootLog_WriteHex32(bootBase);
        BootLog_Line("");
        BootLog_Flush();
        BootJump_JumpToApp(bootBase);
    }

    if (bootBase != 0UL && bootBase != APP_SLOT_A_BASE && BootJump_IsAppVectorValid(APP_SLOT_A_BASE)) {
        BootLog_Line("[BOOT] fallback jump A");
        BootLog_Flush();
        BootJump_JumpToApp(APP_SLOT_A_BASE);
    }

    BootLog_Line("[BOOT] no bootable app");
    BootLog_Flush();
    while (1) {
        __WFI();
    }
}
