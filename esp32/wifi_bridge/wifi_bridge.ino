// ****************************************************************************
//
//   PicoDeck — ESP32 bridge firmware
//
//   General UART command bridge for the PicoPad: Wi-Fi (station + AP), a web
//   server, network diagnostics, NTP, MQTT, BLE and LED.
//
//   Wiring (ESP32 DevKit <-> PicoPad external connector):
//     ESP32 GPIO3 (RX0) <- PicoPad GP0 (TX)
//     ESP32 GPIO1 (TX0) -> PicoPad GP1 (RX)
//     GND <-> GND   (power the ESP32 separately)
//
//   Protocol: 115200 8N1, one command per '\n', one or more reply lines.
//
//     PING                       -> "PONG"
//     INFO                       -> "ESP32 <model> <mac> <heap>"
//     REBOOT                     -> "OK" then restarts
//     LED ON|OFF|TOGGLE|BLINK n  -> "OK"
//     WIFI SCAN                  -> "SCAN <n>" + "<rssi> <ssid>" x n + "END"
//     WIFI CONNECT <ssid>|<pw>   -> "OK" / "ERR ..."
//     WIFI DISCONNECT            -> "OK"
//     WIFI STATUS                -> "WIFI <state> <ssid> <ip> <rssi>"
//     AP START <ssid>|<pw>       -> "OK <ip>" / "ERR ..."
//     AP STOP                    -> "OK"
//     DNS <host>                 -> "DNS <ip>" / "ERR ..."
//     TCP <host> <port>          -> "TCP ok" / "TCP fail"
//     NTP SYNC                   -> "OK <epoch>"
//     TIME                       -> "TIME <epoch> <hh> <mm> <ss>"  (Prague local)
//     WEATHER                    -> "WEATHER <tempC> <wmo_code>"
//     NMAP HOSTS                 -> "HOSTS" + "HOST <ip>" x n + "END"
//     NMAP PORTS <ip>            -> "PORTS" + "PORT <n>" x n + "END"
//     WEB BEGIN <len> + raw data -> "OK"
//     WEB START                  -> "OK <ip>"
//     WEB STOP                   -> "OK"
//     BLE SCAN                   -> "BLE <n>" + "<rssi> <name>" x n + "END"
//     BLE STOP                   -> "OK"
//
//   Partition scheme: "Huge APP (3MB No OTA/1MB SPIFFS)".
//
// ****************************************************************************

#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <esp_now.h>
#include <esp_wifi.h>

// Prague, Czechia (Europe/Prague)
#define WEATHER_LAT "50.08"
#define WEATHER_LON "14.44"

#ifndef LED_PIN
#define LED_PIN 2
#endif

static bool          g_led = false;
static int           g_blink_ms = 0;
static unsigned long g_last_blink = 0;
static BLEScan*      g_bleScan = nullptr;

static WebServer     g_server(80);
static String        g_page = "<h1>PicoPad</h1><p>No page uploaded.</p>";
static bool          g_webRunning = false;

// ---------------------------------------------------------------------------
// Companion link: TCP <-> UART bridge for the PicoPad companion app.
//
//   phone --TCP--> ESP32:   every line is forwarded to the PicoPad UART with
//                           an '@' prefix.  Lines starting with '!' are link
//                           control commands handled here.
//   PicoPad --UART--> ESP32: lines starting with '@' are forwarded to every
//                           TCP client (prefix stripped); all other lines are
//                           the normal PicoPad -> ESP32 command protocol.
//
// The prefixes keep the two protocols apart, so neither side tries to execute
// the other's commands.
// ---------------------------------------------------------------------------

#define LINK_MAX_CLIENTS 2
#define LINK_LINE_MAX    512

static WiFiServer* g_link = nullptr;
static bool        g_linkRunning = false;
static uint16_t    g_linkPort = 2323;
static WiFiClient  g_linkCli[LINK_MAX_CLIENTS];
static String      g_linkBuf[LINK_MAX_CLIENTS];
static String      g_uartLine;


// ---------------------------------------------------------------------------
// ESP-NOW: track stations (they announce "ANN <type> <name>") and relay
// DuckyScript payloads ("RDK" chunks) to a chosen device MAC on channel 1.
// ---------------------------------------------------------------------------

