// ****************************************************************************
//
//                  PicoDeck — ESP32 bridge
//
//   Talks to the bridge firmware (esp32/wifi_bridge/) over UART0 (GP0/GP1,
//   115200) and exposes Wi-Fi scan/connect/status, BLE scan, LED control and
//   device info.
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "esp32_bridge.h"
#include "keyboard.h"
#include "qrcode.h"

#define ESP_UART     0
#define ESP_TX_PIN   0     // PicoPad GP0 -> ESP32 RX0
#define ESP_RX_PIN   1     // PicoPad GP1 <- ESP32 TX0
#define ESP_BAUD     115200

#define ESP_MAX      24
#define ESP_NAME_LEN 33

static char g_names[ESP_MAX][ESP_NAME_LEN];
static int  g_rssi[ESP_MAX];
static int  g_count;

// Persistent connection + cached weather/time.
static Bool g_esp_conn = False;
static Bool g_uart_inited = False;
static char g_weather[32] = "";
static u32  g_weather_ms = 0;

static Bool g_time_valid = False;
static long g_time_sec = 0;      // local seconds since midnight
static u32  g_time_ms = 0;

#define WEATHER_TTL_MS  (15u * 60u * 1000u)
#define TIME_TTL_MS     (5u * 60u * 1000u)

static Bool EspStrEq(const char* a, const char* b)
{
    while (*a && *b) { if (*a++ != *b++) return False; }
    return *a == 0 && *b == 0;
}

static int EspParseInt(const char* s, int* out)
{
    int v = 0, sign = 1, got = 0;
    if (*s == '-') { sign = -1; s++; }
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; got++; }
    *out = v * sign;
    return got;
}

// ---------------------------------------------------------------------------
// UART
// ---------------------------------------------------------------------------

static void EspUartInit(void)
{
    if (g_uart_inited) return;
    UART_Init(ESP_UART, ESP_BAUD, 8, UART_PARITY_NONE, 1, False);
    GPIO_Fnc(ESP_TX_PIN, GPIO_FNC_UART);
    GPIO_Fnc(ESP_RX_PIN, GPIO_FNC_UART);
    GPIO_PullUp(ESP_RX_PIN);
    while (UART_RecvReady(ESP_UART)) UART_RecvChar(ESP_UART);
    g_uart_inited = True;
}

// Forward declarations (defined below).
static void EspSend(const char* s);
static int  EspReadLine(char* buf, int maxlen, u32 timeout_ms);
static void EspParseTime(const char* line, long* epoch, int* hh, int* mm, int* ss);

Bool EspConnected(void) { return g_esp_conn; }

const char* EspWeatherText(void) { return g_weather; }

Bool EspWeatherStale(void)
{
    return (g_weather[0] == 0) ||
           ((u32)(Millis() - g_weather_ms) >= WEATHER_TTL_MS);
}

// Ping the bridge; drops the connection state if it stops answering.
void EspPoll(void)
{
    if (!g_esp_conn) return;

    while (UART_RecvReady(ESP_UART)) UART_RecvChar(ESP_UART);
    EspSend("PING");
    char line[32];
    u32 start = Millis();
    while ((u32)(Millis() - start) < 500)
    {
        int n = EspReadLine(line, sizeof(line), 300);
        if (n < 0) break;
        if (EspStrEq(line, "PONG")) return;
    }
    g_esp_conn = False;
}

// Query the bridge for the list of announced ESP-NOW devices. Returns the
// number of devices filled into `out` (0 if none / on failure).
int EspListDevices(EspDevice* out, int max)
{
    EspUartInit();
    while (UART_RecvReady(ESP_UART)) UART_RecvChar(ESP_UART);
    EspSend("ESPLIST");

    int count = 0;
    char line[64];
    u32 start = Millis();
    while (count < max && (u32)(Millis() - start) < 3000)
    {
        int n = EspReadLine(line, sizeof(line), 500);
        if (n < 0) break;
        if (strncmp(line, "END", 3) == 0) break;
        if (strncmp(line, "DEV ", 4) != 0) continue;

        // "DEV <mac> <type> <name> <online>"
        char* p = line + 4;
        // mac
        char* sp = strchr(p, ' '); if (!sp) continue;
        *sp = 0; strncpy(out[count].mac, p, ESP_DEV_MAC_LEN - 1); out[count].mac[ESP_DEV_MAC_LEN-1] = 0; p = sp + 1;
        // type
        sp = strchr(p, ' '); if (!sp) continue;
        *sp = 0; out[count].type = 0; EspParseInt(p, &out[count].type); p = sp + 1;
        // name
        sp = strchr(p, ' '); if (!sp) continue;
        *sp = 0; strncpy(out[count].name, p, ESP_DEV_NAME_LEN - 1); out[count].name[ESP_DEV_NAME_LEN-1] = 0; p = sp + 1;
        // online
        int on = 0; EspParseInt(p, &on);
        out[count].online = on ? True : False;

        count++;
    }
    return count;
}

// Send a DuckyScript payload to a specific device MAC via ESP-NOW (through the
// bridge). Returns True once the bridge reports the send completed.
Bool EspNowSendTo(const char* mac, const char* script, u32 len)
{
    EspUartInit();
    while (UART_RecvReady(ESP_UART)) UART_RecvChar(ESP_UART);

    char cmd[64];
    strcpy(cmd, "ESPNOW "); strcat(cmd, mac); strcat(cmd, " ");
    char num[8];
    DecNum(num, (s32)len, 0);
    strcat(cmd, num);
    EspSend(cmd);

    char line[48];
    int n = EspReadLine(line, sizeof(line), 5000);
    if (n < 0 || strncmp(line, "GO", 2) != 0) return False;

    u32 total = 0;
    while (total < len)
    {
        u32 want = len - total;
        if (want > 512) want = 512;
        for (u32 i = 0; i < want; i++)
            UART_SendCharWait(ESP_UART, script[total + i]);
        total += want;

        n = EspReadLine(line, sizeof(line), 5000);
        if (n < 0 || strncmp(line, "OK", 2) != 0) return False;
    }

    n = EspReadLine(line, sizeof(line), 5000);
    return (n > 0 && strncmp(line, "DONE", 4) == 0);
}

