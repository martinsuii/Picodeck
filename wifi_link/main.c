// ****************************************************************************
//
//                    PicoDeck - WiFi Link
//
//   Standalone Pico W app that replaces a game in the game flash region and
//   provides:
//
//     - a TCP server on port 2323 speaking the same line protocol as the
//       launcher's Companion Link (USB style, no ESP32 needed)
//     - USB CDC on the same protocol, so the companion works over the cable
//     - WiFi setup and status commands
//     - a small status screen; X reboots back to the launcher
//
//   Flash it like a game (LOAD from SD). To get the game area back, flash a
//   game UF2 over it.
//
//   Known v1 limits: no SD/FAT yet, so APPS/THEMES report 0 and theme upload
//   is rejected. The companion's SD tab still works through a card reader.
//
// ****************************************************************************

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdarg.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/stdio_usb.h"
#include "pico/bootrom.h"
#include "hardware/adc.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "hardware/regs/addressmap.h"
#include "lwip/tcp.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"

#include "pins.h"
#include "lcd.h"

#define LINK_PORT 2323
#define CFG_OFFSET 0x000DE000u          // last 4 KB of the game region
#define CFG_MAGIC  0x31494657u          // "WFI1"
#define CMD_MAX   160

typedef struct {
    uint32_t magic;
    char ssid[33];
    char pass[65];
    uint8_t pad[2];
} app_cfg_t;

static app_cfg_t g_cfg;
static int g_bat_mv;
static char g_ip[20] = "";

// --- line connections -------------------------------------------------------

typedef struct {
    struct tcp_pcb *pcb;
    char buf[512];
    int len;
} conn_t;

static conn_t g_conns[2];
static struct tcp_pcb *g_listen;

// --- WiFi scan state ---------------------------------------------------------

#define SCAN_MAX 16
typedef struct {
    char ssid[33];
    int32_t rssi;
} scan_item_t;

static scan_item_t g_scan[SCAN_MAX];
static int g_scan_count;
static volatile bool g_scanning;
static volatile bool g_scan_done;

// --- display state -----------------------------------------------------------

static char g_screen[6][40];
static char g_screen_prev[6][40];
static bool g_screen_valid;

// ---------------------------------------------------------------------------
// config in flash
// ---------------------------------------------------------------------------

static void cfg_load(void)
{
    memcpy(&g_cfg, (const void *)(XIP_BASE + CFG_OFFSET), sizeof(g_cfg));
    if (g_cfg.magic != CFG_MAGIC) {
        memset(&g_cfg, 0, sizeof(g_cfg));
        g_cfg.magic = CFG_MAGIC;
    }
    g_cfg.ssid[sizeof(g_cfg.ssid) - 1] = 0;
    g_cfg.pass[sizeof(g_cfg.pass) - 1] = 0;
}

static void cfg_save(void)
{
    static uint8_t page[FLASH_SECTOR_SIZE];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &g_cfg, sizeof(g_cfg));

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CFG_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CFG_OFFSET, page, FLASH_SECTOR_SIZE);
    restore_interrupts(ints);
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

static void reply(struct tcp_pcb *pcb, const char *line)
{
    if (pcb) tcp_send(pcb, line);
    else {
        printf("%s\n", line);
    }
}

static void reply_all(const char *line)
{
    for (int i = 0; i < 2; i++) {
        if (g_conns[i].pcb) tcp_send(g_conns[i].pcb, line);
    }
    if (stdio_usb_connected()) printf("%s\n", line);
}

static void reply_num(struct tcp_pcb *pcb, const char *prefix, int value)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "%s%d", prefix, value);
    reply(pcb, buf);
}

// ---------------------------------------------------------------------------
// WiFi helpers
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

static int scan_cb(void *env, const cyw43_ev_scan_result_t *res)
{
    (void)env;
    if (!res) {
        g_scanning = false;
        g_scan_done = true;
        return 0;
    }
    if (g_scan_count < SCAN_MAX) {
        int n = res->ssid_len;
        if (n > 32) n = 32;
        memcpy(g_scan[g_scan_count].ssid, res->ssid, n);
        g_scan[g_scan_count].ssid[n] = 0;
        g_scan[g_scan_count].rssi = res->rssi;
        g_scan_count++;
    }
    return 0;
}

