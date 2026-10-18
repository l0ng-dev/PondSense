#ifndef BSP_FEED_MOTOR_H
#define BSP_FEED_MOTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "bsp_api.h"

#define BSP_FEED_MOTOR_MAX_RUN_MS 17000U

typedef struct
{
    fsp_err_t last_error;
    uint32_t start_ms;
    uint32_t duration_ms;
    uint32_t timeout_count;
    bool initialized;
    bool running;
} bsp_feed_motor_status_t;

/* 供 Keil Watch 观察；字段由主循环和 1 ms 时基回调更新，不是原子快照。 */
extern volatile bsp_feed_motor_status_t g_feed_motor_status;

/* RASC 须先将 P500/P402 配为默认低电平的 GPIO 输出。初始化会再次写低两脚。 */
fsp_err_t BSP_FeedMotor_Init(void);
/* 单次最长 17 秒；只有显式请求才启动，不能从中断调用。 */
fsp_err_t BSP_FeedMotor_Start(uint32_t now_ms, uint32_t duration_ms);
/* 立即拉低两路输出；可从主循环或时基回调调用。 */
fsp_err_t BSP_FeedMotor_Stop(void);
/* 由 1 ms 时基回调调用，超时后停机；允许 now_ms 回绕。 */
void BSP_FeedMotor_Process(uint32_t now_ms);

#endif
