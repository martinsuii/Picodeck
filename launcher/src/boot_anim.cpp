// ****************************************************************************
//
//                  PicoDeck — boot animation
//
//   A fake boot splash driven by the theme's boot element list.
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "boot_anim.h"
#include "theme.h"
#include "rubber_ducky.h"

static int BootBattPct(void)
{
    int mv = LauncherBatMv();
    int pct = (mv - BATTERY_EMPTY_INT) * 100
              / (BATTERY_FULL_INT - BATTERY_EMPTY_INT);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

void BootAnimation(void)
{
    u32 dur = g_theme.boot_ms ? g_theme.boot_ms : 2000u;
    u32 start = Millis();
    int frame = 0;

    // Live values don't change during the short splash.
    Bool usb = RubberDuckyUsbConnected();
    Bool sd = DiskValid();
    int pct = BootBattPct();

    char batt[16];
    DecNum(batt, (u32)pct, 0);
    strcat(batt, "%");

    ThemeDrawCtx c;
    c.duck_x = 0;
    c.t = 0;
    c.frame = 0;
    c.saver = False;
    c.usb = usb ? "connected" : "not connected";
    c.usb_col = usb ? g_theme.ok_color : g_theme.menu_dim;
    c.sd = sd ? "card present" : "no card";
    c.sd_col = sd ? g_theme.ok_color : g_theme.menu_dim;
    c.batt = batt;
    c.batt_col = pct >= 50 ? g_theme.ok_color
               : (pct >= 20 ? g_theme.warn_color : g_theme.err_color);

    while (1)
    {
        u32 t = (u32)(Millis() - start);
        if (t >= dur) break;

        c.t = t;
        c.frame = frame;

        DrawClearCol(g_theme.bg_color);
        ThemeDrawList(g_theme.boot, g_theme.boot_n, &c);
        if (g_theme.boot_show_version)
            DrawText("v" LAUNCHER_VERSION_STR, 4, HEIGHT - 12,
                     g_theme.menu_dim);
        DispUpdate();

        frame++;
        WaitMs(20);
    }
}
