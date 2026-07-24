#include "solarclean_ota_state.h"

#include "flash_if.h"
#include "ds800_protocol.h"

#include <stddef.h>
#include <string.h>

#define SOLARCLEAN_OTA_PRESERVE_DS800_PARAM_BYTES 256U

static uint32_t ota_state_crc32_update(uint32_t crc, uint8_t data)
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

uint32_t SolarCleanOtaState_CalcCrc32(const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;

    if (data == NULL)
        return 0UL;

    for (uint32_t i = 0; i < len; i++)
        crc = ota_state_crc32_update(crc, p[i]);

    return crc ^ 0xFFFFFFFFUL;
}

void SolarCleanOtaState_BuildDefault(T_SolarCleanOtaInfo *info)
{
    if (info == NULL)
        return;

    memset(info, 0, sizeof(*info));
    info->magic = SOLARCLEAN_OTA_INFO_MAGIC;
    info->structVersion = SOLARCLEAN_OTA_INFO_STRUCT_VERSION;
    info->currentSlot = SOLARCLEAN_OTA_SLOT_A;
    info->targetSlot = SOLARCLEAN_OTA_SLOT_A;
    info->previousSlot = SOLARCLEAN_OTA_SLOT_A;
    info->otaState = SOLARCLEAN_BOOT_OTA_STATE_VALID;
    info->maxBootTryCount = SOLARCLEAN_OTA_BOOT_MAX_TRY;
    info->lastOtaResult = SOLARCLEAN_BOOT_OTA_RESULT_NONE;
    info->lastFailReason = SOLARCLEAN_BOOT_OTA_FAIL_NONE;
    info->crc32 = SolarCleanOtaState_CalcCrc32(info, (uint32_t)offsetof(T_SolarCleanOtaInfo, crc32));
}

void SolarCleanOtaState_BuildPendingVerify(T_SolarCleanOtaInfo *info,
                                           E_SolarCleanOtaSlot currentSlot,
                                           E_SolarCleanOtaSlot targetSlot,
                                           uint32_t targetVersion,
                                           uint32_t firmwareSize,
                                           const uint8_t firmwareSha256[32])
{
    if (info == NULL)
        return;

    memset(info, 0, sizeof(*info));
    info->magic = SOLARCLEAN_OTA_INFO_MAGIC;
    info->structVersion = SOLARCLEAN_OTA_INFO_STRUCT_VERSION;
    info->currentSlot = (uint8_t)currentSlot;
    info->targetSlot = (uint8_t)targetSlot;
    info->previousSlot = (uint8_t)currentSlot;
    info->otaState = SOLARCLEAN_BOOT_OTA_STATE_PENDING_VERIFY;
    info->targetVersion = targetVersion;
    info->bootTryCount = 0U;
    info->maxBootTryCount = SOLARCLEAN_OTA_BOOT_MAX_TRY;
    info->lastOtaResult = SOLARCLEAN_BOOT_OTA_RESULT_NONE;
    info->lastFailReason = SOLARCLEAN_BOOT_OTA_FAIL_NONE;
    info->firmwareSize = firmwareSize;
    if (firmwareSha256 != NULL)
        memcpy(info->firmwareSha256, firmwareSha256, sizeof(info->firmwareSha256));
    info->crc32 = SolarCleanOtaState_CalcCrc32(info, (uint32_t)offsetof(T_SolarCleanOtaInfo, crc32));
}

void SolarCleanOtaState_MarkValid(T_SolarCleanOtaInfo *info, E_SolarCleanOtaSlot confirmedSlot)
{
    if (info == NULL)
        return;

    info->currentSlot = (uint8_t)confirmedSlot;
    info->targetSlot = (uint8_t)confirmedSlot;
    info->previousSlot = (uint8_t)confirmedSlot;
    info->otaState = SOLARCLEAN_BOOT_OTA_STATE_VALID;
    info->bootTryCount = 0U;
    info->maxBootTryCount = SOLARCLEAN_OTA_BOOT_MAX_TRY;
    info->lastOtaResult = SOLARCLEAN_BOOT_OTA_RESULT_SUCCESS;
    info->lastFailReason = SOLARCLEAN_BOOT_OTA_FAIL_NONE;
    info->crc32 = SolarCleanOtaState_CalcCrc32(info, (uint32_t)offsetof(T_SolarCleanOtaInfo, crc32));
}

int SolarCleanOtaState_Load(T_SolarCleanOtaInfo *out)
{
    const T_SolarCleanOtaInfo *stored = (const T_SolarCleanOtaInfo *)APPLICATION_PARAM_STORE_ADDRESS;
    uint32_t calc;

    if (out == NULL)
        return -1;

    memcpy(out, stored, sizeof(*out));
    if (out->magic != SOLARCLEAN_OTA_INFO_MAGIC ||
        out->structVersion != SOLARCLEAN_OTA_INFO_STRUCT_VERSION)
        return -1;

    calc = SolarCleanOtaState_CalcCrc32(out, (uint32_t)offsetof(T_SolarCleanOtaInfo, crc32));
    return (calc == out->crc32) ? 0 : -1;
}

int SolarCleanOtaState_Save(T_SolarCleanOtaInfo *info)
{
    uint32_t result;
    uint8_t ds800Param[SOLARCLEAN_OTA_PRESERVE_DS800_PARAM_BYTES];

    if (info == NULL)
        return -1;

    memcpy(ds800Param,
           (const void *)(APPLICATION_PARAM_STORE_ADDRESS + DS800_PARAM_STORE_OFFSET),
           sizeof(ds800Param));

    info->magic = SOLARCLEAN_OTA_INFO_MAGIC;
    info->structVersion = SOLARCLEAN_OTA_INFO_STRUCT_VERSION;
    info->crc32 = SolarCleanOtaState_CalcCrc32(info, (uint32_t)offsetof(T_SolarCleanOtaInfo, crc32));

    result = FLASH_If_Erase(APPLICATION_PARAM_STORE_ADDRESS, APPLICATION_PARAM_STORE_ADDRESS_END);
    if (result != FLASHIF_OK)
        return -1;

    result = FLASH_If_Write(APPLICATION_PARAM_STORE_ADDRESS, (const uint8_t *)info, (uint32_t)sizeof(*info));
    if (result != FLASHIF_OK)
        return -1;

    result = FLASH_If_Write(APPLICATION_PARAM_STORE_ADDRESS + DS800_PARAM_STORE_OFFSET,
                            ds800Param,
                            (uint32_t)sizeof(ds800Param));
    return (result == FLASHIF_OK) ? 0 : -1;
}
