// ****************************************************************************
//
//                  PicoDeck — text editor
//
// ****************************************************************************

#ifndef _TEXT_EDITOR_H
#define _TEXT_EDITOR_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Edit a text file on the SD card (line-based). Returns on X.
void TextEditorRun(const char* name, const char* path);

#ifdef __cplusplus
}
#endif

#endif // _TEXT_EDITOR_H
