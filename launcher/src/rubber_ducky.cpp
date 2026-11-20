// ****************************************************************************
//
//                  PicoDeck — Rubber Ducky tool
//
//   Looks for a /payloads folder on the SD card, lists its *.txt files, then
//   presents the PicoPad as a USB HID keyboard to the connected host and
//   replays the selected DuckyScript payload. Execution starts only once the
//   host has enumerated/mounted the keyboard.
//
//   Supported DuckyScript subset:
//     REM <comment>
//     DELAY <ms>
//     DEFAULTDELAY <ms> / DEFAULT_DELAY <ms>
//     STRING <text>
//     STRINGLN <text>
//     JIGGLE [ms]      (mouse jiggle; optional duration in ms)
//     REPEAT <n>
//     Key combos, e.g. GUI r, CTRL ALT DELETE, CTRL SHIFT ESC
//     Named keys: ENTER RETURN TAB SPACE ESC ESCAPE BACKSPACE DELETE DEL
//                 INSERT HOME END PAGEUP PAGE_UP PAGEDOWN PAGE_DOWN UP DOWN
//                 LEFT RIGHT CAPSLOCK PRINTSCREEN SCROLLLOCK PAUSE MENU APP
//                 F1..F12 and single characters.
//
//   Controls:
//     A  : choose payload
//     X  : cancel / exit (does a watchdog reset)
//
// ****************************************************************************

#include "../include.h"
#if LAUNCHER_HAS_RUBBER_DUCKY

#include "rubber_ducky.h"
#include "file_picker.h"

extern "C" void RuntimeTerm(void);

#define DUCKY_MAX_SCRIPT  4096
#define DUCKY_LINE_MAX    256

// ---------------------------------------------------------------------------
// USB descriptors — a dedicated PID so Windows never confuses this HID-only
// device with the CDC debug shell / MSC reader (which share USB_PID).
// ---------------------------------------------------------------------------

#define DUCKY_PID  0xD001

static const u8 DuckyRepDescKeyboard[] = { HID_REPORT_DESC_KEYBOARD };
static const u8 DuckyRepDescMouse[]    = { HID_REPORT_DESC_MOUSE };

// Composite device: interface 0 = keyboard (EP1 IN), interface 1 = mouse
// (EP2 IN). The mouse interface lets payloads use the JIGGLE command.
static const u8 DuckyDescCfg[9 + 2 * USB_TEMP_HIDD_LEN] = {
    USB_TEMP_CFG(1, 2, 0, 9 + 2 * USB_TEMP_HIDD_LEN, 100),
    USB_TEMP_HIDD(0, 4, USB_HID_ITF_KEYB, sizeof(DuckyRepDescKeyboard),
                  USB_EPADDR(1, USB_DIR_IN), USB_PACKET_MAX, 10),
    USB_TEMP_HIDD(1, 5, USB_HID_ITF_MOUSE, sizeof(DuckyRepDescMouse),
                  USB_EPADDR(2, USB_DIR_IN), USB_PACKET_MAX, 10),
};

static const sUsbDescCfg* DuckyCfgList[1] =
    { (const sUsbDescCfg*)DuckyDescCfg };

// Indexed by HID interface index (0 = keyboard, 1 = mouse).
static const u8* DuckyRepList[2] = { DuckyRepDescKeyboard, DuckyRepDescMouse };

static const sUsbDescDev DuckyDescDev = {
    18,                  // size
    USB_DESC_DEVICE,     // type
    0x0200,              // USB 2.00
    0,                   // class — per-interface
    0,                   // subclass
    0,                   // protocol
    USB_PACKET_MAX,      // EP0 packet size
    0x2E8A,              // vendor (RPi)
    DUCKY_PID,           // product
    0x0100,              // device release
    1, 2, 3,             // string idxs (mfg, product, serial)
    1,                   // configurations
};

static const char* DuckyDescStr[] = {
    "Raspberry Pi",      // 1
    "PicoPad",           // 2
    "00000000",          // 3
    "USB HID Keyboard",  // 4
    "USB HID Mouse",     // 5
};