static const char* WmoText(int code)
{
    if (code == 0) return "Clear";
    if (code <= 3) return "Cloudy";
    if (code == 45 || code == 48) return "Fog";
    if (code >= 51 && code <= 67) return "Rain";
    if (code >= 71 && code <= 77) return "Snow";
    if (code >= 80 && code <= 82) return "Showers";
    if (code >= 95) return "Storm";
    return "?";
}

void EspWeatherRefresh(void)
{
    if (!g_esp_conn) return;

    while (UART_RecvReady(ESP_UART)) UART_RecvChar(ESP_UART);
    EspSend("WEATHER");

    char line[48];
    int n = EspReadLine(line, sizeof(line), 8000);
    if (n <= 0) return;
    if (!(line[0]=='W' && line[1]=='E' && line[2]=='A' && line[3]=='T')) return;

    // "WEATHER <temp> <code>"
    const char* p = line + 8;
    while (*p == ' ') p++;
    int temp10 = 0, sign = 1, frac = 0, seen = 0;
    if (*p == '-') { sign = -1; p++; }
    while (*p >= '0' && *p <= '9') { temp10 = temp10 * 10 + (*p - '0'); p++; }
    if (*p == '.') { p++; if (*p >= '0' && *p <= '9') { frac = *p - '0'; p++; } }
    seen = 1;
    while (*p == ' ') p++;
    int code = 0;
    while (*p >= '0' && *p <= '9') { code = code * 10 + (*p - '0'); p++; }

    if (!seen) return;

    char b[32];
    int i = 0;
    if (sign < 0) b[i++] = '-';
    if (temp10 >= 10) { b[i++] = (char)('0' + (temp10 / 10) % 10); }
    b[i++] = (char)('0' + temp10 % 10);
    b[i++] = '.';
    b[i++] = (char)('0' + frac);
    b[i++] = 'C';
    b[i++] = ' ';
    const char* txt = WmoText(code);
    int k = 0;
    while (txt[k] && i < 31) b[i++] = txt[k++];
    b[i] = 0;

    strcpy(g_weather, b);
    g_weather_ms = Millis();
}

void EspTimeRefresh(void)
{
    if (!g_esp_conn) return;

    while (UART_RecvReady(ESP_UART)) UART_RecvChar(ESP_UART);
    EspSend("TIME");

    char line[64];
    int n = EspReadLine(line, sizeof(line), 8000);
    if (n <= 0) return;
    if (!(line[0]=='T' && line[1]=='I' && line[2]=='M' && line[3]=='E')) return;

    long epoch; int hh, mm, ss;
    EspParseTime(line, &epoch, &hh, &mm, &ss);
    if (epoch < 1000000000) return;

    g_time_sec = (long)hh * 3600 + (long)mm * 60 + ss;
    g_time_ms = Millis();
    g_time_valid = True;
}

Bool EspTimeStale(void)
{
    return !g_time_valid || ((u32)(Millis() - g_time_ms) >= TIME_TTL_MS);
}

Bool EspTimeText(char* out, int outlen)
{
    if (!g_time_valid || outlen < 9) return False;

    long now = g_time_sec + (long)((Millis() - g_time_ms) / 1000u);
    int h = (int)((now / 3600) % 24);
    int m = (int)((now / 60) % 60);
    int s = (int)(now % 60);

    out[0] = (char)('0' + h / 10); out[1] = (char)('0' + h % 10); out[2] = ':';
    out[3] = (char)('0' + m / 10); out[4] = (char)('0' + m % 10); out[5] = ':';
    out[6] = (char)('0' + s / 10); out[7] = (char)('0' + s % 10); out[8] = 0;
    return True;
}

static void EspSend(const char* s)
{
    while (*s) UART_SendCharWait(ESP_UART, *s++);
    UART_SendCharWait(ESP_UART, '\n');
}

static int EspGetc(u32 timeout_ms)
{
    u32 start = Millis();
    while (!UART_RecvReady(ESP_UART))
    {
        if ((u32)(Millis() - start) > timeout_ms) return -1;
        WaitMs(1);
    }
    return (u8)UART_RecvChar(ESP_UART);
}

static int EspReadLine(char* buf, int maxlen, u32 timeout_ms)
{
    int n = 0;
    u32 start = Millis();
    while (1)
    {
        int c = EspGetc(timeout_ms);
        if (c < 0) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < maxlen - 1) buf[n++] = (char)c;
        if ((u32)(Millis() - start) > timeout_ms) break;
    }
    buf[n] = 0;
    return n;
}

static Bool EspHandshake(void)
{
    for (int i = 0; i < 3; i++)
    {
        EspSend("PING");
        char line[48];
        u32 start = Millis();
        while ((u32)(Millis() - start) < 1500)
        {
            int n = EspReadLine(line, sizeof(line), 400);
            if (n < 0) break;
            if (EspStrEq(line, "PONG")) return True;
        }
        WaitMs(200);
    }
    return False;
}

