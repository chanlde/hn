#include "boot_ota_state.h"
#include "boot_flash.h"
#include "boot_log.h"
#include "boot_partition.h"
#include "boot_sha256.h"

#include <stddef.h>
#include <string.h>

static uint32_t crc32_update(uint32_t crc, uint8_t data)
{
    crc ^= data;
    for (uint32_t i = 0; i < 8UL; i++) {
        if ((crc & 1UL) != 0UL)
            crc = (crc >> 1) ^ 0xEDB88320UL;
        else
            crc >>= 1;
    }
    return crc;
}

uint32_t BootOtaState_CalcCrc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    if (data == NULL)
        return 0UL;
    for (uint32_t i = 0; i < len; i++)
        crc = crc32_update(crc, p[i]);
    return crc ^ 0xFFFFFFFFUL;
}

int BootOtaState_Load(T_OtaInfo *out)
{
    const T_OtaInfo *stored = (const T_OtaInfo *)OTA_STATE_BASE;
    uint32_t calc;

    if (out == NULL)
        return -1;
    memcpy(out, stored, sizeof(*out));
    if (out->magic != OTA_INFO_MAGIC || out->structVersion != OTA_INFO_STRUCT_VERSION)
        return -1;

    calc = BootOtaState_CalcCrc32(out, (uint32_t)offsetof(T_OtaInfo, crc32));
    return (calc == out->crc32) ? 0 : -1;
}

int BootOtaState_Save(T_OtaInfo *info)
{
    if (info == NULL)
        return -1;

    info->magic = OTA_INFO_MAGIC;
    info->structVersion = OTA_INFO_STRUCT_VERSION;
    info->crc32 = BootOtaState_CalcCrc32(info, (uint32_t)offsetof(T_OtaInfo, crc32));

    if (BootFlash_Erase(OTA_STATE_BASE, OTA_STATE_END) != 0)
        return -1;
    return BootFlash_Write(OTA_STATE_BASE, (const uint8_t *)info, (uint32_t)sizeof(*info));
}

static int boot_stack_pointer_valid(uint32_t sp)
{
    if (sp >= BOOT_SRAM_BASE && sp < BOOT_SRAM_END)
        return 1;
    if (sp >= BOOT_AXI_SRAM_BASE && sp < BOOT_AXI_SRAM_END)
        return 1;
    return 0;
}

static int boot_staged_image_vector_valid(void)
{
    uint32_t sp = *(const uint32_t *)APP_SLOT_B_BASE;
    uint32_t reset = *(const uint32_t *)(APP_SLOT_B_BASE + 4UL);
    uint32_t resetAddr = reset & ~1UL;

    if (!boot_stack_pointer_valid(sp))
        return 0;
    if ((reset & 1UL) == 0UL)
        return 0;
    return (resetAddr >= APP_SLOT_A_BASE && resetAddr <= APP_SLOT_A_END) ? 1 : 0;
}

static void boot_mark_install_failed(T_OtaInfo *info, uint32_t reason)
{
    if (info == NULL)
        return;

    info->currentSlot = OTA_SLOT_A;
    info->targetSlot = OTA_SLOT_B;
    info->previousSlot = OTA_SLOT_A;
    info->otaState = OTA_STATE_FAILED;
    info->bootTryCount = 0UL;
    info->lastOtaResult = OTA_RESULT_FAILED;
    info->lastFailReason = reason;
}

static void boot_mark_install_valid(T_OtaInfo *info)
{
    if (info == NULL)
        return;

    info->currentSlot = OTA_SLOT_A;
    info->targetSlot = OTA_SLOT_A;
    info->previousSlot = OTA_SLOT_A;
    info->otaState = OTA_STATE_VALID;
    info->currentVersion = info->targetVersion;
    info->bootTryCount = 0UL;
    info->lastOtaResult = OTA_RESULT_SUCCESS;
    info->lastFailReason = OTA_FAIL_NONE;
}

