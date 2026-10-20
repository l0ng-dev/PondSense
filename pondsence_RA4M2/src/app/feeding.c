#include "feeding.h"

#include "hal_data.h"
#include "k230_session.h"
#include "../modules/feed_motor/feed_motor.h"

#define FEED_CYCLE_MS          17000U
#define FEED_CYCLE_GAP_MS       1000U
#define FEED_OBSERVE_DELAY_MS   8000U
#define FEED_KEY_DEBOUNCE_MS      30U

app_feeding_status_t g_app_feeding;

static bool s_unlocked;
static bool s_cycle_active;
static bool s_observation_started;
static uint8_t s_pending_cycles;
static uint32_t s_cycle_start_ms;
static uint32_t s_next_cycle_ms;
static uint32_t s_cycle_timeout_count;
static uint32_t s_active_event_id;
static uint32_t s_seen_completed_count;
static bsp_io_level_t s_last_key_level = BSP_IO_LEVEL_LOW;
static bsp_io_level_t s_stable_key_level = BSP_IO_LEVEL_LOW;
static uint32_t s_key_changed_ms;

static void feeding_fault(app_feeding_fault_t fault)
{
    fsp_err_t const stop_result = BSP_FeedMotor_Stop();
    if (FSP_SUCCESS != stop_result)
    {
        g_app_feeding.last_error = (uint32_t) stop_result;
    }
    g_app_feeding.fault = fault;
    g_app_feeding.state = APP_FEED_FAULT_LOCKED;
    s_cycle_active = false;
    s_pending_cycles = 0U;
    s_active_event_id = 0U;
}

static bool feeding_start_cycle(uint32_t now_ms)
{
    fsp_err_t const result = BSP_FeedMotor_Start(now_ms, FEED_CYCLE_MS);
    g_app_feeding.last_error = (uint32_t) result;
    if (FSP_SUCCESS != result)
    {
        feeding_fault(APP_FEED_FAULT_MOTOR_START);
        return false;
    }
    --s_pending_cycles;
    s_cycle_active = true;
    s_cycle_start_ms = now_ms;
    s_cycle_timeout_count = g_feed_motor_status.timeout_count;
    return true;
}

static void feeding_start_batch(uint8_t cycles, uint32_t now_ms)
{
    s_seen_completed_count = g_app_k230.completed_count;
    g_app_feeding.planned_cycles = cycles;
    g_app_feeding.completed_cycles = 0U;
    g_app_feeding.result_ready = false;
    s_observation_started = false;
    s_active_event_id = 0U;
    s_pending_cycles = cycles;
    g_app_feeding.state = APP_FEED_RUNNING;
    (void) feeding_start_cycle(now_ms);
}

static void feeding_key_process(uint32_t now_ms)
{
    bsp_io_level_t level;
    fsp_err_t const result = R_IOPORT_PinRead(&g_ioport_ctrl, K4, &level);
    if (FSP_SUCCESS != result)
    {
        g_app_feeding.last_error = (uint32_t) result;
        feeding_fault(APP_FEED_FAULT_KEY_READ);
        return;
    }
    if (level != s_last_key_level)
    {
        s_last_key_level = level;
        s_key_changed_ms = now_ms;
        return;
    }
    if (((uint32_t) (now_ms - s_key_changed_ms) < FEED_KEY_DEBOUNCE_MS) ||
        (level == s_stable_key_level))
    {
        return;
    }
    s_stable_key_level = level;
    if ((BSP_IO_LEVEL_LOW == level) && !s_unlocked &&
        (APP_FEED_WAIT_KEY == g_app_feeding.state))
    {
        ++g_app_feeding.key_press_count;
        s_unlocked = true;
        feeding_start_batch(1U, now_ms);
    }
}

