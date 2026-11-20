// ****************************************************************************
//
//                  PicoDeck — animated screensaver
//
//   Shown after the theme's idle timeout (or manually with Y on the menu).
//   Layout comes from the theme's saver element list. The status block is
//   re-read every 30 s; the duck animates every frame. Any button wakes it.
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "screensaver.h"
#include "rubber_ducky.h"
#include "settings.h"
#include "theme.h"

static char g_usb_val[20];
static char g_sd_val[20];
static char g_batt_val[32];
static u16  g_usb_col, g_sd_col, g_batt_col;

static int BattPct(void)
{
    int mv = LauncherBatMv();
    int pct = (mv - BATTERY_EMPTY_INT) * 100
              / (BATTERY_FULL_INT - BATTERY_EMPTY_INT);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static void SsUpdateStatus(void)
{
    Bool usb = RubberDuckyUsbConnected();
    strcpy(g_usb_val, usb ? "connected" : "not connected");
    g_usb_col = usb ? g_theme.ok_color : g_theme.menu_dim;

    Bool sd = DiskValid();
    strcpy(g_sd_val, sd ? "card present" : "no card");
    g_sd_col = sd ? g_theme.ok_color : g_theme.menu_dim;

    int pct = BattPct();
    char num[8];
    strcpy(g_batt_val, "");
    DecNum(num, pct, 0); strcat(g_batt_val, num);
    strcat(g_batt_val, "%  ");
    DecNum(num, LauncherBatMv(), 0); strcat(g_batt_val, num);
    strcat(g_batt_val, " mV");
    g_batt_col = pct >= 50 ? g_theme.ok_color
               : (pct >= 20 ? g_theme.warn_color : g_theme.err_color);
}

void ScreensaverRun(void)
{
    // Drain any buffered key so a held key doesn't instantly wake it.
    while (KeyGet() != NOKEY) {}

    // Dim the backlight if the theme asks for it.
    u8 prev_backlight = SettingsBacklight();
    if (g_theme.saver_backlight < prev_backlight)
        DispBacklight(g_theme.saver_backlight);

    SsUpdateStatus();
    u32 last_status = Millis();
    int duck_off = -g_theme.duck_w;
    int speed = g_theme.saver_speed ? g_theme.saver_speed : 4;

    ThemeDrawCtx c;
    c.duck_x = duck_off;
    c.t = 0;
    c.frame = 0;
    c.saver = True;
    c.usb = g_usb_val;  c.usb_col = g_usb_col;
    c.sd = g_sd_val;    c.sd_col = g_sd_col;
    c.batt = g_batt_val; c.batt_col = g_batt_col;

    while (1)
    {
        u32 now = Millis();
        if ((u32)(now - last_status) >= 30000)
        {
            last_status = now;
            SsUpdateStatus();
        }

        c.t = now;
        c.duck_x = duck_off;
        c.frame = (int)(now / 100);

        DrawClearCol(g_theme.bg_color);
        ThemeDrawList(g_theme.saver, g_theme.saver_n, &c);
        DispUpdate();

        duck_off += speed;
        if (duck_off > WIDTH) duck_off = -g_theme.duck_w;

        if (KeyGet() != NOKEY)
        {
            DispBacklight(prev_backlight);
            return;
        }
        WaitMs(80);
    }
}
