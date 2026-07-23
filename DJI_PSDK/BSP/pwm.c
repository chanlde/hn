/**
 ********************************************************************
 * @file    pwm.c
 * @version V1.0.0
 * @date    2024/05/22
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
#include "pwm.h"
#include "stm32h7xx_hal.h"

/* Private constants ---------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/
static TIM_HandleTypeDef htim1;
static TIM_HandleTypeDef htim12;

/* Private values -------------------------------------------------------------*/
static uint32_t period[PWM_NUM];        /* PWM周期数组（单位：定时器计数） */
static uint32_t duty_pluse[PWM_NUM] __attribute__((unused));    /* PWM占空比数组（单位：微秒），用于记录当前占空比值 */

/* TIM1相关静态变量 */
static uint32_t period_global = 0;      /* TIM1全局周期值（舵机与其它 TIM1 通道共享） */
static uint8_t timer1_inited = 0;       /* TIM1初始化标志，确保只初始化一次 */

/* TIM12相关静态变量（水泵压力 PWM / PB14） */
static uint32_t period12_global = 0;
static uint8_t timer12_inited = 0;

/* Private functions declaration ---------------------------------------------*/
static void Pwm_Timer1_Init(uint32_t freq);
static void Pwm_Timer12_Init(uint32_t freq);

/* Exported functions definition ---------------------------------------------*/
/**
 * @brief 初始化PWM通道
 * @param pwmNum PWM通道编号（PWM_FTS2，TIM1_CH1，PA8）
 * @param freq PWM频率（单位：Hz）
 * @param duty PWM占空比（单位：微秒，1-2ms对应舵机角度）
 * @note 关键配置点：
 *       1. ARR Preload必须启用 - 确保PWM周期完整性，避免中断干扰PWM输出
 *       2. HAL_TIM_PWM_Start只调用一次 - 避免额外的更新事件（UEV），防止舵机角度减半
 *       3. CCR Preload已启用 - 确保PWM占空比稳定
 */
/**
 * @brief TIM1定时器初始化（内部函数，只执行一次）
 * @param freq PWM频率（单位：Hz）
 */
static void Pwm_Timer1_Init(uint32_t freq)
{
    uint32_t tim1_clock;
    uint32_t prescaler;

    if (timer1_inited)
    {
        return;  /* 已经初始化，直接返回 */
    }

    /* 使能TIM1时钟 */
    __HAL_RCC_TIM1_CLK_ENABLE();

    /* 计算TIM1时钟：TIM1在APB2上，如果APB2分频为2，则TIM1时钟 = 2 × APB2时钟 */
    tim1_clock = 2 * HAL_RCC_GetPCLK2Freq();

    /* 计算预分频值：使TIM1计数器时钟为1MHz */
    prescaler = (tim1_clock / 1000000) - 1;

    /* 计算PWM周期：TIM1CLK = 1MHz */
    /* period = (1000000 / freq) - 1，单位：定时器计数 */
    period_global = 1000000 / freq - 1;

    /* 配置TIM1时基参数 */
    htim1.Instance = TIM1;
    htim1.Init.Prescaler = prescaler;              /* 预分频器：使TIM1时钟为1MHz */
    htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim1.Init.Period = period_global;             /* PWM周期 */
    htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;  /* 启用ARR预装载 */
    if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
    {
        return;
    }

    /* 
     * 关键配置1：ARR预装载已在HAL_TIM_PWM_Init中通过AutoReloadPreload启用
     * 作用：确保PWM周期在更新事件时同步加载，避免周期被中断打断
     */

    timer1_inited = 1;  /* 标记TIM1已初始化 */
}

/**
 * @brief TIM12定时器初始化（内部函数，只执行一次）——水泵压力 PWM / PB14 CH1
 * @param freq PWM频率（单位：Hz）
 */
static void Pwm_Timer12_Init(uint32_t freq)
{
    uint32_t tim_clock;
    uint32_t prescaler;

    if (timer12_inited)
    {
        return;
    }

    __HAL_RCC_TIM12_CLK_ENABLE();

    /* TIM12 在 APB1；APB1 分频非 1 时，TIM 时钟 = 2 × PCLK1 */
    tim_clock = 2 * HAL_RCC_GetPCLK1Freq();
    prescaler = (tim_clock / 1000000U) - 1U;
    period12_global = 1000000U / freq - 1U;

    htim12.Instance = TIM12;
    htim12.Init.Prescaler = prescaler;
    htim12.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim12.Init.Period = period12_global;
    htim12.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim12.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    if (HAL_TIM_PWM_Init(&htim12) != HAL_OK)
    {
        return;
    }

    timer12_inited = 1;
}

