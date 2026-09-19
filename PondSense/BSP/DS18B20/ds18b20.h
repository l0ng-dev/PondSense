/**
 * @file    ds18b20.h
 * @brief   DS18B20 单总线温度传感器驱动接口。
 *
 * @details
 * 提供传感器初始化、转换启动、温度读取和 CRC/设备状态定义。
 */

#ifndef BSP_DS18B20_H
#define BSP_DS18B20_H

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_DS18B20_OK = 0,
    BSP_DS18B20_BAD_ARGUMENT,
    BSP_DS18B20_NO_DEVICE,
    BSP_DS18B20_NOT_READY,
    BSP_DS18B20_CRC_ERROR,
    BSP_DS18B20_HAL_ERROR
} BSP_DS18B20_Result;

typedef struct {
    TIM_HandleTypeDef *timer_1mhz;
    GPIO_TypeDef *port;
    uint16_t pin;
    uint32_t conversion_started_ms;
    bool conversion_pending;
} BSP_DS18B20;

/* External-powered, single-device bus. Call only after wiring is verified. */
BSP_DS18B20_Result BSP_DS18B20_Init(BSP_DS18B20 *sensor,
                                    TIM_HandleTypeDef *timer_1mhz,
                                    GPIO_TypeDef *port, uint16_t pin);
BSP_DS18B20_Result BSP_DS18B20_StartConversion(BSP_DS18B20 *sensor);
BSP_DS18B20_Result BSP_DS18B20_ReadCentiC(BSP_DS18B20 *sensor,
                                          int16_t *temperature_centi_c);

#endif