static const sUsbDevSetupDesc DuckySetupDesc = {
    .desc_dev      = &DuckyDescDev,
    .desc_cfg_list = DuckyCfgList,
    .desc_cfg_num  = 1,
    .desc_bos      = NULL,
    .desc_list     = (const void**)DuckyRepList,
    .mounted_cb    = NULL,
    .suspend_cb    = NULL,
    .resume_cb     = NULL,
    .str_cb        = NULL,
    .str_list      = DuckyDescStr,
    .str_num       = 5,
};

// Bring up the USB HID keyboard device. Also called from the menu so its USB
// connection indicator reflects a host connection.
void RubberDuckyUsbInit(void)
{
    UsbDevInit(&DuckySetupDesc);
}

// True while the HID keyboard interface is enumerated by a host.
Bool RubberDuckyUsbConnected(void)
{
    return UsbDevHidIsMounted();
}

// ---------------------------------------------------------------------------
// Script buffer
// ---------------------------------------------------------------------------

static char g_script[DUCKY_MAX_SCRIPT + 1];
static u32  g_default_delay = 0;
static u32  g_total_lines = 0;
static u32  g_line_no = 0;
static char g_status[64] = "";

// ---------------------------------------------------------------------------
// Key tables
// ---------------------------------------------------------------------------

typedef struct { const char* name; u8 key; } DuckyNamedKey;
typedef struct { const char* name; u8 modi; } DuckyModifierKey;

static const DuckyModifierKey DuckyModifiers[] = {
    {"CTRL",     USB_KEY_MODI_LCTRL},
    {"CONTROL",  USB_KEY_MODI_LCTRL},
    {"SHIFT",    USB_KEY_MODI_LSHIFT},
    {"ALT",      USB_KEY_MODI_LALT},
    {"GUI",      USB_KEY_MODI_LWIN},
    {"WINDOWS",  USB_KEY_MODI_LWIN},
    {"WIN",      USB_KEY_MODI_LWIN},
    {"META",     USB_KEY_MODI_LWIN},
    {"COMMAND",  USB_KEY_MODI_LWIN},
    {"CMD",      USB_KEY_MODI_LWIN},
    {"RCTRL",    USB_KEY_MODI_RCTRL},
    {"RSHIFT",   USB_KEY_MODI_RSHIFT},
    {"RALT",     USB_KEY_MODI_RALT},
};

static const DuckyNamedKey DuckyNamedKeys[] = {
    {"ENTER",        HID_KEY_ENTER},
    {"RETURN",       HID_KEY_ENTER},
    {"TAB",          HID_KEY_TAB},
    {"SPACE",        HID_KEY_SPACE},
    {"ESC",          HID_KEY_ESCAPE},
    {"ESCAPE",       HID_KEY_ESCAPE},
    {"BACKSPACE",    HID_KEY_BACKSPACE},
    {"DELETE",       HID_KEY_DELETE},
    {"DEL",          HID_KEY_DELETE},
    {"INSERT",       HID_KEY_INSERT},
    {"HOME",         HID_KEY_HOME},
    {"END",          HID_KEY_END},
    {"PAGEUP",       HID_KEY_PAGE_UP},
    {"PAGE_UP",      HID_KEY_PAGE_UP},
    {"PAGEDOWN",     HID_KEY_PAGE_DOWN},
    {"PAGE_DOWN",    HID_KEY_PAGE_DOWN},
    {"UP",           HID_KEY_ARROW_UP},
    {"DOWN",         HID_KEY_ARROW_DOWN},
    {"LEFT",         HID_KEY_ARROW_LEFT},
    {"RIGHT",        HID_KEY_ARROW_RIGHT},
    {"CAPSLOCK",     HID_KEY_CAPS_LOCK},
    {"CAPS_LOCK",    HID_KEY_CAPS_LOCK},
    {"PRINTSCREEN",  HID_KEY_PRINT_SCREEN},
    {"PRINT_SCREEN", HID_KEY_PRINT_SCREEN},
    {"SCROLLLOCK",   HID_KEY_SCROLL_LOCK},
    {"PAUSE",        HID_KEY_PAUSE},
    {"MENU",         HID_KEY_MENU},
    {"APP",          HID_KEY_MENU},
    {"F1",           HID_KEY_F1},
    {"F2",           HID_KEY_F2},
    {"F3",           HID_KEY_F3},
    {"F4",           HID_KEY_F4},
    {"F5",           HID_KEY_F5},
    {"F6",           HID_KEY_F6},
    {"F7",           HID_KEY_F7},
    {"F8",           HID_KEY_F8},
    {"F9",           HID_KEY_F9},
    {"F10",          HID_KEY_F10},
    {"F11",          HID_KEY_F11},
    {"F12",          HID_KEY_F12},
};

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

