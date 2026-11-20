// ****************************************************************************
//
//              PicoDeck — fatal BSOD screen
//
//   Full-screen BSOD with stop details. Replaces the SDK's printf-based
//   handler (which pulls in ~5 KB of formatting code). Called from the crt0
//   unhandled-IRQ path.
//
// ****************************************************************************

#include "../include.h"
#include "crashlog.h"

#define BSOD_BG   RGBTO16(96, 0, 0)
#define BSOD_HDR  RGBTO16(48, 0, 0)
#define BSOD_DIM  RGBTO16(255, 190, 190)

static void BsodFill(u16 bg)
{
    for (int y = 0; y < HEIGHT; y += FONTH)
        DispDrawTextRow("", 0, y, COL_WHITE, bg);
}

static void BsodRow(int y, const char* text, u16 col, u16 bg)
{
    DispDrawTextRow(text, 8, y, col, bg);
}

// Direct GPIO: the SysTick key scan is dead in a fault handler.
static int BsodKey(void)
{
    if (GPIO_In(BTN_UP_PIN) == 0 && GPIO_In(BTN_X_PIN) == 0) return 3;
    if (GPIO_In(BTN_A_PIN) == 0) return 1;
    if (GPIO_In(BTN_X_PIN) == 0) return 2;
    if (GPIO_In(BTN_Y_PIN) == 0) return 4;
    return 0;
}

static __attribute__((noreturn)) void BsodReset(int mode)
{
    WATCHDOG_SCRATCH[0] = 0;
    WATCHDOG_SCRATCH[APP_SCRATCH_EXIT] = 0;
    WATCHDOG_SCRATCH[APP_SCRATCH_RECOVERY] =
        (mode == 3) ? APP_RECOVERY_MAGIC : 0;

    __asm volatile ("cpsid i" ::: "memory");
    WatchdogSetup(0, False);
    while (1) {}
}

extern "C" void FatalErrorMsg(sFatalFrame* f)
{
    __asm volatile ("cpsid i" ::: "memory");

    SelFont8x8();
    DispBacklight(255);

    BsodFill(BSOD_BG);
    for (int y = 0; y < 16; y += FONTH)
        DispDrawTextRow("", 0, y, COL_WHITE, BSOD_HDR);

    BsodRow(0, "LAUNCHER CRASH", COL_WHITE, BSOD_HDR);
    DispDrawTextRow(":(", WIDTH - 24, 0, COL_WHITE, BSOD_HDR);

    BsodRow(24, "The launcher hit an unrecoverable error.",
            COL_WHITE, BSOD_BG);
    BsodRow(40, "Your apps and settings are unchanged.",
            BSOD_DIM, BSOD_BG);

    char buf[48];

    u32 irq = f ? (u32)f->irq : 0;
    u32 pc  = f ? f->pc : 0;
    u32 cfsr = *(volatile u32*)0xE000ED28u;
    u32 hfsr = *(volatile u32*)0xE000ED2Cu;

    strcpy(buf, "IRQ ");
    DecHexNum(buf + 4, irq, 2);
    strcat(buf, "   PC ");
    DecHexNum(buf + StrLen(buf), pc, 8);
    BsodRow(64, buf, COL_WHITE, BSOD_BG);

    strcpy(buf, "CFSR ");
    DecHexNum(buf + 5, cfsr, 8);
    BsodRow(80, buf, BSOD_DIM, BSOD_BG);

    strcpy(buf, "HFSR ");
    DecHexNum(buf + 5, hfsr, 8);
    BsodRow(96, buf, BSOD_DIM, BSOD_BG);

    // Build the text log once so Y is instant even in the fault handler.
    char body[384];
    strcpy(body, "IRQ ");
    DecHexNum(body + 4, irq, 2);
    strcat(body, "\nPC ");
    DecHexNum(body + StrLen(body), pc, 8);
    strcat(body, "\nCFSR ");
    DecHexNum(body + StrLen(body), cfsr, 8);
    strcat(body, "\nHFSR ");
    DecHexNum(body + StrLen(body), hfsr, 8);

    if (f)
    {
        strcat(body, "\nR0 ");
        DecHexNum(body + StrLen(body), f->r0, 8);
        strcat(body, " R1 ");
        DecHexNum(body + StrLen(body), f->r1, 8);
        strcat(body, "\nR2 ");
        DecHexNum(body + StrLen(body), f->r2, 8);
        strcat(body, " R3 ");
        DecHexNum(body + StrLen(body), f->r3, 8);
        strcat(body, "\nR12 ");
        DecHexNum(body + StrLen(body), f->r12, 8);
        strcat(body, " LR ");
        DecHexNum(body + StrLen(body), f->lr, 8);
        strcat(body, "\nxPSR ");
        DecHexNum(body + StrLen(body), f->xpsr, 8);
        strcat(body, " SP ");
        DecHexNum(body + StrLen(body), f->sp, 8);
    }

    BsodRow(128, "A / X: restart", COL_WHITE, BSOD_BG);
    BsodRow(144, "UP+X: recovery", COL_WHITE, BSOD_BG);
    BsodRow(160, "Y: save crash log", COL_WHITE, BSOD_BG);
    BsodRow(208, "Press RESET if buttons do nothing", BSOD_DIM, BSOD_BG);

    for (;;)
    {
        int k = BsodKey();
        if (k == 3) BsodReset(3);
        if (k == 1 || k == 2) BsodReset(0);
        if (k == 4)
        {
            Bool ok = CrashLogSave("LAUNCHER CRASH", body);
            BsodRow(184, ok ? CrashLogLastPath() : "No SD card - log not saved",
                    ok ? COL_WHITE : BSOD_DIM, BSOD_BG);
            DispDrawTextRow(ok ? "Saved crash log:" : "", 8, 176,
                            BSOD_DIM, BSOD_BG);
            while (BsodKey() == 4) WaitMs(20);
        }
        WaitMs(30);
    }
}
