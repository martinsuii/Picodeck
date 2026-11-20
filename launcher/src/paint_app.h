// ****************************************************************************
//
//                  PicoDeck — Paint
//
// ****************************************************************************

#ifndef _PAINT_APP_H
#define _PAINT_APP_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Simple paint program: D-pad moves the cursor (hold to repeat), A paints,
// B erases, Y cycles colour, X saves to /paint.bmp and exits.
void PaintAppRun(void);

#ifdef __cplusplus
}
#endif

#endif // _PAINT_APP_H
