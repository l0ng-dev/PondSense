/**
 * @file    display.h
 * @brief   LCD 状态显示服务接口。
 *
 * @details
 * 定义显示服务输入快照和初始化、周期刷新接口。
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "feeding.h"
#include "vision.h"
#include <stdint.h>

typedef struct
{
  uint8_t vision_initialized;
  VisionState vision_state;
  uint32_t result_event_id;
  uint8_t result_valid;
  uint8_t result_level;
  FeedingState feeding_state;
  FeedingFault feeding_fault;
  uint32_t planned_cycles;
  uint32_t completed_cycles;
  uint8_t additional_status;
} DisplayData;

void Display_Init(void);
void Display_Process(uint32_t now_ms, const DisplayData *data);

#endif
