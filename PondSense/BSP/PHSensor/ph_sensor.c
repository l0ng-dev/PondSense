/**
 * @file    ph_sensor.c
 * @brief   pH 模拟量传感器 ADC 读取实现。
 *
 * @details
 * 只负责读取和校验原始 ADC 值；pH 换算、稳定性判断由 Services 层完成。
 */

#include "ph_sensor.h"

/**
 * @brief  读取 pH 模块 ADC 原始值。
 * @param  adc ADC 句柄
 * @param  raw 输出的 12 位原始采样值
 * @retval true 读取成功；false 参数、启动或轮询失败
 * @note   本函数不执行 pH 校准和换算。
 */
bool BSP_PH_ReadRaw(ADC_HandleTypeDef *adc, uint16_t *raw)
{
    if (adc == NULL || raw == NULL) {
        return false;
    }

    if (HAL_ADC_Start(adc) != HAL_OK) {
        return false;
    }

    if (HAL_ADC_PollForConversion(adc, 10U) != HAL_OK) {
        (void)HAL_ADC_Stop(adc);
        return false;
    }

    uint32_t value = HAL_ADC_GetValue(adc);
    if (HAL_ADC_Stop(adc) != HAL_OK || value > 4095U) {
        return false;
    }

    *raw = (uint16_t)value;
    return true;
}
