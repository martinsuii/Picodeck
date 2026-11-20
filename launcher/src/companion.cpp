// ****************************************************************************
//
//                  PicoDeck — Companion Link
//
//   Line-based control protocol used by the desktop/Android companion app.
//   Works over USB CDC and over the native WiFi module's TCP server.
//
// ****************************************************************************

#include "../include.h"
#include "companion.h"
#include "menu.h"
#include "theme.h"
#include "native_wifi.h"

#if LAUNCHER_HAS_COMPANION

#define CMP_LINE_MAX  160
#define CMP_PORT      2323
#define CMP_PUT_MAX   8192

extern "C" void RuntimeTerm(void);
extern "C" void WatchdogSetup(u32 us, Bool pause_debug);


static char g_put_name[THEME_NAME_LEN];
static sFile g_put_file;
static Bool g_putting = False;
static u32  g_put_bytes = 0;
static Bool g_put_uart = False;

static void DrawLink(Bool usb, Bool force);

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

static void UsbStr(const char* s)
{
    while (*s) UsbDevCdcWriteChar(*s++);
}

// Send one protocol line over USB CDC.
static void Reply(const char* s)
{
    UsbStr(s);
    UsbStr("\r\n");
}

static void ReplyNum(const char* prefix, u32 v)
{
    char buf[48];
    strcpy(buf, prefix);
    DecNum(buf + StrLen(buf), v, 0);
    Reply(buf);
}

// ---------------------------------------------------------------------------
// Status / apps / themes
// ---------------------------------------------------------------------------

static void CmdStatus(void)
{
    int mv = LauncherBatMv();
    int pct = (mv - BATTERY_EMPTY_INT) * 100 /
              (BATTERY_FULL_INT - BATTERY_EMPTY_INT);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    const char* theme = ThemeActiveName();
    if (!theme || !theme[0]) theme = "(default)";

    char buf[96];
    strcpy(buf, "STATUS ");
    strcat(buf, LAUNCHER_VERSION_STR);
    strcat(buf, " ");
    DecNum(buf + StrLen(buf), (u32)mv, 0);
    strcat(buf, " ");
    DecNum(buf + StrLen(buf), (u32)pct, 0);
    strcat(buf, DiskValid() ? " 1" : " 0");
    strcat(buf, UsbDevCdcIsMounted() ? " 1" : " 0");
    strcat(buf, NativeWifiLinkUp() ? " 1" : " 0");
    strcat(buf, " ");
    strcat(buf, theme);
    Reply(buf);
}

static void CmdInfo(void)
{
    char buf[64];
    strcpy(buf, "INFO ");
    strcat(buf, LAUNCHER_VERSION_STR);
#if LAUNCHER_2MB
    strcat(buf, " 2MB");
#else
    strcat(buf, " 1MB");
#endif
    Reply(buf);
    Reply(UsbDevCdcIsMounted() ? "USB mounted" : "USB idle");
    if (NativeWifiLinkUp())
    {
        strcpy(buf, "WiFi ");
        strcat(buf, NativeWifiIp());
        Reply(buf);
    }
}

static void CmdApps(void)
{
    int n = CompanionAppCount();
    ReplyNum("APPS ", (u32)n);
    for (int i = 0; i < n; i++)
    {
        char buf[64];
        strcpy(buf, "APP ");
        DecNum(buf + StrLen(buf), (u32)i, 0);
        strcat(buf, " ");
        strcat(buf, CompanionAppLabel(i));
        Reply(buf);
    }
    Reply("END");
}

static void CmdThemes(void)
{
    static char names[THEME_MAX_FILES][THEME_NAME_LEN];
    int n = 0;
    if (DiskValid()) n = ThemeList(names, THEME_MAX_FILES);

    ReplyNum("THEMES ", (u32)n);
    for (int i = 0; i < n; i++)
    {
        char buf[THEME_NAME_LEN + 8];
        strcpy(buf, "THEME ");
        strcat(buf, names[i]);
        Reply(buf);
    }
    Reply("END");
}

