/**
 ********************************************************************
 * @file    test_widget.c
 * @brief
 *
 * @copyright (c) 2021 DJI. All rights reserved.
 *
 * All information contained herein is, and remains, the property of DJI.
 * The intellectual and technical concepts contained herein are proprietary
 * to DJI and may be covered by U.S. and foreign patents, patents in process,
 * and protected by trade secret or copyright law.  Dissemination of this
 * information, including but not limited to data and other proprietary
 * material(s) incorporated within the information, in any form, is strictly
 * prohibited without the express written consent of DJI.
 *
 * If you receive this source code without DJI's authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "test_widget.h"
#include <dji_widget.h>
#include <dji_logger.h>
#include "../utils/util_misc.h"
#include <dji_platform.h>
#include <stdarg.h>
#include <stdio.h>
#include "dji_sdk_config.h"
#include "file_binary_array_list_en.h"
#include "../../BSP/uart.h"
#include "../../BSP/pwm.h"
#include "../../BSP/pwm_swing.h"
#include "../../APP/mqtt_task/mqtt_app_config.h"
#include "stm32h7xx_hal.h"

/* Private constants ---------------------------------------------------------*/
#define WIDGET_DIR_PATH_LEN_MAX         (256)
#define WIDGET_TASK_STACK_SIZE          (2048)

/* Private types -------------------------------------------------------------*/

/* Private functions declaration ---------------------------------------------*/
static void *DjiTest_WidgetTask(void *arg);
static T_DjiReturnCode DjiTestWidget_SetWidgetValue(E_DjiWidgetType widgetType, uint32_t index, int32_t value,
                                                    void *userData);
static T_DjiReturnCode DjiTestWidget_GetWidgetValue(E_DjiWidgetType widgetType, uint32_t index, int32_t *value,
                                                    void *userData);

/* Private values ------------------------------------------------------------*/
static T_DjiTaskHandle s_widgetTestThread;
static bool s_isWidgetFileDirPathConfigured = false;
static char s_widgetFileDirPath[DJI_FILE_PATH_SIZE_MAX] = {0};

static const T_DjiWidgetHandlerListItem s_widgetHandlerList[] = {
    {0, DJI_WIDGET_TYPE_SWITCH,        DjiTestWidget_SetWidgetValue, DjiTestWidget_GetWidgetValue, NULL},
    {1, DJI_WIDGET_TYPE_SWITCH,        DjiTestWidget_SetWidgetValue, DjiTestWidget_GetWidgetValue, NULL},
    {2, DJI_WIDGET_TYPE_SCALE,         DjiTestWidget_SetWidgetValue, DjiTestWidget_GetWidgetValue, NULL},
    {3, DJI_WIDGET_TYPE_SCALE,         DjiTestWidget_SetWidgetValue, DjiTestWidget_GetWidgetValue, NULL},
    {4, DJI_WIDGET_TYPE_SCALE,         DjiTestWidget_SetWidgetValue, DjiTestWidget_GetWidgetValue, NULL},
};

static const char *s_widgetTypeNameArray[] = {
    "Unknown",
    "Button",
    "Switch",
    "Scale",
    "List",
    "Int input box"
};

static const uint32_t s_widgetHandlerListCount = sizeof(s_widgetHandlerList) / sizeof(T_DjiWidgetHandlerListItem);
static int32_t s_widgetValueList[5] = {
    DJI_WIDGET_SWITCH_STATE_OFF,
    DJI_WIDGET_SWITCH_STATE_OFF,
    0,
    0,
    0,
};
static volatile uint32_t s_lastWidgetActiveMs;

#define WIDGET_IDX_WATER_PUMP           0U
#define WIDGET_IDX_SWING_SWITCH         1U
/* widget_config: pumpPower scale -> WaterPump_SetPressurePercent (PB14/TIM12) */
#define WIDGET_IDX_PWM_PERCENT          2U
#define WIDGET_IDX_SWING_SPEED          3U
#define WIDGET_IDX_SWING_AMPLITUDE      4U

