#ifndef BOOT_FLASH_H
#define BOOT_FLASH_H

#include <stdint.h>

int BootFlash_Erase(uint32_t startAddress, uint32_t endAddress);
int BootFlash_Write(uint32_t address, const uint8_t *data, uint32_t len);

#endif /* BOOT_FLASH_H */
