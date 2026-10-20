#ifndef APP_K230_H
#define APP_K230_H

#include <stdint.h>

#include "bsp_api.h"
#include "../protocol/k230/k230.h"

#define APP_K230_RESULT_TIMEOUT_MS 20000U

typedef enum
{
    APP_K230_IDLE = 0,
    APP_K230_WAIT_ACK,
    APP_K230_OBSERVING,
    APP_K230_COMPLETE,
    APP_K230_TIMED_OUT,
    APP_K230_REJECTED,
    APP_K230_SEND_ERROR
} app_k230_state_t;

/* 只由主循环修改，供 Keil Watch 观察；last_result 在 COMPLETE 后保持有效。 */
typedef struct
{
    app_k230_state_t state;
    uint32_t next_event_id;
    uint32_t active_event_id;
    uint32_t last_completed_event_id;
    uint32_t started_ms;
    uint32_t last_write_result;
    uint32_t request_count;
    uint32_t busy_request_count;
    uint32_t ack_count;
    uint32_t completed_count;
    uint32_t rejected_count;
    uint32_t timeout_count;
    uint32_t send_error_count;
    uint32_t duplicate_count;
    uint32_t stale_count;
    uint32_t unexpected_count;
    uint32_t upload_count;
    PondSense_K230_FrameTypeDefData last_result;
    PondSense_K230_FrameTypeDefData last_upload;
} app_k230_status_t;

extern app_k230_status_t g_app_k230;

void App_K230_Init(void);
fsp_err_t App_K230_StartObserve(uint32_t now_ms);
void App_K230_Process(uint32_t now_ms);

#endif