static uint32_t ClampU32(uint32_t value, uint32_t minValue, uint32_t maxValue)
{
    if (value < minValue) {
        return minValue;
    }
    if (value > maxValue) {
        return maxValue;
    }
    return value;
}

static void Widget_UartLogf(const char *fmt, ...)
{
    char line[160];
    va_list ap;
    int n;

    if (fmt == NULL) {
        return;
    }
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    if (n <= 0) {
        return;
    }
    if (n >= (int)sizeof(line)) {
        n = (int)sizeof(line) - 1;
    }
    (void)UART_Write(UART_NUM_1, (const uint8_t *)line, (uint16_t)n);
    (void)UART_Write(UART_NUM_1, (const uint8_t *)"\r\n", 2U);
}

/* Exported functions definition ---------------------------------------------*/
void DjiTest_WidgetMarkActive(void)
{
    s_lastWidgetActiveMs = HAL_GetTick();
}

uint8_t DjiTest_WidgetIsRecentlyActive(uint32_t timeoutMs)
{
    uint32_t lastActiveMs = s_lastWidgetActiveMs;

    if (lastActiveMs == 0U) {
        return 0U;
    }
    return ((HAL_GetTick() - lastActiveMs) <= timeoutMs) ? 1U : 0U;
}

T_DjiReturnCode DjiTest_WidgetStartService(void)
{
    T_DjiReturnCode djiStat;
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();

    //Step 1 : Init DJI Widget
    djiStat = DjiWidget_Init();
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Dji test widget init error, stat = 0x%08llX", djiStat);
        return djiStat;
    }

#ifdef SYSTEM_ARCH_LINUX
    //Step 2 : Set UI Config (Linux environment)
    char curFileDirPath[WIDGET_DIR_PATH_LEN_MAX];
    char tempPath[WIDGET_DIR_PATH_LEN_MAX];
    djiStat = DjiUserUtil_GetCurrentFileDirPath(__FILE__, WIDGET_DIR_PATH_LEN_MAX, curFileDirPath);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Get file current path error, stat = 0x%08llX", djiStat);
        return djiStat;
    }

    if (s_isWidgetFileDirPathConfigured == true) {
        snprintf(tempPath, WIDGET_DIR_PATH_LEN_MAX, "%swidget_file/en_big_screen", s_widgetFileDirPath);
    } else {
        snprintf(tempPath, WIDGET_DIR_PATH_LEN_MAX, "%swidget_file/en_big_screen", curFileDirPath);
    }

    //set default ui config path
    USER_LOG_INFO("widget file: %s", tempPath);
    djiStat = DjiWidget_RegDefaultUiConfigByDirPath(tempPath);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Add default widget ui config error, stat = 0x%08llX", djiStat);
        return djiStat;
    }

    //set ui config for English language
    djiStat = DjiWidget_RegUiConfigByDirPath(DJI_MOBILE_APP_LANGUAGE_ENGLISH,
                                             DJI_MOBILE_APP_SCREEN_TYPE_BIG_SCREEN,
                                             tempPath);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Add widget ui config error, stat = 0x%08llX", djiStat);
        return djiStat;
    }

    //set ui config for Chinese language
    if (s_isWidgetFileDirPathConfigured == true) {
        snprintf(tempPath, WIDGET_DIR_PATH_LEN_MAX, "%swidget_file/cn_big_screen", s_widgetFileDirPath);
    } else {
        snprintf(tempPath, WIDGET_DIR_PATH_LEN_MAX, "%swidget_file/cn_big_screen", curFileDirPath);
    }

    djiStat = DjiWidget_RegUiConfigByDirPath(DJI_MOBILE_APP_LANGUAGE_CHINESE,
                                             DJI_MOBILE_APP_SCREEN_TYPE_BIG_SCREEN,
                                             tempPath);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Add widget ui config error, stat = 0x%08llX", djiStat);
        return djiStat;
    }
