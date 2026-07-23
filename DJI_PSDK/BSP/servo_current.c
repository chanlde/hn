/**
 ********************************************************************
 * @file    servo_current.c
 * @brief   ADC1 PA4（IN18）单次转换，供给舵机电流检测硬件
 ********************************************************************
 */

#include "servo_current.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_adc.h"
#include "stm32h7xx_hal_adc_ex.h"
#include "stm32h7xx_hal_rcc_ex.h"

static ADC_HandleTypeDef s_hadc1;
static uint8_t s_adcInited;

void ServoCurrent_Init(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    if (s_adcInited) {
        return;
    }

    /* ADC kernel clock：使用 PLL2_P（需在 RCC 内使能 PLL2） */
    {
        RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
        PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_ADC;
        PeriphClkInitStruct.PLL2.PLL2M = 2;
        PeriphClkInitStruct.PLL2.PLL2N = 12;
        PeriphClkInitStruct.PLL2.PLL2P = 2;
        PeriphClkInitStruct.PLL2.PLL2Q = 2;
        PeriphClkInitStruct.PLL2.PLL2R = 2;
        PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
        PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOMEDIUM;
        PeriphClkInitStruct.PLL2.PLL2FRACN = 0;
        PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
            return;
        }
    }

    s_hadc1.Instance = ADC1;
    s_hadc1.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
    s_hadc1.Init.Resolution = ADC_RESOLUTION_12B;
    s_hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
    s_hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    s_hadc1.Init.LowPowerAutoWait = DISABLE;
    s_hadc1.Init.ContinuousConvMode = DISABLE;
    s_hadc1.Init.NbrOfConversion = 1;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    s_hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    s_hadc1.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DR;
    s_hadc1.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
    s_hadc1.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
    s_hadc1.Init.OversamplingMode = DISABLE;
    if (HAL_ADC_Init(&s_hadc1) != HAL_OK) {
        return;
    }

    if (HAL_ADCEx_Calibration_Start(&s_hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED) != HAL_OK) {
        return;
    }

    sConfig.Channel = ADC_CHANNEL_18;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
    sConfig.SingleDiff = ADC_SINGLE_ENDED;
    sConfig.OffsetNumber = ADC_OFFSET_NONE;
    sConfig.Offset = 0;
    if (HAL_ADC_ConfigChannel(&s_hadc1, &sConfig) != HAL_OK) {
        return;
    }

    s_adcInited = 1;
}

uint16_t ServoCurrent_ReadRaw(void)
{
    uint16_t val = 0;

    if (!s_adcInited) {
        return 0U;
    }

    if (HAL_ADC_Start(&s_hadc1) != HAL_OK) {
        return 0U;
    }
    if (HAL_ADC_PollForConversion(&s_hadc1, 50U) != HAL_OK) {
        (void)HAL_ADC_Stop(&s_hadc1);
        return 0U;
    }
    val = (uint16_t)HAL_ADC_GetValue(&s_hadc1);
    (void)HAL_ADC_Stop(&s_hadc1);
    return val;
}

uint32_t ServoCurrent_ReadMv(void)
{
    uint32_t raw = (uint32_t)ServoCurrent_ReadRaw();
    /* VDDA 典型 3.3V，12 位满量程 4095 */
    return (raw * 3300U) / 4095U;
}
