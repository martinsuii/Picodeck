// ****************************************************************************
//
//                PicoDeck - native WiFi module
//
//   Runs on core 1 inside the game flash region and is started by the
//   launcher at boot. It owns the Pico W radio (CYW43439) and the lwIP stack,
//   and serves the Companion Link protocol on TCP port 2323.
//
//   The launcher (core 0) talks to it through a fixed shared page:
//     - status block (link state, IP, RSSI, clients)
//     - command ring  (launcher -> module)
//     - reply ring    (module -> launcher)
//
//   This image is NOT bootable: it has no own runtime init. The launcher calls
//   wifi_module_entry() on core 1; the entry sets up its own RAM vector table.
//
//   WiFi credentials come from the launcher (read from /wifi.cfg on the SD
//   card) or from WIFI CONNECT over USB/TCP. They live in RAM only: this
//   module never writes internal flash, so it can never stall core 0.
//
// ****************************************************************************

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

#include "pico.h"
#include "pico/time.h"
#include "pico/cyw43_arch.h"
#include "hardware/sync.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "hardware/regs/addressmap.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"
#include "lwip/err.h"

#include "wifi_shared.h"

#define LINK_PORT 2323

// ---------------------------------------------------------------------------
// startup without crt0
// ---------------------------------------------------------------------------

extern char __data_start__[];
extern char __data_end__[];
extern char __etext[];
extern char __bss_start__[];
extern char __bss_end__[];

extern void runtime_init_install_ram_vector_table(void);
extern void __aeabi_mem_init(void);
extern uint32_t aeabi_bits_funcs[];
extern bool rom_funcs_lookup(uint32_t *table, unsigned int count);
extern void runtime_init_spin_locks_reset(void);
void wifi_module_entry(void);
void wifi_module_start(void);

__attribute__((used))
const wifi_hdr_t wifi_hdr = {
    WIFI_HDR_MAGIC,
    WIFI_PROTO_VERSION,
    (uint32_t)(uintptr_t)&wifi_module_entry,
    (uint32_t)WIFI_SHARED_ADDR,
};

// No crt0 ran, so copy .data and zero .bss ourselves. This must NOT use
// memcpy/memset: the SDK wraps them in __wrap_* which jump through the
// aeabi_mem_funcs table, and that table is only filled in by
// __aeabi_mem_init() (normally called from the runtime init we skip).
static void startup_copy(volatile uint32_t* dst, const volatile uint32_t* src, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) dst[i] = src[i];
}

static void startup_zero(volatile uint32_t* dst, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) dst[i] = 0;
}

static void module_startup(void)
{
    __asm volatile("cpsid i" ::: "memory");

    startup_copy((volatile uint32_t*)__data_start__, (const volatile uint32_t*)__etext,
                 (uint32_t)(__data_end__ - __data_start__) / 4);
    startup_zero((volatile uint32_t*)__bss_start__,
                 (uint32_t)(__bss_end__ - __bss_start__) / 4);

    // Resolve the ROM memcpy/memset and bit-op pointers before anything uses
    // them. Missing __aeabi_bits_init is what made __ctzsi2 jump to 0x3354.
    __aeabi_mem_init();
    rom_funcs_lookup(aeabi_bits_funcs, 4);

    // Copy the flash vector table into RAM and point this core's VTOR at it.
    // Core 0 keeps its own VTOR, so the two runtimes do not interfere.
    runtime_init_install_ram_vector_table();

    __asm volatile("cpsie i" ::: "memory");
}

// ---------------------------------------------------------------------------
// shared state
// ---------------------------------------------------------------------------

static wifi_shared_t *const sh = WIFI_SHARED;

static char g_ssid[33];
static char g_pass[65];
static bool g_connected_once;

// TCP server
typedef struct {
    struct tcp_pcb *pcb;
    char buf[512];
    int len;
} conn_t;

static conn_t g_conns[2];
static struct tcp_pcb *g_listen;

// ---------------------------------------------------------------------------
// status
// ---------------------------------------------------------------------------

static bool wifi_up(void)
{
    return cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_UP;
}

static void wifi_ip(char *out, size_t n)
{
    out[0] = 0;
    if (!wifi_up()) return;
    const ip4_addr_t *ip = netif_ip4_addr(&cyw43_state.netif[CYW43_ITF_STA]);
    if (ip4_addr_isany_val(*ip)) return;
    snprintf(out, n, "%s", ip4addr_ntoa(ip));
}

static int wifi_rssi(void)
{
    int32_t rssi = 0;
    if (!wifi_up()) return 0;
    cyw43_wifi_get_rssi(&cyw43_state, &rssi);
    return (int)rssi;
}

static int clients_count(void)
{
    int n = 0;
    for (int i = 0; i < 2; i++)
        if (g_conns[i].pcb) n++;
    return n;
}

