/**
 * @file    test_k230.c
 * @brief   K230 事件协议主机端回归测试。
 *
 * @details
 * 验证帧格式化、CRC 校验、解析和边界输入，不替代 UART/K230 实板联调。
 */

#include "../../Middleware/K230/k230.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                                       \
  do                                                                           \
  {                                                                            \
    if (!(condition))                                                          \
    {                                                                          \
      (void)fprintf(stderr, "CHECK failed at line %d: %s\n",                 \
                    __LINE__, #condition);                                      \
      return 1;                                                                \
    }                                                                          \
  } while (0)

static void format_frame(char *buffer, size_t size, const char *payload)
{
  (void)snprintf(buffer, size, "$%s*%02X\n", payload,
                 (unsigned int)PondSense_K230_Crc8(
                     (const uint8_t *)payload, strlen(payload)));
}

int main(void)
{
  char frame_text[PONDSENSE_K230_FRAME_MAX_LENGTH];
  PondSense_K230_FrameTypeDefData frame;

  CHECK(PondSense_K230_FormatObserve(frame_text, sizeof(frame_text), 42U) > 0);
  CHECK(strcmp(frame_text, "$PS1,OBS,42*12\n") == 0);
  CHECK(PondSense_K230_ParseFrame(frame_text, &frame) ==
        PONDSENSE_K230_PARSE_OK);
  CHECK(frame.type == PONDSENSE_K230_FRAME_OBS && frame.event_id == 42U);

  format_frame(frame_text, sizeof(frame_text), "PS1,ACK,42");
  CHECK(PondSense_K230_ParseFrame(frame_text, &frame) ==
        PONDSENSE_K230_PARSE_OK);
  CHECK(frame.type == PONDSENSE_K230_FRAME_ACK);

  format_frame(frame_text, sizeof(frame_text), "PS1,UPLOAD,42,2");
  CHECK(PondSense_K230_ParseFrame(frame_text, &frame) == PONDSENSE_K230_PARSE_OK);
  CHECK(frame.type == PONDSENSE_K230_FRAME_UPLOAD && frame.upload_status == 2U);

  (void)snprintf(frame_text, sizeof(frame_text), "%s",
                 "$PS1,RESULT,42,1,2,1*5B\n");
  CHECK(PondSense_K230_ParseFrame(frame_text, &frame) ==
        PONDSENSE_K230_PARSE_OK);
  CHECK(frame.type == PONDSENSE_K230_FRAME_RESULT);
  CHECK(frame.event_id == 42U && frame.valid == 1U && frame.level == 2U &&
        frame.upload_status == 1U);

  frame_text[5] = (frame_text[5] == 'A') ? 'B' : 'A';
  CHECK(PondSense_K230_ParseFrame(frame_text, &frame) ==
        PONDSENSE_K230_PARSE_CRC);

  format_frame(frame_text, sizeof(frame_text), "PS1,RESULT,42,0,1,2");
  CHECK(PondSense_K230_ParseFrame(frame_text, &frame) ==
        PONDSENSE_K230_PARSE_RANGE);
  CHECK(PondSense_K230_FormatObserve(frame_text, sizeof(frame_text), 0U) == -1);

  (void)puts("k230 host tests: PASS");
  return 0;
}
