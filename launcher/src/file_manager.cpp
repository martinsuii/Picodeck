// ****************************************************************************
//
//                  PicoDeck — SD file manager
//
//   Navigate the SD card. Keys:
//     UP/DOWN   move selection
//     LEFT/RIGHT page up/down
//     A         open folder
//     Y         delete selected file/folder (asks for confirmation)
//     X         go up one level; at root, exit
//
// ****************************************************************************

#include "../include.h"
#include "file_manager.h"
#include "apps.h"
#include "keyboard.h"
#include "text_editor.h"

#define FM_MAX       64
#define FM_NAME_LEN  32
#define FM_PATH_LEN  256

#define FM_TITLE_Y   12
#define FM_DIV_Y     30
#define FM_LIST_Y    42
#define FM_LINE_H    12
#define FM_VISIBLE   14
#define FM_FOOT_Y    220

typedef struct {
    char name[FM_NAME_LEN];
    u8   type;      // 0 = file, 1 = dir, 2 = parent
    u32  size;
} FmEntry;

static FmEntry g_fm[FM_MAX];
static int     g_fm_count;
static char    g_fm_path[FM_PATH_LEN];

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

static int FmPush(char* path, const char* name)
{
    int plen = (int)StrLen(path);
    int nlen = (int)StrLen(name);
    Bool slash = (plen > 0) && (path[plen - 1] != '/');
    if (plen + (slash ? 1 : 0) + nlen + 1 > FM_PATH_LEN) return -1;
    if (slash) path[plen++] = '/';
    memcpy(path + plen, name, nlen);
    path[plen + nlen] = 0;
    return plen + nlen;
}

static void FmPop(char* path)
{
    int n = (int)StrLen(path);
    if (n <= 1) { path[0] = '/'; path[1] = 0; return; }
    if (path[n - 1] == '/') { path[--n] = 0; }
    int i = n - 1;
    while (i > 0 && path[i] != '/') i--;
    if (i == 0) { path[0] = '/'; path[1] = 0; }
    else path[i] = 0;
}

static Bool FmIsRoot(const char* path)
{
    return path[0] == '/' && path[1] == 0;
}

static void FmFullPath(char* out, int outlen, const char* dir, const char* name)
{
    int n = 0;
    while (dir[n] && n < outlen - 1) { out[n] = dir[n]; n++; }
    if (n > 0 && out[n - 1] != '/' && n < outlen - 1) out[n++] = '/';
    int i = 0;
    while (name[i] && n < outlen - 1) out[n++] = name[i++];
    out[n] = 0;
}

// ---------------------------------------------------------------------------
// Directory listing
// ---------------------------------------------------------------------------

