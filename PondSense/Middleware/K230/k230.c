/**
 * @file    k230.c
 * @brief   K230 事件协议编解码实现。
 *
 * @details
 * 实现 CRC-8、观察/确认/结果/图片上传事件帧的格式化和解析，
 * 为 UART 驱动与应用状态机提供纯协议层能力。
 */

#include "k230.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t PondSense_K230_Crc8(const uint8_t *data, size_t length)
{
  /* 使用 CRC-8/ATM 多项式 0x07 校验协议 payload。 */
  uint8_t crc = 0U;
  size_t index;

  if (data == NULL)
  {
    return 0U;
  }
  for (index = 0U; index < length; index++)
  {
    uint8_t bit;
    crc ^= data[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      crc = ((crc & 0x80U) != 0U) ? (uint8_t)((crc << 1U) ^ 0x07U)
                                  : (uint8_t)(crc << 1U);
    }
  }
  return crc;
}

/**
 * @brief  生成带 CRC 的 K230 OBS 观察请求帧。
 * @param  buffer      输出缓冲区
 * @param  buffer_size 输出缓冲区大小
 * @param  event_id    观察事件编号
 * @retval 正数为帧长度；负数表示参数或缓冲区错误
 */
int PondSense_K230_FormatObserve(char *buffer,
                                 size_t buffer_size,
                                 uint32_t event_id)
{
  char payload[48];
  int payload_length;
  int frame_length;

  if ((buffer == NULL) || (buffer_size == 0U) || (event_id == 0U))
  {
    return -1;
  }
  payload_length = snprintf(payload, sizeof(payload),
                            "PS1,OBS,%lu", (unsigned long)event_id);
  if ((payload_length <= 0) || ((size_t)payload_length >= sizeof(payload)))
  {
    buffer[0] = '\0';
    return -1;
  }
  frame_length = snprintf(buffer, buffer_size, "$%s*%02X\n",
                          payload,
                          (unsigned int)PondSense_K230_Crc8(
                              (const uint8_t *)payload,
                              (size_t)payload_length));
  if ((frame_length <= 0) || ((size_t)frame_length >= buffer_size))
  {
    buffer[0] = '\0';
    return -1;
  }
  return frame_length;
}

/* 严格解析非零事件编号，拒绝空值、溢出和尾随字符。 */
static uint8_t parse_u32(const char *text, uint32_t *value)
{
  char *end;
  unsigned long parsed;

  if ((text == NULL) || (*text == '\0') || (value == NULL))
  {
    return 0U;
  }
  parsed = strtoul(text, &end, 10);
  if ((*end != '\0') || (parsed == 0UL) || (parsed > 0xFFFFFFFFUL))
  {
    return 0U;
  }
  *value = (uint32_t)parsed;
  return 1U;
}

static int hex_value(char value)
{
  if ((value >= '0') && (value <= '9'))
  {
    return value - '0';
  }
  if ((value >= 'A') && (value <= 'F'))
  {
    return value - 'A' + 10;
  }
  return -1;
}

/**
 * @brief  校验并解析一帧 K230 文本协议。
 * @param  line 以 '$' 开头并带 CRC 的协议行
 * @param  frame 输出帧数据
 * @retval 解析状态，包括成功、格式错误、CRC 错误和范围错误
 */
