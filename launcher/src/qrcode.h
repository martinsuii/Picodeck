// ****************************************************************************
//
//                  PicoDeck — QR code generator
//
// ****************************************************************************

#ifndef _QRCODE_H
#define _QRCODE_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QR_MAX_SIZE 37

// Encode `text` (byte mode, ECC level L, versions 1..5) into a square bit
// matrix. Returns True on success; `size` is the module count and `matrix` is
// a size*size array where 1 = dark module. Matrix is a static buffer.
Bool QrEncode(const char* text, const u8** matrix, int* size);

#ifdef __cplusplus
}
#endif

#endif // _QRCODE_H
