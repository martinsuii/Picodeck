// PicoPad 10 / Pico W pin map used by the WiFi Link app.
#ifndef WIFI_LINK_PINS_H
#define WIFI_LINK_PINS_H

// ST7789 display (SPI0)
#define PIN_LCD_BLK   16
#define PIN_LCD_DC    17
#define PIN_LCD_SCK   18
#define PIN_LCD_MOSI  19
#define PIN_LCD_RES   20
#define PIN_LCD_CS    21

// Buttons (active low)
#define PIN_BTN_UP     4
#define PIN_BTN_LEFT   3
#define PIN_BTN_RIGHT  2
#define PIN_BTN_DOWN   5
#define PIN_BTN_X      9
#define PIN_BTN_Y      8
#define PIN_BTN_A      7
#define PIN_BTN_B      6

// LEDs and battery
#define PIN_LED       22   // active low
#define PIN_BAT       29   // ADC3; shared with the CYW43 SPI clock

#define LCD_W 320
#define LCD_H 240

#define APP_VERSION "1.2.5"

#endif
