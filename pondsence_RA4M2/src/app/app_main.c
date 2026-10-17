/**
 * @file app_main.c
 * @brief 调度温度与 pH ADC 采样状态机、未校准临时估算及显示层调用。
 */
#include "app_main.h"

#include "hal_data.h"
#include "app_display.h"

#define APP_TEMPERATURE_PERIOD_MS  1000U
#define APP_CONVERSION_TIMEOUT_MS  1000U
#define APP_PH_ADC_PERIOD_MS       1000U
#define APP_PH_ADC_TIMEOUT_MS      100U
#define APP_PH_FILTER_WINDOW       5U
#define APP_PH_STABILIZATION_MS    5000U
#define APP_PH_STABLE_MAX_SPAN     20U
#define APP_PH_ADC_LOW_LIMIT       10U
#define APP_PH_ADC_HIGH_LIMIT      4085U
#define APP_PH_ADC_FULL_SCALE      4095.0f
#define APP_PH_MAX_VALUE           14.0f
#define APP_PH_CENTI_SCALE         100.0f

/* F407 sensing.c 的临时估算参数；尚未在 RA 信号链上校准。 */
#define APP_PH_REFERENCE_ADC       1539.0f
#define APP_PH_REFERENCE_VALUE     6.8f
#define APP_PH_ADC_REFERENCE_V     3.3f
#define APP_PH_NOMINAL_SLOPE_PER_V (-5.6342f)

static volatile uint32_t s_uptime_ms;

typedef struct
{
    bsp_ds18b20_t sensor;
    uint32_t last_attempt_ms;
    uint32_t last_poll_ms;
    bool attempted;
    bool conversion_active;
    bool sensor_initialized;
} app_temperature_context_t;

static app_temperature_context_t s_temperature;

typedef struct
{
    uint32_t last_attempt_ms;
    bool attempted;
    bool scanning;
    bool initialized;
} app_ph_adc_context_t;

static app_ph_adc_context_t s_ph_adc;
static uint16_t s_ph_samples[APP_PH_FILTER_WINDOW];
/* 五个 12 位采样值之和最大为 20475，uint32_t 足够容纳。 */
static uint32_t s_ph_sum;
static uint8_t s_ph_count;
static uint8_t s_ph_index;

app_temperature_snapshot_t g_app_temperature =
{
    .driver_result = BSP_DS18B20_NO_CONVERSION,
    .state = APP_TEMPERATURE_NO_SAMPLE
};

app_ph_adc_snapshot_t g_app_ph_adc =
{
    .state = APP_PH_ADC_NO_SAMPLE
};

/* AGT0 中断只更新毫秒计数；主循环只读取这个对齐的 32 位值。 */
void app_tick_callback(timer_callback_args_t * p_args)
{
    if ((NULL != p_args) && (TIMER_EVENT_CYCLE_END == p_args->event))
    {
        ++s_uptime_ms;
    }
}

static void app_ph_filter_reset(void)
{
    s_ph_sum = 0U;
    s_ph_count = 0U;
    s_ph_index = 0U;
    g_app_ph_adc.average_raw = 0U;
    g_app_ph_adc.span_raw = 0U;
    g_app_ph_adc.window_count = 0U;
    g_app_ph_adc.estimate_ready = false;
}

static void app_ph_filter_update(uint16_t raw, uint32_t now_ms)
{
    if (s_ph_count < APP_PH_FILTER_WINDOW)
    {
        s_ph_samples[s_ph_index] = raw;
        s_ph_sum += raw;
        ++s_ph_count;
    }
    else
    {
        s_ph_sum -= s_ph_samples[s_ph_index];
        s_ph_samples[s_ph_index] = raw;
        s_ph_sum += raw;
    }
    s_ph_index = (uint8_t) ((s_ph_index + 1U) % APP_PH_FILTER_WINDOW);
    g_app_ph_adc.window_count = s_ph_count;
    g_app_ph_adc.estimate_ready = false;

    if ((s_ph_count < APP_PH_FILTER_WINDOW) || (now_ms < APP_PH_STABILIZATION_MS))
    {
        return;
    }

    uint16_t minimum = s_ph_samples[0];
    uint16_t maximum = s_ph_samples[0];
    for (uint8_t index = 1U; index < APP_PH_FILTER_WINDOW; ++index)
    {
        if (s_ph_samples[index] < minimum)
        {
            minimum = s_ph_samples[index];
        }
        if (s_ph_samples[index] > maximum)
        {
            maximum = s_ph_samples[index];
        }
    }

    g_app_ph_adc.average_raw = (uint16_t) (s_ph_sum / APP_PH_FILTER_WINDOW);
    g_app_ph_adc.span_raw = (uint16_t) (maximum - minimum);
    if ((g_app_ph_adc.average_raw <= APP_PH_ADC_LOW_LIMIT) ||
        (g_app_ph_adc.average_raw >= APP_PH_ADC_HIGH_LIMIT) ||
        (g_app_ph_adc.span_raw > APP_PH_STABLE_MAX_SPAN))
    {
        return;
    }

    float const adc_delta_v = ((float) g_app_ph_adc.average_raw - APP_PH_REFERENCE_ADC) *
                              APP_PH_ADC_REFERENCE_V / APP_PH_ADC_FULL_SCALE;
    float const estimate = APP_PH_REFERENCE_VALUE +
                           APP_PH_NOMINAL_SLOPE_PER_V * adc_delta_v;
    if ((estimate >= 0.0f) && (estimate <= APP_PH_MAX_VALUE))
    {
        g_app_ph_adc.estimate_centi = (uint16_t) (estimate * APP_PH_CENTI_SCALE + 0.5f);
        g_app_ph_adc.estimate_ready = true;
    }
}

