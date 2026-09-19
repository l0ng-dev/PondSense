/**
 * @file    feed_motor.c
 * @brief   L9110 电机限时驱动实现。
 *
 * @details
 * 由 SysTick 统一调用 FeedMotor_Process() 执行 1 ms 级超时停机，
 * 应用层只负责请求启动和推进投喂状态机。
 */

#include "feed_motor.h"
#include "main.h"

/* 防止调用方绕过投喂策略长时间驱动电机。 */
#define FEED_MOTOR_MAX_RUN_MS 17000U

static volatile uint8_t running;
static volatile uint8_t ready;
static volatile uint32_t start_ms;
static volatile uint32_t run_ms;

/**
 * @brief  初始化电机驱动并确保上电输出为停止状态。
 */
void FeedMotor_Init(void)
{
  ready = 1U;
  running = 0U;
  FeedMotor_Stop();
}

/* 原子化设置起始时间、方向输出和运行标志。 */
/**
 * @brief  启动一次限时电机驱动。
 * @param  duration_ms 请求通电时长，不能超过最大允许运行时间
 * @retval HAL_OK 启动成功；HAL_BUSY 已在运行；HAL_ERROR 参数非法
 */
HAL_StatusTypeDef FeedMotor_Start(uint32_t duration_ms)
{
  uint32_t interrupt_state;

  if ((duration_ms == 0U) || (duration_ms > FEED_MOTOR_MAX_RUN_MS))
  {
    return HAL_ERROR;
  }

  interrupt_state = __get_PRIMASK();
  __disable_irq();
  if (running != 0U)
  {
    __set_PRIMASK(interrupt_state);
    return HAL_BUSY;
  }

  start_ms = HAL_GetTick();
  run_ms = duration_ms;
  HAL_GPIO_WritePin(FEED_IN2_GPIO_Port, FEED_IN2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(FEED_IN1_GPIO_Port, FEED_IN1_Pin, GPIO_PIN_SET);
  running = 1U;
  __set_PRIMASK(interrupt_state);
  return HAL_OK;
}

/* 唯一的限时停机入口；当前由 SysTick 每 1 ms 调用。 */
/**
 * @brief  检查运行时长并在到期时停止电机。
 * @param  now_ms 当前系统毫秒时间
 * @note   由 SysTick 每 1 ms 调用，是电机限时控制的唯一时基。
 */
void FeedMotor_Process(uint32_t now_ms)
{
  if ((running != 0U) &&
      ((uint32_t)(now_ms - start_ms) >= run_ms))
  {
    FeedMotor_Stop();
  }
}

/**
 * @brief  立即关闭两个电机方向输出并清除运行标志。
 */
void FeedMotor_Stop(void)
{
  if (ready != 0U)
  {
    HAL_GPIO_WritePin(FEED_IN1_GPIO_Port, FEED_IN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(FEED_IN2_GPIO_Port, FEED_IN2_Pin, GPIO_PIN_RESET);
  }
  running = 0U;
}

/**
 * @brief  查询电机是否处于运行状态。
 * @retval 1 运行中；0 已停止
 */
uint8_t FeedMotor_IsRunning(void)
{
  return running;
}
