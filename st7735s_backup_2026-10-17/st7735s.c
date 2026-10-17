#include "st7735s.h"

#include <stddef.h>

#define TFT_CLK  BSP_IO_PORT_01_PIN_02
#define TFT_SDA  BSP_IO_PORT_01_PIN_03
#define TFT_RS   BSP_IO_PORT_01_PIN_04
#define TFT_RST  BSP_IO_PORT_00_PIN_13
#define TFT_CS   BSP_IO_PORT_00_PIN_15

#define TFT_CASET  0x2AU
#define TFT_RASET  0x2BU
#define TFT_RAMWR  0x2CU

/* Temporary MAR1801 vendor-example comparison; set to 0 for the prior init. */
#define TFT_VENDOR_INIT_DIAGNOSTIC  0

typedef struct
{
    char ch;
    uint8_t columns[5];
} glyph_t;

/* 5x7 column bitmap, bit 0 is the top row. Only UI characters are retained. */
static glyph_t const s_glyphs[] =
{
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
    {'+', {0x08, 0x08, 0x3E, 0x08, 0x08}},
    {'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
    {'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
    {'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
    {'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
    {'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
    {'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
    {'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
    {'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
    {'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
    {'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
    {'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
    {'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
    {'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
    {'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
    {'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
    {'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
    {'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
    {'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
    {'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
    {'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
    {'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
    {'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
    {'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
    {'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
    {'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
    {'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
    {'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
    {'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
    {'W', {0x3F, 0x40, 0x38, 0x40, 0x3F}}
};

static void pin_write(bsp_io_port_pin_t pin, bsp_io_level_t level)
{
    (void) R_IOPORT_PinWrite(&g_ioport_ctrl, pin, level);
}

static void spi_write(uint8_t value)
{
    for (uint8_t bit = 0U; bit < 8U; ++bit)
    {
        pin_write(TFT_CLK, BSP_IO_LEVEL_LOW);
        pin_write(TFT_SDA, (0U != (value & 0x80U)) ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW);
        pin_write(TFT_CLK, BSP_IO_LEVEL_HIGH);
        value <<= 1U;
    }
    pin_write(TFT_CLK, BSP_IO_LEVEL_LOW);
}

static void command(uint8_t value)
{
    pin_write(TFT_CS, BSP_IO_LEVEL_LOW);
    pin_write(TFT_RS, BSP_IO_LEVEL_LOW);
    spi_write(value);
    pin_write(TFT_CS, BSP_IO_LEVEL_HIGH);
}

static void command_data(uint8_t cmd, uint8_t const * data, uint8_t length)
{
    pin_write(TFT_CS, BSP_IO_LEVEL_LOW);
    pin_write(TFT_RS, BSP_IO_LEVEL_LOW);
    spi_write(cmd);
    pin_write(TFT_RS, BSP_IO_LEVEL_HIGH);
    for (uint8_t i = 0U; i < length; ++i)
    {
        spi_write(data[i]);
    }
    pin_write(TFT_CS, BSP_IO_LEVEL_HIGH);
}

static void window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    uint8_t const column[] = {0U, x0, 0U, x1};
    uint8_t const row[] = {0U, y0, 0U, y1};
    command_data(TFT_CASET, column, sizeof(column));
    command_data(TFT_RASET, row, sizeof(row));
    command(TFT_RAMWR);
    pin_write(TFT_CS, BSP_IO_LEVEL_LOW);
    pin_write(TFT_RS, BSP_IO_LEVEL_HIGH);
}

static void pixel_stream(uint16_t color)
{
    spi_write((uint8_t) (color >> 8U));
    spi_write((uint8_t) color);
}

fsp_err_t BSP_ST7735S_Init(void)
{
    /* RASC owns the pin directions. Check that the configured pins can be driven. */
    bsp_io_port_pin_t const pins[] = {TFT_CLK, TFT_SDA, TFT_RS, TFT_RST, TFT_CS};
    bsp_io_level_t const levels[] = {BSP_IO_LEVEL_LOW, BSP_IO_LEVEL_LOW,
                                     BSP_IO_LEVEL_LOW, BSP_IO_LEVEL_HIGH, BSP_IO_LEVEL_HIGH};
    for (uint8_t i = 0U; i < 5U; ++i)
    {
        fsp_err_t const result = R_IOPORT_PinWrite(&g_ioport_ctrl, pins[i], levels[i]);
        if (FSP_SUCCESS != result)
        {
            return result;
        }
    }

    pin_write(TFT_RST, BSP_IO_LEVEL_LOW);
    R_BSP_SoftwareDelay(10U, BSP_DELAY_UNITS_MILLISECONDS);
    pin_write(TFT_RST, BSP_IO_LEVEL_HIGH);
    R_BSP_SoftwareDelay(120U, BSP_DELAY_UNITS_MILLISECONDS);

    /* MAR1801 ST7735S sequence adapted from the LCDWIKI_SPI vendor library. */
    /* Reset-path comparison: ST7735S section 10.1.2 requires 120 ms
     * after SWRESET before the next command. Keep the remaining baseline intact. */
    command(0x01U);
    R_BSP_SoftwareDelay(120U, BSP_DELAY_UNITS_MILLISECONDS);
    command(0x11U);
    R_BSP_SoftwareDelay(120U, BSP_DELAY_UNITS_MILLISECONDS);
#if TFT_VENDOR_INIT_DIAGNOSTIC
    /* Demo_STM32F103ZET6_Software_SPI/HARDWARE/LCD/lcd.c: LCD_Init(). */
    uint8_t const b1[] = {0x01U, 0x2CU, 0x2DU};
    uint8_t const b3[] = {0x01U, 0x2CU, 0x2DU, 0x01U, 0x2CU, 0x2DU};
    uint8_t const c0[] = {0xA2U, 0x02U, 0x84U};
    uint8_t const c2[] = {0x0AU, 0x00U};
    uint8_t const c3[] = {0x8AU, 0x2AU};
    uint8_t const c4[] = {0x8AU, 0xEEU};
    uint8_t const e0[] = {0x0FU, 0x1AU, 0x0FU, 0x18U, 0x2FU, 0x28U, 0x20U, 0x22U,
                          0x1FU, 0x1BU, 0x23U, 0x37U, 0x00U, 0x07U, 0x02U, 0x10U};
    uint8_t const e1[] = {0x0FU, 0x1BU, 0x0FU, 0x17U, 0x33U, 0x2CU, 0x29U, 0x2EU,
                          0x30U, 0x30U, 0x39U, 0x3FU, 0x00U, 0x07U, 0x03U, 0x10U};
    uint8_t const b4 = 0x07U;
    uint8_t const c1 = 0xC5U;
    uint8_t const c5 = 0x0EU;
    uint8_t const madctl = 0xC0U;
    uint8_t const test_command = 0x01U;
    uint8_t const ram_power_save = 0x00U;
    uint8_t const color_mode = 0x05U;
    uint8_t const column[] = {0x00U, 0x00U, 0x00U, 0x7FU};
    uint8_t const row[] = {0x00U, 0x00U, 0x00U, 0x9FU};
    command_data(0xB1U, b1, sizeof(b1));
    command_data(0xB2U, b1, sizeof(b1));
    command_data(0xB3U, b3, sizeof(b3));
    command_data(0xB4U, &b4, 1U);
    command_data(0xC0U, c0, sizeof(c0));
    command_data(0xC1U, &c1, 1U);
    command_data(0xC2U, c2, sizeof(c2));
    command_data(0xC3U, c3, sizeof(c3));
    command_data(0xC4U, c4, sizeof(c4));
    command_data(0xC5U, &c5, 1U);
    command_data(0x36U, &madctl, 1U);
    command_data(0xE0U, e0, sizeof(e0));
    command_data(0xE1U, e1, sizeof(e1));
    command_data(0x2AU, column, sizeof(column));
    command_data(0x2BU, row, sizeof(row));
    command_data(0xF0U, &test_command, 1U);
    command_data(0xF6U, &ram_power_save, 1U);
    command_data(0x3AU, &color_mode, 1U);
#else
    uint8_t const b1[] = {0x05, 0x3C, 0x3C};
    uint8_t const b3[] = {0x05, 0x3C, 0x3C, 0x05, 0x3C, 0x3C};
    uint8_t const c0[] = {0x28, 0x08, 0x04};
    uint8_t const c2[] = {0x0D, 0x00};
    uint8_t const c3[] = {0x8D, 0x2A};
    uint8_t const c4[] = {0x8D, 0xEE};
    uint8_t const e0[] = {0x03, 0x22, 0x07, 0x0A, 0x2E, 0x30, 0x25, 0x2A,
                          0x28, 0x26, 0x2E, 0x3A, 0x00, 0x01, 0x03, 0x13};
    uint8_t const e1[] = {0x04, 0x16, 0x06, 0x0D, 0x2D, 0x26, 0x23, 0x27,
                          0x27, 0x25, 0x2D, 0x3B, 0x00, 0x01, 0x04, 0x13};
    uint8_t const b4 = 0x03U;
    uint8_t const c1 = 0xC0U;
    uint8_t const c5 = 0x1AU;
    uint8_t const gate = 0x05U;
    uint8_t const madctl = 0x08U;
    uint8_t const color_mode = 0x05U;
    command_data(0xB1U, b1, sizeof(b1));
    command_data(0xB2U, b1, sizeof(b1));
    command_data(0xB3U, b3, sizeof(b3));
    command_data(0xB4U, &b4, 1U);
    command_data(0xC0U, c0, sizeof(c0));
    command_data(0xC1U, &c1, 1U);
    command_data(0xC2U, c2, sizeof(c2));
    command_data(0xC3U, c3, sizeof(c3));
    command_data(0xC4U, c4, sizeof(c4));
    command_data(0xC5U, &c5, 1U);
    command_data(0x17U, &gate, 1U);
    command_data(0x36U, &madctl, 1U);
    command_data(0xE0U, e0, sizeof(e0));
    command_data(0xE1U, e1, sizeof(e1));
    command_data(0x3AU, &color_mode, 1U);
#endif
    command(0x29U);
#if TFT_VENDOR_INIT_DIAGNOSTIC
    command_data(0x36U, &madctl, 1U);
#endif
    return FSP_SUCCESS;
}

void BSP_ST7735S_FillScreen(uint16_t color)
{
    window(0U, 0U, BSP_ST7735S_WIDTH - 1U, BSP_ST7735S_HEIGHT - 1U);
    for (uint32_t i = 0U; i < (BSP_ST7735S_WIDTH * BSP_ST7735S_HEIGHT); ++i)
    {
        pixel_stream(color);
    }
    pin_write(TFT_CS, BSP_IO_LEVEL_HIGH);
}

void BSP_ST7735S_DrawChar(uint8_t x, uint8_t y, char ch, uint8_t scale,
                          uint16_t foreground, uint16_t background)
{
    if ((0U == scale) || ((uint16_t) x + (6U * scale) > BSP_ST7735S_WIDTH) ||
        ((uint16_t) y + (8U * scale) > BSP_ST7735S_HEIGHT))
    {
        return;
    }

    uint8_t const * bitmap = s_glyphs[0].columns;
    for (uint32_t i = 1U; i < (sizeof(s_glyphs) / sizeof(s_glyphs[0])); ++i)
    {
        if (s_glyphs[i].ch == ch)
        {
            bitmap = s_glyphs[i].columns;
            break;
        }
    }

    window(x, y, (uint8_t) (x + 6U * scale - 1U), (uint8_t) (y + 8U * scale - 1U));
    for (uint8_t row = 0U; row < 8U; ++row)
    {
        for (uint8_t ry = 0U; ry < scale; ++ry)
        {
            for (uint8_t column = 0U; column < 6U; ++column)
            {
                uint16_t const color = ((column < 5U) &&
                                        (0U != (bitmap[column] & (1U << row)))) ?
                                       foreground : background;
                for (uint8_t rx = 0U; rx < scale; ++rx)
                {
                    pixel_stream(color);
                }
            }
        }
    }
    pin_write(TFT_CS, BSP_IO_LEVEL_HIGH);
}
