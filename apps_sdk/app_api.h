// ****************************************************************************
//
//                    PicoPad custom app API (v1)
//
//   Apps are ordinary PicoLibSDK programs (display, keys, SD, sound) plus the
//   functions below. They run while the native WiFi module keeps running on
//   core 1, so the API talks to it through the shared page.
//
// ****************************************************************************

#ifndef _APP_API_H
#define _APP_API_H

#include <stdint.h>
#include "../app_shared.h"

#define APP_API_VERSION_STR  "1"

typedef struct {
    char ssid[33];
    int  rssi;
} AppWifiNet;

// --- app entry -------------------------------------------------------------
// Define this in your app. Returning from it exits to the launcher.
void AppMain(void);

// --- system ----------------------------------------------------------------
void AppExit(void)     __attribute__((noreturn));
void AppReboot(void)   __attribute__((noreturn));
void AppRecovery(void) __attribute__((noreturn));
void AppBootsel(void)  __attribute__((noreturn));

// Themed crash screen. code is shown in the corner. Never returns.
void AppBsod(const char* title, const char* line1,
             const char* line2, uint32_t code) __attribute__((noreturn));

int         AppBatteryMv(void);     // last value published by the launcher
const char* AppId(void);
uint32_t    AppApiVersion(void);

// File path passed by "Open with app" (empty string if none).
const char* AppArgPath(void);

// Active launcher theme colours. idx is APP_TH_*; returns a sensible default
// when the launcher did not provide a palette.
uint16_t    AppTheme(int idx);
const char* AppThemeName(void);

// --- per-app config and data (/apps/<id>/...) ------------------------------
// Returns length on success, -1 on error.
int AppConfigRead(const char* key, char* out, int max);
// Returns 0 on success, -1 on error.
int AppConfigWrite(const char* key, const char* value);
const char* AppDataPath(void);

// --- WiFi (native module on core 1) ----------------------------------------
int         AppWifiPresent(void);   // module installed in flash
int         AppWifiReady(void);     // module running + radio up
int         AppWifiLinkUp(void);
const char* AppWifiIp(void);
int         AppWifiRssi(void);
const char* AppWifiSsid(void);

int  AppWifiScan(AppWifiNet* out, int max, uint32_t timeout_ms);
void AppWifiConnect(const char* ssid, const char* pass);
void AppWifiDisconnect(void);

// Plain HTTP GET (no TLS). url must start with http://.
// Returns body length (headers included) or a negative error.
// Do not use HTTP and raw sockets at the same time: they share the same 4 KB
// network scratch area.
int AppHttpGet(const char* url, char* out, int max);

// --- raw TCP (module >= 1.2.18) -------------------------------------------
// 2 stop-and-wait sockets, 1 KB RX + 1 KB TX each.
// AppNetOpen returns 0/1, or -1 when no socket is free / unsupported.
int  AppNetFeatures(void);
int  AppNetOpen(const char* host, int port);
int  AppNetState(int sock);              // 0 idle,1 dns,2 connecting,3 open,4 closed,5 error
int  AppNetSend(int sock, const void* data, int len);   // 0 ok, negative error
int  AppNetRecv(int sock, void* buf, int max);          // bytes, 0 none, negative error
void AppNetClose(int sock);

#endif // _APP_API_H
