/**
 ******************************************************************************
 * @file    flash_if.c
 * @brief   STM32H7 内部 Flash 擦写（适配 HAL），供 OTA / 航线存储等使用。
 *          原 IAP 示例针对 F4，已按 STM32H7xx HAL 改写。
 ******************************************************************************
 */
#include "flash_if.h"
#include <string.h>

/* H743/75 等：一次编程为 256 bit = 32 字节 Flash word */
#define FLASHIF_FLASHWORD_BYTES    32U
#define FLASHIF_KEY1               0x45670123UL
#define FLASHIF_KEY2               0xCDEF89ABUL
#define FLASHIF_SR_ERRORS          (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | \
                                    FLASH_SR_INCERR | FLASH_SR_OPERR | FLASH_SR_RDPERR | \
                                    FLASH_SR_RDSERR | FLASH_SR_SNECCERR | FLASH_SR_DBECCERR)
#define FLASHIF_CCR_ERRORS         (FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR | \
                                    FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPERR | FLASH_CCR_CLR_RDPERR | \
                                    FLASH_CCR_CLR_RDSERR | FLASH_CCR_CLR_SNECCERR | FLASH_CCR_CLR_DBECCERR)

static uint32_t FlashIf_SectorBaseAddr(uint32_t addr);
static void FlashIf_GetSectorAndBank(uint32_t address, uint32_t *sector, uint32_t *bank);
static uint32_t FlashIf_IsDCacheEnabled(void);
static void FlashIf_Unlock(void);
static void FlashIf_Lock(void);
static volatile uint32_t *FlashIf_Cr(uint32_t address);
static volatile uint32_t *FlashIf_Sr(uint32_t address);
static volatile uint32_t *FlashIf_Ccr(uint32_t address);
static void FlashIf_ClearBankErrors(uint32_t address);
static int FlashIf_WaitBank(uint32_t address);
static int FlashIf_ProgramFlashWord(uint32_t address, const uint32_t line[FLASHIF_FLASHWORD_BYTES / sizeof(uint32_t)]);

static uint32_t s_flashWriteSessionDepth = 0U;
static uint32_t s_dcacheWasEnabled = 0U;

void FLASH_If_Init(void)
{
    /* 无全局配置；保留接口与旧 IAP 一致 */
}

void FLASH_If_BeginWriteSession(void)
{
    if (s_flashWriteSessionDepth == 0U) {
        s_dcacheWasEnabled = FlashIf_IsDCacheEnabled();
        if (s_dcacheWasEnabled != 0U) {
            SCB_CleanInvalidateDCache();
            SCB_DisableDCache();
        }
        __DSB();
        __ISB();
    }
    s_flashWriteSessionDepth++;
}

void FLASH_If_EndWriteSession(void)
{
    if (s_flashWriteSessionDepth == 0U) {
        return;
    }

    s_flashWriteSessionDepth--;
    if (s_flashWriteSessionDepth == 0U) {
        SCB_InvalidateICache();
        if (s_dcacheWasEnabled != 0U) {
            SCB_InvalidateDCache();
            SCB_EnableDCache();
        }
        s_dcacheWasEnabled = 0U;
        __DSB();
        __ISB();
    }
}

uint32_t FLASH_If_Erase(uint32_t startAddress, uint32_t endAddress)
{
    uint32_t sectorError = 0xFFFFFFFFU;
    FLASH_EraseInitTypeDef eraseInit = {0};
    uint32_t cur;
    uint32_t localSession = (s_flashWriteSessionDepth == 0U) ? 1U : 0U;

    if (endAddress < startAddress) {
        return FLASHIF_ERASE_ERROR;
    }

    if (localSession != 0U) {
        FLASH_If_BeginWriteSession();
    }

    cur = FlashIf_SectorBaseAddr(startAddress);
    while (cur <= endAddress) {
        uint32_t sector = 0;
        uint32_t bank = FLASH_BANK_1;

        FlashIf_GetSectorAndBank(cur, &sector, &bank);

        HAL_FLASH_Unlock();
        __HAL_FLASH_CLEAR_FLAG_BANK1(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_EOP_BANK1);
#if defined(DUAL_BANK)
        if (bank == FLASH_BANK_2) {
            __HAL_FLASH_CLEAR_FLAG_BANK2(FLASH_FLAG_ALL_ERRORS_BANK2 | FLASH_FLAG_EOP_BANK2);
        }
#endif

        eraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
        eraseInit.Banks = bank;
        eraseInit.Sector = sector;
        eraseInit.NbSectors = 1U;
        eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;

        if (HAL_FLASHEx_Erase(&eraseInit, &sectorError) != HAL_OK) {
            HAL_FLASH_Lock();
            if (localSession != 0U) {
                FLASH_If_EndWriteSession();
            }
            return FLASHIF_ERASE_ERROR;
        }
        HAL_FLASH_Lock();

        cur += FLASH_SECTOR_SIZE;
    }

    if (localSession != 0U) {
        FLASH_If_EndWriteSession();
    }
    return FLASHIF_OK;
}