static int FmLoad(const char* dir)
{
    g_fm_count = 0;

    if (!FmIsRoot(dir))
    {
        strcpy(g_fm[g_fm_count].name, "..");
        g_fm[g_fm_count].type = 2;
        g_fm[g_fm_count].size = 0;
        g_fm_count++;
    }

    sFile find;
    sFileInfo info;

    // Directories first
    FileInit(&find);
    if (!FindOpen(&find, dir)) return -1;
    while (FindNext(&find, &info, ATTR_DIR_MASK, "*"))
    {
        if (g_fm_count >= FM_MAX) break;
        if (info.namelen == 0 || info.name[0] == '.') continue;
        if (!(info.attr & ATTR_DIR)) continue;
        int n = info.namelen;
        if (n >= FM_NAME_LEN) n = FM_NAME_LEN - 1;
        memcpy(g_fm[g_fm_count].name, info.name, n);
        g_fm[g_fm_count].name[n] = 0;
        g_fm[g_fm_count].type = 1;
        g_fm[g_fm_count].size = 0;
        g_fm_count++;
    }
    FindClose(&find);

    // Files
    FileInit(&find);
    if (!FindOpen(&find, dir)) return g_fm_count;
    while (FindNext(&find, &info, ATTR_ARCH, "*"))
    {
        if (g_fm_count >= FM_MAX) break;
        if (info.namelen == 0 || info.name[0] == '.') continue;
        if (info.attr & ATTR_DIR) continue;
        int n = info.namelen;
        if (n >= FM_NAME_LEN) n = FM_NAME_LEN - 1;
        memcpy(g_fm[g_fm_count].name, info.name, n);
        g_fm[g_fm_count].name[n] = 0;
        g_fm[g_fm_count].type = 0;
        g_fm[g_fm_count].size = info.size;
        g_fm_count++;
    }
    FindClose(&find);

    return g_fm_count;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

static void FmShowMessage(const char* l1, const char* l2)
{
    DrawClear();
    DrawText2("File manager", (WIDTH - 12 * FONTW * 2) / 2, 30, COL_YELLOW);
    if (l1) DrawText(l1, 20, 100, COL_WHITE);
    if (l2) DrawText(l2, 20, 120, COL_LTGRAY);
    DrawText("Press X to return", 20, FM_FOOT_Y, COL_GRAY);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

static void FmRender(int selected, int scroll)
{
    DrawClear();

    // Title = current path (truncated from the left if too long)
    char title[64];
    int plen = (int)StrLen(g_fm_path);
    if (plen <= 40) strcpy(title, g_fm_path);
    else { strcpy(title, "..."); strcat(title, g_fm_path + plen - 37); }
    DrawText(title, 10, FM_TITLE_Y, COL_YELLOW);
    DrawRect(10, FM_DIV_Y, WIDTH - 20, 1, COL_GRAY);

    for (int i = 0; i < FM_VISIBLE; i++)
    {
        int idx = scroll + i;
        if (idx >= g_fm_count) break;

        int y = FM_LIST_Y + i * FM_LINE_H;
        FmEntry* e = &g_fm[idx];
        Bool sel = (idx == selected);

        if (sel)
            DrawRect(8, y - 1, WIDTH - 16, FONTH + 2, RGBTO16(40, 40, 0));

        u16 col = sel ? COL_YELLOW : COL_WHITE;
        const char* tag = (e->type == 1) ? "[D] " : (e->type == 2) ? "[..]" : "    ";
        DrawText(tag, 12, y, (e->type == 1) ? COL_CYAN : col);
        DrawText(e->name, 44, y, col);

        if (e->type == 0)
        {
            char b[16];
            DecNum(b, e->size, 0);
            DrawText(b, WIDTH - 12 - (int)StrLen(b) * FONTW, y, COL_LTGRAY);
        }
    }

    DrawRect(10, FM_FOOT_Y - 8, WIDTH - 20, 1, COL_GRAY);
    DrawText("A:Open Y:Del L:New R:Ren X:Up", 10, FM_FOOT_Y, COL_LTGRAY);
    DispUpdate();
}

// Ask "delete <name>?" — returns True if the user confirms with A.
static Bool FmConfirmDelete(const char* name)
{
    DrawClear();
    DrawText2("Delete", (WIDTH - 6 * FONTW * 2) / 2, 40, COL_RED);
    DrawText("Delete this entry?", 20, 100, COL_WHITE);
    DrawText(name, 20, 120, COL_YELLOW);
    DrawText("A: Delete    X: Cancel", 20, FM_FOOT_Y, COL_LTGRAY);
    DispUpdate();

    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_X) return False;
        if (k == KEY_A) return True;
        WaitMs(20);
    }
}

// ---------------------------------------------------------------------------
// Text viewer
// ---------------------------------------------------------------------------

#define TV_MAX_LINES 1024
#define TV_VISIBLE   16

static char g_tv_buf[16384];
static u32  g_tv_line[TV_MAX_LINES];

// Load a file and split it into lines. Returns the line count.
static int FmTvLoad(const char* path)
{
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return 0;

    u32 n = FileRead(&f, g_tv_buf, sizeof(g_tv_buf) - 1);
    FileClose(&f);
    g_tv_buf[n] = 0;

    int count = 0;
    g_tv_line[count++] = 0;
    for (u32 i = 0; i < n; i++)
    {
        if (g_tv_buf[i] == '\r') g_tv_buf[i] = 0;
        else if (g_tv_buf[i] == '\n')
        {
            g_tv_buf[i] = 0;
            if (count < TV_MAX_LINES) g_tv_line[count++] = i + 1;
        }
    }
    return count;
}

static void FmTvRender(const char* name, int top, int count)
{
    DrawClear();

    DrawText(name, 6, 8, COL_YELLOW);
    DrawRect(6, 22, WIDTH - 12, 1, COL_GRAY);

    for (int i = 0; i < TV_VISIBLE; i++)
    {
        int li = top + i;
        if (li >= count) break;
        DrawText(&g_tv_buf[g_tv_line[li]], 6, 28 + i * 12, COL_WHITE);
    }

    char b[32];
    char num[12];
    strcpy(b, "");
    DecNum(num, top + 1, 0); strcat(b, num);
    strcat(b, "/");
    DecNum(num, count, 0); strcat(b, num);
    DrawText(b, 6, 226, COL_LTGRAY);
    DrawText("UP/DN  L/R page  X: Back", WIDTH - 25 * FONTW - 6, 226,
             COL_LTGRAY);
    DispUpdate();
}

