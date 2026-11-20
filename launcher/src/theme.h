// ****************************************************************************
//
//                  PicoDeck — theme support (layout engine)
//
//   Looks for /themes on the SD card and loads the theme named in
//   /themes/active.cfg (or the built-in defaults when none is selected).
//   A theme can restyle the boot splash, the screensaver and the menus, and
//   can define colour variables so one palette drives everything.
//
//   File syntax: one "key=value" per line; lines starting with # or ; are
//   comments, and a trailing " ; comment" is ignored too (except on mascot
//   art lines). Colours accept RRGGBB, #RRGGBB, RGB shorthand (e.g. F0C) and
//   names: black white gray ltgray dkgray red green blue yellow cyan magenta
//   orange purple pink brown. A colour may also be a variable reference
//   "$name" declared with "set name=RRGGBB" earlier in the file.
//
//   Global keys:
//     bg=RRGGBB                 boot/saver background
//     duck_color=RRGGBB         mascot colour
//     duck1..duck4=<ascii art>  mascot art
//     accent / ok / warn / err  status palette (icons, battery, live values)
//     menu_bg / menu_title / menu_text / menu_sel / menu_hl / menu_dim
//     home_title1 / home_title2 / home_time / home_time_color /
//     home_time_x / home_time_y / home_weather / home_weather_color
//     menu_title_scale=1|2      title font scale
//     menu_title_y / menu_title2_y / menu_divider_y /
//     menu_start_y / menu_line_h / menu_footer_y
//     menu_sel_style=0..3       0 bar+arrow, 1 arrow, 2 invert, 3 none
//     menu_show_footer=0|1      show the bottom help line
//     menu_show_status=0|1      show SD/USB/WiFi/battery icons
//     menu_help=<text>          submenu footer hint
//     boot_ms=<n>               boot splash duration (default 2000)
//     boot_show_version=0|1     show "vX.Y.Z" on the splash
//     saver_timeout=<sec>       idle time before the screensaver (default 30)
//     saver_speed=<px>          mascot scroll step (default 4)
//     saver_backlight=<0..255>  backlight while saving (255 = unchanged)
//     boot_append=0|1 / saver_append=0|1
//                               replace (0, default) or append to (1) the
//                               built-in element list; may appear anywhere
//                               in the file
//     set <name>=RRGGBB         define a colour variable
//
//   Element lines (drawn in file order):
//     boot=<type>,...           boot splash element
//     saver=<type>,...          screensaver element
//
//   Element types:
//     text,x,y,<string>,colour[,scale[,align]]
//                               align 0=left 1=centre 2=right; x<0 centres
//     blink,x,y,<string>,colour,period_ms[,scale[,align]]
//     marquee,x,y,<string>,colour[,step_ms[,width]]
//                               scrolls right-to-left (width = clip width)
//     time,x,y,colour[,scale[,align]]
//     duck,x,y                  animated mascot (screensaver)
//     bar,x,y,w,h,colour        progress bar (boot: fills with time)
//     pct,x,y,colour            boot percentage
//     spinner,x,y,colour        boot spinner
//     rect,x,y,w,h,colour       filled rectangle
//     frame,x,y,w,h,colour[,thickness]
//     line,x1,y1,x2,y2,colour[,thickness]
//     circle,x,y,r,colour       outlined circle
//     disc,x,y,r,colour         filled circle
//     status,x,y                USB + SD + battery block
//     usb,x,y | sd,x,y | battery,x,y
//     weather,x,y[,colour]
//
//   Example:
//     set neon=00FFFF
//     menu_title=$neon
//     boot=text,-1,40,PICODECK,$neon,2,1
//     boot=frame,10,10,300,220,$neon,2
//
// ****************************************************************************

#ifndef _THEME_H
#define _THEME_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

#define THEME_DUCK_LINES  4
#define THEME_DUCK_LEN    48
#define THEME_TEXT_LEN    64
#define THEME_TITLE_LEN   32
#define THEME_MAX_ELEMS   24

