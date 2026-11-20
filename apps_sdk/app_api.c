// ****************************************************************************
//
//                    PicoPad custom app API (v1) — implementation
//
// ****************************************************************************

#include "include.h"
#include "../app_shared.h"
#include "../wifi_module/wifi_shared.h"

#define CFG_BUF_LEN 1024

static char g_wifi_ip[16];
static char g_wifi_ssid[33];

// ---------------------------------------------------------------------------
// Handoff / identity
// ---------------------------------------------------------------------------

static const app_info_t* Info(void)
{
    const app_info_t* ai = (const app_info_t*)APP_INFO_ADDR;
    if (ai->magic != APP_INFO_MAGIC) return NULL;
    if (ai->api_version != APP_API_VERSION) return NULL;
    return ai;
}

const char* AppId(void)
{
    const app_info_t* ai = Info();
    return ai ? ai->id : "?";
}

uint32_t AppApiVersion(void) { return APP_API_VERSION; }

static const uint16_t g_default_theme[APP_TH_COUNT] = {
    RGBTO16(16, 16, 24),   // bg
    RGBTO16(32, 32, 48),   // panel
    COL_YELLOW,            // title
    COL_WHITE,             // text
    COL_GRAY,              // dim
    COL_CYAN,              // accent
    COL_GREEN,             // ok
    COL_YELLOW,            // warn
    COL_RED,               // err
    COL_WHITE,             // sel
    RGBTO16(60, 60, 90),   // hl
};

const char* AppArgPath(void)
{
    const app_info_t* ai = Info();
    if (ai && ai->ext_magic == APP_INFO_EXT_MAGIC) return ai->arg_path;
    return "";
}

uint16_t AppTheme(int idx)
{
    const app_info_t* ai = Info();
    if (idx < 0 || idx >= APP_TH_COUNT) return COL_WHITE;
    if (ai && ai->ext_magic == APP_INFO_EXT_MAGIC) return ai->theme[idx];
    return g_default_theme[idx];
}

const char* AppThemeName(void)
{
    const app_info_t* ai = Info();
    if (ai && ai->ext_magic == APP_INFO_EXT_MAGIC && ai->theme_name[0])
        return ai->theme_name;
    return "default";
}

const char* AppDataPath(void)
{
    const app_info_t* ai = Info();
    return ai ? ai->data_path : "/apps";
}

// ---------------------------------------------------------------------------
// Exit / reboot / recovery
// ---------------------------------------------------------------------------

static __attribute__((noreturn)) void ResetNow(uint32_t scratch1, uint32_t scratch2)
{
    WATCHDOG_SCRATCH[0] = 0;
    WATCHDOG_SCRATCH[APP_SCRATCH_EXIT] = scratch1;
    WATCHDOG_SCRATCH[APP_SCRATCH_RECOVERY] = scratch2;

    __asm volatile ("cpsid i" ::: "memory");
    WatchdogSetup(0, False);
    while (1) {}
}

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t resume;
    uint32_t snapshot;
    uint32_t size;
    uint32_t vector;
} LauncherApi;

static const LauncherApi* FindLauncherApi(void)
{
    for (uint32_t a = 0x100E0000u;
         a + sizeof(LauncherApi) <= 0x100FB000u; a += 4u)
    {
        const volatile uint32_t* w = (const volatile uint32_t*)a;
        if (w[0] != APP_LAUNCHER_MAGIC) continue;

        const LauncherApi* api = (const LauncherApi*)a;
        if (api->version != 1u) continue;
        if (api->resume < 0x100E0000u || api->resume >= 0x100FB000u) continue;
        if (api->snapshot != LAUNCHER_SNAPSHOT_ADDR) continue;
        if (api->size != LAUNCHER_SNAPSHOT_SIZE) continue;
        if (api->vector < 0x20000000u || api->vector >= 0x20039000u) continue;
        return api;
    }
    return 0;
}

void AppExit(void)
{
    // Preferred: restore the launcher's RAM and re-enter its menu without a
    // chip reset. The launcher writes the snapshot before every app launch.
    if (WATCHDOG_SCRATCH[APP_SCRATCH_SNAPSHOT] == APP_SNAPSHOT_VALID)
    {
        const LauncherApi* api = FindLauncherApi();
        if (api)
        {
            WATCHDOG_SCRATCH[APP_SCRATCH_SNAPSHOT] = 0;
            ((void (*)(void))api->resume)();
        }
    }

    // Fallback for an older launcher or a failed snapshot.
    ResetNow(APP_EXIT_MAGIC, 0);
}