#else
    //Step 2 : Set UI Config (RTOS environment)
    T_DjiWidgetBinaryArrayConfig enWidgetBinaryArrayConfig = {
        .binaryArrayCount = g_EnBinaryArrayCount,
        .fileBinaryArrayList = g_EnFileBinaryArrayList
    };
    T_DjiWidgetBinaryArrayConfig cnWidgetBinaryArrayConfig = {
        .binaryArrayCount = g_CnBinaryArrayCount,
        .fileBinaryArrayList = g_CnFileBinaryArrayList
    };

    /* Default = English; Pilot uses system language to pick EN or CN */
    djiStat = DjiWidget_RegDefaultUiConfigByBinaryArray(&enWidgetBinaryArrayConfig);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Add default widget ui config error, stat = 0x%08llX", djiStat);
        return djiStat;
    }
    djiStat = DjiWidget_RegUiConfigByBinaryArray(DJI_MOBILE_APP_LANGUAGE_ENGLISH,
                                                 DJI_MOBILE_APP_SCREEN_TYPE_BIG_SCREEN,
                                                 &enWidgetBinaryArrayConfig);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Add widget ui config (EN) error, stat = 0x%08llX", djiStat);
        return djiStat;
    }
    djiStat = DjiWidget_RegUiConfigByBinaryArray(DJI_MOBILE_APP_LANGUAGE_CHINESE,
                                                 DJI_MOBILE_APP_SCREEN_TYPE_BIG_SCREEN,
                                                 &cnWidgetBinaryArrayConfig);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Add widget ui config (CN) error, stat = 0x%08llX", djiStat);
        return djiStat;
    }
#endif
    //Step 3 : Set widget handler list
    djiStat = DjiWidget_RegHandlerList(s_widgetHandlerList, s_widgetHandlerListCount);
    if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Set widget handler list error, stat = 0x%08llX", djiStat);
        return djiStat;
    }

    /* Initialize outputs to safe defaults and keep software state aligned. */
    WaterPump_Off();
    WaterPump_SetPressurePercent(0U);
    Pwm_SetDutyPercent(PWM_FTS2, 0U);
    PwmSwing_SetAmplitudeDeg(PWM_SWING_AMPLITUDE_DEG_MAX_F);

    //Step 4 : Run widget api sample task
    if (osalHandler->TaskCreate("user_widget_task", DjiTest_WidgetTask, WIDGET_TASK_STACK_SIZE, NULL,
                                &s_widgetTestThread) != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
        USER_LOG_ERROR("Dji widget test task create error.");
        return DJI_ERROR_SYSTEM_MODULE_CODE_UNKNOWN;
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

T_DjiReturnCode DjiTest_WidgetSetConfigFilePath(const char *path)
{
    memset(s_widgetFileDirPath, 0, sizeof(s_widgetFileDirPath));
    memcpy(s_widgetFileDirPath, path, USER_UTIL_MIN(strlen(path), sizeof(s_widgetFileDirPath) - 1));
    s_isWidgetFileDirPathConfigured = true;

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

__attribute__((weak)) void DjiTest_WidgetLogAppend(const char *fmt, ...)
{

}

#ifndef __CC_ARM
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-noreturn"
#pragma GCC diagnostic ignored "-Wreturn-type"
#pragma GCC diagnostic ignored "-Wformat"
#endif

/* Private functions definition-----------------------------------------------*/
static void *DjiTest_WidgetTask(void *arg)
{
    char message[DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN];
    char deviceId[MQTT_CLIENT_ID_BUF_LEN];
    uint32_t sysTimeMs = 0;
    T_DjiReturnCode djiStat;
    T_DjiOsalHandler *osalHandler = DjiPlatform_GetOsalHandler();

    USER_UTIL_UNUSED(arg);
    MqttAppConfig_BuildDeviceId(deviceId, sizeof(deviceId),
                                HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2());

    while (1) {
        djiStat = osalHandler->GetTimeMs(&sysTimeMs);
        if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_ERROR("Get system time ms error, stat = 0x%08llX", djiStat);
        }

        {
#ifndef USER_FIRMWARE_MAJOR_VERSION
            snprintf(message, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN,
                     "ID:%s\r\nTime:%u ms\r\n", deviceId, sysTimeMs);
#else
            snprintf(message, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN,
                     "ID:%s\r\nv%02d.%02d.%02d.%02d %s %s\r\n",
                     deviceId,
                     USER_FIRMWARE_MAJOR_VERSION, USER_FIRMWARE_MINOR_VERSION,
                     USER_FIRMWARE_MODIFY_VERSION, USER_FIRMWARE_DEBUG_VERSION,
                     __DATE__, __TIME__);
#endif
        }


        djiStat = DjiWidgetFloatingWindow_ShowMessage(message);
        if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_ERROR("Floating window show message error, stat = 0x%08llX", djiStat);
        }

        osalHandler->TaskSleepMs(1000);
    }
}

