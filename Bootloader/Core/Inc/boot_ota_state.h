#ifndef BOOT_OTA_STATE_H
#define BOOT_OTA_STATE_H

#include <stdint.h>

#define OTA_INFO_MAGIC              0x53434F54UL /* "TOCS" little-endian */
#define OTA_INFO_STRUCT_VERSION     1UL
#define OTA_BOOT_MAX_TRY_DEFAULT    3UL

typedef enum {
    OTA_SLOT_A = 0,
    OTA_SLOT_B = 1,
} E_OtaSlot;

typedef enum {
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING = 1,
    OTA_STATE_DOWNLOADED = 2,
    OTA_STATE_VERIFY_FAILED = 3,
    OTA_STATE_PENDING_VERIFY = 4,
    OTA_STATE_VALID = 5,
    OTA_STATE_ROLLBACK = 6,
    OTA_STATE_FAILED = 7,
} E_OtaState;

typedef enum {
    OTA_RESULT_NONE = 0,
    OTA_RESULT_SUCCESS = 1,
    OTA_RESULT_FAILED = 2,
} E_OtaResult;

typedef enum {
    OTA_FAIL_NONE = 0,
    OTA_FAIL_IMAGE_HASH_ERROR = 1,
    OTA_FAIL_IMAGE_SIZE_ERROR = 2,
    OTA_FAIL_NEW_FW_BOOT_FAILED = 3,
    OTA_FAIL_STATE_CRC_ERROR = 4,
    OTA_FAIL_NO_VALID_APP = 5,
} E_OtaFailReason;

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
} T_OtaInfo;

int BootOtaState_Load(T_OtaInfo *out);
int BootOtaState_Save(T_OtaInfo *info);
int BootOtaState_PrepareBoot(T_OtaInfo *info, uint32_t *bootBase);
uint32_t BootOtaState_SlotToBase(uint8_t slot);
uint32_t BootOtaState_SelectBootBase(const T_OtaInfo *info);
uint32_t BootOtaState_CalcCrc32(const void *data, uint32_t len);

#endif /* BOOT_OTA_STATE_H */