void Pwm_Init(E_PwmNum pwmNum, uint32_t freq, uint32_t duty)
{
    TIM_OC_InitTypeDef sConfigOC = {0};
    TIM_HandleTypeDef *htim = NULL;
    uint32_t channel;
    int idx;

    /* 占空比单位为微秒（1 duty = 1us），范围检查在周期计算后进行 */

    switch (pwmNum)
    {
    case PWM_FTS2:
        Pwm_Timer1_Init(freq);
        htim = &htim1;
        channel = TIM_CHANNEL_1;
        idx = PWM_FTS2;
        period[idx] = period_global;
        break;
    case PWM_PUMP:
        Pwm_Timer12_Init(freq);
        htim = &htim12;
        channel = TIM_CHANNEL_2;
        idx = PWM_PUMP;
        period[idx] = period12_global;
        break;
    default:
        return;
    }

    /* 占空比范围检查：必须小于等于PWM周期（period[idx]+1个计数 = 周期微秒数） */
    if (duty > (period[idx] + 1))
    {
        return;
    }

    /* 配置PWM通道 */
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = duty;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(htim, &sConfigOC, channel) != HAL_OK)
    {
        return;
    }

    __HAL_TIM_ENABLE_OCxPRELOAD(htim, channel);

    duty_pluse[idx] = duty;

    if (HAL_TIM_PWM_Start(htim, channel) != HAL_OK)
    {
        return;
    }
}

void Pwm_SetDuty(uint32_t duty)
{
    Pwm_SetDutyEx(PWM_FTS2, duty);
}

/**
 * @brief 设置PWM占空比
 * @param pwmNum PWM通道编号（PWM_FTS2，TIM1_CH1，PA8）
 * @param duty PWM占空比（单位：微秒，1-2ms对应舵机角度）
 * @note 只更新CCR值，不重新配置模式和预装载（已在Pwm_Init中配置）
 * @note 不调用HAL_TIM_PWM_Start()，避免触发额外的更新事件
 */
void Pwm_SetDutyEx(E_PwmNum pwmNum, uint32_t duty)
{
    TIM_HandleTypeDef *htim = NULL;
    uint32_t channel;
    int idx;

    switch (pwmNum)
    {
    case PWM_FTS2:
        htim = &htim1;
        channel = TIM_CHANNEL_1;
        idx = PWM_FTS2;
        break;
    case PWM_PUMP:
        htim = &htim12;
        channel = TIM_CHANNEL_2;
        idx = PWM_PUMP;
        break;
    default:
        return;
    }

    /* 占空比范围检查 */
    if (duty > (period[idx] + 1))
    {
        return;
    }

    duty_pluse[idx] = duty;

    __HAL_TIM_SET_COMPARE(htim, channel, duty);
}

void Pwm_SetDutyPercent(E_PwmNum pwmNum, uint32_t percent)
{
    uint32_t idx;
    uint32_t periodUs;
    uint32_t dutyUs;

    if (percent > 100U) {
        percent = 100U;
    }

    switch (pwmNum)
    {
    case PWM_FTS2:
        idx = PWM_FTS2;
        break;
    case PWM_PUMP:
        idx = PWM_PUMP;
        break;
    default:
        return;
    }

    periodUs = period[idx] + 1U;
    if (periodUs == 0U) {
        return;
    }

    dutyUs = (periodUs * percent) / 100U;
    Pwm_SetDutyEx(pwmNum, dutyUs);
}

/**
 * @brief 启用PWM通道输出
 * @param pwmNum PWM通道编号（PWM_FTS2，TIM1_CH1，PA8）
 * @note 只控制通道输出使能状态，不修改定时器配置
 * @note PWM_FTS2 使用 TIM1 的 TIM_CHANNEL_1（PA8）
 */
void Pwm_Enable(E_PwmNum pwmNum)
{
    TIM_HandleTypeDef *htim = NULL;
    uint32_t channel;

    switch (pwmNum)
    {
    case PWM_FTS2:
        htim = &htim1;
        channel = TIM_CHANNEL_1;
        break;
    case PWM_PUMP:
        htim = &htim12;
        channel = TIM_CHANNEL_2;
        break;
    default:
        return;
    }

    HAL_TIM_PWM_Start(htim, channel);
}

/**
 * @brief 禁用PWM通道输出
 * @param pwmNum PWM通道编号（PWM_FTS2，TIM1_CH1，PA8）
 * @note 只控制通道输出使能状态，不修改定时器配置
 * @note 关闭通道后，PWM输出停止，但定时器继续运行（不影响其他通道）
 * @note PWM_FTS2 使用 TIM1 的 TIM_CHANNEL_1（PA8）
 */
void Pwm_Disable(E_PwmNum pwmNum)
{
    TIM_HandleTypeDef *htim = NULL;
    uint32_t channel;

    switch (pwmNum)
    {
    case PWM_FTS2:
        htim = &htim1;
        channel = TIM_CHANNEL_1;
        break;
    case PWM_PUMP:
        htim = &htim12;
        channel = TIM_CHANNEL_2;
        break;
    default:
        return;
    }

    HAL_TIM_PWM_Stop(htim, channel);
}

/* Private functions definition-----------------------------------------------*/

/****************** (C) COPYRIGHT DJI Innovations *****END OF FILE****/
