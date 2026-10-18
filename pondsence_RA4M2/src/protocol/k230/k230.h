/**
 * @file    k230.h
 * @brief   K230 事件协议公共接口。
 *
 * @details
 * 定义事件帧类型、解析结果、摄食等级和协议编解码函数。
 */

#ifndef K230_H
#define K230_H

#include <stddef.h>
#include <stdint.h>

#define PONDSENSE_K230_FRAME_MAX_LENGTH 96U

typedef enum
{
  PONDSENSE_K230_FRAME_NONE = 0,
  PONDSENSE_K230_FRAME_OBS,
  PONDSENSE_K230_FRAME_ACK,
  PONDSENSE_K230_FRAME_RESULT,
  PONDSENSE_K230_FRAME_UPLOAD,
  PONDSENSE_K230_FRAME_NACK
} PondSense_K230_FrameTypeDef;

typedef enum
{
  PONDSENSE_K230_PARSE_OK = 0,
  PONDSENSE_K230_PARSE_ARGUMENT,
  PONDSENSE_K230_PARSE_FORMAT,
  PONDSENSE_K230_PARSE_CRC,
  PONDSENSE_K230_PARSE_RANGE
} PondSense_K230_ParseStatusTypeDef;

typedef struct
{
  PondSense_K230_FrameTypeDef type;
  uint32_t event_id;
  uint8_t valid;
  uint8_t level;
  uint8_t upload_status;
} PondSense_K230_FrameTypeDefData;

uint8_t PondSense_K230_Crc8(const uint8_t *data, size_t length);
int PondSense_K230_FormatObserve(char *buffer,
                                 size_t buffer_size,
                                 uint32_t event_id);
PondSense_K230_ParseStatusTypeDef PondSense_K230_ParseFrame(
    const char *line,
    PondSense_K230_FrameTypeDefData *frame);

#endif
