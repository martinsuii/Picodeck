// ****************************************************************************
//
//                PicoDeck — custom app contract
//
//   Shared between the launcher (which installs and starts apps) and the
//   app SDK (apps_sdk/). Keep this file in sync with the launcher flash
//   layout: APP_SLOT_BASE is the 256 KB strip just below the launcher.
//
// ****************************************************************************

#ifndef _APP_SHARED_H
#define _APP_SHARED_H

#include <stdint.h>

// 1 MB layout only (the default). The app slot sits between the WiFi module
// / game region and the launcher image.
#define APP_SLOT_BASE     0x100A0000u
#define APP_SLOT_SIZE     0x3F000u     // 252 KB of code
#define APP_SLOT_END      (APP_SLOT_BASE + APP_SLOT_SIZE)
#define APP_RECORD_ADDR   0x100DF000u  // one 4 KB sector, install record

#define APP_API_VERSION   1u

// Extended handoff fields appended to app_info_t. Old apps never look past
// data_path, so keeping this out of the version check preserves them.
#define APP_INFO_EXT_MAGIC  0x324E4649u  // "IFN2"

// Semantic colours apps can use to match the active launcher theme.
enum {
    APP_TH_BG = 0,
    APP_TH_PANEL,
    APP_TH_TITLE,
    APP_TH_TEXT,
    APP_TH_DIM,
    APP_TH_ACCENT,
    APP_TH_OK,
    APP_TH_WARN,
    APP_TH_ERR,
    APP_TH_SEL,
    APP_TH_HL,
    APP_TH_COUNT
};

// Handoff struct written by the launcher at APP_INFO_ADDR just before the
// jump. Not part of app RAM (the app heap ends at 0x20037800).
#define APP_INFO_ADDR     0x20037B00u
#define APP_INFO_MAGIC    0x464E4950u  // "PINF"

// 4 KB network scratch used by the WiFi module for AppHttpGet responses.
#define APP_NET_BUF_ADDR  0x20038000u
#define APP_NET_BUF_SIZE  0x1000u

// Launcher RAM snapshot used to return to the menu without a chip reboot.
#define LAUNCHER_SNAPSHOT_ADDR 0x10050000u
#define LAUNCHER_SNAPSHOT_SIZE 0x37800u

// Watchdog scratch indices shared with recovery / launcher.
#define APP_SCRATCH_EXIT     1
#define APP_SCRATCH_RECOVERY 2
#define APP_SCRATCH_SNAPSHOT 6
#define APP_SCRATCH_RESUME   7
#define APP_SNAPSHOT_VALID   0x534E5056u  // "SNPV"
#define APP_RESUME_MAGIC     0x4D52534Du  // "MSRM"
#define APP_LAUNCHER_MAGIC   0x53524450u  // "PDRS"
#define APP_EXIT_MAGIC     0x58495441u  // "ATIX"
#define APP_RECOVERY_MAGIC 0x52564543u  // "CEVR"

typedef struct {
    uint32_t magic;          // APP_INFO_MAGIC
    uint32_t api_version;    // APP_API_VERSION
    char     id[32];         // folder name under /apps
    char     config_path[64];// /apps/<id>/config.ini
    char     data_path[64];  // /apps/<id>/data

    // Extended fields (check ext_magic first). Old apps ignore these.
    uint32_t ext_magic;      // APP_INFO_EXT_MAGIC
    char     arg_path[96];   // file passed by "Open with app", or ""
    uint16_t theme[APP_TH_COUNT];
    char     theme_name[16];
} app_info_t;

#endif // _APP_SHARED_H
