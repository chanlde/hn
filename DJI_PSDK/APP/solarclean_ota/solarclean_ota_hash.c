#include "solarclean_ota_hash.h"

#include "solarclean_ota_flash.h"

#include <string.h>

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32U - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)     (ROTR32((x), 2U) ^ ROTR32((x), 13U) ^ ROTR32((x), 22U))
#define BSIG1(x)     (ROTR32((x), 6U) ^ ROTR32((x), 11U) ^ ROTR32((x), 25U))
#define SSIG0(x)     (ROTR32((x), 7U) ^ ROTR32((x), 18U) ^ ((x) >> 3U))
#define SSIG1(x)     (ROTR32((x), 17U) ^ ROTR32((x), 19U) ^ ((x) >> 10U))

static const uint32_t k_sha256[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL,
    0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL,
    0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
    0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL,
    0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL,
    0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL,
    0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL,
    0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
    0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL,
};

static uint32_t read_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           ((uint32_t)p[3]);
}

static void write_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void write_be64(uint8_t *p, uint64_t v)
{
    for (uint32_t i = 0; i < 8U; i++)
        p[i] = (uint8_t)(v >> (56U - (i * 8U)));
}

static void sha256_transform(T_SolarCleanSha256Ctx *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;

    for (uint32_t i = 0; i < 16U; i++)
        w[i] = read_be32(&block[i * 4U]);
    for (uint32_t i = 16U; i < 64U; i++)
        w[i] = SSIG1(w[i - 2U]) + w[i - 7U] + SSIG0(w[i - 15U]) + w[i - 16U];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0; i < 64U; i++) {
        uint32_t t1 = h + BSIG1(e) + CH(e, f, g) + k_sha256[i] + w[i];
        uint32_t t2 = BSIG0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

void SolarCleanSha256_Init(T_SolarCleanSha256Ctx *ctx)
{
    if (ctx == NULL)
        return;

    ctx->state[0] = 0x6a09e667UL;
    ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL;
    ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL;
    ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL;
    ctx->state[7] = 0x5be0cd19UL;
    ctx->bitCount = 0U;
    ctx->bufferLen = 0U;
}

void SolarCleanSha256_Update(T_SolarCleanSha256Ctx *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t used = 0U;

    if (ctx == NULL || data == NULL || len == 0U)
        return;

    ctx->bitCount += ((uint64_t)len * 8U);

    while (used < len) {
        uint32_t room = 64U - ctx->bufferLen;
        uint32_t copyLen = len - used;
        if (copyLen > room)
            copyLen = room;

        memcpy(&ctx->buffer[ctx->bufferLen], &data[used], copyLen);
        ctx->bufferLen += copyLen;
        used += copyLen;

        if (ctx->bufferLen == 64U) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bufferLen = 0U;
        }
    }
}

void SolarCleanSha256_Final(T_SolarCleanSha256Ctx *ctx, uint8_t out[SOLARCLEAN_OTA_SHA256_SIZE])
{
    uint8_t pad[64] = {0x80U};
    uint8_t lenBytes[8];
    uint32_t padLen;

    if (ctx == NULL || out == NULL)
        return;

    write_be64(lenBytes, ctx->bitCount);
    padLen = (ctx->bufferLen < 56U) ? (56U - ctx->bufferLen) : (120U - ctx->bufferLen);
    SolarCleanSha256_Update(ctx, pad, padLen);
    SolarCleanSha256_Update(ctx, lenBytes, sizeof(lenBytes));

    for (uint32_t i = 0; i < 8U; i++)
        write_be32(&out[i * 4U], ctx->state[i]);
}

int SolarCleanOtaHash_ParseHex(const char *hex, uint8_t out[SOLARCLEAN_OTA_SHA256_SIZE])
{
    if (hex == NULL || out == NULL)
        return -1;

    for (uint32_t i = 0; i < SOLARCLEAN_OTA_SHA256_HEX_SIZE; i++) {
        if (hex[i] == '\0')
            return -1;
    }
    if (hex[SOLARCLEAN_OTA_SHA256_HEX_SIZE] != '\0')
        return -1;

    for (uint32_t i = 0; i < SOLARCLEAN_OTA_SHA256_SIZE; i++) {
        int hi = hex_nibble(hex[i * 2U]);
        int lo = hex_nibble(hex[i * 2U + 1U]);
        if (hi < 0 || lo < 0)
            return -1;
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return 0;
}

int SolarCleanOtaHash_ToHex(const uint8_t hash[SOLARCLEAN_OTA_SHA256_SIZE],
                            char out[SOLARCLEAN_OTA_SHA256_HEX_SIZE + 1U])
{
    static const char hexDigits[] = "0123456789abcdef";

    if (hash == NULL || out == NULL)
        return -1;

    for (uint32_t i = 0; i < SOLARCLEAN_OTA_SHA256_SIZE; i++) {
        out[i * 2U] = hexDigits[hash[i] >> 4];
        out[i * 2U + 1U] = hexDigits[hash[i] & 0x0FU];
    }
    out[SOLARCLEAN_OTA_SHA256_HEX_SIZE] = '\0';
    return 0;
}

int SolarCleanOtaHash_VerifySlot(E_SolarCleanOtaSlot slot,
                                 uint32_t imageSize,
                                 const uint8_t expected[SOLARCLEAN_OTA_SHA256_SIZE])
{
    T_SolarCleanSha256Ctx ctx;
    uint8_t actual[SOLARCLEAN_OTA_SHA256_SIZE];
    uint32_t offset = 0U;

    if (expected == NULL ||
        imageSize == 0U ||
        imageSize > SolarCleanOtaFlash_GetSlotSize(slot))
        return -1;

    SolarCleanSha256_Init(&ctx);
    while (offset < imageSize) {
        uint32_t chunk = imageSize - offset;
        if (chunk > 1024U)
            chunk = 1024U;
        SolarCleanSha256_Update(&ctx,
                                (const uint8_t *)(SolarCleanOtaFlash_GetSlotBase(slot) + offset),
                                chunk);
        offset += chunk;
    }
    SolarCleanSha256_Final(&ctx, actual);

    return (memcmp(actual, expected, sizeof(actual)) == 0) ? 0 : -1;
}

int SolarCleanOtaHash_VerifySlotB(uint32_t imageSize,
                                  const uint8_t expected[SOLARCLEAN_OTA_SHA256_SIZE])
{
    return SolarCleanOtaHash_VerifySlot(SOLARCLEAN_OTA_SLOT_B, imageSize, expected);
}