static void status_publish(void)
{
    char ip[16];
    wifi_ip(ip, sizeof(ip));

    int ls = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    const char *state = "disconnected";
    if (ls == CYW43_LINK_UP) state = "connected";
    else if (ls == CYW43_LINK_JOIN) state = "joining";
    else if (ls == CYW43_LINK_NOIP) state = "no ip";
    else if (ls == CYW43_LINK_FAIL) state = "link failed";
    else if (ls == CYW43_LINK_NONET) state = "no network";
    else if (ls == CYW43_LINK_BADAUTH) state = "bad auth";

    sh->seq++;
    __dmb();
    sh->link_up = (ls == CYW43_LINK_UP) ? 1 : 0;
    sh->rssi = wifi_rssi();
    strncpy((char *)sh->state, state, sizeof(sh->state) - 1);
    sh->state[sizeof(sh->state) - 1] = 0;
    sh->clients = (uint32_t)clients_count();
    strncpy((char *)sh->ssid, g_ssid, sizeof(sh->ssid) - 1);
    sh->ssid[sizeof(sh->ssid) - 1] = 0;
    strncpy((char *)sh->ip, ip, sizeof(sh->ip) - 1);
    sh->ip[sizeof(sh->ip) - 1] = 0;
    __dmb();
    sh->seq++;
}

// ---------------------------------------------------------------------------
// replies
// ---------------------------------------------------------------------------

static void tcp_send(struct tcp_pcb *pcb, const char *line)
{
    if (!pcb || pcb->state == CLOSED) return;
    size_t n = strlen(line);
    if (n) tcp_write(pcb, line, n, TCP_WRITE_FLAG_COPY);
    tcp_write(pcb, "\n", 1, TCP_WRITE_FLAG_COPY);
    tcp_output(pcb);
}

static void ring_push(const char *line)
{
    uint32_t head = sh->rep_head;
    uint32_t tail = sh->rep_tail;
    if (head - tail >= WIFI_REP_SLOTS) return;  // drop when the launcher is slow
    strncpy(sh->rep[head & (WIFI_REP_SLOTS - 1)], line, WIFI_LINE_MAX - 1);
    sh->rep[head & (WIFI_REP_SLOTS - 1)][WIFI_LINE_MAX - 1] = 0;
    __dmb();
    sh->rep_head = head + 1;
}

static void reply(struct tcp_pcb *pcb, const char *line)
{
    if (pcb) tcp_send(pcb, line);
    else ring_push(line);
}

// ---------------------------------------------------------------------------
// WiFi commands
// ---------------------------------------------------------------------------

static int scan_result_count;
static bool scan_running;
static bool scan_done;

typedef struct {
    char ssid[33];
    int32_t rssi;
} scan_item_t;

static scan_item_t g_scan[16];

static int scan_cb(void *env, const cyw43_ev_scan_result_t *res)
{
    (void)env;
    if (!res) {
        scan_running = false;
        scan_done = true;
        return 0;
    }
    if (scan_result_count < 16) {
        int n = res->ssid_len;
        if (n > 32) n = 32;
        memcpy(g_scan[scan_result_count].ssid, res->ssid, n);
        g_scan[scan_result_count].ssid[n] = 0;
        g_scan[scan_result_count].rssi = res->rssi;
        scan_result_count++;
    }
    return 0;
}

static void start_scan(void)
{
    if (scan_running) return;
    scan_result_count = 0;
    scan_done = false;
    scan_running = true;
    cyw43_wifi_scan_options_t opts;
    memset(&opts, 0, sizeof(opts));
    if (cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_cb) != 0) {
        scan_running = false;
        scan_done = true;
    }
}

static void broadcast(const char *line)
{
    for (int i = 0; i < 2; i++)
        if (g_conns[i].pcb) tcp_send(g_conns[i].pcb, line);
    ring_push(line);
}

static void publish_scan_results(void)
{
    char buf[64];

    // Shared copy first: the launcher's UI reads this, so a burst of lines
    // can never overflow the small reply ring.
    int n = scan_result_count;
    if (n > 16) n = 16;
    sh->scan_count = (uint32_t)n;
    for (int i = 0; i < n; i++) {
        strncpy(sh->scan_ssid[i], g_scan[i].ssid, sizeof(sh->scan_ssid[i]) - 1);
        sh->scan_ssid[i][sizeof(sh->scan_ssid[i]) - 1] = 0;
        sh->scan_rssi[i] = (int16_t)g_scan[i].rssi;
    }
    __dmb();
    sh->scan_gen = sh->scan_gen + 1;

    // TCP clients still get the line protocol.
    snprintf(buf, sizeof(buf), "SCAN %d", n);
    broadcast(buf);
    for (int i = 0; i < n; i++) {
        snprintf(buf, sizeof(buf), "%ld %s", (long)g_scan[i].rssi, g_scan[i].ssid);
        broadcast(buf);
    }
    broadcast("END");
}

// ---------------------------------------------------------------------------
// HTTP GET client (app API)
// ---------------------------------------------------------------------------

#define HTTP_TIMEOUT_MS   8000u
#define HTTP_DEFAULT_PORT 80