static void CmdLaunch(const char* args)
{
    int id = 0;
    while (*args == ' ') args++;
    while (*args >= '0' && *args <= '9') { id = id * 10 + (*args - '0'); args++; }

    const char* label = CompanionAppLabel(id);
    if (!label) { Reply("ERR app id"); return; }

    Reply("OK launching");
    WaitMs(30);
    CompanionAppLaunch(id);
    DrawLink(UsbDevCdcIsMounted(), True);
}

// ---------------------------------------------------------------------------
// Theme upload (text only, terminated by a single '.')
// ---------------------------------------------------------------------------

static Bool ThemeNameOk(const char* name)
{
    int n = 0;
    if (!name[0] || name[0] == '.') return False;
    while (*name)
    {
        char c = *name++;
        Bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '-' ||
                  c == '.' || c == ' ';
        if (!ok || ++n >= THEME_NAME_LEN - 1) return False;
    }
    return True;
}

static void CmdThemePut(const char* name)
{
    char path[THEME_NAME_LEN + 16];

    while (*name == ' ') name++;
    if (!ThemeNameOk(name)) { Reply("ERR name"); return; }
    if (!DiskValid()) { Reply("ERR no sd"); return; }

    strcpy(g_put_name, name);
    DirCreate("/themes");
    strcpy(path, "/themes/");
    strcat(path, name);

    FileInit(&g_put_file);
    if (FileIsOpen(&g_put_file)) FileClose(&g_put_file);
    FileDelete(path);
    if (!FileCreate(&g_put_file, path)) { Reply("ERR create"); return; }

    g_putting = True;
    g_put_bytes = 0;
    g_put_uart = False;
    Reply("READY");
}

