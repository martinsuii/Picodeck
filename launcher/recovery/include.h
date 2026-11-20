// ****************************************************************************
//
//                  PicoPad Recovery — includes
//
// ****************************************************************************

#ifndef _RECOVERY_INCLUDE_H
#define _RECOVERY_INCLUDE_H

#include "config.h"
#include INCLUDES_H          // PicoLibSDK (display, keys, flash, ResetUsbBoot, ...)

static inline u32 RecoveryMillis(void) { return Time() / 1000u; }

#endif // _RECOVERY_INCLUDE_H
