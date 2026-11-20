// ****************************************************************************
//
//                  PicoDeck — fun USB/HID apps
//
// ****************************************************************************

#ifndef _FUN_APPS_H
#define _FUN_APPS_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Toggle continuous mouse jiggling (USB HID mouse). X exits.
void MouseJigglerRun(void);

// Media keys (USB HID consumer control). X exits.
void MediaRemoteRun(void);

// Canned keyboard pranks over the Rubber Ducky HID device. X exits.
void KeyboardPranksRun(void);

#ifdef __cplusplus
}
#endif

#endif // _FUN_APPS_H
