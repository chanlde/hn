#ifndef SOLARCLEAN_OTA_STATE_H
#define SOLARCLEAN_OTA_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SOLARCLEAN_OTA_INFO_MAGIC           0x53434F54UL
#define SOLARCLEAN_OTA_INFO_STRUCT_VERSION  1UL
#define SOLARCLEAN_OTA_BOOT_MAX_TRY         3UL

typedef enum {
    SOLARCLEAN_OTA_SLOT_A = 0,
    SOLARCLEAN_OTA_SLOT_B = 1,
} E_SolarCleanOtaSlot;

typedef enum {
    SOLARCLEAN_BOOT_OTA_STATE_IDLE = 0,
    SOLARCLEAN_BOOT_OTA_STATE_DOWNLOADING = 1,
    SOLARCLEAN_BOOT_OTA_STATE_DOWNLOADED = 2,
    SOLARCLEAN_BOOT_OTA_STATE_VERIFY_FAILED = 3,
    SOLARCLEAN_BOOT_OTA_STATE_PENDING_VERIFY = 4,
    SOLARCLEAN_BOOT_OTA_STATE_VALID = 5,
    SOLARCLEAN_BOOT_OTA_STATE_ROLLBACK = 6,
    SOLARCLEAN_BOOT_OTA_STATE_FAILED = 7,
} E_SolarCleanBootOtaState;

typedef enum {
    SOLARCLEAN_BOOT_OTA_RESULT_NONE = 0,
    SOLARCLEAN_BOOT_OTA_RESULT_SUCCESS = 1,
    SOLARCLEAN_BOOT_OTA_RESULT_FAILED = 2,
} E_SolarCleanBootOtaResult;

typedef enum {
    SOLARCLEAN_BOOT_OTA_FAIL_NONE = 0,
    SOLARCLEAN_BOOT_OTA_FAIL_IMAGE_HASH_ERROR = 1,
    SOLARCLEAN_BOOT_OTA_FAIL_IMAGE_SIZE_ERROR = 2,
    SOLARCLEAN_BOOT_OTA_FAIL_NEW_FW_BOOT_FAILED = 3,
    SOLARCLEAN_BOOT_OTA_FAIL_STATE_CRC_ERROR = 4,
    SOLARCLEAN_BOOT_OTA_FAIL_NO_VALID_APP = 5,
} E_SolarCleanBootOtaFailReason;

typedef struct {
    uint32_t magic;
    uint32_t structVersion;

    uint8_t currentSlot;
    uint8_t targetSlot;
    uint8_t previousSlot;
    uint8_t otaState;

    uint32_t currentVersion;
    uint32_t targetVersion;

    uint32_t bootTryCount;
    uint32_t maxBootTryCount;

    uint32_t lastOtaResult;
    uint32_t lastFailReason;

    uint32_t firmwareSize;
    uint8_t firmwareSha256[32];

    uint32_t crc32;
} T_SolarCleanOtaInfo;

uint32_t SolarCleanOtaState_CalcCrc32(const void *data, uint32_t len);
int SolarCleanOtaState_Load(T_SolarCleanOtaInfo *out);
int SolarCleanOtaState_Save(T_SolarCleanOtaInfo *info);
void SolarCleanOtaState_BuildDefault(T_SolarCleanOtaInfo *info);
void SolarCleanOtaState_BuildPendingVerify(T_SolarCleanOtaInfo *info,
                                           E_SolarCleanOtaSlot currentSlot,
                                           E_SolarCleanOtaSlot targetSlot,
                                           uint32_t targetVersion,
                                           uint32_t firmwareSize,
                                           const uint8_t firmwareSha256[32]);
void SolarCleanOtaState_MarkValid(T_SolarCleanOtaInfo *info, E_SolarCleanOtaSlot confirmedSlot);

#ifdef __cplusplus
}
#endif

#endif /* SOLARCLEAN_OTA_STATE_H */
