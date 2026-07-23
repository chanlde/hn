#include "boot_sha256.h"

#include <string.h>

#define ROTR32(x, n) (((x) >> (n)) | ((x) << (32UL - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define BSIG0(x)     (ROTR32((x), 2UL) ^ ROTR32((x), 13UL) ^ ROTR32((x), 22UL))
#define BSIG1(x)     (ROTR32((x), 6UL) ^ ROTR32((x), 11UL) ^ ROTR32((x), 25UL))
#define SSIG0(x)     (ROTR32((x), 7UL) ^ ROTR32((x), 18UL) ^ ((x) >> 3UL))
#define SSIG1(x)     (ROTR32((x), 17UL) ^ ROTR32((x), 19UL) ^ ((x) >> 10UL))

typedef struct {
    uint32_t state[8];
    uint64_t bitCount;
    uint8_t buffer[64];
    uint32_t bufferLen;
} T_BootSha256Ctx;

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
    for (uint32_t i = 0UL; i < 8UL; i++)
        p[i] = (uint8_t)(v >> (56UL - (i * 8UL)));
}

static void sha256_transform(T_BootSha256Ctx *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    uint32_t a, b, c, d, e, f, g, h;

    for (uint32_t i = 0UL; i < 16UL; i++)
        w[i] = read_be32(&block[i * 4UL]);
    for (uint32_t i = 16UL; i < 64UL; i++)
        w[i] = SSIG1(w[i - 2UL]) + w[i - 7UL] + SSIG0(w[i - 15UL]) + w[i - 16UL];

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (uint32_t i = 0UL; i < 64UL; i++) {
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

static void sha256_init(T_BootSha256Ctx *ctx)
{
    ctx->state[0] = 0x6a09e667UL;
    ctx->state[1] = 0xbb67ae85UL;
    ctx->state[2] = 0x3c6ef372UL;
    ctx->state[3] = 0xa54ff53aUL;
    ctx->state[4] = 0x510e527fUL;
    ctx->state[5] = 0x9b05688cUL;
    ctx->state[6] = 0x1f83d9abUL;
    ctx->state[7] = 0x5be0cd19UL;
    ctx->bitCount = 0UL;
    ctx->bufferLen = 0UL;
}

static void sha256_update(T_BootSha256Ctx *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t used = 0UL;

    ctx->bitCount += ((uint64_t)len * 8UL);
    while (used < len) {
        uint32_t room = 64UL - ctx->bufferLen;
        uint32_t copyLen = len - used;
        if (copyLen > room)
            copyLen = room;

        memcpy(&ctx->buffer[ctx->bufferLen], &data[used], copyLen);
        ctx->bufferLen += copyLen;
        used += copyLen;

        if (ctx->bufferLen == 64UL) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bufferLen = 0UL;
        }
    }
}

static void sha256_final(T_BootSha256Ctx *ctx, uint8_t out[BOOT_SHA256_SIZE])
{
    uint8_t pad[64] = {0x80U};
    uint8_t lenBytes[8];
    uint32_t padLen;

    write_be64(lenBytes, ctx->bitCount);
    padLen = (ctx->bufferLen < 56UL) ? (56UL - ctx->bufferLen) : (120UL - ctx->bufferLen);
    sha256_update(ctx, pad, padLen);
    sha256_update(ctx, lenBytes, sizeof(lenBytes));

    for (uint32_t i = 0UL; i < 8UL; i++)
        write_be32(&out[i * 4UL], ctx->state[i]);
}

int BootSha256_VerifyMemory(uint32_t address, uint32_t len, const uint8_t expected[BOOT_SHA256_SIZE])
{
    T_BootSha256Ctx ctx;
    uint8_t actual[BOOT_SHA256_SIZE];
    uint32_t offset = 0UL;

    if (expected == NULL || len == 0UL)
        return -1;

    sha256_init(&ctx);
    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > 1024UL)
            chunk = 1024UL;
        sha256_update(&ctx, (const uint8_t *)(address + offset), chunk);
        offset += chunk;
    }
    sha256_final(&ctx, actual);

    return (memcmp(actual, expected, BOOT_SHA256_SIZE) == 0) ? 0 : -1;
}
