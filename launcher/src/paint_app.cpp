// ****************************************************************************
//
//                  PicoDeck — Paint
//
//   Draw with the D-pad, save the canvas as /paint.bmp on the SD card.
//     A  paint        B  cycle brush size
//     Y  cycle colour X  save + exit
//   (erase by selecting the white colour)
//
// ****************************************************************************

#include "../include.h"
#include "paint_app.h"

#define PAINT_TOP     16
#define PAINT_BOTTOM  230

static const u16 PaintColors[] = {
    RGBTO16(255, 255, 255), RGBTO16(255, 0, 0),
    RGBTO16(0, 255, 0),     RGBTO16(0, 0, 255),
    RGBTO16(255, 255, 0),   RGBTO16(255, 0, 255),
    RGBTO16(0, 255, 255),   RGBTO16(0, 0, 0),
};
#define PAINT_COLORS ((int)(sizeof(PaintColors) / sizeof(PaintColors[0])))

static const int PaintBrushSizes[] = { 1, 2, 3, 4, 6, 8, 12 };
#define PAINT_BRUSHES ((int)(sizeof(PaintBrushSizes) / sizeof(PaintBrushSizes[0])))

static int g_px, g_py;
static int g_color;
static int g_brush_idx;
static int g_brush;

static void WriteLE32(u8* p, u32 v)
{
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static void PaintChrome(void)
{
    // Top palette bar
    DrawRect(0, 0, WIDTH, PAINT_TOP, COL_BLACK);
    for (int i = 0; i < PAINT_COLORS; i++)
    {
        int x = 4 + i * 24;
        DrawRect(x, 3, 20, 10, PaintColors[i]);
        if (i == g_color) DrawFrame(x - 1, 2, 22, 12, COL_YELLOW);
    }

    // Brush size indicator
    char b[16];
    strcpy(b, "brush ");
    char num[4];
    DecNum(num, g_brush, 0);
    strcat(b, num);
    DrawText(b, WIDTH - (int)StrLen(b) * FONTW - 4, 3, COL_LTGRAY);

    // Bottom hint
    DrawRect(0, PAINT_BOTTOM, WIDTH, HEIGHT - PAINT_BOTTOM, COL_BLACK);
    DrawText("A:Paint  A+UP/DN:Brush  Y:Colour  X:Save+Exit",
             4, PAINT_BOTTOM + 1, COL_LTGRAY);
}

static void PaintMove(int dx, int dy)
{
    DrawPointInv(g_px, g_py);      // restore pixel under cursor
    g_px += dx;
    g_py += dy;
    if (g_px < 0) g_px = 0;
    if (g_px >= WIDTH) g_px = WIDTH - 1;
    if (g_py < PAINT_TOP) g_py = PAINT_TOP;
    if (g_py >= PAINT_BOTTOM) g_py = PAINT_BOTTOM - 1;
    DrawPointInv(g_px, g_py);      // draw cursor
    DispUpdate();
}

static void PaintPut(u16 color)
{
    int half = g_brush / 2;
    int x0 = g_px - half;
    int y0 = g_py - half;

    for (int dy = 0; dy < g_brush; dy++)
    {
        for (int dx = 0; dx < g_brush; dx++)
        {
            int x = x0 + dx;
            int y = y0 + dy;
            if (x >= 0 && x < WIDTH && y >= PAINT_TOP && y < PAINT_BOTTOM)
                DrawPoint(x, y, color);
        }
    }

    DrawPointInv(g_px, g_py);      // show cursor over the brush
    DispUpdate();
}

static Bool PaintSave(const char* path)
{
    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, path)) return False;

    u32 rowsize = ((u32)WIDTH * 3u + 3u) & ~3u;
    u32 filesize = 54u + rowsize * HEIGHT;

    u8 hdr[54];
    memset(hdr, 0, sizeof(hdr));
    hdr[0] = 'B'; hdr[1] = 'M';
    WriteLE32(hdr + 2, filesize);
    WriteLE32(hdr + 10, 54);
    WriteLE32(hdr + 14, 40);
    WriteLE32(hdr + 18, WIDTH);
    WriteLE32(hdr + 22, HEIGHT);
    hdr[26] = 1;
    hdr[28] = 24;
    WriteLE32(hdr + 34, rowsize * HEIGHT);
    if (FileWrite(&f, hdr, 54) != 54) { FileClose(&f); return False; }

    u8 row[WIDTH * 3 + 4];
    for (int y = HEIGHT - 1; y >= 0; y--)
    {
        for (int x = 0; x < WIDTH; x++)
        {
            u16 c = DrawGetPoint(x, y);
            u8 r = (u8)(((c >> 11) & 0x1F) << 3);
            u8 g = (u8)(((c >> 5) & 0x3F) << 2);
            u8 b = (u8)((c & 0x1F) << 3);
            row[x * 3 + 0] = b;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = r;
        }
        for (u32 i = (u32)WIDTH * 3u; i < rowsize; i++) row[i] = 0;
        if (FileWrite(&f, row, rowsize) != rowsize) { FileClose(&f); return False; }
    }
    FileClose(&f);
    return True;
}

