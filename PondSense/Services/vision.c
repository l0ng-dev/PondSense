/**
 * @file    vision.c
 * @brief   K230 视觉事件服务实现。
 *
 * @details
 * 管理 OBS/ACK/RESULT 事件状态、十秒超时、结果单次消费和上传事件缓存。
 */

#include "vision.h"
#include "k230_uart.h"
#include <stddef.h>
#include <string.h>

#define VISION_RESULT_TIMEOUT_MS 20000U

typedef struct
{
  K230Uart transport;
  char line[PONDSENSE_K230_FRAME_MAX_LENGTH];
  uint16_t line_length;
  uint8_t discarding_line;
  volatile VisionState state;
  uint32_t active_event_id;
  uint32_t last_completed_event_id;
  uint32_t deadline_ms;
  PondSense_K230_FrameTypeDefData result;
  PondSense_K230_FrameTypeDefData upload_result;
  volatile uint8_t upload_result_ready;
  volatile uint32_t valid_frame_count;
  volatile uint32_t crc_error_count;
  volatile uint32_t format_error_count;
  volatile uint32_t stale_frame_count;
  volatile uint32_t duplicate_frame_count;
} Vision;

static Vision vision;

/**
 * @brief  初始化 K230 视觉事件服务。
 * @param  uart 与 K230 连接的 UART 句柄
 * @retval HAL 状态
 */
HAL_StatusTypeDef Vision_Init(UART_HandleTypeDef *uart)
{
  if (uart == NULL)
  {
    return HAL_ERROR;
  }
  (void)memset(&vision, 0, sizeof(vision));
  vision.state = VISION_IDLE;
  return K230Uart_Init(&vision.transport, uart);
}

/**
 * @brief  发起一次带事件编号的 K230 观察请求。
 * @param  event_id 事件编号，不能为 0
 * @param  now_ms   当前系统毫秒时间
 * @retval HAL_OK 请求已发送；HAL_BUSY 当前已有活动事件；其他值表示发送失败
 */
HAL_StatusTypeDef Vision_Start(uint32_t event_id, uint32_t now_ms)
{
  char frame[PONDSENSE_K230_FRAME_MAX_LENGTH];
  int length;

  if ((event_id == 0U) ||
      ((vision.state != VISION_IDLE) &&
       (vision.state != VISION_TIMED_OUT) &&
       (vision.state != VISION_REJECTED)))
  {
    return HAL_BUSY;
  }
  length = PondSense_K230_FormatObserve(frame, sizeof(frame), event_id);
  if (length <= 0)
  {
    return HAL_ERROR;
  }
  if (K230Uart_Send(&vision.transport,
                    (const uint8_t *)frame,
                    (uint16_t)length) != HAL_OK)
  {
    vision.state = VISION_ERROR;
    return HAL_ERROR;
  }

  vision.active_event_id = event_id;
  vision.deadline_ms = now_ms + VISION_RESULT_TIMEOUT_MS;
  vision.state = VISION_WAIT_ACK;
  return HAL_OK;
}

/* 按事件编号和当前状态门控 ACK、RESULT、重复帧和过期帧。 */
static void handle_frame(const PondSense_K230_FrameTypeDefData *frame)
{
  if (frame->type == PONDSENSE_K230_FRAME_UPLOAD)
  {
    vision.upload_result = *frame;
    vision.upload_result_ready = 1U;
    return;
  }
  if (frame->event_id != vision.active_event_id)
  {
    if (frame->event_id == vision.last_completed_event_id)
    {
      vision.duplicate_frame_count++;
    }
    else
    {
      vision.stale_frame_count++;
    }
    return;
  }
  if (frame->type == PONDSENSE_K230_FRAME_ACK)
  {
    if (vision.state == VISION_WAIT_ACK)
    {
      vision.state = VISION_OBSERVING;
    }
    else
    {
      vision.duplicate_frame_count++;
    }
    return;
  }
  if (frame->type == PONDSENSE_K230_FRAME_NACK)
  {
    vision.state = VISION_REJECTED;
    return;
  }
  if (frame->type != PONDSENSE_K230_FRAME_RESULT)
  {
    vision.format_error_count++;
    return;
  }
  if ((vision.state != VISION_WAIT_ACK) &&
      (vision.state != VISION_OBSERVING))
  {
    vision.duplicate_frame_count++;
    return;
  }
  vision.result = *frame;
  vision.state = VISION_RESULT_READY;
}

