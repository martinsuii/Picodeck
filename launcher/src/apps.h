// ****************************************************************************
//
//                PicoDeck — custom apps
//
//   Apps are folders on the SD card:
//     /apps/<id>/app.ini   name=..., bin=app.bin
//     /apps/<id>/app.bin   raw image linked at APP_SLOT_BASE
//     /apps/<id>/config.ini / data/
//   /apps/apps.cfg lists installed ids.
//
// ****************************************************************************

#ifndef _APPS_H
#define _APPS_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Top-level APPS menu: installed apps + App Store. Returns on X.
void AppsMenuRun(void);

// File-manager hook: if an installed app claims the file extension via
// `open=` in app.ini, show a chooser and launch it with AppArgPath().
// Returns True when the file was handled (or the chooser cancelled), False
// when the caller should use its normal action.
Bool AppOpenWithRun(const char* path);

#ifdef __cplusplus
}
#endif

#endif // _APPS_H
