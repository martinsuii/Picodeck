// ****************************************************************************
//
//          PicoDeck — "ESP Station" (ESP-NOW devices)
//
//   Lists ESP-NOW stations (announced by the ESP32 bridge), shows their
//   function and online status, then lets you act on them. A Rubber Ducky
//   station receives a DuckyScript payload from /payloads.
//
// ****************************************************************************

#include "../include.h"
#include "esp_station.h"
#include "esp32_bridge.h"
#include "file_picker.h"
#include "theme.h"
#include "rubber_ducky.h"

#define EST_MAX_SCRIPT  16384
#define EST_TYPE_RUBBER_DUCKY  1

static void EstFrame(const char* title, const char* footer)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 30,
              g_theme.menu_title);
    DrawRect(40, 64, WIDTH - 80, 1, g_theme.menu_dim);
    DrawRect(20, 202, WIDTH - 40, 1, g_theme.menu_dim);
    if (footer) DrawText(footer, 20, 210, g_theme.menu_dim);
}

static void EstMessage(const char* l1, const char* l2)
{
    EstFrame("ESP Station", "Press X to return");
    if (l1) DrawText(l1, 20, 100, g_theme.menu_text);
    if (l2) DrawText(l2, 20, 120, g_theme.menu_dim);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

static const char* EstTypeName(int type)
{
    if (type == EST_TYPE_RUBBER_DUCKY) return "Rubber Ducky";
    return "Unknown";
}

static Bool EstFolderExists(const char* path)
{
    sFile find;
    FileInit(&find);
    Bool ok = FindOpen(&find, path);
    if (ok) FindClose(&find);
    return ok;
}

static u32 EstLoadFile(const char* path)
{
    char* buf = RubberDuckyScriptBuffer();
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return 0;

    u32 size = FileSize(&f);
    if (size > EST_MAX_SCRIPT) size = EST_MAX_SCRIPT;
    u32 rd = FileRead(&f, buf, size);
    FileClose(&f);
    buf[rd] = 0;
    return rd;
}

// Pick a .txt payload from /payloads. Returns True and fills path.
static Bool EstPickPayload(char* path, int pathlen)
{
    if (!DiskAutoMount())
    {
        EstMessage("SD card not found", "Insert SD card and try again");
        return False;
    }
    if (!EstFolderExists("/payloads"))
    {
        EstMessage("No /payloads folder", "Create it and add .txt payloads");
        return False;
    }
    return FilePickerRun("/payloads", "*.txt", path, pathlen) == PICKER_RESULT_SELECTED;
}

// Send the currently-loaded script to the Rubber Ducky at `mac`.
static void EstSendDucky(const char* mac)
{
    char path[256];
    if (!EstPickPayload(path, sizeof(path))) return;

    u32 len = EstLoadFile(path);
    if (len == 0)
    {
        EstMessage("Could not read payload", "File is empty or unreadable");
        return;
    }

    EstFrame("ESP Station", "A: Send   X: Cancel");
    DrawText("Payload:", 20, 90, g_theme.menu_dim);
    const char* fname = path;
    for (const char* q = path; *q; q++) if (*q == '/') fname = q + 1;
    DrawText(fname, 20, 106, g_theme.menu_text);
    DrawText("Send to Rubber Ducky?", 20, 140, g_theme.menu_text);
    DispUpdate();

    for (;;)
    {
        u8 k = KeyGet();
        if (k == KEY_X) return;
        if (k == KEY_A) break;
        WaitMs(20);
    }

    EstFrame("ESP Station", NULL);
    DrawText("Sending payload via ESP-NOW...", 20, 100, g_theme.menu_text);
    DispUpdate();

    if (!EspNowSendTo(mac, RubberDuckyScriptBuffer(), len))
        EstMessage("Send failed", "Check the station is powered and in range");
    else
        EstMessage("Payload sent!", "Done.");
}

void EspStationRun(void)
{
    if (!EspEnsureConnected()) return;

    for (;;)
    {
        EspDevice devs[ESP_DEV_MAX];
        int count = EspListDevices(devs, ESP_DEV_MAX);

        if (count == 0)
        {
            EstFrame("ESP Station", "A: Rescan   X: Back");
            DrawText("No devices found.", 20, 100, g_theme.menu_text);
            DrawText("Power on a station nearby.", 20, 120, g_theme.menu_dim);
            DispUpdate();
            for (;;)
            {
                u8 k = KeyGet();
                if (k == KEY_X) return;
                if (k == KEY_A) break;   // rescan
                WaitMs(20);
            }
            continue;
        }

        int sel = 0;
        Bool redraw = True;
        for (;;)
        {
            if (redraw)
            {
                EstFrame("ESP Station", "A: Open   X: Back");
                for (int i = 0; i < count; i++)
                {
                    int y = 80 + i * 20;
                    Bool cur = (i == sel);
                    u16 color = cur ? g_theme.menu_sel : g_theme.menu_text;
                    if (cur)
                        DrawRect(54, y - 2, WIDTH - 108, FONTH + 4, g_theme.menu_hl);
                    DrawText(cur ? ">" : " ", 30, y, color);
                    DrawText(devs[i].name, 60, y, color);
                    DrawText(devs[i].online ? "[online]  " : "[offline] ",
                             200, y, devs[i].online ? g_theme.menu_sel : g_theme.menu_dim);
                }
                DispUpdate();
                redraw = False;
            }

            u8 k = KeyGet();
            if (k == NOKEY) { WaitMs(20); continue; }
            if (k == KEY_UP)   { if (sel > 0) { sel--; redraw = True; } }
            else if (k == KEY_DOWN) { if (sel < count - 1) { sel++; redraw = True; } }
            else if (k == KEY_A) break;
            else if (k == KEY_X) return;
        }

        // Act on the selected device.
        if (devs[sel].type == EST_TYPE_RUBBER_DUCKY)
        {
            EstSendDucky(devs[sel].mac);
            return;
        }

        EstMessage(EstTypeName(devs[sel].type), "No actions for this device yet.");
    }
}
