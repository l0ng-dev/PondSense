#include "ssd1306.h"

#include <string.h>
#include "ssd1306_font.h"
#include "hal_data.h"

/* RA4M2-SENSOR V1.0 的直连引脚：CN4 第 6 针为 P000，第 7 针为 P001。
 * 两脚均为开漏输出，写高表示释放线路，不主动输出高电平。
 * 总线需要上拉到 3.3 V；P407 接有板载电容 C19，因此不用于 OLED。 */
#define OLED_SCL_PIN             BSP_IO_PORT_00_PIN_00
#define OLED_SDA_PIN             BSP_IO_PORT_00_PIN_01
#define OLED_ADDRESS             0x3CU
#define OLED_WIDTH               128U
#define OLED_PAGES               8U
#define OLED_DATA_CHUNK          16U
#define OLED_HALF_CLOCK_US       5U
#define OLED_CLOCK_WAIT_US       1000U
#define OLED_RETRY_DELAY_MS      1000U
#define OLED_POWER_DELAY_MS      100U

typedef enum
{
    OLED_OPEN = 0,
    OLED_POWER_WAIT,
    OLED_INIT_SEND,
    OLED_READY,
    OLED_PAGE_COMMAND_SEND,
    OLED_PAGE_DATA_SEND
} oled_phase_t;

static uint8_t s_frame[OLED_WIDTH * OLED_PAGES];
static uint8_t s_tx[OLED_DATA_CHUNK + 1U];
static oled_phase_t s_phase;
static uint32_t s_phase_since_ms;
static uint32_t s_clock_wait_us;
static uint8_t s_page;
static uint8_t s_column;
static bool s_dirty;

volatile bsp_ssd1306_status_t g_oled_status = {.last_error = FSP_SUCCESS};

static uint8_t const s_init_commands[] =
{
    0x00U, 0xAEU, 0xD5U, 0x80U, 0xA8U, 0x3FU, 0xD3U, 0x00U,
    0x40U, 0xA1U, 0xC8U, 0xDAU, 0x12U, 0x81U, 0xCFU, 0xD9U,
    0xF1U, 0xDBU, 0x30U, 0xA4U, 0xA6U, 0x8DU, 0x14U,
    0x20U, 0x02U, 0xAFU
};

static void bus_delay(void)
{
    R_BSP_SoftwareDelay(OLED_HALF_CLOCK_US, BSP_DELAY_UNITS_MICROSECONDS);
}

static fsp_err_t bus_set(bsp_io_port_pin_t pin, bsp_io_level_t level)
{
    return R_IOPORT_PinWrite(&g_ioport_ctrl, pin, level);
}

static fsp_err_t bus_read(bsp_io_port_pin_t pin, bsp_io_level_t * level)
{
    return R_IOPORT_PinRead(&g_ioport_ctrl, pin, level);
}

static void bus_release(void)
{
    (void) bus_set(OLED_SDA_PIN, BSP_IO_LEVEL_HIGH);
    (void) bus_set(OLED_SCL_PIN, BSP_IO_LEVEL_HIGH);
}

/* 整个事务共用一份 SCL 等待预算，传输期间保持中断开启。
 * SCL 卡低时不会在每一个数据位上重复等待完整超时。 */
static fsp_err_t bus_scl_high(void)
{
    fsp_err_t result = bus_set(OLED_SCL_PIN, BSP_IO_LEVEL_HIGH);
    while (FSP_SUCCESS == result)
    {
        bsp_io_level_t level;
        result = bus_read(OLED_SCL_PIN, &level);
        if ((FSP_SUCCESS != result) || (BSP_IO_LEVEL_HIGH == level))
        {
            return result;
        }
        if (s_clock_wait_us < OLED_HALF_CLOCK_US)
        {
            return FSP_ERR_TIMEOUT;
        }
        s_clock_wait_us -= OLED_HALF_CLOCK_US;
        bus_delay();
    }
    return result;
}

