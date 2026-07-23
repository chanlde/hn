/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * File Name          : freertos.c
 * Description        : Code for freertos applications
 ******************************************************************************
 * @attention
 *
 * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.</center></h2>
 *
 * This software component is licensed by ST under Ultimate Liberty license
 * SLA0044, the "License"; You may not use this file except in compliance with
 * the License. You may obtain a copy of the License at:
 *                             www.st.com/SLA0044
 *
 ******************************************************************************
 */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "application.h"
#include "led.h"
#include "pwm_swing.h"
#include "uart.h"
#include "mqtt_app_config.h"
#include "air780e_power.h"
#include "air780e_mqtt.h"
#include "air780e_ppp.h"
#include "mqtt_task.h"
#include "mqtt_cmd_handler.h"
#include "mqtt_state_publisher.h"
#include "ds800_protocol.h"
#include "serial_ota.h"
#include "usb_device.h"
#include "usbd_cdc_vcp.h"
#include "flash_if.h"
#include "uart.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "usart.h"
#include "stm32h7xx_hal_uart.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/**
 * Skip PSDK aircraft link task（一般保持 0；仅桌面测 MQTT 无飞机时可设 1）。
 * MDK：C/C++ → Preprocessor → SKIP_PSDK_START_TASK=1
 */
#ifndef SKIP_PSDK_START_TASK
#define SKIP_PSDK_START_TASK 0
#endif

/* 1: boot-time B-slot first-sector write test. This erases APPLICATION_STORE_ADDRESS first sector only. */
#ifndef FLASH_B_TEST_ON_BOOT
#define FLASH_B_TEST_ON_BOOT 0
#endif

/* 1: halt after the B-slot test so J-Link can inspect APPLICATION_STORE_ADDRESS immediately. */
#ifndef FLASH_B_TEST_HALT_AFTER
#define FLASH_B_TEST_HALT_AFTER 1
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
#if (MQTT_DEFER_UNTIL_PSDK_READY && SKIP_PSDK_START_TASK)
/* 未启动 PSDK 时无法置位 ready：自动不等待，避免 defaultTask 死等 */
#undef MQTT_DEFER_UNTIL_PSDK_READY
#define MQTT_DEFER_UNTIL_PSDK_READY 0
#endif

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* Private constants ---------------------------------------------------------*/
#define BSP_INIT_TASK_STACK_SIZE 512
#define BSP_INIT_TASK_PRIORITY 0
#define USER_START_TASK_STACK_SIZE 2048
#define USER_START_TASK_PRIORITY 0
#define USER_RUN_INDICATE_TASK_STACK_SIZE 256
#define USER_RUN_INDICATE_TASK_PRIORITY 0
#define LED_HEARTBEAT_TASK_STACK_SIZE 256
#define LED_HEARTBEAT_TASK_PRIORITY 0
#define LED_HEARTBEAT_PULSE_MS 80U
#define LED_HEARTBEAT_GAP_MS 120U
#define LED_HEARTBEAT_REST_MS 900U
#define DEFAULT_TASK_STACK_SIZE 4096
#define DEFAULT_TASK_PRIORITY 0
#define PWM_SWING_TASK_STACK_SIZE 512
#define PWM_SWING_TASK_PRIORITY 1
#define DS800_TASK_STACK_SIZE 512
#define DS800_TASK_PRIORITY 1

/* Private values -------------------------------------------------------------*/
#if !SKIP_PSDK_START_TASK
static TaskHandle_t startTask;
#endif
static TaskHandle_t ledHeartbeatTask;
static TaskHandle_t pwmSwingTask;
static TaskHandle_t ds800Task;

#if MQTT_BOOT_HEAP_LOG
static void mqtt_boot_log_heap(const char *stage)
{
  char buf[140];
  int n;
  if (stage == NULL)
    stage = "?";
  n = snprintf(buf, sizeof(buf), "%s [heap] %s free=%lu\r\n", MQTT_LOG_TAG, stage,
               (unsigned long)xPortGetFreeHeapSize());
  if (n > 0 && n < (int)sizeof(buf))
    (void)UART_Write(UART_NUM_1, (uint8_t *)buf, (uint16_t)n);
}
#else
#define mqtt_boot_log_heap(stage) ((void)0)
#endif

/* USER CODE END Variables */
/* Definitions for defaultTask */
TaskHandle_t defaultTaskHandle;

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void LedHeartbeat_Task(void *argument);
static void Ds800_Task(void *argument);
static void BoardServices_Init(void);
static void StartPsdkTaskIfEnabled(void);
#if 0
static void SolarCleanMqtt_PrepareModemAndConfig(char *deviceId, size_t deviceIdLen,
                                                 char *lifecycleTopic, size_t lifecycleTopicLen);
