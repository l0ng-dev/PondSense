#ifndef APP_FEEDING_H
#define APP_FEEDING_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    APP_FEED_WAIT_KEY = 0,
    APP_FEED_RUNNING,
    APP_FEED_WAIT_RESULT,
    APP_FEED_COMPLETE,
    APP_FEED_FAULT_LOCKED
} app_feeding_state_t;

typedef enum
{
    APP_FEED_FAULT_NONE = 0,
    APP_FEED_FAULT_KEY_READ,
    APP_FEED_FAULT_MOTOR_START,
    APP_FEED_FAULT_MOTOR_STOP,
    APP_FEED_FAULT_OBSERVE_START,
    APP_FEED_FAULT_OBSERVE_TIMEOUT,
    APP_FEED_FAULT_OBSERVE_REJECTED,
    APP_FEED_FAULT_INVALID_RESULT
} app_feeding_fault_t;

/* 仅由主循环写入，供 Keil Watch 观察；不代表机械位置或出料量。 */
typedef struct
{
    app_feeding_state_t state;
    app_feeding_fault_t fault;
    uint32_t round_count;
    uint32_t last_event_id;
    uint32_t ignored_result_count;
    uint32_t key_press_count;
    uint32_t last_error;
    uint8_t planned_cycles;
    uint8_t completed_cycles;
    uint8_t result_level;
    bool result_ready;
} app_feeding_status_t;

extern app_feeding_status_t g_app_feeding;

void App_Feeding_Init(void);
void App_Feeding_Process(uint32_t now_ms);

#endif
