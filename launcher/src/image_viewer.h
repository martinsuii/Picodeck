// ****************************************************************************
//
//                  PicoDeck — Image viewer
//
// ****************************************************************************

#ifndef _IMAGE_VIEWER_H
#define _IMAGE_VIEWER_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Pick a .BMP file from the SD card and display it (24/32-bit uncompressed,
// scaled to fit). Returns on X.
void ImageViewerRun(void);

#ifdef __cplusplus
}
#endif

#endif // _IMAGE_VIEWER_H
