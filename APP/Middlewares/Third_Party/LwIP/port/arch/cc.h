#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

#include <stdint.h>
#include <stdio.h>

typedef uint8_t  u8_t;
typedef int8_t   s8_t;
typedef uint16_t u16_t;
typedef int16_t  s16_t;
typedef uint32_t u32_t;
typedef int32_t  s32_t;
typedef uintptr_t mem_ptr_t;

u32_t sys_now(void);
u32_t sys_jiffies(void);

#define U16_F "hu"
#define S16_F "hd"
#define X16_F "hx"
#define U32_F "lu"
#define S32_F "ld"
#define X32_F "lx"
#define SZT_F "lu"

#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); for (;;) {} } while (0)
#define LWIP_PLATFORM_DIAG(x)   do { printf x; } while (0)

#endif /* LWIP_ARCH_CC_H */