// Read until "OK" (True) or "ERR..." / timeout (False). Fills err with the
// error line if any.
static Bool EspWaitOk(char* err, int errlen, u32 timeout)
{
    char line[64];
    u32 start = Millis();
    if (err && errlen) err[0] = 0;
    while ((u32)(Millis() - start) < timeout)
    {
        int n = EspReadLine(line, sizeof(line), timeout);
        if (n < 0) return False;
        if (EspStrEq(line, "OK")) return True;
        if (line[0] == 'E' && line[1] == 'R' && line[2] == 'R')
        {
            if (err && errlen) { int i = 0; const char* p = line + 4; while (p[i] && i < errlen-1) { err[i]=p[i]; i++; } err[i]=0; }
            return False;
        }
    }
    return False;
}

// Read a scan reply ("SCAN n" / "BLE n" then entries then "END").
static Bool EspReadScan(u32 timeout)
{
    g_count = 0;
    char line[96];
    int expect = -1;
    u32 start = Millis();

    while ((u32)(Millis() - start) < timeout)
    {
        int n = EspReadLine(line, sizeof(line), 3000);
        if (n < 0) break;
        if (n == 0) continue;

        Bool hdr = False;
        if (line[0]=='S' && line[1]=='C' && line[2]=='A' &&
            line[3]=='N' && line[4]==' ') hdr = True;
        if ((line[0]=='B'||line[0]=='b') && (line[1]=='L'||line[1]=='l') &&
            (line[2]=='E'||line[2]=='e') && line[3]==' ') hdr = True;
        if (hdr)
        {
            const char* p = line;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            int v;
            if (EspParseInt(p, &v)) expect = v;
            continue;
        }
        if (EspStrEq(line, "END")) return True;

        if (expect >= 0 && g_count < ESP_MAX)
        {
            int rssi;
            if (EspParseInt(line, &rssi))
            {
                const char* p = line;
                if (*p == '-') p++;
                while (*p >= '0' && *p <= '9') p++;
                while (*p == ' ') p++;
                int k = 0;
                while (p[k] && k < ESP_NAME_LEN - 1) { g_names[g_count][k] = p[k]; k++; }
                g_names[g_count][k] = 0;
                g_rssi[g_count] = rssi;
                g_count++;
            }
        }
    }
    return g_count > 0;
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

static void EspMsg(const char* title, const char* l1, const char* l2)
{
    DrawClear();
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 30, COL_YELLOW);
    if (l1) DrawText(l1, 20, 100, COL_WHITE);
    if (l2) DrawText(l2, 20, 120, COL_LTGRAY);
    DrawText("Press X to return", 20, 210, COL_GRAY);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

// Returns the selected index on A, or -1 on X.
static int EspShowList(const char* title, const char* hint, Bool show_rssi)
{
    int selected = 0;
    int top = 0;
    Bool need_redraw = True;

    while (1)
    {
        if (selected < top) top = selected;
        if (selected >= top + 15) top = selected - 14;
        if (top < 0) top = 0;

        if (need_redraw)
        {
            DrawClear();
            DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 10,
                      COL_YELLOW);
            DrawRect(10, 30, WIDTH - 20, 1, COL_GRAY);

            if (g_count == 0)
                DrawText("Nothing found.", 20, 50, COL_GRAY);

            for (int i = 0; i < 15; i++)
            {
                int idx = top + i;
                if (idx >= g_count) break;
                int y = 40 + i * 12;
                Bool sel = (idx == selected);
                if (sel)
                    DrawRect(8, y - 1, WIDTH - 16, FONTH + 2, RGBTO16(40, 40, 0));

                DrawText(g_names[idx], 16, y, sel ? COL_YELLOW : COL_WHITE);
                if (show_rssi)
                {
                    char b[12];
                    DecNum(b, g_rssi[idx], 0);
                    strcat(b, "dBm");
                    DrawText(b, WIDTH - 12 - (int)StrLen(b) * FONTW, y,
                             sel ? COL_YELLOW : COL_LTGRAY);
                }
            }

            DrawText(hint, 10, 226, COL_LTGRAY);
            DispUpdate();
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }
        switch (k)
        {
        case KEY_UP:   if (selected > 0) { selected--; need_redraw = True; } break;
        case KEY_DOWN: if (selected < g_count - 1) { selected++; need_redraw = True; } break;
        case KEY_A:    return (g_count > 0) ? selected : -1;
        case KEY_X:    return -1;
        default: break;
        }
    }
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Verified Wi-Fi credentials (/wifi.cnf, hidden)
// ---------------------------------------------------------------------------

#define WIFI_CNF "/wifi.cnf"

void WifiSaveCreds(const char* ssid, const char* pass)
{
    DiskAutoMount();
    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, WIFI_CNF)) return;
    FileWrite(&f, ssid, (u32)StrLen(ssid));
    FileWrite(&f, "\n", 1);
    FileWrite(&f, pass, (u32)StrLen(pass));
    FileWrite(&f, "\n", 1);
    FileClose(&f);

    // Mark hidden.
    u8 a = GetFileAttr(WIFI_CNF);
    SetFileAttr(WIFI_CNF, (u8)(a | ATTR_HID));
}

Bool WifiLoadCreds(char* ssid, int ssidlen, char* pass, int passlen)
{
    DiskAutoMount();
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, WIFI_CNF)) return False;

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

