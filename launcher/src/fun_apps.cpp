// ****************************************************************************
//
//                  PicoDeck — fun USB/HID apps
//
//   Mouse jiggler, media remote (consumer control) and keyboard pranks.
//
// ****************************************************************************

#include "../include.h"
#include "fun_apps.h"
#include "rubber_ducky.h"

extern "C" void RuntimeTerm(void);

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

static Bool FunWaitMount(void)
{
    while (!UsbDevHidIsMounted())
    {
        if (KeyGet() == KEY_X) return False;
        WaitMs(100);
    }
    return True;
}

static void FunReset(void)
{
    DrawClear();
    DrawText2("Restarting...", 80, 100, COL_LTGRAY);
    DispUpdate();
    WaitMs(200);
    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}
}

// ---------------------------------------------------------------------------
// USB descriptors (dedicated PIDs)
// ---------------------------------------------------------------------------

// --- mouse-only (jiggler) ---
#define JIG_PID 0xD003
static const u8 JigRepDesc[] = { HID_REPORT_DESC_MOUSE };
static const u8 JigDescCfg[9 + USB_TEMP_HIDD_LEN] = {
    USB_TEMP_CFG(1, 1, 0, 9 + USB_TEMP_HIDD_LEN, 100),
    USB_TEMP_HIDD(0, 4, USB_HID_ITF_MOUSE, sizeof(JigRepDesc),
                  USB_EPADDR(1, USB_DIR_IN), USB_PACKET_MAX, 10),
};
static const sUsbDescCfg* JigCfgList[1] = { (const sUsbDescCfg*)JigDescCfg };
static const u8* JigRepList[1] = { JigRepDesc };
static const sUsbDescDev JigDescDev = {
    18, USB_DESC_DEVICE, 0x0200, 0, 0, 0, USB_PACKET_MAX,
    0x2E8A, JIG_PID, 0x0100, 1, 2, 3, 1,
};
static const char* JigDescStr[] = { "Raspberry Pi", "PicoPad", "00000000",
                                    "USB HID Mouse" };
static const sUsbDevSetupDesc JigSetupDesc = {
    .desc_dev = &JigDescDev, .desc_cfg_list = JigCfgList, .desc_cfg_num = 1,
    .desc_bos = NULL, .desc_list = (const void**)JigRepList,
    .mounted_cb = NULL, .suspend_cb = NULL, .resume_cb = NULL,
    .str_cb = NULL, .str_list = JigDescStr, .str_num = 4,
};

// --- consumer control (media remote) ---
#define MR_PID 0xD004
static const u8 MrRepDesc[] = { HID_REPORT_DESC_CONSUMER };
static const u8 MrDescCfg[9 + USB_TEMP_HIDD_LEN] = {
    USB_TEMP_CFG(1, 1, 0, 9 + USB_TEMP_HIDD_LEN, 100),
    USB_TEMP_HIDD(0, 4, USB_HID_ITF_NONE, sizeof(MrRepDesc),
                  USB_EPADDR(1, USB_DIR_IN), USB_PACKET_MAX, 10),
};
static const sUsbDescCfg* MrCfgList[1] = { (const sUsbDescCfg*)MrDescCfg };
static const u8* MrRepList[1] = { MrRepDesc };
static const sUsbDescDev MrDescDev = {
    18, USB_DESC_DEVICE, 0x0200, 0, 0, 0, USB_PACKET_MAX,
    0x2E8A, MR_PID, 0x0100, 1, 2, 3, 1,
};
static const char* MrDescStr[] = { "Raspberry Pi", "PicoPad", "00000000",
                                   "USB Media Remote" };
static const sUsbDevSetupDesc MrSetupDesc = {
    .desc_dev = &MrDescDev, .desc_cfg_list = MrCfgList, .desc_cfg_num = 1,
    .desc_bos = NULL, .desc_list = (const void**)MrRepList,
    .mounted_cb = NULL, .suspend_cb = NULL, .resume_cb = NULL,
    .str_cb = NULL, .str_list = MrDescStr, .str_num = 4,
};

// ---------------------------------------------------------------------------
// Mouse jiggler
// ---------------------------------------------------------------------------

