// ****************************************************************************
//
//                  PicoDeck — UF2 Inspector
//
//   Reads a .UF2 from the SD card, validates every block and shows what is
//   inside: version tag (AMTV), family, address range and block count.
//
//   Controls:
//     A : inspect another file
//     X : back to the menu
//
// ****************************************************************************

#include "../include.h"
#include "uf2_inspect.h"
#include "file_picker.h"
#include "theme.h"
#include "uf2_parser.h"

#define UI_X        12
#define UI_VAL_X    100
#define UI_LINE_Y   56
#define UI_LINE_H   17
#define UI_NAME_Y   34
#define UI_FOOT_DIV 206
#define UI_FOOT_Y   212

typedef struct {
    Bool valid;              // at least one good block
    u32  declared;           // block count from the first valid block
    u32  blocks;             // valid blocks seen
    u32  bad_blocks;         // blocks with bad magic / payload size
    u32  family;
    u32  start_addr;
    u32  end_addr;           // one past the last byte
    Bool ver_found;
    char ver[16];
} Uf2Report;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static u32 ReadLE32(const u8* p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static void Hex32(char* out, u32 v)
{
    static const char* hd = "0123456789ABCDEF";
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 8; i++)
        out[2 + i] = hd[(v >> ((7 - i) * 4)) & 0xF];
    out[10] = 0;
}

static const char* FamilyName(u32 id, char* buf)
{
    if (id == FAMILY_RP2040) return "RP2040";
    if (id == 0)             return "none";

    Hex32(buf, id);
    return buf;
}

static const char* BaseName(const char* path)
{
    const char* p = path;
    const char* base = path;
    while (*p)
    {
        if (*p == '/') base = p + 1;
        p++;
    }
    return base;
}

static void DrawLine(int y, const char* label, const char* value, u16 color)
{
    DrawText(label, UI_X, y, g_theme.menu_dim);
    DrawText(value, UI_VAL_X, y, color);
}

static void Frame(const char* title)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 8,
              g_theme.menu_title);
    DrawRect(20, 30, WIDTH - 40, 1, g_theme.menu_dim);
    DrawRect(20, UI_FOOT_DIV, WIDTH - 40, 1, g_theme.menu_dim);
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

