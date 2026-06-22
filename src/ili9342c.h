#pragma once
#include <stdint.h>

// M5Stack Core display pins
#define LCD_MOSI  23
#define LCD_CLK   18
#define LCD_CS    14
#define LCD_DC    27
#define LCD_RST   33
#define LCD_BL    32

#define LCD_WIDTH  320
#define LCD_HEIGHT 240

void lcd_init(void);
void lcd_fill(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg);
void lcd_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg);
void lcd_draw_char_scaled(int x, int y, char c, uint16_t fg, uint16_t bg, int scale);
void lcd_draw_string_scaled(int x, int y, const char *str, uint16_t fg, uint16_t bg, int scale);
void lcd_printf_scaled(int x, int y, uint16_t fg, uint16_t bg, int scale, const char *fmt, ...);

// RGB565 helper
#define RGB565(r, g, b) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define WHITE  0xFFFF
#define BLACK  0x0000
#define RED    RGB565(255, 0, 0)
#define GREEN  RGB565(0, 255, 0)
#define BLUE   RGB565(0, 0, 255)