static void process_line(void)
{
  PondSense_K230_FrameTypeDefData frame;
  PondSense_K230_ParseStatusTypeDef status;

  vision.line[vision.line_length] = '\0';
  status = PondSense_K230_ParseFrame(vision.line, &frame);
  if (status == PONDSENSE_K230_PARSE_OK)
  {
    vision.valid_frame_count++;
    handle_frame(&frame);
  }
  else if (status == PONDSENSE_K230_PARSE_CRC)
  {
    vision.crc_error_count++;
  }
  else
  {
    vision.format_error_count++;
  }
}

/* 非阻塞读取 UART 环形缓冲并推进视觉事件超时状态。 */
/**
 * @brief  处理 K230 接收字节并推进视觉状态超时。
 * @param  now_ms 当前系统毫秒时间
 */
void Vision_Process(uint32_t now_ms)
{
  uint8_t value;
  uint8_t transport_status = K230Uart_Process(&vision.transport);

  if (transport_status == 2U)
  {
    vision.state = VISION_ERROR;
  }
  else if ((transport_status == 1U) && (vision.state == VISION_ERROR))
  {
    vision.active_event_id = 0U;
    vision.state = VISION_TIMED_OUT;
  }

  while (K230Uart_ReadByte(&vision.transport, &value) != 0U)
  {
    if ((value == '\n') || (value == '\r'))
    {
      if ((vision.line_length > 0U) && (vision.discarding_line == 0U))
      {
        process_line();
      }
      vision.line_length = 0U;
      vision.discarding_line = 0U;
    }
    else if (vision.discarding_line == 0U)
    {
      if (vision.line_length < (PONDSENSE_K230_FRAME_MAX_LENGTH - 1U))
      {
        vision.line[vision.line_length++] = (char)value;
      }
      else
      {
        vision.discarding_line = 1U;
        vision.format_error_count++;
      }
    }
  }

  if (((vision.state == VISION_WAIT_ACK) ||
       (vision.state == VISION_OBSERVING)) &&
      ((int32_t)(now_ms - vision.deadline_ms) >= 0))
  {
    vision.state = VISION_TIMED_OUT;
  }
}

VisionState Vision_GetState(void)
{
  return vision.state;
}

/**
 * @brief  取出并消费一次匹配当前事件的 RESULT。
 * @param  result 输出结果地址
 * @retval 1 已取出；0 当前没有可消费结果
 */
uint8_t Vision_TakeResult(PondSense_K230_FrameTypeDefData *result)
{
  if ((result == NULL) || (vision.state != VISION_RESULT_READY))
  {
    return 0U;
  }
  *result = vision.result;
  vision.last_completed_event_id = result->event_id;
  vision.active_event_id = 0U;
  vision.state = VISION_IDLE;
  return 1U;
}

/**
 * @brief  取出 K230 异步图片上传事件。
 * @param  result 输出事件地址
 * @retval 1 已取出；0 没有新的上传事件
 */
uint8_t Vision_TakeUpload(PondSense_K230_FrameTypeDefData *result)
{
  if ((result == NULL) || (vision.upload_result_ready == 0U))
  {
    return 0U;
  }
  *result = vision.upload_result;
  vision.upload_result_ready = 0U;
  return 1U;
}

void Vision_RxEventCallback(UART_HandleTypeDef *uart, uint16_t size)
{
  K230Uart_RxEventCallback(&vision.transport, uart, size);
}

void Vision_ErrorCallback(UART_HandleTypeDef *uart)
{
  K230Uart_ErrorCallback(&vision.transport, uart);
}
