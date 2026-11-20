// ****************************************************************************
//
//                  PicoDeck — USB MIDI pad
//
// ****************************************************************************

#ifndef _MIDI_PAD_H
#define _MIDI_PAD_H

#include "../include.h"

#ifdef __cplusplus
extern "C" {
#endif

// Present the PicoPad as a USB MIDI device; the 8 buttons play notes.
// Hold X+Y for 1 s (or press RESET) to exit.
void MidiPadRun(void);

#ifdef __cplusplus
}
#endif

#endif // _MIDI_PAD_H