void AppReboot(void)
{
    ResetNow(0, 0);
}

void AppRecovery(void)
{
    ResetNow(0, APP_RECOVERY_MAGIC);
}

void AppBootsel(void)
{
    reset_usb_boot(0, 0);
    while (1) {}
}

// ---------------------------------------------------------------------------
// Crash screens
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Crash screen (full-screen BSOD style, direct display rows)
// ---------------------------------------------------------------------------

#define BSOD_BG   RGBTO16(96, 0, 0)
#define BSOD_HDR  RGBTO16(48, 0, 0)
#define BSOD_DIM  RGBTO16(255, 190, 190)

static void BsodFill(u16 bg)
{
    for (int y = 0; y < HEIGHT; y += FONTH)
        DispDrawTextRow("", 0, y, COL_WHITE, bg);
}

static void BsodRow(int y, const char* text, u16 col, u16 bg)
{
    DispDrawTextRow(text, 8, y, col, bg);
}

static void BsodHeader(const char* title)
{
    BsodFill(BSOD_BG);
    for (int y = 0; y < 16; y += FONTH)
        DispDrawTextRow("", 0, y, COL_WHITE, BSOD_HDR);

    BsodRow(0, title, COL_WHITE, BSOD_HDR);
    DispDrawTextRow(":(", WIDTH - 24, 0, COL_WHITE, BSOD_HDR);
}

static char g_crash_path[32];

static Bool AppSaveCrashLog(const char* title, const char* body)
{
    g_crash_path[0] = 0;

    // App runtime init zeroes the SDK state (including SDType). If the crash
    // happened before SDInit, the card is still there; mount it now.
    if (!DiskValid())
    {
        SDInit();
        DiskAutoMount();
    }
    if (!DiskValid()) return False;

    DirCreate("/crash");

    char path[32];
    Bool found = False;
    for (int i = 1; i < 1000; i++)
    {
        strcpy(path, "/crash/CRASH");
        path[12] = (char)('0' + (i / 100) % 10);
        path[13] = (char)('0' + (i / 10) % 10);
        path[14] = (char)('0' + i % 10);
        strcat(path, ".TXT");
        if (!FileExist(path)) { found = True; break; }
    }
    if (!found) strcpy(path, "/crash/CRASH999.TXT");

    char buf[512];
    int o = 0;
    const char* a = title ? title : "APP CRASH";
    while (*a && o < 511) buf[o++] = *a++;
    buf[o++] = '\n';
    const char* b = "PicoDeck app log\n";
    while (*b && o < 511) buf[o++] = *b++;
    if (body) { const char* c = body; while (*c && o < 511) buf[o++] = *c++; }
    if (o < 511) buf[o++] = '\n';
    buf[o] = 0;

    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, path)) return False;
    Bool ok = (FileWrite(&f, buf, (u32)o) == (u32)o);
    FileClose(&f);
    if (!ok) return False;

    strncpy(g_crash_path, path, sizeof(g_crash_path) - 1);
    g_crash_path[sizeof(g_crash_path) - 1] = 0;
    return True;
}

// Direct GPIO read: in a fault the SysTick key scan is dead.
static int BsodKey(void)
{
    if (GPIO_In(BTN_UP_PIN) == 0 && GPIO_In(BTN_X_PIN) == 0) return 3;
    if (GPIO_In(BTN_A_PIN) == 0) return 1;
    if (GPIO_In(BTN_X_PIN) == 0) return 2;
    if (GPIO_In(BTN_Y_PIN) == 0) return 4;
    return 0;
}

static __attribute__((noreturn)) void BsodWait(const char* body)
{
    for (;;)
    {
        int k = BsodKey();
        if (k == 3) AppRecovery();
        if (k == 1) AppExit();
        if (k == 2) AppReboot();
        if (k == 4)
        {
            Bool ok = AppSaveCrashLog("APP CRASH", body);
            BsodRow(200, ok ? g_crash_path : "No SD card - log not saved",
                    ok ? COL_WHITE : BSOD_DIM, BSOD_BG);
            while (BsodKey() == 4) WaitMs(20);
        }
        WaitMs(30);
    }
}

