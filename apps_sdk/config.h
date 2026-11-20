// ****************************************************************************
//
//                 PicoPad custom app — PicoLibSDK configuration
//
//   Minimal launcher-compatible setup. No USB, no sound IRQ, no RTC, no
//   battery ADC (GP29 belongs to the WiFi radio while the module runs).
//
// ****************************************************************************

#ifndef _CONFIG_H
#define _CONFIG_H

// 8x8 system font, matching the launcher UI and stripping FontBold8x16.
#define FONT                FontBold8x8
#define FONTH               8

// Keep the image small and leave the radio's pins alone.
#define USE_PWMSND          0
#define USE_RAND            0
#define USE_RTC             0
#define USE_CALENDAR        0
#define USE_CALENDAR64      0

#define USE_USB             0
#define USE_USB_DEV         0
#define USE_RING            1

// Use our compact fatal handler (app_api.c) instead of the SDK printf one.
#define USE_FATALERROR      0

#include CONFIG_DEF_H        // default configuration

#endif // _CONFIG_H
