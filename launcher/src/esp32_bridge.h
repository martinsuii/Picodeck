// ****************************************************************************
//
//                  PicoDeck — ESP32 bridge
//
// ****************************************************************************

#ifndef _ESP32_BRIDGE_H
#define _ESP32_BRIDGE_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// ESP32 tools menu (web server, DNS/TCP, nmap, NTP, weather, LED, info).
// Requires the bridge firmware from esp32/wifi_bridge/.
void Esp32BridgeRun(void);

// Wi-Fi submenu (scan/connect/status/disconnect/AP).
void WifiMenuRun(void);
void EspWifiMenuRun(void);

// /wifi.cnf helpers (two lines: SSID, password)
void WifiSaveCreds(const char* ssid, const char* pass);
Bool WifiLoadCreds(char* ssid, int ssidlen, char* pass, int passlen);

// Bluetooth submenu (BLE scan).
void BluetoothMenuRun(void);

// Persistent connection state (established by opening the bridge, kept until
// reboot or an explicit disconnect).
Bool EspConnected(void);
void EspPoll(void);              // ping the bridge (called from the menu)
Bool EspEnsureConnected(void);   // handshake + "contacting" UI; False on fail

// Weather (Prague) cached on the PicoPad.
void EspWeatherRefresh(void);
Bool EspWeatherStale(void);
const char* EspWeatherText(void);

// Local time (Prague) cached on the PicoPad. EspTimeText fills "HH:MM:SS"
// and returns False if no time is available yet.
void EspTimeRefresh(void);
Bool EspTimeStale(void);
Bool EspTimeText(char* out, int outlen);

// ESP-NOW device (a station announcing itself on the network).
#define ESP_DEV_MAX       8
#define ESP_DEV_MAC_LEN   18
#define ESP_DEV_NAME_LEN  16

typedef struct {
    char mac[ESP_DEV_MAC_LEN];
    int  type;               // 1 = rubber ducky
    char name[ESP_DEV_NAME_LEN];
    Bool online;
} EspDevice;

// Query the bridge for the list of announced devices. Returns the count.
int  EspListDevices(EspDevice* out, int max);

// Send a DuckyScript payload to a specific device MAC via ESP-NOW.
Bool EspNowSendTo(const char* mac, const char* script, u32 len);

#ifdef __cplusplus
}
#endif

#endif // _ESP32_BRIDGE_H