static void wifi_scan_start(void)
{
    if (g_scanning) return;
    g_scan_count = 0;
    g_scan_done = false;
    g_scanning = true;
    cyw43_wifi_scan_options_t opts;
    memset(&opts, 0, sizeof(opts));
    if (cyw43_wifi_scan(&cyw43_state, &opts, NULL, scan_cb) != 0) {
        g_scanning = false;
        g_scan_done = true;
    }
}

// ---------------------------------------------------------------------------
// protocol
// ---------------------------------------------------------------------------

static int battery_pct(void)
{
    int pct = (g_bat_mv - 3200) * 100 / (4200 - 3200);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}

static void reboot_to_menu(void)
{
    // The launcher sets its "came from menu" magic right before jumping to
    // the game image; clear it or the launcher would relaunch us.
    watchdog_hw->scratch[0] = 0;
    watchdog_reboot(0, 0, 0);
}

static void handle_line(const char *raw, struct tcp_pcb *pcb)
{
    char line[CMD_MAX];
    size_t n = strlen(raw);
    if (n >= sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, raw, n);
    line[n] = 0;

    // Over the ESP32 bridge, "!cmd" is ESP32-local. Emulate the useful bits.
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
        snprintf(buf, sizeof(buf), "INFO %s WIFI", APP_VERSION);
        reply(pcb, buf);
        reply(pcb, stdio_usb_connected() ? "USB mounted" : "USB idle");
        return;
    }

    if (!strcmp(line, "STATUS")) {
        char buf[128];
        char ip[20];
        wifi_ip(ip, sizeof(ip));
        snprintf(buf, sizeof(buf), "STATUS %s %d %d 0 %d 0 %s",
                 APP_VERSION, g_bat_mv, battery_pct(),
                 stdio_usb_connected() ? 1 : 0,
                 wifi_up() ? "(wifi)" : "(default)");
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
        reply(pcb, "ERR no sd");
        return;
    }

    if (!strcmp(line, "REBOOT") || !strcmp(line, "HOME")) {
        reply(pcb, "OK");
        sleep_ms(80);
        reboot_to_menu();
        return;
    }

    if (!strcmp(line, "BOOTSEL")) {
        reply(pcb, "OK");
        sleep_ms(80);
        reset_usb_boot(0, 0);
        return;
    }

    if (!strcmp(line, "SAVER")) {
        reply(pcb, "OK");
        return;
    }

    if (!strcmp(line, "WIFI STATUS") || !strcmp(line, "WIFI")) {
        char buf[96];
        char ip[20];
        wifi_ip(ip, sizeof(ip));
        if (wifi_up()) {
            snprintf(buf, sizeof(buf), "WIFI connected %s %s %d",
                     g_cfg.ssid[0] ? g_cfg.ssid : "?", ip, wifi_rssi());
        } else {
            snprintf(buf, sizeof(buf), "WIFI disconnected");
        }
        reply(pcb, buf);
        return;
    }

    if (!strcmp(line, "WIFI SCAN")) {
        if (g_scanning) {
            reply(pcb, "ERR busy");
        } else {
            wifi_scan_start();
            reply(pcb, "OK scanning");
        }
        return;
    }

    if (!strncmp(line, "WIFI CONNECT ", 13)) {
        const char *arg = line + 13;
        const char *bar = strchr(arg, '|');
        char ssid[33];
        char pass[65];
        size_t sl = bar ? (size_t)(bar - arg) : strlen(arg);
        if (sl >= sizeof(ssid)) sl = sizeof(ssid) - 1;
        memcpy(ssid, arg, sl);
        ssid[sl] = 0;
        const char *pw = bar ? bar + 1 : "";
        strncpy(pass, pw, sizeof(pass) - 1);
        pass[sizeof(pass) - 1] = 0;

        if (!ssid[0]) {
            reply(pcb, "ERR ssid");
            return;
        }
        strncpy(g_cfg.ssid, ssid, sizeof(g_cfg.ssid) - 1);
        g_cfg.ssid[sizeof(g_cfg.ssid) - 1] = 0;
        strncpy(g_cfg.pass, pass, sizeof(g_cfg.pass) - 1);
        g_cfg.pass[sizeof(g_cfg.pass) - 1] = 0;
        g_cfg.magic = CFG_MAGIC;
        cfg_save();

        cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass, CYW43_AUTH_WPA2_AES_PSK);
        reply(pcb, "OK connecting");
        return;
    }

    if (!strcmp(line, "WIFI CLEAR")) {
        memset(&g_cfg, 0, sizeof(g_cfg));
        g_cfg.magic = CFG_MAGIC;
        cfg_save();
        cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
        reply(pcb, "OK");
        return;
    }

    if (!strncmp(line, "LINK", 4) || bang) {
        if (!strcmp(line, "LINK STATUS")) {
            char buf[64];
            char ip[20];
            wifi_ip(ip, sizeof(ip));
            snprintf(buf, sizeof(buf), "LINK ON %d %d", LINK_PORT,
                     g_conns[0].pcb || g_conns[1].pcb ? 1 : 0);
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
    }

    if (!strcmp(line, "HELP") || !strcmp(line, "?")) {
        reply(pcb, "PING | INFO | STATUS | WIFI STATUS");
        reply(pcb, "WIFI SCAN | WIFI CONNECT ssid|pass | WIFI CLEAR");
        reply(pcb, "LINK STATUS | REBOOT | BOOTSEL | HELP");
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
        if (c) {
            c->pcb = NULL;
            c->len = 0;
        }
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);
    u16_t copied = pbuf_copy_partial(p, c->buf + c->len,
                                     (u16_t)(sizeof(c->buf) - 1 - c->len), 0);
    c->len += copied;
    pbuf_free(p);
    c->buf[c->len] = 0;

    // Process complete lines.
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
    if (c->len >= (int)sizeof(c->buf) - 1) c->len = 0;  // runaway line

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
    if (c) {
        c->pcb = NULL;
        c->len = 0;
    }
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

// ---------------------------------------------------------------------------
// USB CDC input
// ---------------------------------------------------------------------------

static char g_usb_buf[CMD_MAX];
static int g_usb_len;

static void usb_poll(void)
{
    int ch;
    while ((ch = getchar_timeout_us(0)) >= 0) {
        if (ch == '\r') continue;
        if (ch == '\n') {
            g_usb_buf[g_usb_len] = 0;
            if (g_usb_len) handle_line(g_usb_buf, NULL);
            g_usb_len = 0;
            continue;
        }
        if (g_usb_len < (int)sizeof(g_usb_buf) - 1)
            g_usb_buf[g_usb_len++] = (char)ch;
        else
            g_usb_len = 0;
    }
}

// ---------------------------------------------------------------------------
// battery / buttons
// ---------------------------------------------------------------------------

static void battery_read_boot(void)
{
    adc_init();
    adc_gpio_init(PIN_BAT);
    adc_select_input(3);
    sleep_ms(3);
    uint16_t raw = adc_read();
    g_bat_mv = (int)((uint32_t)raw * 3300u * 3u / 4095u) + 311;
}

static void buttons_init(void)
{
    const int pins[] = {PIN_BTN_UP, PIN_BTN_LEFT, PIN_BTN_RIGHT, PIN_BTN_DOWN,
                        PIN_BTN_X, PIN_BTN_Y, PIN_BTN_A, PIN_BTN_B};
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_init(pins[i]);
        gpio_set_dir(pins[i], GPIO_IN);
        gpio_pull_up(pins[i]);
    }
}

