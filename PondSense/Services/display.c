/**
 * @file    display.c
 * @brief   LCD 状态显示服务实现。
 *
 * @details
 * 周期性组织温度、pH、K230 和投喂状态文本，仅在内容变化时刷新对应区域，
 * 避免整屏频繁重绘影响主循环。
 */

#include "display.h"
#include "lcd.h"
#include "sensing.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define DISPLAY_PERIOD_MS   100U
#define DISPLAY_X           20U
#define DISPLAY_END_X       299U
#define DISPLAY_TEMP_Y      60U
#define DISPLAY_PH_Y        104U
#define DISPLAY_VISION_Y    148U
#define DISPLAY_RESULT_Y    176U
#define DISPLAY_FEED_Y      204U
#define DISPLAY_LINE_END(y) ((uint16_t)((y) + 23U))

static uint32_t next_refresh_ms;
static char cached_temperature[32];
static char cached_ph[32];
static char cached_vision[32];
static char cached_result[32];
static char cached_feeding[32];

static void draw_line(uint16_t y,
                      const char *line,
                      uint16_t color,
                      char *cached,
                      size_t cached_size)
{
  if (strncmp(line, cached, cached_size) == 0)
  {
    return;
  }
  lcd_fill(DISPLAY_X,
           y,
           DISPLAY_END_X,
           DISPLAY_LINE_END(y),
           WHITE);
  lcd_show_string(DISPLAY_X,
                  y,
                  DISPLAY_END_X - DISPLAY_X + 1U,
                  24U,
                  24U,
                  line,
                  color);
  (void)snprintf(cached, cached_size, "%s", line);
}

static uint16_t sensor_color(const char *text)
{
  if ((strstr(text, "ERROR") != NULL) ||
      (strstr(text, "UNSTABLE") != NULL) ||
      (strstr(text, "LIMIT") != NULL) ||
      (strstr(text, "NO DEVICE") != NULL) ||
      (strstr(text, "OUT OF RANGE") != NULL))
  {
    return RED;
  }
  return BLUE;
}

/* 将视觉链路状态转换为短文本，供 LCD 局部刷新使用。 */
static void format_vision(const DisplayData *data,
                          char *line,
                          size_t size,
                          uint16_t *color)
{
  if (data->vision_initialized == 0U)
  {
    (void)snprintf(line, size, "K230: INIT ERROR");
    *color = RED;
    return;
  }
  switch (data->vision_state)
  {
    case VISION_IDLE:
      (void)snprintf(line, size, "K230: READY");
      *color = GREEN;
      break;
    case VISION_WAIT_ACK:
      (void)snprintf(line, size, "K230: WAIT ACK");
      *color = BLUE;
      break;
    case VISION_OBSERVING:
      (void)snprintf(line, size, "K230: OBSERVING");
      *color = BLUE;
      break;
    case VISION_RESULT_READY:
      (void)snprintf(line, size, "K230: RESULT READY");
      *color = GREEN;
      break;
    case VISION_TIMED_OUT:
      (void)snprintf(line, size, "K230: TIMEOUT");
      *color = RED;
      break;
    case VISION_REJECTED:
      (void)snprintf(line, size, "K230: REJECTED");
      *color = RED;
      break;
    case VISION_ERROR:
    default:
      (void)snprintf(line, size, "K230: LINK ERROR");
      *color = RED;
      break;
  }
}

static void format_result(const DisplayData *data,
                          char *line,
                          size_t size,
                          uint16_t *color)
{
  if (data->result_valid == 0U)
  {
    (void)snprintf(line, size, "RESULT: --");
    *color = BLACK;
  }
  else if (data->result_level <= 2U)
  {
    (void)snprintf(line,
                   size,
                   "RESULT E%lu: LEVEL %u",
                   (unsigned long)data->result_event_id,
                   (unsigned int)data->result_level);
    *color = GREEN;
  }
  else
  {
    (void)snprintf(line, size, "RESULT: INVALID");
    *color = RED;
  }
}

/* 显示初次投喂、观察、追加循环和故障锁定状态。 */
static void format_feeding(const DisplayData *data,
                           char *line,
                           size_t size,
                           uint16_t *color)
{
  uint32_t displayed_cycle;

  switch (data->feeding_state)
  {
    case FEEDING_WAIT_KEY0:
      (void)snprintf(line, size, "FEED: WAIT KEY0");
      *color = GREEN;
      break;
    case FEEDING_INITIAL_RUNNING:
      (void)snprintf(line, size, "FEED: INITIAL");
      *color = BLUE;
      break;
    case FEEDING_AUTO_WAIT:
      (void)snprintf(line, size, "FEED: AUTO RUN");
      *color = GREEN;
      break;
    case FEEDING_OBSERVING:
      (void)snprintf(line, size, "FEED: WAIT RESULT");
      *color = BLUE;
      break;
    case FEEDING_ADDITIONAL_RUNNING:
      displayed_cycle = data->completed_cycles;
      if ((data->additional_status == FEEDING_ADDITIONAL_MOTOR_RUNNING) &&
          (displayed_cycle < data->planned_cycles))
      {
        displayed_cycle++;
      }
      (void)snprintf(line,
                     size,
                     "FEED: EXTRA %lu/%lu",
                     (unsigned long)displayed_cycle,
                     (unsigned long)data->planned_cycles);
      *color = BLUE;
      break;
    case FEEDING_FAULT_LOCKED:
    default:
      (void)snprintf(line,
                     size,
                     "FEED: FAULT %u",
                     (unsigned int)data->feeding_fault);
      *color = RED;
      break;
  }
}

/**
 * @brief  初始化 LCD 显示服务和缓存文本。
 */
void Display_Init(void)
{
  cached_temperature[0] = '\0';
  cached_ph[0] = '\0';
  cached_vision[0] = '\0';
  cached_result[0] = '\0';
  cached_feeding[0] = '\0';
  next_refresh_ms = 0U;

  lcd_init();
  lcd_display_dir(1U);
  lcd_clear(WHITE);
  lcd_show_string(20U, 16U, 280U, 24U, 24U, "PondSense Sensors", BLACK);
}

/**
 * @brief  周期刷新温度、pH、视觉和投喂状态显示。
 * @param  now_ms 当前系统毫秒时间
 * @param  data   当前显示数据快照
 * @note   仅在内容变化时刷新对应区域。
 */
void Display_Process(uint32_t now_ms, const DisplayData *data)
{
  char line[32];
  uint16_t color;

  if ((data == NULL) || ((int32_t)(now_ms - next_refresh_ms) < 0))
  {
    return;
  }
  next_refresh_ms = now_ms + DISPLAY_PERIOD_MS;

  draw_line(DISPLAY_TEMP_Y,
            Sensing_TemperatureText(),
            sensor_color(Sensing_TemperatureText()),
            cached_temperature,
            sizeof(cached_temperature));
  draw_line(DISPLAY_PH_Y,
            Sensing_PhText(),
            sensor_color(Sensing_PhText()),
            cached_ph,
            sizeof(cached_ph));

  format_vision(data, line, sizeof(line), &color);
  draw_line(DISPLAY_VISION_Y,
            line,
            color,
            cached_vision,
            sizeof(cached_vision));
  format_result(data, line, sizeof(line), &color);
  draw_line(DISPLAY_RESULT_Y,
            line,
            color,
            cached_result,
            sizeof(cached_result));
  format_feeding(data, line, sizeof(line), &color);
  draw_line(DISPLAY_FEED_Y,
            line,
            color,
            cached_feeding,
            sizeof(cached_feeding));
}