static void SolarCleanMqtt_WaitPsdkIfNeeded(void);
static void SolarCleanMqtt_StartLocalServices(const char *deviceId);
static void SolarCleanMqtt_ConnectAndStartPublishers(const char *deviceId, const char *lifecycleTopic);
static void SolarCleanMqtt_StopModem(uint8_t *fourGRunning);
static void SolarCleanMqtt_StartModemIfNeeded(char *deviceId, size_t deviceIdLen,
                                             char *lifecycleTopic, size_t lifecycleTopicLen,
                                             uint8_t *fourGRunning);
static void SolarCleanMqtt_ServiceLoop(char *deviceId, size_t deviceIdLen,
                                       char *lifecycleTopic, size_t lifecycleTopicLen,
                                       uint8_t *fourGRunning);
#endif
static void FlashB_TestOnBootIfEnabled(void);

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void configureTimerForRunTimeStats(void);
unsigned long getRunTimeCounterValue(void);
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);
void vApplicationMallocFailedHook(void);

/* USER CODE BEGIN 1 */
/* Functions needed when configGENERATE_RUN_TIME_STATS is on */
void configureTimerForRunTimeStats(void)
{
  /* 启用DWT (Data Watchpoint and Trace) */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

  /* 先禁用CYCCNT（保险） */
  DWT->CTRL &= ~DWT_CTRL_CYCCNTENA_Msk;

  /* 清零计数器 */
  DWT->CYCCNT = 0;

  /* 启用CYCCNT (Cycle Counter) */
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

unsigned long getRunTimeCounterValue(void)
{
  return DWT->CYCCNT;
}
/* USER CODE END 1 */

/* USER CODE BEGIN 4 */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
  char line[96];
  int n;
  (void)xTask;

  taskDISABLE_INTERRUPTS();
  n = snprintf(line, sizeof(line), "\r\n*** stack overflow task=%s ***\r\n",
               (pcTaskName != NULL) ? (const char *)pcTaskName : "?");
  if (n > 0 && n < (int)sizeof(line))
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)n, 200);

  for (;;)
  {
  }
}
/* USER CODE END 4 */

/* USER CODE BEGIN 5 */
void vApplicationMallocFailedHook(void)
{
  char line[96];
  int n;

  taskDISABLE_INTERRUPTS();
  n = snprintf(line, sizeof(line), "\r\n*** malloc failed free_heap=%lu ***\r\n",
               (unsigned long)xPortGetFreeHeapSize());
  if (n > 0 && n < (int)sizeof(line))
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)line, (uint16_t)n, 200);

  for (;;)
  {
  }
}
/* USER CODE END 5 */

/**
 * @brief  FreeRTOS initialization
 * @param  None
 * @retval None
 */
void MX_FREERTOS_Init(void)
{
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* USER CODE BEGIN RTOS_THREADS */
  /* Create the thread(s) */
  /* creation of defaultTask */
  if (xTaskCreate((TaskFunction_t)StartDefaultTask, "defaultTask", DEFAULT_TASK_STACK_SIZE,
                  NULL, DEFAULT_TASK_PRIORITY, &defaultTaskHandle) != pdPASS) {
    const char *m = "\r\n*** xTaskCreate defaultTask FAILED ***\r\n";
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)m, (uint16_t)strlen(m), 200);
  }

  /* Create LED heartbeat task. Keep it independent from PSDK so MQTT-only tests still show life. */
  if (xTaskCreate((TaskFunction_t)LedHeartbeat_Task, "led_hb", LED_HEARTBEAT_TASK_STACK_SIZE,
                  NULL, LED_HEARTBEAT_TASK_PRIORITY, &ledHeartbeatTask) != pdPASS) {
    const char *m = "\r\n*** xTaskCreate led_hb FAILED ***\r\n";
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)m, (uint16_t)strlen(m), 200);
  }

  /* Create PWM swing task */
  if (xTaskCreate((TaskFunction_t)PwmSwing_Task, "pwm_swing_task", PWM_SWING_TASK_STACK_SIZE,
                  NULL, PWM_SWING_TASK_PRIORITY, (TaskHandle_t *)&pwmSwingTask) != pdPASS) {
    const char *m = "\r\n*** xTaskCreate pwm_swing_task FAILED ***\r\n";
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)m, (uint16_t)strlen(m), 200);
  }

  if (xTaskCreate((TaskFunction_t)Ds800_Task, "ds800_task", DS800_TASK_STACK_SIZE,
                  NULL, DS800_TASK_PRIORITY, (TaskHandle_t *)&ds800Task) != pdPASS) {
    const char *m = "\r\n*** xTaskCreate ds800_task FAILED ***\r\n";
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)m, (uint16_t)strlen(m), 200);
  }
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */
}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  LED heartbeat thread.
 * @param  argument: Not used
 * @retval None
 */
