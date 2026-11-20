// ****************************************************************************
//
//                  PicoDeck — USB MIDI pad
//
//   Minimal USB-MIDI device: Audio Control + MIDI Streaming interface with a
//   bulk IN endpoint. The 8 PicoPad buttons play a C-major scale.
//
//   NOTE: the SDK's MIDI class driver is a stub (in TODO/), so this module
//   drives the bulk endpoint directly. Untested on hardware.
//
// ****************************************************************************

#include "../include.h"
#include "midi_pad.h"

extern "C" void RuntimeTerm(void);

#define MIDI_PID    0xD005
#define MIDI_EP_IN  2      // EP1 IN  (endpoint index)
#define MIDI_EP_OUT 3      // EP1 OUT

// ---------------------------------------------------------------------------
// Descriptors (standard USB-MIDI 1.0: AC + MS interfaces, 2 bulk endpoints)
// ---------------------------------------------------------------------------

static const u8 MidiDescCfg[] = {
    // Configuration descriptor (9 B) — wTotalLength = 71 (0x47)
    0x09, 0x02, 0x47, 0x00, 0x02, 0x01, 0x00, 0xC0, 0x32,

    // Audio Control interface (9 B) — iface 0, class Audio, subclass Control
    0x09, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
    // CS AC interface header (9 B)
    0x09, 0x24, 0x01, 0x00, 0x01, 0x09, 0x00, 0x01, 0x01,

    // MIDI Streaming interface (9 B) — iface 1, 2 endpoints, subclass MIDIStreaming
    0x09, 0x04, 0x01, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00,
    // CS MS interface header (7 B) — wTotalLength = 17 (0x11)
    0x07, 0x24, 0x01, 0x00, 0x01, 0x11, 0x00,

    // Bulk OUT endpoint (9 B) — addr 0x01, bulk, 64
    0x09, 0x05, 0x01, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    // CS MS bulk data endpoint (5 B)
    0x05, 0x25, 0x01, 0x01, 0x01,

    // Bulk IN endpoint (9 B) — addr 0x81, bulk, 64
    0x09, 0x05, 0x81, 0x02, 0x40, 0x00, 0x00, 0x00, 0x00,
    // CS MS bulk data endpoint (5 B)
    0x05, 0x25, 0x01, 0x01, 0x01,
};

static const sUsbDescCfg* MidiCfgList[1] = { (const sUsbDescCfg*)MidiDescCfg };

static const sUsbDescDev MidiDescDev = {
    18, USB_DESC_DEVICE, 0x0200, 0, 0, 0, USB_PACKET_MAX,
    0x2E8A, MIDI_PID, 0x0100, 1, 2, 3, 1,
};

static const char* MidiDescStr[] = {
    "Raspberry Pi", "PicoPad", "00000000",
};

static const sUsbDevSetupDesc MidiSetupDesc = {
    .desc_dev      = &MidiDescDev,
    .desc_cfg_list = MidiCfgList,
    .desc_cfg_num  = 1,
    .desc_bos      = NULL,
    .desc_list     = NULL,
    .mounted_cb    = NULL,
    .suspend_cb    = NULL,
    .resume_cb     = NULL,
    .str_cb        = NULL,
    .str_list      = MidiDescStr,
    .str_num       = 3,
};

// ---------------------------------------------------------------------------
// MIDI send
// ---------------------------------------------------------------------------

static const u8 MidiKeys[8]  = { KEY_A, KEY_B, KEY_X, KEY_Y,
                                 KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT };
static const u8 MidiNotes[8] = { 60, 62, 64, 65, 67, 69, 71, 72 };  // C major

static u8 g_midi_pkt[4];

static void MidiSend(u8 code, u8 status, u8 b2, u8 b3)
{
    g_midi_pkt[0] = code;
    g_midi_pkt[1] = status;
    g_midi_pkt[2] = b2;
    g_midi_pkt[3] = b3;
    while (UsbEpIsBusy(MIDI_EP_IN)) {}
    UsbXferStart(MIDI_EP_IN, g_midi_pkt, 4, False);
}

static void MidiNoteOn(u8 note)  { MidiSend(0x09, 0x90, note, 127); }
static void MidiNoteOff(u8 note) { MidiSend(0x08, 0x80, note, 0);   }

// ---------------------------------------------------------------------------
// Run
// ---------------------------------------------------------------------------

void MidiPadRun(void)
{
    UsbDevInit(&MidiSetupDesc);

    DrawClear();
    DrawText2("MIDI Pad", (WIDTH - 8 * FONTW * 2) / 2, 40, COL_YELLOW);
    DrawText("Connect to a PC...", 20, 110, COL_LTGRAY);
    DrawText("Press X to cancel", 20, 210, COL_GRAY);
    DispUpdate();

    while (!UsbIsMounted())
    {
        if (KeyGet() == KEY_X) goto exit_pad;
        WaitMs(100);
    }

    // Open the bulk endpoints manually (no MIDI class driver in the SDK).
    UsbDevEpInit(MIDI_EP_IN,  64, USB_XFER_BULK);
    UsbDevEpInit(MIDI_EP_OUT, 64, USB_XFER_BULK);

    DrawClear();
    DrawText2("MIDI Pad", (WIDTH - 8 * FONTW * 2) / 2, 30, COL_YELLOW);
    DrawText("Buttons play a C-major scale.", 20, 90, COL_LTGRAY);
    DrawText("A B X Y  Up Dn Lt Rt", 20, 120, COL_WHITE);
    DrawText("Hold X+Y for 1s to exit", 20, 160, COL_LTGRAY);
    DrawText("(hardware RESET also works)", 20, 176, COL_LTGRAY);
    DispUpdate();
    WaitMs(300);

    {
        Bool pressed[8];
        for (int i = 0; i < 8; i++) pressed[i] = False;

        u32 hold_start = 0;
        Bool holding = False;

        while (1)
        {
            for (int i = 0; i < 8; i++)
            {
                Bool now = KeyPressed(MidiKeys[i]);
                if (now && !pressed[i])       MidiNoteOn(MidiNotes[i]);
                else if (!now && pressed[i])  MidiNoteOff(MidiNotes[i]);
                pressed[i] = now;
            }

            if (KeyPressed(KEY_X) && KeyPressed(KEY_Y))
            {
                if (!holding) { holding = True; hold_start = Millis(); }
                else if ((u32)(Millis() - hold_start) >= 1000) break;
            }
            else holding = False;

            WaitMs(5);
        }
    }

exit_pad:
    DrawClear();
    DrawText2("Restarting...", 80, 100, COL_LTGRAY);
    DispUpdate();
    WaitMs(200);
    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}
}
