// ****************************************************************************
//
//                  PicoDeck — text editor
//
//   Line-based editor for small text files on the SD card.
//     UP/DOWN   move the cursor line
//     LEFT/RIGHT page up/down
//     A         edit the current line (on-screen keyboard)
//     Y         insert an empty line below the cursor
//     X         exit (asks to save)
//
// ****************************************************************************

#include "../include.h"
#include "text_editor.h"
#include "keyboard.h"

#define TED_MAX_LINES 64
#define TED_LINE_LEN  64
#define TED_VISIBLE   16

static char g_ted[TED_MAX_LINES][TED_LINE_LEN];
static int  g_ted_count;
static char g_ted_raw[8192];

static void TedLoad(const char* path)
{
    g_ted_count = 0;

    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return;
    u32 n = FileRead(&f, g_ted_raw, sizeof(g_ted_raw) - 1);
    FileClose(&f);
    g_ted_raw[n] = 0;

    char* p = g_ted_raw;
    while (g_ted_count < TED_MAX_LINES)
    {
        if (*p == 0) break;
        char* line = p;
        while (*p && *p != '\n') p++;
        int len = (int)(p - line);
        if (len > TED_LINE_LEN - 1) len = TED_LINE_LEN - 1;
        memcpy(g_ted[g_ted_count], line, len);
        g_ted[g_ted_count][len] = 0;
        g_ted_count++;
        if (*p == '\n') { p++; if (*p == 0) break; }
    }

    if (g_ted_count == 0)
    {
        g_ted[0][0] = 0;
        g_ted_count = 1;
    }
}

static Bool TedSave(const char* path)
{
    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, path)) return False;
    for (int i = 0; i < g_ted_count; i++)
    {
        FileWrite(&f, g_ted[i], (u32)StrLen(g_ted[i]));
        FileWrite(&f, "\n", 1);
    }
    FileClose(&f);
    return True;
}

static void TedRender(const char* name, int top, int sel)
{
    DrawClear();
    DrawText(name, 6, 8, COL_YELLOW);
    DrawRect(6, 22, WIDTH - 12, 1, COL_GRAY);

    for (int i = 0; i < TED_VISIBLE; i++)
    {
        int li = top + i;
        if (li >= g_ted_count) break;
        int y = 28 + i * 12;
        Bool cur = (li == sel);
        if (cur)
            DrawRect(4, y - 1, WIDTH - 8, FONTH + 2, RGBTO16(40, 40, 0));
        DrawText(g_ted[li], 6, y, cur ? COL_YELLOW : COL_WHITE);
    }

    char b[32];
    char num[8];
    strcpy(b, ""); DecNum(num, sel + 1, 0); strcat(b, num);
    strcat(b, "/"); DecNum(num, g_ted_count, 0); strcat(b, num);
    DrawText(b, 6, 226, COL_LTGRAY);
    DrawText("A:Edit Y:New  X:Save+Exit", WIDTH - 25 * FONTW - 6, 226,
             COL_LTGRAY);
    DispUpdate();
}

void TextEditorRun(const char* name, const char* path)
{
    TedLoad(path);

    int sel = 0;
    int top = 0;
    Bool need_redraw = True;

    while (1)
    {
        if (sel < top) top = sel;
        if (sel >= top + TED_VISIBLE) top = sel - TED_VISIBLE + 1;
        if (top < 0) top = 0;

        if (need_redraw) { TedRender(name, top, sel); need_redraw = False; }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            if (sel > 0) { sel--; need_redraw = True; }
            break;
        case KEY_DOWN:
            if (sel < g_ted_count - 1) { sel++; need_redraw = True; }
            break;
        case KEY_LEFT:
            sel -= TED_VISIBLE; if (sel < 0) sel = 0; need_redraw = True;
            break;
        case KEY_RIGHT:
            sel += TED_VISIBLE;
            if (sel >= g_ted_count) sel = g_ted_count - 1;
            need_redraw = True;
            break;

        case KEY_A:
            KeyboardInput("Edit line", g_ted[sel], TED_LINE_LEN);
            need_redraw = True;
            break;

        case KEY_Y:
            if (g_ted_count < TED_MAX_LINES)
            {
                for (int i = g_ted_count; i > sel + 1; i--)
                    strcpy(g_ted[i], g_ted[i - 1]);
                g_ted[sel + 1][0] = 0;
                g_ted_count++;
                sel++;
                need_redraw = True;
            }
            break;

        case KEY_X:
        {
            DrawTextBg("Save? A:Yes  X:No", 6, 226, COL_YELLOW, COL_BLACK);
            DispUpdate();
            while (1)
            {
                u8 s = KeyGet();
                if (s == KEY_X) break;
                if (s == KEY_A)
                {
                    if (TedSave(path))
                    {
                        DrawTextBg("Saved.                  ", 6, 226,
                                   COL_GREEN, COL_BLACK);
                        DispUpdate();
                        WaitMs(800);
                    }
                    break;
                }
                WaitMs(20);
            }
            return;
        }

        default:
            break;
        }
    }
}