typedef enum {
    TE_TEXT = 0,
    TE_DUCK,
    TE_BAR,
    TE_PCT,
    TE_SPINNER,
    TE_STATUS,
    TE_USB,
    TE_SD,
    TE_BATTERY,
    TE_WEATHER,
    TE_TIME,
    TE_RECT,
    TE_FRAME,
    TE_LINE,
    TE_CIRCLE,
    TE_DISC,
    TE_MARQUEE,
    TE_BLINK,
} ThemeElemType;

typedef struct {
    u8   type;
    int  x, y, w, h;
    char text[THEME_TEXT_LEN];
    u16  color;
    u8   scale;
} ThemeElem;

typedef struct {
    char duck[THEME_DUCK_LINES][THEME_DUCK_LEN];
    int  duck_w;
    u16  duck_color;
    u16  bg_color;

    ThemeElem boot[THEME_MAX_ELEMS];
    int boot_n;
    ThemeElem saver[THEME_MAX_ELEMS];
    int saver_n;

    // Menu styling (home + category/subcategory screens).
    char home_title1[THEME_TITLE_LEN];
    char home_title2[THEME_TITLE_LEN];
    u16  menu_bg;
    u16  menu_title;
    u16  menu_text;
    u16  menu_sel;
    u16  menu_hl;
    u16  menu_dim;

    // Status palette (SD/USB/WiFi icons, battery, live values).
    u16  ok_color;
    u16  warn_color;
    u16  err_color;
    u16  accent_color;

    // Home-screen time/weather (from the ESP32 bridge).
    Bool home_time_show;
    u16  home_time_color;
    int  home_time_x;        // < 0 = right-aligned
    int  home_time_y;        // < 0 = next to the first title line
    Bool home_weather_show;
    u16  home_weather_color;

    // Menu layout. Defaults match the built-in screens.
    u8   menu_title_scale;   // 1 or 2
    int  menu_title_y;
    int  menu_title2_y;
    int  menu_sub_title_y;   // submenu title (home uses menu_title_y)
    int  menu_divider_y;
    int  menu_start_y;
    int  menu_line_h;
    int  menu_footer_y;
    u8   menu_sel_style;     // 0 bar+arrow, 1 arrow, 2 invert, 3 none
    Bool menu_show_footer;
    Bool menu_show_status;
    char menu_help[THEME_TEXT_LEN];

    // Animation / screensaver timing.
    u16  boot_ms;
    Bool boot_show_version;
    u16  saver_timeout;      // seconds
    u8   saver_speed;        // mascot step in px
    u8   saver_backlight;    // 0..255, 255 = no change
} Theme;

// Drawing context shared by the boot splash and the screensaver. Fills in
// the animated x offset / clock / live status strings.
typedef struct {
    int  duck_x;      // mascot x offset
    u32  t;           // elapsed time in ms (boot) or frame time
    int  frame;       // spinner frame counter
    Bool saver;       // True = screensaver mode (bar full, cached status)
    const char* usb;  u16 usb_col;
    const char* sd;   u16 sd_col;
    const char* batt; u16 batt_col;
} ThemeDrawCtx;

#define THEME_MAX_FILES 16
#define THEME_NAME_LEN  16

extern Theme g_theme;

// Load the theme selected in /themes/active.cfg, or defaults if none.
void ThemeLoad(void);

// List available theme files (returns count, fills short names).
int ThemeList(char names[][THEME_NAME_LEN], int max);

// Select a theme by filename ("" = default) and persist the choice.
void ThemeSetActive(const char* name);

// Currently selected theme filename ("" = default).
const char* ThemeActiveName(void);

// Draw one element / a whole element list with the given context.
void ThemeDrawElem(const ThemeElem* e, const ThemeDrawCtx* c);
void ThemeDrawList(const ThemeElem* list, int n, const ThemeDrawCtx* c);

#ifdef __cplusplus
}
#endif

#endif // _THEME_H
