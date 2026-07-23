#ifndef SOLARCLEAN_OTA_HASH_H
#define SOLARCLEAN_OTA_HASH_H

#include <stdint.h>

#include "solarclean_ota_state.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SOLARCLEAN_OTA_SHA256_SIZE      32U
#define SOLARCLEAN_OTA_SHA256_HEX_SIZE  64U

typedef struct {
    uint32_t state[8];
    uint64_t bitCount;
    uint8_t buffer[64];
    uint32_t bufferLen;
} T_SolarCleanSha256Ctx;

void SolarCleanSha256_Init(T_SolarCleanSha256Ctx *ctx);
void SolarCleanSha256_Update(T_SolarCleanSha256Ctx *ctx, const uint8_t *data, uint32_t len);
void SolarCleanSha256_Final(T_SolarCleanSha256Ctx *ctx, uint8_t out[SOLARCLEAN_OTA_SHA256_SIZE]);

int SolarCleanOtaHash_ParseHex(const char *hex, uint8_t out[SOLARCLEAN_OTA_SHA256_SIZE]);
int SolarCleanOtaHash_ToHex(const uint8_t hash[SOLARCLEAN_OTA_SHA256_SIZE],
                            char out[SOLARCLEAN_OTA_SHA256_HEX_SIZE + 1U]);
int SolarCleanOtaHash_VerifySlot(E_SolarCleanOtaSlot slot,
                                 uint32_t imageSize,
                                 const uint8_t expected[SOLARCLEAN_OTA_SHA256_SIZE]);
int SolarCleanOtaHash_VerifySlotB(uint32_t imageSize,
                                  const uint8_t expected[SOLARCLEAN_OTA_SHA256_SIZE]);

#ifdef __cplusplus
}
#endif

#endif /* SOLARCLEAN_OTA_HASH_H */
