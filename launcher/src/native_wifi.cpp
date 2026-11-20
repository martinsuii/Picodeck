// ****************************************************************************
//
//              PicoDeck — native WiFi module (core 1)
//
//   Detects the WiFi module image in the game flash region and starts it on
//   core 1. The module speaks the Companion Link protocol on TCP 2323 using
//   the Pico W's own radio.
//
//   The module is not a game: it exports a small header with its entry point
//   and a fixed shared RAM page (see wifi_module/wifi_shared.h).
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "../../wifi_module/wifi_shared.h"

static const wifi_hdr_t* g_hdr = NULL;
static wifi_shared_t*    g_sh  = NULL;
static Bool g_started = False;
static char g_ip[16];
static int g_bat_mv = -1;

static inline void mem_barrier(void)
{
    __asm volatile("dmb" ::: "memory");
}

// ---------------------------------------------------------------------------
// detection
// ---------------------------------------------------------------------------

static const wifi_hdr_t* FindModule(void)
{
    const u32 base = 0x10000100u;
    // The module image is ~300 KB; its header sits wherever the linker put
    // .rodata, sometimes past 64 KB. Scan the whole game area below the app
    // slot, not just the first 64 KB.
    for (u32 off = 0; off + sizeof(wifi_hdr_t) <= 0x50000u; off += 4)
    {
        const u32* p = (const u32*)(base + off);
        if (p[0] != WIFI_HDR_MAGIC) continue;
        const wifi_hdr_t* h = (const wifi_hdr_t*)p;
        u32 entry = h->entry & ~1u;
        if (h->version != WIFI_PROTO_VERSION) continue;
        if (entry < base || entry >= 0x100E0000u) continue;
        if (h->shared != WIFI_SHARED_ADDR) continue;
        return h;
    }
    return NULL;
}

Bool NativeWifiPresent(void)
{
    if (!g_hdr) g_hdr = FindModule();
    return g_hdr != NULL;
}

static Bool g_failed = False;
static Bool g_skipped = False;

Bool NativeWifiStart(void)
{
    if (g_started) return True;
    if (g_failed) return False;
    if (!NativeWifiPresent()) return False;

    g_sh = (wifi_shared_t*)WIFI_SHARED_ADDR;
    g_sh->stage = 0;
    mem_barrier();

    // Safe mode: put /no_wifi on the SD card. (No key-based safe mode: a
    // stuck button would otherwise silently disable WiFi on every boot.)
    if (DiskValid()) {
        sFile f;
        FileInit(&f);
        if (FileOpen(&f, "/no_wifi") || FileOpen(&f, "/NO_WIFI")) {
            FileClose(&f);
            g_skipped = True;
            return False;
        }
    }

    // Initialise the shared page so a stale module from a previous boot
    // cannot confuse us.
    g_sh->ready = 0;
    g_sh->stopped = 0;
    g_sh->paused = 0;
    g_sh->pause_req = 0;
    g_sh->shutdown_req = 0;
    g_sh->battery_mv = 0;
    g_sh->cmd_head = g_sh->cmd_tail = 0;
    g_sh->rep_head = g_sh->rep_tail = 0;
    g_sh->http_state = 0;
    g_sh->http_len = 0;
    g_sh->http_code = 0;
    g_sh->http_err = 0;
    g_sh->net_features = 0;
    for (int i = 0; i < WIFI_NET_SOCKETS; i++)
    {
        g_sh->net_state[i] = 0;
        g_sh->net_err[i] = 0;
        g_sh->net_rx_len[i] = 0;
        g_sh->net_tx_len[i] = 0;
        g_sh->net_events[i] = 0;
    }
    mem_barrier();

    // GP29 is the radio's SPI clock once the module runs; sample the battery
    // now and serve the cached value from then on.
    g_bat_mv = GetBatInt();

    Core1Exec((pCore1Fnc)(uintptr_t)g_hdr->entry);

    // The radio firmware download takes a moment.
    for (int i = 0; i < 500; i++)
    {
        if (g_sh->ready) break;
        WaitMs(10);
    }

    if (!g_sh->ready) {
        // Five seconds is well past radio firmware load. Stop core 1 and
        // leave the stage value for the status screen.
        Core1Reset();
        g_started = False;
        g_failed = True;
        return False;
    }

    g_started = True;
    return True;
}

Bool NativeWifiFailed(void)
{
    return g_failed && !g_skipped;
}

u32 NativeWifiFailStage(void)
{
    return g_sh ? (u32)g_sh->stage : 0;
}

Bool NativeWifiReady(void)
{
    return g_started && g_sh && g_sh->ready;
}

Bool NativeWifiLinkUp(void)
{
    return NativeWifiReady() && g_sh->link_up;
}

int NativeWifiRssi(void)
{
    return NativeWifiReady() ? (int)g_sh->rssi : 0;
}

