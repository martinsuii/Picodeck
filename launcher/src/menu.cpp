// ****************************************************************************
//
//             PicoDeck — menu state machine
//
// ****************************************************************************

#include "../include.h"
#include "menu.h"
#include "apps.h"
#include "boot_anim.h"
#include "button_test.h"
#include "calculator.h"
#include "cred_app.h"
#include "dev_menu.h"
#include "clock_apps.h"
#include "file_picker.h"
#include "file_manager.h"
#include "fun_apps.h"
#include "native_wifi.h"
#include "gpio_app.h"
#include "image_viewer.h"
#include "midi_pad.h"
#include "paint_app.h"
#include "rubber_ducky.h"
#include "screensaver.h"
#include "sd_upgrade.h"
#include "settings.h"

void WifiMenuRun(void);
#include "snake.h"
#include "sysinfo.h"
#include "theme.h"
#include "themes_app.h"
#include "usb_gamepad.h"
#include "uf2_inspect.h"
#include "uf2_parser.h"
#include "flash_dialog.h"
#include "flasher.h"
#include "arcade_patches.h"
#if LAUNCHER_HAS_COMPANION
#include "companion.h"
#endif
#if LAUNCHER_HAS_DEBUG_SHELL
#include "debug_shell.h"
#endif
#if LAUNCHER_HAS_USB_MSC
#include "usb_msc_screen.h"
#endif

// Menu labels
static const char* const MenuLabels[MENU_COUNT] = {
    "PLAY",
    "LOAD from SD",
    "TOOLS",
    "FUN",
    "APPS",
    "SETTINGS",
};

// Layout constants
#define TITLE_Y       16
#define TITLE2_Y      36
#define DIVIDER_Y     64
#define MENU_START_Y  80
#define MENU_LINE_H   20
#define FOOTER_Y      210
#define ARROW_X       30
#define LABEL_X       60

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int TextWidth(const char* s)
{
    return (int)StrLen(s) * FONTW;
}

static int TextWidth2(const char* s)
{
    return (int)StrLen(s) * FONTW * 2;
}

static void DrawCenteredText(const char* s, int y, u16 color)
{
    DrawText(s, (WIDTH - TextWidth(s)) / 2, y, color);
}

static void DrawCenteredText2(const char* s, int y, u16 color)
{
    DrawText2(s, (WIDTH - TextWidth2(s)) / 2, y, color);
}

// Title honouring the theme's menu_title_scale.
static void DrawThemeTitle(const char* s, int y, u16 color)
{
    if (g_theme.menu_title_scale >= 2) DrawCenteredText2(s, y, color);
    else                               DrawCenteredText(s, y, color);
}

// Small battery indicator in the bottom-right corner of the screen.
// PicoPad uses a Li-ion pack: BATTERY_FULL_INT (4200 mV) = 100%,
// BATTERY_EMPTY_INT (3100 mV) = 0%. LauncherBatMv() is initialized by
// BatInit() in the SDK's DeviceInit().
static void DrawBatteryIndicator(void)
{
    int mv = LauncherBatMv();
    int pct = (mv - BATTERY_EMPTY_INT) * 100 / (BATTERY_FULL_INT - BATTERY_EMPTY_INT);
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;

    u16 col;
    if (pct >= 50)      col = g_theme.ok_color;
    else if (pct >= 20) col = g_theme.warn_color;
    else                col = g_theme.err_color;

    // Percentage label — manual int->string to avoid stdio dependency
    char buf[5];
    int n = 0;
    if (pct >= 100)      { buf[n++] = '1'; buf[n++] = '0'; buf[n++] = '0'; }
    else if (pct >= 10)  { buf[n++] = (char)('0' + pct / 10);
                           buf[n++] = (char)('0' + pct % 10); }
    else                 { buf[n++] = (char)('0' + pct); }
    buf[n++] = '%';
    buf[n]   = 0;

    const int bw = 20;                      // body width
    const int bh = 9;                       // body height
    const int bx = WIDTH - bw - 12;         // icon left
    const int by = HEIGHT - 13;             // icon top

    // Percentage text, right-aligned to the left of the icon
    DrawText(buf, bx - 4 - n * FONTW, by + 1, col);

    // Battery outline + terminal nub
    DrawFrame(bx, by, bw, bh, COL_WHITE);
    DrawRect(bx + bw, by + 2, 2, bh - 4, COL_WHITE);

    // Charge fill (1 px inner margin)
    int fill = (bw - 4) * pct / 100;
    if (fill > 0) DrawRect(bx + 2, by + 2, fill, bh - 4, col);
}

// ---------------------------------------------------------------------------
// Game launch
// ---------------------------------------------------------------------------

Bool IsGameValid(void)
{
    // The game area may contain the native WiFi module (or an app image),
    // which also has a valid-looking vector table. Never treat it as a game.
    if (NativeWifiPresent()) return False;

    volatile uint32_t* vt = (volatile uint32_t*)GAME_VECTOR_TABLE_ADDR;
    uint32_t sp    = vt[0];
    uint32_t entry = vt[1];

    // Erased flash (0xFFFFFFFF) or zeroed sector → no game
    if (sp == 0xFFFFFFFFu || sp == 0u)        return False;
    if (entry == 0xFFFFFFFFu || entry == 0u)  return False;

    // SP must point into RAM
    if (sp < RAM_BASE || sp > RAM_END)        return False;

    // Entry point must be in game flash region with Thumb bit set
    if (entry < GAME_FLASH_LO || entry >= GAME_FLASH_HI) return False;
    if ((entry & 1u) == 0u)                   return False;

    return True;
}

extern "C" void RuntimeTerm(void);
extern "C" void KeyWaitNoPressed(void);

// Magic written to WATCHDOG_SCRATCH[0] just before jumping to the game.
// On launcher boot, presence of this value means "we got here via a watchdog
// reset from a running game" (= soft reset, auto-relaunch). Power-on and
// RUN-pin reset zero the scratch registers (= hard reset → show menu).
#define LAUNCHER_BOOT_MAGIC  0xC0DEBA5Eu

