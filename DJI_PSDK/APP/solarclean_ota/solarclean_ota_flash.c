#include "solarclean_ota_flash.h"

#include "flash_if.h"

#include <string.h>

#define SOLARCLEAN_OTA_FLASHWORD_BYTES 32U

static int ota_flash_check_range(E_SolarCleanOtaSlot slot, uint32_t offset, uint32_t len)
{
    uint32_t slotSize = SolarCleanOtaFlash_GetSlotSize(slot);

    if (len == 0U)
        return SOLARCLEAN_OTA_FLASH_OK;
    if (slotSize == 0U)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;
    if (offset >= slotSize)
        return SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW;
    if (len > (slotSize - offset))
        return SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW;
    return SOLARCLEAN_OTA_FLASH_OK;
}

static int ota_flash_flush_pending(T_SolarCleanOtaFlashWriter *writer)
{
    uint32_t writeAddress;

    if (writer == NULL)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;
    if (writer->pendingLen == 0U)
        return SOLARCLEAN_OTA_FLASH_OK;
    if (writer->pendingOffset >= SolarCleanOtaFlash_GetSlotSize(writer->targetSlot))
        return SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW;
    if (SOLARCLEAN_OTA_FLASHWORD_BYTES > (SolarCleanOtaFlash_GetSlotSize(writer->targetSlot) - writer->pendingOffset))
        return SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW;

    while (writer->pendingLen < SOLARCLEAN_OTA_FLASHWORD_BYTES) {
        writer->pending[writer->pendingLen] = 0xFFU;
        writer->pendingLen++;
    }

    writeAddress = writer->slotBase + writer->pendingOffset;
    if (FLASH_If_Write(writeAddress, writer->pending, SOLARCLEAN_OTA_FLASHWORD_BYTES) != FLASHIF_OK)
        return SOLARCLEAN_OTA_FLASH_WRITE_FAILED;

    if (memcmp((const void *)writeAddress, writer->pending, SOLARCLEAN_OTA_FLASHWORD_BYTES) != 0)
        return SOLARCLEAN_OTA_FLASH_VERIFY_FAILED;

    writer->pendingLen = 0U;
    return SOLARCLEAN_OTA_FLASH_OK;
}

uint32_t SolarCleanOtaFlash_GetSlotBase(E_SolarCleanOtaSlot slot)
{
    if (slot == SOLARCLEAN_OTA_SLOT_A)
        return APPLICATION_ADDRESS;
    if (slot == SOLARCLEAN_OTA_SLOT_B)
        return APPLICATION_STORE_ADDRESS;
    return 0U;
}

uint32_t SolarCleanOtaFlash_GetSlotEnd(E_SolarCleanOtaSlot slot)
{
    if (slot == SOLARCLEAN_OTA_SLOT_A)
        return APPLICATION_ADDRESS_END;
    if (slot == SOLARCLEAN_OTA_SLOT_B)
        return APPLICATION_STORE_ADDRESS_END;
    return 0U;
}

uint32_t SolarCleanOtaFlash_GetSlotSize(E_SolarCleanOtaSlot slot)
{
    uint32_t base = SolarCleanOtaFlash_GetSlotBase(slot);
    uint32_t end = SolarCleanOtaFlash_GetSlotEnd(slot);

    if (base == 0U || end < base)
        return 0U;
    return end - base + 1U;
}

int SolarCleanOtaFlash_GetSlotByAddress(uint32_t address, E_SolarCleanOtaSlot *slot)
{
    if (slot == NULL)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;

    if (address >= APPLICATION_ADDRESS && address <= APPLICATION_ADDRESS_END) {
        *slot = SOLARCLEAN_OTA_SLOT_A;
        return SOLARCLEAN_OTA_FLASH_OK;
    }
    if (address >= APPLICATION_STORE_ADDRESS && address <= APPLICATION_STORE_ADDRESS_END) {
        *slot = SOLARCLEAN_OTA_SLOT_B;
        return SOLARCLEAN_OTA_FLASH_OK;
    }

    return SOLARCLEAN_OTA_FLASH_BAD_ARG;
}

uint32_t SolarCleanOtaFlash_GetSlotBBase(void)
{
    return SolarCleanOtaFlash_GetSlotBase(SOLARCLEAN_OTA_SLOT_B);
}

uint32_t SolarCleanOtaFlash_GetSlotBEnd(void)
{
    return SolarCleanOtaFlash_GetSlotEnd(SOLARCLEAN_OTA_SLOT_B);
}

uint32_t SolarCleanOtaFlash_GetSlotBSize(void)
{
    return SolarCleanOtaFlash_GetSlotSize(SOLARCLEAN_OTA_SLOT_B);
}

