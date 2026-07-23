/**
 ********************************************************************
 * @file    air780e_power.c
 * @brief   AIR780E power control module
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
#include "air780e_power.h"
#include "stm32h7xx_hal.h"

/* Private constants ---------------------------------------------------------*/
#define AIR780E_POWER_GPIO_PIN                      GPIO_PIN_10
#define AIR780E_POWER_GPIO_PORT                     GPIOE
#define AIR780E_POWER_GPIO_CLK_ENABLE()             __HAL_RCC_GPIOE_CLK_ENABLE()

/* Private types -------------------------------------------------------------*/

/* Private values -------------------------------------------------------------*/

/* Private functions declaration ---------------------------------------------*/

/* Exported functions definition ---------------------------------------------*/
void Air780ePower_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;

    /* Enable the GPIOE clock */
    AIR780E_POWER_GPIO_CLK_ENABLE();

    /* Configure AIR780E power control pin as output */
    GPIO_InitStruct.Pin = AIR780E_POWER_GPIO_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(AIR780E_POWER_GPIO_PORT, &GPIO_InitStruct);

    /* Set initial state to low (power off) */
    HAL_GPIO_WritePin(AIR780E_POWER_GPIO_PORT, AIR780E_POWER_GPIO_PIN, GPIO_PIN_RESET);
}

void Air780ePower_On(void)
{
    HAL_GPIO_WritePin(AIR780E_POWER_GPIO_PORT, AIR780E_POWER_GPIO_PIN, GPIO_PIN_SET);
}

void Air780ePower_Off(void)
{
    HAL_GPIO_WritePin(AIR780E_POWER_GPIO_PORT, AIR780E_POWER_GPIO_PIN, GPIO_PIN_RESET);
}

/* Private functions definition-----------------------------------------------*/

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
