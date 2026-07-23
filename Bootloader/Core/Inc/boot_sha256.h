#ifndef BOOT_SHA256_H
#define BOOT_SHA256_H

#include <stdint.h>

#define BOOT_SHA256_SIZE 32UL

int BootSha256_VerifyMemory(uint32_t address, uint32_t len, const uint8_t expected[BOOT_SHA256_SIZE]);

#endif /* BOOT_SHA256_H */
