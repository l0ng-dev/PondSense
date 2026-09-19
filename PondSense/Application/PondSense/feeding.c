/**
 * @file    feeding.c
 * @brief   投喂流程状态机实现。
 *
 * @details
 * 管理初次投喂、观察等待、等级追加投喂和故障锁定状态。
 * 本模块不直接操作 GPIO，电机动作通过 FeedingOps 回调交给应用层。
 */

#include "feeding.h"
#include <stddef.h>
#include <string.h>

#define FEEDING_MAX_FAILURES 3U

static void lock_fault(Feeding *feeding, FeedingFault fault)
{
  if (feeding->ops.stop_motor != NULL)
  {
    feeding->ops.stop_motor(feeding->ops_context);
  }
  feeding->fault = fault;
  feeding->state = FEEDING_FAULT_LOCKED;
}

static void fail_observation(Feeding *feeding, FeedingFault fault)
{
  feeding->fault = fault;
  feeding->active_event_id = 0U;
  feeding->consecutive_failure_count++;
  if (feeding->consecutive_failure_count >= FEEDING_MAX_FAILURES)
  {
    lock_fault(feeding, fault);
  }
  else
  {
    feeding->state = FEEDING_AUTO_WAIT;
  }
}

void Feeding_Init(Feeding *feeding,
                  uint32_t period_ms,
                  const FeedingOps *ops,
                  void *context)
{
  if ((feeding == NULL) || (ops == NULL))
  {
    return;
  }
  (void)memset(feeding, 0, sizeof(*feeding));
  feeding->period_ms = period_ms;
  feeding->ops = *ops;
  feeding->ops_context = context;
  feeding->state = FEEDING_WAIT_KEY0;
}

/**
 * @brief  解锁并启动一次初次投喂。
 * @param  feeding 投喂状态机句柄
 * @retval 1 启动成功；0 当前状态不允许或启动失败
 */
uint8_t Feeding_Unlock(Feeding *feeding)
{
  if ((feeding == NULL) || (feeding->state != FEEDING_WAIT_KEY0) ||
      (feeding->ops.start_initial == NULL))
  {
    return 0U;
  }
  if (feeding->ops.start_initial(feeding->ops_context) == 0U)
  {
    lock_fault(feeding, FEEDING_FAULT_INITIAL_START);
    return 0U;
  }
  feeding->state = FEEDING_INITIAL_RUNNING;
  return 1U;
}

/**
 * @brief  推进投喂状态机。
 * @param  feeding 投喂状态机句柄
 * @param  now_ms  当前系统毫秒时间
 * @note   该函数必须周期调用，不执行阻塞等待。
 */
