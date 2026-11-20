#ifndef WIFI_LINK_LCD_H
#define WIFI_LINK_LCD_H

#include <stdint.h>

#define COL_BLACK  0x0000
#define COL_WHITE  0xFFFF
#define COL_GREY   0x8410
#define COL_DIM    0x4208
#define COL_GREEN  0x07E0
#define COL_RED    0xF800
#define COL_YELLOW 0xFFE0
#define COL_CYAN   0x07FF
#define COL_BG     0x0841

void lcd_init(void);
void lcd_fill(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_text(int x, int y, const char *s, uint16_t fg, uint16_t bg);
void lcd_text_n(int x, int y, int maxchars, const char *s, uint16_t fg, uint16_t bg);

#endif