static void PutLine(const char* line)
{
    if (StrComp(line, ".") == 0)
    {
        FileClose(&g_put_file);
        g_putting = False;

        // Reload the active theme so a changed file shows up immediately.
        ThemeLoad();

        char buf[32];
        strcpy(buf, "OK ");
        DecNum(buf + StrLen(buf), g_put_bytes, 0);
        Reply(buf);
        return;
    }

    u32 len = StrLen(line);
    if (g_put_bytes + len + 1 > CMP_PUT_MAX)
    {
        Reply("ERR too big");
        return;
    }
    FileWrite(&g_put_file, line, len);
    FileWrite(&g_put_file, "\n", 1);
    g_put_bytes += len + 1;
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

static void CmdHelp(void)
{
    Reply("Commands:");
    Reply("  PING | INFO | STATUS | APPS | THEMES");
    Reply("  LAUNCH <id> | HOME");
    Reply("  THEME SET <file> | THEME PUT <file>");
    Reply("  REBOOT | BOOTSEL");
    Reply("Theme upload: THEME PUT name.thm, then lines, then '.'");
}

static void Dispatch(const char* line)
{
    if (!line[0]) return;

    if (StrComp(line, "PING") == 0) { Reply("PONG"); return; }
    if (StrComp(line, "INFO") == 0) { CmdInfo(); return; }
    if (StrComp(line, "STATUS") == 0) { CmdStatus(); return; }
    if (StrComp(line, "APPS") == 0) { CmdApps(); return; }
    if (StrComp(line, "THEMES") == 0) { CmdThemes(); return; }
    if (StrComp(line, "HELP") == 0 || StrComp(line, "?") == 0) { CmdHelp(); return; }
    if (StrComp(line, "HOME") == 0) { Reply("OK"); return; }

    if (strncmp(line, "LAUNCH ", 7) == 0) { CmdLaunch(line + 7); return; }

    if (strncmp(line, "THEME SET ", 10) == 0)
    {
        const char* name = line + 10;
        if (!DiskValid()) { Reply("ERR no sd"); return; }
        ThemeSetActive(name);
        Reply("OK");
        return;
    }
    if (strncmp(line, "THEME PUT ", 10) == 0) { CmdThemePut(line + 10); return; }

    // Native WiFi module commands (WIFI CONNECT/SCAN/STATUS, LINK ...).
    if (NativeWifiReady() &&
        (strncmp(line, "WIFI", 4) == 0 || line[0] == '!'))
    {
        NativeWifiSend(line);
        return;
    }

    if (StrComp(line, "REBOOT") == 0)
    {
        Reply("OK");
        WaitMs(80);
        RuntimeTerm();
        WatchdogSetup(0, False);
        while (1) {}
    }
    if (StrComp(line, "BOOTSEL") == 0)
    {
        Reply("OK");
        WaitMs(80);
        RebootToBootsel();
        return;
    }

    Reply("ERR unknown");
}

// ---------------------------------------------------------------------------
// Screen
// ---------------------------------------------------------------------------

static void DrawLink(Bool usb, Bool force)
{
    static Bool last_usb = False;
    static Bool last_wifi = False;
    Bool wifi = NativeWifiLinkUp();
    if (!force && usb == last_usb && wifi == last_wifi) return;
    last_usb = usb;
    last_wifi = wifi;

    DrawClearCol(g_theme.menu_bg);
    DrawText2("Companion Link", (WIDTH - 14 * FONTW * 2) / 2, 8,
              g_theme.menu_title);
    DrawRect(20, 30, WIDTH - 40, 1, g_theme.menu_dim);

    DrawText("USB:", 24, 52, g_theme.menu_dim);
    DrawText(usb ? "connected" : "waiting for host",
             84, 52, usb ? g_theme.ok_color : g_theme.menu_text);

    DrawText("WiFi:", 24, 74, g_theme.menu_dim);
    if (NativeWifiLinkUp())
    {
        char buf[40];
        strcpy(buf, NativeWifiIp());
        strcat(buf, ":2323");
        DrawText(buf, 84, 74, g_theme.ok_color);
    }
    else if (NativeWifiPresent())
    {
        DrawText("native: connecting", 84, 74, g_theme.warn_color);
    }
    else
    {
        DrawText("module not installed", 84, 74, g_theme.warn_color);
    }

    DrawText("Phone/PC app: Companion", 24, 108, g_theme.menu_text);
    DrawText("USB CDC or WiFi TCP", 24, 124, g_theme.menu_text);
    DrawText("X: back to menu", 24, HEIGHT - 14, g_theme.menu_dim);
    DispUpdate();
}

// ---------------------------------------------------------------------------
// Always-on USB CDC service (background)
// ---------------------------------------------------------------------------

void CompanionUsbInit(void)
{
    UsbDevInit(&UsbDevCdcSetupDesc);
}

// Read pending USB CDC lines and run the normal command dispatcher. Called
// from the main menu loop and from the Companion Link screen.
void CompanionPoll(void)
{
    static char line[CMP_LINE_MAX];
    static int len = 0;

    while (UsbDevCdcReadReady() > 0)
    {
        char c = (char)(u8)UsbDevCdcReadChar();
        if (c == '\r') continue;

        if (c == '\n')
        {
            line[len] = 0;
            if (g_putting && !g_put_uart) PutLine(line);
            else if (!g_putting) Dispatch(line);
            else Reply("ERR busy");
            len = 0;
            continue;
        }

        if (len < CMP_LINE_MAX - 1) line[len++] = c;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void CompanionLinkRun(void)
{
    // USB CDC must be initialised explicitly (same as the debug shell).
    UsbDevInit(&UsbDevCdcSetupDesc);

    while (UsbDevCdcReadReady() > 0) UsbDevCdcReadChar();
    DrawLink(False, True);

    u32  last_draw = 0;
    Bool usb_was = False;

    while (1)
    {
        if (KeyGet() == KEY_X) break;

        Bool usb = UsbDevCdcIsMounted();
        if (usb != usb_was || (u32)(Millis() - last_draw) > 1000)
        {
            DrawLink(usb, False);
            usb_was = usb;
            last_draw = Millis();
        }

        // Replies from the native WiFi module.
        {
            char rep[CMP_LINE_MAX];
            while (NativeWifiPopReply(rep, sizeof(rep))) Reply(rep);
        }

        CompanionPoll();
        WaitMs(10);
    }
}

#endif // LAUNCHER_HAS_COMPANION
