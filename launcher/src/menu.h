// ****************************************************************************
//
//                  PicoDeck — main menu
//
// ****************************************************************************

#ifndef _MENU_H
#define _MENU_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Menu items
typedef enum {
    MENU_PLAY = 0,
    MENU_LOAD,
    MENU_TOOLS,
    MENU_FUN,
    MENU_APPS,
    MENU_SETTINGS,
    MENU_COUNT,
} MenuItem;

// Address of the game vector table — first thing flashed by Arcade UF2
#define GAME_VECTOR_TABLE_ADDR 0x10000100u

// Memory bounds for game vector table validation
#define RAM_BASE     0x20000000u
#define RAM_END      0x20042000u
#define GAME_FLASH_LO  0x10000100u
#define GAME_FLASH_HI  LAUNCHER_GAME_HI    // = 0x100E0000 (1 MB) or 0x101E0000 (2 MB)

// Public entry point — called from main(), never returns
void MenuRun(void);

// Game launch helpers
Bool IsGameValid(void);
void RunGame(void);

// Start a custom app image (vector table at APP_SLOT_BASE). Unlike RunGame
// this does not set the auto-relaunch magic and it keeps the launcher's
// hardware setup intact (the app runtime re-inits display/keys/SD itself).
// Never returns on success.
void RunAppImage(u32 vector_addr);

// Reboot to true RP2040 BOOTSEL mode (USB MSC drive RPI-RP2)
void RebootToBootsel(void);

// Remote app list used by Companion Link (companion.cpp). Returns the number
// of launchable apps; label/launch use a zero-based index.
int CompanionAppCount(void);
const char* CompanionAppLabel(int index);
Bool CompanionAppLaunch(int index);

#ifdef __cplusplus
}
#endif

#endif // _MENU_H