#define ESPNOW_CHUNK  240
#define ESPNOW_MAX    16384
#define MAX_DEVICES   8

typedef struct {
  char    magic[3];
  uint8_t total;
  uint8_t index;
  uint8_t len;
  uint8_t data[ESPNOW_CHUNK];
} EspNowMsg;

typedef struct {
  uint8_t  mac[6];
  uint8_t  type;
  char     name[16];
  uint32_t last_ms;
} EspDevice;

static EspDevice g_devices[MAX_DEVICES];
static int       g_dev_count = 0;
static bool      espNowReady = false;

static void espNowOnRecv(const esp_now_recv_info_t* info, const uint8_t* data, int len)
{
  if (len < 4 || memcmp(data, "ANN", 3) != 0) return;
  uint8_t type = data[3];
  char name[16];
  memset(name, 0, sizeof(name));
  int nl = len - 4; if (nl > 15) nl = 15;
  memcpy(name, data + 4, nl);

  const uint8_t* mac = info->src_addr;
  for (int i = 0; i < g_dev_count; i++)
  {
    if (memcmp(g_devices[i].mac, mac, 6) == 0)
    {
      g_devices[i].type = type;
      strncpy(g_devices[i].name, name, 15);
      g_devices[i].name[15] = 0;
      g_devices[i].last_ms = millis();
      return;
    }
  }
  if (g_dev_count < MAX_DEVICES)
  {
    memcpy(g_devices[g_dev_count].mac, mac, 6);
    g_devices[g_dev_count].type = type;
    strncpy(g_devices[g_dev_count].name, name, 15);
    g_devices[g_dev_count].name[15] = 0;
    g_devices[g_dev_count].last_ms = millis();
    g_dev_count++;
  }
}

static void espNowInit()
{
  if (espNowReady) return;
  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) return;

  uint8_t bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, bcast, 6);
  peer.channel = 1;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_now_add_peer(&peer);

  esp_now_register_recv_cb(espNowOnRecv);
  espNowReady = true;
}

static bool parseMac(const char* s, uint8_t out[6])
{
  unsigned v[6];
  if (sscanf(s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
    return false;
  for (int i = 0; i < 6; i++) out[i] = (uint8_t)v[i];
  return true;
}

static void espNowList()
{
  espNowInit();
  uint32_t now = millis();
  for (int i = 0; i < g_dev_count; i++)
  {
    bool online = (now - g_devices[i].last_ms < 6000);
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             (unsigned)g_devices[i].mac[0], (unsigned)g_devices[i].mac[1],
             (unsigned)g_devices[i].mac[2], (unsigned)g_devices[i].mac[3],
             (unsigned)g_devices[i].mac[4], (unsigned)g_devices[i].mac[5]);
    Serial.print("DEV "); Serial.print(mac);
    Serial.print(' '); Serial.print(g_devices[i].type);
    Serial.print(' '); Serial.print(g_devices[i].name);
    Serial.print(' '); Serial.println(online ? 1 : 0);
  }
  Serial.println("END");
}

// Send a payload of `len` bytes via ESP-NOW to a specific device MAC (chunked).
static void espNowSend(const uint8_t* mac, const uint8_t* payload, int len)
{
  espNowInit();
  if (!espNowReady) { Serial.println("ERR espnow"); return; }

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, mac, 6);
  peer.channel = 1;
  peer.ifidx = WIFI_IF_STA;
  peer.encrypt = false;
  esp_err_t r = esp_now_add_peer(&peer);
  if (r != ESP_OK && r != ESP_ERR_ESPNOW_EXIST) { Serial.println("ERR peer"); return; }

  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);

  int total = (len + ESPNOW_CHUNK - 1) / ESPNOW_CHUNK;
  for (int i = 0; i < total; i++)
  {
    EspNowMsg msg;
    memset(&msg, 0, sizeof(msg));
    memcpy(msg.magic, "RDK", 3);
    msg.total = (uint8_t)total;
    msg.index = (uint8_t)i;
    msg.len   = (uint8_t)((len - i * ESPNOW_CHUNK > ESPNOW_CHUNK) ? ESPNOW_CHUNK
                                                               : len - i * ESPNOW_CHUNK);
    memcpy(msg.data, payload + i * ESPNOW_CHUNK, msg.len);
    esp_now_send(mac, (uint8_t*)&msg, 6 + msg.len);
    delay(8);
  }
  Serial.println("DONE");
}

