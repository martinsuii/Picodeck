// ****************************************************************************
//
//                  PicoDeck — persistent settings
//
//   Stored in the reserved state sector at the top of the launcher region:
//   0x100FE000 (1 MB layout) or 0x101FE000 (2 MB layout).
//
// ****************************************************************************

#ifndef _SETTINGS_H
#define _SETTINGS_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SETTINGS_FLASH_ADDR   LAUNCHER_STATE_ADDR
#define SETTINGS_SECTOR_SIZE  4096u
#define SETTINGS_MAGIC        0x53455431u   // "SET1"

typedef struct {
    u32 magic;
    u8  backlight;   // 0..255
    u8  _pad[3];
} Settings;

// Load settings from flash (or defaults), apply nothing by itself.
void SettingsLoad(void);
// Save settings to flash.
void SettingsSave(void);

u8  SettingsBacklight(void);
void SettingsSetBacklight(u8 v);

// Settings → Backlight screen (up/down adjust, A save, X back).
void BacklightRun(void);

#ifdef __cplusplus
}
#endif

#endif // _SETTINGS_H