// Hand off control to an image via direct VTOR + jump.
//
// We CANNOT use watchdog vector boot — that path bypasses boot2, which
// means QSPI XIP is not set up, and the CPU can't fetch instructions from
// flash. Watchdog vector boot is intended for RAM-resident images.
//
// Direct jump preserves the XIP setup that our boot2 + launcher's crt0
// established at startup. The target's reset handler will reconfigure clocks
// and peripherals as needed.
//
// terminate = True for games (full RuntimeTerm teardown). Apps use False:
// RuntimeTerm would call BatTerm() and reconfigure GP29, which is the CYW43
// SPI clock while the native WiFi module is running.
static void JumpToVector(u32 sp, u32 entry, u32 vector_addr, Bool terminate)
{
    if (terminate) RuntimeTerm();

    // Disable IRQs while we tear down core state
    __asm volatile ("cpsid i" ::: "memory");

    // Clean ARMv6-M core state. PicoLibSDK app's RuntimeInit + DeviceInit
    // re-configures peripherals from scratch but leaves NVIC IRQ enables
    // and pending bits alone — stale enables from the launcher would let
    // wrong handlers fire on the new vector table.
    *(volatile uint32_t*)0xE000E180u = 0xFFFFFFFFu;  // NVIC_ICER0: disable all IRQs
    *(volatile uint32_t*)0xE000E280u = 0xFFFFFFFFu;  // NVIC_ICPR0: clear pending IRQs
    *(volatile uint32_t*)0xE000E010u = 0;            // SYST_CSR: stop SysTick
    *(volatile uint32_t*)0xE000E014u = 0;            // SYST_RVR: reset reload
    *(volatile uint32_t*)0xE000E018u = 0;            // SYST_CVR: reset current

    // Point VTOR at the chosen vector table. App's reset_handler sets it
    // again from its linker symbol, but for the small window between bx
    // and that store the CPU still uses VTOR for any pending exception.
    *(volatile uint32_t*)0xE000ED08u = vector_addr;

    // Re-enable IRQs. PicoLibSDK app's reset_handler (crt0_rp2040.S) does
    // not call cpsie i — it assumes a fresh power-on state where the I bit
    // is already clear. Our cpsid i above would otherwise mask SysTick
    // exception forever → KeyScan never runs → keys appear dead.
    __asm volatile ("dsb; isb" ::: "memory");
    __asm volatile ("cpsie i" ::: "memory");

    // Set MSP and jump (BX preserves Thumb bit). The target's reset handler
    // takes over from here.
    __asm volatile (
        "msr msp, %0\n"
        "bx  %1\n"
        :: "r"(sp), "r"(entry) : "memory"
    );

    while (1) {}  // unreachable
}

void RunAppImage(u32 vector_addr)
{
    volatile uint32_t* vt = (volatile uint32_t*)vector_addr;
    u32 sp    = vt[0];
    u32 entry = vt[1];

    // Basic sanity before handing over. A bad image should never reach here
    // (AppsLaunch validates first), but stay defensive.
    if (sp < RAM_BASE || sp > RAM_END || (sp & 3u)) return;
    if (entry < vector_addr || entry >= APP_SLOT_END) return;
    if ((entry & 1u) == 0u) return;

    KeyWaitNoPressed();

    // Do NOT set LAUNCHER_BOOT_MAGIC: an app exit/reboot must return to the
    // menu, not auto-launch the game.
    WATCHDOG_SCRATCH[0] = 0;
    WATCHDOG_SCRATCH[APP_SCRATCH_EXIT] = APP_EXIT_MAGIC;
    WATCHDOG_SCRATCH[APP_SCRATCH_RECOVERY] = 0;

    JumpToVector(sp, entry, vector_addr, False);
}

void RunGame(void)
{
    // The game region may hold the native WiFi module instead of a game.
    // Its vector table is valid, so never jump into it as if it were a game.
    if (NativeWifiPresent())
    {
        DrawClearCol(g_theme.menu_bg);
        DrawCenteredText2("WiFi module", 60, g_theme.menu_title);
        DrawCenteredText("No game flashed.", 100, g_theme.menu_text);
        DrawCenteredText("Load a game to replace it.", 120, g_theme.menu_dim);
        DrawCenteredText("Press X to return", 200, g_theme.menu_dim);
        DispUpdate();
        KeyWaitNoPressed();
        while (KeyGet() != KEY_X) WaitMs(50);
        return;
    }

    // Default: vector table at 0x10000100 (= Arcade game or PicoLibSDK
    // loader, depending on what was flashed).
    u32 vector_addr = GAME_VECTOR_TABLE_ADDR;

    // PicoLibSDK app layout: loader at 0x10000100, actual app at 0x10008000.
    // If both have PPAD magic, launching the loader would just show its app
    // browser → user wants the app instead. Redirect to app at 0x10008000.
    const u32 PPAD_MAGIC = 0x44415050u;
    if (*(const volatile u32*)0x100001C0u == PPAD_MAGIC &&
        *(const volatile u32*)0x100080C0u == PPAD_MAGIC) {
        volatile uint32_t* app_vt = (volatile uint32_t*)0x10008000u;
        uint32_t app_sp = app_vt[0];
        uint32_t app_ep = app_vt[1];
        // Validate before redirecting — bad app vector would brick PLAY.
        if (app_sp >= RAM_BASE && app_sp <= RAM_END &&
            app_ep >= 0x10008000u && app_ep < GAME_FLASH_HI &&
            (app_ep & 1u)) {
            vector_addr = 0x10008000u;
        }
    }

    volatile uint32_t* vt = (volatile uint32_t*)vector_addr;
    uint32_t sp    = vt[0];
    uint32_t entry = vt[1];

    // Wait for any held keys (= so player doesn't bounce straight into menu
    // when game finishes if A is still down)
    KeyWaitNoPressed();

    // Mark this boot path as "came via launcher → game". If the game
    // watchdog-resets, the next launcher boot will see this magic and
    // auto-relaunch instead of showing menu.
    WATCHDOG_SCRATCH[0] = LAUNCHER_BOOT_MAGIC;

    JumpToVector(sp, entry, vector_addr, True);
}

