#ifndef BSP_ST7735S_H
#define BSP_ST7735S_H

#include <stdint.h>

#include "hal_data.h"

#define BSP_ST7735S_WIDTH   128U
#define BSP_ST7735S_HEIGHT  160U

/* MAR1801, software SPI: CLK=P102, SDA=P103, RS=P104, RST=P013, CS=P015.
 * Pins are configured as GPIO outputs by the user's RASC configuration. */
fsp_err_t BSP_ST7735S_Init(void);
void BSP_ST7735S_FillScreen(uint16_t color);
void BSP_ST7735S_DrawChar(uint8_t x, uint8_t y, char ch, uint8_t scale,
                         uint16_t foreground, uint16_t background);

#endif