// ---------------------------------------------------------------------------
// LED
// ---------------------------------------------------------------------------

static void ledWrite(bool on) { g_led = on; digitalWrite(LED_PIN, on ? HIGH : LOW); }

static void ledBlinkTick()
{
  if (g_blink_ms <= 0) return;
  unsigned long now = millis();
  if (now - g_last_blink >= (unsigned long)g_blink_ms)
  {
    g_last_blink = now;
    ledWrite(!g_led);
  }
}

// ---------------------------------------------------------------------------
// Wi-Fi station
// ---------------------------------------------------------------------------

static void wifiScan()
{
  int n = WiFi.scanNetworks();
  Serial.print("SCAN "); Serial.println(n);
  for (int i = 0; i < n; i++)
  {
    Serial.print(WiFi.RSSI(i)); Serial.print(' '); Serial.println(WiFi.SSID(i));
  }
  Serial.println("END");
  WiFi.scanDelete();
}

static const char* wifiStateStr(wl_status_t st)
{
  switch (st)
  {
  case WL_CONNECTED:     return "connected";
  case WL_CONNECT_FAILED:return "failed";
  case WL_NO_SSID_AVAIL: return "no_ssid";
  case WL_IDLE_STATUS:   return "idle";
  case WL_DISCONNECTED:  return "disconnected";
  default:               return "other";
  }
}

static void wifiConnect(String ssid, String pass)
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);
  delay(100);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) delay(200);

  if (WiFi.status() == WL_CONNECTED) Serial.println("OK");
  else { Serial.print("ERR status="); Serial.println((int)WiFi.status()); }
}

static void wifiStatus()
{
  Serial.print("WIFI ");
  Serial.print(wifiStateStr(WiFi.status())); Serial.print(' ');
  Serial.print(WiFi.SSID()); Serial.print(' ');
  Serial.print(WiFi.localIP()); Serial.print(' ');
  Serial.println(WiFi.RSSI());
}

// ---------------------------------------------------------------------------
// AP mode
// ---------------------------------------------------------------------------

static void apStart(String ssid, String pass)
{
  WiFi.mode(WIFI_AP);
  bool ok = (pass.length() >= 8) ? WiFi.softAP(ssid.c_str(), pass.c_str())
                                 : WiFi.softAP(ssid.c_str());
  if (ok) { Serial.print("OK "); Serial.println(WiFi.softAPIP()); }
  else      Serial.println("ERR ap failed");
}

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------

static void dnsLookup(String host)
{
  IPAddress ip;
  if (WiFi.hostByName(host.c_str(), ip)) { Serial.print("DNS "); Serial.println(ip); }
  else                                    Serial.println("ERR dns");
}

static void tcpTest(String host, int port)
{
  WiFiClient c;
  c.setTimeout(3);
  if (c.connect(host.c_str(), (uint16_t)port)) { Serial.println("TCP ok"); c.stop(); }
  else                                          Serial.println("TCP fail");
}

// ---------------------------------------------------------------------------
// NTP
// ---------------------------------------------------------------------------

static time_t ntpNow(bool forceSync)
{
  time_t t = time(nullptr);
  if (forceSync || t < 1000000000)
  {
    // CET/CEST (Europe/Prague) with automatic DST.
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    unsigned long start = millis();
    while ((t = time(nullptr)) < 1000000000 && millis() - start < 15000) delay(200);
  }
  return t;
}

// ---------------------------------------------------------------------------
// Weather (Open-Meteo, no API key)
// ---------------------------------------------------------------------------

