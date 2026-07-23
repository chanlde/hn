#include "boot_flash.h"

#include "stm32h743xx.h"

#include <string.h>

#define BOOT_FLASH_WORD_BYTES 32UL
#define BOOT_FLASH_KEY1       0x45670123UL
#define BOOT_FLASH_KEY2       0xCDEF89ABUL
#define BOOT_FLASH_SR_ERRORS  (FLASH_SR_WRPERR | FLASH_SR_PGSERR | FLASH_SR_STRBERR | \
                               FLASH_SR_INCERR | FLASH_SR_OPERR | FLASH_SR_RDPERR | \
                               FLASH_SR_RDSERR | FLASH_SR_SNECCERR | FLASH_SR_DBECCERR)
#define BOOT_FLASH_CCR_ERRORS (FLASH_CCR_CLR_WRPERR | FLASH_CCR_CLR_PGSERR | FLASH_CCR_CLR_STRBERR | \
                               FLASH_CCR_CLR_INCERR | FLASH_CCR_CLR_OPERR | FLASH_CCR_CLR_RDPERR | \
                               FLASH_CCR_CLR_RDSERR | FLASH_CCR_CLR_SNECCERR | FLASH_CCR_CLR_DBECCERR)

static uint32_t flash_bank_base(uint32_t address)
{
    return (address >= 0x08100000UL) ? 0x08100000UL : 0x08000000UL;
}

static uint32_t flash_sector(uint32_t address)
{
    return (address - flash_bank_base(address)) / 0x20000UL;
}

static uint32_t flash_bank_bit(uint32_t address)
{
    (void)address;
    return 0UL;
}

static void flash_unlock(void)
{
    if ((FLASH->CR1 & FLASH_CR_LOCK) != 0UL) {
        FLASH->KEYR1 = BOOT_FLASH_KEY1;
        FLASH->KEYR1 = BOOT_FLASH_KEY2;
    }
    if ((FLASH->CR2 & FLASH_CR_LOCK) != 0UL) {
        FLASH->KEYR2 = BOOT_FLASH_KEY1;
        FLASH->KEYR2 = BOOT_FLASH_KEY2;
    }
}

static void flash_lock(void)
{
    FLASH->CR1 |= FLASH_CR_LOCK;
    FLASH->CR2 |= FLASH_CR_LOCK;
}

static int flash_wait(void)
{
    while ((FLASH->SR1 & FLASH_SR_QW) != 0UL || (FLASH->SR2 & FLASH_SR_QW) != 0UL) {
    }
    while ((FLASH->SR1 & FLASH_SR_BSY) != 0UL || (FLASH->SR2 & FLASH_SR_BSY) != 0UL) {
    }

    if ((FLASH->SR1 & BOOT_FLASH_SR_ERRORS) != 0UL ||
        (FLASH->SR2 & BOOT_FLASH_SR_ERRORS) != 0UL)
        return -1;

    return 0;
}

static void flash_clear_errors(void)
{
    FLASH->CCR1 = BOOT_FLASH_CCR_ERRORS | FLASH_CCR_CLR_EOP;
    FLASH->CCR2 = BOOT_FLASH_CCR_ERRORS | FLASH_CCR_CLR_EOP;
}

int BootFlash_Erase(uint32_t startAddress, uint32_t endAddress)
{
    uint32_t cur;

    if (endAddress < startAddress)
        return -1;

    flash_unlock();
    for (cur = startAddress; cur <= endAddress; cur = flash_bank_base(cur) + ((flash_sector(cur) + 1UL) * 0x20000UL)) {
        uint32_t sector = flash_sector(cur);
        volatile uint32_t *cr = (cur >= 0x08100000UL) ? &FLASH->CR2 : &FLASH->CR1;

        flash_clear_errors();
        *cr &= ~(FLASH_CR_BER | FLASH_CR_SER | FLASH_CR_SNB | FLASH_CR_PSIZE);
        *cr |= flash_bank_bit(cur) | (sector << FLASH_CR_SNB_Pos) | FLASH_CR_SER | FLASH_CR_START | FLASH_CR_PSIZE_1;

        if (flash_wait() != 0) {
            *cr &= ~(FLASH_CR_BER | FLASH_CR_SER | FLASH_CR_START);
            flash_lock();
            return -1;
        }
        *cr &= ~(FLASH_CR_BER | FLASH_CR_SER | FLASH_CR_START);
    }
    flash_lock();
    return 0;
}

int BootFlash_Write(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint8_t line[BOOT_FLASH_WORD_BYTES];
    uint32_t offset = 0UL;

    if (data == NULL)
        return -1;

    flash_unlock();
    while (offset < len) {
        uint32_t chunk = len - offset;
        volatile uint32_t *cr = ((address + offset) >= 0x08100000UL) ? &FLASH->CR2 : &FLASH->CR1;

        if (chunk > BOOT_FLASH_WORD_BYTES)
            chunk = BOOT_FLASH_WORD_BYTES;
        memset(line, 0xFF, sizeof(line));
        memcpy(line, &data[offset], chunk);

        flash_clear_errors();
        *cr &= ~(FLASH_CR_BER | FLASH_CR_SER | FLASH_CR_SNB);
        *cr |= FLASH_CR_PG;
        __DSB();
        for (uint32_t i = 0; i < BOOT_FLASH_WORD_BYTES; i += 4UL)
            *(volatile uint32_t *)(address + offset + i) = *(uint32_t *)&line[i];
        __DSB();

        if (flash_wait() != 0) {
            *cr &= ~FLASH_CR_PG;
            flash_lock();
            return -1;
        }
        *cr &= ~FLASH_CR_PG;

        if (memcmp((const void *)(address + offset), line, BOOT_FLASH_WORD_BYTES) != 0) {
            flash_lock();
            return -1;
        }
        offset += chunk;
    }

    flash_lock();
    return 0;
}
