/**
 ********************************************************************
 * @file    application.h
 * @date    2019/8/23
 * @brief   This is the header file for "application.c", defining the structure and
 * (exported) function prototypes.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef APPLICATION_H
#define APPLICATION_H

/* Includes ------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C" {
#endif

/* Exported constants --------------------------------------------------------*/

/* Exported types ------------------------------------------------------------*/

/* Exported functions --------------------------------------------------------*/
void DjiUser_StartTask(void const *argument);
void DjiUser_MonitorTask(void const *argument);

/**
 * @brief PSDK 应用层是否已完成启动（DjiCore_ApplicationStart + CustomSerial_StorageInit 之后置位）。
 *        MQTT 等重资源任务应在此返回非 0 后再创建，避免与 PSDK 争用 heap。
 * @return 非 0 表示已就绪；0 表示尚未就绪或启动失败已删任务。
 */
int DjiUser_IsApplicationStarted(void);

#ifdef __cplusplus
}
#endif

#endif // APPLICATION_H
/************************ (C) COPYRIGHT DJI Innovations *******END OF FILE******/