PondSense_K230_ParseStatusTypeDef PondSense_K230_ParseFrame(
    const char *line,
    PondSense_K230_FrameTypeDefData *frame)
{
  char payload[PONDSENSE_K230_FRAME_MAX_LENGTH];
  char *fields[7];
  char *cursor;
  char *asterisk;
  size_t payload_length;
  size_t field_count = 0U;
  int crc_high;
  int crc_low;
  uint32_t value;

  if ((line == NULL) || (frame == NULL))
  {
    return PONDSENSE_K230_PARSE_ARGUMENT;
  }
  (void)memset(frame, 0, sizeof(*frame));
  if (line[0] != '$')
  {
    return PONDSENSE_K230_PARSE_FORMAT;
  }
  asterisk = strchr(line, '*');
  if ((asterisk == NULL) || (asterisk <= &line[1]) ||
      (asterisk[1] == '\0') || (asterisk[2] == '\0') ||
      ((asterisk[3] != '\0') && (asterisk[3] != '\r') &&
       (asterisk[3] != '\n')))
  {
    return PONDSENSE_K230_PARSE_FORMAT;
  }
  payload_length = (size_t)(asterisk - &line[1]);
  if (payload_length >= sizeof(payload))
  {
    return PONDSENSE_K230_PARSE_FORMAT;
  }
  (void)memcpy(payload, &line[1], payload_length);
  payload[payload_length] = '\0';
  crc_high = hex_value(asterisk[1]);
  crc_low = hex_value(asterisk[2]);
  if ((crc_high < 0) || (crc_low < 0))
  {
    return PONDSENSE_K230_PARSE_FORMAT;
  }
  if (PondSense_K230_Crc8((const uint8_t *)payload, payload_length) !=
      (uint8_t)((crc_high << 4) | crc_low))
  {
    return PONDSENSE_K230_PARSE_CRC;
  }

  cursor = payload;
  while ((cursor != NULL) && (field_count < (sizeof(fields) / sizeof(fields[0]))))
  {
    char *comma = strchr(cursor, ',');
    fields[field_count++] = cursor;
    if (comma == NULL)
    {
      cursor = NULL;
    }
    else
    {
      *comma = '\0';
      cursor = comma + 1;
    }
  }
  if ((cursor != NULL) || (field_count < 3U) ||
      (strcmp(fields[0], "PS1") != 0) ||
      (parse_u32(fields[2], &frame->event_id) == 0U))
  {
    return PONDSENSE_K230_PARSE_FORMAT;
  }
  if ((strcmp(fields[1], "OBS") == 0) && (field_count == 3U))
  {
    frame->type = PONDSENSE_K230_FRAME_OBS;
    return PONDSENSE_K230_PARSE_OK;
  }
  if ((strcmp(fields[1], "ACK") == 0) && (field_count == 3U))
  {
    frame->type = PONDSENSE_K230_FRAME_ACK;
    return PONDSENSE_K230_PARSE_OK;
  }
  if ((strcmp(fields[1], "NACK") == 0) && (field_count == 4U))
  {
    frame->type = PONDSENSE_K230_FRAME_NACK;
    return PONDSENSE_K230_PARSE_OK;
  }
  if ((strcmp(fields[1], "UPLOAD") == 0) && (field_count == 4U))
  {
    if ((fields[3][0] < '1') || (fields[3][0] > '2') ||
        (fields[3][1] != '\0'))
    {
      return PONDSENSE_K230_PARSE_RANGE;
    }
    frame->upload_status = (uint8_t)(fields[3][0] - '0');
    frame->type = PONDSENSE_K230_FRAME_UPLOAD;
    return PONDSENSE_K230_PARSE_OK;
  }
  if ((strcmp(fields[1], "RESULT") != 0) || (field_count != 6U))
  {
    return PONDSENSE_K230_PARSE_FORMAT;
  }
  if ((parse_u32(fields[3], &value) == 0U) || (value > 1U))
  {
    return PONDSENSE_K230_PARSE_RANGE;
  }
  frame->valid = (uint8_t)value;
  if ((fields[4][0] == '\0') || (fields[4][1] != '\0') ||
      (fields[4][0] < '0') || (fields[4][0] > '2'))
  {
    return PONDSENSE_K230_PARSE_RANGE;
  }
  frame->level = (uint8_t)(fields[4][0] - '0');
  if ((fields[5][0] == '\0') || (fields[5][1] != '\0') ||
      (fields[5][0] < '0') || (fields[5][0] > '2'))
  {
    return PONDSENSE_K230_PARSE_RANGE;
  }
  frame->upload_status = (uint8_t)(fields[5][0] - '0');
  if ((frame->valid == 0U) && (frame->level != 0U))
  {
    return PONDSENSE_K230_PARSE_RANGE;
  }
  frame->type = PONDSENSE_K230_FRAME_RESULT;
  return PONDSENSE_K230_PARSE_OK;
}
