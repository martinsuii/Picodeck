// ****************************************************************************
//
//                  PicoDeck — screensaver
//
// ****************************************************************************

#ifndef _SCREENSAVER_H
#define _SCREENSAVER_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Idle screen: duck + USB/SD/battery status. Status refreshes every 30 s.
// Returns as soon as any button is pressed.
void ScreensaverRun(void);

#ifdef __cplusplus
}
#endif

#endif // _SCREENSAVER_H
