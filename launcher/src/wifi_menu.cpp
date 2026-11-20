// ****************************************************************************
//
//              PicoDeck — Settings > WIFI (native)
//
//   WiFi setup for the Pico W's own radio. Uses the launcher-integrated
//   module when it is installed; otherwise falls back to the ESP32 bridge
//   menu.
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "keyboard.h"
#include "theme.h"
#include "crashlog.h"
#include "flasher.h"
#include "uf2_parser.h"
#include "../../wifi_module/wifi_shared.h"

#define WIFI_NET_MAX 16

static const char* BsodReason(u32 stage);
static void WifiInstallModule(Bool ask);

static void WifiFrame(const char* title, const char* footer)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 14,
              g_theme.menu_title);
    DrawRect(20, 42, WIDTH - 40, 1, g_theme.menu_dim);
    DrawRect(20, 204, WIDTH - 40, 1, g_theme.menu_dim);
    if (footer) DrawText(footer, 20, 212, g_theme.menu_dim);
}

static void WifiMessage(const char* l1, const char* l2)
{
    WifiFrame("WIFI", "Press X to return");
    if (l1) DrawText(l1, 20, 96, g_theme.menu_text);
    if (l2) DrawText(l2, 20, 118, g_theme.menu_dim);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

static char g_last_wifi_reason[48] = "Connection failed.";

static void WifiStateCopy(char* out, int max)
{
    int i = 0;
    for (; i < max - 1 && WIFI_SHARED->state[i]; i++)
        out[i] = (char)WIFI_SHARED->state[i];
    out[i] = 0;
}

// Human-readable version of the module's link state.
static const char* WifiStateFriendly(const char* s)
{
    if (!s || !s[0]) return "Starting...";
    if (!strcmp(s, "joining")) return "Connecting to network...";
    if (!strcmp(s, "no ip")) return "Connected. Getting an IP address...";
    if (!strcmp(s, "bad auth")) return "Wrong password or security type.";
    if (!strcmp(s, "no network")) return "Can't find that network.";
    if (!strcmp(s, "link failed")) return "Connection failed.";
    if (!strcmp(s, "connected")) return "Connected.";
    if (!strcmp(s, "init failed")) return "The radio failed to start.";
    return s;
}

static u16 WifiStateColor(const char* s)
{
    if (!s) return g_theme.menu_dim;
    if (!strcmp(s, "connected")) return g_theme.ok_color;
    if (!strcmp(s, "bad auth") || !strcmp(s, "no network") ||
        !strcmp(s, "link failed") || !strcmp(s, "init failed"))
        return g_theme.err_color;
    if (!strcmp(s, "no ip") || !strcmp(s, "joining"))
        return g_theme.warn_color;
    return g_theme.menu_text;
}

static Bool WifiStateIsFinalFailure(const char* s)
{
    return !strcmp(s, "bad auth") || !strcmp(s, "no network") ||
           !strcmp(s, "link failed") || !strcmp(s, "init failed");
}

static void WifiStatusScreen(void)
{
    WifiFrame("WIFI STATUS", "X: back");

    if (!NativeWifiPresent())
    {
        DrawText("No WiFi module installed.", 20, 96, g_theme.warn_color);
        DrawText("Flash wifi_module.uf2 to the game area.", 20, 118,
                 g_theme.menu_dim);
        DispUpdate();
        while (KeyGet() != KEY_X) WaitMs(50);
        return;
    }

    if (!NativeWifiReady())
    {
        u32 stage = (u32)WIFI_SHARED->stage;
        DrawText("Module is not running.", 20, 86, g_theme.warn_color);
        if (stage == 0)
            DrawText("It was not started (safe mode?).", 20, 108, g_theme.menu_text);
        else if (stage & 0x80000000u)
        {
            char buf[64];
            char num[8];
            strcpy(buf, "Core 1 fault at stage 0x");
            DecHexNum(num, stage & 0xFFFFu, 2);
            strcat(buf, num);
            DrawText(buf, 20, 108, g_theme.menu_text);
            DrawText(BsodReason(stage), 20, 130, g_theme.menu_dim);
        }
        else
        {
            char buf[48];
            char num[8];
            strcpy(buf, "Stopped at stage ");
            DecNum(num, stage, 0);
            strcat(buf, num);
            DrawText(buf, 20, 108, g_theme.menu_text);
        }
        DrawText("Hold B at power-on to skip it, then", 20, 140,
                 g_theme.menu_dim);
        DrawText("reflash wifi_module.uf2.", 20, 158, g_theme.menu_dim);
        DispUpdate();
        while (KeyGet() != KEY_X) WaitMs(50);
        return;
    }

    DrawText("State:", 20, 66, g_theme.menu_dim);
    {
        char st[24];
        WifiStateCopy(st, sizeof(st));
        if (!st[0]) strcpy(st, NativeWifiLinkUp() ? "connected" : "disconnected");
        DrawText(WifiStateFriendly(st), 20, 82, WifiStateColor(st));
    }

    DrawText("SSID:", 20, 104, g_theme.menu_dim);
    {
        char ssid[33];
        int i = 0;
        for (; i < 32; i++)
        {
            char c = (char)WIFI_SHARED->ssid[i];
            ssid[i] = c;
            if (!c) break;
        }
        ssid[32] = 0;
        DrawText(ssid, 120, 104, g_theme.menu_text);
    }

    DrawText("IP:", 20, 128, g_theme.menu_dim);
    DrawText(NativeWifiIp(), 120, 128, g_theme.menu_text);

    DrawText("RSSI:", 20, 152, g_theme.menu_dim);
    {
        char buf[16];
        char num[8];
        strcpy(buf, "");
        DecNum(num, (u32)(NativeWifiRssi() < 0 ? -NativeWifiRssi() : NativeWifiRssi()), 0);
        strcat(buf, "-");
        strcat(buf, num);
        strcat(buf, " dBm");
        DrawText(buf, 120, 152, g_theme.menu_text);
    }

    DrawText("TCP: 2323", 20, 176, g_theme.menu_dim);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

// Try to connect and wait for the link. Returns True when connected.
static Bool WifiConnectAndWait(const char* ssid, const char* pass)
{
    char line[WIFI_LINE_MAX];
    strcpy(line, "WIFI CONNECT ");
    int at = (int)StrLen(line);
    for (int i = 0; ssid[i] && at < WIFI_LINE_MAX - 2; i++) line[at++] = ssid[i];
    line[at++] = '|';
    for (int i = 0; pass[i] && at < WIFI_LINE_MAX - 1; i++) line[at++] = pass[i];
    line[at] = 0;
    NativeWifiSend(line);

    // Persist for the ESP32 app and the next native boot.
    WifiSaveCreds(ssid, pass);

    WifiFrame("WIFI", "X: cancel");
    DrawText("Connecting to:", 20, 70, g_theme.menu_dim);
    DrawText(ssid, 20, 88, g_theme.menu_text);
    DrawText("Waiting for the radio...", 20, 130, g_theme.menu_dim);
    DispUpdate();

    char last[24] = "";
    for (int i = 0; i < 600; i++)   // up to 30 s
    {
        if (NativeWifiLinkUp())
        {
            strcpy(g_last_wifi_reason, "Connected.");
            return True;
        }
        if (KeyGet() == KEY_X)
        {
            strcpy(g_last_wifi_reason, "Cancelled.");
            return False;
        }

        char raw[24];
        WifiStateCopy(raw, sizeof(raw));

        if (strcmp(raw, last) != 0)
        {
            strncpy(last, raw, sizeof(last) - 1);
            last[sizeof(last) - 1] = 0;

            // Repaint only the status line, not the whole frame.
            DrawRect(20, 128, WIDTH - 40, FONTH + 4, g_theme.menu_bg);
            DrawText(WifiStateFriendly(raw), 20, 130, WifiStateColor(raw));
            DispUpdate();
        }

        if (WifiStateIsFinalFailure(raw))
        {
            strcpy(g_last_wifi_reason, WifiStateFriendly(raw));
            return False;
        }
        WaitMs(50);
    }

    strcpy(g_last_wifi_reason, "Timed out. Check signal and password.");
    return NativeWifiLinkUp();
}

static void WifiConnectManual(void)
{
    char ssid[33];
    char pass[65];
    ssid[0] = 0;
    pass[0] = 0;

    if (!KeyboardInput("SSID", ssid, sizeof(ssid)) || !ssid[0]) return;
    if (!KeyboardInput("Password", pass, sizeof(pass))) return;

    if (WifiConnectAndWait(ssid, pass))
    {
        WifiFrame("WIFI", "X: back");
        DrawText("Connected.", 20, 90, g_theme.ok_color);
        DrawText("IP:", 20, 116, g_theme.menu_dim);
        DrawText(NativeWifiIp(), 60, 116, g_theme.menu_text);
        DispUpdate();
        while (KeyGet() != KEY_X) WaitMs(50);
    }
    else
    {
        WifiMessage("Connection failed.", g_last_wifi_reason);
    }
}

static void WifiConnectSaved(void)
{
    char ssid[33];
    char pass[65];
    ssid[0] = 0;
    pass[0] = 0;

    if (!WifiLoadCreds(ssid, sizeof(ssid), pass, sizeof(pass)) || !ssid[0])
    {
        WifiMessage("No saved network.", "Use /wifi.cnf or connect manually.");
        return;
    }

    if (WifiConnectAndWait(ssid, pass))
        WifiMessage("Connected.", NativeWifiIp());
    else
        WifiMessage("Connection failed.", g_last_wifi_reason);
}

static void WifiDisconnect(void)
{
    NativeWifiSend("WIFI CLEAR");
    WifiMessage("Disconnected.", "Saved credentials kept on the SD card.");
}

// Network picker. Returns the selected index or -1.
static int WifiPickNetwork(NativeWifiNet* nets, int count)
{
    int sel = 0;
    Bool redraw = True;

    while (1)
    {
        if (redraw)
        {
            WifiFrame("SELECT NETWORK", "A: connect   X: back");
            int page = sel / 7;
            int first = page * 7;
            for (int i = 0; i < 7; i++)
            {
                int idx = first + i;
                if (idx >= count) break;
                int y = 56 + i * 21;
                Bool cur = (idx == sel);
                if (cur)
                    DrawRect(24, y - 2, WIDTH - 48, FONTH + 4, g_theme.menu_hl);
                DrawText(cur ? ">" : " ", 30, y,
                         cur ? g_theme.menu_sel : g_theme.menu_text);
                DrawText(nets[idx].ssid, 50, y,
                         cur ? g_theme.menu_sel : g_theme.menu_text);
                char b[16];
                char num[8];
                strcpy(b, "");
                if (nets[idx].rssi < 0) strcat(b, "-");
                DecNum(num, (u32)(nets[idx].rssi < 0 ? -nets[idx].rssi : nets[idx].rssi), 0);
                strcat(b, num);
                DrawText(b, WIDTH - 60, y,
                         cur ? g_theme.menu_sel : g_theme.menu_dim);
            }
            DispUpdate();
            redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }
        if (k == KEY_UP)   { sel = (sel + count - 1) % count; redraw = True; }
        else if (k == KEY_DOWN) { sel = (sel + 1) % count; redraw = True; }
        else if (k == KEY_A) return sel;
        else if (k == KEY_X) return -1;
    }
}

static void WifiScanMenu(void)
{
    static NativeWifiNet nets[WIFI_NET_MAX];

    WifiFrame("WIFI SCAN", "Please wait...");
    DrawText("Scanning...", 20, 100, g_theme.menu_text);
    DispUpdate();

    int n = NativeWifiScan(nets, WIFI_NET_MAX, 20000);
    if (n == -2)
    {
        WifiMessage("Scan timed out.", "Try again.");
        return;
    }
    if (n < 0)
    {
        WifiMessage("Module is not ready.", "Check SETTINGS > WIFI status.");
        return;
    }
    if (n == 0)
    {
        WifiMessage("No networks found.", "Move closer to the router.");
        return;
    }

    int sel = WifiPickNetwork(nets, n);
    if (sel < 0) return;

    char pass[65];
    pass[0] = 0;
    if (!KeyboardInput("Password", pass, sizeof(pass))) return;

    if (WifiConnectAndWait(nets[sel].ssid, pass))
        WifiMessage("Connected.", NativeWifiIp());
    else
        WifiMessage("Connection failed.", g_last_wifi_reason);
}

// ---------------------------------------------------------------------------


// ---------------------------------------------------------------------------
// BSOD (blue screen) for a failed WiFi module start
// ---------------------------------------------------------------------------

static const char* BsodReason(u32 stage)
{
    u32 code = stage & 0xFFFFu;
    switch (code)
    {
    case 0:  return "module was not started";
    case 1:
    case 2:  return "module startup";
    case 3:  return "PIO/DMA reset";
    case 4:  return "radio hardware init";
    case 5:  return "TCP server start";
    case 6:  return "STA/netif bring-up";
    case 7:  return "after ready";
    case 90: return "CYW43 firmware init";
    case 0x0F: return "old module image (reflash)";
    default: return "unknown";
    }
}

void WifiBsodShow(void)
{
    u32 stage = NativeWifiFailStage();
    Bool fault = (stage & 0x80000000u) != 0;
    u32 code = stage & 0xFFFFu;

    // Deep blue background with a lighter header band.
    DrawClearCol(RGBTO16(0, 16, 110));
    DrawRect(0, 0, WIDTH, 26, RGBTO16(0, 8, 64));
    DrawRect(0, 26, WIDTH, 2, RGBTO16(90, 130, 220));

    DrawText("PICOPAD WIFI", 10, 8, COL_WHITE);
    DrawText("STOP", WIDTH - 44, 8, COL_WHITE);

    DrawText2(":(", 18, 40, COL_WHITE);

    DrawText("The radio module stopped.", 18, 88, COL_WHITE);
    DrawText("Launcher is fine. WiFi is off.",
             18, 106, RGBTO16(180, 205, 255));

    char buf[64];
    strcpy(buf, "STOP CODE: ");
    strcat(buf, fault ? "CORE1_HARD_FAULT" : "WIFI_START_FAILED");
    DrawText(buf, 18, 134, COL_WHITE);

    strcpy(buf, "STAGE: 0x");
    DecHexNum(buf + StrLen(buf), code, 2);
    strcat(buf, "  ");
    strcat(buf, BsodReason(stage));
    DrawText(buf, 18, 152, COL_WHITE);

    {
        char vbuf[48];
        u32 mv = (u32)WIFI_SHARED->version;   // 0x00MMmmpp
        if (mv)
        {
            strcpy(vbuf, "MODULE: wifi_module ");
            DecNum(vbuf + StrLen(vbuf), (mv >> 16) & 0xFF, 0);
            strcat(vbuf, ".");
            DecNum(vbuf + StrLen(vbuf), (mv >> 8) & 0xFF, 0);
            strcat(vbuf, ".");
            DecNum(vbuf + StrLen(vbuf), mv & 0xFF, 0);
        }
        else
        {
            strcpy(vbuf, "MODULE: old image - reflash");
        }
        DrawText(vbuf, 18, 176, RGBTO16(150, 180, 235));
    }
    {
        char cbuf[64];
        strcpy(cbuf, "CFSR: 0x");
        DecHexNum(cbuf + StrLen(cbuf), (u32)WIFI_SHARED->fault_cfsr, 8);
        DrawText(cbuf, 18, 194, RGBTO16(90, 120, 190));
    }

    // Blinking-ish footer prompt (static; the screen is just a stop).
    DrawText("Y: save log   A: reinstall   X: continue", 18, 216, COL_WHITE);
    DispUpdate();

    // Build the log text while the screen is visible.
    char body[256];
    strcpy(body, "Stage: 0x");
    DecHexNum(body + StrLen(body), code, 2);
    strcat(body, "\nReason: ");
    strcat(body, BsodReason(stage));
    strcat(body, "\nModule: ");
    u32 mv = (u32)WIFI_SHARED->version;
    DecNum(body + StrLen(body), (mv >> 16) & 0xFF, 0);
    strcat(body, ".");
    DecNum(body + StrLen(body), (mv >> 8) & 0xFF, 0);
    strcat(body, ".");
    DecNum(body + StrLen(body), mv & 0xFF, 0);
    strcat(body, "\nCFSR: 0x");
    DecHexNum(body + StrLen(body), (u32)WIFI_SHARED->fault_cfsr, 8);
    strcat(body, "\nHFSR: 0x");
    DecHexNum(body + StrLen(body), (u32)WIFI_SHARED->fault_hfsr, 8);
    strcat(body, "\nLink: ");
    strcat(body, (const char*)WIFI_SHARED->state);

    KeyWaitNoPressed();

    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_X) break;
        if (k == KEY_A)
        {
            WifiInstallModule(True);
            return;
        }
        if (k == KEY_Y)
        {
            Bool ok = CrashLogSave("WIFI STOP", body);
            DrawRect(18, 202, WIDTH - 36, 10, RGBTO16(0, 16, 110));
            DrawText(ok ? "Saved crash log." : "No SD card - not saved",
                     18, 204, COL_WHITE);
            DispUpdate();
            while (KeyGet() != NOKEY) WaitMs(20);
        }
        WaitMs(50);
    }
}