static void BsodFooter(void)
{
    BsodRow(152, "A: launcher", COL_WHITE, BSOD_BG);
    BsodRow(168, "X: restart", COL_WHITE, BSOD_BG);
    BsodRow(184, "UP+X: recovery", COL_WHITE, BSOD_BG);
    BsodRow(200, "Y: save crash log", COL_WHITE, BSOD_BG);
    BsodRow(224, "Press RESET if buttons do nothing", BSOD_DIM, BSOD_BG);
}

void AppBsod(const char* title, const char* line1,
             const char* line2, uint32_t code)
{
    SelFont8x8();
    DispBacklight(255);

    BsodHeader(title ? title : "APP CRASH");

    char buf[40];
    strcpy(buf, "App: ");
    strcat(buf, AppId());
    BsodRow(24, buf, COL_WHITE, BSOD_BG);

    if (line1) BsodRow(40, line1, COL_WHITE, BSOD_BG);
    if (line2) BsodRow(56, line2, BSOD_DIM, BSOD_BG);

    strcpy(buf, "CODE ");
    DecHexNum(buf + 5, code, 8);
    BsodRow(80, buf, COL_WHITE, BSOD_BG);

    // Text log body.
    char body[256];
    strcpy(body, "App: ");
    strcat(body, AppId());
    strcat(body, "\nCode: 0x");
    DecHexNum(body + StrLen(body), code, 8);

    BsodFooter();
    BsodWait(body);
}

// Called by the crt0 unhandled-IRQ path.
void FatalErrorMsg(sFatalFrame* f)
{
    __asm volatile ("cpsid i" ::: "memory");
    SelFont8x8();
    DispBacklight(255);

    BsodHeader("APP CRASH");

    char buf[48];
    strcpy(buf, "App: ");
    strcat(buf, AppId());
    BsodRow(24, buf, COL_WHITE, BSOD_BG);
    BsodRow(40, "Unhandled IRQ or hard fault", BSOD_DIM, BSOD_BG);

    u32 irq = f ? (u32)f->irq : 0;
    u32 pc  = f ? f->pc : 0;
    u32 cfsr = *(volatile u32*)0xE000ED28u;
    u32 hfsr = *(volatile u32*)0xE000ED2Cu;

    strcpy(buf, "IRQ ");
    DecHexNum(buf + 4, irq, 2);
    strcat(buf, "   PC ");
    DecHexNum(buf + StrLen(buf), pc, 8);
    BsodRow(64, buf, COL_WHITE, BSOD_BG);

    strcpy(buf, "CFSR ");
    DecHexNum(buf + 5, cfsr, 8);
    BsodRow(80, buf, BSOD_DIM, BSOD_BG);

    strcpy(buf, "HFSR ");
    DecHexNum(buf + 5, hfsr, 8);
    BsodRow(96, buf, BSOD_DIM, BSOD_BG);

    char body[320];
    strcpy(body, "App: ");
    strcat(body, AppId());
    strcat(body, "\nIRQ: ");
    DecHexNum(body + StrLen(body), irq, 2);
    strcat(body, "\nPC: 0x");
    DecHexNum(body + StrLen(body), pc, 8);
    strcat(body, "\nCFSR: 0x");
    DecHexNum(body + StrLen(body), cfsr, 8);
    strcat(body, "\nHFSR: 0x");
    DecHexNum(body + StrLen(body), hfsr, 8);
    if (f)
    {
        strcat(body, "\nR0: 0x");
        DecHexNum(body + StrLen(body), f->r0, 8);
        strcat(body, " R1: 0x");
        DecHexNum(body + StrLen(body), f->r1, 8);
        strcat(body, "\nLR: 0x");
        DecHexNum(body + StrLen(body), f->lr, 8);
        strcat(body, " SP: 0x");
        DecHexNum(body + StrLen(body), f->sp, 8);
    }

    BsodFooter();
    BsodWait(body);
}

// ---------------------------------------------------------------------------
// Battery
// ---------------------------------------------------------------------------

int AppBatteryMv(void)
{
    const wifi_shared_t* sh = (const wifi_shared_t*)WIFI_SHARED_ADDR;
    if (sh->magic == WIFI_HDR_MAGIC) return (int)sh->battery_mv;
    return 0;
}

// ---------------------------------------------------------------------------
// Config (apps/<id>/config.ini)
// ---------------------------------------------------------------------------

