/**
 * @file    button.h
 * @brief   按键输入与消抖驱动接口。
 *
 * @details
 * 定义按键句柄、初始化和一次性按下事件读取接口。
 */

#ifndef BUTTON_H
#define BUTTON_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  uint32_t debounce_ms;
  uint32_t change_ms;
  uint8_t candidate;
  uint8_t stable;
  uint8_t release_seen;
} Button;

void Button_Init(Button *button,
                 GPIO_TypeDef *port,
                 uint16_t pin,
                 uint32_t debounce_ms,
                 uint32_t now_ms);
uint8_t Button_TakePress(Button *button, uint32_t now_ms);

#endif
