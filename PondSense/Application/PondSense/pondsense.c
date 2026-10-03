/**
 * @file    pondsense.c
 * @brief   PondSense 应用层流程编排与主循环处理。
 *
 * @details
 * 负责传感器、视觉、显示、通信、投喂状态机和看门狗的协同。
 * CubeMX 生成的入口 main.c 只通过 PondSense_Init/PondSense_Process 调用本模块。
 */

#include "pondsense.h"
#include "adc.h"
#include "button.h"
#include "display.h"
#include "dma.h"
#include "feed_motor.h"
#include "feeding.h"
#include "main.h"
#include "sensing.h"
#include "telemetry.h"
#include "tim.h"
#include "usart.h"
#include "vision.h"

/* 单个滑板伸出—收回循环的限时驱动时长。 */
#define FEED_MOTOR_RUN_MS              17000U
#define FEED_MOTOR_GAP_MS              1000U
#define FEEDING_OBSERVATION_DELAY_MS     8000U
#define KEY0_DEBOUNCE_MS                30U
#define IWDG_UPDATE_TIMEOUT_MS          100U

static Button key0;
static Feeding feeding;
static uint8_t batch_cycle_active;
static uint32_t batch_next_cycle_ms;
static uint8_t final_cycle_started;
static uint32_t final_cycle_start_ms;
static uint32_t next_event_id = 1U;

volatile HAL_StatusTypeDef sensing_init_status;
volatile HAL_StatusTypeDef telemetry_init_status;
volatile HAL_StatusTypeDef vision_init_status;
volatile uint32_t last_result_event_id;
volatile uint32_t last_result_valid;
volatile uint32_t last_result_level;
volatile uint32_t last_upload_event_id;
volatile uint32_t last_upload_status;
volatile uint32_t planned_batch_cycles;
volatile uint32_t pending_batch_cycles;
volatile uint32_t completed_batch_cycles;
volatile FeedingBatchStatus batch_status;

static void batch_process(uint32_t now_ms);

/* 准备包含 1 或 2 个机械循环的当前投喂批次。 */
static void batch_arm(uint8_t cycles, uint32_t now_ms)
{
  planned_batch_cycles = cycles;
  pending_batch_cycles = cycles;
  completed_batch_cycles = 0U;
  batch_cycle_active = 0U;
  batch_next_cycle_ms = now_ms;
  final_cycle_started = 0U;
  final_cycle_start_ms = 0U;
  batch_status = (cycles > 0U)
                     ? FEEDING_BATCH_WAITING
                     : FEEDING_BATCH_IDLE;
}

/* 非阻塞推进当前投喂批次，循环之间保留机械释放间隔。 */
static void batch_process(uint32_t now_ms)
{
  if (batch_cycle_active != 0U)
  {
    if (FeedMotor_IsRunning() != 0U)
    {
      return;
    }
    batch_cycle_active = 0U;
    completed_batch_cycles++;
    if (pending_batch_cycles == 0U)
    {
      batch_status = FEEDING_BATCH_COMPLETE;
      return;
    }
    batch_next_cycle_ms = now_ms + FEED_MOTOR_GAP_MS;
    batch_status = FEEDING_BATCH_WAITING;
  }

  if ((pending_batch_cycles == 0U) ||
      ((int32_t)(now_ms - batch_next_cycle_ms) < 0) ||
      (FeedMotor_IsRunning() != 0U))
  {
    return;
  }

  if (FeedMotor_Start(FEED_MOTOR_RUN_MS) == HAL_OK)
  {
    pending_batch_cycles--;
    batch_cycle_active = 1U;
    if (pending_batch_cycles == 0U)
    {
      final_cycle_started = 1U;
      final_cycle_start_ms = now_ms;
    }
    batch_status = FEEDING_BATCH_MOTOR_RUNNING;
  }
  else
  {
    pending_batch_cycles = 0U;
    batch_status = FEEDING_BATCH_FAILED;
  }
}

static uint8_t start_batch(uint8_t cycles,
                           uint32_t now_ms,
                           void *context)
{
  VisionState vision_state = Vision_GetState();

  (void)context;
  if ((FeedMotor_IsRunning() != 0U) ||
      (cycles == 0U) ||
      (cycles > 2U) ||
      (pending_batch_cycles != 0U) ||
      (batch_cycle_active != 0U) ||
      ((vision_state != VISION_IDLE) &&
       (vision_state != VISION_TIMED_OUT) &&
       (vision_state != VISION_REJECTED)))
  {
    return 0U;
  }
  batch_arm(cycles, now_ms);
  batch_process(now_ms);
  return (uint8_t)(batch_status != FEEDING_BATCH_FAILED);
}

static uint8_t observation_due(uint32_t now_ms, void *context)
{
  (void)context;
  return (uint8_t)((final_cycle_started != 0U) &&
                   ((uint32_t)(now_ms - final_cycle_start_ms) >=
                    FEEDING_OBSERVATION_DELAY_MS));
}

static uint8_t start_observation(uint32_t now_ms,
                                 uint32_t *event_id,
                                 void *context)
{
  uint32_t requested_event_id = next_event_id;
  (void)context;

  if (Vision_Start(requested_event_id, now_ms) != HAL_OK)
  {
    return 0U;
  }
  if (event_id != NULL)
  {
    *event_id = requested_event_id;
  }
  next_event_id++;
  if (next_event_id == 0U)
  {
    next_event_id = 1U;
  }
  return 1U;
}

static uint8_t get_batch_status(void *context)
{
  (void)context;
  return (uint8_t)batch_status;
}

static void stop_motor(void *context)
{
  (void)context;
  FeedMotor_Stop();
  batch_arm(0U, HAL_GetTick());
}