void Feeding_Process(Feeding *feeding, uint32_t now_ms)
{
  uint32_t event_id = 0U;
  uint8_t additional_status;

  if (feeding == NULL)
  {
    return;
  }

  /* 初次投喂结束后才建立首个固定周期，避免提前发送 OBS。 */
  if (feeding->state == FEEDING_INITIAL_RUNNING)
  {
    if ((feeding->ops.motor_running != NULL) &&
        (feeding->ops.motor_running(feeding->ops_context) == 0U))
    {
      feeding->next_deadline_ms = now_ms + feeding->period_ms;
      feeding->state = FEEDING_AUTO_WAIT;
    }
    return;
  }

  /* 等待等级追加循环全部完成，期间禁止进入下一观察周期。 */
  if (feeding->state == FEEDING_ADDITIONAL_RUNNING)
  {
    if (feeding->ops.additional_status == NULL)
    {
      lock_fault(feeding, FEEDING_FAULT_ADDITIONAL_START);
      return;
    }
    additional_status = feeding->ops.additional_status(feeding->ops_context);
    if (additional_status == FEEDING_ADDITIONAL_COMPLETE)
    {
      feeding->state = FEEDING_AUTO_WAIT;
    }
    else if (additional_status == FEEDING_ADDITIONAL_FAILED)
    {
      lock_fault(feeding, FEEDING_FAULT_ADDITIONAL_START);
      return;
    }
  }

  if ((feeding->state == FEEDING_AUTO_WAIT) &&
      (feeding->ops.motor_running != NULL) &&
      (feeding->ops.motor_running(feeding->ops_context) != 0U))
  {
    lock_fault(feeding, FEEDING_FAULT_MOTOR_OVERRUN);
    return;
  }

  if ((feeding->state == FEEDING_WAIT_KEY0) ||
      (feeding->state == FEEDING_FAULT_LOCKED) ||
      ((int32_t)(now_ms - feeding->next_deadline_ms) < 0))
  {
    return;
  }

  /* 使用绝对截止时间推进周期，避免单次处理延迟累积漂移。 */
  do
  {
    feeding->next_deadline_ms += feeding->period_ms;
  } while ((int32_t)(now_ms - feeding->next_deadline_ms) >= 0);

  if (feeding->state == FEEDING_ADDITIONAL_RUNNING)
  {
    lock_fault(feeding, FEEDING_FAULT_MOTOR_OVERRUN);
    return;
  }
  /* 到期仍在观察说明本轮结果超时，按失败策略处理。 */
  if (feeding->state == FEEDING_OBSERVING)
  {
    feeding->skipped_deadline_count++;
    fail_observation(feeding, FEEDING_FAULT_OBSERVATION_TIMEOUT);
    return;
  }
  if ((feeding->ops.start_observation == NULL) ||
      (feeding->ops.start_observation(now_ms,
                                      &event_id,
                                      feeding->ops_context) == 0U) ||
      (event_id == 0U))
  {
    feeding->skipped_deadline_count++;
    fail_observation(feeding, FEEDING_FAULT_OBSERVATION_START);
    return;
  }
  feeding->active_event_id = event_id;
  feeding->state = FEEDING_OBSERVING;
}

/**
 * @brief  接收并消费当前观察事件的 K230 结果。
 * @param  feeding    投喂状态机句柄
 * @param  event_id   结果对应的事件编号
 * @param  valid      结果是否有效
 * @param  level      摄食等级，支持 0/1/2
 * @param  now_ms     当前系统毫秒时间
 * @retval 1 结果被接受；0 事件不匹配或结果无效
 */
uint8_t Feeding_AcceptResult(Feeding *feeding,
                             uint32_t event_id,
                             uint8_t valid,
                             uint8_t level,
                             uint32_t now_ms)
{
  if ((feeding == NULL) || (feeding->state != FEEDING_OBSERVING))
  {
    return 0U;
  }
  if ((event_id == 0U) || (event_id != feeding->active_event_id))
  {
    fail_observation(feeding, FEEDING_FAULT_OBSERVATION_LINK);
    return 0U;
  }

  feeding->active_event_id = 0U;
  if ((valid == 0U) || (level > 2U))
  {
    fail_observation(feeding, FEEDING_FAULT_INVALID_RESULT);
    return 0U;
  }

  feeding->consecutive_failure_count = 0U;
  feeding->fault = FEEDING_FAULT_NONE;
  if (level == 0U)
  {
    feeding->state = FEEDING_AUTO_WAIT;
  }
  else if (feeding->ops.arm_additional != NULL)
  {
    feeding->ops.arm_additional(level, now_ms, feeding->ops_context);
    feeding->state = FEEDING_ADDITIONAL_RUNNING;
  }
  else
  {
    lock_fault(feeding, FEEDING_FAULT_ADDITIONAL_START);
  }
  return 1U;
}

/**
 * @brief  将当前观察标记为失败并按连续失败次数处理。
 * @param  feeding 投喂状态机句柄
 * @param  fault   失败原因
 */
void Feeding_FailObservation(Feeding *feeding, FeedingFault fault)
{
  if ((feeding != NULL) && (feeding->state == FEEDING_OBSERVING))
  {
    fail_observation(feeding, fault);
  }
}
