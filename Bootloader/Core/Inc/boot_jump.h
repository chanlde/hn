#ifndef BOOT_JUMP_H
#define BOOT_JUMP_H

#include <stdint.h>

int BootJump_IsAppVectorValid(uint32_t appBase);
void BootJump_JumpToApp(uint32_t appBase);

#endif /* BOOT_JUMP_H */