static struct tcp_pcb *http_pcb;
static ip_addr_t http_ip;
static char http_host[64];
static char http_path[128];
static u16_t http_port;
static uint32_t http_start_ms;
static bool http_active;

static void http_abort(int32_t err)
{
    if (http_pcb) {
        tcp_arg(http_pcb, NULL);
        tcp_recv(http_pcb, NULL);
        tcp_err(http_pcb, NULL);
        tcp_abort(http_pcb);
        http_pcb = NULL;
    }
    http_active = false;
    sh->http_err = err;
    __dmb();
    sh->http_state = 3;
}

static void http_done(void)
{
    if (http_pcb) {
        tcp_arg(http_pcb, NULL);
        tcp_recv(http_pcb, NULL);
        tcp_err(http_pcb, NULL);
        tcp_close(http_pcb);
        http_pcb = NULL;
    }
    http_active = false;
    __dmb();
    sh->http_state = 2;
}

static void http_parse_code(const char *b, uint32_t n)
{
    if (n < 12) return;
    if (b[0] != 'H' || b[1] != 'T' || b[2] != 'T' || b[3] != 'P' || b[4] != '/')
        return;
    for (uint32_t i = 5; i + 3 < n; i++) {
        if (b[i] == ' ' &&
            b[i+1] >= '1' && b[i+1] <= '5' &&
            b[i+2] >= '0' && b[i+2] <= '9' &&
            b[i+3] >= '0' && b[i+3] <= '9') {
            sh->http_code = (uint32_t)((b[i+1]-'0')*100 + (b[i+2]-'0')*10 + (b[i+3]-'0'));
            return;
        }
    }
}

static err_t http_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;
    if (!p) {
        http_done();
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);

    uint32_t len = sh->http_len;
    if (len < WIFI_HTTP_BUF_SIZE) {
        u16_t space = (u16_t)(WIFI_HTTP_BUF_SIZE - len);
        u16_t want = (p->tot_len < space) ? p->tot_len : space;
        u16_t n = pbuf_copy_partial(p, (uint8_t*)WIFI_HTTP_BUF_ADDR + len, want, 0);
        if (len == 0) http_parse_code((const char*)(WIFI_HTTP_BUF_ADDR + 0), n);
        sh->http_len = len + n;
    }
    pbuf_free(p);

    if (sh->http_len >= WIFI_HTTP_BUF_SIZE) {
        // Buffer full: keep what we have and close the connection.
        http_done();
    }
    return ERR_OK;
}

static void http_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    http_pcb = NULL;
    http_active = false;
    sh->http_err = -3;
    __dmb();
    sh->http_state = 3;
}

static err_t http_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) {
        http_abort(-3);
        return err;
    }

    char req[256];
    int n = snprintf(req, sizeof(req),
        "GET %s HTTP/1.0\r\nHost: %s\r\n"
        "User-Agent: PicoPad\r\nConnection: close\r\n\r\n",
        http_path, http_host);
    if (n <= 0) {
        http_abort(-1);
        return ERR_VAL;
    }

    err_t e = tcp_write(pcb, req, (u16_t)n, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) {
        http_abort(-3);
        return e;
    }
    tcp_output(pcb);
    return ERR_OK;
}

static void http_connect_now(void)
{
    http_pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!http_pcb) {
        http_abort(-1);
        return;
    }
    tcp_arg(http_pcb, NULL);
    tcp_err(http_pcb, http_err_cb);
    tcp_recv(http_pcb, http_recv_cb);
    if (tcp_connect(http_pcb, &http_ip, http_port, http_connected_cb) != ERR_OK)
        http_abort(-3);
}

static void http_dns_cb(const char *name, const ip_addr_t *ip, void *arg)
{
    (void)name;
    (void)arg;
    if (!ip) {
        http_abort(-2);
        return;
    }
    http_ip = *ip;
    http_connect_now();
}

static void http_start(const char *url)
{
    if (http_active) {
        sh->http_err = -1;
        __dmb();
        sh->http_state = 3;
        return;
    }
    if (strncmp(url, "http://", 7) != 0) {
        sh->http_err = -1;
        __dmb();
        sh->http_state = 3;
        return;
    }

    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);
    const char *colon = NULL;
    for (const char *q = p; q < host_end; q++)
        if (*q == ':') colon = q;

    int hlen = (int)((colon ? colon : host_end) - p);
    if (hlen <= 0 || hlen >= (int)sizeof(http_host)) {
        sh->http_err = -1;
        __dmb();
        sh->http_state = 3;
        return;
    }
    memcpy(http_host, p, (size_t)hlen);
    http_host[hlen] = 0;

    http_port = HTTP_DEFAULT_PORT;
    if (colon) {
        int v = atoi(colon + 1);
        if (v > 0 && v <= 65535) http_port = (u16_t)v;
    }

    const char *path = slash ? slash : "/";
    int plen = (int)strlen(path);
    if (plen >= (int)sizeof(http_path)) plen = (int)sizeof(http_path) - 1;
    memcpy(http_path, path, (size_t)plen);
    http_path[plen] = 0;

    sh->http_len = 0;
    sh->http_code = 0;
    sh->http_err = 0;
    __dmb();
    sh->http_state = 1;

    http_active = true;
    http_start_ms = to_ms_since_boot(get_absolute_time());

    if (ip4addr_aton(http_host, &http_ip)) {
        http_connect_now();
        return;
    }

    err_t e = dns_gethostbyname(http_host, &http_ip, http_dns_cb, NULL);
    if (e == ERR_OK) {
        http_connect_now();
    } else if (e != ERR_INPROGRESS) {
        http_abort(-2);
    }
}