static int boot_install_staged_image(T_OtaInfo *info, uint32_t *bootBase)
{
    uint32_t maxTry;
    int saveResult;

    if (info == NULL || bootBase == NULL)
        return -1;

    *bootBase = APP_SLOT_A_BASE;
    BootLog_Write("[BOOT] install pending size=");
    BootLog_WriteDec(info->firmwareSize);
    BootLog_Write(" targetVer=");
    BootLog_WriteDec(info->targetVersion);
    BootLog_Line("");

    if (info->firmwareSize == 0UL ||
        info->firmwareSize > APP_SLOT_A_SIZE ||
        info->firmwareSize > APP_SLOT_B_SIZE) {
        BootLog_Line("[BOOT] install fail: bad size");
        boot_mark_install_failed(info, OTA_FAIL_IMAGE_SIZE_ERROR);
        (void)BootOtaState_Save(info);
        return -1;
    }

    if (!boot_staged_image_vector_valid()) {
        BootLog_Write("[BOOT] install fail: bad staged vector sp=");
        BootLog_WriteHex32(*(const uint32_t *)APP_SLOT_B_BASE);
        BootLog_Write(" reset=");
        BootLog_WriteHex32(*(const uint32_t *)(APP_SLOT_B_BASE + 4UL));
        BootLog_Line("");
        boot_mark_install_failed(info, OTA_FAIL_IMAGE_SIZE_ERROR);
        (void)BootOtaState_Save(info);
        return -1;
    }

    BootLog_Line("[BOOT] verify B");
    if (BootSha256_VerifyMemory(APP_SLOT_B_BASE, info->firmwareSize, info->firmwareSha256) != 0) {
        BootLog_Line("[BOOT] install fail: B sha256");
        boot_mark_install_failed(info, OTA_FAIL_IMAGE_HASH_ERROR);
        (void)BootOtaState_Save(info);
        return -1;
    }
    BootLog_Line("[BOOT] verify B OK");

    maxTry = (info->maxBootTryCount == 0UL) ? OTA_BOOT_MAX_TRY_DEFAULT : info->maxBootTryCount;
    if (info->bootTryCount >= maxTry) {
        BootLog_Line("[BOOT] install fail: max try");
        boot_mark_install_failed(info, OTA_FAIL_NEW_FW_BOOT_FAILED);
        (void)BootOtaState_Save(info);
        return -1;
    }

    info->bootTryCount++;
    saveResult = BootOtaState_Save(info);
    BootLog_Write("[BOOT] save try state rc=");
    BootLog_WriteDec((uint32_t)(saveResult == 0 ? 0 : 1));
    BootLog_Line("");

    BootLog_Line("[BOOT] erase A");
    if (BootFlash_Erase(APP_SLOT_A_BASE, APP_SLOT_A_END) != 0) {
        BootLog_Line("[BOOT] install fail: erase A");
        *bootBase = 0UL;
        return -1;
    }
    BootLog_Line("[BOOT] erase A OK");

    BootLog_Line("[BOOT] copy B to A");
    if (BootFlash_Write(APP_SLOT_A_BASE, (const uint8_t *)APP_SLOT_B_BASE, info->firmwareSize) != 0) {
        BootLog_Line("[BOOT] install fail: copy B to A");
        *bootBase = 0UL;
        return -1;
    }
    BootLog_Write("[BOOT] copy B to A OK w0=");
    BootLog_WriteHex32(*(const uint32_t *)APP_SLOT_A_BASE);
    BootLog_Write(" w1=");
    BootLog_WriteHex32(*(const uint32_t *)(APP_SLOT_A_BASE + 4UL));
    BootLog_Line("");

    BootLog_Line("[BOOT] verify A");
    if (BootSha256_VerifyMemory(APP_SLOT_A_BASE, info->firmwareSize, info->firmwareSha256) != 0) {
        BootLog_Line("[BOOT] install fail: A sha256");
        *bootBase = 0UL;
        return -1;
    }
    BootLog_Line("[BOOT] verify A OK");

    boot_mark_install_valid(info);
    saveResult = BootOtaState_Save(info);
    BootLog_Write("[BOOT] mark valid rc=");
    BootLog_WriteDec((uint32_t)(saveResult == 0 ? 0 : 1));
    BootLog_Line("");
    *bootBase = APP_SLOT_A_BASE;
    return 0;
}

uint32_t BootOtaState_SlotToBase(uint8_t slot)
{
    return (slot == OTA_SLOT_B) ? APP_SLOT_B_BASE : APP_SLOT_A_BASE;
}

uint32_t BootOtaState_SelectBootBase(const T_OtaInfo *info)
{
    (void)info;
    return APP_SLOT_A_BASE;
}

int BootOtaState_PrepareBoot(T_OtaInfo *info, uint32_t *bootBase)
{
    if (bootBase == NULL)
        return -1;
    *bootBase = APP_SLOT_A_BASE;
    if (info == NULL)
        return -1;

    if (info->otaState == OTA_STATE_PENDING_VERIFY)
        return boot_install_staged_image(info, bootBase);

    BootLog_Line("[BOOT] no pending install");
    *bootBase = APP_SLOT_A_BASE;
    return 0;
}
