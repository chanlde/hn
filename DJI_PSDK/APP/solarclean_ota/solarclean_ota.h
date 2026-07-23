#ifndef SOLARCLEAN_OTA_H
#define SOLARCLEAN_OTA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SOLARCLEAN_HARDWARE_VERSION
#define SOLARCLEAN_HARDWARE_VERSION "HW-A"
#endif

#ifndef SOLARCLEAN_OTA_SLOT
#define SOLARCLEAN_OTA_SLOT "A"
#endif

#ifndef SOLARCLEAN_FIRMWARE_INNER_VERSION
#define SOLARCLEAN_FIRMWARE_INNER_VERSION 1U
#endif

typedef void (*SolarCleanOta_PublishStatus)(const char *payload, void *user);

typedef struct {
    char targetVersion[48];
    uint32_t targetInnerVersion;
    char hardwareVersion[32];
    uint32_t fileSize;
    char sha256[80];
    char downloadUrl[256];
} T_SolarCleanOtaStartRequest;

typedef struct {
    int ok;
    int code;
    char msg[96];
} T_SolarCleanOtaAck;

typedef enum {
    SOLARCLEAN_OTA_STATE_IDLE = 0,
    SOLARCLEAN_OTA_STATE_VALIDATING,
    SOLARCLEAN_OTA_STATE_DOWNLOADING,
    SOLARCLEAN_OTA_STATE_VERIFYING,
    SOLARCLEAN_OTA_STATE_WRITING,
    SOLARCLEAN_OTA_STATE_PENDING_REBOOT,
    SOLARCLEAN_OTA_STATE_FAILED,
} E_SolarCleanOtaState;

typedef enum {
    SOLARCLEAN_OTA_FAIL_NONE = 0,
    SOLARCLEAN_OTA_FAIL_BAD_REQUEST,
    SOLARCLEAN_OTA_FAIL_HW_NOT_MATCH,
    SOLARCLEAN_OTA_FAIL_DEVICE_BUSY,
    SOLARCLEAN_OTA_FAIL_IMAGE_TOO_LARGE,
    SOLARCLEAN_OTA_FAIL_BAD_HASH,
    SOLARCLEAN_OTA_FAIL_HASH_VERIFY_FAILED,
    SOLARCLEAN_OTA_FAIL_DOWNLOAD_FAILED,
    SOLARCLEAN_OTA_FAIL_DOWNLOAD_NOT_READY,
    SOLARCLEAN_OTA_FAIL_FLASH_NOT_READY,
    SOLARCLEAN_OTA_FAIL_STATE_WRITE_FAILED,
} E_SolarCleanOtaFailReason;

const char *SolarCleanOta_GetHardwareVersion(void);
const char *SolarCleanOta_GetFirmwareVersion(void);
uint32_t SolarCleanOta_GetInnerVersion(void);
const char *SolarCleanOta_GetSlot(void);
const char *SolarCleanOta_GetStatus(void);
const char *SolarCleanOta_GetLastResult(void);
const char *SolarCleanOta_GetLastFailReason(void);
const char *SolarCleanOta_GetNetwork(void);
uint8_t SolarCleanOta_IsActive(void);

void SolarCleanOta_ConfirmRunningImage(void);
void SolarCleanOta_Init(void);
void SolarCleanOta_Service(void);
void SolarCleanOta_HandleStart(const T_SolarCleanOtaStartRequest *req,
                               T_SolarCleanOtaAck *ack,
                               SolarCleanOta_PublishStatus publish,
                               void *publishUser);

int SolarCleanOta_CommitNextSlotForReboot(const T_SolarCleanOtaStartRequest *req,
                                          T_SolarCleanOtaAck *ack,
                                          SolarCleanOta_PublishStatus publish,
                                          void *publishUser);
int SolarCleanOta_DownloadNextSlotAndCommit(const T_SolarCleanOtaStartRequest *req,
                                            T_SolarCleanOtaAck *ack,
                                            SolarCleanOta_PublishStatus publish,
                                            void *publishUser);

int SolarCleanOta_CommitSlotBForReboot(const T_SolarCleanOtaStartRequest *req,
                                       T_SolarCleanOtaAck *ack,
                                       SolarCleanOta_PublishStatus publish,
                                       void *publishUser);
int SolarCleanOta_DownloadSlotBAndCommit(const T_SolarCleanOtaStartRequest *req,
                                         T_SolarCleanOtaAck *ack,
                                         SolarCleanOta_PublishStatus publish,
                                         void *publishUser);

#ifdef __cplusplus
}
#endif

#endif /* SOLARCLEAN_OTA_H */