static Bool FindKey(const char* buf, const char* key, char* out, int max)
{
    int klen = (int)StrLen(key);
    const char* p = buf;

    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '#' || *p == ';')
        {
            while (*p && *p != '\n') p++;
            continue;
        }

        Bool match = True;
        for (int i = 0; i < klen; i++)
            if (p[i] != key[i]) { match = False; break; }

        if (match)
        {
            const char* q = p + klen;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '=')
            {
                q++;
                while (*q == ' ' || *q == '\t') q++;
                int n = 0;
                while (*q && *q != '\r' && *q != '\n' && n < max - 1)
                    out[n++] = *q++;
                while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
                out[n] = 0;
                return True;
            }
        }
        while (*p && *p != '\n') p++;
    }
    return False;
}

static int ReadConfig(char* buf, int max)
{
    const app_info_t* ai = Info();
    if (!ai) return -1;

    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, ai->config_path)) { buf[0] = 0; return 0; }
    int n = (int)FileRead(&f, buf, (u32)(max - 1));
    FileClose(&f);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

int AppConfigRead(const char* key, char* out, int max)
{
    static char buf[CFG_BUF_LEN];
    if (!key || !out || max <= 0) return -1;
    if (ReadConfig(buf, sizeof(buf)) < 0) return -1;
    return FindKey(buf, key, out, max) ? (int)StrLen(out) : -1;
}

int AppConfigWrite(const char* key, const char* value)
{
    static char in[CFG_BUF_LEN];
    static char out[CFG_BUF_LEN];
    const app_info_t* ai = Info();
    if (!ai || !key || !value) return -1;

    int len = ReadConfig(in, sizeof(in));
    if (len < 0) len = 0;

    int o = 0;
    const char* p = in;
    int klen = (int)StrLen(key);

    // Copy every line except the old value of this key.
    while (*p && o < CFG_BUF_LEN - 1)
    {
        const char* line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
        int llen = (int)(p - line);

        Bool match = False;
        if (llen > klen)
        {
            match = True;
            for (int i = 0; i < klen; i++)
                if (line[i] != key[i]) { match = False; break; }
            const char* q = line + klen;
            while (*q == ' ' || *q == '\t') q++;
            if (*q != '=') match = False;
        }
        if (match) continue;

        if (o + llen > CFG_BUF_LEN - 1) break;
        memcpy(out + o, line, llen);
        o += llen;
    }

    // Append key=value
    int need = klen + (int)StrLen(value) + 2;
    if (o + need > CFG_BUF_LEN - 1) return -1;
    memcpy(out + o, key, klen); o += klen;
    out[o++] = '=';
    int vlen = (int)StrLen(value);
    memcpy(out + o, value, vlen); o += vlen;
    out[o++] = '\n';

    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, ai->config_path)) return -1;
    Bool ok = (FileWrite(&f, out, (u32)o) == (u32)o);
    FileClose(&f);
    return ok ? 0 : -1;
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------

#define MOD_HDR_BASE   0x10000100u
#define MOD_HDR_SCAN   0x50000u

static int ModuleFound(void)
{
    static int found = -1;
    if (found >= 0) return found;

    found = 0;
    for (u32 off = 0; off + sizeof(wifi_hdr_t) <= MOD_HDR_SCAN; off += 4)
    {
        const wifi_hdr_t* h = (const wifi_hdr_t*)(MOD_HDR_BASE + off);
        if (h->magic != WIFI_HDR_MAGIC) continue;
        if (h->version != WIFI_PROTO_VERSION) continue;
        if (h->shared != WIFI_SHARED_ADDR) continue;
        found = 1;
        break;
    }
    return found;
}

int AppWifiPresent(void) { return ModuleFound(); }

static wifi_shared_t* Sh(void)
{
    if (!ModuleFound()) return NULL;
    return (wifi_shared_t*)WIFI_SHARED_ADDR;
}

int AppWifiReady(void)
{
    wifi_shared_t* sh = Sh();
    if (!sh || sh->magic != WIFI_HDR_MAGIC) return 0;
    return (sh->ready && !sh->stopped && !sh->paused) ? 1 : 0;
}

int AppWifiLinkUp(void)
{
    wifi_shared_t* sh = Sh();
    return (AppWifiReady() && sh->link_up) ? 1 : 0;
}

const char* AppWifiIp(void)
{
    wifi_shared_t* sh = Sh();
    g_wifi_ip[0] = 0;
    if (sh)
    {
        for (int i = 0; i < 15; i++)
        {
            g_wifi_ip[i] = (char)sh->ip[i];
            if (!g_wifi_ip[i]) break;
        }
        g_wifi_ip[15] = 0;
    }
    return g_wifi_ip;
}

