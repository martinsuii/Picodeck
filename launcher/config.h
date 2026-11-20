// ****************************************************************************
//
//          PicoDeck — project library configuration
//
// ****************************************************************************

#ifndef _CONFIG_H
#define _CONFIG_H

// Final config: boot2 → recovery, launcher at 0x100E0000 (1 MB layout, default)
// or 0x101E0000 (2 MB layout, set -DLAUNCHER_2MB=1 in EXTRA_CFLAGS).

// Enable core1 control (native WiFi module)
#define USE_MULTICORE       1

// Disable USB keyboard simulation — we use real PicoPad keys
#define USE_USBPAD          0

// Enable USB device CDC for debug output (optional)
//#define USE_USB_DEV_CDC   1

// Enable screenshot (Y key combo) — useful during development
#define USE_SCREENSHOT      1

// Size optimizations — disable PicoLibSDK features the launcher doesn't use.
// PWM sound IRQ + init (= ~2 KB), random gen, RTC/calendar.
#define USE_PWMSND          0
#define USE_RAND            0
#define USE_RTC             0
#define USE_CALENDAR        0
#define USE_CALENDAR64      0

// System font height — PicoLibSDK default is 16 (= FontBold8x16). We use
// 8x8 throughout the menu UI. Override BOTH FONT (= used by lib_draw.c
// static init `const u8* pDrawFont = FONT`) and FONTH; this lets
// --gc-sections strip the unused FontBold8x16 (= ~4 KB rodata).
#define FONT                FontBold8x8
#define FONTH               8

// Flash layout selector (addresses are defined further down). Needed here so
// the feature flags below can tell the roomy 2 MB layout from the 1 MB one.
#ifndef LAUNCHER_2MB
  #define LAUNCHER_2MB  0
#endif

// Optional development features. Define LAUNCHER_RELEASE on the build command
// line (= -DLAUNCHER_RELEASE) to disable both for production.
//   - DEBUG_SHELL: USB CDC interactive shell over serial port
//   - USB_MSC:     SD card exposed as USB drive (PC drag-drop file transfer)
// Each feature pulls ~10-15 KB UF2 overhead (USB stack + class driver).
#ifdef LAUNCHER_RELEASE
  #define LAUNCHER_HAS_DEBUG_SHELL 0
  #define LAUNCHER_HAS_USB_MSC     0
#else
  #define LAUNCHER_HAS_DEBUG_SHELL 1
  // USB MSC enabled in dev builds (= experimental). PicoLibSDK MSC source
  // (TODO/) calls UsbXferComplete() which the SDK never defined; we
  // provide a tail-call stub in usb_msc_screen.cpp that re-fires the
  // driver's MscdXferComp completion handler. Functional status TBD.
  #define LAUNCHER_HAS_USB_MSC     1
#endif

// USB device stack base. Always enabled because the Rubber Ducky tool needs a
// USB HID keyboard interface; debug shell / MSC add CDC / MSC on top in dev
// builds. We use the direct UsbDev* APIs — NOT USB_STDIO (= they conflict).
#define USE_USB_DEV         1
#define USE_USB             1
#define USE_RING            1

// USB HID device. Value = number of HID interfaces: the Rubber Ducky presents
// a composite keyboard + mouse so payloads can type and use JIGGLE.
#define USE_USB_DEV_HID     2

// Rubber Ducky tool (SD payload → USB HID keyboard). Always available.
#define LAUNCHER_HAS_RUBBER_DUCKY 1

#if LAUNCHER_HAS_DEBUG_SHELL
// USB CDC class driver (= /dev/ttyACMx, COMx)
#define USE_USB_DEV_CDC     1
#endif

#if LAUNCHER_HAS_USB_MSC
// USB MSC class driver (Mass Storage = SD as USB drive)
#define USE_USB_DEV_MSC     1
#endif

