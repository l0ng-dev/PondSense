/**
 * @file    test_feeding.c
 * @brief   投喂批次与观察协调状态机主机端回归测试。
 */

#include "../../Application/PondSense/feeding.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct
{
  uint8_t batch_cycles;
  FeedingBatchStatus status;
  uint8_t observation_due;
  uint32_t event;
  uint32_t batch_starts;
  uint32_t observation_starts;
  uint32_t stops;
} Fake;

static uint8_t start_batch(uint8_t cycles,
                           uint32_t now_ms,
                           void *context)
{
  Fake *fake = context;
  (void)now_ms;
  fake->batch_cycles = cycles;
  fake->status = FEEDING_BATCH_MOTOR_RUNNING;
  fake->observation_due = 0U;
  fake->batch_starts++;
  return 1U;
}

static uint8_t batch_status(void *context)
{
  return ((Fake *)context)->status;
}

static uint8_t observation_due(uint32_t now_ms, void *context)
{
  (void)now_ms;
  return ((Fake *)context)->observation_due;
}

static uint8_t start_observation(uint32_t now_ms,
                                 uint32_t *event_id,
                                 void *context)
{
  Fake *fake = context;
  (void)now_ms;
  fake->observation_starts++;
  *event_id = fake->event++;
  return 1U;
}

static void stop_motor(void *context)
{
  Fake *fake = context;
  fake->status = FEEDING_BATCH_IDLE;
  fake->stops++;
}

static FeedingOps make_ops(void)
{
  FeedingOps ops = {
      .start_batch = start_batch,
      .batch_status = batch_status,
      .observation_due = observation_due,
      .start_observation = start_observation,
      .stop_motor = stop_motor};
  return ops;
}

static void start_observation_now(Feeding *feeding,
                                  Fake *fake,
                                  uint32_t now_ms)
{
  fake->observation_due = 1U;
  Feeding_Process(feeding, now_ms);
  assert(fake->observation_starts > 0U);
  fake->observation_due = 0U;
}

static void test_result_is_buffered_until_batch_complete(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();

  fake.event = 1U;
  Feeding_Init(&feeding, &ops, &fake);
  assert(feeding.state == FEEDING_WAIT_KEY0);
  assert(Feeding_Unlock(&feeding, 0U));
  assert(feeding.state == FEEDING_INITIAL_RUNNING);
  assert(fake.batch_cycles == 1U);

  Feeding_Process(&feeding, 7999U);
  assert(fake.observation_starts == 0U);
  start_observation_now(&feeding, &fake, 8000U);
  assert(feeding.active_event_id == 1U);

  assert(Feeding_AcceptResult(&feeding, 1U, 1U, 2U, 18000U));
  assert(feeding.result_ready == 1U);
  assert(fake.batch_starts == 1U);
  assert(feeding.state == FEEDING_INITIAL_RUNNING);

  fake.status = FEEDING_BATCH_COMPLETE;
  Feeding_Process(&feeding, 18000U);
  assert(fake.batch_starts == 2U);
  assert(fake.batch_cycles == 2U);
  assert(feeding.state == FEEDING_BATCH_RUNNING);
}

static void test_batch_complete_waits_for_result(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();

  fake.event = 1U;
  Feeding_Init(&feeding, &ops, &fake);
  assert(Feeding_Unlock(&feeding, 0U));
  start_observation_now(&feeding, &fake, 8000U);
  fake.status = FEEDING_BATCH_COMPLETE;
  Feeding_Process(&feeding, 17000U);
  assert(feeding.state == FEEDING_OBSERVING);

  assert(Feeding_AcceptResult(&feeding, 1U, 1U, 1U, 18000U));
  assert(feeding.state == FEEDING_BATCH_RUNNING);
  assert(fake.batch_cycles == 1U);
}

static void test_zero_level_finishes_session(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();

  fake.event = 1U;
  Feeding_Init(&feeding, &ops, &fake);
  assert(Feeding_Unlock(&feeding, 0U));
  start_observation_now(&feeding, &fake, 8000U);
  assert(Feeding_AcceptResult(&feeding, 1U, 1U, 0U, 18000U));
  fake.status = FEEDING_BATCH_COMPLETE;
  Feeding_Process(&feeding, 18000U);
  assert(feeding.state == FEEDING_WAIT_KEY0);
  assert(feeding.completed_round_count == 1U);
}

static void test_stale_result_is_ignored(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();

  fake.event = 5U;
  Feeding_Init(&feeding, &ops, &fake);
  assert(Feeding_Unlock(&feeding, 0U));
  start_observation_now(&feeding, &fake, 8000U);
  assert(!Feeding_AcceptResult(&feeding, 4U, 1U, 2U, 18000U));
  assert(feeding.state == FEEDING_INITIAL_RUNNING);
  assert(feeding.fault == FEEDING_FAULT_NONE);
  assert(Feeding_IsObservationPending(&feeding));
}

static void test_observation_failure_locks(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();

  fake.event = 1U;
  Feeding_Init(&feeding, &ops, &fake);
  assert(Feeding_Unlock(&feeding, 0U));
  start_observation_now(&feeding, &fake, 8000U);
  Feeding_FailObservation(&feeding,
                          FEEDING_FAULT_OBSERVATION_TIMEOUT);
  assert(feeding.state == FEEDING_FAULT_LOCKED);
  assert(feeding.fault == FEEDING_FAULT_OBSERVATION_TIMEOUT);
  assert(fake.stops == 1U);
}

int main(void)
{
  test_result_is_buffered_until_batch_complete();
  test_batch_complete_waits_for_result();
  test_zero_level_finishes_session();
  test_stale_result_is_ignored();
  test_observation_failure_locks();
  (void)puts("feeding host tests: PASS");
  return 0;
}