static void LedHeartbeat_Task(void *argument)
{
  (void)argument;

  Led_Init(LED1);

  for (;;) {
    Led_On(LED1);
    osDelay(LED_HEARTBEAT_PULSE_MS);
    Led_Off(LED1);
    osDelay(LED_HEARTBEAT_GAP_MS);
    Led_On(LED1);
    osDelay(LED_HEARTBEAT_PULSE_MS);
    Led_Off(LED1);
    osDelay(LED_HEARTBEAT_REST_MS);
  }
}

static void Ds800_Task(void *argument)
{
  (void)argument;

  osDelay(500);
  Ds800Protocol_Init();

  for (;;) {
    Ds800Protocol_Service();
    osDelay(20);
  }
}

/**
 * @brief  Initialize board-level services used by this task.
 * @note   Keep hardware bring-up here so MQTT setup code stays focused on network logic.
 */
static void BoardServices_Init(void)
{
  USBD_CDC_VCP_Init();
  MX_USB_DEVICE_Init();
  SerialOta_Init();
  DevCtrl_Init();

  UART_Init(DJI_CONSOLE_UART_NUM, DJI_CONSOLE_UART_BAUD);
#if DS800_ENABLE_PWM_SWITCH_TEST
  /* F-08A temporary PWM switch mode uses PC7/USART6_RX as GPIO EXTI, no SBUS UART needed. */
#elif DS800_ENABLE_UART2_LOOPBACK_TEST
  UART_InitSbus(UART_NUM_2, 0U);
#else
  UART_InitSbus(UART_NUM_6, 1U);
#endif
  UART_Init(UART_NUM_7, MQTT_AIR780E_UART_BAUD);
}

static void StartPsdkTaskIfEnabled(void)
{
#if !SKIP_PSDK_START_TASK && !DS800_ENABLE_UART2_LOOPBACK_TEST
  if (!Ds800Protocol_GetPsdkEnabled()) {
    const char *m = "\r\n[PSDK] disabled by device param, skip start_task\r\n";
    (void)HAL_UART_Transmit(&huart1, (uint8_t *)m, (uint16_t)strlen(m), 200);
    return;
  }
  if (startTask == NULL) {
    if (xTaskCreate((TaskFunction_t)DjiUser_StartTask, "start_task", USER_START_TASK_STACK_SIZE,
                    NULL, USER_START_TASK_PRIORITY, &startTask) != pdPASS) {
      const char *m = "\r\n*** xTaskCreate start_task FAILED ***\r\n";
      (void)HAL_UART_Transmit(&huart1, (uint8_t *)m, (uint16_t)strlen(m), 200);
    }
  }
#endif
}

#if FLASH_B_TEST_ON_BOOT
static void FlashB_TestLog(const char *msg)
{
  if (msg == NULL)
    return;
  UART_Write(UART_NUM_1, (const uint8_t *)msg, (uint16_t)strlen(msg));
}

static void FlashB_TestLogf(const char *fmt, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
  char line[160];
  int n = snprintf(line, sizeof(line), fmt,
                   (unsigned long)a, (unsigned long)b,
                   (unsigned long)c, (unsigned long)d);
  if (n > 0 && n < (int)sizeof(line))
    UART_Write(UART_NUM_1, (const uint8_t *)line, (uint16_t)n);
}
#endif

