/**
 * @file    feeding.h
 * @brief   投喂流程状态机接口。
 *
 * @details
 * 定义投喂状态、故障类型、等级追加循环和应用层动作回调。
 */

#ifndef FEEDING_H
#define FEEDING_H

#include <stdint.h>

typedef enum
{
  FEEDING_WAIT_KEY0 = 0,
  FEEDING_INITIAL_RUNNING,
  FEEDING_AUTO_WAIT,
  FEEDING_OBSERVING,
  FEEDING_ADDITIONAL_RUNNING,
  FEEDING_FAULT_LOCKED
} FeedingState;

typedef enum
{
  FEEDING_FAULT_NONE = 0,
  FEEDING_FAULT_INITIAL_START,
  FEEDING_FAULT_OBSERVATION_START,
  FEEDING_FAULT_OBSERVATION_TIMEOUT,
  FEEDING_FAULT_OBSERVATION_REJECTED,
  FEEDING_FAULT_OBSERVATION_LINK,
  FEEDING_FAULT_INVALID_RESULT,
  FEEDING_FAULT_ADDITIONAL_START,
  FEEDING_FAULT_MOTOR_OVERRUN
} FeedingFault;

#define FEEDING_ADDITIONAL_IDLE          0U
#define FEEDING_ADDITIONAL_WAITING       1U
#define FEEDING_ADDITIONAL_MOTOR_RUNNING 2U
#define FEEDING_ADDITIONAL_COMPLETE      3U
#define FEEDING_ADDITIONAL_FAILED        4U

typedef struct
{
  uint8_t (*start_initial)(void *context);
  uint8_t (*motor_running)(void *context);
  uint8_t (*start_observation)(uint32_t now_ms,
                               uint32_t *event_id,
                               void *context);
  void (*arm_additional)(uint8_t cycles,
                         uint32_t now_ms,
                         void *context);
  uint8_t (*additional_status)(void *context);
  void (*stop_motor)(void *context);
} FeedingOps;

typedef struct
{
  volatile FeedingState state;
  volatile FeedingFault fault;
  volatile uint32_t active_event_id;
  volatile uint32_t consecutive_failure_count;
  volatile uint32_t skipped_deadline_count;
  uint32_t period_ms;
  uint32_t next_deadline_ms;
  FeedingOps ops;
  void *ops_context;
} Feeding;

void Feeding_Init(Feeding *feeding,
                  uint32_t period_ms,
                  const FeedingOps *ops,
                  void *context);
uint8_t Feeding_Unlock(Feeding *feeding);
void Feeding_Process(Feeding *feeding, uint32_t now_ms);
uint8_t Feeding_AcceptResult(Feeding *feeding,
                             uint32_t event_id,
                             uint8_t valid,
                             uint8_t level,
                             uint32_t now_ms);
void Feeding_FailObservation(Feeding *feeding, FeedingFault fault);

#endif