void PaintAppRun(void)
{
    DrawClear();
    // White canvas
    DrawRect(0, PAINT_TOP, WIDTH, PAINT_BOTTOM - PAINT_TOP,
             RGBTO16(255, 255, 255));

    g_px = WIDTH / 2;
    g_py = (PAINT_TOP + PAINT_BOTTOM) / 2;
    g_color = 0;
    g_brush_idx = 0;
    g_brush = PaintBrushSizes[0];

    PaintChrome();
    DrawPointInv(g_px, g_py);
    DispUpdate();

    u32 last_move = 0;
    u32 last_brush = 0;

    while (1)
    {
        // Hold A + UP/DOWN to change brush size.
        Bool brush_mod = KeyPressed(KEY_A) &&
                         (KeyPressed(KEY_UP) || KeyPressed(KEY_DOWN));

        if (brush_mod)
        {
            if ((u32)(Millis() - last_brush) >= 150)
            {
                if (KeyPressed(KEY_UP))
                    g_brush_idx = (g_brush_idx + 1) % PAINT_BRUSHES;
                else
                    g_brush_idx = (g_brush_idx + PAINT_BRUSHES - 1) % PAINT_BRUSHES;
                g_brush = PaintBrushSizes[g_brush_idx];
                PaintChrome();
                DispUpdate();
                last_brush = Millis();
            }
        }
        // Otherwise, continuous cursor movement while a D-pad button is held.
        else if ((u32)(Millis() - last_move) >= 60)
        {
            int dx = 0, dy = 0;
            if (KeyPressed(KEY_UP))         dy = -1;
            else if (KeyPressed(KEY_DOWN))  dy = 1;
            if (KeyPressed(KEY_LEFT))       dx = -1;
            else if (KeyPressed(KEY_RIGHT)) dx = 1;
            if (dx || dy) { PaintMove(dx, dy); last_move = Millis(); }
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(10); continue; }

        switch (k)
        {
        case KEY_A:
            // Don't paint when A is being used as the brush modifier.
            if (!(KeyPressed(KEY_UP) || KeyPressed(KEY_DOWN)))
                PaintPut(PaintColors[g_color]);
            break;

        case KEY_B:
            g_brush_idx = (g_brush_idx + 1) % PAINT_BRUSHES;
            g_brush = PaintBrushSizes[g_brush_idx];
            PaintChrome();
            DispUpdate();
            break;

        case KEY_Y:
            g_color = (g_color + 1) % PAINT_COLORS;
            PaintChrome();
            DispUpdate();
            break;

        case KEY_X:
        {
            DrawTextBg("Save /paint.bmp? A:Yes X:No",
                       4, PAINT_BOTTOM + 1, COL_YELLOW, COL_BLACK);
            DispUpdate();
            while (1)
            {
                u8 s = KeyGet();
                if (s == KEY_X) break;
                if (s == KEY_A)
                {
                    if (!DiskAutoMount())
                        DrawTextBg("No SD card - not saved   ",
                                   4, PAINT_BOTTOM + 1, COL_RED, COL_BLACK);
                    else if (PaintSave("/paint.bmp"))
                        DrawTextBg("Saved to /paint.bmp      ",
                                   4, PAINT_BOTTOM + 1, COL_GREEN, COL_BLACK);
                    else
                        DrawTextBg("Save failed              ",
                                   4, PAINT_BOTTOM + 1, COL_RED, COL_BLACK);
                    DispUpdate();
                    WaitMs(1200);
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
