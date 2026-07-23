/**
 ********************************************************************
 * @file    file_binary_array_list_en.c
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
 * If you receive this source code without DJI’s authorization, you may not
 * further disseminate the information, and you must immediately remove the
 * source code and notify DJI of its removal. DJI reserves the right to pursue
 * legal actions against you for any loss(es) or damage(s) caused by your
 * failure to do so.
 *
 *********************************************************************
 */

/* Includes ------------------------------------------------------------------*/
#include "file_binary_array_list_en.h"

#include "widget_file_c/en_big_screen/pump_on_png.h"
#include "widget_file_c/en_big_screen/pump_off_png.h"
#include "widget_file_c/en_big_screen/sway_on_png.h"
#include "widget_file_c/en_big_screen/sway_off_png.h"
#include "widget_file_c/en_big_screen/pump_power_png.h"
#include "widget_file_c/en_big_screen/sway_speed_png.h"
#include "widget_file_c/en_big_screen/left_limit_png.h"
#include "widget_file_c/en_big_screen/right_limit_png.h"
#include "widget_file_c/en_big_screen/widget_config_json.h"
#include "widget_file_c/cn_big_screen/widget_config_json_cn.h"

/* Private constants ---------------------------------------------------------*/

/* Export types -------------------------------------------------------------*/
// English language file binary array list (default / DJI_MOBILE_APP_LANGUAGE_ENGLISH)
static T_DjiWidgetFileBinaryArray s_EnWidgetFileBinaryArrayList[] = {
    {widget_config_json_fileName, widget_config_json_fileSize, widget_config_json_fileBinaryArray},
    {pump_on_png_fileName, pump_on_png_fileSize, pump_on_png_fileBinaryArray},
    {pump_off_png_fileName, pump_off_png_fileSize, pump_off_png_fileBinaryArray},
    {sway_on_png_fileName, sway_on_png_fileSize, sway_on_png_fileBinaryArray},
    {sway_off_png_fileName, sway_off_png_fileSize, sway_off_png_fileBinaryArray},
    {pump_power_png_fileName, pump_power_png_fileSize, pump_power_png_fileBinaryArray},
    {sway_speed_png_fileName, sway_speed_png_fileSize, sway_speed_png_fileBinaryArray},
    {left_limit_png_fileName, left_limit_png_fileSize, left_limit_png_fileBinaryArray},
    {right_limit_png_fileName, right_limit_png_fileSize, right_limit_png_fileBinaryArray}};

// Chinese widget_config.json only; PNG assets same as English (DJI_MOBILE_APP_LANGUAGE_CHINESE)
static T_DjiWidgetFileBinaryArray s_CnWidgetFileBinaryArrayList[] = {
    {cn_widget_config_json_fileName, cn_widget_config_json_fileSize, cn_widget_config_json_fileBinaryArray},
    {pump_on_png_fileName, pump_on_png_fileSize, pump_on_png_fileBinaryArray},
    {pump_off_png_fileName, pump_off_png_fileSize, pump_off_png_fileBinaryArray},
    {sway_on_png_fileName, sway_on_png_fileSize, sway_on_png_fileBinaryArray},
    {sway_off_png_fileName, sway_off_png_fileSize, sway_off_png_fileBinaryArray},
    {pump_power_png_fileName, pump_power_png_fileSize, pump_power_png_fileBinaryArray},
    {sway_speed_png_fileName, sway_speed_png_fileSize, sway_speed_png_fileBinaryArray},
    {left_limit_png_fileName, left_limit_png_fileSize, left_limit_png_fileBinaryArray},
    {right_limit_png_fileName, right_limit_png_fileSize, right_limit_png_fileBinaryArray}};

/* Export values -------------------------------------------------------------*/
uint32_t g_EnBinaryArrayCount = sizeof(s_EnWidgetFileBinaryArrayList) / sizeof(T_DjiWidgetFileBinaryArray);
T_DjiWidgetFileBinaryArray *g_EnFileBinaryArrayList = s_EnWidgetFileBinaryArrayList;

uint32_t g_CnBinaryArrayCount = sizeof(s_CnWidgetFileBinaryArrayList) / sizeof(T_DjiWidgetFileBinaryArray);
T_DjiWidgetFileBinaryArray *g_CnFileBinaryArrayList = s_CnWidgetFileBinaryArrayList;

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
