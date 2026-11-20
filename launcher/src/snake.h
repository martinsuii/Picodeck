// ****************************************************************************
//
//                  PicoDeck — fun apps: Snake
//
//   Classic snake game on the D-pad.
//     Arrows  turn
//     A       pause / resume
//     X       exit
//
// ****************************************************************************

#ifndef _SNAKE_H
#define _SNAKE_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Play snake until the user exits with X.
void SnakeRun(void);

#ifdef __cplusplus
}
#endif

#endif // _SNAKE_H