int AppWifiRssi(void)
{
    wifi_shared_t* sh = Sh();
    return sh ? (int)sh->rssi : 0;
}

const char* AppWifiSsid(void)
{
    wifi_shared_t* sh = Sh();
    g_wifi_ssid[0] = 0;
    if (sh)
    {
        for (int i = 0; i < 32; i++)
        {
            g_wifi_ssid[i] = (char)sh->ssid[i];
            if (!g_wifi_ssid[i]) break;
        }
        g_wifi_ssid[32] = 0;
    }
    return g_wifi_ssid;
}

static Bool WifiSend(const char* line)
{
    wifi_shared_t* sh = Sh();
    if (!sh) return False;

    uint32_t head = sh->cmd_head;
    if (head - sh->cmd_tail >= WIFI_CMD_SLOTS) return False;

    char* dst = sh->cmd[head & (WIFI_CMD_SLOTS - 1)];
    int i = 0;
    for (; line[i] && i < WIFI_LINE_MAX - 1; i++) dst[i] = line[i];
    dst[i] = 0;

    __asm volatile ("dmb" ::: "memory");
    sh->cmd_head = head + 1;
    return True;
}

int AppWifiScan(AppWifiNet* out, int max, uint32_t timeout_ms)
{
    wifi_shared_t* sh = Sh();
    if (!sh || !out || max <= 0) return -1;

    uint32_t gen = sh->scan_gen;
    if (!WifiSend("WIFI SCAN")) return -3;

    u32 start = Time() / 1000u;
    while ((Time() / 1000u - start) < timeout_ms)
    {
        if (sh->scan_gen != gen)
        {
            int n = (int)sh->scan_count;
            if (n > 16) n = 16;
            if (n > max) n = max;
            for (int i = 0; i < n; i++)
            {
                for (int j = 0; j < 32; j++)
                {
                    out[i].ssid[j] = (char)sh->scan_ssid[i][j];
                    if (!out[i].ssid[j]) break;
                }
                out[i].ssid[32] = 0;
                out[i].rssi = (int)sh->scan_rssi[i];
            }
            return n;
        }
        WaitMs(20);
    }
    return -2;
}

void AppWifiConnect(const char* ssid, const char* pass)
{
    if (!ssid) return;
    char line[WIFI_LINE_MAX];
    strcpy(line, "WIFI CONNECT ");
    int at = (int)StrLen(line);

    for (int i = 0; ssid[i] && at < WIFI_LINE_MAX - 2; i++) line[at++] = ssid[i];
    line[at++] = '|';
    if (pass)
        for (int i = 0; pass[i] && at < WIFI_LINE_MAX - 1; i++) line[at++] = pass[i];
    line[at] = 0;
    WifiSend(line);
}

void AppWifiDisconnect(void)
{
    WifiSend("WIFI CLEAR");
}

// ---------------------------------------------------------------------------
// HTTP GET
// ---------------------------------------------------------------------------

int AppHttpGet(const char* url, char* out, int max)
{
    wifi_shared_t* sh = Sh();
    if (!sh || !AppWifiReady() || !url || !out || max <= 0) return -1;

    if (url[0] != 'h' || url[1] != 't' || url[2] != 't' ||
        url[3] != 'p' || url[4] != ':' || url[5] != '/' || url[6] != '/')
        return -2;
    if (StrLen(url) > WIFI_LINE_MAX - 12) return -3;

    char line[WIFI_LINE_MAX];
    strcpy(line, "HTTP GET ");
    strcat(line, url);

    sh->http_len = 0;
    sh->http_err = 0;
    sh->http_code = 0;
    __asm volatile ("dmb" ::: "memory");
    sh->http_state = 0;
    if (!WifiSend(line)) return -4;

    u32 start = Time() / 1000u;
    while ((Time() / 1000u - start) < 12000u)
    {
        uint32_t st = sh->http_state;
        if (st == 2)
        {
            int n = (int)sh->http_len;
            if (n > max) n = max;
            memcpy(out, (const void*)APP_NET_BUF_ADDR, (u32)n);
            return n;
        }
        if (st == 3) return -(int)sh->http_err;
        WaitMs(20);
    }
    return -5;
}


// ---------------------------------------------------------------------------
// Raw TCP sockets
// ---------------------------------------------------------------------------

