/**
 * @file runtime.c
 * @brief 初始化工程并调度应用模块、毫秒时基和 IWDT。
 */
#include "runtime.h"

#include "hal_data.h"
#include "sensing.h"
#include "display.h"
#include "k230_session.h"
#include "network.h"
#include "feeding.h"
#include "../modules/feed_motor/feed_motor.h"
#include "../modules/uart_links/uart_links.h"

#define APP_IWDT_REFRESH_PERIOD_MS  1000U

static volatile uint32_t s_uptime_ms;

app_iwdt_status_t g_app_iwdt_status;

/* AGT0 中断更新毫秒计数，并检查电机硬停机期限。 */
void app_tick_callback(timer_callback_args_t * p_args)
{
    if ((NULL != p_args) && (TIMER_EVENT_CYCLE_END == p_args->event))
    {
        ++s_uptime_ms;
        BSP_FeedMotor_Process(s_uptime_ms);
    }
}

void App_Runtime_Run(void)
{
    /* IOPORT 已由 FSP 启动流程打开；先将两路电机输入再次置低。
     * 初始化失败保留在 g_feed_motor_status，启动请求会被拒绝。 */
    (void) BSP_FeedMotor_Init();

    fsp_err_t result = R_IWDT_Open(&g_iwdt0_ctrl, &g_iwdt0_cfg);
    g_app_iwdt_status.last_error = (uint32_t) result;
    if (FSP_SUCCESS != result)
    {
        for (;;)
        {
            __WFI();
        }
    }
    g_app_iwdt_status.initialized = true;

    result = R_IWDT_Refresh(&g_iwdt0_ctrl);
    g_app_iwdt_status.last_error = (uint32_t) result;
    if (FSP_SUCCESS != result)
    {
        for (;;)
        {
            __WFI();
        }
    }
    g_app_iwdt_status.refresh_count = 1U;

    result = R_AGT_Open(&g_tick_timer_ctrl, &g_tick_timer_cfg);
    if (FSP_SUCCESS == result)
    {
        result = R_AGT_Start(&g_tick_timer_ctrl);
    }
    if (FSP_SUCCESS != result)
    {
        g_app_temperature.state = APP_TEMPERATURE_TIMEBASE_ERROR;
        for (;;)
        {
            __WFI();
        }
    }

    App_Display_Init();
    Uart_Links_Init();
    App_K230_Init();
    App_Feeding_Init();
    App_Network_Init(s_uptime_ms);

    for (;;)
    {
        App_Sensing_Process(s_uptime_ms);
        App_Network_Process(s_uptime_ms);
        Uart_Links_Process();
        App_Feeding_Process(s_uptime_ms);
        App_K230_Process(s_uptime_ms);
        App_Display_Process(s_uptime_ms, &g_app_temperature, &g_app_ph_adc, &g_app_feeding);
        uint32_t const now_ms = s_uptime_ms;
        if ((uint32_t) (now_ms - g_app_iwdt_status.last_refresh_ms) >= APP_IWDT_REFRESH_PERIOD_MS)
        {
            result = R_IWDT_Refresh(&g_iwdt0_ctrl);
            g_app_iwdt_status.last_error = (uint32_t) result;
            if (FSP_SUCCESS != result)
            {
                (void) BSP_FeedMotor_Stop();
                for (;;)
                {
                    __WFI();
                }
            }
            g_app_iwdt_status.last_refresh_ms = now_ms;
            ++g_app_iwdt_status.refresh_count;
        }
        __WFI();
    }
}
