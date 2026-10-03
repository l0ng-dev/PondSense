/**
 * @file    feeding.h
 * @brief   投喂批次与视觉观察协调状态机接口。
 *
 * @details
 * 每个批次包含 1 或 2 个机械循环。K230 在批次最后一个循环开始
 * 8 秒后观察，结果只决定下一批次，不改变当前批次。
 */

#ifndef FEEDING_H
#define FEEDING_H

#include <stdint.h>

typedef enum
{
  FEEDING_WAIT_KEY0 = 0,
  FEEDING_INITIAL_RUNNING,
  FEEDING_OBSERVING,
  FEEDING_BATCH_RUNNING,
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
  FEEDING_FAULT_BATCH_START,
  FEEDING_FAULT_MOTOR_OVERRUN
} FeedingFault;

typedef enum
{
  FEEDING_BATCH_IDLE = 0,
  FEEDING_BATCH_WAITING,
  FEEDING_BATCH_MOTOR_RUNNING,
  FEEDING_BATCH_COMPLETE,
  FEEDING_BATCH_FAILED
} FeedingBatchStatus;

typedef struct
{
  uint8_t (*start_batch)(uint8_t cycles,
                         uint32_t now_ms,
                         void *context);
  uint8_t (*batch_status)(void *context);
  uint8_t (*observation_due)(uint32_t now_ms, void *context);
  uint8_t (*start_observation)(uint32_t now_ms,
                               uint32_t *event_id,
                               void *context);
  void (*stop_motor)(void *context);
} FeedingOps;

typedef struct
{
  volatile FeedingState state;
  volatile FeedingFault fault;
  volatile uint32_t active_event_id;
  volatile uint32_t completed_round_count;
  volatile uint8_t result_ready;
  volatile uint8_t result_level;
  uint8_t observation_started;
  FeedingOps ops;
  void *ops_context;
} Feeding;

void Feeding_Init(Feeding *feeding,
                  const FeedingOps *ops,
                  void *context);
uint8_t Feeding_Unlock(Feeding *feeding, uint32_t now_ms);
void Feeding_Process(Feeding *feeding, uint32_t now_ms);
uint8_t Feeding_AcceptResult(Feeding *feeding,
                             uint32_t event_id,
                             uint8_t valid,
                             uint8_t level,
                             uint32_t now_ms);
void Feeding_FailObservation(Feeding *feeding, FeedingFault fault);
uint8_t Feeding_IsObservationPending(const Feeding *feeding);

#endif
