// ****************************************************************************
//
//                  PicoDeck — Stopwatch and timer
//
// ****************************************************************************

#include "../include.h"
#include "clock_apps.h"

// Format ms as "MM:SS.CC".
static void FormatTime(char* out, u32 ms)
{
    u32 total_s = ms / 1000u;
    u32 mm = total_s / 60u;
    u32 ss = total_s % 60u;
    u32 cc = (ms % 1000u) / 10u;

    out[0] = (char)('0' + (mm / 10) % 10);
    out[1] = (char)('0' + mm % 10);
    out[2] = ':';
    out[3] = (char)('0' + ss / 10);
    out[4] = (char)('0' + ss % 10);
    out[5] = '.';
    out[6] = (char)('0' + cc / 10);
    out[7] = (char)('0' + cc % 10);
    out[8] = 0;
}

static void BigTime(const char* t, u16 col)
{
    DrawText2(t, (WIDTH - (int)StrLen(t) * FONTW * 2) / 2, 90, col);
}

// ---------------------------------------------------------------------------
// Stopwatch
// ---------------------------------------------------------------------------

void StopwatchRun(void)
{
    u32 elapsed = 0;
    u32 start = 0;
    Bool running = False;

    while (1)
    {
        u32 shown = elapsed + (running ? (u32)(Millis() - start) : 0);

        DrawClear();
        DrawText2("Stopwatch", (WIDTH - 9 * FONTW * 2) / 2, 24, COL_YELLOW);
        char t[12];
        FormatTime(t, shown);
        BigTime(t, running ? COL_GREEN : COL_WHITE);
        DrawText(running ? "Running" : "Stopped", 20, 150,
                 running ? COL_GREEN : COL_GRAY);
        DrawText("A: Start/Stop   Y: Reset   X: Exit", 10, 220, COL_LTGRAY);
        DispUpdate();

        u8 k = KeyGet();
        if (k != NOKEY)
        {
            if (k == KEY_A)
            {
                if (running) { elapsed += (u32)(Millis() - start); running = False; }
                else { start = Millis(); running = True; }
            }
            else if (k == KEY_Y) { elapsed = 0; running = False; }
            else if (k == KEY_X) return;
        }

        WaitMs(40);
    }
}

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------

void TimerRun(void)
{
    int total = 60;              // seconds
    int remaining_ms = total * 1000;
    Bool running = False;
    Bool expired = False;
    u32 start = 0;

    while (1)
    {
        if (running)
        {
            int el = (int)(Millis() - start);
            remaining_ms = total * 1000 - el;
            if (remaining_ms <= 0) { remaining_ms = 0; running = False; expired = True; }
        }

        DrawClear();
        DrawText2("Timer", (WIDTH - 5 * FONTW * 2) / 2, 24, COL_YELLOW);

        char t[12];
        FormatTime(t, (u32)remaining_ms);

        if (expired)
        {
            Bool flash = (Millis() / 300u) & 1u;
            BigTime(t, flash ? COL_RED : COL_YELLOW);
            DrawText("TIME UP!", (WIDTH - 8 * FONTW) / 2, 150,
                     flash ? COL_RED : COL_YELLOW);
            DrawText("Y: Reset   X: Exit", 10, 220, COL_LTGRAY);
        }
        else
        {
            BigTime(t, running ? COL_GREEN : COL_WHITE);
            if (!running)
                DrawText("UP/DN +/-1min  L/R +/-5s  A: Start  X: Exit",
                         10, 220, COL_LTGRAY);
            else
                DrawText("A: Pause   X: Exit", 10, 220, COL_LTGRAY);
        }
        DispUpdate();

        u8 k = KeyGet();
        if (k != NOKEY)
        {
            if (expired)
            {
                if (k == KEY_X) return;
                if (k == KEY_Y) { expired = False; remaining_ms = total * 1000; }
            }
            else if (!running)
            {
                if      (k == KEY_UP)    total += 60;
                else if (k == KEY_DOWN)  total = (total > 60) ? total - 60 : 0;
                else if (k == KEY_RIGHT) total += 5;
                else if (k == KEY_LEFT)  total = (total > 5) ? total - 5 : 0;
                else if (k == KEY_A) { if (total > 0) { running = True; start = Millis(); } }
                else if (k == KEY_X) return;
                remaining_ms = total * 1000;
            }
            else
            {
                if (k == KEY_A) { running = False; total = remaining_ms / 1000; }
                else if (k == KEY_X) return;
            }
        }

        WaitMs(40);
    }
}
