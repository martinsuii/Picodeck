// ****************************************************************************
//
//                  PicoDeck — GPIO Control app
//
//   Control the six GPIO pins exposed on the PicoPad external connector:
//     GP0, GP1, GP14, GP26, GP27, GP28
//
//   These pins are unused by the launcher (display uses GP16-21, SD uses
//   GP10-13, buttons use GP2-9, sound GP15, LEDs GP22/GP25, battery GP29).
//   GP26/27/28 are also the ADC-capable pins.
//
//   Controls:
//     UP/DOWN          : select pin
//     A / LEFT / RIGHT : toggle selected pin HIGH/LOW (output mode)
//     Y                : toggle View mode (input) for the selected pin
//     X                : exit back to the menu
//
//   In View mode the pin becomes a pulled-up input. The display then reports
//   whether the pin is tied to ground (= a button/switch pressed) or left
//   open, so the PicoPad can be used as a simple continuity/button tester.
//
// ****************************************************************************

#include "../include.h"
#include "gpio_app.h"

#define GPIO_APP_PINS  7

// GP22 is the on-board user LED (inverted on the PicoPad).
static const int GpioAppPins[GPIO_APP_PINS] = { 0, 1, 14, 22, 26, 27, 28 };
static const char* const GpioAppLabels[GPIO_APP_PINS] = {
    "GP0", "GP1", "GP14", "[USR led]", "GP26", "GP27", "GP28"
};

// State — persists across app entries so experiments survive a trip back to
// the menu. GpioAppView[i] == True means the pin is an input being monitored.
static Bool GpioAppInit = False;
static Bool GpioAppHigh[GPIO_APP_PINS];
static Bool GpioAppView[GPIO_APP_PINS];

// Active-low pins: the on-board USR LED (GP22) is wired inverted, so a
// logical HIGH (= ON) drives the pin LOW.
static const Bool GpioAppInvert[GPIO_APP_PINS] = {
    False, False, False, True, False, False, False
};

// Layout
#define GA_TITLE_Y    12
#define GA_SUB_Y      36
#define GA_DIV_Y      52
#define GA_LIST_Y     62
#define GA_ROW_H      20
#define GA_FOOT_Y     214

// (Re)configure one pin according to its current mode and output level.
static void GpioAppConfigure(int i)
{
    GPIO_Init(GpioAppPins[i]);

    if (GpioAppView[i])
    {
        // Pulled-up input: reads LOW when tied to GND, HIGH when open.
        GPIO_DirIn(GpioAppPins[i]);
        GPIO_PullUp(GpioAppPins[i]);
    }
    else
    {
        GPIO_DirOut(GpioAppPins[i]);
        int level = GpioAppHigh[i];
        if (GpioAppInvert[i]) level = !level;
        if (level) GPIO_Out1(GpioAppPins[i]);
        else       GPIO_Out0(GpioAppPins[i]);
    }
}

// Configure all pins once, driven from the saved state.
static void GpioAppEnsureInit(void)
{
    if (GpioAppInit) return;

    for (int i = 0; i < GPIO_APP_PINS; i++)
    {
        GpioAppHigh[i] = False;
        GpioAppView[i] = False;
        GpioAppConfigure(i);
    }

    GpioAppInit = True;
}

// True if at least one pin is in View mode (= needs periodic polling).
static Bool GpioAppAnyView(void)
{
    for (int i = 0; i < GPIO_APP_PINS; i++)
        if (GpioAppView[i]) return True;
    return False;
}

static void GpioAppRender(int selected)
{
    DrawClear();

    const char* title = "GPIO Control";
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, GA_TITLE_Y,
              COL_YELLOW);

    const char* sub = "External connector";
    DrawText(sub, (WIDTH - (int)StrLen(sub) * FONTW) / 2, GA_SUB_Y, COL_LTGRAY);

    DrawRect(24, GA_DIV_Y, WIDTH - 48, 1, COL_GRAY);

    for (int i = 0; i < GPIO_APP_PINS; i++)
    {
        int y = GA_LIST_Y + i * GA_ROW_H;
        Bool sel = (i == selected);
        u16 col = sel ? COL_YELLOW : COL_WHITE;
        Bool active;   // "on" state used for the indicator box

        if (sel)
            DrawRect(18, y - 3, WIDTH - 36, FONTH + 6, RGBTO16(40, 40, 0));

        DrawText(sel ? ">" : " ", 24, y, col);
        DrawText(GpioAppLabels[i], 42, y, col);

        if (GpioAppView[i])
        {
            // Input / View mode: report connection to ground.
            Bool grounded = (GPIO_In(GpioAppPins[i]) == 0);
            active = grounded;
            DrawText("IN", 132, y, COL_CYAN);
            DrawText(grounded ? "GND" : "OPEN", 168, y,
                     grounded ? COL_GREEN : COL_GRAY);
        }
        else
        {
            // Output mode: report the logical level (active-low pins show
            // ON/OFF so the USR LED reads correctly).
            active = GpioAppHigh[i];
            DrawText("OUT", 132, y, COL_LTGRAY);
            if (GpioAppInvert[i])
                DrawText(active ? "ON" : "OFF", 168, y,
                         active ? COL_GREEN : COL_GRAY);
            else
                DrawText(active ? "HIGH" : "LOW", 168, y,
                         active ? COL_GREEN : COL_GRAY);
        }

        // Level indicator box
        DrawFrame(234, y - 2, 42, FONTH + 4, COL_WHITE);
        DrawRect(236, y, 38, FONTH,
                 active ? COL_GREEN : RGBTO16(0, 40, 0));
    }

    DrawRect(24, GA_FOOT_Y - 8, WIDTH - 48, 1, COL_GRAY);

    const char* hint = "A:Toggle  Y:View  X:Exit";
    DrawText(hint, (WIDTH - (int)StrLen(hint) * FONTW) / 2, GA_FOOT_Y,
             COL_LTGRAY);

    DispUpdate();
}

void GpioAppRun(void)
{
    GpioAppEnsureInit();

    int selected = 0;
    Bool need_redraw = True;
    u32 last = Millis();

    while (1)
    {
        // Redraw on demand, and poll View-mode pins a few times per second so
        // the connection state stays live.
        Bool periodic = GpioAppAnyView() && ((u32)(Millis() - last) >= 100);
        if (need_redraw || periodic)
        {
            GpioAppRender(selected);
            need_redraw = False;
            last = Millis();
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            selected = (selected - 1 + GPIO_APP_PINS) % GPIO_APP_PINS;
            need_redraw = True;
            break;

        case KEY_DOWN:
            selected = (selected + 1) % GPIO_APP_PINS;
            need_redraw = True;
            break;

        case KEY_LEFT:
        case KEY_RIGHT:
        case KEY_A:
            // Only drive the pin in output mode.
            if (!GpioAppView[selected])
            {
                GpioAppHigh[selected] = !GpioAppHigh[selected];
                GpioAppConfigure(selected);
                need_redraw = True;
            }
            break;

        case KEY_Y:
            // Toggle View (input) mode for the selected pin.
            GpioAppView[selected] = !GpioAppView[selected];
            GpioAppConfigure(selected);
            need_redraw = True;
            break;

        case KEY_X:
            return;

        default:
            break;
        }
    }
}