static fsp_err_t bus_stop(void)
{
    fsp_err_t result = bus_set(OLED_SCL_PIN, BSP_IO_LEVEL_LOW);
    if (FSP_SUCCESS == result) { result = bus_set(OLED_SDA_PIN, BSP_IO_LEVEL_LOW); }
    bus_delay();
    if (FSP_SUCCESS == result) { result = bus_scl_high(); }
    bus_delay();
    if (FSP_SUCCESS == result) { result = bus_set(OLED_SDA_PIN, BSP_IO_LEVEL_HIGH); }
    bus_delay();
    if (FSP_SUCCESS == result)
    {
        bsp_io_level_t level;
        result = bus_read(OLED_SDA_PIN, &level);
        if ((FSP_SUCCESS == result) && (BSP_IO_LEVEL_LOW == level))
        {
            result = FSP_ERR_IN_USE;
        }
    }
    bus_release();
    return result;
}

static fsp_err_t bus_recover(void)
{
    s_clock_wait_us = OLED_CLOCK_WAIT_US;
    fsp_err_t result = bus_set(OLED_SDA_PIN, BSP_IO_LEVEL_HIGH);
    if (FSP_SUCCESS == result) { result = bus_scl_high(); }
    bus_delay();
    for (uint32_t pulse = 0U; (pulse < 9U) && (FSP_SUCCESS == result); ++pulse)
    {
        bsp_io_level_t level;
        result = bus_read(OLED_SDA_PIN, &level);
        if ((FSP_SUCCESS != result) || (BSP_IO_LEVEL_HIGH == level)) { break; }
        result = bus_set(OLED_SCL_PIN, BSP_IO_LEVEL_LOW);
        bus_delay();
        if (FSP_SUCCESS == result) { result = bus_scl_high(); }
        bus_delay();
    }
    if (FSP_SUCCESS == result) { result = bus_stop(); }
    else { bus_release(); }
    return result;
}

static fsp_err_t bus_start(void)
{
    fsp_err_t result = bus_set(OLED_SDA_PIN, BSP_IO_LEVEL_HIGH);
    if (FSP_SUCCESS == result) { result = bus_scl_high(); }
    bus_delay();
    if (FSP_SUCCESS == result)
    {
        bsp_io_level_t level;
        result = bus_read(OLED_SDA_PIN, &level);
        if ((FSP_SUCCESS == result) && (BSP_IO_LEVEL_LOW == level))
        {
            result = FSP_ERR_IN_USE;
        }
    }
    if (FSP_SUCCESS == result) { result = bus_set(OLED_SDA_PIN, BSP_IO_LEVEL_LOW); }
    bus_delay();
    if (FSP_SUCCESS == result) { result = bus_set(OLED_SCL_PIN, BSP_IO_LEVEL_LOW); }
    return result;
}