// ---------------------------------------------------------------------------
// Reboot to BOOTSEL — RP2040 ROM API call
// ---------------------------------------------------------------------------

void RebootToBootsel(void)
{
    // reset_usb_boot(gpio_mask, interface)
    //   gpio_mask = 0  → no GPIO indicator LED
    //   interface = 0  → both PICOBOOT and MSC interfaces (cold-boot default)
    reset_usb_boot(0, 0);
    // Never returns — RP2040 is now in BOOTSEL mode
    while (1) {}
}

// ---------------------------------------------------------------------------
// Stub screen helper — used by LOAD/USB/SETTINGS until M3/M4/M5
// ---------------------------------------------------------------------------

static void ShowStub(const char* title, const char* milestone)
{
    DrawClearCol(g_theme.menu_bg);
    DrawCenteredText2(title, 30, g_theme.menu_title);
    DrawCenteredText("Not implemented yet", 80, g_theme.menu_text);
    DrawCenteredText("Coming in milestone:", 100, g_theme.menu_dim);
    DrawCenteredText(milestone, 120, g_theme.menu_text);
    DrawCenteredText("Press X to return", 200, g_theme.menu_dim);
    DispUpdate();

    // Wait for KEY_X
    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_X) return;
    }
}

// ---------------------------------------------------------------------------
// Menu rendering
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// SD / USB connection status icons (below the game status line)
// ---------------------------------------------------------------------------

#define STATUS_ICON_Y   222
#define SD_ICON_X       20
#define USB_ICON_X      52
#define WIFI_ICON_X     84
#define WEATHER_X       112

static Bool g_sd_present = False;
static Bool g_usb_connected = False;
static u8   g_sd_verify_buf[512];

// Small microSD-card glyph (chamfered corner + contact lines).
static void DrawSdIcon(int x, int y, u16 col)
{
    DrawLine(x + 3,  y,      x + 11, y,      col);
    DrawLine(x + 11, y,      x + 11, y + 13, col);
    DrawLine(x + 11, y + 13, x,      y + 13, col);
    DrawLine(x,      y + 13, x,      y + 3,  col);
    DrawLine(x,      y + 3,  x + 3,  y,      col);
    DrawRect(x + 4, y + 9,  6, 1, col);
    DrawRect(x + 4, y + 11, 6, 1, col);
}

// Small USB trident glyph.
static void DrawUsbIcon(int x, int y, u16 col)
{
    DrawRect(x + 6, y + 2, 2, 10, col);              // stem
    DrawLine(x + 3,  y + 4, x + 7,  y + 1, col);     // arrow head
    DrawLine(x + 7,  y + 1, x + 11, y + 4, col);
    DrawFillCircle(x + 7, y + 13, 2, col);           // bottom circle
    DrawLine(x + 6, y + 6, x + 2, y + 8, col);       // left branch
    DrawFillCircle(x + 2, y + 8, 2, col);
    DrawLine(x + 8, y + 8, x + 11, y + 10, col);     // right branch
    DrawRect(x + 10, y + 10, 4, 4, col);
}

// Small Wi-Fi glyph (two arcs + dot).
static void DrawWifiIcon(int x, int y, u16 col)
{
    DrawFillCircle(x + 6, y + 11, 1, col);
    DrawLine(x + 2, y + 7,  x + 6,  y + 4, col);
    DrawLine(x + 6, y + 4,  x + 10, y + 7, col);
    DrawLine(x,     y + 3,  x + 6,  y,     col);
    DrawLine(x + 6, y,      x + 12, y + 3, col);
}

static void DrawStatusIcons(void)
{
    DrawRect(SD_ICON_X - 2, STATUS_ICON_Y - 2,
             WIDTH - SD_ICON_X - 56, 16, g_theme.menu_bg);
    DrawSdIcon(SD_ICON_X,   STATUS_ICON_Y,
               g_sd_present    ? g_theme.ok_color : g_theme.menu_dim);
    DrawUsbIcon(USB_ICON_X, STATUS_ICON_Y,
               g_usb_connected ? g_theme.ok_color : g_theme.menu_dim);
    DrawWifiIcon(WIFI_ICON_X, STATUS_ICON_Y,
                 NativeWifiLinkUp() ? g_theme.ok_color : g_theme.menu_dim);

}


// Poll SD presence and USB mount state (called every ~2 s from the menu loop).
static void MenuUpdateStatus(void)
{
    static u32 last_sd_try = 0;
    u32 now = Millis();

    g_usb_connected = UsbDevCdcIsMounted() || RubberDuckyUsbConnected();

    // Prefer the SD library's own state (a FAT mount may have connected it).
    if (DiskValid() || g_sd_present)
    {
        // Cheap liveness check: read sector 0. Fails once the card is removed.
        if (SDReadSect(0, g_sd_verify_buf))
            g_sd_present = True;
        else
        {
            g_sd_present = False;
            SDDisconnect();
        }
    }
    else if ((u32)(now - last_sd_try) >= 10000)
    {
        // Probe for a newly inserted card, but only every 10 s. SDConnect can
        // block for a while when the slot is empty, which made the main menu
        // feel laggy while submenus were fine.
        last_sd_try = now;
        if (SDConnect()) g_sd_present = True;
    }
}

