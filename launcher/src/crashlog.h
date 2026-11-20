// ****************************************************************************
//
//                  PicoDeck — crash log writer
//
//   Writes a text log to /crash/CRASHnnn.TXT on the SD card. Used by the
//   launcher BSOD, the WiFi stop screen and (a copy in the app SDK) by apps.
//
// ****************************************************************************

#ifndef _CRASHLOG_H
#define _CRASHLOG_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Save title + body. Returns False when no SD card / write failed.
Bool CrashLogSave(const char* title, const char* body);

// Path of the last successfully written log, or "".
const char* CrashLogLastPath(void);

#ifdef __cplusplus
}
#endif

#endif // _CRASHLOG_H