static Bool StrEqCI(const char* a, const char* b)
{
    while (*a && *b)
    {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return False;
        a++; b++;
    }
    return *a == 0 && *b == 0;
}

// True if "s" begins with command word "cmd" followed by space/tab/end.
static Bool CmdIs(const char* s, const char* cmd)
{
    while (*cmd)
    {
        char ca = *s, cb = *cmd;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return False;
        s++; cmd++;
    }
    return (*s == 0 || *s == ' ' || *s == '\t');
}

static u8 DuckyModifier(const char* tok)
{
    for (u32 i = 0; i < sizeof(DuckyModifiers)/sizeof(DuckyModifiers[0]); i++)
        if (StrEqCI(tok, DuckyModifiers[i].name)) return DuckyModifiers[i].modi;
    return 0;
}

// Returns HID key code, or 0xFF if unknown. Sets *extra_modi for shifted chars.
static u8 DuckyKeyCode(const char* tok, u8* extra_modi)
{
    *extra_modi = 0;

    // Single character → ASCII→HID table
    if (tok[0] != 0 && tok[1] == 0)
    {
        u8 c = (u8)tok[0];
        if (c < ASCII_TO_HIDKEY_NUM)
        {
            *extra_modi = (AsciiToHidKey[c*2] != 0) ? USB_KEY_MODI_LSHIFT : 0;
            return AsciiToHidKey[c*2 + 1];
        }
        return 0xFF;
    }

    for (u32 i = 0; i < sizeof(DuckyNamedKeys)/sizeof(DuckyNamedKeys[0]); i++)
        if (StrEqCI(tok, DuckyNamedKeys[i].name)) return DuckyNamedKeys[i].key;

    return 0xFF;
}

static Bool ParseU32(const char* s, u32* out)
{
    while (*s == ' ' || *s == '\t') s++;
    if (*s < '0' || *s > '9') return False;
    u32 v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (u32)(*s - '0'); s++; }
    *out = v;
    return True;
}

// ---------------------------------------------------------------------------
// HID send helpers
// ---------------------------------------------------------------------------
// Note: PicoLibSDK's HID device driver never clears hid->tx_len after a
// transfer completes (UsbDevHidComp just re-sends the same report), so
// UsbDevHidSendReady() reports "not ready" forever once the first report has
// been queued. We therefore do NOT gate on SendReady — the SDK's SendKey/
// SendChar already insert the required per-report delays. We only verify the
// host is still mounted, which is the condition that actually matters.

// Wait briefly for the host to (re)appear before declaring it gone.
static Bool DuckyEnsureMounted(void)
{
    if (UsbDevHidIsMounted()) return True;
    u32 start = Millis();
    while ((u32)(Millis() - start) < 500)
    {
        WaitMs(10);
        if (UsbDevHidIsMounted()) return True;
    }
    return False;
}

static Bool DuckySendKey(u8 key, u8 modi)
{
    if (!DuckyEnsureMounted()) return False;
    return UsbDevHidSendKey(key, modi);
}

