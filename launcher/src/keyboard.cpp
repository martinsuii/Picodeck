// ****************************************************************************
//
//                  PicoDeck — on-screen keyboard
//
//   A small modal keyboard for text entry (file names, passwords, etc.).
//   Includes a Caps Lock toggle (CAP) in the action row.
//
// ****************************************************************************

#include "../include.h"
#include "keyboard.h"

#define KB_COLS      10
#define KB_ROWS      4

#define KB_TITLE_Y   10
#define KB_INPUT_Y   40
#define KB_GRID_Y    78
#define KB_ROW_H     24
#define KB_CELL_W    30
#define KB_GRID_X    10

#define KB_ACT_Y     (KB_GRID_Y + KB_ROWS * KB_ROW_H + 8)
#define KB_ACT_N     6
#define KB_ACT_W     48
#define KB_ACT_GAP   4

static const char* const KbRowsAlpha[KB_ROWS] = {
    "1234567890",
    "qwertyuiop",
    "asdfghjkl.",
    "zxcvbnm_-/",
};

static const char* const KbRowsSym[KB_ROWS] = {
    "!@#$%^&*()",
    "+=[]{}:;'\"",
    "<>,.?/\\|~`",
    "          ",
};

// action row: DEL, SYM, CAP, SPC, OK, CAN
static const char* const KbActions[KB_ACT_N] = {
    "DEL", "SYM", "CAP", "SPC", "OK", "CAN"
};
#define KB_ACT_SYM 1
#define KB_ACT_CAP 2

// mode: 0 = character grid, 1 = action row
static void KbRender(const char* title, const char* buf, int len,
                     int mode, int row, int col, int act, Bool caps, Bool sym)
{
    DrawClear();

    DrawText(title, (WIDTH - (int)StrLen(title) * FONTW) / 2, KB_TITLE_Y,
             COL_YELLOW);

    // Input line with cursor
    DrawRect(18, KB_INPUT_Y, WIDTH - 36, 16, RGBTO16(20, 20, 20));
    DrawFrame(18, KB_INPUT_Y, WIDTH - 36, 16, COL_GRAY);

    char shown[64];
    int max_chars = (WIDTH - 44) / FONTW;
    int off = 0;
    if (len > max_chars - 1) off = len - (max_chars - 1);
    int n = 0;
    for (int i = off; i < len && n < (int)sizeof(shown) - 2; i++)
        shown[n++] = buf[i];
    shown[n++] = '_';
    shown[n] = 0;
    DrawText(shown, 22, KB_INPUT_Y + 4, COL_WHITE);

    // Character grid
    for (int r = 0; r < KB_ROWS; r++)
    {
        for (int c = 0; c < KB_COLS; c++)
        {
            int x = KB_GRID_X + c * KB_CELL_W;
            int y = KB_GRID_Y + r * KB_ROW_H;
            Bool sel = (mode == 0) && (r == row) && (c == col);

            if (sel)
                DrawRect(x, y - 3, KB_CELL_W - 2, FONTH + 6,
                         RGBTO16(60, 60, 0));

            char ch = (sym ? KbRowsSym : KbRowsAlpha)[r][c];
            if (!sym && caps && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
            char s[2];
            s[0] = ch;
            s[1] = 0;
            DrawText(s, x + 10, y, sel ? COL_YELLOW : COL_WHITE);
        }
    }

    // Action row
    {
        int total = KB_ACT_N * KB_ACT_W + (KB_ACT_N - 1) * KB_ACT_GAP;
        int x0 = (WIDTH - total) / 2;
        for (int i = 0; i < KB_ACT_N; i++)
        {
            int x = x0 + i * (KB_ACT_W + KB_ACT_GAP);
            Bool sel = (mode == 1) && (i == act);
            Bool on = ((i == KB_ACT_CAP) && caps) || ((i == KB_ACT_SYM) && sym);
            u16 bg = sel ? RGBTO16(60, 60, 0)
                         : (on ? RGBTO16(0, 60, 0) : RGBTO16(20, 20, 20));
            DrawRect(x, KB_ACT_Y, KB_ACT_W, FONTH + 8, bg);
            DrawFrame(x, KB_ACT_Y, KB_ACT_W, FONTH + 8, COL_GRAY);
            const char* lab = KbActions[i];
            DrawText(lab, x + (KB_ACT_W - (int)StrLen(lab) * FONTW) / 2,
                     KB_ACT_Y + 4, sel ? COL_YELLOW : (on ? COL_GREEN : COL_WHITE));
        }
    }

    DrawText("A: Select   Y: Backspace   X: Cancel", 20, 222, COL_LTGRAY);
    DispUpdate();
}

Bool KeyboardInput(const char* title, char* buf, int buflen)
{
    int len = (int)StrLen(buf);
    if (len > buflen - 1) len = buflen - 1;

    int mode = 0;
    int row = 0, col = 0, act = 0;
    Bool caps = False;
    Bool sym = False;
    Bool need_redraw = True;

    while (1)
    {
        if (need_redraw)
        {
            KbRender(title, buf, len, mode, row, col, act, caps, sym);
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            if (mode == 1) { mode = 0; row = KB_ROWS - 1; }
            else if (row > 0) row--;
            need_redraw = True;
            break;

        case KEY_DOWN:
            if (mode == 0)
            {
                if (row < KB_ROWS - 1) row++;
                else { mode = 1; act = 0; }
            }
            need_redraw = True;
            break;

        case KEY_LEFT:
            if (mode == 0) col = (col - 1 + KB_COLS) % KB_COLS;
            else act = (act - 1 + KB_ACT_N) % KB_ACT_N;
            need_redraw = True;
            break;

        case KEY_RIGHT:
            if (mode == 0) col = (col + 1) % KB_COLS;
            else act = (act + 1) % KB_ACT_N;
            need_redraw = True;
            break;

        case KEY_Y:
            if (len > 0) { len--; buf[len] = 0; need_redraw = True; }
            break;

        case KEY_X:
            return False;

        case KEY_A:
            if (mode == 0)
            {
                if (len < buflen - 1)
                {
                    char ch = (sym ? KbRowsSym : KbRowsAlpha)[row][col];
                    if (!sym && caps && ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
                    buf[len++] = ch;
                    buf[len] = 0;
                    need_redraw = True;
                }
            }
            else
            {
                switch (act)
                {
                case 0:  // DEL
                    if (len > 0) { len--; buf[len] = 0; need_redraw = True; }
                    break;
                case KB_ACT_SYM:  // symbol layer
                    sym = !sym;
                    need_redraw = True;
                    break;
                case KB_ACT_CAP:  // caps
                    caps = !caps;
                    need_redraw = True;
                    break;
                case 3:  // SPC
                    if (len < buflen - 1)
                    {
                        buf[len++] = ' ';
                        buf[len] = 0;
                        need_redraw = True;
                    }
                    break;
                case 4:  // OK
                    buf[len] = 0;
                    return True;
                case 5:  // CAN
                    return False;
                }
            }
            break;

        default:
            break;
        }
    }
}
