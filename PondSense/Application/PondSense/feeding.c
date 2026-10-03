/**
 * @file    feeding.c
 * @brief   投喂批次与视觉观察协调状态机实现。
 */

#include "feeding.h"
#include <stddef.h>
#include <string.h>

static void clear_round_result(Feeding *feeding)
{
  feeding->active_event_id = 0U;
  feeding->observation_started = 0U;
  feeding->result_ready = 0U;
  feeding->result_level = 0U;
}

static void lock_fault(Feeding *feeding, FeedingFault fault)
{
  if (feeding->ops.stop_motor != NULL)
  {
    feeding->ops.stop_motor(feeding->ops_context);
  }
  feeding->fault = fault;
  feeding->active_event_id = 0U;
  feeding->observation_started = 0U;
  feeding->state = FEEDING_FAULT_LOCKED;
}

static uint8_t begin_batch(Feeding *feeding,
                           uint8_t cycles,
                           uint32_t now_ms,
                           FeedingState running_state)
{
  clear_round_result(feeding);
  if ((feeding->ops.start_batch == NULL) ||
      (feeding->ops.start_batch(cycles,
                                now_ms,
                                feeding->ops_context) == 0U))
  {
    lock_fault(feeding,
               (running_state == FEEDING_INITIAL_RUNNING)
                   ? FEEDING_FAULT_INITIAL_START
                   : FEEDING_FAULT_BATCH_START);
    return 0U;
  }
  feeding->state = running_state;
  return 1U;
}

static void apply_result(Feeding *feeding, uint32_t now_ms)
{
  uint8_t level = feeding->result_level;

  feeding->completed_round_count++;
  if (level == 0U)
  {
    clear_round_result(feeding);
    feeding->fault = FEEDING_FAULT_NONE;
    feeding->state = FEEDING_WAIT_KEY0;
    return;
  }

  (void)begin_batch(feeding,
                    level,
                    now_ms,
                    FEEDING_BATCH_RUNNING);
}

static uint8_t start_observation_if_due(Feeding *feeding,
                                        uint32_t now_ms)
{
  uint32_t event_id = 0U;

  if ((feeding->observation_started != 0U) ||
      (feeding->result_ready != 0U))
  {
    return 1U;
  }
  if ((feeding->ops.observation_due == NULL) ||
      (feeding->ops.observation_due(now_ms,
                                    feeding->ops_context) == 0U))
  {
    return 1U;
  }
  if ((feeding->ops.start_observation == NULL) ||
      (feeding->ops.start_observation(now_ms,
                                      &event_id,
                                      feeding->ops_context) == 0U) ||
      (event_id == 0U))
  {
    lock_fault(feeding, FEEDING_FAULT_OBSERVATION_START);
    return 0U;
  }
  feeding->active_event_id = event_id;
  feeding->observation_started = 1U;
  return 1U;
}

void Feeding_Init(Feeding *feeding,
                  const FeedingOps *ops,
                  void *context)
{
  if ((feeding == NULL) || (ops == NULL))
  {
    return;
  }
  (void)memset(feeding, 0, sizeof(*feeding));
  feeding->ops = *ops;
  feeding->ops_context = context;
  feeding->state = FEEDING_WAIT_KEY0;
}

uint8_t Feeding_Unlock(Feeding *feeding, uint32_t now_ms)
{
  if ((feeding == NULL) || (feeding->state != FEEDING_WAIT_KEY0))
  {
    return 0U;
  }
  feeding->fault = FEEDING_FAULT_NONE;
  feeding->completed_round_count = 0U;
  return begin_batch(feeding, 1U, now_ms, FEEDING_INITIAL_RUNNING);
}

void Feeding_Process(Feeding *feeding, uint32_t now_ms)
{
  uint8_t batch_status;

  if ((feeding == NULL) ||
      (feeding->state == FEEDING_WAIT_KEY0) ||
      (feeding->state == FEEDING_FAULT_LOCKED))
  {
    return;
  }

  if (feeding->state == FEEDING_OBSERVING)
  {
    if (feeding->result_ready != 0U)
    {
      apply_result(feeding, now_ms);
    }
    return;
  }

  if (start_observation_if_due(feeding, now_ms) == 0U)
  {
    return;
  }

  if (feeding->ops.batch_status == NULL)
  {
    lock_fault(feeding, FEEDING_FAULT_MOTOR_OVERRUN);
    return;
  }
  batch_status = feeding->ops.batch_status(feeding->ops_context);
  if (batch_status == FEEDING_BATCH_FAILED)
  {
    lock_fault(feeding, FEEDING_FAULT_MOTOR_OVERRUN);
    return;
  }
  if (batch_status != FEEDING_BATCH_COMPLETE)
  {
    return;
  }

  if ((feeding->observation_started == 0U) &&
      (feeding->result_ready == 0U))
  {
    lock_fault(feeding, FEEDING_FAULT_OBSERVATION_START);
    return;
  }
  if (feeding->result_ready != 0U)
  {
    apply_result(feeding, now_ms);
  }
  else
  {
    feeding->state = FEEDING_OBSERVING;
  }
}

uint8_t Feeding_AcceptResult(Feeding *feeding,
                             uint32_t event_id,
                             uint8_t valid,
                             uint8_t level,
                             uint32_t now_ms)
{
  if ((feeding == NULL) ||
      (feeding->observation_started == 0U) ||
      (feeding->result_ready != 0U) ||
      (event_id == 0U) ||
      (event_id != feeding->active_event_id))
  {
    return 0U;
  }

  feeding->active_event_id = 0U;
  feeding->observation_started = 0U;
  if ((valid == 0U) || (level > 2U))
  {
    lock_fault(feeding, FEEDING_FAULT_INVALID_RESULT);
    return 0U;
  }

  feeding->fault = FEEDING_FAULT_NONE;
  feeding->result_level = level;
  feeding->result_ready = 1U;
  if (feeding->state == FEEDING_OBSERVING)
  {
    apply_result(feeding, now_ms);
  }
  return 1U;
}

void Feeding_FailObservation(Feeding *feeding, FeedingFault fault)
{
  if ((feeding != NULL) &&
      (feeding->observation_started != 0U) &&
      (feeding->result_ready == 0U))
  {
    lock_fault(feeding, fault);
  }
}

uint8_t Feeding_IsObservationPending(const Feeding *feeding)
{
  if (feeding == NULL)
  {
    return 0U;
  }
  return (uint8_t)((feeding->observation_started != 0U) &&
                   (feeding->result_ready == 0U));
}
