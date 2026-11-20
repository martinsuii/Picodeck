// PicoPad example app — WiFi scan / connect
// A: scan   Y: connect using /wifi.cnf   X: exit

#include "include.h"

static void ShowStatus(void)
{
    DrawClear();
    DrawText2("WiFi", 10, 10, COL_YELLOW);
    DrawRect(10, 36, WIDTH - 20, 1, COL_GRAY);

    DrawText("Module:", 12, 52, COL_LTGRAY);
    DrawText(AppWifiPresent() ? "installed" : "missing",
             110, 52, AppWifiPresent() ? COL_GREEN : COL_RED);

    DrawText("Radio:", 12, 72, COL_LTGRAY);
    DrawText(AppWifiReady() ? "ready" : "not ready",
             110, 72, AppWifiReady() ? COL_GREEN : COL_YELLOW);

    DrawText("Link:", 12, 92, COL_LTGRAY);
    DrawText(AppWifiLinkUp() ? "connected" : "disconnected",
             110, 92, AppWifiLinkUp() ? COL_GREEN : COL_YELLOW);

    DrawText("SSID:", 12, 112, COL_LTGRAY);
    DrawText(AppWifiSsid(), 110, 112, COL_WHITE);

    DrawText("IP:", 12, 132, COL_LTGRAY);
    DrawText(AppWifiIp(), 110, 132, COL_WHITE);

    DrawText("RSSI:", 12, 152, COL_LTGRAY);
    {
        char buf[16];
        int r = AppWifiRssi();
        DecNum(buf, (u32)(r < 0 ? -r : r), 0);
        strcat(buf, " dBm");
        DrawText(buf, 110, 152, COL_WHITE);
    }

    DrawText("A: scan  Y: connect /wifi.cnf  X: exit",
             12, 210, COL_GRAY);
    DispUpdate();
}

// Read /wifi.cnf: line 1 = SSID, line 2 = password.
static Bool ReadCreds(char* ssid, int smax, char* pass, int pmax)
{
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, "/wifi.cnf") && !FileOpen(&f, "/WIFI.CNF")) return False;

    char buf[160];
    u32 n = FileRead(&f, buf, sizeof(buf) - 1);
    FileClose(&f);
    buf[n] = 0;

    int i = 0, o = 0;
    while (buf[i] && buf[i] != '\r' && buf[i] != '\n' && o < smax - 1)
        ssid[o++] = buf[i++];
    ssid[o] = 0;
    while (buf[i] == '\r' || buf[i] == '\n') i++;

    o = 0;
    while (buf[i] && buf[i] != '\r' && buf[i] != '\n' && o < pmax - 1)
        pass[o++] = buf[i++];
    pass[o] = 0;
    return ssid[0] != 0;
}

static void ShowScan(void)
{
    AppWifiNet nets[16];
    int n = AppWifiScan(nets, 16, 8000);

    DrawClear();
    DrawText2("WiFi scan", 10, 10, COL_YELLOW);
    DrawRect(10, 36, WIDTH - 20, 1, COL_GRAY);

    if (n < 0)
    {
        DrawText("Scan failed (is WiFi ready?)", 12, 56, COL_RED);
    }
    else
    {
        for (int i = 0; i < n && i < 12; i++)
        {
            DrawText(nets[i].ssid, 12, 48 + i * 13, COL_WHITE);
            char r[12];
            int v = nets[i].rssi < 0 ? -nets[i].rssi : nets[i].rssi;
            DecNum(r, (u32)v, 0);
            strcat(r, " dBm");
            DrawText(r, 220, 48 + i * 13, COL_GRAY);
        }
    }

    DrawText("Press A to continue", 12, 210, COL_GRAY);
    DispUpdate();
    while (1)
    {
        if (KeyGet() == KEY_A) return;
        WaitMs(30);
    }
}

void AppMain(void)
{
    ShowStatus();

    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_X) return;

        if (k == KEY_A)
        {
            ShowScan();
            ShowStatus();
        }

        if (k == KEY_Y)
        {
            char ssid[33];
            char pass[65];
            if (!ReadCreds(ssid, sizeof(ssid), pass, sizeof(pass)))
            {
                DrawText("No /wifi.cnf on the SD card", 12, 190, COL_RED);
                DispUpdate();
            }
            else
            {
                DrawClear();
                DrawText2("Connecting", 10, 10, COL_YELLOW);
                DrawText(ssid, 12, 60, COL_WHITE);
                DrawText("please wait...", 12, 80, COL_LTGRAY);
                DispUpdate();
                AppWifiConnect(ssid, pass);
                WaitMs(4000);
                ShowStatus();
            }
        }

        WaitMs(30);
    }
}
