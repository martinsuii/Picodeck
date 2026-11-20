// ****************************************************************************
//
//                  PicoDeck — USB HID gamepad
//
//   Enumerates as a standard HID gamepad and forwards the PicoPad's 8 buttons
//   and D-pad to the host. The D-pad is reported as a hat switch, A/B/X/Y as
//   buttons 1-4.
//
//   Exit: hold X+Y for one second, or press the hardware RESET button.
//
// ****************************************************************************

#include "../include.h"
#include "usb_gamepad.h"

extern "C" void RuntimeTerm(void);

#define PAD_PID  0xD002

// ---------------------------------------------------------------------------
// USB descriptors (dedicated PID so hosts keep this separate from the
// keyboard / CDC / MSC devices)
// ---------------------------------------------------------------------------

static const u8 PadRepDesc[] = { HID_REPORT_DESC_GAMEPAD };

static const u8 PadDescCfg[9 + USB_TEMP_HIDD_LEN] = {
    USB_TEMP_CFG(1, 1, 0, 9 + USB_TEMP_HIDD_LEN, 100),
    USB_TEMP_HIDD(0, 4, USB_HID_ITF_NONE, sizeof(PadRepDesc),
                  USB_EPADDR(1, USB_DIR_IN), USB_PACKET_MAX, 10),
};

static const sUsbDescCfg* PadCfgList[1] = { (const sUsbDescCfg*)PadDescCfg };
static const u8* PadRepList[1] = { PadRepDesc };

static const sUsbDescDev PadDescDev = {
    18, USB_DESC_DEVICE, 0x0200, 0, 0, 0, USB_PACKET_MAX,
    0x2E8A, PAD_PID, 0x0100, 1, 2, 3, 1,
};

static const char* PadDescStr[] = {
    "Raspberry Pi", "PicoPad", "00000000", "USB Gamepad",
};

static const sUsbDevSetupDesc PadSetupDesc = {
    .desc_dev      = &PadDescDev,
    .desc_cfg_list = PadCfgList,
    .desc_cfg_num  = 1,
    .desc_bos      = NULL,
    .desc_list     = (const void**)PadRepList,
    .mounted_cb    = NULL,
    .suspend_cb    = NULL,
    .resume_cb     = NULL,
    .str_cb        = NULL,
    .str_list      = PadDescStr,
    .str_num       = 4,
};

// ---------------------------------------------------------------------------
// Report build
// ---------------------------------------------------------------------------

static void PadBuildReport(sUsbHidPad* rep)
{
    rep->x = rep->y = rep->z = rep->rz = rep->rx = rep->ry = 0;
    rep->btn = 0;
    rep->hat = 8;   // neutral

    Bool up    = KeyPressed(KEY_UP);
    Bool down  = KeyPressed(KEY_DOWN);
    Bool left  = KeyPressed(KEY_LEFT);
    Bool right = KeyPressed(KEY_RIGHT);

    if      (up   && left)  rep->hat = 7;   // NW
    else if (up   && right) rep->hat = 1;   // NE
    else if (down && left)  rep->hat = 5;   // SW
    else if (down && right) rep->hat = 3;   // SE
    else if (up)            rep->hat = 0;   // N
    else if (right)         rep->hat = 2;   // E
    else if (down)          rep->hat = 4;   // S
    else if (left)          rep->hat = 6;   // W

    if (KeyPressed(KEY_A)) rep->btn |= 1u << 0;
    if (KeyPressed(KEY_B)) rep->btn |= 1u << 1;
    if (KeyPressed(KEY_X)) rep->btn |= 1u << 2;
    if (KeyPressed(KEY_Y)) rep->btn |= 1u << 3;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

static void PadDrawWaiting(int blink)
{
    DrawClear();
    DrawText2("USB Gamepad", (WIDTH - 11 * FONTW * 2) / 2, 30, COL_YELLOW);
    DrawText("Connect PicoPad to a PC.", 20, 90, COL_LTGRAY);
    DrawText("It will appear as a gamepad.", 20, 110, COL_LTGRAY);
    if (blink & 1) DrawText("Waiting for host...", 20, 150, COL_WHITE);
    DrawText("Press X to cancel", 20, 220, COL_GRAY);
    DispUpdate();
}

static void PadDrawActive(void)
{
    DrawClear();
    DrawText2("USB Gamepad", (WIDTH - 11 * FONTW * 2) / 2, 30, COL_YELLOW);
    DrawText("Connected — forwarding input.", 20, 90, COL_GREEN);
    DrawText("D-pad  -> hat switch", 20, 120, COL_LTGRAY);
    DrawText("A/B/X/Y -> buttons 1-4", 20, 135, COL_LTGRAY);
    DrawText("Hold X+Y for 1s to exit", 20, 165, COL_WHITE);
    DrawText("(hardware RESET also works)", 20, 180, COL_LTGRAY);
    DispUpdate();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

void UsbGamepadRun(void)
{
    UsbDevInit(&PadSetupDesc);

    u32 hold_start = 0;
    Bool holding = False;

    PadDrawWaiting(0);

    {
        int blink = 0;
        while (!UsbDevHidIsMounted())
        {
            if (KeyGet() == KEY_X) goto exit_pad;
            WaitMs(150);
            PadDrawWaiting(++blink);
        }
    }

    PadDrawActive();
    WaitMs(300);

    while (1)
    {
        sUsbHidPad rep;
        PadBuildReport(&rep);
        if (!UsbDevHidSendPadRep(&rep)) break;   // host gone

        // Exit combo: hold X+Y for one second.
        if (KeyPressed(KEY_X) && KeyPressed(KEY_Y))
        {
            if (!holding) { holding = True; hold_start = Millis(); }
            else if ((u32)(Millis() - hold_start) >= 1000) break;
        }
        else
        {
            holding = False;
        }

        WaitMs(20);
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