int AppNetFeatures(void)
{
    if (!AppWifiReady()) return 0;
    return ((const wifi_shared_t*)WIFI_SHARED_ADDR)->net_features;
}

int AppNetState(int sock)
{
    if (!AppWifiReady() || sock < 0 || sock >= WIFI_NET_SOCKETS) return -1;
    return (int)((const wifi_shared_t*)WIFI_SHARED_ADDR)->net_state[sock];
}

int AppNetOpen(const char* host, int port)
{
    wifi_shared_t* sh = Sh();
    if (!sh || !AppWifiReady() || !host || !host[0]) return -1;
    if (!(sh->net_features & WIFI_NET_FEATURE_SOCKETS)) return -1;
    if (port <= 0 || port > 65535) return -1;
    if (StrLen(host) > 60) return -1;

    int sock = -1;
    for (int i = 0; i < WIFI_NET_SOCKETS; i++)
    {
        uint32_t st = sh->net_state[i];
        if (st == 0 || st == 4 || st == 5) { sock = i; break; }
    }
    if (sock < 0) return -1;

    sh->net_rx_len[sock] = 0;
    sh->net_tx_len[sock] = 0;
    sh->net_events[sock] = 0;
    sh->net_err[sock] = 0;
    __asm volatile ("dmb" ::: "memory");
    sh->net_state[sock] = 1;

    char line[WIFI_LINE_MAX];
    strcpy(line, "NET OPEN ");
    char num[8];
    DecNum(num, (u32)sock, 0);
    strcat(line, num);
    strcat(line, " ");
    strcat(line, host);
    strcat(line, ":");
    DecNum(num, (u32)port, 0);
    strcat(line, num);

    if (!WifiSend(line)) return -1;
    return sock;
}

int AppNetSend(int sock, const void* data, int len)
{
    wifi_shared_t* sh = Sh();
    if (!sh || !AppWifiReady() || sock < 0 || sock >= WIFI_NET_SOCKETS)
        return -1;
    if (len < 0 || len > (int)WIFI_NET_BUF_SIZE || !data) return -1;
    if (sh->net_state[sock] != 3) return -2;

    // Wait for a previous send to be consumed.
    u32 start = Time() / 1000u;
    while (sh->net_tx_len[sock] != 0 && (Time() / 1000u - start) < 2000u)
        WaitMs(10);
    if (sh->net_tx_len[sock] != 0) return -3;

    if (len > 0)
        memcpy((void*)WIFI_NET_TX(sock), data, (u32)len);
    __asm volatile ("dmb" ::: "memory");
    sh->net_tx_len[sock] = (u32)len;

    char line[32];
    strcpy(line, "NET SEND ");
    char num[8];
    DecNum(num, (u32)sock, 0);
    strcat(line, num);
    strcat(line, " ");
    DecNum(num, (u32)len, 0);
    strcat(line, num);
    if (!WifiSend(line))
    {
        sh->net_tx_len[sock] = 0;
        return -1;
    }

    start = Time() / 1000u;
    while (sh->net_tx_len[sock] != 0 && (Time() / 1000u - start) < 2000u)
        WaitMs(10);
    return (sh->net_tx_len[sock] == 0) ? 0 : -3;
}

int AppNetRecv(int sock, void* buf, int max)
{
    wifi_shared_t* sh = Sh();
    if (!sh || !AppWifiReady() || sock < 0 || sock >= WIFI_NET_SOCKETS ||
        !buf || max <= 0)
        return -1;

    u32 n = sh->net_rx_len[sock];
    if (n == 0) return 0;
    if (n > (u32)max) n = (u32)max;

    memcpy(buf, (const void*)WIFI_NET_RX(sock), n);
    __asm volatile ("dmb" ::: "memory");
    sh->net_rx_len[sock] = 0;
    return (int)n;
}

void AppNetClose(int sock)
{
    wifi_shared_t* sh = Sh();
    if (!sh || sock < 0 || sock >= WIFI_NET_SOCKETS) return;

    char line[24];
    strcpy(line, "NET CLOSE ");
    char num[8];
    DecNum(num, (u32)sock, 0);
    strcat(line, num);
    WifiSend(line);

    sh->net_rx_len[sock] = 0;
    sh->net_tx_len[sock] = 0;
    __asm volatile ("dmb" ::: "memory");
    sh->net_state[sock] = 0;
}