// ---------------------------------------------------------------------------
// raw TCP sockets for apps (2 stop-and-wait sockets)
// ---------------------------------------------------------------------------

#define NET_SOCKS WIFI_NET_SOCKETS

static struct tcp_pcb *net_pcb[NET_SOCKS];
static ip_addr_t net_ip[NET_SOCKS];
static char net_host[NET_SOCKS][64];
static u16_t net_port[NET_SOCKS];
static uint32_t net_start_ms[NET_SOCKS];

static void net_fail(int s, int32_t err)
{
    if (net_pcb[s]) {
        tcp_arg(net_pcb[s], NULL);
        tcp_recv(net_pcb[s], NULL);
        tcp_err(net_pcb[s], NULL);
        tcp_abort(net_pcb[s]);
        net_pcb[s] = NULL;
    }
    sh->net_err[s] = err;
    sh->net_events[s] |= 4;
    __dmb();
    sh->net_state[s] = 5;
}

static void net_close(int s)
{
    if (s < 0 || s >= NET_SOCKS) return;
    if (net_pcb[s]) {
        tcp_arg(net_pcb[s], NULL);
        tcp_recv(net_pcb[s], NULL);
        tcp_err(net_pcb[s], NULL);
        tcp_close(net_pcb[s]);
        net_pcb[s] = NULL;
    }
    sh->net_rx_len[s] = 0;
    sh->net_tx_len[s] = 0;
    sh->net_events[s] = 0;
    __dmb();
    sh->net_state[s] = 0;
}

static err_t net_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    int s = (int)(intptr_t)arg;
    (void)err;

    if (!p) {
        tcp_arg(pcb, NULL);
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_close(pcb);
        net_pcb[s] = NULL;
        sh->net_events[s] |= 2;
        __dmb();
        sh->net_state[s] = 4;
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);

    uint32_t len = sh->net_rx_len[s];
    if (len < WIFI_NET_BUF_SIZE) {
        u16_t space = (u16_t)(WIFI_NET_BUF_SIZE - len);
        u16_t want = (p->tot_len < space) ? p->tot_len : space;
        u16_t n = pbuf_copy_partial(p, (uint8_t*)WIFI_NET_RX(s) + len, want, 0);
        sh->net_rx_len[s] = len + n;
    }
    pbuf_free(p);
    return ERR_OK;
}

static void net_err_cb(void *arg, err_t err)
{
    int s = (int)(intptr_t)arg;
    net_pcb[s] = NULL;
    sh->net_err[s] = (int32_t)err;
    sh->net_events[s] |= 4;
    __dmb();
    sh->net_state[s] = 5;
}

static err_t net_connected_cb(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)pcb;
    int s = (int)(intptr_t)arg;
    if (err != ERR_OK) {
        sh->net_err[s] = -3;
        sh->net_events[s] |= 4;
        __dmb();
        sh->net_state[s] = 5;
        return err;
    }
    sh->net_events[s] |= 1;
    __dmb();
    sh->net_state[s] = 3;
    return ERR_OK;
}

static void net_connect_now(int s)
{
    net_pcb[s] = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (!net_pcb[s]) {
        net_fail(s, -1);
        return;
    }
    tcp_arg(net_pcb[s], (void*)(intptr_t)s);
    tcp_err(net_pcb[s], net_err_cb);
    tcp_recv(net_pcb[s], net_recv_cb);
    if (tcp_connect(net_pcb[s], &net_ip[s], net_port[s],
                    net_connected_cb) != ERR_OK)
        net_fail(s, -3);
}

static void net_dns_cb(const char *name, const ip_addr_t *ip, void *arg)
{
    (void)name;
    int s = (int)(intptr_t)arg;
    if (!ip) {
        net_fail(s, -2);
        return;
    }
    net_ip[s] = *ip;
    net_connect_now(s);
}

static void net_open(int s, const char *host, u16_t port)
{
    if (s < 0 || s >= NET_SOCKS || !host || !host[0]) return;

    net_close(s);

    strncpy(net_host[s], host, sizeof(net_host[s]) - 1);
    net_host[s][sizeof(net_host[s]) - 1] = 0;
    net_port[s] = port;

    sh->net_err[s] = 0;
    sh->net_events[s] = 0;
    sh->net_rx_len[s] = 0;
    sh->net_tx_len[s] = 0;
    __dmb();
    sh->net_state[s] = 1;
    net_start_ms[s] = to_ms_since_boot(get_absolute_time());

    if (ip4addr_aton(net_host[s], &net_ip[s])) {
        __dmb();
        sh->net_state[s] = 2;
        net_connect_now(s);
        return;
    }

    err_t e = dns_gethostbyname(net_host[s], &net_ip[s], net_dns_cb,
                                (void*)(intptr_t)s);
    if (e == ERR_OK) {
        __dmb();
        sh->net_state[s] = 2;
        net_connect_now(s);
    } else if (e != ERR_INPROGRESS) {
        net_fail(s, -2);
    }
}