// Companion Link (USB CDC command set + ESP32 WiFi bridge). Shares the CDC
// driver with the debug shell, so the release image pays for the USB serial
// stack either way. Dev images (debug shell + USB MSC) have no spare flash in
// the 1 MB launcher region, so the link is only built into release images.
#ifndef LAUNCHER_HAS_COMPANION
  #ifdef LAUNCHER_RELEASE
    #define LAUNCHER_HAS_COMPANION 1
  #else
    #define LAUNCHER_HAS_COMPANION 0
  #endif
#endif

#if LAUNCHER_HAS_COMPANION
  #define USE_USB_DEV_CDC   1
#endif

// Flash layout: 1 MB (default) or 2 MB.
//   1 MB: launcher at 0x100E0000, recovery at 0x100FB000, settings at
//         0x100FE000, CF2 at 0x100FF000, game ~960 KB.
//         Second 1 MB of the 2 MB flash chip is left untouched so a
//         CircuitPython CIRCUITPY filesystem (0x10100000+) survives.
//   2 MB: launcher at 0x101E0000, recovery at 0x101FB000, settings at
//         0x101FE000, CF2 at 0x101FF000, game ~1.95 MB.
//         Wipes CIRCUITPY data — only use when you don't need it.
// Enable with -DLAUNCHER_2MB=1 on the build line.
#ifndef LAUNCHER_2MB
  #define LAUNCHER_2MB  0
#endif

#if LAUNCHER_2MB
  #define LAUNCHER_FLASH_BASE   0x101E0000u
  #define LAUNCHER_FLASH_END    0x10200000u
  #define LAUNCHER_GAME_HI      0x101E0000u
  #define LAUNCHER_LUT_ADDR     0x101D0000u
  #define LAUNCHER_CF2_ADDR     0x101FF000u
  #define LAUNCHER_RECOVERY_BASE 0x101FB000u
  #define LAUNCHER_STATE_ADDR   0x101FE000u
#else
  #define LAUNCHER_FLASH_BASE   0x100E0000u
  #define LAUNCHER_FLASH_END    0x10100000u
  #define LAUNCHER_GAME_HI      0x100E0000u
  #define LAUNCHER_LUT_ADDR     0x10080000u
  #define LAUNCHER_CF2_ADDR     0x100FF000u
  #define LAUNCHER_RECOVERY_BASE 0x100FB000u
  #define LAUNCHER_STATE_ADDR   0x100FE000u
#endif

// ---------------------------------------------------------------------------
// Custom app slot (see ../app_shared.h for the values apps are built for)
// ---------------------------------------------------------------------------

#if LAUNCHER_2MB
  // Not supported yet: the SDK app linker script targets the 1 MB layout.
  #define LAUNCHER_HAS_APPS 0
#else
  #define LAUNCHER_HAS_APPS 1
  #ifndef __ASSEMBLER__
    #include "../app_shared.h"
  #endif
#endif

// The app strip sits directly below the launcher. Games and WiFi modules are
// never allowed to erase above GAME_ERASE_HI, so installed app code survives
// a game/module flash.
#if LAUNCHER_2MB
  #define GAME_ERASE_HI    LAUNCHER_GAME_HI
#else
  #define GAME_ERASE_HI    APP_SLOT_BASE
#endif

// Shared with app_shared.h. The launcher only compiles apps.cpp for the
// 1 MB layout, so a mismatch here is a build-time error.
#if !LAUNCHER_2MB && !defined(__ASSEMBLER__)
#define APP_SHARED_CHECK   ((LAUNCHER_FLASH_BASE - 0x40000u == APP_SLOT_BASE) ? 1 : -1)
typedef char app_slot_layout_check[APP_SHARED_CHECK];
#endif

// Replace the SDK's printf-based fatal screen with a compact one
// (src/fatal.cpp). Saves ~5 KB of formatting code.
#define USE_FATALERROR      0

#include CONFIG_DEF_H        // default configuration

#endif // _CONFIG_H
