// ****************************************************************************
//
//                  PicoDeck — hidden developer mode
//
//   Enabled from the calculator with the code 789789. Writes an empty
//   /Conf/dev.txt marker; while that file exists SETTINGS shows the
//   Credential manager.
//
// ****************************************************************************

#ifndef _DEV_MENU_H
#define _DEV_MENU_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// True when the marker file exists on the SD card.
Bool DevModeEnabled(void);

// Hidden menu: toggle developer mode. Returns on X.
void DevMenuRun(void);

#ifdef __cplusplus
}
#endif

#endif // _DEV_MENU_H
