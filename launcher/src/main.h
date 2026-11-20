// ****************************************************************************
//
//                  PicoDeck — main header
//
// ****************************************************************************

#ifndef _MAIN_H
#define _MAIN_H

// Launcher version — bump on every change (major.minor.patch).
#define LAUNCHER_VERSION_MAJOR  1
#define LAUNCHER_VERSION_MINOR  0
#define LAUNCHER_VERSION_PATCH  0
#define LAUNCHER_VERSION_STR    "1.0.0"

// PicoLibSDK's Time() returns microseconds (timer_hw->timerawl). Use Millis()
// for anything that expects milliseconds.
static inline u32 Millis(void) { return Time() / 1000u; }

#endif // _MAIN_H
