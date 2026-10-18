/**
 * @file app_display.c
 * @brief 负责 OLED 显示节奏、数值排版及显示缓存。
 */
#include "app_display.h"

#include <stddef.h>
#include <stdio.h>

#include "../modules/ssd1306/ssd1306.h"

#define APP_OLED_UPDATE_PERIOD_MS  1000U

static uint32_t s_oled_last_update_ms;
static bool s_oled_updated;
static uint16_t s_oled_last_ph_centi;
static bool s_oled_last_ph_valid;
static uint32_t s_oled_last_ph_sample_count;

/* 数值占七个字符并右对齐，正数不显示加号，单位由界面单独绘制。
 * 温度与 pH 的小数点使用同一列；调用方传入百分之一单位的幅值。 */
static void app_format_centi(char output[8], uint16_t magnitude, bool negative)
{
    for (uint32_t i = 0U; i < 7U; ++i)
    {
        output[i] = ' ';
    }
    output[7] = '\0';
    output[6] = (char) ('0' + (magnitude % 10U));
    output[5] = (char) ('0' + ((magnitude / 10U) % 10U));
    output[4] = '.';
    uint16_t whole = (uint16_t) (magnitude / 100U);
    int32_t column = 3;
    do
    {
        output[column--] = (char) ('0' + (whole % 10U));
        whole = (uint16_t) (whole / 10U);
    } while ((whole > 0U) && (column >= 0));
    if (negative && (column >= 0))
    {
        output[column] = '-';
    }
}

static void app_display_feeding(app_feeding_status_t const * feeding)
{
    char status[17];
    char last_level[8] = "LAST:--";

    switch (feeding->state)
    {
        case APP_FEED_WAIT_KEY:
            (void) snprintf(status, sizeof(status), "WAIT K4");
            break;
        case APP_FEED_RUNNING:
            (void) snprintf(status, sizeof(status), "FEED %u/%u",
                            (unsigned int) feeding->completed_cycles,
                            (unsigned int) feeding->planned_cycles);
            break;
        case APP_FEED_WAIT_RESULT:
            (void) snprintf(status, sizeof(status), "OBS WAIT");
            break;
        case APP_FEED_COMPLETE:
            (void) snprintf(status, sizeof(status), "DONE");
            break;
        case APP_FEED_FAULT_LOCKED:
        default:
            (void) snprintf(status, sizeof(status), "FAULT %u", (unsigned int) feeding->fault);
            break;
    }

    /* 新批次等待识别时继续显示上一份有效结果，避免把旧等级误作本次结果。 */
    if ((feeding->result_ready || (feeding->round_count > 0U)) && (feeding->result_level <= 2U))
    {
        last_level[5] = (char) ('0' + feeding->result_level);
        last_level[6] = '\0';
    }
    BSP_SSD1306_DrawText(3U, 0U, status);
    BSP_SSD1306_DrawText(3U, 9U, last_level);
}

static void app_display_update(uint32_t now_ms,
                               app_temperature_snapshot_t const * temperature,
                               app_ph_adc_snapshot_t const * ph,
                               app_feeding_status_t const * feeding)
{
    bool const temperature_error = (APP_TEMPERATURE_ERROR == temperature->state) ||
                                   (APP_TEMPERATURE_TIMEOUT == temperature->state) ||
                                   (APP_TEMPERATURE_TIMEBASE_ERROR == temperature->state);
    bool const ph_error = (APP_PH_ADC_ERROR == ph->state) ||
                          (APP_PH_ADC_TIMEOUT == ph->state) ||
                          (APP_PH_ADC_INIT_ERROR == ph->state);
    if (ph_error)
    {
        s_oled_last_ph_valid = false;
    }
    else if (ph->sample_count != s_oled_last_ph_sample_count)
    {
        s_oled_last_ph_sample_count = ph->sample_count;
        s_oled_last_ph_valid = ph->valid && ph->estimate_ready;
        if (s_oled_last_ph_valid)
        {
            s_oled_last_ph_centi = ph->estimate_centi;
        }
    }

    /* 每轮先接收有效估算，再限制绘制频率，避免错过 ADC 的有效结果窗口。 */
    if (s_oled_updated &&
        ((uint32_t) (now_ms - s_oled_last_update_ms) < APP_OLED_UPDATE_PERIOD_MS))
    {
        return;
    }
    s_oled_updated = true;
    s_oled_last_update_ms = now_ms;

    BSP_SSD1306_ClearBuffer();
    BSP_SSD1306_DrawText(0U, 4U, "水质监测");
    BSP_SSD1306_DrawText(1U, 0U, "水温:");
    BSP_SSD1306_DrawText(2U, 0U, "pH值:");

    if (temperature_error)
    {
        BSP_SSD1306_DrawText(1U, 8U, "异常");
    }
    else
    {
        char value[8] = "  --.--";
        if (temperature->valid)
        {
            int32_t const signed_centi = temperature->centi_c;
            uint16_t const magnitude = (uint16_t) ((signed_centi < 0) ? -signed_centi : signed_centi);
            app_format_centi(value, magnitude, signed_centi < 0);
        }
        BSP_SSD1306_DrawText(1U, 6U, value);
        BSP_SSD1306_DrawText(1U, 14U, "C");
    }

    if (ph_error)
    {
        BSP_SSD1306_DrawText(2U, 8U, "异常");
    }
    else
    {
        char value[8] = "  --.--";
        if (s_oled_last_ph_valid)
        {
            app_format_centi(value, s_oled_last_ph_centi, false);
        }
        BSP_SSD1306_DrawText(2U, 6U, value);
        if (s_oled_last_ph_valid && !ph->calibrated)
        {
            BSP_SSD1306_DrawText(2U, 14U, "*");
        }
    }

    app_display_feeding(feeding);
}

void App_Display_Init(void)
{
    s_oled_last_update_ms = 0U;
    s_oled_updated = false;
    s_oled_last_ph_centi = 0U;
    s_oled_last_ph_valid = false;
    s_oled_last_ph_sample_count = 0U;
    BSP_SSD1306_Init();
}

void App_Display_Process(uint32_t now_ms,
                         app_temperature_snapshot_t const * temperature,
                         app_ph_adc_snapshot_t const * ph,
                         app_feeding_status_t const * feeding)
{
    if ((NULL == temperature) || (NULL == ph) || (NULL == feeding))
    {
        return;
    }
    app_display_update(now_ms, temperature, ph, feeding);
    BSP_SSD1306_Process(now_ms);
}
