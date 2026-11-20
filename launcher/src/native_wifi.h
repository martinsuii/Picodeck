// Native WiFi module (core 1) control interface.
#ifndef _NATIVE_WIFI_H
#define _NATIVE_WIFI_H

#include "../include.h"

// Detect the module image and start it on core 1. Safe to call when the
// module is not installed: returns False and leaves the launcher unchanged.
Bool NativeWifiStart(void);

Bool NativeWifiPresent(void);
Bool NativeWifiFailed(void);
u32  NativeWifiFailStage(void);
void WifiBsodShow(void);
Bool NativeWifiReady(void);
Bool NativeWifiLinkUp(void);
int  NativeWifiRssi(void);
const char* NativeWifiIp(void);

// Plain HTTP GET (the module has no TLS). Returns a pointer to the module's
// 4 KB shared response buffer (not NUL-terminated) and sets *outLen. The
// buffer is overwritten by the next call, so consume it first.
// Returns NULL on failure.
const char* NativeWifiHttpGet(const char* url, int* outLen);

// Queue a protocol line for the module. Replies come back through
// NativeWifiPopReply().
void NativeWifiSend(const char* line);
Bool NativeWifiPopReply(char* out, int max);

// Stop core 1 before touching internal flash. Pause parks it in RAM and is
// reversible; Shutdown deinitialises the radio and resets core 1.
Bool NativeWifiPause(void);
void NativeWifiResume(void);
void NativeWifiShutdown(void);

// /wifi.cnf helpers (two lines: SSID, password).
void WifiSaveCreds(const char* ssid, const char* pass);
Bool WifiLoadCreds(char* ssid, int ssidlen, char* pass, int passlen);

// Read /wifi.cfg from the (already mounted) SD card and send the credentials.
void NativeWifiSeedFromSd(void);

// Scan the network and wait for the module to publish results.
typedef struct { char ssid[33]; int rssi; } NativeWifiNet;
// Returns the number of entries, -1 when the module is not ready, -2 on timeout.
int NativeWifiScan(NativeWifiNet* out, int max, u32 timeout_ms);

// Battery: GP29 belongs to the radio once the module is running, so the
// launcher caches the value captured before start.
int LauncherBatMv(void);
void NativeWifiPublishBattery(void);

// Short status for the UI, e.g. "WiFi 10.0.0.5" or "WiFi: off".
void NativeWifiStatusText(char* out, int max);

#endif