const char* NativeWifiIp(void)
{
    g_ip[0] = 0;
    if (!NativeWifiReady()) return g_ip;

    // Copy under the seqlock so the UI never sees a half-written string.
    for (int tries = 0; tries < 4; tries++)
    {
        u32 s1 = g_sh->seq;
        if (s1 & 1u) continue;
        mem_barrier();
        int i = 0;
        for (; i < (int)sizeof(g_ip) - 1; i++)
        {
            char c = (char)g_sh->ip[i];
            g_ip[i] = c;
            if (!c) break;
        }
        g_ip[sizeof(g_ip) - 1] = 0;
        mem_barrier();
        if (g_sh->seq == s1) break;
    }
    return g_ip;
}

// ---------------------------------------------------------------------------
// line queue
// ---------------------------------------------------------------------------

void NativeWifiSend(const char* line)
{
    if (!NativeWifiReady() || !line) return;

    u32 head = g_sh->cmd_head;
    u32 tail = g_sh->cmd_tail;
    if (head - tail >= WIFI_CMD_SLOTS) return;

    char* slot = g_sh->cmd[head & (WIFI_CMD_SLOTS - 1)];
    int n = 0;
    while (line[n] && n < WIFI_LINE_MAX - 1) { slot[n] = line[n]; n++; }
    slot[n] = 0;
    mem_barrier();
    g_sh->cmd_head = head + 1;
}

Bool NativeWifiPopReply(char* out, int max)
{
    if (!NativeWifiReady() || !out || max <= 0) return False;

    u32 head = g_sh->rep_head;
    u32 tail = g_sh->rep_tail;
    if (head == tail) return False;

    const char* slot = g_sh->rep[tail & (WIFI_REP_SLOTS - 1)];
    int i = 0;
    while (slot[i] && i < max - 1) { out[i] = slot[i]; i++; }
    out[i] = 0;
    mem_barrier();
    g_sh->rep_tail = tail + 1;
    return True;
}

// ---------------------------------------------------------------------------
// flash safety
// ---------------------------------------------------------------------------

Bool NativeWifiPause(void)
{
    if (!g_started) return True;
    if (!NativeWifiReady()) { NativeWifiShutdown(); return True; }

    g_sh->pause_req = 1;
    mem_barrier();

    for (int i = 0; i < 2000; i++)
    {
        if (g_sh->paused) return True;
        WaitMs(1);
    }

    // Could not park: reset core 1 so a flash erase cannot fault it.
    g_sh->pause_req = 0;
    mem_barrier();
    NativeWifiShutdown();
    return True;
}

void NativeWifiResume(void)
{
    if (!g_started || !g_sh) return;
    g_sh->pause_req = 0;
    mem_barrier();
}

void NativeWifiShutdown(void)
{
    if (!g_started || !g_sh) return;

    // Wake a parked module so it can see the shutdown request.
    g_sh->pause_req = 0;
    mem_barrier();
    g_sh->shutdown_req = 1;
    mem_barrier();
    for (int i = 0; i < 300; i++)
    {
        if (g_sh->stopped) break;
        WaitMs(10);
    }

    Core1Reset();
    g_started = False;
}

// ---------------------------------------------------------------------------
// HTTP GET (used by the app store)
// ---------------------------------------------------------------------------

const char* NativeWifiHttpGet(const char* url, int* outLen)
{
    if (outLen) *outLen = 0;
    if (!NativeWifiReady() || !NativeWifiLinkUp() || !url) return NULL;
    if (StrLen(url) + 10 >= WIFI_LINE_MAX) return NULL;

    char line[WIFI_LINE_MAX];
    strcpy(line, "HTTP GET ");
    strcat(line, url);

    g_sh->http_len = 0;
    g_sh->http_err = 0;
    g_sh->http_code = 0;
    mem_barrier();
    g_sh->http_state = 0;

    NativeWifiSend(line);

    u32 start = Millis();
    while ((u32)(Millis() - start) < 15000)
    {
        if (g_sh->http_state == 2)
        {
            if (outLen) *outLen = (int)g_sh->http_len;
            return (const char*)WIFI_HTTP_BUF_ADDR;
        }
        if (g_sh->http_state == 3) return NULL;
        WaitMs(20);
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// /wifi.cnf credentials
// ---------------------------------------------------------------------------

void WifiSaveCreds(const char* ssid, const char* pass)
{
    DiskAutoMount();
    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, "/wifi.cnf")) return;
    FileWrite(&f, ssid, (u32)StrLen(ssid));
    FileWrite(&f, "\n", 1);
    FileWrite(&f, pass, (u32)StrLen(pass));
    FileWrite(&f, "\n", 1);
    FileClose(&f);

    u8 a = GetFileAttr("/wifi.cnf");
    SetFileAttr("/wifi.cnf", (u8)(a | ATTR_HID));
}

Bool WifiLoadCreds(char* ssid, int ssidlen, char* pass, int passlen)
{
    DiskAutoMount();
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, "/wifi.cnf")) return False;

    char buf[160];
    u32 n = FileRead(&f, buf, sizeof(buf) - 1);
    FileClose(&f);
    buf[n] = 0;

    int i = 0, k = 0;
    while (i < (int)n && buf[i] != '\n' && buf[i] != '\r')
    { if (k < ssidlen - 1) ssid[k++] = buf[i]; i++; }
    ssid[k] = 0;
    while (i < (int)n && (buf[i] == '\n' || buf[i] == '\r')) i++;
    k = 0;
    while (i < (int)n && buf[i] != '\n' && buf[i] != '\r')
    { if (k < passlen - 1) pass[k++] = buf[i]; i++; }
    pass[k] = 0;

    return ssid[0] != 0;
}