static const char* FindModuleUf2(void)
{
    static const char* const paths[] = {
        "/wifi_module.uf2",
        "/WIFI_MODULE.UF2",
        "/apps/wifi_module.uf2",
        "/apps/WIFI_MODULE.UF2",
    };
    for (u32 i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
        if (FileExist(paths[i])) return paths[i];
    return NULL;
}

// Flash the native WiFi module from the SD card. On success the flasher
// resets the device; on failure it shows the reason and returns.
static void WifiInstallModule(Bool ask)
{
    if (!DiskValid() && !DiskAutoMount())
    {
        WifiMessage("No SD card.", "Copy wifi_module.uf2 to the card.");
        return;
    }

    const char* path = FindModuleUf2();
    if (!path)
    {
        WifiMessage("wifi_module.uf2 not found.", "Put it in / or /apps.");
        return;
    }

    Uf2Info info;
    if (!Uf2ReadInfo(path, &info) || info.kind != UF2_TARGET_FLASH ||
        (info.family_id != 0 && info.family_id != FAMILY_RP2040))
    {
        WifiMessage("Invalid WiFi module image.", path);
        return;
    }

    if (ask)
    {
        WifiFrame("WIFI INSTALL", "A: install   X: cancel");
        DrawText("Install the native WiFi module?", 20, 78, g_theme.menu_text);
        DrawText("This replaces any flashed game.", 20, 100, g_theme.warn_color);
        DrawText("The launcher, apps and SD stay.", 20, 120, g_theme.menu_dim);
        DrawText(path, 20, 150, g_theme.menu_text);
        DispUpdate();

        while (1)
        {
            u8 k = KeyGet();
            if (k == KEY_X) return;
            if (k == KEY_A) break;
            WaitMs(30);
        }
    }

    FlashOptions opts;
    opts.preserve_launcher = True;
    opts.apply_st7789_init = False;
    opts.apply_palette_lut = False;
    opts.apply_cf2 = False;
    opts.swap_yb = False;

    FlashResult r = FlasherWriteFlash(path, &info, &opts);
    const char* err = "Flash failed.";
    switch (r)
    {
    case FLASH_ERR_OPEN:      err = "Cannot open the module file."; break;
    case FLASH_ERR_INVALID:   err = "Invalid UF2 image."; break;
    case FLASH_ERR_TOO_BIG:   err = "Module is too big."; break;
    case FLASH_ERR_WRITE:     err = "Flash write error."; break;
    case FLASH_ERR_INTERRUPTED: err = "Cancelled."; break;
    default: break;
    }
    WifiMessage(err, path);
}

void WifiMenuRun(void)
{
    if (!NativeWifiPresent())
    {
        WifiFrame("WIFI", "A: install   X: back");
        DrawText("No WiFi module installed.", 20, 80, g_theme.warn_color);
        DrawText("Install the Pico W native module?", 20, 104, g_theme.menu_text);
        DrawText("It replaces any flashed game.", 20, 124, g_theme.menu_dim);
        DrawText("The launcher and SD card are kept.", 20, 142, g_theme.menu_dim);
        DispUpdate();

        while (1)
        {
            u8 k = KeyGet();
            if (k == KEY_X) return;
            if (k == KEY_A)
            {
                WifiInstallModule(True);
                return;
            }
            WaitMs(30);
        }
    }

    static const char* const labels[] = {
        "Status",
        "Scan networks",
        "Connect manually",
        "Connect saved",
        "Disconnect",
    };
    int selected = 0;
    Bool need_redraw = True;

    while (1)
    {
        if (need_redraw)
        {
            WifiFrame("WIFI", "A: run   X: back");
            DrawText(NativeWifiLinkUp() ? "Connected" : "Disconnected",
                     20, 52, NativeWifiLinkUp() ? g_theme.ok_color : g_theme.warn_color);
            DrawText(NativeWifiIp(), WIDTH - 150, 52, g_theme.menu_dim);

            for (int i = 0; i < 5; i++)
            {
                int y = 84 + i * 22;
                Bool cur = (i == selected);
                if (cur)
                    DrawRect(20, y - 2, WIDTH - 40, FONTH + 4, g_theme.menu_hl);
                DrawText(cur ? ">" : " ", 26, y,
                         cur ? g_theme.menu_sel : g_theme.menu_text);
                DrawText(labels[i], 46, y,
                         cur ? g_theme.menu_sel : g_theme.menu_text);
            }
            DispUpdate();
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }
        if (k == KEY_UP)   { selected = (selected + 4) % 5; need_redraw = True; }
        else if (k == KEY_DOWN) { selected = (selected + 1) % 5; need_redraw = True; }
        else if (k == KEY_A)
        {
            switch (selected)
            {
            case 0: WifiStatusScreen(); break;
            case 1: WifiScanMenu(); break;
            case 2: WifiConnectManual(); break;
            case 3: WifiConnectSaved(); break;
            case 4: WifiDisconnect(); break;
            }
            need_redraw = True;
        }
        else if (k == KEY_X) return;
    }
}