static Bool DuckySendString(const char* s)
{
    while (*s)
    {
        if (!DuckyEnsureMounted()) return False;
        if (!UsbDevHidSendChar(*s)) return False;
        s++;
    }
    return True;
}

// One mouse "jiggle": a small move out and back so the pointer doesn't drift.
static Bool DuckyJiggleOnce(void)
{
    if (!DuckyEnsureMounted()) return False;
    UsbDevHidSendMouse(6, 6, False, False, False);
    WaitMs(20);
    UsbDevHidSendMouse(-6, -6, False, False, False);
    WaitMs(20);
    return True;
}

// Public wrappers for other tools (keyboard pranks, etc.).
Bool RubberDuckySendKey(u8 key, u8 modi) { return DuckySendKey(key, modi); }
Bool RubberDuckySendText(const char* text) { return DuckySendString(text); }
Bool RubberDuckyMouseJiggle(void) { return DuckyJiggleOnce(); }
char* RubberDuckyScriptBuffer(void) { return g_script; }

// ---------------------------------------------------------------------------
// Line execution
// ---------------------------------------------------------------------------

// Execute one (mutable, in-place tokenized) DuckyScript line.
// Returns False if the host went away / send failed.
static Bool DuckyExecLine(char* line)
{
    // Trim
    char* s = line;
    while (*s == ' ' || *s == '\t') s++;
    int n = (int)StrLen(s);
    while (n > 0 && (s[n-1] == '\r' || s[n-1] == '\n' ||
                     s[n-1] == ' '  || s[n-1] == '\t')) s[--n] = 0;
    if (*s == 0) return True;

    if (CmdIs(s, "REM")) return True;

    if (CmdIs(s, "STRINGLN"))
    {
        char* txt = s + 8;
        while (*txt == ' ' || *txt == '\t') txt++;
        if (!DuckySendString(txt)) return False;
        return DuckySendKey(HID_KEY_ENTER, 0);
    }

    if (CmdIs(s, "STRING"))
    {
        char* txt = s + 6;
        while (*txt == ' ' || *txt == '\t') txt++;
        return DuckySendString(txt);
    }

    if (CmdIs(s, "JIGGLE"))
    {
        // JIGGLE          -> one jiggle
        // JIGGLE <ms>     -> keep jiggling for <ms> milliseconds
        u32 ms = 0;
        if (ParseU32(s + 6, &ms) && ms > 0)
        {
            u32 start = Millis();
            while ((u32)(Millis() - start) < ms)
            {
                if (!DuckyJiggleOnce()) return False;
                WaitMs(30);
            }
        }
        else
        {
            if (!DuckyJiggleOnce()) return False;
        }
        return True;
    }

    if (CmdIs(s, "DELAY"))
    {
        u32 ms;
        if (ParseU32(s + 5, &ms)) WaitMs(ms);
        return True;
    }

    if (CmdIs(s, "DEFAULTDELAY") || CmdIs(s, "DEFAULT_DELAY"))
    {
        const char* p = s;
        while (*p && *p != ' ' && *p != '\t') p++;
        u32 ms;
        if (ParseU32(p, &ms)) g_default_delay = ms;
        return True;
    }

    // Key combination: modifiers + final key.
    u8 modi = 0, key = 0;
    Bool have_key = False;

    char* p = s;
    while (*p)
    {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 0) break;

        char* tok = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        char saved = *p;
        *p = 0;

        u8 m = DuckyModifier(tok);
        if (m != 0)
        {
            modi |= m;
        }
        else
        {
            u8 extra = 0;
            u8 k = DuckyKeyCode(tok, &extra);
            if (k != 0xFF) { key = k; modi |= extra; have_key = True; }
        }

        if (saved == 0) break;
        p++;
    }

    if (modi != 0 || have_key)
        return DuckySendKey(have_key ? key : 0, modi);

    return True;
}

// ---------------------------------------------------------------------------
// Script runner
// ---------------------------------------------------------------------------