void MouseJigglerRun(void)
{
    UsbDevInit(&JigSetupDesc);

    Bool on = False;
    Bool need_redraw = True;
    int dx = 4;

    DrawClear();
    DrawText2("Mouse Jiggler", (WIDTH - 13 * FONTW * 2) / 2, 40, COL_YELLOW);
    DrawText("Connect to a PC...", 20, 110, COL_LTGRAY);
    DrawText("Press X to cancel", 20, 210, COL_GRAY);
    DispUpdate();

    if (!FunWaitMount()) goto done;

    DrawClear();
    DrawText2("Mouse Jiggler", (WIDTH - 13 * FONTW * 2) / 2, 30, COL_YELLOW);
    DrawText("A: Toggle jiggling", 20, 150, COL_WHITE);
    DrawText("X: Exit", 20, 170, COL_WHITE);

    while (1)
    {
        if (need_redraw)
        {
            DrawTextBg(on ? "State: ON " : "State: OFF",
                       20, 100, on ? COL_GREEN : COL_GRAY, COL_BLACK);
            DispUpdate();
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k != NOKEY)
        {
            if (k == KEY_X) break;
            if (k == KEY_A) { on = !on; need_redraw = True; }
        }

        if (on)
        {
            UsbDevHidSendMouse((s8)dx, 0, False, False, False);
            dx = -dx;
            WaitMs(400);
        }
        else
        {
            WaitMs(50);
        }
    }

done:
    FunReset();
}

// ---------------------------------------------------------------------------
// Media remote
// ---------------------------------------------------------------------------

typedef struct { const char* label; u16 usage; } MediaKey;
static const MediaKey MediaKeys[] = {
    {"Play/Pause", 0x00CD},
    {"Next track", 0x00B5},
    {"Prev track", 0x00B6},
    {"Stop",       0x00B7},
    {"Volume +",   0x00E9},
    {"Volume -",   0x00EA},
    {"Mute",       0x00E2},
    {"Home",       0x0223},
    {"Back",       0x0224},
    {"Search",     0x0221},
};
#define MEDIA_COUNT ((int)(sizeof(MediaKeys) / sizeof(MediaKeys[0])))

static void MediaSend(u16 usage)
{
    sUsbHidCtrl c;
    c.val = usage;
    UsbDevHidSendReport(0, &c, sizeof(c), 0);
    WaitMs(30);
    c.val = 0;
    UsbDevHidSendReport(0, &c, sizeof(c), 0);
}

void MediaRemoteRun(void)
{
    UsbDevInit(&MrSetupDesc);

    DrawClear();
    DrawText2("Media Remote", (WIDTH - 12 * FONTW * 2) / 2, 40, COL_YELLOW);
    DrawText("Connect to a PC...", 20, 110, COL_LTGRAY);
    DrawText("Press X to cancel", 20, 210, COL_GRAY);
    DispUpdate();

    if (!FunWaitMount()) goto done;

    {
        int selected = 0;
        Bool need_redraw = True;

        while (1)
        {
            if (need_redraw)
            {
                DrawClear();
                DrawText2("Media Remote", (WIDTH - 12 * FONTW * 2) / 2, 12,
                          COL_YELLOW);
                DrawRect(10, 36, WIDTH - 20, 1, COL_GRAY);

                for (int i = 0; i < MEDIA_COUNT; i++)
                {
                    int y = 50 + i * 15;
                    Bool sel = (i == selected);
                    if (sel)
                        DrawRect(8, y - 2, WIDTH - 16, FONTH + 4,
                                 RGBTO16(40, 40, 0));
                    DrawText(sel ? ">" : " ", 14, y, sel ? COL_YELLOW : COL_WHITE);
                    DrawText(MediaKeys[i].label, 34, y,
                             sel ? COL_YELLOW : COL_WHITE);
                }

                DrawText("A: Send   X: Back", 10, 226, COL_LTGRAY);
                DispUpdate();
                need_redraw = False;
            }

            u8 k = KeyGet();
            if (k == NOKEY) { WaitMs(20); continue; }

            switch (k)
            {
            case KEY_UP:
                selected = (selected - 1 + MEDIA_COUNT) % MEDIA_COUNT;
                need_redraw = True;
                break;
            case KEY_DOWN:
                selected = (selected + 1) % MEDIA_COUNT;
                need_redraw = True;
                break;
            case KEY_A:
                MediaSend(MediaKeys[selected].usage);
                break;
            case KEY_X:
                goto done;
            default:
                break;
            }
        }
    }

done:
    FunReset();
}