static void app_temperature_process(uint32_t now_ms)
{
    if (s_temperature.conversion_active)
    {
        if ((uint32_t) (now_ms - s_temperature.last_attempt_ms) >= APP_CONVERSION_TIMEOUT_MS)
        {
            s_temperature.conversion_active = false;
            s_temperature.last_attempt_ms = now_ms;
            g_app_temperature.state = APP_TEMPERATURE_TIMEOUT;
            g_app_temperature.driver_result = BSP_DS18B20_NOT_READY;
            g_app_temperature.valid = false;
            bsp_ds18b20_result_t const init_result = BSP_DS18B20_Init(&s_temperature.sensor);
            s_temperature.sensor_initialized = (BSP_DS18B20_OK == init_result);
            if (BSP_DS18B20_OK != init_result)
            {
                g_app_temperature.state = APP_TEMPERATURE_ERROR;
                g_app_temperature.driver_result = init_result;
            }
            return;
        }

        if (s_temperature.last_poll_ms == now_ms)
        {
            return;
        }
        s_temperature.last_poll_ms = now_ms;

        int16_t sample_centi_c;
        bsp_ds18b20_result_t const result = BSP_DS18B20_ReadCentiC(&s_temperature.sensor,
                                                                      now_ms,
                                                                      &sample_centi_c);
        if (BSP_DS18B20_NOT_READY == result)
        {
            return;
        }
        s_temperature.conversion_active = false;
        g_app_temperature.driver_result = result;
        if (BSP_DS18B20_OK == result)
        {
            g_app_temperature.centi_c = sample_centi_c;
            g_app_temperature.last_sample_ms = now_ms;
            ++g_app_temperature.sample_count;
            g_app_temperature.valid = true;
            g_app_temperature.state = APP_TEMPERATURE_VALID;
        }
        else
        {
            g_app_temperature.valid = false;
            g_app_temperature.state = APP_TEMPERATURE_ERROR;
            if ((BSP_DS18B20_IO_ERROR == result) || (BSP_DS18B20_NOT_INITIALIZED == result))
            {
                s_temperature.sensor_initialized = false;
            }
        }
        return;
    }

    if (s_temperature.attempted &&
        ((uint32_t) (now_ms - s_temperature.last_attempt_ms) < APP_TEMPERATURE_PERIOD_MS))
    {
        return;
    }

    s_temperature.attempted = true;
    s_temperature.last_attempt_ms = now_ms;
    if (!s_temperature.sensor_initialized)
    {
        bsp_ds18b20_result_t const init_result = BSP_DS18B20_Init(&s_temperature.sensor);
        g_app_temperature.driver_result = init_result;
        if (BSP_DS18B20_OK != init_result)
        {
            g_app_temperature.valid = false;
            g_app_temperature.state = APP_TEMPERATURE_ERROR;
            return;
        }
        s_temperature.sensor_initialized = true;
    }

    bsp_ds18b20_result_t const result = BSP_DS18B20_StartConversion(&s_temperature.sensor, now_ms);
    g_app_temperature.driver_result = result;
    if (BSP_DS18B20_OK == result)
    {
        s_temperature.conversion_active = true;
        s_temperature.last_poll_ms = now_ms;
        g_app_temperature.state = APP_TEMPERATURE_CONVERTING;
    }
    else
    {
        g_app_temperature.valid = false;
        g_app_temperature.state = APP_TEMPERATURE_ERROR;
        if ((BSP_DS18B20_IO_ERROR == result) || (BSP_DS18B20_NOT_INITIALIZED == result))
        {
            s_temperature.sensor_initialized = false;
        }
    }
}

