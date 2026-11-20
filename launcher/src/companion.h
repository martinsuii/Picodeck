// ****************************************************************************
//
//                  PicoDeck — Companion Link
//
// ****************************************************************************

#ifndef _COMPANION_H
#define _COMPANION_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Line-based control protocol over USB CDC and (through the ESP32) WiFi.
// X exits.
void CompanionLinkRun(void);

// Always-on USB CDC service: init at boot and poll from the menu loop, so the
// desktop/Android companion works without opening the Companion Link screen.
void CompanionUsbInit(void);
void CompanionPoll(void);

#ifdef __cplusplus
}
#endif

#endif // _COMPANION_H
