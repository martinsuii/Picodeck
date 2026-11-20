// ****************************************************************************
//
//                  PicoDeck — GPIO Control app
//
// ****************************************************************************

#ifndef _GPIO_APP_H
#define _GPIO_APP_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Interactive GPIO toggle app for the pins exposed on the external connector.
// Returns when the user exits (= X button).
void GpioAppRun(void);

#ifdef __cplusplus
}
#endif

#endif // _GPIO_APP_H