int SolarCleanOtaFlash_Begin(T_SolarCleanOtaFlashWriter *writer,
                             E_SolarCleanOtaSlot targetSlot,
                             uint32_t expectedSize)
{
    uint32_t slotSize = SolarCleanOtaFlash_GetSlotSize(targetSlot);

    if (writer == NULL || expectedSize == 0U)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;
    if (slotSize == 0U)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;
    if (expectedSize > slotSize)
        return SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW;

    memset(writer, 0, sizeof(*writer));
    writer->expectedSize = expectedSize;
    writer->slotBase = SolarCleanOtaFlash_GetSlotBase(targetSlot);
    writer->slotEnd = SolarCleanOtaFlash_GetSlotEnd(targetSlot);
    writer->targetSlot = targetSlot;
    writer->sessionActive = 1U;

    FLASH_If_BeginWriteSession();
    if (FLASH_If_Erase(writer->slotBase, writer->slotEnd) != FLASHIF_OK) {
        SolarCleanOtaFlash_Abort(writer);
        return SOLARCLEAN_OTA_FLASH_ERASE_FAILED;
    }

    writer->active = 1U;
    return SOLARCLEAN_OTA_FLASH_OK;
}

int SolarCleanOtaFlash_Write(T_SolarCleanOtaFlashWriter *writer, const uint8_t *data, uint32_t len)
{
    int rangeResult;
    uint32_t consumed = 0U;

    if (writer == NULL || data == NULL)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;
    if (writer->active == 0U)
        return SOLARCLEAN_OTA_FLASH_SEQUENCE_ERROR;
    if (len == 0U)
        return SOLARCLEAN_OTA_FLASH_OK;
    if (writer->writtenSize > writer->expectedSize)
        return SOLARCLEAN_OTA_FLASH_SEQUENCE_ERROR;
    if (len > (writer->expectedSize - writer->writtenSize))
        return SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW;

    rangeResult = ota_flash_check_range(writer->targetSlot, writer->writtenSize, len);
    if (rangeResult != SOLARCLEAN_OTA_FLASH_OK)
        return rangeResult;

    while (consumed < len) {
        uint32_t room;
        uint32_t copyLen;

        if (writer->pendingLen == 0U) {
            writer->pendingOffset = writer->writtenSize;
            memset(writer->pending, 0xFF, sizeof(writer->pending));
        }

        room = SOLARCLEAN_OTA_FLASHWORD_BYTES - writer->pendingLen;
        copyLen = len - consumed;
        if (copyLen > room)
            copyLen = room;

        memcpy(&writer->pending[writer->pendingLen], &data[consumed], copyLen);
        writer->pendingLen += copyLen;
        writer->writtenSize += copyLen;
        consumed += copyLen;

        if (writer->pendingLen == SOLARCLEAN_OTA_FLASHWORD_BYTES) {
            int flushResult = ota_flash_flush_pending(writer);
            if (flushResult != SOLARCLEAN_OTA_FLASH_OK)
                return flushResult;
        }
    }

    return SOLARCLEAN_OTA_FLASH_OK;
}

int SolarCleanOtaFlash_End(T_SolarCleanOtaFlashWriter *writer)
{
    int flushResult;

    if (writer == NULL)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;
    if (writer->active == 0U)
        return SOLARCLEAN_OTA_FLASH_SEQUENCE_ERROR;
    if (writer->writtenSize != writer->expectedSize) {
        SolarCleanOtaFlash_Abort(writer);
        return SOLARCLEAN_OTA_FLASH_SIZE_OVERFLOW;
    }

    flushResult = ota_flash_flush_pending(writer);
    if (flushResult != SOLARCLEAN_OTA_FLASH_OK) {
        SolarCleanOtaFlash_Abort(writer);
        return flushResult;
    }

    writer->active = 0U;
    if (writer->sessionActive != 0U) {
        writer->sessionActive = 0U;
        FLASH_If_EndWriteSession();
    }
    return SOLARCLEAN_OTA_FLASH_OK;
}

void SolarCleanOtaFlash_Abort(T_SolarCleanOtaFlashWriter *writer)
{
    if (writer == NULL)
        return;

    writer->active = 0U;
    writer->pendingLen = 0U;
    if (writer->sessionActive != 0U) {
        writer->sessionActive = 0U;
        FLASH_If_EndWriteSession();
    }
}

int SolarCleanOtaFlash_VerifyRange(E_SolarCleanOtaSlot slot, uint32_t offset, const uint8_t *data, uint32_t len)
{
    int rangeResult;
    uint32_t slotBase;

    if (data == NULL && len != 0U)
        return SOLARCLEAN_OTA_FLASH_BAD_ARG;

    rangeResult = ota_flash_check_range(slot, offset, len);
    if (rangeResult != SOLARCLEAN_OTA_FLASH_OK)
        return rangeResult;

    if (len == 0U)
        return SOLARCLEAN_OTA_FLASH_OK;

    slotBase = SolarCleanOtaFlash_GetSlotBase(slot);
    return (memcmp((const void *)(slotBase + offset), data, len) == 0)
               ? SOLARCLEAN_OTA_FLASH_OK
               : SOLARCLEAN_OTA_FLASH_VERIFY_FAILED;
}