static void FlashB_TestOnBootIfEnabled(void)
{
#if FLASH_B_TEST_ON_BOOT
  static const uint32_t testWords[8] = {
    0x53425431UL, /* "SBT1" */
    APPLICATION_STORE_ADDRESS,
    0x12345678UL,
    0xA5A55A5AUL,
    0x00000001UL,
    0x00000002UL,
    0x00000003UL,
    0x00000004UL,
  };
  uint32_t base = APPLICATION_STORE_ADDRESS;
  uint32_t eraseEnd = APPLICATION_STORE_ADDRESS + FLASH_SECTOR_SIZE - 1U;
  uint32_t w0;
  uint32_t w1;
  uint32_t w2;
  uint32_t w3;

  FlashB_TestLog("\r\n[FLASH_TEST] B slot write test start\r\n");
  FlashB_TestLogf("[FLASH_TEST] erase 0x%08lX-0x%08lX\r\n", base, eraseEnd, 0U, 0U);
  if (FLASH_If_Erase(base, eraseEnd) != FLASHIF_OK) {
    FlashB_TestLog("[FLASH_TEST] erase FAIL\r\n");
    goto done;
  }
  FlashB_TestLog("[FLASH_TEST] erase OK\r\n");

  if (FLASH_If_Write(base, (const uint8_t *)testWords, sizeof(testWords)) != FLASHIF_OK) {
    FlashB_TestLog("[FLASH_TEST] write FAIL\r\n");
    goto done;
  }
  FlashB_TestLog("[FLASH_TEST] write OK\r\n");

  SCB_InvalidateDCache_by_Addr((uint32_t *)base, 32);
  __DSB();
  __ISB();
  w0 = *(volatile uint32_t *)(base + 0U);
  w1 = *(volatile uint32_t *)(base + 4U);
  w2 = *(volatile uint32_t *)(base + 8U);
  w3 = *(volatile uint32_t *)(base + 12U);
  FlashB_TestLogf("[FLASH_TEST] probe 0x%08lX: %08lX %08lX %08lX\r\n", base, w0, w1, w2);
  FlashB_TestLogf("[FLASH_TEST] probe +12: %08lX\r\n", w3, 0U, 0U, 0U);

  if (memcmp((const void *)base, testWords, sizeof(testWords)) == 0)
    FlashB_TestLog("[FLASH_TEST] verify OK\r\n");
  else
    FlashB_TestLog("[FLASH_TEST] verify FAIL\r\n");

done:
#if FLASH_B_TEST_HALT_AFTER
  FlashB_TestLogf("[FLASH_TEST] halted, read with J-Link: mem32 0x%08lX 8\r\n", base, 0U, 0U, 0U);
  for (;;) {
    osDelay(1000U);
  }
#endif
#endif
}

/**
 * @brief  Power on Air780E and prepare MQTT identity/topics.
 * @note   This function only configures local driver state; it does not connect to broker yet.
 */
#if 0
static void SolarCleanMqtt_PrepareModemAndConfig(char *deviceId, size_t deviceIdLen,
                                                 char *lifecycleTopic, size_t lifecycleTopicLen)
{
  Air780ePower_Init();
  Air780ePower_On();
  osDelay(MQTT_POWER_ON_DELAY_MS);
  (void)Air780eMqtt_AtSelfTest(5U, 1000U, 1000U);

  Air780eMqtt_SetBroker(MQTT_BROKER_IP, MQTT_BROKER_PORT);
  Air780eMqtt_SetAuth(MQTT_USERNAME, MQTT_PASSWORD);

  MqttAppConfig_BuildDeviceId(deviceId, deviceIdLen,
                              HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2());
  {
    char idMsg[96];
    (void)snprintf(idMsg, sizeof(idMsg), "%s deviceId=%s\r\n", MQTT_LOG_TAG, deviceId);
    UART_Write(UART_NUM_1, (uint8_t *)idMsg, (uint16_t)strlen(idMsg));
  }

  Air780eMqtt_SetClientId(deviceId);
  (void)snprintf(lifecycleTopic, lifecycleTopicLen, MQTT_TOPIC_FMT_LIFECYCLE, deviceId);
  Air780eMqtt_SetWill(lifecycleTopic, "offline", 0, 1);

  {
    char topic[MQTT_TOPIC_BUF_LEN];
    (void)snprintf(topic, sizeof(topic), "%s%s", MQTT_TOPIC_PREFIX, deviceId);
    MqttTask_SetTopic(topic);
  }

  mqtt_boot_log_heap("after Air780E cfg (before MQTT tasks)");
}

/**
 * @brief  Optionally wait until PSDK startup is complete before MQTT starts.
 * @note   A timeout keeps 4G/MQTT from being blocked forever when aircraft link is absent.
 */
