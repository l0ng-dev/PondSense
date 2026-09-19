/**
 * @file    vision.h
 * @brief   K230 视觉事件服务接口。
 *
 * @details
 * 定义视觉状态、结果消费、上传事件和 UART 回调转发接口。
 */

#ifndef VISION_H
#define VISION_H

#include "k230.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef enum
{
  VISION_IDLE = 0,
  VISION_WAIT_ACK,
  VISION_OBSERVING,
  VISION_RESULT_READY,
  VISION_TIMED_OUT,
  VISION_REJECTED,
  VISION_ERROR
} VisionState;

HAL_StatusTypeDef Vision_Init(UART_HandleTypeDef *uart);
HAL_StatusTypeDef Vision_Start(uint32_t event_id, uint32_t now_ms);
void Vision_Process(uint32_t now_ms);
VisionState Vision_GetState(void);
uint8_t Vision_TakeResult(PondSense_K230_FrameTypeDefData *result);
uint8_t Vision_TakeUpload(PondSense_K230_FrameTypeDefData *result);
void Vision_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size);
void Vision_ErrorCallback(UART_HandleTypeDef *uart);

#endif