// Send WIFI CONNECT; optionally persist the credentials on success.
static void EspDoConnect(const char* ssid, const char* pass, Bool save)
{
    char cmd[128];
    strcpy(cmd, "WIFI CONNECT ");
    strcat(cmd, ssid);
    strcat(cmd, "|");
    strcat(cmd, pass);

    DrawClear();
    DrawText2("Wi-Fi Connect", (WIDTH - 13 * FONTW * 2) / 2, 80, COL_YELLOW);
    char b[48];
    strcpy(b, "SSID: ");
    strcat(b, ssid);
    DrawText(b, 20, 120, COL_LTGRAY);
    DrawText("Connecting...", 20, 140, COL_LTGRAY);
    DispUpdate();

    EspSend(cmd);
    char err[48];
    if (EspWaitOk(err, sizeof(err), 20000))
    {
        if (save) WifiSaveCreds(ssid, pass);
        EspMsg("Wi-Fi Connect", "Connected!", ssid);
    }
    else
    {
        EspMsg("Wi-Fi Connect", "Failed.", err[0] ? err : "timeout");
    }
}

// Prompt for a password and connect to `ssid`.
static void EspConnectTo(const char* ssid)
{
    char pass[64];
    pass[0] = 0;
    if (!KeyboardInput("Password", pass, sizeof(pass))) return;
    EspDoConnect(ssid, pass, True);
}

// Reconnect using the saved /wifi.cnf credentials.
static void EspConnectSaved(void)
{
    char ssid[33], pass[64];
    if (!WifiLoadCreds(ssid, sizeof(ssid), pass, sizeof(pass)) || !ssid[0])
    {
        EspMsg("Wi-Fi", "No saved network.", "Connect once to save it.");
        return;
    }
    EspDoConnect(ssid, pass, False);
}

static void EspWifiScan(void)
{
    DrawClear();
    DrawText2("Wi-Fi Scan", (WIDTH - 10 * FONTW * 2) / 2, 90, COL_YELLOW);
    DrawText("Scanning...", 20, 140, COL_LTGRAY);
    DispUpdate();

    EspSend("WIFI SCAN");
    EspReadScan(20000);

    // Select a network to connect to it.
    int idx = EspShowList("Wi-Fi networks", "A: Connect   X: Back", True);
    if (idx >= 0 && g_names[idx][0]) EspConnectTo(g_names[idx]);
}

static void EspWifiConnect(void)
{
    char ssid[33];
    ssid[0] = 0;
    if (!KeyboardInput("SSID", ssid, sizeof(ssid)) || !ssid[0]) return;
    EspConnectTo(ssid);
}

