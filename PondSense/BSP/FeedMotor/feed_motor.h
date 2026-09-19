/**
 * @file    feed_motor.h
 * @brief   L9110 电机限时驱动接口。
 *
 * @details
 * 提供电机初始化、启动、超时处理、停止和运行状态查询接口。
 */

#ifndef FEED_MOTOR_H
#define FEED_MOTOR_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

void FeedMotor_Init(void);
HAL_StatusTypeDef FeedMotor_Start(uint32_t duration_ms);
void FeedMotor_Process(uint32_t now_ms);
void FeedMotor_Stop(void);
uint8_t FeedMotor_IsRunning(void);

#endif