static void RenderMenu(int selected, Bool game_valid)
{
    DrawClearCol(g_theme.menu_bg);

    // Title
    DrawThemeTitle(g_theme.home_title1, g_theme.menu_title_y,  g_theme.menu_title);
    DrawThemeTitle(g_theme.home_title2, g_theme.menu_title2_y, g_theme.menu_title);

    // NTP time (top-right)

    // Divider line under title
    DrawRect(40, g_theme.menu_divider_y, WIDTH - 80, 1, g_theme.menu_dim);

    // Menu items
    for (int i = 0; i < MENU_COUNT; i++)
    {
        int y = g_theme.menu_start_y + i * g_theme.menu_line_h;

        u16 color;
        const char* arrow = "";

        if (i == selected)
        {
            color = g_theme.menu_sel;
            switch (g_theme.menu_sel_style)
            {
            case 0:   // highlight bar + arrow
                DrawRect(LABEL_X - 6, y - 2, WIDTH - 2 * LABEL_X + 12,
                         FONTH + 4, g_theme.menu_hl);
                arrow = ">";
                break;
            case 1:   // arrow only
                arrow = ">";
                break;
            case 2:   // inverted bar
                DrawRect(LABEL_X - 6, y - 2, WIDTH - 2 * LABEL_X + 12,
                         FONTH + 4, g_theme.menu_sel);
                color = g_theme.menu_bg;
                break;
            default:  // colour only
                break;
            }
        }
        else if (i == MENU_PLAY && !game_valid)
        {
            // Disabled state for PLAY when no game flashed
            color = g_theme.menu_dim;
            arrow = " ";
        }
        else
        {
            color = g_theme.menu_text;
            arrow = " ";
        }

        DrawText(arrow, ARROW_X, y, color);
        DrawText(MenuLabels[i], LABEL_X, y, color);
    }

    if (g_theme.menu_show_footer)
    {
        int fy = g_theme.menu_footer_y;

        // Footer divider + game status
        DrawRect(20, fy - 8, WIDTH - 40, 1, g_theme.menu_dim);

        if (NativeWifiPresent()) {
            DrawText("WiFi module installed", 20, fy, g_theme.accent_color);
        } else if (game_valid) {
            // PPAD magic at vector_table+0xC0 marks PicoLibSDK loader (= boot
            // variant at 0x10000100) and apps (= default variant at 0x10008000).
            const u32 PPAD_MAGIC = 0x44415050u;
            Bool has_loader = (*(const volatile u32*)0x100001C0u == PPAD_MAGIC);
            Bool has_app    = (*(const volatile u32*)0x100080C0u == PPAD_MAGIC);
            if (has_loader && has_app) {
                DrawText("App: PicoLibSDK", 20, fy, g_theme.ok_color);
            } else if (has_loader) {
                DrawText("Loader: PicoLibSDK (no app)", 20, fy, g_theme.accent_color);
            } else {
                char name[32];
                if (PicoDeckReadName(name, sizeof(name))) {
                    char buf[48];
                    strcpy(buf, "Game: ");
                    strcat(buf, name);
                    DrawText(buf, 20, fy, g_theme.ok_color);
                } else {
                    DrawText("Game OK (unknown name)", 20, fy, g_theme.ok_color);
                }
            }
        } else {
            DrawText("No game flashed", 20, fy, g_theme.menu_dim);
        }
    }

    if (g_theme.menu_show_status)
    {
        DrawStatusIcons();
        DrawBatteryIndicator();
    }

    DispUpdate();
}

// ---------------------------------------------------------------------------
// Submenus (TOOLS / SETTINGS)
// ---------------------------------------------------------------------------

typedef void (*ToolFunc)(void);

static void FlashSettingsMenuRun(void);   // nested SETTINGS submenu

// --- TOOLS ----------------------------------------------------------------
// Add new tools here (label + entry point); TOOL_COUNT follows automatically.
static const char* const ToolLabels[] = {
    "GPIO Control",
    "Rubber Ducky",
    "SD File Manager",
    "Image Viewer",
    "WiFi",
    "Button Test",
    "UF2 Inspector",
#if LAUNCHER_HAS_COMPANION
    "Companion Link",
#endif
#if LAUNCHER_HAS_USB_MSC
    "SD CARD READER",
#endif
#if LAUNCHER_HAS_DEBUG_SHELL
    "DEBUG SHELL",
#endif
};
static ToolFunc const ToolFuncs[] = {
    GpioAppRun,
    RubberDuckyRun,
    FileManagerRun,
    ImageViewerRun,
    WifiMenuRun,
    ButtonTestRun,
    Uf2InspectRun,
#if LAUNCHER_HAS_COMPANION
    CompanionLinkRun,
#endif
#if LAUNCHER_HAS_USB_MSC
    UsbMscScreenRun,
#endif
#if LAUNCHER_HAS_DEBUG_SHELL
    DebugShellRun,
#endif
};
#define TOOL_COUNT ((int)(sizeof(ToolLabels) / sizeof(ToolLabels[0])))

// --- FUN ------------------------------------------------------------------
static const char* const FunLabels[] = {
    "Snake",
    "USB Gamepad",
    "Mouse Jiggler",
    "Media Remote",
    "Keyboard Pranks",
    "Paint",
    "Calculator",
    "Stopwatch",
    "Timer",
    "MIDI Pad",
};
static ToolFunc const FunFuncs[] = {
    SnakeRun,
    UsbGamepadRun,
    MouseJigglerRun,
    MediaRemoteRun,
    KeyboardPranksRun,
    PaintAppRun,
    CalculatorRun,
    StopwatchRun,
    TimerRun,
    MidiPadRun,
};
#define FUN_COUNT ((int)(sizeof(FunLabels) / sizeof(FunLabels[0])))

// --- Companion Link app registry ------------------------------------------
// Only apps that keep their hands off the USB device stack are listed: the
// companion owns USB CDC while it is open, so HID/MIDI tools are excluded.
#if LAUNCHER_HAS_COMPANION
typedef struct {
    const char* label;
    ToolFunc    fn;
} CompanionApp;