// ---------------------------------------------------------------------------
// display
// ---------------------------------------------------------------------------

static void screen_set(int row, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_screen[row], sizeof(g_screen[row]), fmt, ap);
    va_end(ap);
}

static void screen_draw(void)
{
    bool changed = !g_screen_valid;
    for (int i = 0; i < 6 && !changed; i++)
        if (strcmp(g_screen[i], g_screen_prev[i])) changed = true;
    if (!changed) return;

    lcd_fill_rect(0, 0, LCD_W, 128, COL_BG);
    for (int i = 0; i < 6; i++) {
        uint16_t col = COL_WHITE;
        if (i == 0) col = COL_CYAN;
        if (i == 2) col = wifi_up() ? COL_GREEN : COL_RED;
        if (i == 5) col = COL_GREY;
        lcd_text(10, 10 + i * 18, g_screen[i], col, COL_BG);
        strncpy(g_screen_prev[i], g_screen[i], sizeof(g_screen_prev[i]) - 1);
        g_screen_prev[i][sizeof(g_screen_prev[i]) - 1] = 0;
    }
    g_screen_valid = true;
}

static void screen_update(void)
{
    char ip[20];
    wifi_ip(ip, sizeof(ip));

    screen_set(0, "WiFi Link %s", APP_VERSION);
    if (wifi_up())
        screen_set(2, "WiFi: %s", g_cfg.ssid[0] ? g_cfg.ssid : "connected");
    else if (cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA) == CYW43_LINK_JOIN)
        screen_set(2, "WiFi: connecting...");
    else
        screen_set(2, "WiFi: offline (use app)");
    screen_set(3, "IP: %s  port %d", ip[0] ? ip : "-", LINK_PORT);
    screen_set(4, "USB: %s", stdio_usb_connected() ? "connected" : "idle");
    screen_set(5, "Battery: %d.%02d V   X: exit", g_bat_mv / 1000,
               (g_bat_mv % 1000) / 10);
    screen_draw();
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(void)
{
    stdio_init_all();
    battery_read_boot();

    gpio_init(PIN_LED);
    gpio_set_dir(PIN_LED, GPIO_OUT);
    gpio_put(PIN_LED, 1);

    lcd_init();
    lcd_text(10, 90, "WiFi Link starting...", COL_WHITE, COL_BG);

    cfg_load();
    buttons_init();

    if (cyw43_arch_init()) {
        lcd_text(10, 110, "CYW43 init failed", COL_RED, COL_BG);
        for (;;) tight_loop_contents();
    }
    cyw43_arch_enable_sta_mode();

    if (g_cfg.ssid[0])
        cyw43_arch_wifi_connect_async(g_cfg.ssid, g_cfg.pass, CYW43_AUTH_WPA2_AES_PSK);

    server_start();

    uint32_t last_screen = 0, last_led = 0;
    bool x_held = false;
    uint32_t x_since = 0;

    for (;;) {
        cyw43_arch_poll();
        usb_poll();

        // Deliver scan results after the driver finishes.
        if (g_scan_done) {
            g_scan_done = false;
            char buf[64];
            char tmp[64];
            snprintf(tmp, sizeof(tmp), "SCAN %d", g_scan_count);
            reply_all(tmp);
            for (int i = 0; i < g_scan_count; i++) {
                snprintf(buf, sizeof(buf), "%ld %s", (long)g_scan[i].rssi, g_scan[i].ssid);
                reply_all(buf);
            }
            reply_all("END");
        }

        uint32_t now = to_ms_since_boot(get_absolute_time());

        if (now - last_screen >= 500) {
            last_screen = now;
            screen_update();
            gpio_put(PIN_LED, ((now / 500) & 1) ? 0 : 1);  // heartbeat
        }

        // X held for 300 ms -> back to the launcher menu.
        bool x = gpio_get(PIN_BTN_X) == 0;
        if (x && !x_held) {
            x_held = true;
            x_since = now;
        } else if (!x) {
            x_held = false;
        } else if (x_held && now - x_since > 300) {
            lcd_fill(COL_BLACK);
            reboot_to_menu();
        }
    }
}
