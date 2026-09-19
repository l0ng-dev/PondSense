/**
 * @file    button.c
 * @brief   按键输入与消抖驱动实现。
 *
 * @details
 * 采用主循环轮询和时间消抖方式输出一次性按下事件，不在 GPIO 中断中阻塞。
 */

#include "button.h"
#include <stddef.h>

/**
 * @brief  初始化按键句柄和消抖状态。
 * @param  button      按键句柄
 * @param  port        GPIO 端口
 * @param  pin         GPIO 引脚
 * @param  debounce_ms 消抖时间
 * @param  now_ms      当前系统毫秒时间
 */
void Button_Init(Button *button,
                 GPIO_TypeDef *port,
                 uint16_t pin,
                 uint32_t debounce_ms,
                 uint32_t now_ms)
{
  if ((button == NULL) || (port == NULL))
  {
    return;
  }

  button->port = port;
  button->pin = pin;
  button->debounce_ms = debounce_ms;
  button->candidate =
      (uint8_t)(HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET);
  button->stable = button->candidate;
  button->release_seen = (uint8_t)(button->stable == 0U);
  button->change_ms = now_ms;
}

/**
 * @brief  轮询按键并提取一次性按下事件。
 * @param  button 按键句柄
 * @param  now_ms 当前系统毫秒时间
 * @retval 1 检测到稳定按下；0 无新按下事件
 */
uint8_t Button_TakePress(Button *button, uint32_t now_ms)
{
  uint8_t raw;

  if ((button == NULL) || (button->port == NULL))
  {
    return 0U;
  }

  raw = (uint8_t)(HAL_GPIO_ReadPin(button->port, button->pin) == GPIO_PIN_SET);
  if (raw != button->candidate)
  {
    button->candidate = raw;
    button->change_ms = now_ms;
  }
  if ((raw != button->stable) &&
      ((uint32_t)(now_ms - button->change_ms) >= button->debounce_ms))
  {
    button->stable = raw;
    if (raw == 0U)
    {
      button->release_seen = 1U;
    }
    else if (button->release_seen != 0U)
    {
      button->release_seen = 0U;
      return 1U;
    }
  }
  return 0U;
}
