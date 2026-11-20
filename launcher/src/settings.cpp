// ****************************************************************************
//
//                  PicoDeck — persistent settings
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "settings.h"

static Settings g_settings;

void SettingsLoad(void)
{
    const Settings* s = (const Settings*)SETTINGS_FLASH_ADDR;
    if (s->magic == SETTINGS_MAGIC && s->backlight >= 8 && s->backlight <= 255)
        g_settings = *s;
    else
    {
        g_settings.magic = SETTINGS_MAGIC;
        g_settings.backlight = 255;
    }
}

u8 SettingsBacklight(void) { return g_settings.backlight; }

void SettingsSetBacklight(u8 v)
{
    if (v < 8)  v = 8;
    if (v > 255) v = 255;
    g_settings.backlight = v;
    SettingsSave();
    DispBacklight(v);
}

void SettingsSave(void)
{
    // Erase the 4 KB sector, then program just the first 256-byte page
    // (the rest of the sector stays erased = 0xFF).
    static u8 buf[256];
    memset(buf, 0xFF, sizeof(buf));
    memcpy(buf, &g_settings, sizeof(g_settings));
    NativeWifiPause();
    FlashErase(SETTINGS_FLASH_ADDR - 0x10000000u, SETTINGS_SECTOR_SIZE);
    FlashProgram(SETTINGS_FLASH_ADDR - 0x10000000u, buf, sizeof(buf));
    NativeWifiResume();
}

// ---------------------------------------------------------------------------
// Settings → Backlight screen
// ---------------------------------------------------------------------------

void BacklightRun(void)
{
    u8 v = SettingsBacklight();
    Bool need_redraw = True;

    while (1)
    {
        if (need_redraw)
        {
            DrawClear();
            DrawText("backlight", 8, 8, COL_WHITE);
            DrawRect(8, 22, WIDTH - 16, 1, COL_GRAY);

            DrawFrame(20, 90, WIDTH - 40, 20, COL_GRAY);
            int fill = (WIDTH - 44) * v / 255;
            if (fill > 0) DrawRect(22, 92, fill, 16, COL_WHITE);

            char b[16];
            DecNum(b, v, 0); strcat(b, " / 255");
            DrawText(b, 20, 122, COL_WHITE);

            DrawText("up/dn: adjust  a: save  x: back", 8, HEIGHT - 14, COL_GRAY);
            DispUpdate();
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        if (k == KEY_UP)
        {
            if (v < 247) v += 8; else v = 255;
            DispBacklight(v);
            need_redraw = True;
        }
        else if (k == KEY_DOWN)
        {
            if (v > 8) v -= 8; else v = 8;
            DispBacklight(v);
            need_redraw = True;
        }
        else if (k == KEY_A)
        {
            SettingsSetBacklight(v);
            need_redraw = True;
        }
        else if (k == KEY_X)
        {
            DispBacklight(SettingsBacklight());   // revert to saved
            return;
        }
    }
}