static void weatherFetch()
{
  if (WiFi.status() != WL_CONNECTED) { Serial.println("ERR no wifi"); return; }

  HTTPClient http;
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") +
               WEATHER_LAT + "&longitude=" + WEATHER_LON +
               "&current=temperature_2m,weather_code";
  http.begin(url);
  int code = http.GET();
  if (code != 200) { http.end(); Serial.print("ERR http "); Serial.println(code); return; }

  String body = http.getString();
  http.end();

  float temp = 0;
  int wcode = -1;
  int i = body.indexOf("\"temperature_2m\":");
  if (i >= 0) temp = body.substring(i + 17).toFloat();
  i = body.indexOf("\"weather_code\":");
  if (i >= 0) wcode = body.substring(i + 15).toInt();

  Serial.print("WEATHER ");
  Serial.print(temp, 1);
  Serial.print(' ');
  Serial.println(wcode);
}

// ---------------------------------------------------------------------------
// Web server
// ---------------------------------------------------------------------------

static void webStart()
{
  if (!g_webRunning)
  {
    g_server.on("/", []() { g_server.send(200, "text/html", g_page); });
    g_server.onNotFound([]() { g_server.send(404, "text/plain", "not found"); });
    g_server.begin();
    g_webRunning = true;
  }
  Serial.print("OK ");
  if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA)
    Serial.println(WiFi.softAPIP());
  else
    Serial.println(WiFi.localIP());
}

// ---------------------------------------------------------------------------
// Mini "nmap": subnet host discovery + port scan
// ---------------------------------------------------------------------------

static void nmapHosts()
{
  if (WiFi.status() != WL_CONNECTED) { Serial.println("ERR no wifi"); return; }

  IPAddress ip = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  uint32_t base = (uint32_t)ip & (uint32_t)mask;

  Serial.println("HOSTS");
  for (int h = 1; h < 255; h++)
  {
    IPAddress target(base | (uint32_t)h);
    if (target == ip) continue;

    bool up = false;
    { WiFiClient c; if (c.connect(target, 80, 40)) { up = true; c.stop(); } }
    if (!up) { WiFiClient c; if (c.connect(target, 443, 40)) { up = true; c.stop(); } }
    if (up) { Serial.print("HOST "); Serial.println(target); }
  }
  Serial.println("END");
}

static void nmapPorts(String ipStr)
{
  IPAddress ip;
  if (!ip.fromString(ipStr)) { Serial.println("ERR bad ip"); return; }

  static const uint16_t ports[] = {
    21, 22, 23, 25, 53, 80, 110, 143, 443, 445, 3306, 3389, 8080, 8443
  };
  Serial.println("PORTS");
  for (unsigned i = 0; i < sizeof(ports) / sizeof(ports[0]); i++)
  {
    WiFiClient c;
    if (c.connect(ip, ports[i], 150)) {
      Serial.print("PORT "); Serial.println(ports[i]);
      c.stop();
    }
  }
  Serial.println("END");
}

// ---------------------------------------------------------------------------
// Web GPIO control (relay button presses to the PicoPad over UART)
// ---------------------------------------------------------------------------

static const int WebPins[]  = {0, 1, 14, 22, 26, 27, 28};
static const char* WebNames[] = {"GP0", "GP1", "GP14", "USR LED",
                                 "GP26", "GP27", "GP28"};

static String webCtrlPage()
{
  String h = "<html><head><meta charset=\"utf-8\">"
             "<meta name=\"viewport\" content=\"width=device-width\">"
             "<title>PicoPad GPIO</title></head><body>"
             "<h2>PicoPad GPIO</h2>";
  for (unsigned i = 0; i < sizeof(WebPins)/sizeof(WebPins[0]); i++)
  {
    h += "<p>" + String(WebNames[i]) + " [" + String(WebPins[i]) + "] "
         "<a href=\"/set?pin=" + String(WebPins[i]) + "&val=1\">ON</a> "
         "<a href=\"/set?pin=" + String(WebPins[i]) + "&val=0\">OFF</a></p>";
  }
  h += "</body></html>";
  return h;
}

static void webCtrlStart()
{
  if (g_webRunning) g_server.stop();
  g_server.on("/", HTTP_GET, []() {
    g_server.send(200, "text/html", webCtrlPage());
  });
  g_server.on("/set", HTTP_GET, []() {
    Serial.print("SETGPIO "); Serial.print(g_server.arg("pin"));
    Serial.print(' '); Serial.println(g_server.arg("val"));
    g_server.sendHeader("Location", "/", true);
    g_server.send(302, "text/plain", "");
  });
  g_server.begin();
  g_webRunning = true;
  Serial.print("OK "); Serial.println(WiFi.localIP());
}

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------

