// ****************************************************************************
//
//                  PicoDeck — Themes app
//
// ****************************************************************************

#ifndef _THEMES_APP_H
#define _THEMES_APP_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pick the active theme (Default or a file from /themes). Persists the choice.
void ThemesRun(void);

#ifdef __cplusplus
}
#endif

#endif // _THEMES_APP_H
