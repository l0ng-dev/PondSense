/**
 * @file    test_feeding.c
 * @brief   投喂状态机主机端回归测试。
 *
 * @details
 * 使用模拟 FeedingOps 验证初次投喂、观察、等级追加和故障状态转换，
 * 不替代 STM32 实板电机和机构验收。
 */

#include "../../Application/PondSense/feeding.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

typedef struct
{
  uint8_t motor;
  uint8_t additional;
  uint8_t status;
  uint32_t event;
  uint32_t starts;
  uint32_t stops;
} Fake;

static uint8_t start_initial(void *context)
{
  ((Fake *)context)->motor = 1U;
  return 1U;
}

static uint8_t motor_running(void *context)
{
  return ((Fake *)context)->motor;
}

static uint8_t start_observation(uint32_t now_ms,
                                 uint32_t *event_id,
                                 void *context)
{
  Fake *fake = context;
  (void)now_ms;
  fake->starts++;
  *event_id = fake->event++;
  return 1U;
}

static void arm_additional(uint8_t cycles,
                           uint32_t now_ms,
                           void *context)
{
  Fake *fake = context;
  (void)now_ms;
  fake->additional = cycles;
  fake->status = cycles != 0U
                     ? FEEDING_ADDITIONAL_WAITING
                     : FEEDING_ADDITIONAL_IDLE;
}

static uint8_t additional_status(void *context)
{
  return ((Fake *)context)->status;
}

static void stop_motor(void *context)
{
  Fake *fake = context;
  fake->motor = 0U;
  fake->stops++;
}

static FeedingOps make_ops(void)
{
  FeedingOps ops = {
      start_initial,
      motor_running,
      start_observation,
      arm_additional,
      additional_status,
      stop_motor};
  return ops;
}

static void test_timeline(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();

  fake.event = 1U;
  Feeding_Init(&feeding, 60000U, &ops, &fake);
  assert(feeding.state == FEEDING_WAIT_KEY0);
  assert(Feeding_Unlock(&feeding));
  fake.motor = 0U;
  Feeding_Process(&feeding, 14000U);
  assert(feeding.next_deadline_ms == 74000U);
  Feeding_Process(&feeding, 73999U);
  assert(fake.starts == 0U);
  Feeding_Process(&feeding, 74000U);
  assert(fake.starts == 1U);
  assert(Feeding_AcceptResult(&feeding, 1U, 1U, 2U, 84000U));
  assert(fake.additional == 2U);
  fake.status = FEEDING_ADDITIONAL_COMPLETE;
  Feeding_Process(&feeding, 113000U);
  assert(feeding.state == FEEDING_AUTO_WAIT);
  Feeding_Process(&feeding, 134000U);
  assert(fake.starts == 2U);
  assert(feeding.next_deadline_ms == 194000U);
}

static void test_failure_lock(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();
  uint32_t index;

  fake.event = 1U;
  Feeding_Init(&feeding, 10U, &ops, &fake);
  assert(Feeding_Unlock(&feeding));
  fake.motor = 0U;
  Feeding_Process(&feeding, 1U);
  for (index = 0U; index < 3U; index++)
  {
    Feeding_Process(&feeding, 11U + index * 10U);
    Feeding_FailObservation(&feeding,
                            FEEDING_FAULT_OBSERVATION_TIMEOUT);
  }
  assert(feeding.state == FEEDING_FAULT_LOCKED);
  assert(feeding.consecutive_failure_count == 3U);
  assert(fake.stops == 1U);
}

static void test_tick_wrap(void)
{
  Feeding feeding;
  Fake fake = {0};
  FeedingOps ops = make_ops();

  fake.event = 1U;
  Feeding_Init(&feeding, 100U, &ops, &fake);
  assert(Feeding_Unlock(&feeding));
  fake.motor = 0U;
  Feeding_Process(&feeding, 0xFFFFFFF0U);
  assert(feeding.next_deadline_ms == 84U);
  Feeding_Process(&feeding, 84U);
  assert(fake.starts == 1U);
}

int main(void)
{
  test_timeline();
  test_failure_lock();
  test_tick_wrap();
  (void)puts("feeding host tests: PASS");
  return 0;
}