static void SolarCleanMqtt_WaitPsdkIfNeeded(void)
{
#if MQTT_DEFER_UNTIL_PSDK_READY
  if (!Ds800Protocol_GetPsdkEnabled()) {
    return;
  }
  {
#if MQTT_WAIT_PSDK_LOG_MS > 0U
    char wmsg[160];
#endif
    {
      TickType_t waitStart = xTaskGetTickCount();
#if MQTT_WAIT_PSDK_LOG_MS > 0U
      TickType_t lastLog = 0;
#endif
      for (;;) {
        TickType_t now;
        if (DjiUser_IsApplicationStarted())
          break;
        now = xTaskGetTickCount();
#if MQTT_WAIT_PSDK_TIMEOUT_MS > 0U
        if ((now - waitStart) >= (TickType_t)pdMS_TO_TICKS(MQTT_WAIT_PSDK_TIMEOUT_MS)) {
          char tmsg[128];
          (void)snprintf(tmsg, sizeof(tmsg), "%s PSDK wait timeout, starting MQTT anyway\r\n", MQTT_LOG_TAG);
          UART_Write(UART_NUM_1, (uint8_t *)tmsg, (uint16_t)strlen(tmsg));
          break;
        }
#endif
#if MQTT_WAIT_PSDK_LOG_MS > 0U
        if (MQTT_WAIT_PSDK_LOG_MS > 0U) {
          if (lastLog == 0 || (now - lastLog) >= (TickType_t)pdMS_TO_TICKS(MQTT_WAIT_PSDK_LOG_MS)) {
            lastLog = now;
            (void)snprintf(wmsg, sizeof(wmsg),
                           "%s waiting PSDK tick=%lu free=%lu\r\n", MQTT_LOG_TAG,
                           (unsigned long)now, (unsigned long)xPortGetFreeHeapSize());
            UART_Write(UART_NUM_1, (uint8_t *)wmsg, (uint16_t)strlen(wmsg));
          }
        }
#endif
        osDelay(50);
      }
    }
    mqtt_boot_log_heap("creating MQTT tasks/queues");
  }
#endif
}

/**
 * @brief  Start MCU-side MQTT helpers.
 * @note   Command handling and publish queues are prepared before broker connection.
 */
static void SolarCleanMqtt_StartLocalServices(const char *deviceId)
{
  MqttCmdHandler_Init(deviceId);
  mqtt_boot_log_heap("after MqttCmdHandler_Init");

  MqttTask_Start();
  mqtt_boot_log_heap("after MqttTask_Start");
}

/**
 * @brief  Connect to MQTT broker and publish initial online state.
 */
static void SolarCleanMqtt_ConnectAndStartPublishers(const char *deviceId, const char *lifecycleTopic)
{
  Air780eMqtt_EnableLog(MQTT_DEBUG_AT_MQTT_DRIVER_LOG);
  osDelay(MQTT_STARTUP_DELAY_MS);
  mqtt_boot_log_heap("after MQTT_STARTUP_DELAY");

  if (Air780eMqtt_Connect() == 0) {
    char onlineMsg[64];
    char okMsg[96];
    (void)snprintf(onlineMsg, sizeof(onlineMsg), "{\"v\":1,\"type\":\"online\",\"ts\":%lu}",
                   (unsigned long)osKernelGetTickCount());
    (void)snprintf(okMsg, sizeof(okMsg), "\r\n%s connect OK\r\n", MQTT_LOG_TAG);
    UART_Write(UART_NUM_1, (uint8_t *)okMsg, (uint16_t)strlen(okMsg));

#if MQTT_DEBUG_STARTUP_TEST_PUBLISH
    {
      char mqttTopic[MQTT_TOPIC_BUF_LEN];
      char payload[128];
      char logMsg[192];
      (void)snprintf(mqttTopic, sizeof(mqttTopic), "%s%s", MQTT_TOPIC_PREFIX, deviceId);
      (void)snprintf(payload, sizeof(payload), MQTT_STARTUP_TEST_PAYLOAD_FMT,
                     (unsigned long)osKernelGetTickCount());
      if (Air780eMqtt_Publish(mqttTopic, payload) == 0) {
        (void)snprintf(logMsg, sizeof(logMsg), "%s startup test publish OK topic=%s\r\n", MQTT_LOG_TAG,
                       mqttTopic);
      } else {
        (void)snprintf(logMsg, sizeof(logMsg), "%s startup test publish FAIL err=%d (%s)\r\n",
                       MQTT_LOG_TAG, Air780eMqtt_GetLastError(), Air780eMqtt_GetLastErrorString());
      }
      UART_Write(UART_NUM_1, (uint8_t *)logMsg, (uint16_t)strlen(logMsg));
    }
#endif

    mqtt_boot_log_heap("after Air780eMqtt_Connect OK");
    MqttCmdHandler_OnTcpConnected();
    (void)Air780eMqtt_PublishRetain(lifecycleTopic, onlineMsg, 1);
    MqttStatePublisher_Start(deviceId);
    mqtt_boot_log_heap("after MqttStatePublisher_Start");
  } else {
    char fail[160];
    (void)snprintf(fail, sizeof(fail), "%s startup connect FAIL err=%d connack_rc=%d (%s)\r\n",
                   MQTT_LOG_TAG, Air780eMqtt_GetLastError(), Air780eMqtt_GetLastConnackRc(),
                   Air780eMqtt_GetLastErrorString());
    UART_Write(UART_NUM_1, (uint8_t *)fail, (uint16_t)strlen(fail));
  }

  {
    const char *banner = "\r\n=== MQTT ready ===\r\n";
    UART_Write(UART_NUM_1, (uint8_t *)banner, (uint16_t)strlen(banner));
  }
}