static void EspWifiStatus(void)
{
    EspSend("WIFI STATUS");
    char line[96];
    int n = EspReadLine(line, sizeof(line), 3000);
    if (n <= 0) { EspMsg("Wi-Fi Status", "No response.", NULL); return; }

    // "WIFI <state> <ssid> <ip> <rssi>"
    char state[20] = "", ssid[33] = "", ip[20] = "", rssi[12] = "";
    const char* p = line;
    if (p[0]=='W'&&p[1]=='I'&&p[2]=='F'&&p[3]=='I'&&p[4]==' ') p += 5;

    // tokenize 4 fields
    char* fields[4] = { state, ssid, ip, rssi };
    int fs[4] = { 20, 33, 20, 12 };
    for (int f = 0; f < 4; f++)
    {
        int i = 0;
        while (*p && *p != ' ' && i < fs[f]-1) fields[f][i++] = *p++;
        fields[f][i] = 0;
        while (*p == ' ') p++;
    }

    DrawClear();
    DrawText2("Wi-Fi Status", (WIDTH - 12 * FONTW * 2) / 2, 30, COL_YELLOW);
    DrawText("State:", 20, 90, COL_LTGRAY);
    DrawText(state, 120, 90, COL_WHITE);
    DrawText("SSID:", 20, 110, COL_LTGRAY);
    DrawText(ssid, 120, 110, COL_WHITE);
    DrawText("IP:", 20, 130, COL_LTGRAY);
    DrawText(ip, 120, 130, COL_WHITE);
    DrawText("RSSI:", 20, 150, COL_LTGRAY);
    DrawText(rssi, 120, 150, COL_WHITE);
    DrawText("Press X to return", 20, 210, COL_GRAY);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

static void EspWifiDisconnect(void)
{
    EspSend("WIFI DISCONNECT");
    char err[48];
    if (EspWaitOk(err, sizeof(err), 3000))
        EspMsg("Wi-Fi", "Disconnected.", NULL);
    else
        EspMsg("Wi-Fi", "Failed.", err[0] ? err : "timeout");
}

static void EspBleScan(void)
{
    DrawClear();
    DrawText2("BLE Scan", (WIDTH - 8 * FONTW * 2) / 2, 90, COL_YELLOW);
    DrawText("Scanning (5s)...", 20, 140, COL_LTGRAY);
    DispUpdate();

    EspSend("BLE SCAN");
    EspReadScan(15000);
    EspShowList("BLE devices", "X: Back", True);
}

static void EspLedCmd(const char* arg, const char* okmsg)
{
    char cmd[32];
    strcpy(cmd, "LED ");
    strcat(cmd, arg);
    EspSend(cmd);
    char err[48];
    if (EspWaitOk(err, sizeof(err), 3000)) EspMsg("ESP32 LED", okmsg, NULL);
    else EspMsg("ESP32 LED", "Failed.", err[0] ? err : "timeout");
}

static void EspLedOn(void)     { EspLedCmd("ON", "LED on"); }
static void EspLedOff(void)    { EspLedCmd("OFF", "LED off"); }
static void EspLedBlink(void)  { EspLedCmd("BLINK 500", "LED blinking"); }

static void EspInfo(void)
{
    EspSend("INFO");
    char line[96];
    int n = EspReadLine(line, sizeof(line), 3000);
    if (n <= 0) { EspMsg("ESP Info", "No response.", NULL); return; }
    EspMsg("ESP Info", line, NULL);
}

// ---------------------------------------------------------------------------
// Wi-Fi AP
// ---------------------------------------------------------------------------

static void EspWifiAp(void)
{
    char ssid[33]; ssid[0] = 0;
    char pass[64]; pass[0] = 0;
    if (!KeyboardInput("AP SSID", ssid, sizeof(ssid)) || !ssid[0]) return;
    if (!KeyboardInput("AP Password (8+)", pass, sizeof(pass))) return;

    char cmd[128];
    strcpy(cmd, "AP START "); strcat(cmd, ssid); strcat(cmd, "|"); strcat(cmd, pass);
    EspSend(cmd);

    char line[64];
    int n = EspReadLine(line, sizeof(line), 5000);
    char ip[32] = "";
    if (n > 0 && line[0] == 'O' && line[1] == 'K')
    {
        const char* p = line; while (*p && *p != ' ') p++; while (*p == ' ') p++;
        int i = 0; while (p[i] && i < 31) { ip[i] = p[i]; i++; }
        ip[i] = 0;
    }

    // Wi-Fi QR string so a phone can scan and join.
    char wf[160];
    strcpy(wf, "WIFI:T:");
    if (StrLen(pass) >= 8)
    {
        strcat(wf, "WPA;S:"); strcat(wf, ssid);
        strcat(wf, ";P:"); strcat(wf, pass); strcat(wf, ";;");
    }
    else
    {
        strcat(wf, "nopass;S:"); strcat(wf, ssid); strcat(wf, ";;");
    }

    DrawClear();
    DrawText("wifi ap", 8, 8, COL_WHITE);
    DrawRect(8, 22, WIDTH - 16, 1, COL_GRAY);

    const u8* matrix; int size;
    if (QrEncode(wf, &matrix, &size))
    {
        int scale = 4;
        int px = size * scale;
        if (px > 150) { scale = 3; px = size * scale; }
        int x0 = (WIDTH - px) / 2;
        int y0 = 34;
        for (int r = 0; r < size; r++)
            for (int c = 0; c < size; c++)
                if (matrix[r * size + c])
                    DrawRect(x0 + c * scale, y0 + r * scale, scale, scale, COL_WHITE);

        DrawText(ssid, 8, y0 + px + 6, COL_WHITE);
        if (ip[0])
        {
            char b[48]; strcpy(b, "ip: "); strcat(b, ip);
            DrawText(b, 8, y0 + px + 20, COL_GRAY);
        }
        DrawText("scan to connect", 8, y0 + px + 34, COL_GRAY);
    }
    else
    {
        DrawText("failed to build qr", 8, 60, COL_WHITE);
        if (ip[0]) DrawText(ip, 8, 76, COL_GRAY);
    }

    DrawText("x: back", 8, HEIGHT - 14, COL_GRAY);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

// ---------------------------------------------------------------------------
// Web server (page uploaded from /web on the SD card)
// ---------------------------------------------------------------------------

static void EspWebServer(void)
{
    char path[64]; path[0] = 0;
    const char* dirs[2] = { "/web", "/Web" };
    for (int d = 0; d < 2 && !path[0]; d++)
    {
        sFile find; sFileInfo info;
        FileInit(&find);
        if (!FindOpen(&find, dirs[d])) continue;
        while (FindNext(&find, &info, ATTR_FILE_MASK, "*"))
        {
            if ((info.attr & ATTR_DIR) || info.namelen == 0) continue;
            int n = 0; const char* s = dirs[d];
            while (s[n] && n < 58) { path[n] = s[n]; n++; }
            path[n++] = '/';
            for (int i = 0; i < info.namelen && n < 62; i++) path[n++] = info.name[i];
            path[n] = 0;
            break;
        }
        FindClose(&find);
    }
    if (!path[0]) { EspMsg("Web Server", "No file in /web", "Add an .htm page"); return; }

    sFile f; FileInit(&f);
    if (!FileOpen(&f, path)) { EspMsg("Web Server", "Cannot open", path); return; }
    static u8 buf[4096];
    u32 len = FileRead(&f, buf, sizeof(buf));
    FileClose(&f);
    if (len == 0) { EspMsg("Web Server", "Empty file", path); return; }

    DrawClear();
    DrawText2("Web Server", (WIDTH - 10 * FONTW * 2) / 2, 90, COL_YELLOW);
    DrawText("Uploading page...", 20, 140, COL_LTGRAY);
    DispUpdate();

    char cmd[32];
    strcpy(cmd, "WEB BEGIN ");
    char num[8]; DecNum(num, len, 0); strcat(cmd, num);
    EspSend(cmd);
    for (u32 i = 0; i < len; i++) UART_SendCharWait(ESP_UART, (char)buf[i]);

    char line[64];
    EspReadLine(line, sizeof(line), 5000);

    EspSend("WEB START");
    int n = EspReadLine(line, sizeof(line), 5000);
    if (n > 0 && line[0] == 'O' && line[1] == 'K')
    {
        const char* p = line; while (*p && *p != ' ') p++; while (*p == ' ') p++;
        char b[48]; strcpy(b, "http://"); strcat(b, p);
        EspMsg("Web Server", "Serving on", b);
    }
    else EspMsg("Web Server", "Failed.", line);
}

// ---------------------------------------------------------------------------
// Network diagnostics
// ---------------------------------------------------------------------------

static void EspDns(void)
{
    char host[64]; host[0] = 0;
    if (!KeyboardInput("Host", host, sizeof(host)) || !host[0]) return;

    char cmd[80]; strcpy(cmd, "DNS "); strcat(cmd, host);
    EspSend(cmd);

    char line[80];
    int n = EspReadLine(line, sizeof(line), 8000);
    if (n > 0 && line[0]=='D' && line[1]=='N' && line[2]=='S')
    {
        const char* p = line; while (*p && *p != ' ') p++; while (*p == ' ') p++;
        EspMsg("DNS Lookup", host, p);
    }
    else EspMsg("DNS Lookup", "Failed.", line);
}

static void EspTcp(void)
{
    char host[64]; host[0] = 0;
    char port[8];  port[0] = 0;
    if (!KeyboardInput("Host", host, sizeof(host)) || !host[0]) return;
    if (!KeyboardInput("Port", port, sizeof(port)) || !port[0]) return;

    char cmd[96]; strcpy(cmd, "TCP "); strcat(cmd, host);
    strcat(cmd, " "); strcat(cmd, port);
    EspSend(cmd);

    char line[64];
    int n = EspReadLine(line, sizeof(line), 8000);
    EspMsg("TCP Test", (n > 0) ? line : "No response.", NULL);
}

// ---------------------------------------------------------------------------
// Nmap (mini network scanner)
// ---------------------------------------------------------------------------

static Bool EspStartsWith(const char* s, const char* prefix)
{
    while (*prefix) { if (*s++ != *prefix++) return False; }
    return True;
}

// Collect "<prefix><value>" lines into g_names until "END".
static void EspCollectLines(const char* prefix, u32 timeout)
{
    g_count = 0;
    char line[64];
    u32 start = Millis();
    while ((u32)(Millis() - start) < timeout)
    {
        int n = EspReadLine(line, sizeof(line), 3000);
        if (n < 0) break;
        if (EspStrEq(line, "END")) break;
        if (!EspStartsWith(line, prefix)) continue;

        const char* p = line + StrLen(prefix);
        if (g_count < ESP_MAX)
        {
            int i = 0;
            while (p[i] && i < ESP_NAME_LEN - 1) { g_names[g_count][i] = p[i]; i++; }
            g_names[g_count][i] = 0;
            g_rssi[g_count] = 0;
            g_count++;
        }
    }
}

static void EspNmapHosts(void)
{
    DrawClear();
    DrawText2("Nmap Hosts", (WIDTH - 10 * FONTW * 2) / 2, 80, COL_YELLOW);
    DrawText("Scanning subnet...", 20, 130, COL_LTGRAY);
    DrawText("May take ~15 seconds.", 20, 148, COL_GRAY);
    DispUpdate();

    EspSend("NMAP HOSTS");
    EspCollectLines("HOST ", 60000);
    EspShowList("Live hosts", "X: Back", False);
}

static void EspNmapPorts(void)
{
    char ip[20]; ip[0] = 0;
    if (!KeyboardInput("Target IP", ip, sizeof(ip)) || !ip[0]) return;

    char cmd[48];
    strcpy(cmd, "NMAP PORTS "); strcat(cmd, ip);

    DrawClear();
    DrawText2("Nmap Ports", (WIDTH - 10 * FONTW * 2) / 2, 80, COL_YELLOW);
    DrawText(ip, 20, 130, COL_WHITE);
    DrawText("Scanning ports...", 20, 150, COL_LTGRAY);
    DispUpdate();

    EspSend(cmd);
    EspCollectLines("PORT ", 30000);
    EspShowList("Open ports", "X: Back", False);
}

// ---------------------------------------------------------------------------
// Web GPIO (control the PicoPad pins from a browser via the ESP32)
// ---------------------------------------------------------------------------

static const int WebPins[] = { 0, 1, 14, 22, 26, 27, 28 };
static const char* const WebPinLabels[] = {
    "GP0", "GP1", "GP14", "LED", "GP26", "GP27", "GP28"
};
#define WEB_PIN_N 7

static void EspWebGpio(void)
{
    EspSend("WEB CTRL");
    char line[64];
    int n = EspReadLine(line, sizeof(line), 5000);

    char ip[32] = "";
    if (n > 0 && line[0] == 'O' && line[1] == 'K')
    {
        const char* p = line;
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        int i = 0;
        while (p[i] && i < 31) { ip[i] = p[i]; i++; }
        ip[i] = 0;
    }

    static char g_web_last[40] = "";
    u32 last = 0;
    Bool need_redraw = True;

    while (1)
    {
        // Poll for incoming SETGPIO commands.
        int m = EspReadLine(line, sizeof(line), 20);
        if (m > 0 && EspStartsWith(line, "SETGPIO "))
        {
            const char* p = line + 8;
            int pin = 0;
            while (*p >= '0' && *p <= '9') { pin = pin * 10 + (*p - '0'); p++; }
            while (*p == ' ') p++;
            int val = 0;
            while (*p >= '0' && *p <= '9') { val = val * 10 + (*p - '0'); p++; }

            if (pin <= 29)
            {
                GPIO_Init(pin);
                GPIO_DirOut(pin);
                if (val) GPIO_Out1(pin); else GPIO_Out0(pin);

                strcpy(g_web_last, "");
                const char* lbl = "GP";
                for (int i = 0; i < WEB_PIN_N; i++)
                    if (WebPins[i] == pin) lbl = WebPinLabels[i];
                strcat(g_web_last, lbl);
                strcat(g_web_last, " = ");
                char num[8]; DecNum(num, val, 0); strcat(g_web_last, num);
            }
            need_redraw = True;
        }

        if (need_redraw || (u32)(Millis() - last) >= 500)
        {
            last = Millis();
            need_redraw = False;

            DrawClear();
            DrawText2("Web GPIO", (WIDTH - 8 * FONTW * 2) / 2, 10, COL_YELLOW);
            char b[48];
            strcpy(b, "http://"); strcat(b, ip);
            DrawText(b, 10, 38, COL_LTGRAY);
            DrawText("Open in a browser.", 10, 52, COL_WHITE);

            int y = 76;
            for (int i = 0; i < WEB_PIN_N; i++)
            {
                int st = GPIO_GetOut(WebPins[i]);
                DrawText(WebPinLabels[i], 14, y, COL_LTGRAY);
                DrawText(st ? "ON" : "OFF", 110, y,
                         st ? COL_GREEN : COL_GRAY);
                y += 16;
            }

            if (g_web_last[0])
                DrawText(g_web_last, 10, 196, COL_YELLOW);

            DrawText("X: Back", 10, 224, COL_LTGRAY);
            DispUpdate();
        }

        if (KeyGet() == KEY_X) break;
        WaitMs(10);
    }

    EspSend("WEB STOP");
    EspReadLine(line, sizeof(line), 2000);
}

// ---------------------------------------------------------------------------
// NTP clock
// ---------------------------------------------------------------------------

// Parse "TIME <epoch> <hh> <mm> <ss>" (local Prague time).
static void EspParseTime(const char* line, long* epoch, int* hh, int* mm, int* ss)
{
    const char* p = line;
    while (*p && *p != ' ') p++;      // skip "TIME"
    while (*p == ' ') p++;
    *epoch = 0; while (*p >= '0' && *p <= '9') { *epoch = *epoch * 10 + (*p - '0'); p++; }
    while (*p == ' ') p++;
    *hh = 0; while (*p >= '0' && *p <= '9') { *hh = *hh * 10 + (*p - '0'); p++; }
    while (*p == ' ') p++;
    *mm = 0; while (*p >= '0' && *p <= '9') { *mm = *mm * 10 + (*p - '0'); p++; }
    while (*p == ' ') p++;
    *ss = 0; while (*p >= '0' && *p <= '9') { *ss = *ss * 10 + (*p - '0'); p++; }
}

static void EspNtpClock(void)
{
    DrawClear();
    DrawText2("NTP Clock", (WIDTH - 9 * FONTW * 2) / 2, 90, COL_YELLOW);
    DrawText("Syncing time...", 20, 140, COL_LTGRAY);
    DispUpdate();

    EspSend("TIME");
    char line[64];
    EspReadLine(line, sizeof(line), 18000);

    long epoch; int hh, mm, ss;
    EspParseTime(line, &epoch, &hh, &mm, &ss);
    if (epoch < 1000000000) { EspMsg("NTP Clock", "Sync failed.", line); return; }

    long sec_of_day = (long)hh * 3600 + (long)mm * 60 + ss;
    u32 base_ms = Millis();
    u32 last_sync = Millis();

    while (1)
    {
        long now = sec_of_day + (long)((Millis() - base_ms) / 1000u);
        int h = (int)((now / 3600) % 24);
        int m = (int)((now / 60) % 60);
        int s = (int)(now % 60);

        char t[12];
        t[0] = (char)('0' + h / 10); t[1] = (char)('0' + h % 10); t[2] = ':';
        t[3] = (char)('0' + m / 10); t[4] = (char)('0' + m % 10); t[5] = ':';
        t[6] = (char)('0' + s / 10); t[7] = (char)('0' + s % 10); t[8] = 0;

        DrawClear();
        DrawText2("Prague", (WIDTH - 6 * FONTW * 2) / 2, 30, COL_LTGRAY);
        DrawText2(t, (WIDTH - (int)StrLen(t) * FONTW * 2) / 2, 100, COL_GREEN);
        DrawText("X: Back", 10, 220, COL_LTGRAY);
        DispUpdate();

        if (KeyGet() == KEY_X) return;

        if ((u32)(Millis() - last_sync) >= 60000)
        {
            last_sync = Millis();
            EspSend("TIME");
            char l2[64];
            EspReadLine(l2, sizeof(l2), 5000);
            long e; int h2, m2, s2;
            EspParseTime(l2, &e, &h2, &m2, &s2);
            if (e > 1000000000)
            {
                sec_of_day = (long)h2 * 3600 + (long)m2 * 60 + s2;
                base_ms = Millis();
            }
        }
        WaitMs(200);
    }
}

static void EspDisconnect(void)
{
    g_esp_conn = False;
    EspMsg("ESP32", "Disconnected.", NULL);
}

static void EspWeatherShow(void)
{
    DrawClear();
    DrawText2("Weather", (WIDTH - 7 * FONTW * 2) / 2, 90, COL_YELLOW);
    DrawText("Fetching...", 20, 140, COL_LTGRAY);
    DispUpdate();

    EspWeatherRefresh();
    const char* w = EspWeatherText();
    EspMsg("Weather (Prague)", w[0] ? w : "No data.", NULL);
}

// ---------------------------------------------------------------------------
// Menu
// ---------------------------------------------------------------------------

typedef void (*EspAction)(void);

static const char* const EspMenuLabels[] = {
    "Web Server",
    "Web GPIO",
    "DNS Lookup",
    "TCP Test",
    "Nmap Hosts",
    "Nmap Ports",
    "NTP Clock",
    "Weather",
    "LED On",
    "LED Off",
    "LED Blink",
    "ESP Info",
    "Disconnect",
};
static EspAction const EspMenuActions[] = {
    EspWebServer,
    EspWebGpio,
    EspDns,
    EspTcp,
    EspNmapHosts,
    EspNmapPorts,
    EspNtpClock,
    EspWeatherShow,
    EspLedOn,
    EspLedOff,
    EspLedBlink,
    EspInfo,
    EspDisconnect,
};
#define ESP_MENU_N ((int)(sizeof(EspMenuLabels) / sizeof(EspMenuLabels[0])))
#define ESP_VISIBLE 6

// Ensure the bridge is initialised and connected (error screen if not).
Bool EspEnsureConnected(void)
{
    EspUartInit();
    if (g_esp_conn) return True;

    DrawClear();
    DrawText2("ESP32", (WIDTH - 5 * FONTW * 2) / 2, 40, COL_YELLOW);
    DrawText("Contacting ESP32 on GP0/GP1...", 20, 110, COL_LTGRAY);
    DrawText("Press X to cancel", 20, 210, COL_GRAY);
    DispUpdate();

    g_esp_conn = EspHandshake();
    if (!g_esp_conn)
    {
        DrawClear();
        DrawText2("ESP32", (WIDTH - 5 * FONTW * 2) / 2, 40, COL_YELLOW);
        DrawText("ESP32 not responding.", 20, 100, COL_RED);
        DrawText("Check wiring (GP0<->GPIO3,", 20, 124, COL_LTGRAY);
        DrawText("GP1<->GPIO1, GND) and flash", 20, 140, COL_LTGRAY);
        DrawText("the bridge firmware.", 20, 156, COL_LTGRAY);
        DrawText("Press X to return", 20, 210, COL_GRAY);
        DispUpdate();
        while (KeyGet() != KEY_X) WaitMs(50);
        return False;
    }
    return True;
}

// Generic ESP action menu.
static void EspMenuLoop(const char* title, const char* const* labels,
                        EspAction const* actions, int count)
{
    int selected = 0;
    Bool need_redraw = True;

    while (1)
    {
        if (need_redraw)
        {
            DrawClear();
            DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 14,
                      COL_YELLOW);
            DrawRect(10, 40, WIDTH - 20, 1, COL_GRAY);

            int page = selected / ESP_VISIBLE;
            int first = page * ESP_VISIBLE;
            for (int i = 0; i < ESP_VISIBLE; i++)
            {
                int idx = first + i;
                if (idx >= count) break;
                int y = 52 + i * 22;
                Bool sel = (idx == selected);
                if (sel)
                    DrawRect(8, y - 3, WIDTH - 16, FONTH + 6, RGBTO16(40, 40, 0));
                DrawText(sel ? ">" : " ", 14, y, sel ? COL_YELLOW : COL_WHITE);
                DrawText(labels[idx], 34, y, sel ? COL_YELLOW : COL_WHITE);
            }

            if (count > ESP_VISIBLE)
            {
                char b[16];
                char num[8];
                strcpy(b, "L/R ");
                DecNum(num, page + 1, 0); strcat(b, num);
                strcat(b, "/");
                DecNum(num, (count + ESP_VISIBLE - 1) / ESP_VISIBLE, 0);
                strcat(b, num);
                DrawText(b, 10, 226, COL_LTGRAY);
            }
            DrawText("A: Run   X: Back", WIDTH - 16 * FONTW - 10, 226,
                     COL_LTGRAY);
            DispUpdate();
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            selected = (selected - 1 + count) % count;
            need_redraw = True;
            break;
        case KEY_DOWN:
            selected = (selected + 1) % count;
            need_redraw = True;
            break;
        case KEY_LEFT:
            if (selected >= ESP_VISIBLE) { selected -= ESP_VISIBLE; need_redraw = True; }
            break;
        case KEY_RIGHT:
        {
            int ns = selected + ESP_VISIBLE;
            if (ns >= count) ns = count - 1;
            if (ns != selected) { selected = ns; need_redraw = True; }
            break;
        }
        case KEY_A:
            actions[selected]();
            need_redraw = True;
            break;
        case KEY_X:
            return;
        default:
            break;
        }
    }
}

// Wi-Fi submenu (scan/connect/status/disconnect/AP).
static const char* const WifiMenuLabels[] = {
    "Wi-Fi Scan",
    "Wi-Fi Connect",
    "Wi-Fi Status",
    "Wi-Fi Disconnect",
    "Connect saved",
    "Wi-Fi AP",
};
static EspAction const WifiMenuActions[] = {
    EspWifiScan,
    EspWifiConnect,
    EspWifiStatus,
    EspWifiDisconnect,
    EspConnectSaved,
    EspWifiAp,
};
#define WIFI_MENU_N ((int)(sizeof(WifiMenuLabels) / sizeof(WifiMenuLabels[0])))

// Bluetooth submenu (BLE scan).
static const char* const BluetoothMenuLabels[] = {
    "BLE Scan",
};
static EspAction const BluetoothMenuActions[] = {
    EspBleScan,
};
#define BLUETOOTH_MENU_N ((int)(sizeof(BluetoothMenuLabels) / sizeof(BluetoothMenuLabels[0])))

void Esp32BridgeRun(void)
{
    // Prefer the PicoPad's own radio. The ESP32 menu is only a fallback when
    // the native module is not installed.
    if (NativeWifiPresent())
    {
        WifiMenuRun();
        return;
    }

    if (!EspEnsureConnected()) return;
    EspMenuLoop("ESP32", EspMenuLabels, EspMenuActions, ESP_MENU_N);
}

void EspWifiMenuRun(void)
{
    if (!EspEnsureConnected()) return;
    EspMenuLoop("WIFI", WifiMenuLabels, WifiMenuActions, WIFI_MENU_N);
}

void BluetoothMenuRun(void)
{
    if (!EspEnsureConnected()) return;
    EspMenuLoop("Bluetooth", BluetoothMenuLabels, BluetoothMenuActions,
                BLUETOOTH_MENU_N);
}
