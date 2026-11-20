// ****************************************************************************
//
//                  PicoDeck — System info / diagnostics
//
// ****************************************************************************

#ifndef _SYSINFO_H
#define _SYSINFO_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Show battery, temperature, clocks, RAM/flash usage, SD and USB state.
// Returns on X.
void SysInfoRun(void);

#ifdef __cplusplus
}
#endif

#endif // _SYSINFO_H
