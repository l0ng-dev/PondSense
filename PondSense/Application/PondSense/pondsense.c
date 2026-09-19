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
#define FEEDING_PERIOD_MS               60000U
#define KEY0_DEBOUNCE_MS                30U
#define IWDG_UPDATE_TIMEOUT_MS          100U

static Button key0;
static Feeding feeding;
static uint8_t additional_active;
static uint32_t additional_next_ms;
static uint32_t next_event_id = 1U;

volatile HAL_StatusTypeDef sensing_init_status;
volatile HAL_StatusTypeDef telemetry_init_status;
volatile HAL_StatusTypeDef vision_init_status;
volatile uint32_t last_result_event_id;
volatile uint32_t last_result_valid;
volatile uint32_t last_result_level;
volatile uint32_t last_upload_event_id;
volatile uint32_t last_upload_status;
volatile uint32_t planned_additional_cycles;
volatile uint32_t pending_additional_cycles;
volatile uint32_t completed_additional_cycles;
volatile uint8_t additional_status;

/* 根据 K230 返回的等级准备 0/1/2 个追加循环。 */
static void additional_arm(uint8_t cycles, uint32_t now_ms)
{
  planned_additional_cycles = cycles;
  pending_additional_cycles = cycles;
  completed_additional_cycles = 0U;
  additional_active = 0U;
  additional_next_ms = now_ms;
  additional_status = (cycles > 0U)
                          ? FEEDING_ADDITIONAL_WAITING
                          : FEEDING_ADDITIONAL_IDLE;
}

/* 非阻塞推进追加投喂，循环之间保留机械释放间隔。 */
static void additional_process(uint32_t now_ms)
{
  if (additional_active != 0U)
  {
    if (FeedMotor_IsRunning() != 0U)
    {
      return;
    }
    additional_active = 0U;
    completed_additional_cycles++;
    if (pending_additional_cycles == 0U)
    {
      additional_status = FEEDING_ADDITIONAL_COMPLETE;
      return;
    }
    additional_next_ms = now_ms + FEED_MOTOR_GAP_MS;
    additional_status = FEEDING_ADDITIONAL_WAITING;
  }

  if ((pending_additional_cycles == 0U) ||
      ((int32_t)(now_ms - additional_next_ms) < 0) ||
      (FeedMotor_IsRunning() != 0U))
  {
    return;
  }

  if (FeedMotor_Start(FEED_MOTOR_RUN_MS) == HAL_OK)
  {
    pending_additional_cycles--;
    additional_active = 1U;
    additional_status = FEEDING_ADDITIONAL_MOTOR_RUNNING;
  }
  else
  {
    pending_additional_cycles = 0U;
    additional_status = FEEDING_ADDITIONAL_FAILED;
  }
}

static uint8_t start_initial(void *context)
{
  VisionState vision_state = Vision_GetState();

  (void)context;
  if ((FeedMotor_IsRunning() != 0U) ||
      (pending_additional_cycles != 0U) ||
      (additional_active != 0U) ||
      ((vision_state != VISION_IDLE) &&
       (vision_state != VISION_TIMED_OUT) &&
       (vision_state != VISION_REJECTED)))
  {
    return 0U;
  }
  additional_arm(0U, HAL_GetTick());
  return (uint8_t)(FeedMotor_Start(FEED_MOTOR_RUN_MS) == HAL_OK);
}

static uint8_t motor_running(void *context)
{
  (void)context;
  return FeedMotor_IsRunning();
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

static void arm_additional(uint8_t cycles,
                           uint32_t now_ms,
                           void *context)
{
  (void)context;
  additional_arm(cycles, now_ms);
}

static uint8_t get_additional_status(void *context)
{
  (void)context;
  return additional_status;
}

static void stop_motor(void *context)
{
  (void)context;
  FeedMotor_Stop();
  additional_arm(0U, HAL_GetTick());
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
      start_initial,
      motor_running,
      start_observation,
      arm_additional,
      get_additional_status,
      stop_motor};
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
  Feeding_Init(&feeding,
               FEEDING_PERIOD_MS,
               &feeding_ops,
               NULL);
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
    (void)Feeding_Unlock(&feeding);
  }

  if (Vision_TakeResult(&result) != 0U)
  {
    last_result_event_id = result.event_id;
    last_result_valid = result.valid;
    last_result_level = result.level;
    last_upload_status = result.upload_status;
    if (feeding.state == FEEDING_OBSERVING)
    {
      (void)Feeding_AcceptResult(&feeding,
                                 result.event_id,
                                 result.valid,
                                 result.level,
                                 now_ms);
    }
  }
  if (Vision_TakeUpload(&result) != 0U)
  {
    last_upload_event_id = result.event_id;
    last_upload_status = result.upload_status;
  }

  vision_state = Vision_GetState();
  if (feeding.state == FEEDING_OBSERVING)
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
  additional_process(now_ms);
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
  display_data.planned_cycles = planned_additional_cycles;
  display_data.completed_cycles = completed_additional_cycles;
  display_data.additional_status = additional_status;
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