static void bleScan()
{
  if (!g_bleScan)
  {
    BLEDevice::init("PicoPadBridge");
    g_bleScan = BLEDevice::getScan();
    g_bleScan->setActiveScan(true);
    g_bleScan->setInterval(100);
    g_bleScan->setWindow(99);
  }

  BLEScanResults* found = g_bleScan->start(5, false);
  int n = found ? found->getCount() : 0;
  Serial.print("BLE "); Serial.println(n);
  for (int i = 0; i < n; i++)
  {
    BLEAdvertisedDevice d = found->getDevice(i);
    Serial.print(d.getRSSI()); Serial.print(' '); Serial.println(d.getName().c_str());
  }
  Serial.println("END");
  g_bleScan->clearResults();
}

// ---------------------------------------------------------------------------
// Command dispatch
// ---------------------------------------------------------------------------

static void linkStart(uint16_t port);
static void linkStop();
static void linkStatus();
static void linkLoop();

static void handleCommand(String line)
{
  if (line.length() == 0) return;

  if (line == "?" || line == "PING") { Serial.println("PONG"); return; }

  if (line == "INFO")
  {
    Serial.print("ESP32 "); Serial.print(ESP.getChipModel());
    Serial.print(' '); Serial.print(WiFi.macAddress());
    Serial.print(' '); Serial.println(ESP.getFreeHeap());
    return;
  }

  if (line == "REBOOT") { Serial.println("OK"); delay(50); ESP.restart(); return; }

  if (line.startsWith("LED "))
  {
    String a = line.substring(4);
    if      (a == "ON")     { g_blink_ms = 0; ledWrite(true);   Serial.println("OK"); }
    else if (a == "OFF")    { g_blink_ms = 0; ledWrite(false);  Serial.println("OK"); }
    else if (a == "TOGGLE") { g_blink_ms = 0; ledWrite(!g_led); Serial.println("OK"); }
    else if (a.startsWith("BLINK "))
    {
      g_blink_ms = a.substring(6).toInt();
      if (g_blink_ms < 20) g_blink_ms = 20;
      g_last_blink = millis();
      Serial.println("OK");
    }
    else Serial.println("ERR bad led arg");
    return;
  }

  if (line == "WIFI SCAN")       { wifiScan(); return; }
  if (line == "WIFI DISCONNECT") { WiFi.disconnect(true); Serial.println("OK"); return; }
  if (line == "WIFI STATUS")     { wifiStatus(); return; }
  if (line.startsWith("WIFI CONNECT "))
  {
    String rest = line.substring(13);
    int bar = rest.indexOf('|');
    wifiConnect((bar >= 0) ? rest.substring(0, bar) : rest,
                (bar >= 0) ? rest.substring(bar + 1) : "");
    return;
  }

  if (line.startsWith("AP START "))
  {
    String rest = line.substring(9);
    int bar = rest.indexOf('|');
    apStart((bar >= 0) ? rest.substring(0, bar) : rest,
            (bar >= 0) ? rest.substring(bar + 1) : "");
    return;
  }
  if (line == "AP STOP") { WiFi.softAPdisconnect(true); Serial.println("OK"); return; }

  if (line.startsWith("DNS ")) { dnsLookup(line.substring(4)); return; }
  if (line.startsWith("TCP "))
  {
    String rest = line.substring(4);
    int sp = rest.indexOf(' ');
    if (sp < 0) { Serial.println("ERR args"); return; }
    tcpTest(rest.substring(0, sp), rest.substring(sp + 1).toInt());
    return;
  }

  if (line == "NTP SYNC") { time_t t = ntpNow(true); Serial.print("OK "); Serial.println((long)t); return; }
  if (line == "TIME")
  {
    time_t t = ntpNow(false);
    struct tm lt;
    localtime_r(&t, &lt);
    Serial.print("TIME "); Serial.print((long)t);
    Serial.print(' '); Serial.print(lt.tm_hour);
    Serial.print(' '); Serial.print(lt.tm_min);
    Serial.print(' '); Serial.println(lt.tm_sec);
    return;
  }
  if (line == "WEATHER") { weatherFetch(); return; }

  if (line == "NMAP HOSTS") { nmapHosts(); return; }
  if (line.startsWith("NMAP PORTS ")) { nmapPorts(line.substring(11)); return; }

  if (line == "WEB CTRL")  { webCtrlStart(); return; }
  if (line == "WEB START") { webStart(); return; }
  if (line == "WEB STOP")  { g_server.stop(); g_webRunning = false; Serial.println("OK"); return; }
  if (line.startsWith("WEB BEGIN "))
  {
    int len = line.substring(10).toInt();
    if (len < 0 || len > 20000) { Serial.println("ERR size"); return; }
    String page;
    page.reserve(len);
    Serial.setTimeout(5000);
    for (int i = 0; i < len; i++)
    {
      int c = Serial.read();
      while (c < 0) { c = Serial.read(); delay(1); }
      page += (char)c;
    }
    g_page = page;
    Serial.println("OK");
    return;
  }

  if (line == "BLE SCAN") { bleScan(); return; }
  if (line == "BLE STOP") { if (g_bleScan) g_bleScan->stop(); Serial.println("OK"); return; }

  if (line.startsWith("LINK START"))
  {
    int port = line.substring(10).toInt();
    linkStart(port > 0 ? (uint16_t)port : 2323);
    return;
  }
  if (line == "LINK STOP")  { linkStop();  Serial.println("OK"); return; }
  if (line == "LINK STATUS") { linkStatus(); return; }

  if (line == "ESPLIST") { espNowList(); return; }

  if (line.startsWith("ESPNOW "))
  {
    // "ESPNOW <mac> <len>"
    String rest = line.substring(7);
    int sp = rest.indexOf(' ');
    if (sp < 0) { Serial.println("ERR args"); return; }
    String macstr = rest.substring(0, sp);
    int len = rest.substring(sp + 1).toInt();

    uint8_t mac[6];
    if (!parseMac(macstr.c_str(), mac)) { Serial.println("ERR mac"); return; }
    if (len <= 0 || len > ESPNOW_MAX) { Serial.println("ERR size"); return; }

    static uint8_t buf[ESPNOW_MAX];
    Serial.println("GO");

    int total = 0;
    while (total < len)
    {
      int want = (len - total > 512) ? 512 : (len - total);
      int got = 0;
      uint32_t start = millis();
      while (got < want)
      {
        if (Serial.available())
        {
          buf[total + got] = (uint8_t)Serial.read();
          got++;
          start = millis();
        }
        else if ((uint32_t)(millis() - start) > 15000) break;
      }
      if (got <= 0) break;
      total += got;
      Serial.println("OK");
    }

    if (total != len) { Serial.println("ERR short"); return; }
    espNowSend(mac, buf, len);   // prints "DONE" or "ERR ..."
    return;
  }

  Serial.println("ERR unknown command");
}

