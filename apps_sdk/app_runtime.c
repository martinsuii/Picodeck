// ****************************************************************************
//
//                PicoPad custom app — radio-safe runtime init
//
//   The stock PicoLibSDK RuntimeInit() resets every peripheral and DeviceInit()
//   calls BatInit(), which reconfigures GP29. On a Pico W GP29 is the CYW43
//   SPI clock, so either one would kill the native WiFi module running on
//   core 1. This replacement initializes only what an app needs.
//
// ****************************************************************************

#include "include.h"

extern void (*__init_array_start)();
extern void (*__init_array_end)();

// PicoLibSDK's SetHandler() writes into the current vector table. VTOR starts
// out pointing at our flash table, and a store to XIP flash bus-faults. Keep
// a RAM copy like the stock RuntimeInit does, so SysTick/Key/SD handlers can
// be installed safely.
u32 __attribute__((section(".ram_vector_table")))
    __attribute__((aligned(256))) RamVectorTable[VECTOR_TABLE_SIZE];

void RuntimeInit(void)
{
    // ROM helpers first (the SDK's memcpy/memset wrappers need them).
    RomFncInit();

    // Move the vector table to RAM and point VTOR at it. Must happen before
    // SysTickInit / AlarmInit / KeyInit / SDInit, all of which may install
    // handlers through SetHandler().
    memcpy(RamVectorTable, (u32*)GetVTOR(), sizeof(RamVectorTable));
    SetVTOR((irq_handler_t*)RamVectorTable);
    NVIC_IRQPrioDef();

    // Core 0 services. Do NOT touch the other peripherals: PIO0/SPI are
    // shared with the WiFi module's core-1 loop.
    SysTickInit();
    AlarmInit();
    MemInit();

    for (void (**p)() = &__init_array_start; p < &__init_array_end; p++)
        (*p)();

    // Display / keys / SD (all already wired up by the launcher; re-init the
    // driver state that lives in our freshly zeroed BSS).
    // The launcher already initialised the display hardware (SPI/ST7789/
    // backlight). Re-running DispInit here resets SPI and was the source of
    // the app RuntimeInit hard fault. Only restore the driver state that
    // lives in our zeroed BSS.
    DispBacklight(255);
    SelFont8x8();
    DispDirtyAll();
    KeyInit();
    SDInit();
    DiskAutoMount();
}

// Some SDK code may reference this. Apps are expected to use AppExit().
void RuntimeTerm(void) {}

void _exit(int status)
{
    (void)status;
    AppExit();
}

void exit(int status)
{
    _exit(status);
}

int main(void)
{
    AppMain();
    AppExit();
    return 0;
}