/* 使用寄存器启动独立看门狗，并等待预分频/重载值同步完成。 */
static void watchdog_start(void)
{
  uint32_t start_ms;

  __HAL_DBGMCU_FREEZE_IWDG();
  IWDG->KR = 0xCCCCU;
  IWDG->KR = 0x5555U;
  IWDG->PR = 4U;
  IWDG->RLR = 1999U;
  start_ms = HAL_GetTick();
  while ((IWDG->SR & (IWDG_SR_PVU | IWDG_SR_RVU)) != 0U)
  {
    if ((uint32_t)(HAL_GetTick() - start_ms) >= IWDG_UPDATE_TIMEOUT_MS)
    {
      Error_Handler();
    }
  }
  IWDG->KR = 0xAAAAU;
}

/**
 * @brief  初始化 PondSense 应用层模块。
 * @note   必须在 CubeMX 外设初始化完成后调用。
 */
void PondSense_Init(void)
{
  const FeedingOps feeding_ops = {
      .start_batch = start_batch,
      .batch_status = get_batch_status,
      .observation_due = observation_due,
      .start_observation = start_observation,
      .stop_motor = stop_motor};
  uint32_t now_ms = HAL_GetTick();

  FeedMotor_Init();
  Button_Init(&key0,
              KEY0_GPIO_Port,
              KEY0_Pin,
              KEY0_DEBOUNCE_MS,
              now_ms);
  Display_Init();
  sensing_init_status = Sensing_Init(&htim2,
                                     DS18B20_DQ_GPIO_Port,
                                     DS18B20_DQ_Pin,
                                     &hadc1,
                                     now_ms);
  telemetry_init_status = Telemetry_Init(&huart5, now_ms);
  vision_init_status = Vision_Init(&huart2);
  Feeding_Init(&feeding, &feeding_ops, NULL);
  watchdog_start();
}

/* 主循环应用调度：所有耗时模块均以非阻塞状态机方式推进。 */
/**
 * @brief  执行一次应用层周期调度。
 * @note   由 CubeMX main.c 无限循环调用，内部不主动等待外部事件。
 */
void PondSense_Process(void)
{
  PondSense_K230_FrameTypeDefData result;
  SensingSnapshot sensors;
  DisplayData display_data;
  VisionState vision_state;
  uint32_t now_ms = HAL_GetTick();

  Vision_Process(now_ms);
  if ((feeding.state == FEEDING_WAIT_KEY0) &&
      (Button_TakePress(&key0, now_ms) != 0U))
  {
    (void)Feeding_Unlock(&feeding, now_ms);
  }

  if (Vision_TakeResult(&result) != 0U)
  {
    last_result_event_id = result.event_id;
    last_result_valid = result.valid;
    last_result_level = result.level;
    last_upload_status = result.upload_status;
    (void)Feeding_AcceptResult(&feeding,
                               result.event_id,
                               result.valid,
                               result.level,
                               now_ms);
  }
  if (Vision_TakeUpload(&result) != 0U)
  {
    last_upload_event_id = result.event_id;
    last_upload_status = result.upload_status;
  }

  vision_state = Vision_GetState();
  if (Feeding_IsObservationPending(&feeding) != 0U)
  {
    if (vision_state == VISION_TIMED_OUT)
    {
      Feeding_FailObservation(&feeding,
                              FEEDING_FAULT_OBSERVATION_TIMEOUT);
    }
    else if (vision_state == VISION_REJECTED)
    {
      Feeding_FailObservation(&feeding,
                              FEEDING_FAULT_OBSERVATION_REJECTED);
    }
    else if (vision_state == VISION_ERROR)
    {
      Feeding_FailObservation(&feeding,
                              FEEDING_FAULT_OBSERVATION_LINK);
    }
  }

  Sensing_Process(now_ms);
  /* Use a fresh timestamp for motor sequencing after sensor processing. */
  now_ms = HAL_GetTick();
  batch_process(now_ms);
  Feeding_Process(&feeding, now_ms);
  Sensing_GetSnapshot(&sensors);

  display_data.vision_initialized =
      (uint8_t)(vision_init_status == HAL_OK);
  display_data.vision_state = vision_state;
  display_data.result_event_id = last_result_event_id;
  display_data.result_valid = (uint8_t)last_result_valid;
  display_data.result_level = (uint8_t)last_result_level;
  display_data.feeding_state = feeding.state;
  display_data.feeding_fault = feeding.fault;
  display_data.planned_cycles = planned_batch_cycles;
  display_data.completed_cycles = completed_batch_cycles;
  display_data.batch_status = batch_status;
  Display_Process(now_ms, &display_data);

  Telemetry_Process(now_ms,
                    &sensors,
                    (uint8_t)last_result_valid,
                    (uint8_t)last_result_level);
  IWDG->KR = 0xAAAAU;
  HAL_Delay(10U);
}

/**
 * @brief  转发 UART Receive-to-Idle DMA 接收完成事件。
 * @param  uart UART 句柄
 * @param  size 本次接收字节数
 */
void PondSense_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
  Vision_RxEventCallback(uart, size);
  Telemetry_RxEventCallback(uart, size);
}

/**
 * @brief  转发 UART 错误回调给视觉和遥测模块。
 * @param  uart 发生错误的 UART 句柄
 */
void PondSense_UartErrorCallback(UART_HandleTypeDef *uart)
{
  Vision_ErrorCallback(uart);
  Telemetry_ErrorCallback(uart);
}

/**
 * @brief  应用异常时立即停止投喂电机。
 */
void PondSense_EmergencyStop(void)
{
  FeedMotor_Stop();
}
