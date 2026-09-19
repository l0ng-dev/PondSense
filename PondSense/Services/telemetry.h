/**
 * @file    telemetry.h
 * @brief   ESP-01S/MQTT 遥测服务接口。
 *
 * @details
 * 定义遥测初始化、周期处理及 UART 接收/错误回调转发接口。
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "sensing.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

HAL_StatusTypeDef Telemetry_Init(UART_HandleTypeDef *uart, uint32_t now_ms);
void Telemetry_Process(uint32_t now_ms,
                       const SensingSnapshot *sensors,
                       uint8_t feeding_result_valid,
                       uint8_t feeding_result_level);
void Telemetry_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size);
void Telemetry_ErrorCallback(UART_HandleTypeDef *uart);

#endif
