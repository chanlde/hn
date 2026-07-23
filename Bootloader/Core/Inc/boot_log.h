#ifndef BOOT_LOG_H
#define BOOT_LOG_H

#include <stdint.h>

void BootLog_Init(void);
void BootLog_Write(const char *s);
void BootLog_WriteDec(uint32_t value);
void BootLog_WriteHex32(uint32_t value);
void BootLog_Line(const char *s);
void BootLog_Flush(void);

#endif /* BOOT_LOG_H */
