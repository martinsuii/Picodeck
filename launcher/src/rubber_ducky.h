// ****************************************************************************
//
//                  PicoDeck — Rubber Ducky tool
//
// ****************************************************************************

#ifndef _RUBBER_DUCKY_H
#define _RUBBER_DUCKY_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pick a .txt payload from /payloads on the SD card, present the PicoPad as a
// USB HID keyboard to the connected host, wait until the host mounts it, then
// replay the DuckyScript payload. Returns on X (after a watchdog reset).
void RubberDuckyRun(void);

// Bring up the USB HID keyboard device (also used by the menu for its USB
// connection indicator). Safe to call again when a tool re-inits USB.
void RubberDuckyUsbInit(void);

// True while the HID keyboard interface is enumerated by a host.
Bool RubberDuckyUsbConnected(void);

// HID helpers for other tools (keyboard pranks etc.). Require the USB HID
// device to be initialised (RubberDuckyUsbInit) and mounted.
Bool RubberDuckySendKey(u8 key, u8 modi);
Bool RubberDuckySendText(const char* text);
Bool RubberDuckyMouseJiggle(void);

// Shared script buffer (DUCKY_MAX_SCRIPT + 1 bytes). Lets other tools load a
// payload without allocating their own large buffer.
char* RubberDuckyScriptBuffer(void);

#ifdef __cplusplus
}
#endif

#endif // _RUBBER_DUCKY_H
