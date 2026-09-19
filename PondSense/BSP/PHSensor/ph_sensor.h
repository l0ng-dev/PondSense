/**
 * @file    ph_sensor.h
 * @brief   pH 模拟量传感器 ADC 读取接口。
 *
 * @details
 * 提供原始 ADC 采样接口，不在 BSP 层保存校准系数或业务状态。
 */

#ifndef BSP_PH_H
#define BSP_PH_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/* Reads the voltage at the MCU ADC pin as a raw 12-bit value. */
bool BSP_PH_ReadRaw(ADC_HandleTypeDef *adc, uint16_t *raw);

#endif
