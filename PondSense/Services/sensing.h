/**
 * @file    sensing.h
 * @brief   温度与 pH 传感器服务接口。
 *
 * @details
 * 提供传感器初始化、周期处理、快照读取和显示文本访问接口。
 */

#ifndef SENSING_H
#define SENSING_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef struct
{
  int16_t temperature_centi_c;
  uint16_t ph_centi;
  uint8_t temperature_valid;
  uint8_t ph_valid;
} SensingSnapshot;

HAL_StatusTypeDef Sensing_Init(TIM_HandleTypeDef *timer,
                               GPIO_TypeDef *temperature_port,
                               uint16_t temperature_pin,
                               ADC_HandleTypeDef *adc,
                               uint32_t now_ms);
void Sensing_Process(uint32_t now_ms);
void Sensing_GetSnapshot(SensingSnapshot *snapshot);
const char *Sensing_TemperatureText(void);
const char *Sensing_PhText(void);

#endif
