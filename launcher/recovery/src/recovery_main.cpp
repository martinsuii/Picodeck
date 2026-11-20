// ****************************************************************************
//
//                  PicoDeck — recovery image
//
//   This is a separate firmware image in its own flash partition. boot2 jumps
//   here FIRST. It then either:
//     - chain-loads the launcher (normal boot), or
//     - shows the recovery menu when UP+X are held at power-on / RESET,
//       or when the launcher image fails validation.
//
//   Because it never shares code or flash with the launcher, a corrupted
//   launcher can no longer take the recovery menu down with it.
//
// ****************************************************************************

#include "../include.h"
#include "../../../app_shared.h"

extern "C" void RuntimeTerm(void);

// Launcher vector table base (where boot2 used to jump before this change).
#define LAUNCHER_BASE   LAUNCHER_FLASH_BASE
#define RECOVERY_BASE   RECOVERY_FLASH_BASE

#define RAM_LO          0x20000000u
#define RAM_HI          0x20042000u
#define PPAD_MAGIC      0x44415050u   // "PPAD" at launcher offset 0xC0

// ---------------------------------------------------------------------------
// Launcher image validation
// ---------------------------------------------------------------------------

// The launcher vector table starts with initial SP + reset handler, and its
// PicoLibSDK app info header sits at offset 0xC0. A blank/corrupt partition
// fails at least one of these checks.
static Bool LauncherValid(void)
{
    volatile const u32* vt = (volatile const u32*)LAUNCHER_BASE;
    u32 sp = vt[0];
    u32 ep = vt[1];

    if (sp < RAM_LO || sp > RAM_HI) return False;
    if (ep < LAUNCHER_BASE || ep >= RECOVERY_BASE) return False;
    if ((ep & 1u) == 0) return False;
    if (*(volatile const u32*)(LAUNCHER_BASE + 0xC0) != PPAD_MAGIC) return False;
    return True;
}

// ---------------------------------------------------------------------------
// Chain-load the launcher
// ---------------------------------------------------------------------------

static void RunLauncher(void)
{
    volatile const u32* vt = (volatile const u32*)LAUNCHER_BASE;
    u32 sp    = vt[0];
    u32 entry = vt[1];

    // Tear down everything recovery brought up (display, SD, SysTick, ...).
    RuntimeTerm();

    __asm volatile ("cpsid i" ::: "memory");

    // Clear NVIC enables/pending and stop SysTick so no recovery handler can
    // fire after VTOR switches to the launcher's table.
    *(volatile u32*)0xE000E180u = 0xFFFFFFFFu;  // NVIC_ICER0
    *(volatile u32*)0xE000E280u = 0xFFFFFFFFu;  // NVIC_ICPR0
    *(volatile u32*)0xE000E010u = 0;            // SYST_CSR
    *(volatile u32*)0xE000E014u = 0;            // SYST_RVR
    *(volatile u32*)0xE000E018u = 0;            // SYST_CVR

    *(volatile u32*)0xE000ED08u = LAUNCHER_BASE;

    __asm volatile ("dsb; isb" ::: "memory");
    __asm volatile ("cpsie i" ::: "memory");

    __asm volatile (
        "msr msp, %0\n"
        "bx  %1\n"
        :: "r"(sp), "r"(entry) : "memory"
    );

    while (1) {}   // unreachable
}

// ---------------------------------------------------------------------------
// Reboot / BOOTSEL
// ---------------------------------------------------------------------------

void RebootToBootsel(void)
{
    reset_usb_boot(0, 0);
    while (1) {}
}

static void Reboot(void)
{
    // Don't let a stale game-auto-launch magic make the next boot jump
    // straight back into a game instead of the menu.
    WATCHDOG_SCRATCH[0] = 0;
    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}
}

// ---------------------------------------------------------------------------
// Recovery menu
// ---------------------------------------------------------------------------

static const char* const RecLabels[] = {
    "Boot launcher",
    "Boot to BOOTSEL",
    "Restart",
};
#define REC_COUNT ((int)(sizeof(RecLabels) / sizeof(RecLabels[0])))

// Item 0 is only usable when the launcher image passed validation.
static Bool RecEnabled(int i, Bool launcher_ok)
{
    return (i != 0) || launcher_ok;
}

static void RecoveryMenu(Bool launcher_ok)
{
    SelFont8x8();
    DispBacklight(255);

    int selected = launcher_ok ? 0 : 1;
    Bool need_redraw = True;

    // Drain any buffered key and wait for the combo to be released.
    while (KeyGet() != NOKEY) {}
    while (KeyPressed(KEY_UP) || KeyPressed(KEY_X)) WaitMs(20);

    while (1)
    {
        if (need_redraw)
        {
            DrawClear();

            DrawText("PicoDeck Recovery", 8, 8, COL_WHITE);
            if (launcher_ok)
                DrawText("launcher OK", 8, 20, COL_GREEN);
            else
                DrawText("launcher image invalid!", 8, 20, COL_RED);
            DrawRect(8, 34, WIDTH - 16, 1, COL_GRAY);

            for (int i = 0; i < REC_COUNT; i++)
            {
                int y = 46 + i * 16;
                Bool sel = (i == selected);
                u16 col = RecEnabled(i, launcher_ok) ? COL_WHITE : COL_GRAY;
                DrawText(sel ? "> " : "  ", 8, y, COL_WHITE);
                DrawText(RecLabels[i], 24, y, col);
            }

            DrawRect(8, 46 + REC_COUNT * 16 + 8, WIDTH - 16, 1, COL_GRAY);
            DrawText("A = select", 8, HEIGHT - 14, COL_GRAY);
            DispUpdate();
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            do { selected = (selected - 1 + REC_COUNT) % REC_COUNT; }
            while (!RecEnabled(selected, launcher_ok));
            need_redraw = True;
            break;
        case KEY_DOWN:
            do { selected = (selected + 1) % REC_COUNT; }
            while (!RecEnabled(selected, launcher_ok));
            need_redraw = True;
            break;
        case KEY_A:
            if (selected == 0)
            {
                if (launcher_ok) RunLauncher();
            }
            else if (selected == 1)
            {
                RebootToBootsel();
            }
            else if (selected == 2)
            {
                Reboot();
            }
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main()
{
    // Read the buttons directly — SysTick key scan may not have run yet.
    Bool combo = (GPIO_In(BTN_UP_PIN) == 0 && GPIO_In(BTN_X_PIN) == 0);
    Bool launcher_ok = LauncherValid();

    // An app can request the recovery menu explicitly (AppRecovery()).
    Bool app_recovery = (WATCHDOG_SCRATCH[APP_SCRATCH_RECOVERY] == APP_RECOVERY_MAGIC);
    if (app_recovery)
    {
        WATCHDOG_SCRATCH[APP_SCRATCH_RECOVERY] = 0;
        WATCHDOG_SCRATCH[APP_SCRATCH_EXIT] = 0;
        WATCHDOG_SCRATCH[0] = 0;
    }

    // Normal boot: no combo held and the launcher looks sane → hand over.
    if (!combo && !app_recovery && launcher_ok)
    {
        RunLauncher();
    }

    // Combo held, app asked for recovery, or the launcher partition is bad
    // → stay in recovery.
    RecoveryMenu(launcher_ok);
    return 0;
}
