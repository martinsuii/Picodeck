// ****************************************************************************
//
//          PicoPad Recovery — minimal SDK configuration
//
// ****************************************************************************

#ifndef _RECOVERY_CONFIG_H
#define _RECOVERY_CONFIG_H

// Recovery runs from its own partition in the tail of the launcher region.
// Keep it small: no USB stack, no sound, no SD/FAT, no random, no calendar.
#define USE_USBPAD          0
#define USE_SCREENSHOT      0
#define USE_PWMSND          0
#define USE_RAND            0
#define USE_SD              0
#define USE_FAT             0
// No fatal-error screen — it pulls in the 8x16 font (4 KB of flash) that the
// recovery image never otherwise uses.
#define USE_FATALERROR      0
#define USE_RTC             0
#define USE_CALENDAR        0
#define USE_CALENDAR64      0

// Same 8x8 font as the launcher so the menu looks identical.
#define FONT                FontBold8x8
#define FONTH               8

// No USB device — BOOTSEL is entered through the ROM reset_usb_boot() API.
#define USE_USB             0
#define USE_USB_DEV         0
#define USE_USB_DEV_CDC     0
#define USE_USB_DEV_MSC     0
#define USE_USB_DEV_HID     0
#define USE_RING            1

// Flash layout mirrors the launcher with the recovery image in the tail.
#ifndef RECOVERY_2MB
  #define RECOVERY_2MB 0
#endif

#if RECOVERY_2MB
  #define RECOVERY_FLASH_BASE  0x101FB000u
  #define RECOVERY_FLASH_END   0x101FE000u
  #define LAUNCHER_FLASH_BASE  0x101E0000u
#else
  #define RECOVERY_FLASH_BASE  0x100FB000u
  #define RECOVERY_FLASH_END   0x100FE000u
  #define LAUNCHER_FLASH_BASE  0x100E0000u
#endif

// Watchdog scratch slot used by the launcher for game auto-launch.
#define LAUNCHER_BOOT_MAGIC  0x4C41554Eu   // "LAUN"

#include CONFIG_DEF_H

#endif // _RECOVERY_CONFIG_H
