// ****************************************************************************
//
//                  PicoDeck — Calculator
//
//   D-pad to move, A to press a key, X to exit.
//
// ****************************************************************************

#include "../include.h"
#include "calculator.h"

#define CALC_COLS 4
#define CALC_ROWS 5
#define CALC_X0   20
#define CALC_Y0   74
#define CALC_CW   70
#define CALC_CH   28

static const char* const CalcKeys[CALC_ROWS][CALC_COLS] = {
    {"C",  "DEL", "%", "/"},
    {"7",  "8",   "9", "*"},
    {"4",  "5",   "6", "-"},
    {"1",  "2",   "3", "+"},
    {"0",  ".",   "=", ""},
};

static char  g_entry[24];
static Bool  g_entering;
static float g_acc;
static char  g_op;

static float ParseFloat(const char* s)
{
    float v = 0.0f;
    float frac = 0.0f;
    float scale = 1.0f;
    Bool dot = False;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    while (*s)
    {
        if (*s == '.') { dot = True; s++; continue; }
        if (*s < '0' || *s > '9') { s++; continue; }
        if (!dot) v = v * 10.0f + (float)(*s - '0');
        else { scale *= 0.1f; frac += (float)(*s - '0') * scale; }
        s++;
    }
    return (float)sign * (v + frac);
}

static void FormatFloat(char* out, float v)
{
    Bool neg = (v < 0.0f);
    if (neg) v = -v;
    long scaled = (long)(v * 100.0f + 0.5f);
    long ip = scaled / 100;
    long fp = scaled % 100;

    char tmp[16];
    int m = 0;
    if (ip == 0) tmp[m++] = '0';
    else { while (ip > 0) { tmp[m++] = (char)('0' + ip % 10); ip /= 10; } }

    int n = 0;
    if (neg) out[n++] = '-';
    while (m > 0) out[n++] = tmp[--m];
    out[n++] = '.';
    out[n++] = (char)('0' + (fp / 10) % 10);
    out[n++] = (char)('0' + fp % 10);
    out[n] = 0;
}

static float Apply(float a, float b, char op)
{
    switch (op)
    {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': return (b == 0.0f) ? 0.0f : a / b;
    }
    return b;
}

static void CalcRender(int row, int col)
{
    DrawClear();
    DrawText2("Calculator", (WIDTH - 10 * FONTW * 2) / 2, 10, COL_YELLOW);

    // Display
    DrawRect(18, 34, WIDTH - 36, 22, RGBTO16(20, 20, 20));
    DrawFrame(18, 34, WIDTH - 36, 22, COL_GRAY);

    char shown[40];
    if (g_entering) strcpy(shown, g_entry);
    else FormatFloat(shown, g_acc);
    DrawText(shown, WIDTH - 24 - (int)StrLen(shown) * FONTW, 40, COL_WHITE);

    // Keypad
    for (int r = 0; r < CALC_ROWS; r++)
    {
        for (int c = 0; c < CALC_COLS; c++)
        {
            const char* lab = CalcKeys[r][c];
            if (lab[0] == 0) continue;

            int x = CALC_X0 + c * CALC_CW;
            int y = CALC_Y0 + r * CALC_CH;
            Bool sel = (r == row && c == col);

            DrawRect(x, y, CALC_CW - 4, CALC_CH - 4,
                     sel ? RGBTO16(60, 60, 0) : RGBTO16(25, 25, 25));
            DrawFrame(x, y, CALC_CW - 4, CALC_CH - 4, COL_GRAY);
            DrawText(lab, x + (CALC_CW - 4 - (int)StrLen(lab) * FONTW) / 2,
                     y + 8, sel ? COL_YELLOW : COL_WHITE);
        }
    }

    DrawText("D-pad: Move   A: Press   X: Exit", 10, 226, COL_LTGRAY);
    DispUpdate();
}

static Bool CalcEmpty(int r, int c)
{
    return CalcKeys[r][c][0] == 0;
}

void CalculatorRun(void)
{
    g_entry[0] = 0;
    g_entering = False;
    g_acc = 0.0f;
    g_op = 0;

    int row = 1, col = 0;   // start on "7"
    Bool need_redraw = True;

    while (1)
    {
        if (need_redraw)
        {
            CalcRender(row, col);
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            do { row = (row - 1 + CALC_ROWS) % CALC_ROWS; } while (CalcEmpty(row, col));
            need_redraw = True;
            break;
        case KEY_DOWN:
            do { row = (row + 1) % CALC_ROWS; } while (CalcEmpty(row, col));
            need_redraw = True;
            break;
        case KEY_LEFT:
            do { col = (col - 1 + CALC_COLS) % CALC_COLS; } while (CalcEmpty(row, col));
            need_redraw = True;
            break;
        case KEY_RIGHT:
            do { col = (col + 1) % CALC_COLS; } while (CalcEmpty(row, col));
            need_redraw = True;
            break;

        case KEY_A:
        {
            const char* key = CalcKeys[row][col];
            char ch = key[0];

            if (ch >= '0' && ch <= '9')
            {
                if (!g_entering) { g_entry[0] = 0; g_entering = True; }
                int n = (int)StrLen(g_entry);
                if (n < (int)sizeof(g_entry) - 1)
                {
                    g_entry[n] = ch;
                    g_entry[n + 1] = 0;
                }
            }
            else if (ch == '.')
            {
                if (!g_entering) { g_entry[0] = 0; g_entering = True; }
                Bool has_dot = False;
                for (const char* p = g_entry; *p; p++)
                    if (*p == '.') { has_dot = True; break; }
                if (!has_dot)
                {
                    int n = (int)StrLen(g_entry);
                    if (n == 0) { g_entry[0] = '0'; g_entry[1] = 0; n = 1; }
                    if (n < (int)sizeof(g_entry) - 1)
                    {
                        g_entry[n] = '.';
                        g_entry[n + 1] = 0;
                    }
                }
            }
            else if (key[0] == 'C' && key[1] == 0)
            {
                g_entry[0] = 0;
                g_entering = False;
                g_acc = 0.0f;
                g_op = 0;
            }
            else if (key[0] == 'D')   // DEL
            {
                if (g_entering)
                {
                    int n = (int)StrLen(g_entry);
                    if (n > 0) g_entry[n - 1] = 0;
                }
            }
            else if (ch == '%')
            {
                if (g_entering) FormatFloat(g_entry, ParseFloat(g_entry) / 100.0f);
            }
            else if (ch == '=')
            {
                float b = g_entering ? ParseFloat(g_entry) : g_acc;
                if (g_op) g_acc = Apply(g_acc, b, g_op);
                else      g_acc = b;
                g_op = 0;
                g_entering = False;
            }
            else if (ch == '+' || ch == '-' || ch == '*' || ch == '/')
            {
                if (g_op && g_entering)
                    g_acc = Apply(g_acc, ParseFloat(g_entry), g_op);
                else
                    g_acc = g_entering ? ParseFloat(g_entry) : g_acc;
                g_op = ch;
                g_entering = False;
            }

            need_redraw = True;
            break;
        }

        case KEY_X:
            return;

        default:
            break;
        }
    }
}
