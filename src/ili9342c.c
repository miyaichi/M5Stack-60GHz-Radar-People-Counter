#include "ili9342c.h"
#include <stdarg.h>
#include <stdio.h>
#include "font8x8.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "ili9342c";
static spi_device_handle_t spi;

static void lcd_cmd(uint8_t cmd, const uint8_t *data, int len)
{
    gpio_set_level(LCD_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    spi_device_polling_transmit(spi, &t);

    if (data && len > 0) {
        gpio_set_level(LCD_DC, 1);
        spi_transaction_t td = { .length = len * 8, .tx_buffer = data };
        spi_device_polling_transmit(spi, &td);
    }
}

static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t caset[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t raset[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    lcd_cmd(0x2A, caset, 4); // Column Address Set
    lcd_cmd(0x2B, raset, 4); // Row Address Set
    lcd_cmd(0x2C, NULL, 0);  // Memory Write
    gpio_set_level(LCD_DC, 1);
}

void lcd_init(void)
{
    // Backlight
    gpio_set_direction(LCD_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_BL, 0);

    // RST
    gpio_set_direction(LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    // DC
    gpio_set_direction(LCD_DC, GPIO_MODE_OUTPUT);

    spi_bus_config_t buscfg = {
        .mosi_io_num = LCD_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = LCD_CLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_WIDTH * LCD_HEIGHT * 2,
    };
    spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 20 * 1000 * 1000,
        .mode = 0,
        .spics_io_num = LCD_CS,
        .queue_size = 7,
    };
    spi_bus_add_device(SPI2_HOST, &devcfg, &spi);

    // ILI9342C init sequence
    lcd_cmd(0x01, NULL, 0);  // Software Reset
    vTaskDelay(pdMS_TO_TICKS(5));
    lcd_cmd(0x11, NULL, 0);  // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x3A, (uint8_t[]){0x55}, 1); // COLMOD: 16-bit color
    lcd_cmd(0x36, (uint8_t[]){0x08}, 1); // MADCTL: BGR (X mirrored in software)
    lcd_cmd(0xB4, (uint8_t[]){0x00}, 1); // Display Inversion Control

    uint8_t frmctr1[] = {0x00, 0x18};
    lcd_cmd(0xB1, frmctr1, 2);           // Frame Rate Control

    lcd_cmd(0x21, NULL, 0);              // Display Inversion ON (ILI9342C needs this)
    lcd_cmd(0x29, NULL, 0);              // Display ON
    vTaskDelay(pdMS_TO_TICKS(20));

    // Backlight on
    gpio_set_level(LCD_BL, 1);

    ESP_LOGI(TAG, "LCD initialized");
}

void lcd_fill(uint16_t color)
{
    set_window(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1);

    uint16_t line[LCD_WIDTH];
    for (int i = 0; i < LCD_WIDTH; i++) {
        line[i] = (color >> 8) | (color << 8); // byte swap for SPI
    }

    spi_transaction_t t = {
        .length = LCD_WIDTH * 2 * 8,
        .tx_buffer = line,
    };
    for (int y = 0; y < LCD_HEIGHT; y++) {
        spi_device_polling_transmit(spi, &t);
    }
}

void lcd_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (c < 32 || c > 127) c = 32;
    const uint8_t *glyph = font8x8[c - 32];

    uint16_t pixels[8 * 8];
    uint16_t fg_be = (fg >> 8) | (fg << 8);
    uint16_t bg_be = (bg >> 8) | (bg << 8);

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            // Font data is LSB-first (bit0 = leftmost pixel)
            pixels[row * 8 + col] = (glyph[row] & (1 << col)) ? fg_be : bg_be;
        }
    }

    set_window(x, y, x + 7, y + 7);
    spi_transaction_t t = { .length = 8 * 8 * 2 * 8, .tx_buffer = pixels };
    spi_device_polling_transmit(spi, &t);
}

void lcd_draw_string(int x, int y, const char *str, uint16_t fg, uint16_t bg)
{
    while (*str) {
        lcd_draw_char(x, y, *str++, fg, bg);
        x += 8;
    }
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0 || y < 0 || w <= 0 || h <= 0) return;
    set_window(x, y, x + w - 1, y + h - 1);

    uint16_t color_be = (color >> 8) | (color << 8);
    // send one row at a time
    static uint16_t row[LCD_WIDTH];
    int cols = (w <= LCD_WIDTH) ? w : LCD_WIDTH;
    for (int i = 0; i < cols; i++) row[i] = color_be;

    spi_transaction_t t = { .length = cols * 2 * 8, .tx_buffer = row };
    for (int i = 0; i < h; i++) {
        spi_device_polling_transmit(spi, &t);
    }
}

void lcd_draw_char_scaled(int x, int y, char c, uint16_t fg, uint16_t bg, int scale)
{
    if (c < 32 || c > 127) c = 32;
    if (scale < 1) scale = 1;
    if (scale > 4) scale = 4;

    const uint8_t *glyph = font8x8[c - 32];
    int w = 8 * scale;
    int h = 8 * scale;

    static uint16_t buf[8 * 4 * 8 * 4];  // max scale=4: 32x32
    uint16_t fg_be = (fg >> 8) | (fg << 8);
    uint16_t bg_be = (bg >> 8) | (bg << 8);

    for (int row = 0; row < 8; row++) {
        for (int sr = 0; sr < scale; sr++) {
            for (int col = 0; col < 8; col++) {
                // Font data is LSB-first (bit0 = leftmost pixel)
                uint16_t px = (glyph[row] & (1 << col)) ? fg_be : bg_be;
                for (int sc = 0; sc < scale; sc++) {
                    buf[(row * scale + sr) * w + col * scale + sc] = px;
                }
            }
        }
    }

    set_window(x, y, x + w - 1, y + h - 1);
    spi_transaction_t t = { .length = w * h * 2 * 8, .tx_buffer = buf };
    spi_device_polling_transmit(spi, &t);
}

void lcd_draw_string_scaled(int x, int y, const char *str, uint16_t fg, uint16_t bg, int scale)
{
    while (*str) {
        lcd_draw_char_scaled(x, y, *str++, fg, bg, scale);
        x += 8 * scale;
    }
}

void lcd_printf_scaled(int x, int y, uint16_t fg, uint16_t bg, int scale, const char *fmt, ...)
{
    char buf[64];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lcd_draw_string_scaled(x, y, buf, fg, bg, scale);
}
