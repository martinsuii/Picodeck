// ****************************************************************************
//
//                  PicoDeck — on-screen keyboard
//
// ****************************************************************************

#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Modal text input. `buf` is filled with the entered text (NUL-terminated).
//   D-pad    move
//   A        type selected key
//   Y        backspace
//   X        cancel
// Returns True if the user pressed OK, False if cancelled.
Bool KeyboardInput(const char* title, char* buf, int buflen);

#ifdef __cplusplus
}
#endif

#endif // _KEYBOARD_H