// Build the "[line] description" status string for the current command.
static void DuckyDescribe(const char* line)
{
    const char* s = line;
    while (*s == ' ' || *s == '\t') s++;

    char num[12];
    DecNum(num, g_line_no, 0);
    strcpy(g_status, "[");
    strcat(g_status, num);
    strcat(g_status, "] ");

    if (CmdIs(s, "STRINGLN") || CmdIs(s, "STRING"))
    {
        strcat(g_status, "Typing...");
    }
    else if (CmdIs(s, "JIGGLE"))
    {
        strcat(g_status, "Jiggling mouse...");
    }
    else if (CmdIs(s, "DELAY"))
    {
        u32 ms;
        if (ParseU32(s + 5, &ms))
        {
            char b[16];
            DecNum(b, ms, 0);
            strcat(g_status, "Delaying ");
            strcat(g_status, b);
            strcat(g_status, "ms");
        }
        else strcat(g_status, "Delay");
    }
    else if (CmdIs(s, "DEFAULTDELAY") || CmdIs(s, "DEFAULT_DELAY"))
    {
        const char* p = s;
        while (*p && *p != ' ' && *p != '\t') p++;
        u32 ms;
        if (ParseU32(p, &ms))
        {
            char b[16];
            DecNum(b, ms, 0);
            strcat(g_status, "Default delay ");
            strcat(g_status, b);
            strcat(g_status, "ms");
        }
        else strcat(g_status, "Default delay");
    }
    else if (CmdIs(s, "REM"))
    {
        strcat(g_status, "Comment");
    }
    else if (CmdIs(s, "REPEAT"))
    {
        const char* p = s;
        while (*p && *p != ' ' && *p != '\t') p++;
        u32 c;
        if (ParseU32(p, &c))
        {
            char b[16];
            DecNum(b, c, 0);
            strcat(g_status, "Repeating x");
            strcat(g_status, b);
        }
        else strcat(g_status, "Repeat");
    }
    else if (*s == 0)
    {
        strcat(g_status, "Blank");
    }
    else
    {
        // Key combination — show the raw command (truncated).
        strcat(g_status, "Pressing ");
        int len = (int)StrLen(g_status);
        int n = 0;
        while (s[n] && n < 24 && len < (int)sizeof(g_status) - 1)
            g_status[len++] = s[n++];
        g_status[len] = 0;
    }
}

static void DuckyProgress(void)
{
    // Clear the previous (possibly longer) status line, then draw the new one.
    DrawTextBg("                                        ", 20, 168,
               COL_LTGRAY, COL_BLACK);
    DrawTextBg(g_status, 20, 168, COL_LTGRAY, COL_BLACK);
    DispUpdate();
}

static Bool DuckyRun(void)
{
    char line[DUCKY_LINE_MAX];
    char prev[DUCKY_LINE_MAX];
    prev[0] = 0;

    const char* p = g_script;
    while (*p)
    {
        int i = 0;
        while (*p && *p != '\n' && i < DUCKY_LINE_MAX - 1) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;

        g_line_no++;
        DuckyDescribe(line);
        DuckyProgress();

        if (CmdIs(line, "REPEAT"))
        {
            u32 cnt;
            const char* q = line;
            while (*q && *q != ' ' && *q != '\t') q++;
            if (ParseU32(q, &cnt))
            {
                for (u32 r = 0; r < cnt; r++)
                {
                    char work[DUCKY_LINE_MAX];
                    strcpy(work, prev);
                    if (!DuckyExecLine(work)) return False;
                    if (g_default_delay) WaitMs(g_default_delay);
                }
            }
        }
        else
        {
            char work[DUCKY_LINE_MAX];
            strcpy(work, line);
            // Remember before executing (ExecLine mutates work, not line).
            strcpy(prev, line);
            if (!DuckyExecLine(work)) return False;
            if (g_default_delay) WaitMs(g_default_delay);
        }

        if (!DuckyEnsureMounted()) return False;  // host unplugged
    }

    return True;
}

// ---------------------------------------------------------------------------
// SD helpers
// ---------------------------------------------------------------------------