static void net_send(int s, uint32_t len)
{
    if (s < 0 || s >= NET_SOCKS) return;
    if (!net_pcb[s] || sh->net_state[s] != 3 ||
        len > WIFI_NET_BUF_SIZE || len > sh->net_tx_len[s]) {
        sh->net_tx_len[s] = 0;
        net_fail(s, -3);
        return;
    }

    err_t e = ERR_OK;
    if (len > 0) {
        e = tcp_write(net_pcb[s], (const void*)WIFI_NET_TX(s),
                      (u16_t)len, TCP_WRITE_FLAG_COPY);
        if (e == ERR_OK) tcp_output(net_pcb[s]);
    }
    sh->net_tx_len[s] = 0;
    __dmb();
    if (e != ERR_OK) net_fail(s, -3);
}

// ---------------------------------------------------------------------------
// protocol
// ---------------------------------------------------------------------------

static void handle_line(const char *raw, struct tcp_pcb *pcb)
{
    char line[WIFI_LINE_MAX];
    size_t n = strlen(raw);
    if (n >= sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, raw, n);
    line[n] = 0;

    bool bang = false;
    if (line[0] == '!') {
        bang = true;
        memmove(line, line + 1, strlen(line));
    }

    if (!strcmp(line, "PING")) {
        reply(pcb, "PONG");
        return;
    }

    if (!strcmp(line, "INFO")) {
        char buf[64];
        snprintf(buf, sizeof(buf), "INFO %s WIFI", "1.2.20");
        reply(pcb, buf);
        reply(pcb, "USB relayed");
        return;
    }

    if (!strcmp(line, "STATUS")) {
        char buf[128];
        int mv = sh->battery_mv;
        int pct = (mv - 3200) * 100 / (4200 - 3200);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        snprintf(buf, sizeof(buf), "STATUS %s %d %d 0 0 %d %s",
                 "1.2.20", mv, pct, wifi_up() ? 1 : 0,
                 g_ssid[0] ? g_ssid : "(default)");
        reply(pcb, buf);
        return;
    }

    if (!strcmp(line, "APPS")) {
        reply(pcb, "APPS 0");
        reply(pcb, "END");
        return;
    }

    if (!strcmp(line, "THEMES")) {
        reply(pcb, "THEMES 0");
        reply(pcb, "END");
        return;
    }

    if (!strncmp(line, "THEME ", 6)) {
        reply(pcb, "ERR launcher");
        return;
    }

    if (!strcmp(line, "WIFI STATUS") || !strcmp(line, "WIFI")) {
        char buf[96];
        char ip[16];
        wifi_ip(ip, sizeof(ip));
        if (wifi_up())
            snprintf(buf, sizeof(buf), "WIFI connected %s %s %d",
                     g_ssid[0] ? g_ssid : "?", ip, wifi_rssi());
        else
            snprintf(buf, sizeof(buf), "WIFI disconnected");
        reply(pcb, buf);
        return;
    }

    if (!strcmp(line, "WIFI SCAN")) {
        if (scan_running) reply(pcb, "ERR busy");
        else {
            start_scan();
            reply(pcb, "OK scanning");
        }
        return;
    }

    if (!strncmp(line, "WIFI CONNECT ", 13)) {
        const char *arg = line + 13;
        const char *bar = strchr(arg, '|');
        size_t sl = bar ? (size_t)(bar - arg) : strlen(arg);
        if (sl >= sizeof(g_ssid)) sl = sizeof(g_ssid) - 1;
        memcpy(g_ssid, arg, sl);
        g_ssid[sl] = 0;
        const char *pw = bar ? bar + 1 : "";
        strncpy(g_pass, pw, sizeof(g_pass) - 1);
        g_pass[sizeof(g_pass) - 1] = 0;
        if (!g_ssid[0]) {
            reply(pcb, "ERR ssid");
            return;
        }
        cyw43_arch_wifi_connect_async(g_ssid, g_pass, CYW43_AUTH_WPA2_AES_PSK);
        g_connected_once = true;
        status_publish();
        reply(pcb, "OK connecting");
        return;
    }

    if (!strcmp(line, "WIFI CLEAR")) {
        g_ssid[0] = 0;
        g_pass[0] = 0;
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        status_publish();
        reply(pcb, "OK");
        return;
    }

    if (!strcmp(line, "LINK STATUS")) {
        char buf[64];
        snprintf(buf, sizeof(buf), "LINK ON %d %d", LINK_PORT, clients_count() ? 1 : 0);
        reply(pcb, buf);
        return;
    }
    if (!strcmp(line, "LINK STOP")) {
        reply(pcb, "OK stopping");
        return;
    }
    if (!strncmp(line, "LINK START", 10)) {
        reply(pcb, "OK 0.0.0.0:2323");
        return;
    }

    if (!strncmp(line, "NET OPEN ", 9)) {
        const char *p = line + 9;
        int s = atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        const char *colon = strchr(p, ':');
        if (!colon || s < 0 || s >= NET_SOCKS) {
            if (s >= 0 && s < NET_SOCKS) net_fail(s, -1);
            return;
        }
        char host[64];
        int hlen = (int)(colon - p);
        if (hlen <= 0 || hlen >= (int)sizeof(host)) {
            net_fail(s, -1);
            return;
        }
        memcpy(host, p, (size_t)hlen);
        host[hlen] = 0;
        int port = atoi(colon + 1);
        if (port <= 0 || port > 65535) port = 80;
        if (!wifi_up()) {
            net_fail(s, -6);
            return;
        }
        net_open(s, host, (u16_t)port);
        return;
    }
    if (!strncmp(line, "NET CLOSE ", 10)) {
        int s = atoi(line + 10);
        net_close(s);
        return;
    }
    if (!strncmp(line, "NET SEND ", 9)) {
        const char *p = line + 9;
        int s = atoi(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        net_send(s, (uint32_t)atoi(p));
        return;
    }

    if (!strncmp(line, "HTTP GET ", 9)) {
        if (!wifi_up()) {
            sh->http_err = -6;
            __dmb();
            sh->http_state = 3;
            return;
        }
        http_start(line + 9);
        return;
    }

    if (!strcmp(line, "HELP") || !strcmp(line, "?")) {
        reply(pcb, "PING | INFO | STATUS | WIFI STATUS");
        reply(pcb, "WIFI SCAN | WIFI CONNECT ssid|pass | WIFI CLEAR");
        reply(pcb, "LINK STATUS | HELP");
        return;
    }

    if (bang) {
        reply(pcb, "ERR only !LINK / !INFO");
        return;
    }

    reply(pcb, "ERR unknown");
}

// ---------------------------------------------------------------------------
// TCP server
// ---------------------------------------------------------------------------

static conn_t *conn_free(void)
{
    for (int i = 0; i < 2; i++)
        if (!g_conns[i].pcb) return &g_conns[i];
    return NULL;
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    conn_t *c = (conn_t *)arg;
    (void)err;
    if (!p) {
        tcp_close(pcb);
        if (c) { c->pcb = NULL; c->len = 0; }
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);
    u16_t copied = pbuf_copy_partial(p, c->buf + c->len,
                                     (u16_t)(sizeof(c->buf) - 1 - c->len), 0);
    c->len += copied;
    pbuf_free(p);
    c->buf[c->len] = 0;

    int start = 0;
    for (int i = 0; i < c->len; i++) {
        if (c->buf[i] != '\n') continue;
        int end = i;
        if (end > start && c->buf[end - 1] == '\r') end--;
        char save = c->buf[end];
        c->buf[end] = 0;
        handle_line(c->buf + start, pcb);
        c->buf[end] = save;
        start = i + 1;
    }
    if (start > 0) {
        memmove(c->buf, c->buf + start, c->len - start);
        c->len -= start;
    }
    if (c->len >= (int)sizeof(c->buf) - 1) c->len = 0;
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    (void)err;
    conn_t *c = conn_free();
    if (!c) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    c->pcb = pcb;
    c->len = 0;
    tcp_arg(pcb, c);
    tcp_recv(pcb, on_recv);
    tcp_nagle_disable(pcb);
    tcp_send(pcb, "PICOPAD COMPANION");
    return ERR_OK;
}

static void on_error(void *arg, err_t err)
{
    (void)err;
    conn_t *c = (conn_t *)arg;
    if (c) { c->pcb = NULL; c->len = 0; }
}

static void server_start(void)
{
    g_listen = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!g_listen) return;
    if (tcp_bind(g_listen, IP_ANY_TYPE, LINK_PORT) != ERR_OK) return;
    g_listen = tcp_listen_with_backlog(g_listen, 2);
    if (!g_listen) return;
    tcp_accept(g_listen, on_accept);
}