static void FmTextViewer(const char* name, const char* path)
{
    int count = FmTvLoad(path);
    if (count == 0)
    {
        FmShowMessage("Empty or unreadable", name);
        return;
    }

    int max_top = count - TV_VISIBLE;
    if (max_top < 0) max_top = 0;

    int top = 0;
    Bool need_redraw = True;

    while (1)
    {
        if (need_redraw) { FmTvRender(name, top, count); need_redraw = False; }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            if (top > 0) { top--; need_redraw = True; }
            break;
        case KEY_DOWN:
            if (top < max_top) { top++; need_redraw = True; }
            break;
        case KEY_LEFT:
            top -= TV_VISIBLE;
            if (top < 0) top = 0;
            need_redraw = True;
            break;
        case KEY_RIGHT:
            top += TV_VISIBLE;
            if (top > max_top) top = max_top;
            need_redraw = True;
            break;
        case KEY_X:
            return;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

void FileManagerRun(void)
{
    if (!DiskAutoMount())
    {
        FmShowMessage("SD card not found", "Insert a card and try again");
        return;
    }

    strcpy(g_fm_path, "/");
    int selected = 0;
    int scroll = 0;
    Bool need_reload = True;
    Bool need_redraw = True;

    while (1)
    {
        if (need_reload)
        {
            if (FmLoad(g_fm_path) < 0)
            {
                FmShowMessage("Cannot open folder", g_fm_path);
                FmPop(g_fm_path);
            }
            selected = 0;
            scroll = 0;
            need_reload = False;
            need_redraw = True;
        }

        if (selected < scroll) scroll = selected;
        if (selected >= scroll + FM_VISIBLE) scroll = selected - FM_VISIBLE + 1;
        if (scroll < 0) scroll = 0;

        if (need_redraw)
        {
            FmRender(selected, scroll);
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            if (selected > 0) { selected--; need_redraw = True; }
            break;

        case KEY_DOWN:
            if (selected < g_fm_count - 1) { selected++; need_redraw = True; }
            break;

        case KEY_LEFT:   // New folder
        {
            char name[32];
            name[0] = 0;
            if (KeyboardInput("New folder", name, sizeof(name)) && name[0])
            {
                char full[FM_PATH_LEN];
                FmFullPath(full, sizeof(full), g_fm_path, name);
                if (!DirCreate(full)) FmShowMessage("Create failed", full);
                need_reload = True;
            }
            else need_redraw = True;
            break;
        }

        case KEY_RIGHT:  // Rename selected
        {
            if (g_fm_count == 0 || g_fm[selected].type == 2)
            {
                need_redraw = True;
                break;
            }
            char name[32];
            strcpy(name, g_fm[selected].name);
            if (KeyboardInput("Rename", name, sizeof(name)) && name[0])
            {
                char oldp[FM_PATH_LEN], newp[FM_PATH_LEN];
                FmFullPath(oldp, sizeof(oldp), g_fm_path, g_fm[selected].name);
                FmFullPath(newp, sizeof(newp), g_fm_path, name);
                if (!FileMove(oldp, newp)) FmShowMessage("Rename failed", newp);
                need_reload = True;
            }
            else need_redraw = True;
            break;
        }

        case KEY_A:
            if (g_fm_count == 0) break;
            if (g_fm[selected].type == 2)
            {
                FmPop(g_fm_path);
                need_reload = True;
            }
            else if (g_fm[selected].type == 1)
            {
                if (FmPush(g_fm_path, g_fm[selected].name) < 0)
                    FmShowMessage("Path too long", NULL);
                else
                    need_reload = True;
            }
            else
            {
                // File: let an installed app claim the extension, otherwise
                // fall back to the text editor.
                char full[FM_PATH_LEN];
                FmFullPath(full, sizeof(full), g_fm_path, g_fm[selected].name);
                if (!AppOpenWithRun(full))
                    TextEditorRun(g_fm[selected].name, full);
                need_redraw = True;
            }
            break;

        case KEY_Y:
            if (g_fm_count == 0) break;
            if (g_fm[selected].type == 2) break;   // don't delete ".."
            {
                char full[FM_PATH_LEN];
                FmFullPath(full, sizeof(full), g_fm_path, g_fm[selected].name);
                if (FmConfirmDelete(g_fm[selected].name))
                {
                    if (!FileDelete(full))
                        FmShowMessage("Delete failed", full);
                    need_reload = True;
                }
                else
                {
                    need_redraw = True;
                }
            }
            break;

        case KEY_X:
            if (FmIsRoot(g_fm_path)) return;
            FmPop(g_fm_path);
            need_reload = True;
            break;

        default:
            break;
        }
    }
}
