// Minimal ST7789 driver for the WiFi Link app.
//
// Deliberately has no framebuffer: the app only shows a few lines of status,
// so it renders text row by row straight into the panel. That keeps ~150 KB
// of RAM free for lwIP.

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"

#include "pins.h"
#include "lcd.h"
#include "font5x8.h"

static spi_inst_t *const LCD_SPI = spi0;

static inline void lcd_cmd(uint8_t c)
{
    gpio_put(PIN_LCD_DC, 0);
    gpio_put(PIN_LCD_CS, 0);
    spi_write_blocking(LCD_SPI, &c, 1);
    gpio_put(PIN_LCD_CS, 1);
}

static inline void lcd_data(const uint8_t *buf, size_t n)
{
    gpio_put(PIN_LCD_DC, 1);
    gpio_put(PIN_LCD_CS, 0);
    spi_write_blocking(LCD_SPI, buf, n);
    gpio_put(PIN_LCD_CS, 1);
}

static void lcd_cmd_data(uint8_t cmd, const uint8_t *buf, size_t n)
{
    lcd_cmd(cmd);
    lcd_data(buf, n);
}

static void lcd_window(int x0, int y0, int x1, int y1)
{
    uint8_t b[4];
    b[0] = (uint8_t)(x0 >> 8); b[1] = (uint8_t)x0;
    b[2] = (uint8_t)(x1 >> 8); b[3] = (uint8_t)x1;
    lcd_cmd_data(0x2A, b, 4);
    b[0] = (uint8_t)(y0 >> 8); b[1] = (uint8_t)y0;
    b[2] = (uint8_t)(y1 >> 8); b[3] = (uint8_t)y1;
    lcd_cmd_data(0x2B, b, 4);
    lcd_cmd(0x2C);
}

void lcd_init(void)
{
    gpio_init(PIN_LCD_DC);
    gpio_set_dir(PIN_LCD_DC, GPIO_OUT);
    gpio_init(PIN_LCD_CS);
    gpio_set_dir(PIN_LCD_CS, GPIO_OUT);
    gpio_put(PIN_LCD_CS, 1);
    gpio_init(PIN_LCD_RES);
    gpio_set_dir(PIN_LCD_RES, GPIO_OUT);
    gpio_init(PIN_LCD_BLK);
    gpio_set_dir(PIN_LCD_BLK, GPIO_OUT);
    gpio_put(PIN_LCD_BLK, 0);

    spi_init(LCD_SPI, 24 * 1000 * 1000);
    spi_set_format(LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);

    gpio_put(PIN_LCD_RES, 0);
    sleep_ms(20);
    gpio_put(PIN_LCD_RES, 1);
    sleep_ms(150);

    // Same sequence the launcher patches into MakeCode games.
    lcd_cmd(0x01);            // SWRESET
    sleep_ms(150);
    lcd_cmd(0x11);            // SLPOUT
    sleep_ms(255);
    {
        const uint8_t colmod = 0x55;   // 16 bpp RGB565
        lcd_cmd_data(0x3A, &colmod, 1);
        sleep_ms(10);
    }
    {
        const uint8_t madctl = 0x40;   // landscape, RGB order
        lcd_cmd_data(0x36, &madctl, 1);
    }
    lcd_cmd(0x21);            // INVON (IPS panel)
    lcd_cmd(0x13);            // NORON
    sleep_ms(10);
    lcd_cmd(0x29);            // DISPON
    sleep_ms(100);

    gpio_put(PIN_LCD_BLK, 1);
    lcd_fill(COL_BG);
}

static void lcd_pixels(int x, int y, int w, const uint16_t *px)
{
    lcd_window(x, y, x + w - 1, y);
    gpio_put(PIN_LCD_DC, 1);
    gpio_put(PIN_LCD_CS, 0);
    spi_write_blocking(LCD_SPI, (const uint8_t *)px, (size_t)w * 2);
    gpio_put(PIN_LCD_CS, 1);
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;

    static uint16_t chunk[LCD_W];
    for (int i = 0; i < w; i++) chunk[i] = color;

    lcd_window(x, y, x + w - 1, y + h - 1);
    gpio_put(PIN_LCD_DC, 1);
    gpio_put(PIN_LCD_CS, 0);
    for (int row = 0; row < h; row++)
        spi_write_blocking(LCD_SPI, (const uint8_t *)chunk, (size_t)w * 2);
    gpio_put(PIN_LCD_CS, 1);
}

void lcd_fill(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_W, LCD_H, color);
}

void lcd_text_n(int x, int y, int maxchars, const char *s, uint16_t fg, uint16_t bg)
{
    static uint16_t line[LCD_W];
    if (!s || maxchars <= 0) return;
    if (maxchars > LCD_W / 6) maxchars = LCD_W / 6;

    int n = 0;
    while (s[n] && n < maxchars) n++;
    if (n == 0) return;

    int px = n * 6;
    if (x < 0) x = 0;
    if (x + px > LCD_W) px = LCD_W - x;
    int cols = px / 6;

    for (int row = 0; row < 8; row++) {
        int yy = y + row;
        if (yy < 0 || yy >= LCD_H) continue;
        int o = 0;
        for (int i = 0; i < cols; i++) {
            unsigned char c = (unsigned char)s[i];
            if (c < 32 || c > 126) c = '?';
            uint8_t bits = font5x8[c - 32][row];
            for (int col = 0; col < 5; col++)
                line[o++] = (bits & (0x80 >> col)) ? fg : bg;
            line[o++] = bg;
        }
        lcd_pixels(x, yy, o, line);
    }
}

void lcd_text(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    lcd_text_n(x, y, 64, s, fg, bg);
}