static void server_stop(void)
{
    if (g_listen) {
        tcp_close(g_listen);
        g_listen = NULL;
    }
    for (int i = 0; i < 2; i++) {
        if (g_conns[i].pcb) {
            tcp_arg(g_conns[i].pcb, NULL);
            tcp_recv(g_conns[i].pcb, NULL);
            tcp_close(g_conns[i].pcb);
            g_conns[i].pcb = NULL;
            g_conns[i].len = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// park / shutdown
// ---------------------------------------------------------------------------

// This runs while core 0 erases or programs flash. It must not fetch from
// flash, so it lives in RAM (.__time_critical, copied in module_startup).
static __attribute__((noinline, used)) void __not_in_flash_func(wifi_park_loop)(void)
{
    while (sh->pause_req) tight_loop_contents();
    __dmb();
}

static void wifi_park(void)
{
    // Stop the radio IRQ and all interrupts before entering the RAM loop.
    irq_set_enabled(IO_IRQ_BANK0, false);
    __asm volatile("cpsid i" ::: "memory");
    sh->paused = 1;
    __dmb();

    wifi_park_loop();

    sh->paused = 0;
    __dmb();
    __asm volatile("cpsie i" ::: "memory");
    irq_set_enabled(IO_IRQ_BANK0, true);
}

// ---------------------------------------------------------------------------
// command ring (core 0 -> module)
// ---------------------------------------------------------------------------

static void process_commands(void)
{
    while (sh->cmd_tail != sh->cmd_head) {
        uint32_t tail = sh->cmd_tail;
        char line[WIFI_LINE_MAX];
        strncpy(line, sh->cmd[tail & (WIFI_CMD_SLOTS - 1)], WIFI_LINE_MAX - 1);
        line[WIFI_LINE_MAX - 1] = 0;
        __dmb();
        sh->cmd_tail = tail + 1;
        handle_line(line, NULL);
    }
}

// ---------------------------------------------------------------------------
// entry
// ---------------------------------------------------------------------------

// Naked entry: point core 1 at the module's own vector table before any C
// code runs. If the module faults later, it must never fall into the
// launcher's fault handler (that shares the display/font state).
__attribute__((naked, noreturn)) void wifi_module_entry(void)
{
    __asm volatile(
        "ldr r0, =0xE000ED08\n"   // SCB->VTOR
        "ldr r1, =0x10000100\n"   // module's flash vector table
        "str r1, [r0]\n"
        "ldr r1, =wifi_module_start\n"
        "bx r1\n"
    );
}

extern uint32_t ram_vector_table[];
extern void __unhandled_user_irq(void);

static __attribute__((noinline, used)) void __not_in_flash_func(irq_safe_stub)(void)
{
    uint32_t ipsr;
    __asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    uint32_t irq = ipsr - 16u;
    if (irq < 32u) {
        WIFI_SHARED->fault_cfsr = irq;      // remember which one fired
        WIFI_SHARED->fault_hfsr = 1;
        *(volatile uint32_t*)0xE000E280u = 1u << irq; // clear pending
        *(volatile uint32_t*)0xE000E180u = 1u << irq; // disable IRQ
    }
}

static void install_unhandled_fallback(void)
{
    uint32_t unhandled = (uint32_t)&__unhandled_user_irq;
    for (int i = 16; i < 48; i++) {
        if (ram_vector_table[i] == unhandled)
            ram_vector_table[i] = (uint32_t)&irq_safe_stub;
    }
}

// All panic() calls land here. Keep the first 8 characters of the message so
// the launcher can display them without a console.
void wifi_panic(const char *fmt, ...)
{
    WIFI_SHARED->fault_cfsr = (uint32_t)fmt;
    WIFI_SHARED->fault_hfsr = (uint32_t)__builtin_return_address(0);
    uint32_t st = WIFI_SHARED->stage & 0xFFFFu;
    WIFI_SHARED->stage = 0x80000000u | st;
    for (;;) tight_loop_contents();
}

// C assert() goes straight to __assert_func -> _exit, bypassing panic().
// Capture the failed expression instead.
void __assert_func(const char *file, int line, const char *func, const char *expr)
{
    (void)file; (void)func;
    WIFI_SHARED->fault_cfsr = (uint32_t)expr;
    WIFI_SHARED->fault_hfsr = (uint32_t)line;
    uint32_t st = WIFI_SHARED->stage & 0xFFFFu;
    WIFI_SHARED->stage = 0x80000000u | st;
    for (;;) tight_loop_contents();
}

// Last-resort exit path (assert/abort). Mark it clearly.
void _exit(int status)
{
    (void)status;
    WIFI_SHARED->fault_cfsr = 0x54495845u;   // "EXIT"
    WIFI_SHARED->fault_hfsr = (uint32_t)__builtin_return_address(0);
    uint32_t st = WIFI_SHARED->stage & 0xFFFFu;
    WIFI_SHARED->stage = 0x80000000u | st;
    for (;;) tight_loop_contents();
}

// lwIP assertion: keep the first 8 characters so the launcher can show them
// even though the module has no console.
void wifi_lwip_assert(const char *msg)
{
    WIFI_SHARED->fault_cfsr = (uint32_t)msg;
    WIFI_SHARED->fault_hfsr = (uint32_t)__builtin_return_address(0);
    uint32_t st = WIFI_SHARED->stage & 0xFFFFu;
    WIFI_SHARED->stage = 0x80000000u | st;
    for (;;) tight_loop_contents();
}

static void __attribute__((used, noinline)) wifi_fault_c(uint32_t *frame);

// Naked so MSP still points at the exception frame (r0..r3, r12, lr, pc, xpsr).
__attribute__((naked)) static void wifi_fault(void)
{
    __asm volatile("mrs r0, msp\n"
                   "ldr r1, =wifi_fault_c\n"
                   "bx r1\n");
}

static void __attribute__((used, noinline)) wifi_fault_c(uint32_t *frame)
{
    uint32_t st = WIFI_SHARED->stage & 0xFFFFu;

    // __unhandled_user_irq puts the IRQ number in r0 before the bkpt, and
    // the exception frame still has it. Fall back to IABR if that is not a
    // plausible IRQ number.
    WIFI_SHARED->fault_cfsr = frame[6];    // stacked PC (where it died)
    WIFI_SHARED->fault_hfsr = *(volatile uint32_t*)0xE000E300u; // active IRQs
    WIFI_SHARED->stage = 0x80000000u | st;
    for (;;) tight_loop_contents();
}

void wifi_module_start(void)
{
    // Mark "started" before touching .data/.bss so a very early fault is
    // visible to the launcher instead of showing random RAM.
    WIFI_SHARED->stage = 1;
    WIFI_SHARED->magic = WIFI_HDR_MAGIC;
    WIFI_SHARED->version = 0x010214u;   // 1.2.20
    WIFI_SHARED->fault_cfsr = 0;
    WIFI_SHARED->fault_hfsr = 0;
    __dmb();

    module_startup();

    // Pieces of the pico-sdk runtime init that matter on a secondary core.
    runtime_init_spin_locks_reset();

    // The RAM vector table is live now; catch faults from the radio init.
    ram_vector_table[3] = (uint32_t)wifi_fault;   // HardFault
    WIFI_SHARED->stage = 2;

    sh->ready = 0;
    sh->stopped = 0;
    sh->paused = 0;
    sh->shutdown_req = 0;
    sh->cmd_head = sh->cmd_tail = 0;
    sh->rep_head = sh->rep_tail = 0;
    sh->http_state = 0;
    sh->http_len = 0;
    sh->http_code = 0;
    sh->http_err = 0;
    sh->net_features = 0;
    for (int i = 0; i < NET_SOCKS; i++) {
        sh->net_state[i] = 0;
        sh->net_err[i] = 0;
        sh->net_rx_len[i] = 0;
        sh->net_tx_len[i] = 0;
        sh->net_events[i] = 0;
        net_pcb[i] = NULL;
    }
    __dmb();

    sh->stage = 3;
    // The launcher never ran the pico-sdk runtime init, so PIO/DMA may still
    // be held in reset. Give the radio driver working peripherals first.
    reset_block(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_DMA_BITS);
    unreset_block_wait(RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_DMA_BITS);
    sh->stage = 4;
    if (cyw43_arch_init()) {
        sh->stage = 90;
        strncpy((char *)sh->state, "init failed", sizeof(sh->state) - 1);
        // Leave ready = 0 so the launcher reports the radio failure instead
        // of pretending the module is usable.
        sh->ready = 0;
        __dmb();
        for (;;) {
            if (sh->shutdown_req) { sh->stopped = 1; return; }
            tight_loop_contents();
        }
    }

    // Any IRQ still pointing at the SDK's bkpt handler would escalate to a
    // HardFault on the first interrupt. Disable those instead of dying.
    install_unhandled_fallback();

    // TCP first, then the STA interface, so a fault tells us which call it is.
    sh->stage = 5;              // server_start()
    server_start();

    sh->stage = 6;              // cyw43_arch_enable_sta_mode()
    cyw43_arch_enable_sta_mode();

    sh->stage = 7;              // both done; about to report ready
    sh->net_features = WIFI_NET_FEATURE_SOCKETS;
    sh->ready = 1;
    __dmb();
    status_publish();

    uint32_t last_status = 0;

    for (;;) {
        cyw43_arch_poll();

        // Fallback: some CYW43 builds drop the final scan callback. The
        // driver clears wifi_scan_state when the scan is really finished.
        if (scan_running && !cyw43_wifi_scan_active(&cyw43_state)) {
            scan_running = false;
            scan_done = true;
        }

        if (sh->pause_req) wifi_park();

        if (sh->shutdown_req) {
            if (http_active) http_abort(-7);
            for (int i = 0; i < NET_SOCKS; i++) net_close(i);
            server_stop();
            cyw43_arch_deinit();
            sh->ready = 0;
            sh->stopped = 1;
            return;
        }

        process_commands();

        if (scan_done) {
            scan_done = false;
            publish_scan_results();
            status_publish();
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (http_active && (uint32_t)(now - http_start_ms) > HTTP_TIMEOUT_MS)
            http_abort(-4);
        for (int i = 0; i < NET_SOCKS; i++) {
            uint32_t st = sh->net_state[i];
            if ((st == 1 || st == 2) &&
                (uint32_t)(now - net_start_ms[i]) > 10000u)
                net_fail(i, -4);
        }
        if (now - last_status >= 500) {
            last_status = now;
            status_publish();
        }
    }
}

// Never called: the launcher starts wifi_module_entry() directly. crt0 is
// linked only for the vector table, so it needs a main to satisfy the linker.
int main(void)
{
    for (;;) tight_loop_contents();
}
