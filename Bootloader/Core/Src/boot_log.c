#include "boot_log.h"

#include "stm32h743xx.h"

#define BOOT_LOG_BAUD        115200UL
#define BOOT_LOG_USART_PCLK  64000000UL
#define BOOT_LOG_TIMEOUT     1000000UL

static int s_bootLogReady;

static int boot_log_wait_flag(uint32_t flag)
{
    uint32_t timeout = BOOT_LOG_TIMEOUT;

    while ((USART1->ISR & flag) == 0UL) {
        if (timeout-- == 0UL)
            return -1;
    }
    return 0;
}

static void boot_log_putc(char c)
{
    if (!s_bootLogReady)
        return;
    if (boot_log_wait_flag(USART_ISR_TXE_TXFNF) != 0)
        return;
    USART1->TDR = (uint32_t)(uint8_t)c;
}

void BootLog_Init(void)
{
    uint32_t tmp;

    RCC->AHB4ENR |= RCC_AHB4ENR_GPIOAEN;
    tmp = RCC->AHB4ENR;
    (void)tmp;

    RCC->D2CCIP2R &= ~RCC_D2CCIP2R_USART16SEL;
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;
    tmp = RCC->APB2ENR;
    (void)tmp;

    USART1->CR1 = 0UL;
    USART1->CR2 = 0UL;
    USART1->CR3 = 0UL;
    USART1->PRESC = 0UL;

    GPIOA->MODER &= ~((3UL << (9UL * 2UL)) | (3UL << (10UL * 2UL)));
    GPIOA->MODER |= ((2UL << (9UL * 2UL)) | (2UL << (10UL * 2UL)));
    GPIOA->OTYPER &= ~((1UL << 9UL) | (1UL << 10UL));
    GPIOA->OSPEEDR &= ~((3UL << (9UL * 2UL)) | (3UL << (10UL * 2UL)));
    GPIOA->OSPEEDR |= ((2UL << (9UL * 2UL)) | (2UL << (10UL * 2UL)));
    GPIOA->PUPDR &= ~((3UL << (9UL * 2UL)) | (3UL << (10UL * 2UL)));
    GPIOA->AFR[1] &= ~((0xFUL << ((9UL - 8UL) * 4UL)) | (0xFUL << ((10UL - 8UL) * 4UL)));
    GPIOA->AFR[1] |= ((7UL << ((9UL - 8UL) * 4UL)) | (7UL << ((10UL - 8UL) * 4UL)));

    USART1->BRR = (BOOT_LOG_USART_PCLK + (BOOT_LOG_BAUD / 2UL)) / BOOT_LOG_BAUD;
    USART1->ICR = 0xFFFFFFFFUL;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
    (void)boot_log_wait_flag(USART_ISR_TEACK);

    s_bootLogReady = 1;
}

void BootLog_Write(const char *s)
{
    if (s == 0)
        return;
    while (*s != '\0')
        boot_log_putc(*s++);
}

void BootLog_WriteDec(uint32_t value)
{
    char buf[10];
    uint32_t i = 0UL;

    if (value == 0UL) {
        boot_log_putc('0');
        return;
    }

    while (value != 0UL && i < sizeof(buf)) {
        buf[i++] = (char)('0' + (value % 10UL));
        value /= 10UL;
    }
    while (i != 0UL)
        boot_log_putc(buf[--i]);
}

void BootLog_WriteHex32(uint32_t value)
{
    static const char hex[] = "0123456789ABCDEF";

    BootLog_Write("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        boot_log_putc(hex[(value >> (uint32_t)shift) & 0xFUL]);
}

void BootLog_Line(const char *s)
{
    BootLog_Write(s);
    BootLog_Write("\r\n");
}

void BootLog_Flush(void)
{
    if (!s_bootLogReady)
        return;
    (void)boot_log_wait_flag(USART_ISR_TC);
}