static const CompanionApp CompanionApps[] = {
    { "GPIO Control",    GpioAppRun },
    { "SD File Manager", FileManagerRun },
    { "Image Viewer",    ImageViewerRun },
    { "Button Test",     ButtonTestRun },
    { "UF2 Inspector",   Uf2InspectRun },
    { "Themes",          ThemesRun },
    { "System Info",     SysInfoRun },
    { "Backlight",       BacklightRun },
    { "Snake",           SnakeRun },
    { "Paint",           PaintAppRun },
    { "Calculator",      CalculatorRun },
    { "Stopwatch",       StopwatchRun },
    { "Timer",           TimerRun },
};
#define COMPANION_APP_COUNT \
    ((int)(sizeof(CompanionApps) / sizeof(CompanionApps[0])))

int CompanionAppCount(void) { return COMPANION_APP_COUNT; }

const char* CompanionAppLabel(int index)
{
    if (index < 0 || index >= COMPANION_APP_COUNT) return NULL;
    return CompanionApps[index].label;
}

Bool CompanionAppLaunch(int index)
{
    if (index < 0 || index >= COMPANION_APP_COUNT) return False;
    CompanionApps[index].fn();
    return True;
}
#endif

// --- SETTINGS -------------------------------------------------------------
// The visible list is built at runtime: developer mode adds one entry.
static const char* const SettingBaseLabels[] = {
    "System Info",
    "Backlight",
    "Themes",
    "WIFI",
    "Flash settings",
    "Update from SD",
    "Reboot to BOOTSEL",
};
static ToolFunc const SettingBaseFuncs[] = {
    SysInfoRun,
    BacklightRun,
    ThemesRun,
    WifiMenuRun,
    FlashSettingsMenuRun,
    SdUpgradeRun,
    RebootToBootsel,
};
#define SETTING_BASE_COUNT \
    ((int)(sizeof(SettingBaseLabels) / sizeof(SettingBaseLabels[0])))

// --- SETTINGS > FLASH SETTINGS -------------------------------------------
static const char* const FlashSettingLabels[] = {
    "Flasher trasher",
    "Flash backup",
};
static ToolFunc const FlashSettingFuncs[] = {
    FlasherTrashGame,
    FlasherBackupRun,
};
#define FLASH_SETTING_COUNT \
    ((int)(sizeof(FlashSettingLabels) / sizeof(FlashSettingLabels[0])))

// Number of list rows that fit between the theme's list start and the footer
// (or the bottom of the screen when the footer is hidden). Themes that use a
// taller line height automatically page fewer items.
static int SubVisible(void)
{
    int bottom = HEIGHT - 8;
    if (g_theme.menu_show_footer) bottom = g_theme.menu_footer_y - 10;

    int n = (bottom - g_theme.menu_start_y - FONTH) / g_theme.menu_line_h + 1;
    if (n < 1)  n = 1;
    if (n > 20) n = 20;
    return n;
}

static void RenderSubMenu(const char* title,
                          const char* const* labels, int count, int selected)
{
    DrawClearCol(g_theme.menu_bg);

    DrawThemeTitle(title, g_theme.menu_sub_title_y, g_theme.menu_title);
    DrawRect(40, g_theme.menu_divider_y, WIDTH - 80, 1, g_theme.menu_dim);

    const int vis = SubVisible();
    int page  = selected / vis;
    int first = page * vis;
    int last  = first + vis;
    if (last > count) last = count;

    for (int i = first; i < last; i++)
    {
        int y = g_theme.menu_start_y + (i - first) * g_theme.menu_line_h;
        Bool sel = (i == selected);
        u16 color = sel ? g_theme.menu_sel : g_theme.menu_text;
        const char* arrow = sel ? ">" : " ";

        if (sel)
        {
            switch (g_theme.menu_sel_style)
            {
            case 0:
                DrawRect(LABEL_X - 6, y - 2, WIDTH - 2 * LABEL_X + 12,
                         FONTH + 4, g_theme.menu_hl);
                break;
            case 1:
                break;
            case 2:
                DrawRect(LABEL_X - 6, y - 2, WIDTH - 2 * LABEL_X + 12,
                         FONTH + 4, g_theme.menu_sel);
                color = g_theme.menu_bg;
                arrow = " ";
                break;
            default:
                arrow = " ";
                break;
            }
        }

        DrawText(arrow, ARROW_X, y, color);
        DrawText(labels[i], LABEL_X, y, color);
    }

    if (g_theme.menu_show_footer)
    {
        int fy = g_theme.menu_footer_y;
        DrawRect(20, fy - 8, WIDTH - 40, 1, g_theme.menu_dim);

        if (count > vis)
        {
            // Page indicator + scroll arrows
            int pages = (count + vis - 1) / vis;
            char buf[16];
            char num[8];
            strcpy(buf, "L/R  ");
            DecNum(num, page + 1, 0); strcat(buf, num);
            strcat(buf, "/");
            DecNum(num, pages, 0); strcat(buf, num);
            DrawText(buf, 20, fy, g_theme.menu_dim);

            int hx = WIDTH - 20 - (int)StrLen(g_theme.menu_help) * FONTW;
            if (hx < 70) hx = 70;   // keep clear of the page indicator
            DrawText(g_theme.menu_help, hx, fy, g_theme.menu_dim);
        }
        else
        {
            DrawText(g_theme.menu_help, 20, fy, g_theme.menu_dim);
        }
    }

    DispUpdate();
}

static void SubMenuRun(const char* title,
                       const char* const* labels,
                       ToolFunc const* funcs, int count)
{
    int selected = 0;
    Bool need_redraw = True;
    const int vis = SubVisible();

    while (1)
    {
        if (need_redraw)
        {
            RenderSubMenu(title, labels, count, selected);
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
            // Previous page (keep position within the page, clamp at the top)
            if (selected >= vis)
            {
                selected -= vis;
                need_redraw = True;
            }
            break;

        case KEY_RIGHT:
        {
            // Next page (keep position within the page, clamp at the bottom)
            int ns = selected + vis;
            if (ns >= count) ns = count - 1;
            if (ns != selected)
            {
                selected = ns;
                need_redraw = True;
            }
            break;
        }

        case KEY_A:
            funcs[selected]();   // returns when the tool exits
            need_redraw = True;
            break;

        case KEY_X:
            return;

        default:
            break;
        }
    }
}