static void app_ph_adc_process(uint32_t now_ms)
{
    if (s_ph_adc.scanning)
    {
        adc_status_t status;
        fsp_err_t const result = R_ADC_StatusGet(&g_adc0_ctrl, &status);
        if (FSP_SUCCESS != result)
        {
            (void) R_ADC_ScanStop(&g_adc0_ctrl);
            s_ph_adc.scanning = false;
            g_app_ph_adc.driver_result = result;
            g_app_ph_adc.state = APP_PH_ADC_ERROR;
            g_app_ph_adc.valid = false;
            app_ph_filter_reset();
            return;
        }

        if (ADC_STATE_SCAN_IN_PROGRESS == status.state)
        {
            if ((uint32_t) (now_ms - s_ph_adc.last_attempt_ms) >= APP_PH_ADC_TIMEOUT_MS)
            {
                fsp_err_t const stop_result = R_ADC_ScanStop(&g_adc0_ctrl);
                s_ph_adc.scanning = false;
                g_app_ph_adc.driver_result = (FSP_SUCCESS == stop_result) ? FSP_ERR_TIMEOUT : stop_result;
                g_app_ph_adc.state = (FSP_SUCCESS == stop_result) ? APP_PH_ADC_TIMEOUT : APP_PH_ADC_ERROR;
                g_app_ph_adc.valid = false;
                app_ph_filter_reset();
            }
            return;
        }

        uint16_t raw;
        fsp_err_t const read_result = R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_12, &raw);
        s_ph_adc.scanning = false;
        g_app_ph_adc.driver_result = read_result;
        if (FSP_SUCCESS == read_result)
        {
            g_app_ph_adc.raw = raw;
            g_app_ph_adc.last_sample_ms = now_ms;
            ++g_app_ph_adc.sample_count;
            g_app_ph_adc.state = APP_PH_ADC_VALID;
            g_app_ph_adc.valid = true;
            app_ph_filter_update(raw, now_ms);
        }
        else
        {
            g_app_ph_adc.state = APP_PH_ADC_ERROR;
            g_app_ph_adc.valid = false;
            app_ph_filter_reset();
        }
        return;
    }

    if (s_ph_adc.attempted &&
        ((uint32_t) (now_ms - s_ph_adc.last_attempt_ms) < APP_PH_ADC_PERIOD_MS))
    {
        return;
    }

    s_ph_adc.attempted = true;
    s_ph_adc.last_attempt_ms = now_ms;
    if (!s_ph_adc.initialized)
    {
        fsp_err_t result = R_ADC_Open(&g_adc0_ctrl, &g_adc0_cfg);
        if (FSP_SUCCESS == result)
        {
            result = R_ADC_ScanCfg(&g_adc0_ctrl, &g_adc0_channel_cfg);
            if (FSP_SUCCESS != result)
            {
                (void) R_ADC_Close(&g_adc0_ctrl);
            }
        }
        if (FSP_SUCCESS != result)
        {
            g_app_ph_adc.driver_result = result;
            g_app_ph_adc.state = APP_PH_ADC_INIT_ERROR;
            g_app_ph_adc.valid = false;
            app_ph_filter_reset();
            return;
        }
        s_ph_adc.initialized = true;
    }

    fsp_err_t const result = R_ADC_ScanStart(&g_adc0_ctrl);
    g_app_ph_adc.driver_result = result;
    g_app_ph_adc.valid = false;
    g_app_ph_adc.estimate_ready = false;
    if (FSP_SUCCESS == result)
    {
        s_ph_adc.scanning = true;
        g_app_ph_adc.state = APP_PH_ADC_SCANNING;
    }
    else
    {
        g_app_ph_adc.state = APP_PH_ADC_ERROR;
        app_ph_filter_reset();
    }
}

void App_Main(void)
{
    fsp_err_t result = R_AGT_Open(&g_tick_timer_ctrl, &g_tick_timer_cfg);
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

    for (;;)
    {
        app_temperature_process(s_uptime_ms);
        app_ph_adc_process(s_uptime_ms);
        App_Display_Process(s_uptime_ms, &g_app_temperature, &g_app_ph_adc);
        __WFI();
    }
}