// ---------------------------------------------------------------------------
// SD seed (/wifi.cfg)
// ---------------------------------------------------------------------------

int LauncherBatMv(void)
{
    if (g_bat_mv >= 0 && NativeWifiReady()) return g_bat_mv;
    return GetBatInt();
}

void NativeWifiPublishBattery(void)
{
    if (!NativeWifiReady()) return;
    if (g_bat_mv < 0) g_bat_mv = GetBatInt();
    g_sh->battery_mv = g_bat_mv;
}

int NativeWifiScan(NativeWifiNet* out, int max, u32 timeout_ms)
{
    if (!NativeWifiReady() || !out || max <= 0) return -1;

    u32 gen = g_sh->scan_gen;
    NativeWifiSend("WIFI SCAN");

    u32 start = Millis();
    while ((u32)(Millis() - start) < timeout_ms)
    {
        if (g_sh->scan_gen != gen)
        {
            int n = (int)g_sh->scan_count;
            if (n > 16) n = 16;
            if (n > max) n = max;
            for (int i = 0; i < n; i++)
            {
                strncpy(out[i].ssid, (const char*)g_sh->scan_ssid[i], sizeof(out[i].ssid) - 1);
                out[i].ssid[sizeof(out[i].ssid) - 1] = 0;
                out[i].rssi = (int)g_sh->scan_rssi[i];
            }
            return n;
        }
        WaitMs(20);
    }
    return -2;
}

static void SendConnect(const char* ssid, const char* pass)
{
    char line[WIFI_LINE_MAX];
    strcpy(line, "WIFI CONNECT ");
    int at = (int)strlen(line);
    for (int i = 0; ssid[i] && at < WIFI_LINE_MAX - 2; i++) line[at++] = ssid[i];
    line[at++] = '|';
    for (int i = 0; pass[i] && at < WIFI_LINE_MAX - 1; i++) line[at++] = pass[i];
    line[at] = 0;
    NativeWifiSend(line);
}

void NativeWifiSeedFromSd(void)
{
    if (!NativeWifiReady() || !DiskValid()) return;

    char ssid[33];
    char pass[65];
    ssid[0] = 0;
    pass[0] = 0;

    // The ESP32 app's existing /wifi.cnf (two lines) is authoritative.
    if (!WifiLoadCreds(ssid, sizeof(ssid), pass, sizeof(pass)) || !ssid[0])
    {
        // Fallback: /wifi.cfg with key=value lines.
        sFile f;
        FileInit(&f);
        if (!FileOpen(&f, "/wifi.cfg") && !FileOpen(&f, "/WIFI.CFG")) return;

        char buf[256];
        u32 n = FileRead(&f, buf, sizeof(buf) - 1);
        FileClose(&f);
        buf[n] = 0;

        char* p = buf;
        while (*p)
        {
            char* eol = p;
            while (*eol && *eol != '\n' && *eol != '\r') eol++;
            char save = *eol;
            *eol = 0;
            if (strncmp(p, "ssid=", 5) == 0)
            {
                strncpy(ssid, p + 5, sizeof(ssid) - 1);
                ssid[sizeof(ssid) - 1] = 0;
            }
            else if (strncmp(p, "pass=", 5) == 0)
            {
                strncpy(pass, p + 5, sizeof(pass) - 1);
                pass[sizeof(pass) - 1] = 0;
            }
            *eol = save;
            p = eol;
            while (*p == '\r' || *p == '\n') p++;
        }
    }

    if (ssid[0]) SendConnect(ssid, pass);
}

// ---------------------------------------------------------------------------
// UI helper
// ---------------------------------------------------------------------------

void NativeWifiStatusText(char* out, int max)
{
    if (!out || max <= 0) return;

    if (!NativeWifiPresent())
    {
        strncpy(out, "WiFi: no module", max - 1);
        out[max - 1] = 0;
        return;
    }

    if (!NativeWifiReady())
    {
        char buf[40];
        unsigned stage = g_sh ? g_sh->stage : 0;
        if (stage == 0)
            snprintf(buf, sizeof(buf), "WiFi: not started");
        else if (stage & 0x80000000u)
            snprintf(buf, sizeof(buf), "WiFi: fault (stage %u)", stage & 0xFFFFu);
        else
            snprintf(buf, sizeof(buf), "WiFi: starting (stage %u)", stage);
        strncpy(out, buf, max - 1);
        out[max - 1] = 0;
        return;
    }

    if (NativeWifiLinkUp())
    {
        char buf[40];
        strcpy(buf, "WiFi: ");
        strncat(buf, NativeWifiIp(), sizeof(buf) - strlen(buf) - 1);
        strncpy(out, buf, max - 1);
        out[max - 1] = 0;
    }
    else
    {
        strncpy(out, "WiFi: connecting", max - 1);
        out[max - 1] = 0;
    }
}