#ifndef __CC_ARM
#pragma GCC diagnostic pop
#endif

static T_DjiReturnCode DjiTestWidget_SetWidgetValue(E_DjiWidgetType widgetType, uint32_t index, int32_t value,
                                                    void *userData)
{
    USER_UTIL_UNUSED(userData);
    DjiTest_WidgetMarkActive();

    USER_LOG_INFO("Set widget value, widgetType = %s, widgetIndex = %d ,widgetValue = %d",
                  s_widgetTypeNameArray[widgetType], index, value);
    Widget_UartLogf("[WIDGET CMD] type=%s index=%lu value=%ld",
                    s_widgetTypeNameArray[widgetType],
                    (unsigned long)index,
                    (long)value);
    
    /* Log switch widget events specifically */
    if (widgetType == DJI_WIDGET_TYPE_SWITCH)
    {
        USER_LOG_INFO("[SWITCH EVENT] Index=%d, Value=%d (%s), Previous Value=%d",
                      index, value,
                      (value == DJI_WIDGET_SWITCH_STATE_ON) ? "ON" : "OFF",
                      s_widgetValueList[index]);
    }
    
    s_widgetValueList[index] = value;

    if (widgetType == DJI_WIDGET_TYPE_SWITCH && index == WIDGET_IDX_WATER_PUMP)
    {
        char msg[DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN];
        T_DjiReturnCode djiStat;

        USER_LOG_INFO("[WATER PUMP] Index 0 state change");
        if (value == DJI_WIDGET_SWITCH_STATE_ON)
        {
            Widget_UartLogf("[WIDGET CMD] pump switch ON -> WaterPump_On()");
            WaterPump_On();
            USER_LOG_INFO("[WATER PUMP] ON");
            Widget_UartLogf("[WIDGET CMD] pump state=%u pressure=%lu",
                            (unsigned)WaterPump_GetSwitchState(),
                            (unsigned long)WaterPump_GetPressurePercent());
            snprintf(msg, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN,
                     "Water pump ON\r\nPower:%ld",
                     (long)s_widgetValueList[WIDGET_IDX_PWM_PERCENT]);
            djiStat = DjiWidgetFloatingWindow_ShowMessage(msg);
            if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_ERROR("[WATER PUMP] Floating window error, stat = 0x%08llX", djiStat);
            }
        }
        else if (value == DJI_WIDGET_SWITCH_STATE_OFF)
        {
            Widget_UartLogf("[WIDGET CMD] pump switch OFF -> WaterPump_Off()");
            WaterPump_Off();
            USER_LOG_INFO("[WATER PUMP] OFF");
            Widget_UartLogf("[WIDGET CMD] pump state=%u pressure=%lu",
                            (unsigned)WaterPump_GetSwitchState(),
                            (unsigned long)WaterPump_GetPressurePercent());
            snprintf(msg, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN, "Water pump OFF");
            djiStat = DjiWidgetFloatingWindow_ShowMessage(msg);
            if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_ERROR("[WATER PUMP] Floating window error, stat = 0x%08llX", djiStat);
            }
        }
        else
        {
            USER_LOG_ERROR("[WATER PUMP] Invalid switch value: %d", value);
            Widget_UartLogf("[WIDGET CMD] pump switch invalid value=%ld", (long)value);
        }
    }

    if (widgetType == DJI_WIDGET_TYPE_SWITCH && index == WIDGET_IDX_SWING_SWITCH)
    {
        char msg[DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN];
        T_DjiReturnCode djiStat;
        
        USER_LOG_INFO("[SWAY SWITCH] Processing sway switch (index %u) state change", (unsigned)WIDGET_IDX_SWING_SWITCH);
        if (value == DJI_WIDGET_SWITCH_STATE_ON)
        {
            /* Sway Switch ON: Start PWM swing */
            Widget_UartLogf("[WIDGET CMD] swing switch ON -> PwmSwing_Start()");
            USER_LOG_INFO("[SWAY SWITCH] State changed to ON, starting PWM swing");
            PwmSwing_Start();
            USER_LOG_INFO("[SWAY SWITCH] PWM swing started successfully");
            Widget_UartLogf("[WIDGET CMD] swing running=%u speed=%lu amp=%lu",
                            (unsigned)PwmSwing_IsRunning(),
                            (unsigned long)PwmSwing_GetSpeedPercent(),
                            (unsigned long)PwmSwing_GetAmplitudePercent());
            
            /* Show message in floating window */
            snprintf(msg, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN,
                     "Sway Started\r\nSpeed:%lu Amp:%u deg",
                     PwmSwing_GetSpeed(), (unsigned)PWM_SWING_AMPLITUDE_DEG_MAX);
            djiStat = DjiWidgetFloatingWindow_ShowMessage(msg);
            if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_ERROR("[SWAY SWITCH] Floating window show message error, stat = 0x%08llX", djiStat);
            }
        }
        else if (value == DJI_WIDGET_SWITCH_STATE_OFF)
        {
            /* Sway Switch OFF: Stop PWM swing */
            Widget_UartLogf("[WIDGET CMD] swing switch OFF -> PwmSwing_Stop()");
            USER_LOG_INFO("[SWAY SWITCH] State changed to OFF, stopping PWM swing");
            PwmSwing_Stop();
            USER_LOG_INFO("[SWAY SWITCH] PWM swing stopped successfully");
            Widget_UartLogf("[WIDGET CMD] swing running=%u",
                            (unsigned)PwmSwing_IsRunning());
            
            /* Show message in floating window */
            snprintf(msg, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN, 
                     "Sway Stopped");
            djiStat = DjiWidgetFloatingWindow_ShowMessage(msg);
            if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
                USER_LOG_ERROR("[SWAY SWITCH] Floating window show message error, stat = 0x%08llX", djiStat);
            }
        }
        else
        {
            USER_LOG_ERROR("[SWAY SWITCH] Invalid switch state value: %d", value);
            Widget_UartLogf("[WIDGET CMD] swing switch invalid value=%ld", (long)value);
        }
    }

    if (widgetType == DJI_WIDGET_TYPE_SCALE && index == WIDGET_IDX_PWM_PERCENT)
    {
        char msg[DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN];
        T_DjiReturnCode djiStat;
        uint32_t uiPercent = ClampU32((uint32_t)((value < 0) ? 0 : value), 0U, 100U);

        USER_LOG_INFO("[PUMP POWER] pumpPower UI=%lu -> PB14 TIM12 pressure duty", uiPercent);
        Widget_UartLogf("[WIDGET CMD] pump pressure=%lu -> WaterPump_SetPressurePercent()",
                        (unsigned long)uiPercent);
        WaterPump_SetPressurePercent(uiPercent);
        Widget_UartLogf("[WIDGET CMD] pump pressure done=%lu",
                        (unsigned long)WaterPump_GetPressurePercent());
        snprintf(msg, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN,
                 "Pump pressure PWM\r\nPB14 TIM12 200Hz\r\nDuty:0/10-90%%\r\nValue:%lu",
                 uiPercent);
        djiStat = DjiWidgetFloatingWindow_ShowMessage(msg);
        if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_ERROR("[PUMP POWER] Floating window show message error, stat = 0x%08llX", djiStat);
        }
    }

    if (widgetType == DJI_WIDGET_TYPE_SCALE && index == WIDGET_IDX_SWING_SPEED)
    {
        char msg[DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN];
        T_DjiReturnCode djiStat;
        uint32_t uiSpeed = ClampU32((uint32_t)((value < 0) ? 0 : value), 0U, 100U);

        USER_LOG_INFO("[SWAY SPEED] Setting swing speed to UI value: %lu", uiSpeed);
        Widget_UartLogf("[WIDGET CMD] swing speed=%lu -> PwmSwing_SetSpeedFromUI()",
                        (unsigned long)uiSpeed);
        PwmSwing_SetSpeedFromUI(uiSpeed);
        USER_LOG_INFO("[SWAY SPEED] Swing speed set to internal value: %lu", PwmSwing_GetSpeed());
        Widget_UartLogf("[WIDGET CMD] swing speed done internal=%lu percent=%lu",
                        (unsigned long)PwmSwing_GetSpeed(),
                        (unsigned long)PwmSwing_GetSpeedPercent());
        snprintf(msg, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN, 
                 "Sway Speed Set\r\nUI:%lu Internal:%lu", 
                 uiSpeed, PwmSwing_GetSpeed());
        djiStat = DjiWidgetFloatingWindow_ShowMessage(msg);
        if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_ERROR("[SWAY SPEED] Floating window show message error, stat = 0x%08llX", djiStat);
        }
    }

    if (widgetType == DJI_WIDGET_TYPE_SCALE && index == WIDGET_IDX_SWING_AMPLITUDE)
    {
        char msg[DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN];
        T_DjiReturnCode djiStat;
        uint32_t uiAmp = ClampU32((uint32_t)((value < 0) ? 0 : value), 0U, 100U);
        float ampDeg = ((float)uiAmp * PWM_SWING_AMPLITUDE_DEG_MAX_F) / 100.0f;

        USER_LOG_INFO("[SWAY AMPLITUDE] Setting amplitude UI=%lu -> deg=%.1f (max=%u)",
                      (unsigned long)uiAmp, (double)ampDeg, (unsigned)PWM_SWING_AMPLITUDE_DEG_MAX);
        Widget_UartLogf("[WIDGET CMD] swing amplitude UI=%lu -> PwmSwing_SetAmplitudeDeg()",
                        (unsigned long)uiAmp);
        PwmSwing_SetAmplitudeDeg(ampDeg);
        Widget_UartLogf("[WIDGET CMD] swing amplitude done percent=%lu",
                        (unsigned long)PwmSwing_GetAmplitudePercent());

        snprintf(msg, DJI_WIDGET_FLOATING_WINDOW_MSG_MAX_LEN,
                 "Sway Amplitude Set\r\nUI:%lu Deg:%.1f",
                 (unsigned long)uiAmp, (double)ampDeg);
        djiStat = DjiWidgetFloatingWindow_ShowMessage(msg);
        if (djiStat != DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS) {
            USER_LOG_ERROR("[SWAY AMPLITUDE] Floating window show message error, stat = 0x%08llX", djiStat);
        }
    }

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