static fsp_err_t bus_write_byte(uint8_t value, bool * acknowledged)
{
    for (uint32_t bit = 0U; bit < 8U; ++bit)
    {
        bool const high = (0U != (value & 0x80U));
        fsp_err_t result = bus_set(OLED_SDA_PIN, high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
        bus_delay();
        if (FSP_SUCCESS == result) { result = bus_scl_high(); }
        bus_delay();
        if ((FSP_SUCCESS == result) && high)
        {
            bsp_io_level_t level;
            result = bus_read(OLED_SDA_PIN, &level);
            if ((FSP_SUCCESS == result) && (BSP_IO_LEVEL_LOW == level))
            {
                result = FSP_ERR_IN_USE;
            }
        }
        if (FSP_SUCCESS == result) { result = bus_set(OLED_SCL_PIN, BSP_IO_LEVEL_LOW); }
        if (FSP_SUCCESS != result) { return result; }
        value = (uint8_t) (value << 1U);
    }

    /* 释放 SDA，让从机在第九个时钟周期返回应答。 */
    fsp_err_t result = bus_set(OLED_SDA_PIN, BSP_IO_LEVEL_HIGH);
    bus_delay();
    if (FSP_SUCCESS == result) { result = bus_scl_high(); }
    bus_delay();
    if (FSP_SUCCESS == result)
    {
        bsp_io_level_t level;
        result = bus_read(OLED_SDA_PIN, &level);
        if (FSP_SUCCESS == result) { *acknowledged = (BSP_IO_LEVEL_LOW == level); }
    }
    if (FSP_SUCCESS == result) { result = bus_set(OLED_SCL_PIN, BSP_IO_LEVEL_LOW); }
    return result;
}

static fsp_err_t bus_write(uint8_t const * data, uint32_t size)
{
    s_clock_wait_us = OLED_CLOCK_WAIT_US;
    fsp_err_t result = bus_start();
    for (uint32_t index = 0U; (index <= size) && (FSP_SUCCESS == result); ++index)
    {
        bool acknowledged = false;
        uint8_t const value = (0U == index) ? (uint8_t) (OLED_ADDRESS << 1U) : data[index - 1U];
        result = bus_write_byte(value, &acknowledged);
        if ((FSP_SUCCESS == result) && !acknowledged)
        {
            ++g_oled_status.nack_count;
            g_oled_status.last_nack_byte = index; /* 0 表示地址字节，1 表示控制字节，后续为数据字节。 */
            result = FSP_ERR_ABORTED;
        }
    }
    if ((FSP_SUCCESS == result) || (FSP_ERR_ABORTED == result))
    {
        fsp_err_t const stop_result = bus_stop();
        if (FSP_SUCCESS == result) { result = stop_result; }
    }
    else { bus_release(); }
    return result;
}

static void oled_fail(fsp_err_t error, uint32_t now_ms)
{
    g_oled_status.last_error = error;
    ++g_oled_status.error_count;
    if (FSP_ERR_TIMEOUT == error) { ++g_oled_status.timeout_count; }
    else if (FSP_ERR_IN_USE == error) { ++g_oled_status.bus_error_count; }
    g_oled_status.initialized = false;
    bus_release();
    s_phase = OLED_OPEN;
    s_phase_since_ms = now_ms;
}

static bool oled_send(uint8_t const * data, uint32_t size, uint32_t now_ms)
{
    fsp_err_t const result = bus_write(data, size);
    if (FSP_SUCCESS != result)
    {
        oled_fail(result, now_ms);
        return false;
    }
    return true;
}

void BSP_SSD1306_Init(void)
{
    memset(s_frame, 0, sizeof(s_frame));
    s_phase = OLED_OPEN;
    s_phase_since_ms = 0U;
    s_page = 0U;
    s_column = 0U;
    s_dirty = true;
    g_oled_status.last_error = FSP_SUCCESS;
    g_oled_status.error_count = 0U;
    g_oled_status.frame_count = 0U;
    g_oled_status.initialized = false;
    g_oled_status.nack_count = 0U;
    g_oled_status.timeout_count = 0U;
    g_oled_status.bus_error_count = 0U;
    g_oled_status.last_nack_byte = UINT32_MAX;
}

void BSP_SSD1306_ClearBuffer(void)
{
    memset(s_frame, 0, sizeof(s_frame));
    s_dirty = true;
}

/* 每行高 16 像素，每列按 8 像素单元计数；UTF-8 汉字占两列。
 * 字库不支持的字符显示为 ASCII 问号，超出右边界时停止绘制。 */
void BSP_SSD1306_DrawText(uint8_t row, uint8_t column, char const * text)
{
    if ((NULL == text) || (row >= 4U) || (column >= 16U))
    {
        return;
    }
    uint8_t const * cursor = (uint8_t const *) text;
    while (('\0' != *cursor) && (column < 16U))
    {
        uint16_t unicode = *cursor++;
        if (((unicode & 0xF0U) == 0xE0U) && (cursor[0] != 0U) &&
            (cursor[1] != 0U) && ((cursor[0] & 0xC0U) == 0x80U) &&
            ((cursor[1] & 0xC0U) == 0x80U))
        {
            unicode = (uint16_t) (((unicode & 0x0FU) << 12U) |
                                 ((cursor[0] & 0x3FU) << 6U) | (cursor[1] & 0x3FU));
            cursor += 2;
        }
        uint8_t width = 8U;
        uint8_t const * glyph = &OLED_F8x16['?' - ' '][0];
        if ((unicode >= ' ') && (unicode <= '~'))
        {
            glyph = &OLED_F8x16[unicode - ' '][0];
        }
        else
        {
            for (uint32_t i = 0U; i < sizeof(OLED_CJK16) / sizeof(OLED_CJK16[0]); ++i)
            {
                if (unicode == OLED_CJK16[i].unicode)
                {
                    glyph = OLED_CJK16[i].bitmap;
                    width = 16U;
                    break;
                }
            }
        }
        if ((uint32_t) column + width / 8U > 16U)
        {
            break;
        }
        uint32_t const upper = (uint32_t) row * 2U * OLED_WIDTH + (uint32_t) column * 8U;
        uint32_t const lower = upper + OLED_WIDTH;
        memcpy(&s_frame[upper], glyph, width);
        memcpy(&s_frame[lower], &glyph[width], width);
        column = (uint8_t) (column + width / 8U);
    }
    s_dirty = true;
}

/* 每次调用最多执行一个有界的同步事务，显示数据按 16 字节分块发送。
 * 各块之间返回主循环，使温度与 ADC 状态机有机会继续运行。 */
void BSP_SSD1306_Process(uint32_t now_ms)
{
    switch (s_phase)
    {
        case OLED_OPEN:
        {
            if ((g_oled_status.error_count > 0U) &&
                ((uint32_t) (now_ms - s_phase_since_ms) < OLED_RETRY_DELAY_MS))
            {
                break;
            }
            fsp_err_t const result = bus_recover();
            if (FSP_SUCCESS != result) { oled_fail(result, now_ms); break; }
            s_phase_since_ms = now_ms;
            s_phase = OLED_POWER_WAIT;
            break;
        }
        case OLED_POWER_WAIT:
            if ((uint32_t) (now_ms - s_phase_since_ms) >= OLED_POWER_DELAY_MS)
            {
                s_phase = OLED_INIT_SEND;
            }
            break;
        case OLED_INIT_SEND:
            if (oled_send(s_init_commands, sizeof(s_init_commands), now_ms))
            {
                g_oled_status.initialized = true;
                g_oled_status.last_error = FSP_SUCCESS;
                s_dirty = true;
                s_phase = OLED_READY;
            }
            break;
        case OLED_READY:
            if (s_dirty)
            {
                s_dirty = false;
                s_page = 0U;
                s_phase = OLED_PAGE_COMMAND_SEND;
            }
            break;
        case OLED_PAGE_COMMAND_SEND:
            s_tx[0] = 0x00U;
            s_tx[1] = (uint8_t) (0xB0U | s_page);
            s_tx[2] = 0x00U;
            s_tx[3] = 0x10U;
            if (oled_send(s_tx, 4U, now_ms))
            {
                s_column = 0U;
                s_phase = OLED_PAGE_DATA_SEND;
            }
            break;
        case OLED_PAGE_DATA_SEND:
            s_tx[0] = 0x40U;
            memcpy(&s_tx[1], &s_frame[(uint32_t) s_page * OLED_WIDTH + s_column], OLED_DATA_CHUNK);
            if (oled_send(s_tx, sizeof(s_tx), now_ms))
            {
                s_column = (uint8_t) (s_column + OLED_DATA_CHUNK);
                if (s_column >= OLED_WIDTH)
                {
                    ++s_page;
                    if (s_page >= OLED_PAGES)
                    {
                        ++g_oled_status.frame_count;
                        s_phase = OLED_READY;
                    }
                    else { s_phase = OLED_PAGE_COMMAND_SEND; }
                }
            }
            break;
        default:
            oled_fail(FSP_ERR_INVALID_STATE, now_ms);
            break;
    }
}
