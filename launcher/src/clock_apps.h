// ****************************************************************************
//
//                  PicoDeck — Stopwatch and timer
//
// ****************************************************************************

#ifndef _CLOCK_APPS_H
#define _CLOCK_APPS_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Stopwatch: A start/stop, Y reset, X exit.
void StopwatchRun(void);

// Countdown timer: UP/DOWN +/-1 min, LEFT/RIGHT +/-5 s, A start/pause,
// Y reset, X exit.
void TimerRun(void);

#ifdef __cplusplus
}
#endif

#endif // _CLOCK_APPS_H
