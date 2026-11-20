// ****************************************************************************
//
//                  PicoDeck — USB HID gamepad
//
// ****************************************************************************

#ifndef _USB_GAMEPAD_H
#define _USB_GAMEPAD_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Present the PicoPad as a standard USB HID gamepad: all 8 buttons and the
// D-pad are forwarded to the connected host. Hold X+Y for 1 s (or press the
// hardware RESET button) to exit.
void UsbGamepadRun(void);

#ifdef __cplusplus
}
#endif

#endif // _USB_GAMEPAD_H
