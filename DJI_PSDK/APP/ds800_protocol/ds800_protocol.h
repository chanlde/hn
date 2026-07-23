#ifndef DS800_PROTOCOL_H
#define DS800_PROTOCOL_H

#include <stdint.h>

/*
 * DS800 protocol debug switches.
 *
 * DS800_ENABLE_FLOW_LOG:
 *   1U = print detailed workflow logs, including pressure/pump/swing/fixed calls.
 *   0U = disable detailed workflow logs. Basic SBUS status and state logs remain.
 *
 * DS800_ENABLE_CHANNEL_TRACE:
 *   1U = print mapped channel changes such as CH3/VRA and CH5/A.
 *   0U = disable raw channel-change trace logs.
 *
 * DS800_LOG_SBUS_WAIT_STATUS:
 *   1U = print periodic no_data/sync_wait SBUS diagnostics.
 *   0U = keep serial output quiet when the remote receiver is disconnected.
 *
 * DS800_ENABLE_UART2_LOOPBACK_TEST:
 *   1 = temporary USART2 TX/RX short test. Short PA2(TX) to PA3(RX).
 *   0U = normal DS800 SBUS receive mode.
 *
 * DS800_ENABLE_PWM_SWITCH_TEST:
 *   1U = temporary F-08A single PWM switch mode. Connect CH5 S to PC7/USART6_RX.
 *   0U = normal DS800 SBUS mode.
 *
 * Keep DS800 protocol debug macros in this header so field builds can disable
 * extra logs from one place without editing the protocol implementation.
 */
#define DS800_ENABLE_FLOW_LOG 0U
#define DS800_ENABLE_CHANNEL_TRACE 0U
#define DS800_LOG_SBUS_WAIT_STATUS 0U
#define DS800_ENABLE_UART2_LOOPBACK_TEST 0U
#define DS800_ENABLE_PWM_SWITCH_TEST 0U

/*
 * DS800 persistent control settings.
 *
 * Stored inside the application parameter flash sector after the OTA state
 * record. OTA state save code preserves this block.
 */
#define DS800_PARAM_STORE_OFFSET 0x00001000UL

#ifdef __cplusplus
extern "C" {
#endif

void Ds800Protocol_Init(void);
void Ds800Protocol_Service(void);
uint8_t Ds800Protocol_GetFailsafeHoldEnabled(void);
int Ds800Protocol_SetFailsafeHoldEnabled(uint8_t enabled, uint8_t saveNow);
uint8_t Ds800Protocol_GetRemoteControlEnabled(void);
int Ds800Protocol_SetRemoteControlEnabled(uint8_t enabled, uint8_t saveNow);
uint8_t Ds800Protocol_GetFourGEnabled(void);
int Ds800Protocol_SetFourGEnabled(uint8_t enabled, uint8_t saveNow);
uint8_t Ds800Protocol_GetPsdkEnabled(void);
int Ds800Protocol_SetPsdkEnabled(uint8_t enabled, uint8_t saveNow);

#ifdef __cplusplus
}
#endif

#endif /* DS800_PROTOCOL_H */