// ---------------------------------------------------------------------------
// Companion link server
// ---------------------------------------------------------------------------

static IPAddress linkIp()
{
  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_AP || mode == WIFI_AP_STA)
  {
    IPAddress ip = WiFi.softAPIP();
    if (ip != IPAddress(0, 0, 0, 0)) return ip;
  }
  return WiFi.localIP();
}

static void linkSendAll(const String& text)
{
  for (int i = 0; i < LINK_MAX_CLIENTS; i++)
  {
    if (!g_linkCli[i].connected()) continue;
    g_linkCli[i].print(text);
    g_linkCli[i].print("\r\n");
    g_linkCli[i].flush();
  }
}

static void linkStart(uint16_t port)
{
  if (g_link && port != g_linkPort) linkStop();

  if (!g_link) g_link = new WiFiServer(port);
  g_linkPort = port;
  g_link->begin();
  g_link->setNoDelay(true);

  for (int i = 0; i < LINK_MAX_CLIENTS; i++) g_linkBuf[i] = "";
  g_uartLine = "";
  g_linkRunning = true;

  IPAddress ip = linkIp();
  Serial.print("OK ");
  Serial.print(ip);
  Serial.print(':');
  Serial.println(g_linkPort);
}

static void linkStop()
{
  for (int i = 0; i < LINK_MAX_CLIENTS; i++)
  {
    if (g_linkCli[i].connected())
    {
      g_linkCli[i].print("BYE\r\n");
      g_linkCli[i].flush();
      g_linkCli[i].stop();
    }
    g_linkBuf[i] = "";
  }
  if (g_link)
  {
    g_link->stop();
    delete g_link;
    g_link = nullptr;
  }
  g_linkRunning = false;
  g_uartLine = "";
}

