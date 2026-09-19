/**
 * @file    lcd_controller.h
 * @brief   LCD 控制器初始化与兼容接口。
 *
 * @details
 * 为不同 LCD 控制器提供统一初始化入口，并封装当前工程选用的屏幕控制器差异。
 */

#ifndef LCD_CONTROLLER_H
#define LCD_CONTROLLER_H

#include "lcd.h"

void lcd_controller_st7789_init(void);
void lcd_controller_st7796_init(void);
void lcd_controller_ili9341_init(void);
void lcd_controller_nt35310_init(void);
void lcd_controller_nt35510_init(void);
void lcd_controller_ssd1963_init(void);
void lcd_controller_ili9806_init(void);

#endif