static T_DjiReturnCode DjiTestWidget_GetWidgetValue(E_DjiWidgetType widgetType, uint32_t index, int32_t *value,
                                                    void *userData)
{
    USER_UTIL_UNUSED(userData);
    USER_UTIL_UNUSED(widgetType);
    DjiTest_WidgetMarkActive();

    if (index == WIDGET_IDX_SWING_SWITCH) {
        *value = PwmSwing_IsRunning() ? DJI_WIDGET_SWITCH_STATE_ON : DJI_WIDGET_SWITCH_STATE_OFF;
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    if (index == WIDGET_IDX_WATER_PUMP) {
        *value = WaterPump_GetSwitchState() ? DJI_WIDGET_SWITCH_STATE_ON : DJI_WIDGET_SWITCH_STATE_OFF;
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    if (index == WIDGET_IDX_PWM_PERCENT) {
        *value = (int32_t)WaterPump_GetPressurePercent();
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    if (index == WIDGET_IDX_SWING_SPEED) {
        *value = (int32_t)PwmSwing_GetSpeedPercent();
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    if (index == WIDGET_IDX_SWING_AMPLITUDE) {
        *value = (int32_t)PwmSwing_GetAmplitudePercent();
        return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
    }

    *value = s_widgetValueList[index];

    return DJI_ERROR_SYSTEM_MODULE_CODE_SUCCESS;
}

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
