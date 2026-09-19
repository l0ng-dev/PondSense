/**
 * @file    pondsense.h
 * @brief   PondSense 应用层入口接口。
 *
 * @details
 * 为 CubeMX 生成的 main.c 提供初始化和周期处理入口，并声明串口回调转发接口。
 */

#ifndef PONDSENSE_H
#define PONDSENSE_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

void PondSense_Init(void);
void PondSense_Process(void);
void PondSense_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size);
void PondSense_UartErrorCallback(UART_HandleTypeDef *uart);
void PondSense_EmergencyStop(void);

#endif