static void linkStatus()
{
  int n = 0;
  for (int i = 0; i < LINK_MAX_CLIENTS; i++)
    if (g_linkCli[i].connected()) n++;

  Serial.print("LINK ");
  Serial.print(g_linkRunning ? "ON " : "OFF ");
  Serial.print(g_linkPort);
  Serial.print(' ');
  Serial.println(n);
}

// Handle one line received over TCP. Everything is PicoPad traffic unless it
// starts with '!', which is reserved for the ESP32 itself.
static void linkClientLine(int idx, String line)
{
  line.trim();
  if (line.length() == 0) return;

  if (line.startsWith("!"))
  {
    String cmd = line.substring(1);
    if (cmd == "LINK STATUS")
    {
      g_linkCli[idx].print("LINK ON ");
      g_linkCli[idx].print(g_linkPort);
      g_linkCli[idx].print(' ');
      g_linkCli[idx].println("1");
      return;
    }
    if (cmd == "LINK STOP") { g_linkCli[idx].println("OK stopping"); linkStop(); return; }
    if (cmd == "INFO")
    {
      g_linkCli[idx].print("ESP32 ");
      g_linkCli[idx].print(ESP.getChipModel());
      g_linkCli[idx].print(' ');
      g_linkCli[idx].println(linkIp());
      return;
    }
    g_linkCli[idx].println("ERR only !LINK / !INFO");
    return;
  }

  // Forward to the PicoPad with the '@' marker.
  Serial.print('@');
  Serial.print(line);
  Serial.print("\r\n");
}

static void linkLoop()
{
  // Accept a new client.
  WiFiClient nc = g_link->available();
  if (nc)
  {
    bool placed = false;
    for (int i = 0; i < LINK_MAX_CLIENTS; i++)
    {
      if (!g_linkCli[i].connected())
      {
        g_linkCli[i] = nc;
        g_linkCli[i].setNoDelay(true);
        g_linkBuf[i] = "";
        g_linkCli[i].println("PICOPAD COMPANION");
        placed = true;
        break;
      }
    }
    if (!placed) { nc.println("BUSY"); nc.stop(); }
  }

  // PicoPad -> phone (only '@' lines).
  while (Serial.available())
  {
    char c = (char)Serial.read();
    if (c == '\n')
    {
      g_uartLine.trim();
      if (g_uartLine.length())
      {
        if (g_uartLine.startsWith("@")) linkSendAll(g_uartLine.substring(1));
        else                            handleCommand(g_uartLine);
      }
      g_uartLine = "";
    }
    else if (c != '\r' && g_uartLine.length() < LINK_LINE_MAX)
    {
      g_uartLine += c;
    }
  }

  // Phone -> PicoPad.
  for (int i = 0; i < LINK_MAX_CLIENTS; i++)
  {
    if (!g_linkCli[i].connected()) continue;
    while (g_linkCli[i].available())
    {
      char c = (char)g_linkCli[i].read();
      if (c == '\n')
      {
        linkClientLine(i, g_linkBuf[i]);
        g_linkBuf[i] = "";
        if (!g_linkRunning) return;   // !LINK STOP raced with the loop
      }
      else if (c != '\r' && g_linkBuf[i].length() < LINK_LINE_MAX)
      {
        g_linkBuf[i] += c;
      }
    }
  }
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  pinMode(LED_PIN, OUTPUT);
  ledWrite(false);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("READY");
}

void loop()
{
  ledBlinkTick();
  if (g_webRunning) g_server.handleClient();

  if (g_linkRunning)
  {
    linkLoop();
    return;
  }

  if (Serial.available())
  {
    String line = Serial.readStringUntil('\n');
    line.trim();
    handleCommand(line);
  }
}