uint32_t FLASH_If_Write(uint32_t flashAddress, const uint8_t *data, uint32_t dataLength)
{
    uint32_t endAddr;
    uint32_t lineStart;
    uint32_t i;
    uint32_t result = FLASHIF_OK;
    uint32_t localSession = (s_flashWriteSessionDepth == 0U) ? 1U : 0U;

    if (data == NULL) {
        return FLASHIF_WRITING_ERROR;
    }
    if (dataLength == 0U) {
        return FLASHIF_OK;
    }

    endAddr = flashAddress + dataLength;
    if (endAddr < flashAddress || (endAddr - 1U) > FLASH_END_ADDRESS) {
        return FLASHIF_WRITING_ERROR;
    }

    if (localSession != 0U) {
        FLASH_If_BeginWriteSession();
    }

    FlashIf_Unlock();
#if defined(FLASH_BANK2_BASE)
    if (flashAddress >= FLASH_BANK2_BASE) {
        __HAL_FLASH_CLEAR_FLAG_BANK2(FLASH_FLAG_ALL_ERRORS_BANK2 | FLASH_FLAG_EOP_BANK2);
    } else
#endif
    {
        __HAL_FLASH_CLEAR_FLAG_BANK1(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_EOP_BANK1);
    }

    for (lineStart = (flashAddress / FLASHIF_FLASHWORD_BYTES) * FLASHIF_FLASHWORD_BYTES;
         lineStart < endAddr;
         lineStart += FLASHIF_FLASHWORD_BYTES) {
        uint32_t lineBuf[FLASHIF_FLASHWORD_BYTES / sizeof(uint32_t)];
        uint8_t *lineBytes = (uint8_t *)lineBuf;

        memcpy(lineBuf, (const void *)lineStart, FLASHIF_FLASHWORD_BYTES);

        for (i = 0; i < FLASHIF_FLASHWORD_BYTES; i++) {
            uint32_t absAddr = lineStart + i;
            if (absAddr >= flashAddress && absAddr < endAddr) {
                lineBytes[i] = data[absAddr - flashAddress];
            }
        }

        if (FlashIf_ProgramFlashWord(lineStart, lineBuf) != 0) {
            result = FLASHIF_WRITING_ERROR;
            break;
        }

        if (memcmp((const void *)lineStart, lineBuf, FLASHIF_FLASHWORD_BYTES) != 0) {
            result = FLASHIF_WRITINGCTRL_ERROR;
            break;
        }
    }

    FlashIf_Lock();
    if (localSession != 0U) {
        FLASH_If_EndWriteSession();
    }
    return result;
}

uint16_t FLASH_If_GetWriteProtectionStatus(void)
{
    FLASH_OBProgramInitTypeDef ob;

    memset(&ob, 0, sizeof(ob));
    ob.Banks = FLASH_BANK_1;
    HAL_FLASHEx_OBGetConfig(&ob);

    if ((ob.WRPSector & FLASH_SECTOR_TO_BE_PROTECTED) != 0U) {
        return FLASHIF_PROTECTION_WRPENABLED;
    }
#if defined(DUAL_BANK)
    memset(&ob, 0, sizeof(ob));
    ob.Banks = FLASH_BANK_2;
    HAL_FLASHEx_OBGetConfig(&ob);
    if ((ob.WRPSector & FLASH_SECTOR_TO_BE_PROTECTED) != 0U) {
        return FLASHIF_PROTECTION_WRPENABLED;
    }
#endif
    return FLASHIF_PROTECTION_NONE;
}