static void ToolsMenuRun(void)
{
    SubMenuRun("TOOLS", ToolLabels, ToolFuncs, TOOL_COUNT);
}

static void FunMenuRun(void)
{
    SubMenuRun("FUN", FunLabels, FunFuncs, FUN_COUNT);
}

static void FlashSettingsMenuRun(void)
{
    SubMenuRun("FLASH SETTINGS", FlashSettingLabels, FlashSettingFuncs,
               FLASH_SETTING_COUNT);
}

static void SettingsMenuRun(void)
{
    static const char* labels[SETTING_BASE_COUNT + 1];
    static ToolFunc funcs[SETTING_BASE_COUNT + 1];

    int n = 0;
    for (int i = 0; i < SETTING_BASE_COUNT; i++)
    {
        labels[n] = SettingBaseLabels[i];
        funcs[n]  = SettingBaseFuncs[i];
        n++;
    }

    // Developer mode is a file on the SD card, so this is re-evaluated every
    // time SETTINGS opens. No reboot needed.
    if (DevModeEnabled())
    {
        labels[n] = "Credential manager";
        funcs[n]  = CredentialRun;
        n++;
    }

    SubMenuRun("SETTINGS", labels, funcs, n);
}

// ---------------------------------------------------------------------------
// Main menu loop
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Low battery warning
// ---------------------------------------------------------------------------

#define LOW_BATTERY_PCT  15
static Bool g_low_batt_warned = False;

