#ifndef APP_STRING_H
#define APP_STRING_H

#include <stddef.h>
#include <string.h>

static inline void AppString_Copy(char *dst, size_t dstSize, const char *src)
{
    size_t copyLen;

    if (dst == NULL || dstSize == 0U) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    copyLen = strlen(src);
    if (copyLen > (dstSize - 1U)) {
        copyLen = dstSize - 1U;
    }
    if (copyLen > 0U) {
        (void)memcpy(dst, src, copyLen);
    }
    dst[copyLen] = '\0';
}

static inline void AppString_CopyBytes(char *dst, size_t dstSize, const void *src, size_t srcLen)
{
    size_t copyLen;

    if (dst == NULL || dstSize == 0U) {
        return;
    }
    if (src == NULL) {
        srcLen = 0U;
    }
    copyLen = (srcLen < (dstSize - 1U)) ? srcLen : (dstSize - 1U);
    if (copyLen > 0U) {
        (void)memcpy(dst, src, copyLen);
    }
    dst[copyLen] = '\0';
}

#endif /* APP_STRING_H */