static void feeding_accept_result(void)
{
    if (g_app_k230.completed_count == s_seen_completed_count)
    {
        return;
    }
    s_seen_completed_count = g_app_k230.completed_count;
    if (!s_observation_started ||
        (g_app_k230.last_completed_event_id != s_active_event_id) ||
        (g_app_feeding.result_ready))
    {
        ++g_app_feeding.ignored_result_count;
        return;
    }
    g_app_feeding.last_event_id = s_active_event_id;
    s_active_event_id = 0U;
    s_observation_started = false;
    if ((0U == g_app_k230.last_result.valid) || (g_app_k230.last_result.level > 2U))
    {
        feeding_fault(APP_FEED_FAULT_INVALID_RESULT);
        return;
    }
    g_app_feeding.result_level = g_app_k230.last_result.level;
    g_app_feeding.result_ready = true;
}

static void feeding_observation_process(uint32_t now_ms)
{
    if (s_observation_started)
    {
        if (APP_K230_TIMED_OUT == g_app_k230.state)
        {
            feeding_fault(APP_FEED_FAULT_OBSERVE_TIMEOUT);
        }
        else if ((APP_K230_REJECTED == g_app_k230.state) ||
                 (APP_K230_SEND_ERROR == g_app_k230.state))
        {
            feeding_fault(APP_FEED_FAULT_OBSERVE_REJECTED);
        }
        return;
    }
    if (g_app_feeding.result_ready || !s_cycle_active || (0U != s_pending_cycles) ||
        ((uint32_t) (now_ms - s_cycle_start_ms) < FEED_OBSERVE_DELAY_MS))
    {
        return;
    }
    fsp_err_t const result = App_K230_StartObserve(now_ms);
    g_app_feeding.last_error = (uint32_t) result;
    if (FSP_SUCCESS != result)
    {
        feeding_fault(APP_FEED_FAULT_OBSERVE_START);
        return;
    }
    s_active_event_id = g_app_k230.active_event_id;
    s_observation_started = true;
}

void App_Feeding_Init(void)
{
    g_app_feeding.state = APP_FEED_WAIT_KEY;
    g_app_feeding.fault = APP_FEED_FAULT_NONE;
    s_seen_completed_count = g_app_k230.completed_count;
}

void App_Feeding_Process(uint32_t now_ms)
{
    if (APP_FEED_FAULT_LOCKED == g_app_feeding.state)
    {
        return;
    }
    if (APP_FEED_WAIT_KEY == g_app_feeding.state)
    {
        feeding_key_process(now_ms);
        return;
    }
    if (APP_FEED_COMPLETE == g_app_feeding.state)
    {
        return;
    }

    feeding_accept_result();
    if (APP_FEED_FAULT_LOCKED == g_app_feeding.state)
    {
        return;
    }
    feeding_observation_process(now_ms);
    if (APP_FEED_FAULT_LOCKED == g_app_feeding.state)
    {
        return;
    }

    if (s_cycle_active && !g_feed_motor_status.running)
    {
        if ((g_feed_motor_status.timeout_count != s_cycle_timeout_count + 1U) ||
            (FSP_SUCCESS != g_feed_motor_status.last_error))
        {
            feeding_fault(APP_FEED_FAULT_MOTOR_STOP);
            return;
        }
        s_cycle_active = false;
        ++g_app_feeding.completed_cycles;
        s_next_cycle_ms = now_ms + FEED_CYCLE_GAP_MS;
    }
    if ((0U != s_pending_cycles) && !s_cycle_active &&
        ((int32_t) (now_ms - s_next_cycle_ms) >= 0))
    {
        (void) feeding_start_cycle(now_ms);
        return;
    }
    if (!s_cycle_active && (0U == s_pending_cycles))
    {
        if (!g_app_feeding.result_ready)
        {
            g_app_feeding.state = APP_FEED_WAIT_RESULT;
        }
        else
        {
            ++g_app_feeding.round_count;
            if (0U == g_app_feeding.result_level)
            {
                g_app_feeding.state = APP_FEED_COMPLETE;
            }
            else
            {
                feeding_start_batch(g_app_feeding.result_level, now_ms);
            }
        }
    }
}
