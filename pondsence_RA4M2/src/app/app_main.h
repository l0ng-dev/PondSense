#ifndef APP_MAIN_H
#define APP_MAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "../modules/ds18b20/ds18b20.h"

typedef enum
{
    APP_TEMPERATURE_NO_SAMPLE = 0,
    APP_TEMPERATURE_CONVERTING,
    APP_TEMPERATURE_VALID,
    APP_TEMPERATURE_ERROR,
    APP_TEMPERATURE_TIMEOUT,
    APP_TEMPERATURE_TIMEBASE_ERROR
} app_temperature_state_t;

/* 仅由主循环写入；valid 为 false 时 centi_c 只是上次成功的读数。
 * driver_result 对应当前状态；TIMEOUT 时为 NOT_READY。 */
typedef struct
{
    int16_t centi_c;
    uint32_t last_sample_ms;
    uint32_t sample_count;
    bsp_ds18b20_result_t driver_result;
    app_temperature_state_t state;
    bool valid;
} app_temperature_snapshot_t;

extern app_temperature_snapshot_t g_app_temperature;

typedef enum
{
    APP_PH_ADC_NO_SAMPLE = 0,
    APP_PH_ADC_SCANNING,
    APP_PH_ADC_VALID,
    APP_PH_ADC_ERROR,
    APP_PH_ADC_TIMEOUT,
    APP_PH_ADC_INIT_ERROR
} app_ph_adc_state_t;

/* 仅由主循环写入；valid 为 false 时 raw 为上次成功的原始 ADC 值。
 * estimate_centi 只作未校准的临时估算；只有 estimate_ready 为 true 时可读取。 */
typedef struct
{
    uint16_t raw;
    uint32_t last_sample_ms;
    uint32_t sample_count;
    uint32_t driver_result; /* fsp_err_t 数值，0 表示 FSP_SUCCESS。 */
    app_ph_adc_state_t state;
    bool valid;
    uint16_t average_raw;
    uint16_t span_raw;
    uint8_t window_count;
    uint16_t estimate_centi;
    bool estimate_ready;
    bool calibrated;
} app_ph_adc_snapshot_t;

extern app_ph_adc_snapshot_t g_app_ph_adc;

void App_Main(void);

#endif