static void ScanFile(const char* path, Uf2Report* r)
{
    memset(r, 0, sizeof(*r));

    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return;

    u8 block[UF2_BLOCK_SIZE];
    Bool first = True;
    u32  first_ps = 0;
    int  ver_state = 0;
    int  ver_len = 0;
    Bool ver_collect = False;

    while (1)
    {
        u32 n = FileRead(&f, block, UF2_BLOCK_SIZE);
        if (n == 0) break;

        Bool ok = (n == UF2_BLOCK_SIZE) &&
                  (ReadLE32(block + 0) == UF2_MAGIC_START_0) &&
                  (ReadLE32(block + 4) == UF2_MAGIC_START_1) &&
                  (ReadLE32(block + 508) == UF2_MAGIC_END);

        u32 flags = 0, addr = 0, ps = 0, num = 0, family = 0;
        if (ok)
        {
            flags  = ReadLE32(block + 8);
            addr   = ReadLE32(block + 12);
            ps     = ReadLE32(block + 16);
            num    = ReadLE32(block + 24);
            family = (flags & UF2_FLAG_FAMILY_ID) ? ReadLE32(block + 28) : 0;
            ok = (ps > 0) && (ps <= 476);
        }

        if (!ok)
        {
            if (first) break;
            r->bad_blocks++;
            continue;
        }

        if (first)
        {
            first = False;
            r->declared   = num;
            r->family     = family;
            r->start_addr = addr;
            r->end_addr   = addr + ps;
            first_ps      = ps;
        }
        else
        {
            if (num != r->declared) r->bad_blocks++;
            if (ps != first_ps)     r->bad_blocks++;
            if (family != r->family) r->bad_blocks++;
        }

        if (addr < r->start_addr) r->start_addr = addr;
        if (addr + ps > r->end_addr) r->end_addr = addr + ps;

        // Version tag: "AMTV" followed by digits and dots.
        for (u32 i = 0; i < ps && !r->ver_found; i++)
        {
            u8 c = block[32 + i];
            if (!ver_collect)
            {
                const char* tag = "AMTV";
                if (c == (u8)tag[ver_state])
                {
                    ver_state++;
                    if (ver_state == 4) ver_collect = True;
                }
                else
                {
                    ver_state = (c == (u8)'A') ? 1 : 0;
                }
            }
            else if ((c >= '0' && c <= '9') || c == '.')
            {
                if (ver_len < (int)sizeof(r->ver) - 1)
                    r->ver[ver_len++] = (char)c;
            }
            else
            {
                r->ver[ver_len] = 0;
                if (ver_len > 0) r->ver_found = True;
                ver_collect = False;
                ver_state = 0;
                ver_len = 0;
            }
        }

        r->blocks++;
    }

    FileClose(&f);

    if (!r->ver_found && ver_len > 0)
    {
        r->ver[ver_len] = 0;
        r->ver_found = True;
    }

    r->valid = (r->blocks > 0);
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

static void DrawReport(const char* path, const Uf2Report* r)
{
    Frame("UF2 Inspector");
    DrawText(BaseName(path), UI_X, UI_NAME_Y, g_theme.menu_text);

    char buf[48];
    char hex[24];

    if (!r->valid)
    {
        DrawText("Not a UF2 file.", UI_X, 80, g_theme.err_color);
        DrawText("A: another    X: back", UI_X, UI_FOOT_Y,
                 g_theme.menu_dim);
        DispUpdate();
        return;
    }

    int y = UI_LINE_Y;

    DecNum(buf, r->blocks, 0);
    strcat(buf, " / ");
    DecNum(buf + StrLen(buf), r->declared, 0);
    DrawLine(y, "Blocks:", buf,
             (r->declared == r->blocks) ? g_theme.menu_text : g_theme.warn_color);
    y += UI_LINE_H;

    if (r->ver_found)
    {
        strcpy(buf, "v");
        strcat(buf, r->ver);
        DrawLine(y, "Version:", buf, g_theme.ok_color);
    }
    else
    {
        DrawLine(y, "Version:", "none", g_theme.menu_dim);
    }
    y += UI_LINE_H;

    DrawLine(y, "Family:", FamilyName(r->family, hex), g_theme.menu_text);
    y += UI_LINE_H;

    Hex32(hex, r->start_addr);
    strcat(hex, "-");
    Hex32(hex + 11, r->end_addr);
    DrawLine(y, "Range:", hex, g_theme.menu_text); y += UI_LINE_H;

    Bool status_ok = (r->bad_blocks == 0) && (r->declared == r->blocks);

    DrawLine(y, "Status:", status_ok ? "OK" : "CHECK",
             status_ok ? g_theme.ok_color : g_theme.err_color);

    DrawText("A: another    X: back", UI_X, UI_FOOT_Y,
             g_theme.menu_dim);
    DispUpdate();
}

static void DrawNoCard(void)
{
    Frame("UF2 Inspector");
    DrawText("SD card not found.", UI_X, 80, g_theme.err_color);
    DrawText("X: back", UI_X, UI_FOOT_Y, g_theme.menu_dim);
    DispUpdate();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void Uf2InspectRun(void)
{
    if (!DiskAutoMount())
    {
        DrawNoCard();
        while (KeyGet() != KEY_X) WaitMs(30);
        return;
    }

    char path[128];

    while (1)
    {
        if (FilePickerRun("/", "*.UF2", path, sizeof(path)) !=
            PICKER_RESULT_SELECTED)
            return;

        Uf2Report r;
        ScanFile(path, &r);
        DrawReport(path, &r);

        while (1)
        {
            u8 k = KeyGet();
            if (k == KEY_A) break;
            if (k == KEY_X) return;
            WaitMs(30);
        }
    }
}