static void SolarCleanMqtt_StopModem(uint8_t *fourGRunning)
{
  if (fourGRunning != NULL && *fourGRunning) {
    const char *m = "\r\n[MQTT] 4G disabled, disconnect and power off AIR780E\r\n";
    UART_Write(UART_NUM_1, (uint8_t *)m, (uint16_t)strlen(m));
    Air780eMqtt_Disconnect();
    Air780ePower_Off();
    *fourGRunning = 0U;
  }
}

static void SolarCleanMqtt_StartModemIfNeeded(char *deviceId, size_t deviceIdLen,
                                             char *lifecycleTopic, size_t lifecycleTopicLen,
                                             uint8_t *fourGRunning)
{
  if (fourGRunning != NULL && *fourGRunning) {
    return;
  }

  SolarCleanMqtt_PrepareModemAndConfig(deviceId, deviceIdLen, lifecycleTopic, lifecycleTopicLen);
  SolarCleanMqtt_WaitPsdkIfNeeded();
  SolarCleanMqtt_StartLocalServices(deviceId);
  SolarCleanMqtt_ConnectAndStartPublishers(deviceId, lifecycleTopic);
  if (fourGRunning != NULL) {
    *fourGRunning = 1U;
  }
}

/**
 * @brief  Periodic MQTT maintenance loop.
 * @note   Keep this small; long operations should live in their own module/task.
 */
static void SolarCleanMqtt_ServiceLoop(char *deviceId, size_t deviceIdLen,
                                       char *lifecycleTopic, size_t lifecycleTopicLen,
                                       uint8_t *fourGRunning)
{
  uint32_t hbCnt = 0;

  for (;;) {
    if (!Ds800Protocol_GetFourGEnabled()) {
      SolarCleanMqtt_StopModem(fourGRunning);
      osDelay(1000U);
      continue;
    }

    SolarCleanMqtt_StartModemIfNeeded(deviceId, deviceIdLen, lifecycleTopic, lifecycleTopicLen, fourGRunning);

    Air780eMqtt_Service();
    hbCnt++;
    if (((hbCnt % (uint32_t)MQTT_PINGREQ_PERIOD_S) == 0U) && Air780eMqtt_IsTcpConnected()) {
      Air780eMqtt_Pingreq();
    }
#if (MQTT_HEARTBEAT_LOG_S > 0U)
    if ((hbCnt % (uint32_t)MQTT_HEARTBEAT_LOG_S) == 0U) {
      char hbMsg[224];
      (void)snprintf(hbMsg, sizeof(hbMsg),
                     "%s tcp=%d err=%d (%s) uart7_line_err=%lu srv_miss=%lu\r\n",
                     MQTT_LOG_TAG,
                     Air780eMqtt_IsTcpConnected() ? 1 : 0,
                     Air780eMqtt_GetLastError(),
                     Air780eMqtt_GetLastErrorString(),
                     (unsigned long)UART_GetUart7LineErrorClears(),
                     (unsigned long)Air780eMqtt_GetServiceLockMissCount());
      UART_Write(UART_NUM_1, (uint8_t *)hbMsg, (uint16_t)strlen(hbMsg));
    }
#endif

    osDelay(MQTT_DEFAULT_TASK_DELAY_MS);
  }
}
#endif

/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  (void)argument;

  BoardServices_Init();
  if (SerialOta_WaitEnterWindow(SERIAL_OTA_ENTER_WINDOW_MS)) {
    SerialOta_ServiceLoop();
  }
  StartPsdkTaskIfEnabled();
  FlashB_TestOnBootIfEnabled();
  Air780ePpp_Run();
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
