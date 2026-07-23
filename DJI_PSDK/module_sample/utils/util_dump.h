/**
 * @file util_dump.h
 * @brief Optional hex dump for serial debug (no-op when UTIL_DUMP_ENABLE is 0).
 */
#ifndef UTIL_DUMP_H
#define UTIL_DUMP_H

#include <stdint.h>

#ifndef UTIL_DUMP_ENABLE
#define UTIL_DUMP_ENABLE 0
#endif

#if UTIL_DUMP_ENABLE
void Util_DumpHexAndAscii(const char *title, const uint8_t *data, uint32_t len);
#else
static inline void Util_DumpHexAndAscii(const char *title, const uint8_t *data, uint32_t len)
{
    (void)title;
    (void)data;
    (void)len;
}
#endif

#endif