static Bool DuckyFolderExists(const char* path)
{
    sFile find;
    FileInit(&find);
    Bool ok = FindOpen(&find, path);
    if (ok) FindClose(&find);
    return ok;
}

static u32 DuckyLoadFile(const char* path)
{
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return 0;

    u32 size = FileSize(&f);
    if (size > DUCKY_MAX_SCRIPT) size = DUCKY_MAX_SCRIPT;

    u32 rd = FileRead(&f, g_script, size);
    FileClose(&f);
    g_script[rd] = 0;
    return rd;
}

static u32 DuckyCountLines(const char* s)
{
    u32 n = 0;
    while (*s) { if (*s == '\n') n++; s++; }
    return n + 1;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

static void DuckyMessage(const char* title, const char* l1, const char* l2)
{
    DrawClear();
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 30,
              COL_YELLOW);
    if (l1) DrawText(l1, 20, 100, COL_WHITE);
    if (l2) DrawText(l2, 20, 120, COL_LTGRAY);
    DrawText("Press X to return", 20, 210, COL_GRAY);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

void RubberDuckyRun(void)
{
    // 1) Locate /payloads and pick a .txt payload.
    if (!DiskAutoMount())
    {
        DuckyMessage("Rubber Ducky", "SD card not found",
                     "Insert SD card and try again");
        return;
    }

    if (!DuckyFolderExists("/payloads"))
    {
        DuckyMessage("Rubber Ducky", "No /payloads folder on SD card",
                     "Create it and add .txt payloads");
        return;
    }

    char path[256];
    PickerResult pr = FilePickerRun("/payloads", "*.txt", path, sizeof(path));
    if (pr != PICKER_RESULT_SELECTED) return;

    // 2) Load the script.
    u32 len = DuckyLoadFile(path);
    if (len == 0)
    {
        DuckyMessage("Rubber Ducky", "Could not read payload",
                     "File is empty or unreadable");
        return;
    }

    g_total_lines = DuckyCountLines(g_script);
    g_line_no = 0;
    g_default_delay = 0;

    // 3) Bring up USB HID and wait for the host to connect.
    UsbDevInit(&DuckySetupDesc);
    Bool ok = False;   // declared before the wait loop so the X-cancel goto
                       // does not cross its initialization

    DrawClear();
    DrawText2("Rubber Ducky", (WIDTH - 12 * FONTW * 2) / 2, 30, COL_YELLOW);
    DrawText("Payload:", 20, 80, COL_LTGRAY);
    const char* fname = path;
    for (const char* q = path; *q; q++) if (*q == '/') fname = q + 1;
    DrawText(fname, 20, 96, COL_WHITE);
    DrawText("Connect PicoPad to target PC.", 20, 130, COL_LTGRAY);
    DrawText("Press X to cancel", 20, 210, COL_GRAY);
    DispUpdate();

    {
        int blink = 0;
        while (!UsbDevHidIsMounted())
        {
            if (KeyGet() == KEY_X) goto exit_ducky;
            WaitMs(100);
            DrawTextBg((++blink & 1) ? "Waiting for host..." : "                  ",
                       20, 155, COL_WHITE, COL_BLACK);
            DispUpdate();
        }
    }

    // Let the host finish binding the HID driver before typing.
    DrawTextBg("Connected! Sending payload...     ", 20, 155,
               COL_GREEN, COL_BLACK);
    DispUpdate();
    WaitMs(800);

    // 4) Execute.
    ok = DuckyRun();

    DuckyProgress();

    if (!ok)
        DuckyMessage("Rubber Ducky", "Payload aborted",
                     "Host disconnected or send failed");
    else
        DuckyMessage("Rubber Ducky", "Payload sent!", "Done.");

exit_ducky:
    // Watchdog reset for clean USB state (same as MSC / debug shell).
    DrawClear();
    DrawText2("Restarting...", 80, 100, COL_LTGRAY);
    DispUpdate();
    WaitMs(300);
    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}
}

#endif // LAUNCHER_HAS_RUBBER_DUCKY
