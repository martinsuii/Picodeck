// PicoPad example app — hello
// Draws a screen, shows WiFi module state, X exits.

#include "include.h"

void AppMain(void)
{
    int frames = 0;

    while (1)
    {
        DrawClear();
        DrawText2("Hello from SD", 10, 14, COL_YELLOW);
        DrawRect(10, 40, WIDTH - 20, 1, COL_GRAY);

        DrawText("App id:", 12, 60, COL_LTGRAY);
        DrawText(AppId(), 90, 60, COL_WHITE);

        DrawText("WiFi module:", 12, 80, COL_LTGRAY);
        DrawText(AppWifiPresent() ? "installed" : "not installed",
                 110, 80, AppWifiPresent() ? COL_GREEN : COL_RED);

        DrawText("WiFi ready:", 12, 100, COL_LTGRAY);
        DrawText(AppWifiReady() ? "yes" : "no",
                 110, 100, AppWifiReady() ? COL_GREEN : COL_YELLOW);

        DrawText("Battery:", 12, 120, COL_LTGRAY);
        {
            char buf[16];
            DecNum(buf, (u32)AppBatteryMv(), 0);
            strcat(buf, " mV");
            DrawText(buf, 110, 120, COL_WHITE);
        }

        DrawText("A: show scan   X: back to menu", 12, 200, COL_GRAY);

        // Simple animation so we know the app is alive.
        DrawRect(12 + (frames % 120), 150, 12, 12, COL_CYAN);
        frames++;

        DispUpdate();

        u8 k = KeyGet();
        if (k == KEY_X) return;

        if (k == KEY_A)
        {
            AppWifiNet nets[16];
            int n = AppWifiScan(nets, 16, 5000);
            DrawClear();
            DrawText2("WiFi scan", 10, 10, COL_YELLOW);
            char buf[32];
            if (n < 0)
            {
                DecNum(buf, (u32)(-n), 0);
                DrawText("Scan failed: ", 12, 50, COL_RED);
                DrawText(buf, 110, 50, COL_RED);
            }
            else
            {
                for (int i = 0; i < n && i < 12; i++)
                {
                    DrawText(nets[i].ssid, 12, 40 + i * 14, COL_WHITE);
                    char r[12];
                    DecNum(r, (u32)(nets[i].rssi < 0 ? -nets[i].rssi : nets[i].rssi), 0);
                    strcat(r, " dBm");
                    DrawText(r, 220, 40 + i * 14, COL_GRAY);
                }
            }
            DrawText("Press A to continue", 12, 210, COL_GRAY);
            DispUpdate();
            while (1)
            {
                if (KeyGet() == KEY_A) break;
                WaitMs(30);
            }
        }

        WaitMs(30);
    }
}
