// Shared memory contract between the launcher (core 0) and the WiFi module
// (core 1). Both binaries include this file; the addresses are fixed.
#ifndef WIFI_SHARED_H
#define WIFI_SHARED_H

#include <stdint.h>

#define WIFI_SHARED_ADDR    0x20039000u   // 4 KB page between launcher RAM and module BSS
#define WIFI_MODULE_HDR     0x100001C0u   // right after the module's vector table
#define WIFI_HDR_MAGIC      0x314D4350u   // "PCM1"
#define WIFI_PROTO_VERSION  1u

#define WIFI_CMD_SLOTS  4
#define WIFI_REP_SLOTS  8
#define WIFI_LINE_MAX   160

typedef struct {
    volatile uint32_t magic;      // module writes WIFI_HDR_MAGIC when ready
    volatile uint32_t ready;
    volatile uint32_t stopped;
    volatile uint32_t seq;        // odd while the status fields are being written
    volatile uint32_t link_up;
    volatile int32_t  rssi;
    volatile uint32_t clients;
    volatile int32_t  battery_mv;
    volatile uint32_t paused;
    volatile uint32_t pause_req;  // core 0 asks core 1 to park
    volatile uint32_t shutdown_req;
    volatile uint32_t stage;
    volatile char ssid[33];
    volatile char ip[16];
    volatile char state[24];

    volatile uint32_t cmd_head, cmd_tail;
    char cmd[WIFI_CMD_SLOTS][WIFI_LINE_MAX];

    volatile uint32_t rep_head, rep_tail;
    char rep[WIFI_REP_SLOTS][WIFI_LINE_MAX];

    // Latest scan results (shared directly so a long list cannot overflow
    // the reply ring).
    volatile uint32_t scan_gen;
    volatile uint32_t scan_count;
    char scan_ssid[16][33];
    int16_t scan_rssi[16];

    // Diagnostics appended for backward compatibility.
    volatile uint32_t version;
    volatile uint32_t fault_cfsr;
    volatile uint32_t fault_hfsr;

    // App HTTP client. The response body is written to the fixed app-side
    // buffer at WIFI_HTTP_BUF_ADDR (APP_NET_BUF_ADDR).
    volatile uint32_t http_state;   // 0 idle, 1 busy, 2 done, 3 error
    volatile uint32_t http_code;    // HTTP status code, if parsed
    volatile uint32_t http_len;     // bytes in the response buffer
    volatile int32_t  http_err;     // negative error when http_state == 3

    // Raw TCP sockets for apps (module >= 1.2.18). Buffers live in the app
    // side of the shared 4 KB net area:
    //   sock 0: RX 0x20038000, TX 0x20038400
    //   sock 1: RX 0x20038800, TX 0x20038C00
    // HTTP GET uses the whole 4 KB, so do not mix sockets and HTTP.
    volatile uint32_t net_features;      // bit0 = raw sockets available
    volatile uint32_t net_state[2];      // 0 idle,1 dns,2 connecting,3 open,4 closed,5 error
    volatile int32_t  net_err[2];
    volatile uint32_t net_rx_len[2];     // app clears to 0 after reading
    volatile uint32_t net_tx_len[2];     // app fills, module clears after send
    volatile uint32_t net_events[2];     // bit0 connected, bit1 closed, bit2 error
} wifi_shared_t;

#define WIFI_NET_SOCKETS   2
#define WIFI_NET_BUF_SIZE  1024u
#define WIFI_NET_RX(i)     (WIFI_HTTP_BUF_ADDR + (uint32_t)(i) * 0x800u)
#define WIFI_NET_TX(i)     (WIFI_HTTP_BUF_ADDR + (uint32_t)(i) * 0x800u + 0x400u)

#define WIFI_NET_FEATURE_SOCKETS  1u

#define WIFI_HTTP_BUF_ADDR  0x20038000u
#define WIFI_HTTP_BUF_SIZE  0x1000u

#define WIFI_SHARED ((wifi_shared_t *)WIFI_SHARED_ADDR)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry;    // absolute address of wifi_module_entry
    uint32_t shared;   // WIFI_SHARED_ADDR
} wifi_hdr_t;

#endif