static uint32_t FlashIf_SectorBaseAddr(uint32_t address)
{
    uint32_t bankBase = FLASH_BASE;
#if defined(FLASH_BANK2_BASE)
    if (address >= FLASH_BANK2_BASE) {
        bankBase = FLASH_BANK2_BASE;
    }
#endif
    {
        uint32_t off = address - bankBase;
        return bankBase + (off / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;
    }
}

static void FlashIf_GetSectorAndBank(uint32_t address, uint32_t *sector, uint32_t *bank)
{
    uint32_t bankBase = FLASH_BASE;
    *bank = FLASH_BANK_1;
#if defined(FLASH_BANK2_BASE)
    if (address >= FLASH_BANK2_BASE) {
        bankBase = FLASH_BANK2_BASE;
        *bank = FLASH_BANK_2;
    }
#endif
    *sector = (address - bankBase) / FLASH_SECTOR_SIZE;
}

static uint32_t FlashIf_IsDCacheEnabled(void)
{
    return ((SCB->CCR & SCB_CCR_DC_Msk) != 0U) ? 1U : 0U;
}

static void FlashIf_Unlock(void)
{
    if ((FLASH->CR1 & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR1 = FLASHIF_KEY1;
        FLASH->KEYR1 = FLASHIF_KEY2;
    }
#if defined(DUAL_BANK)
    if ((FLASH->CR2 & FLASH_CR_LOCK) != 0U) {
        FLASH->KEYR2 = FLASHIF_KEY1;
        FLASH->KEYR2 = FLASHIF_KEY2;
    }
#endif
}

static void FlashIf_Lock(void)
{
    FLASH->CR1 |= FLASH_CR_LOCK;
#if defined(DUAL_BANK)
    FLASH->CR2 |= FLASH_CR_LOCK;
#endif
}

static volatile uint32_t *FlashIf_Cr(uint32_t address)
{
#if defined(DUAL_BANK)
    if (address >= FLASH_BANK2_BASE)
        return &FLASH->CR2;
#endif
    return &FLASH->CR1;
}

static volatile uint32_t *FlashIf_Sr(uint32_t address)
{
#if defined(DUAL_BANK)
    if (address >= FLASH_BANK2_BASE)
        return &FLASH->SR2;
#endif
    return &FLASH->SR1;
}

static volatile uint32_t *FlashIf_Ccr(uint32_t address)
{
#if defined(DUAL_BANK)
    if (address >= FLASH_BANK2_BASE)
        return &FLASH->CCR2;
#endif
    return &FLASH->CCR1;
}

static void FlashIf_ClearBankErrors(uint32_t address)
{
    *FlashIf_Ccr(address) = FLASHIF_CCR_ERRORS | FLASH_CCR_CLR_EOP;
}

static int FlashIf_WaitBank(uint32_t address)
{
    volatile uint32_t *sr = FlashIf_Sr(address);

    while ((*sr & (FLASH_SR_QW | FLASH_SR_BSY)) != 0U) {
    }
    return ((*sr & FLASHIF_SR_ERRORS) == 0U) ? 0 : -1;
}

static int FlashIf_ProgramFlashWord(uint32_t address, const uint32_t line[FLASHIF_FLASHWORD_BYTES / sizeof(uint32_t)])
{
    volatile uint32_t *cr;
    volatile uint32_t *dst;

    if (line == NULL || (address % FLASHIF_FLASHWORD_BYTES) != 0U)
        return -1;

    cr = FlashIf_Cr(address);
    dst = (volatile uint32_t *)address;

    FlashIf_ClearBankErrors(address);
    *cr |= FLASH_CR_PG;
    __DSB();
    __ISB();

    for (uint32_t i = 0U; i < (FLASHIF_FLASHWORD_BYTES / sizeof(uint32_t)); i++) {
        dst[i] = line[i];
    }

    __DSB();
    __ISB();

    if (FlashIf_WaitBank(address) != 0) {
        *cr &= ~FLASH_CR_PG;
        return -1;
    }

    *cr &= ~FLASH_CR_PG;
    return 0;
}

HAL_StatusTypeDef FLASH_If_WriteProtectionConfig(uint32_t modifier)
{
    FLASH_OBProgramInitTypeDef configNew = {0};
    FLASH_OBProgramInitTypeDef configOld = {0};
    HAL_StatusTypeDef result;

    configOld.Banks = FLASH_BANK_1;
    HAL_FLASHEx_OBGetConfig(&configOld);

    configNew.OptionType = OPTIONBYTE_WRP;
    configNew.WRPState = modifier;
    configNew.Banks = FLASH_BANK_1;
    configNew.RDPLevel = OB_RDP_LEVEL_0;
    configNew.USERConfig = configOld.USERConfig;
    configNew.WRPSector = configOld.WRPSector | FLASH_SECTOR_TO_BE_PROTECTED;

    HAL_FLASH_Unlock();
    HAL_FLASH_OB_Unlock();

    result = HAL_FLASHEx_OBProgram(&configNew);

    HAL_FLASH_OB_Lock();
    HAL_FLASH_Lock();

    return result;
}
