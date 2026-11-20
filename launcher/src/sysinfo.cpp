// ****************************************************************************
//
//                  PicoDeck — System info / diagnostics
//
//   Live readout of battery, chip temperature, CPU clock, RAM, flash usage,
//   SD card and USB state. Refreshes once per second. X returns to the menu.
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "sysinfo.h"
#include "arcade_patches.h"
#include "rubber_ducky.h"

extern "C" Bool IsGameValid(void);          // defined in menu.cpp
extern "C" u8 __bss_end__[];                // linker symbols
extern "C" u8 __StackLimit[];
extern "C" u8 __flash_binary_start[];
extern "C" u8 __flash_binary_end[];

#define SI_LABEL_X   16
#define SI_VALUE_X   150
#define SI_LINE_H    15
#define SI_TOP_Y     46
#define SI_FOOTER_Y  220

// Append a decimal number to `buf` (manual, no stdio dependency).
static void AppendU32(char* buf, u32 v)
{
    char tmp[12];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    else { while (v > 0) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; } }
    int len = (int)StrLen(buf);
    while (n > 0) buf[len++] = tmp[--n];
    buf[len] = 0;
}

static void AppendInt(char* buf, int v)
{
    if (v < 0) { strcat(buf, "-"); v = -v; }
    AppendU32(buf, (u32)v);
}

static void DrawLine(const char* label, const char* value, int y, u16 vcol)
{
    DrawText(label, SI_LABEL_X, y, COL_LTGRAY);
    DrawText(value, SI_VALUE_X, y, vcol);
}

static void SysInfoRender(void)
{
    DrawClear();

    DrawText2("System Info", (WIDTH - 11 * FONTW * 2) / 2, 12, COL_YELLOW);
    DrawRect(20, 36, WIDTH - 40, 1, COL_GRAY);

    int y = SI_TOP_Y;
    char buf[48];

    // --- Battery ---
    int mv = LauncherBatMv();
    int pct = (mv - BATTERY_EMPTY_INT) * 100
              / (BATTERY_FULL_INT - BATTERY_EMPTY_INT);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    u16 batt_col = pct >= 50 ? COL_GREEN : (pct >= 20 ? COL_YELLOW : COL_RED);

    strcpy(buf, "");
    AppendU32(buf, (u32)pct);
    strcat(buf, "%");
    DrawLine("Battery", buf, y, batt_col);
    y += SI_LINE_H;

    // Battery voltage, shown as "3.95 V  (3950 mV)"
    {
        char vbuf[32];
        int whole = mv / 1000;
        int frac  = (mv % 1000) / 10;   // 0..99
        int n = 0;

        char tmp[8];
        int m = 0;
        if (whole == 0) tmp[m++] = '0';
        else { while (whole > 0) { tmp[m++] = (char)('0' + whole % 10); whole /= 10; } }
        while (m > 0) vbuf[n++] = tmp[--m];

        vbuf[n++] = '.';
        vbuf[n++] = (char)('0' + (frac / 10) % 10);
        vbuf[n++] = (char)('0' + frac % 10);
        vbuf[n++] = ' ';
        vbuf[n++] = 'V';
        vbuf[n] = 0;

        strcat(vbuf, "  (");
        AppendU32(vbuf, (u32)mv);
        strcat(vbuf, " mV)");

        DrawLine("Battery voltage", vbuf, y, batt_col);
        y += SI_LINE_H;
    }

    // --- Temperature ---
    float t = ADC_Temp();
    int t10 = (int)(t * 10.0f + (t >= 0.0f ? 0.5f : -0.5f));
    strcpy(buf, "");
    AppendInt(buf, t10 / 10);
    strcat(buf, ".");
    int frac = t10 % 10;
    if (frac < 0) frac = -frac;
    char fd[4]; fd[0] = (char)('0' + frac); fd[1] = 0;
    strcat(buf, fd);
    strcat(buf, " C");
    DrawLine("Temperature", buf, y, COL_WHITE);
    y += SI_LINE_H;

    // --- CPU clock ---
    u32 mhz = ClockGetHz(clk_sys) / 1000000u;
    strcpy(buf, "");
    AppendU32(buf, mhz);
    strcat(buf, " MHz");
    DrawLine("CPU clock", buf, y, COL_WHITE);
    y += SI_LINE_H;

    // --- RAM ---
    u32 free_ram = (u32)(__StackLimit - __bss_end__);
    strcpy(buf, "");
    AppendU32(buf, free_ram / 1024u);
    strcat(buf, " KB free");
    DrawLine("RAM", buf, y, COL_WHITE);
    y += SI_LINE_H;

    // --- Launcher flash footprint ---
    u32 launcher_kb = (u32)(__flash_binary_end - __flash_binary_start) / 1024u;
    strcpy(buf, "");
    AppendU32(buf, launcher_kb);
    strcat(buf, " KB");
    DrawLine("Launcher flash", buf, y, COL_WHITE);
    y += SI_LINE_H;

    // --- Game ---
    if (IsGameValid())
    {
        char name[32];
        if (PicoDeckReadName(name, sizeof(name)))
        {
            strcpy(buf, name);
            DrawLine("Game", buf, y, COL_GREEN);
        }
        else DrawLine("Game", "flashed (unknown)", y, COL_GREEN);
    }
    else DrawLine("Game", "none", y, COL_GRAY);
    y += SI_LINE_H;

    // --- SD card ---
    if (DiskValid())
    {
        u32 mb = (SDMediaSize() / 2048u);   // sectors * 512 / 1M
        u32 freemb = DiskFreeKB() / 1024u;
        strcpy(buf, GetSDName());
        strcat(buf, "  ");
        AppendU32(buf, mb);
        strcat(buf, " MB  ");
        AppendU32(buf, freemb);
        strcat(buf, " MB free");
        DrawLine("SD card", buf, y, COL_GREEN);
    }
    else DrawLine("SD card", "not present", y, COL_GRAY);
    y += SI_LINE_H;

    // --- USB ---
    DrawLine("USB", RubberDuckyUsbConnected() ? "connected" : "not connected",
             y, RubberDuckyUsbConnected() ? COL_GREEN : COL_GRAY);
    y += SI_LINE_H;

    DrawRect(20, SI_FOOTER_Y - 8, WIDTH - 40, 1, COL_GRAY);
    DrawText("Refreshing every second    X: Back", 20, SI_FOOTER_Y, COL_LTGRAY);
    DispUpdate();
}

void SysInfoRun(void)
{
    // Make sure the battery ADC is initialized (DeviceInit calls BatInit, but
    // be defensive if a tool reinitialised the ADC).
    BatInit();

    // Mount the SD card so free/total space is valid.
    DiskAutoMount();

    u32 last = 0;
    Bool first = True;

    while (1)
    {
        u32 now = Millis();
        if (first || (u32)(now - last) >= 1000)
        {
            last = now;
            first = False;
            SysInfoRender();
        }

        if (KeyGet() == KEY_X) return;
        WaitMs(20);
    }
}