static void MenuCheckLowBattery(void)
{
    if (g_low_batt_warned) return;

    int mv = LauncherBatMv();
    int pct = (mv - BATTERY_EMPTY_INT) * 100
              / (BATTERY_FULL_INT - BATTERY_EMPTY_INT);
    if (pct > LOW_BATTERY_PCT) return;

    g_low_batt_warned = True;

    DrawClearCol(g_theme.menu_bg);
    DrawText2("LOW BATTERY", (WIDTH - 11 * FONTW * 2) / 2, 50,
              g_theme.err_color);

    char buf[32];
    char num[8];
    strcpy(buf, "");
    DecNum(num, pct, 0); strcat(buf, num);
    strcat(buf, "%   ");
    DecNum(num, mv, 0); strcat(buf, num);
    strcat(buf, " mV");
    DrawText(buf, (WIDTH - (int)StrLen(buf) * FONTW) / 2, 110,
             g_theme.menu_text);

    DrawText("Please charge the PicoPad.",
             (WIDTH - 26 * FONTW) / 2, 145, g_theme.menu_text);
    DrawText("Press any button", (WIDTH - 16 * FONTW) / 2, 205,
             g_theme.menu_dim);
    DispUpdate();

    // Show for at least 2 s, then wait for a button.
    u32 start = Millis();
    while ((u32)(Millis() - start) < 2000) WaitMs(50);
    while (KeyGet() == NOKEY) WaitMs(50);
    while (KeyGet() != NOKEY) WaitMs(20);
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

// Called by an app's AppExit() through the launcher API below. The app has
// already saved our RAM into LAUNCHER_SNAPSHOT_ADDR; this copies it back and
// re-enters the menu without a chip reset.
extern "C" void LauncherResumeStub(void)
{
    __asm volatile ("cpsid i" ::: "memory");

    volatile const u32* src = (volatile const u32*)LAUNCHER_SNAPSHOT_ADDR;
    volatile u32* dst = (volatile u32*)0x20000000u;

    for (u32 i = 0; i < LAUNCHER_SNAPSHOT_SIZE / 4; i++)
        dst[i] = src[i];

    *(volatile u32*)0xE000ED08u = (u32)RamVectorTable;   // launcher vectors
    *(volatile u32*)0xE000E280u = 0xFFFFFFFFu;           // clear pending IRQs

    WATCHDOG_SCRATCH[APP_SCRATCH_RESUME] = APP_RESUME_MAGIC;
    WATCHDOG_SCRATCH[APP_SCRATCH_SNAPSHOT] = 0;

    __asm volatile ("dsb; isb" ::: "memory");
    __asm volatile ("cpsie i" ::: "memory");

    __asm volatile (
        "msr msp, %0\n"
        "bx  %1\n"
        :: "r"(0x20041000u), "r"(&MenuRun) : "memory"
    );

    while (1) {}
}

typedef struct {
    u32 magic;
    u32 version;
    u32 resume;
    u32 snapshot;
    u32 size;
    u32 vector;
} LauncherApi;

// Scanned by the app SDK (AppExit).
const LauncherApi g_launcher_api __attribute__((used, section(".rodata"))) = {
    APP_LAUNCHER_MAGIC, 1u, (u32)LauncherResumeStub,
    LAUNCHER_SNAPSHOT_ADDR, LAUNCHER_SNAPSHOT_SIZE, (u32)RamVectorTable
};

static Bool FreshMarkerExists(void)
{
    return FileExist("/FRESH.TXT") || FileExist("/.fresh") || FileExist("/FRESH");
}

static void TutorialPage(int page)
{
    static const char* const titles[] = {
        "Welcome to PicoDeck",
        "Apps",
        "Arcade",
        "Themes",
        "Backups and Conf",
        "You're all set",
    };
    static const char* const lines[][3] = {
        { "Your SD card is set up.", "This quick tour shows what", "each folder is for." },
        { "Custom apps live in Apps.", "Install them in APPS >", "App Store > From SD card." },
        { "MakeCode Arcade games go in", "Arcade. Use LOAD from SD to", "flash one, then PLAY." },
        { "Themes are .thm files.", "Choose one in", "SETTINGS > Themes." },
        { "Backups holds game backups.", "Conf holds updates and", "configuration files." },
        { "Delete FRESH.TXT to skip", "this tour next time.", "Have fun!" },
    };
    int n = (int)(sizeof(titles) / sizeof(titles[0]));

    DrawClearCol(g_theme.menu_bg);
    DrawText2(titles[page], (WIDTH - (int)StrLen(titles[page]) * FONTW * 2) / 2,
              22, g_theme.menu_title);
    DrawRect(24, 48, WIDTH - 48, 1, g_theme.menu_dim);

    for (int i = 0; i < 3; i++)
        DrawText(lines[page][i], 24, 78 + i * 20, g_theme.menu_text);

    char pbuf[16];
    strcpy(pbuf, "Page ");
    DecNum(pbuf + 5, (u32)(page + 1), 0);
    strcat(pbuf, " / ");
    DecNum(pbuf + StrLen(pbuf), (u32)n, 0);
    DrawText(pbuf, 24, 196, g_theme.menu_dim);

    DrawText("A: next    X: skip", 24, 220, g_theme.menu_dim);
    DispUpdate();
}

static void ShowFirstBootTutorial(void)
{
    if (!FreshMarkerExists()) return;

    int n = 6;
    for (int page = 0; page < n; page++)
    {
        TutorialPage(page);
        while (1)
        {
            u8 k = KeyGet();
            if (k == KEY_A) break;
            if (k == KEY_X) { page = n; break; }
            WaitMs(30);
        }
    }

    // Only show once.
    FileDelete("/FRESH.TXT");
    FileDelete("/.fresh");
    FileDelete("/FRESH");
}

void MenuRun(void)
{
    Bool resume = (WATCHDOG_SCRATCH[APP_SCRATCH_RESUME] == APP_RESUME_MAGIC);

    if (resume)
    {
        // RAM is already restored; the WiFi module kept running. Just bring
        // the core-0 timers/display back into the launcher's world.
        WATCHDOG_SCRATCH[APP_SCRATCH_RESUME] = 0;
        WATCHDOG_SCRATCH[APP_SCRATCH_SNAPSHOT] = 0;
        SysTickInit();
        DispBacklight(SettingsBacklight());
        DispDirtyAll();
    }
    else
    {
    // Soft reset (= watchdog reset from a previously-running game) →
    // auto-relaunch the game. Hard reset (power-on, RESET pin) clears
    // WATCHDOG_SCRATCH → falls through to the menu.
    Bool soft_reset = (WATCHDOG_SCRATCH[0] == LAUNCHER_BOOT_MAGIC);
    WATCHDOG_SCRATCH[0] = 0;
    // App exit/reboot markers are informational; recovery clears its own
    // request when it shows the menu.
    WATCHDOG_SCRATCH[APP_SCRATCH_EXIT] = 0;
    WATCHDOG_SCRATCH[APP_SCRATCH_RESUME] = 0;
    WATCHDOG_SCRATCH[APP_SCRATCH_SNAPSHOT] = 0;
    if (soft_reset && IsGameValid()) {
        RunGame();  // never returns
    }

    // Load the SD-card theme (falls back to defaults) before the splash.
    ThemeLoad();

    // Cosmetic boot splash (skipped when auto-launching a game above).
    BootAnimation();

    // No SD card: say so plainly after the boot splash. The launcher still
    // runs (WiFi and tools work), but games/themes/apps need the card.
    if (!DiskValid())
    {
        DrawClearCol(g_theme.menu_bg);
        DrawCenteredText2("No SD card", 44, g_theme.warn_color);
        DrawRect(40, 78, WIDTH - 80, 1, g_theme.menu_dim);

        DrawCenteredText("Games, themes and apps need one.", 100, g_theme.menu_text);
        DrawCenteredText("Insert a card and restart, or carry on", 122, g_theme.menu_dim);
        DrawCenteredText("without it.", 138, g_theme.menu_dim);
        DrawCenteredText("WiFi and built-in tools still work.", 168, g_theme.menu_text);
        DrawCenteredText("Press any button", 210, g_theme.menu_dim);
        DispUpdate();

        u32 start = Millis();
        while ((u32)(Millis() - start) < 5000)
        {
            if (KeyGet() != NOKEY) break;
            WaitMs(50);
        }
        while (KeyGet() != NOKEY) WaitMs(20);
    }

    ShowFirstBootTutorial();

    // Start the native WiFi module (no-op when not installed) and seed it
    // from /wifi.cfg. Retried on the status tick if the radio is still loading.
    NativeWifiStart();
    if (NativeWifiFailed()) WifiBsodShow();
    NativeWifiSeedFromSd();

    // Present as a USB CDC device so the desktop/Android companion can
    // connect at any time. Tools that need HID (Rubber Ducky, MIDI, ...)
    // re-init USB themselves and the menu restores CDC when they return.
#if LAUNCHER_HAS_COMPANION
    CompanionUsbInit();
#else
    RubberDuckyUsbInit();
#endif
    }

    int selected = MENU_PLAY;
    Bool need_redraw = True;
    Bool wifi_seeded = False;
    u32 t_status = Millis();
    u32 t_battery = Millis();
    u32 last_activity = Millis();

    while (1)
    {
#if LAUNCHER_HAS_COMPANION
        // Always-on companion service; this is what lets the desktop app
        // talk to the PicoPad without opening the Companion Link screen.
        CompanionPoll();
#endif

        // Periodic refresh: SD + USB every 2 s, battery every 5 s.
        u32 now = Millis();
        if ((u32)(now - t_status) >= 2000)
        {
            t_status = now;
            NativeWifiPublishBattery();
            if (!wifi_seeded && NativeWifiReady()) {
                NativeWifiSeedFromSd();
                wifi_seeded = True;
            }
            MenuUpdateStatus();
            DrawStatusIcons();
                    DispUpdate();
        }
        if ((u32)(now - t_battery) >= 5000)
        {
            t_battery = now;
            DrawBatteryIndicator();
            DispUpdate();
            MenuCheckLowBattery();
            last_activity = Millis();
        }

        // Screensaver after the theme's idle timeout (default 30 s, 0 = off).
        // Use a fresh timestamp here: last_activity may have been updated
        // after `now` was captured above, and the subtraction would underflow.
        u32 idle_ms = (u32)g_theme.saver_timeout * 1000u;
        if (idle_ms && (u32)(Millis() - last_activity) >= idle_ms)
        {
            ScreensaverRun();
            last_activity = Millis();
            t_status = last_activity;
            t_battery = last_activity;
            need_redraw = True;
            continue;
        }

        Bool game_valid = IsGameValid();

        // If selected is PLAY but no game, jump to next valid item on first entry
        if (selected == MENU_PLAY && !game_valid && need_redraw)
        {
            selected = MENU_LOAD;
        }

        if (need_redraw)
        {
            RenderMenu(selected, game_valid);
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY)
        {
            WaitMs(20);
            continue;
        }

        last_activity = Millis();

        switch (k)
        {
        case KEY_UP:
            do {
                selected = (selected - 1 + MENU_COUNT) % MENU_COUNT;
            } while (selected == MENU_PLAY && !game_valid);
            need_redraw = True;
            break;

        case KEY_DOWN:
            do {
                selected = (selected + 1) % MENU_COUNT;
            } while (selected == MENU_PLAY && !game_valid);
            need_redraw = True;
            break;

        case KEY_Y:    // manually show the screensaver
            ScreensaverRun();
            last_activity = Millis();
            t_status = last_activity;
            t_battery = last_activity;
            need_redraw = True;
            break;

        case KEY_A:    // confirm / select
            switch (selected)
            {
            case MENU_PLAY:
                if (game_valid) RunGame(); // never returns
                break;
#if LAUNCHER_HAS_APPS
            case MENU_APPS:
                AppsMenuRun(); // returns on X
#if LAUNCHER_HAS_COMPANION
                if (!UsbDevCdcIsMounted()) CompanionUsbInit();
#endif
                need_redraw = True;
                break;
#endif
            case MENU_LOAD:
            {
                char selected_path[256];
                PickerResult r = FilePickerRun("/", "*.UF2",
                                               selected_path, sizeof(selected_path));
                if (r == PICKER_RESULT_SELECTED)
                {
                    // Parse UF2 metadata
                    Uf2Info info;
                    if (!Uf2ReadInfo(selected_path, &info))
                    {
                        DrawClearCol(g_theme.menu_bg);
                        DrawText2("Invalid UF2", 60, 30, g_theme.err_color);
                        DrawText("File is not a valid UF2 image", 10, 90, g_theme.menu_text);
                        DrawText("Press X to return", 10, 200, g_theme.menu_dim);
                        DispUpdate();
                        while (KeyGet() != KEY_X) {}
                        need_redraw = True;
                        break;
                    }

                    // Defaults — PicoDeck preset
                    FlashOptions opts;
                    opts.preserve_launcher = True;
                    opts.apply_st7789_init = True;
                    opts.apply_palette_lut = True;
                    opts.apply_cf2         = True;
                    opts.swap_yb           = False;

                    // Confirmation dialog
                    DialogResult d = FlashDialogShow(selected_path, &info, &opts);
                    if (d == DLG_PROCEED)
                    {
                        FlashResult fr;
                        if (info.kind == UF2_TARGET_RAM) {
                            fr = FlasherRunRam(selected_path, &info);
                            // RunRam never returns on success
                        } else if (info.kind == UF2_TARGET_FLASH) {
                            fr = FlasherWriteFlash(selected_path, &info, &opts);
                        } else {
                            fr = FLASH_ERR_INVALID;
                        }

                        // If we get here, flash failed — show error
                        const char* err;
                        switch (fr) {
                        case FLASH_ERR_OPEN:        err = "Cannot open UF2"; break;
                        case FLASH_ERR_INVALID:     err = "Invalid UF2 data"; break;
                        case FLASH_ERR_TOO_BIG:     err = "Image too big"; break;
                        case FLASH_ERR_PROTECTED:   err = "Overlaps protected region"; break;
                        case FLASH_ERR_WRITE:       err = "Flash write failed"; break;
                        case FLASH_ERR_INTERRUPTED: err = "Cancelled by user"; break;
                        default:                    err = "Unknown error"; break;
                        }
                        DrawClearCol(g_theme.menu_bg);
                        DrawText2("Error", 110, 30, g_theme.err_color);
                        DrawText(err, 10, 90, g_theme.menu_text);
                        DrawText("Press X to return", 10, 200, g_theme.menu_dim);
                        DispUpdate();
                        while (KeyGet() != KEY_X) {}
                    }
                }
                need_redraw = True;
                break;
            }
            case MENU_TOOLS:
                ToolsMenuRun(); // returns on X
#if LAUNCHER_HAS_COMPANION
                if (!UsbDevCdcIsMounted()) CompanionUsbInit();
#endif
                need_redraw = True;
                break;

            case MENU_FUN:
                FunMenuRun(); // returns on X
#if LAUNCHER_HAS_COMPANION
                if (!UsbDevCdcIsMounted()) CompanionUsbInit();
#endif
                need_redraw = True;
                break;

            case MENU_SETTINGS:
                SettingsMenuRun(); // returns on X
#if LAUNCHER_HAS_COMPANION
                if (!UsbDevCdcIsMounted()) CompanionUsbInit();
#endif
                need_redraw = True;
                break;
            }
            break;

        default:
            break;
        }

        // Reset the idle timer after (possibly long) submenu sessions.
        last_activity = Millis();
    }
}
