#ifndef SOLARCLEAN_OTA_FLASH_H
#define SOLARCLEAN_OTA_FLASH_H

#include <stdint.h>

#include "solarclean_ota_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SOLARCLEAN_OTA_FLASH_OK = 0,
    SOLARCLEAN_OTA_FLASH_BAD_ARG = -1,
    SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW = -2,
    SOLARCLEAN_OTA_FLASH_ERASE_FAILED = -3,
    SOLARCLEAN_OTA_FLASH_WRITE_FAILED = -4,
    SOLARCLEAN_OTA_FLASH_VERIFY_FAILED = -5,
    SOLARCLEAN_OTA_FLASH_SEQUENCE_ERROR = -6,
} E_SolarCleanOtaFlashResult;

typedef struct {
    uint32_t expectedSize;
    uint32_t writtenSize;
    uint32_t slotBase;
    uint32_t slotEnd;
    uint32_t pendingOffset;
    uint32_t pendingLen;
    uint8_t pending[32];
    E_SolarCleanOtaSlot targetSlot;
    uint8_t active;
    uint8_t sessionActive;
} T_SolarCleanOtaFlashWriter;

uint32_t SolarCleanOtaFlash_GetSlotBase(E_SolarCleanOtaSlot slot);
uint32_t SolarCleanOtaFlash_GetSlotEnd(E_SolarCleanOtaSlot slot);
uint32_t SolarCleanOtaFlash_GetSlotSize(E_SolarCleanOtaSlot slot);
int SolarCleanOtaFlash_GetSlotByAddress(uint32_t address, E_SolarCleanOtaSlot *slot);

uint32_t SolarCleanOtaFlash_GetSlotBBase(void);
uint32_t SolarCleanOtaFlash_GetSlotBEnd(void);
uint32_t SolarCleanOtaFlash_GetSlotBSize(void);

int SolarCleanOtaFlash_Begin(T_SolarCleanOtaFlashWriter *writer,
                             E_SolarCleanOtaSlot targetSlot,
                             uint32_t expectedSize);
int SolarCleanOtaFlash_Write(T_SolarCleanOtaFlashWriter *writer, const uint8_t *data, uint32_t len);
int SolarCleanOtaFlash_End(T_SolarCleanOtaFlashWriter *writer);
void SolarCleanOtaFlash_Abort(T_SolarCleanOtaFlashWriter *writer);
int SolarCleanOtaFlash_VerifyRange(E_SolarCleanOtaSlot slot, uint32_t offset, const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* SOLARCLEAN_OTA_FLASH_H */