// ---------------------------------------------------------------------------
// Keyboard pranks
// ---------------------------------------------------------------------------

static void PrankNotepad(void)
{
    RubberDuckySendKey(HID_KEY_R, USB_KEY_MODI_LWIN);
    WaitMs(700);
    RubberDuckySendText("notepad");
    WaitMs(200);
    RubberDuckySendKey(HID_KEY_ENTER, 0);
    WaitMs(900);
    for (int i = 0; i < 3; i++)
        RubberDuckySendText("You have been quacked! ");
}

static void PrankCapslock(void)
{
    for (int i = 0; i < 6; i++)
    {
        RubberDuckySendKey(HID_KEY_CAPS_LOCK, 0);
        WaitMs(150);
    }
}

static void PrankRickroll(void)
{
    RubberDuckySendKey(HID_KEY_R, USB_KEY_MODI_LWIN);
    WaitMs(700);
    RubberDuckySendText("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
    WaitMs(200);
    RubberDuckySendKey(HID_KEY_ENTER, 0);
}

static void PrankMinimizeAll(void)
{
    RubberDuckySendKey(HID_KEY_D, USB_KEY_MODI_LWIN);
}

static void PrankTabs(void)
{
    for (int i = 0; i < 8; i++)
    {
        RubberDuckySendKey(HID_KEY_T, USB_KEY_MODI_LCTRL);
        WaitMs(120);
    }
}

typedef struct { const char* label; void (*run)(void); } Prank;
static const Prank Pranks[] = {
    {"Notepad spam",  PrankNotepad},
    {"Caps Lock",     PrankCapslock},
    {"Rickroll",      PrankRickroll},
    {"Minimize all",  PrankMinimizeAll},
    {"New tab spam",  PrankTabs},
};
#define PRANK_COUNT ((int)(sizeof(Pranks) / sizeof(Pranks[0])))

void KeyboardPranksRun(void)
{
    RubberDuckyUsbInit();

    DrawClear();
    DrawText2("Keyboard Pranks", (WIDTH - 15 * FONTW * 2) / 2, 40, COL_YELLOW);
    DrawText("Connect to a PC...", 20, 110, COL_LTGRAY);
    DrawText("Press X to cancel", 20, 210, COL_GRAY);
    DispUpdate();

    if (!FunWaitMount()) goto done;

    {
        int selected = 0;
        Bool need_redraw = True;

        while (1)
        {
            if (need_redraw)
            {
                DrawClear();
                DrawText2("Keyboard Pranks", (WIDTH - 15 * FONTW * 2) / 2, 20,
                          COL_YELLOW);
                DrawRect(10, 50, WIDTH - 20, 1, COL_GRAY);

                for (int i = 0; i < PRANK_COUNT; i++)
                {
                    int y = 70 + i * 22;
                    Bool sel = (i == selected);
                    if (sel)
                        DrawRect(8, y - 3, WIDTH - 16, FONTH + 6,
                                 RGBTO16(40, 40, 0));
                    DrawText(sel ? ">" : " ", 14, y, sel ? COL_YELLOW : COL_WHITE);
                    DrawText(Pranks[i].label, 34, y,
                             sel ? COL_YELLOW : COL_WHITE);
                }

                DrawText("A: Fire   X: Back", 10, 226, COL_LTGRAY);
                DispUpdate();
                need_redraw = False;
            }

            u8 k = KeyGet();
            if (k == NOKEY) { WaitMs(20); continue; }

            switch (k)
            {
            case KEY_UP:
                selected = (selected - 1 + PRANK_COUNT) % PRANK_COUNT;
                need_redraw = True;
                break;
            case KEY_DOWN:
                selected = (selected + 1) % PRANK_COUNT;
                need_redraw = True;
                break;
            case KEY_A:
                DrawTextBg("Working...      ", 10, 210, COL_WHITE, COL_BLACK);
                DispUpdate();
                Pranks[selected].run();
                need_redraw = True;
                break;
            case KEY_X:
                goto done;
            default:
                break;
            }
        }
    }

done:
    FunReset();
}
