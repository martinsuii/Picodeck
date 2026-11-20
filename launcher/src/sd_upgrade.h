// ****************************************************************************
//
//                  PicoDeck — "Update from SD"
//
// ****************************************************************************

#ifndef _SD_UPGRADE_H
#define _SD_UPGRADE_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Find a launcher update UF2 on the SD card, check its version against the
// running one (warn if not newer), then reflash the launcher in place.
// Uses the USR LED as a progress indicator. Returns on X.
void SdUpgradeRun(void);

#ifdef __cplusplus
}
#endif

#endif // _SD_UPGRADE_H
