// ****************************************************************************
//
//                  PicoDeck — Image viewer
//
//   Minimal BMP viewer: 24- or 32-bit uncompressed BMPs, scaled to fit the
//   320x240 display (aspect ratio preserved, centred). Picks files from SD.
//
// ****************************************************************************

#include "../include.h"
#include "image_viewer.h"
#include "file_picker.h"

#define IV_MAX_SRC_W   1024

static u8  g_row_raw[IV_MAX_SRC_W * 4];
static u16 g_row565[IV_MAX_SRC_W];

static u32 RdU16(const u8* p) { return (u32)p[0] | ((u32)p[1] << 8); }
static u32 RdU32(const u8* p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void IvMessage(const char* l1, const char* l2)
{
    DrawClear();
    DrawText2("Image viewer", (WIDTH - 12 * FONTW * 2) / 2, 30, COL_YELLOW);
    if (l1) DrawText(l1, 20, 100, COL_WHITE);
    if (l2) DrawText(l2, 20, 120, COL_LTGRAY);
    DrawText("Press X to return", 20, 220, COL_GRAY);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

// Load and draw one BMP. Returns False on parse/read error.
static Bool IvShow(const char* path)
{
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return False;

    u8 hdr[54];
    if (FileRead(&f, hdr, 54) != 54) { FileClose(&f); return False; }
    if (hdr[0] != 'B' || hdr[1] != 'M') { FileClose(&f); return False; }

    u32 data_off = RdU32(hdr + 10);
    int w = (int)RdU32(hdr + 18);
    int h = (int)RdU32(hdr + 22);
    u16 bpp = (u16)RdU16(hdr + 28);
    u32 comp = RdU32(hdr + 30);

    Bool bottom_up = (h > 0);
    if (h < 0) h = -h;

    if (comp != 0 || (bpp != 24 && bpp != 32) ||
        w <= 0 || h <= 0 || w > IV_MAX_SRC_W)
    {
        FileClose(&f);
        return False;
    }

    int bytes_pp = bpp / 8;
    u32 stride = ((u32)w * (u32)bytes_pp + 3u) & ~3u;

    // Fit into the display, preserving aspect ratio.
    int dw = w, dh = h;
    if (dw > WIDTH)  { dh = dh * WIDTH / dw;  dw = WIDTH; }
    if (dh > HEIGHT) { dw = dw * HEIGHT / dh; dh = HEIGHT; }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    int dest_x = (WIDTH - dw) / 2;
    int dest_y = (HEIGHT - dh) / 2;

    DrawClear();

    for (int dy = 0; dy < dh; dy++)
    {
        int sy = (int)((u32)dy * (u32)h / (u32)dh);
        u32 rowoff = data_off + (u32)(bottom_up ? (h - 1 - sy) : sy) * stride;

        FileSeek(&f, rowoff);
        u32 want = (u32)w * (u32)bytes_pp;
        if (FileRead(&f, g_row_raw, want) != want) break;

        for (int x = 0; x < w; x++)
        {
            u8 b = g_row_raw[x * bytes_pp + 0];
            u8 g = g_row_raw[x * bytes_pp + 1];
            u8 r = g_row_raw[x * bytes_pp + 2];
            g_row565[x] = RGBTO16(r, g, b);
        }

        DrawImgLine(g_row565, dest_x, dest_y + dy, dw, w, 0);
    }

    FileClose(&f);
    DispUpdate();

    // Brief hint overlay, then wait for X.
    DrawTextBg("X: Back", WIDTH - 8 * FONTW - 4, HEIGHT - 9, COL_WHITE, COL_BLACK);
    DispUpdate();
    WaitMs(1500);

    while (KeyGet() != KEY_X) WaitMs(50);
    return True;
}

void ImageViewerRun(void)
{
    if (!DiskAutoMount())
    {
        IvMessage("SD card not found", "Insert a card and try again");
        return;
    }

    char path[256];
    PickerResult r = FilePickerRun("/", "*.BMP", path, sizeof(path));
    if (r != PICKER_RESULT_SELECTED) return;

    if (!IvShow(path))
    {
        IvMessage("Cannot display image",
                  "Only 24/32-bit uncompressed BMP up to 1024 px wide");
    }
}
