// ****************************************************************************
//
//                  PicoDeck — custom apps
//
//   Flow:
//     APPS -> My Apps / App Store
//     App Store -> From SD card -> pick a valid /apps/<id> package
//     A on an installed app: copy app.bin to the app slot if needed, then jump
//
//   The app slot is a fixed 252 KB strip below the launcher. It is never
//   touched by game/module flashing and never contains boot2.
//
// ****************************************************************************

#include "../include.h"
#include "apps.h"
#include "menu.h"
#include "native_wifi.h"
#include "theme.h"
#include "keyboard.h"
#include "rubber_ducky.h"
#include "store_net.h"

extern FRAMETYPE ALIGNED FrameBuf[FRAMESIZE];

#define APPS_DIR        "/apps"
#define APPS_CFG        "/apps/apps.cfg"

#define APPS_MAX        11
#define APP_ID_LEN      24
#define APP_NAME_LEN    32
#define APPS_BUF_LEN    1024

#define APPS_TITLE_Y    14
#define APPS_DIV_Y      34
#define APPS_LIST_Y     44
#define APPS_LINE_H     18
#define APPS_VISIBLE    9
#define APPS_FOOT_Y     216

#define APP_RECORD_MAGIC 0x50415050u   // "PAPP"

typedef struct {
    char id[APP_ID_LEN];
    char name[APP_NAME_LEN];
    char open[64];      // comma-separated extensions claimed by the app
    u32  size;
    u16  wdate;
    u16  wtime;
    Bool is_papp;       // package list entry, not an installed folder
    char papp[16];      // /apps/<papp>
    char version[12];   // from app.ini
    char disp[APP_NAME_LEN + 12];  // name + optional " (update)"
} AppEntry;

typedef struct {
    u32  magic;
    u32  api_version;
    u32  size;
    u16  wdate;
    u16  wtime;
    u16  _pad;
    char id[APP_ID_LEN];
} AppRecord;

static AppEntry g_apps[APPS_MAX];
static int      g_app_count;
static char     g_path[96];
static char     g_text[APPS_BUF_LEN];

// Separate buffer for /apps/apps.cfg. AppReadManifest() reuses g_text, so
// parsing the installed list from the same buffer would clobber the pointer.
static char     g_cfg[APPS_BUF_LEN];

static Bool IdEq(const char* a, const char* b);

// ---------------------------------------------------------------------------
// Small string/path helpers (no stdio)
// ---------------------------------------------------------------------------

static void CopyStr(char* dst, const char* src, int max)
{
    int i = 0;
    if (max <= 0) return;
    for (; src[i] && i < max - 1; i++) dst[i] = src[i];
    dst[i] = 0;
}

static void JoinPath(char* out, int max, const char* dir, const char* name)
{
    int n = 0;
    while (dir[n] && n < max - 1) { out[n] = dir[n]; n++; }
    if (n > 0 && out[n - 1] != '/' && n < max - 1) out[n++] = '/';
    int i = 0;
    while (name[i] && n < max - 1) out[n++] = name[i++];
    out[n] = 0;
}

static void JoinPath3(char* out, int max, const char* a,
                      const char* b, const char* c)
{
    char tmp[72];
    JoinPath(tmp, sizeof(tmp), a, b);
    JoinPath(out, max, tmp, c);
}

// ---------------------------------------------------------------------------
// Text file helpers
// ---------------------------------------------------------------------------

// Read a small text file. Returns length, or -1.
static int ReadText(const char* path, char* buf, int max)
{
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return -1;
    int n = (int)FileRead(&f, buf, (u32)(max - 1));
    FileClose(&f);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

static Bool WriteText(const char* path, const char* buf, int len)
{
    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, path)) return False;
    Bool ok = (FileWrite(&f, buf, (u32)len) == (u32)len);
    FileClose(&f);
    return ok;
}

// Find key=value in a buffer. Returns True when found.
static Bool IniGet(const char* buf, const char* key, char* out, int max)
{
    int klen = (int)StrLen(key);
    const char* p = buf;

    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '#' || *p == ';')
        {
            while (*p && *p != '\n') p++;
            continue;
        }

        Bool match = True;
        for (int i = 0; i < klen; i++)
        {
            if (p[i] != key[i]) { match = False; break; }
        }
        if (match)
        {
            const char* q = p + klen;
            while (*q == ' ' || *q == '\t') q++;
            if (*q == '=')
            {
                q++;
                while (*q == ' ' || *q == '\t') q++;
                int n = 0;
                while (*q && *q != '\r' && *q != '\n' && n < max - 1)
                    out[n++] = *q++;
                while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
                out[n] = 0;
                return True;
            }
        }

        while (*p && *p != '\n') p++;
    }
    return False;
}

// ---------------------------------------------------------------------------
// Manifest / installed list
// ---------------------------------------------------------------------------

// Read /apps/<id>/app.ini. name defaults to the id, bin to "app.bin".
static Bool AppReadManifest(const char* id, char* name, char* bin,
                            char* openList, char* version,
                            int nameMax, int binMax, int openMax, int verMax)
{
    CopyStr(name, id, nameMax);
    CopyStr(bin, "app.bin", binMax);
    if (openList && openMax > 0) openList[0] = 0;
    if (version && verMax > 0) version[0] = 0;

    JoinPath3(g_path, sizeof(g_path), APPS_DIR, id, "app.ini");
    if (ReadText(g_path, g_text, APPS_BUF_LEN) < 0) return False;

    IniGet(g_text, "name", name, nameMax);
    IniGet(g_text, "bin", bin, binMax);
    if (openList && openMax > 0) IniGet(g_text, "open", openList, openMax);
    if (version && verMax > 0) IniGet(g_text, "version", version, verMax);
    if (!bin[0]) CopyStr(bin, "app.bin", binMax);
    return True;
}

static Bool AppsCfgHas(const char* id)
{
    if (ReadText(APPS_CFG, g_text, APPS_BUF_LEN) < 0) return False;

    const char* p = g_text;
    while (*p)
    {
        const char* line_start = p;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\r' || *p == '\n')
        {
            while (*p && *p != '\n') p++;
            if (p == line_start) break;
            continue;
        }
        int n = 0;
        while (p[n] && p[n] != '\r' && p[n] != '\n' && n < APP_ID_LEN - 1) n++;
        if (n > 0 && StrLen(id) == (u32)n)
        {
            char tmp[APP_ID_LEN];
            memcpy(tmp, p, (u32)n);
            tmp[n] = 0;
            if (IdEq(tmp, id)) return True;
        }
        while (*p && *p != '\n') p++;
    }
    return False;
}

static Bool AppsCfgAdd(const char* id)
{
    if (AppsCfgHas(id)) return True;

    int len = ReadText(APPS_CFG, g_text, APPS_BUF_LEN);
    if (len < 0) len = 0;

    // Keep the file bounded.
    if (len + (int)StrLen(id) + 2 >= APPS_BUF_LEN) return False;
    if (len > 0 && g_text[len - 1] != '\n') g_text[len++] = '\n';
    CopyStr(g_text + len, id, APPS_BUF_LEN - len);
    len += (int)StrLen(id);
    g_text[len++] = '\n';

    // Ensure /apps exists (the package is already there, but be defensive).
    DirCreate(APPS_DIR);
    return WriteText(APPS_CFG, g_text, len);
}

static Bool AppsCfgRemove(const char* id)
{
    int len = ReadText(APPS_CFG, g_text, APPS_BUF_LEN);
    if (len < 0) return False;

    char out[APPS_BUF_LEN];
    int o = 0;
    const char* p = g_text;
    while (*p && o < APPS_BUF_LEN - 1)
    {
        const char* line = p;
        int n = 0;
        while (p[n] && p[n] != '\n' && n < APP_ID_LEN - 1) n++;
        Bool match = False;
        if (n > 0 && StrLen(id) == (u32)n)
        {
            char tmp[APP_ID_LEN];
            memcpy(tmp, line, (u32)n);
            tmp[n] = 0;
            match = IdEq(tmp, id);
        }
        if (!match)
        {
            while (*p && *p != '\n' && o < APPS_BUF_LEN - 1) out[o++] = *p++;
            if (*p == '\n' && o < APPS_BUF_LEN - 1) out[o++] = *p++;
        }
        else
        {
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
        }
    }
    out[o] = 0;
    return WriteText(APPS_CFG, out, o);
}

// ---------------------------------------------------------------------------
// Install record (last sector of the app strip)
// ---------------------------------------------------------------------------

static Bool AppRecordGet(AppRecord* rec)
{
    const AppRecord* p = (const AppRecord*)APP_RECORD_ADDR;
    if (p->magic != APP_RECORD_MAGIC) return False;
    if (p->api_version != APP_API_VERSION) return False;
    *rec = *p;
    return True;
}

static Bool AppRecordMatches(const AppEntry* e, u32 size)
{
    AppRecord rec;
    if (!AppRecordGet(&rec)) return False;
    if (rec.size != size || rec.wdate != e->wdate || rec.wtime != e->wtime)
        return False;
    if (!IdEq(rec.id, e->id)) return False;
    return True;
}

static Bool AppRecordWrite(const AppEntry* e, u32 size)
{
    static AppRecord rec;
    memset(&rec, 0xFF, sizeof(rec));
    rec.magic = APP_RECORD_MAGIC;
    rec.api_version = APP_API_VERSION;
    rec.size = size;
    rec.wdate = e->wdate;
    rec.wtime = e->wtime;
    CopyStr(rec.id, e->id, sizeof(rec.id));

    u8 page[256];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &rec, sizeof(rec));

    u32 off = APP_RECORD_ADDR - 0x10000000u;
    FlashErase(off, 4096);
    FlashProgram(off, page, sizeof(page));

    AppRecord check;
    if (!AppRecordGet(&check)) return False;
    return (check.size == size && strcmp(check.id, e->id) == 0);
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

static void AppFrame(const char* title, const char* footer)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2(title,
              (WIDTH - (int)StrLen(title) * FONTW * 2) / 2,
              APPS_TITLE_Y, g_theme.menu_title);
    DrawRect(20, APPS_DIV_Y, WIDTH - 40, 1, g_theme.menu_dim);
    DrawRect(20, APPS_FOOT_Y - 8, WIDTH - 40, 1, g_theme.menu_dim);
    if (footer) DrawText(footer, 20, APPS_FOOT_Y, g_theme.menu_dim);
}

static void AppMessage(const char* l1, const char* l2)
{
    AppFrame("APPS", "X: back");
    DrawText(l1, 20, 96, g_theme.menu_text);
    if (l2) DrawText(l2, 20, 118, g_theme.menu_dim);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(40);
}

static void AppProgress(const char* title, u32 done, u32 total)
{
    AppFrame(title, NULL);

    int bx = 30, by = 110, bw = WIDTH - 60, bh = 18;
    DrawRect(bx, by, bw, bh, g_theme.menu_dim);
    if (total > 0)
    {
        int fill = (int)((u64)bw * done / total);
        if (fill > bw - 2) fill = bw - 2;
        if (fill > 0) DrawRect(bx + 1, by + 1, fill, bh - 2, g_theme.ok_color);
    }
    DispUpdate();
}

// Draw a simple list. Returns nothing; caller handles keys.
static void AppDrawList(const char* title, const char* footer,
                        const char** labels, int count, int selected,
                        const Bool* disabled)
{
    static const char* rows[APPS_VISIBLE];
    int first = 0;

    if (selected >= APPS_VISIBLE) first = selected - APPS_VISIBLE + 1;
    if (first > count - APPS_VISIBLE) first = count - APPS_VISIBLE;
    if (first < 0) first = 0;

    int shown = 0;
    for (int i = first; i < count && shown < APPS_VISIBLE; i++, shown++)
        rows[shown] = labels[i];

    AppFrame(title, footer);

    for (int i = 0; i < shown; i++)
    {
        int idx = first + i;
        int y = APPS_LIST_Y + i * APPS_LINE_H;
        Bool sel = (idx == selected);
        Bool dis = disabled && disabled[idx];

        u16 col = dis ? g_theme.menu_dim
                      : (sel ? g_theme.menu_sel : g_theme.menu_text);

        if (sel)
            DrawRect(24, y - 2, WIDTH - 48, FONTH + 4, g_theme.menu_hl);
        DrawText(sel ? ">" : " ", 28, y, sel ? g_theme.menu_sel : g_theme.menu_dim);
        DrawText(rows[i], 42, y, col);
    }

    if (count > APPS_VISIBLE)
    {
        // scroll hint
        if (first > 0) DrawText("^", WIDTH - 18, APPS_LIST_Y - 2, g_theme.menu_dim);
        if (first + APPS_VISIBLE < count)
            DrawText("v", WIDTH - 18, APPS_LIST_Y + (APPS_VISIBLE - 1) * APPS_LINE_H,
                     g_theme.menu_dim);
    }

    DispUpdate();
}

static int AppListKey(int* selected, int count)
{
    u8 k = KeyGet();
    if (k == NOKEY) { WaitMs(30); return -1; }
    if (k == KEY_UP && count > 0)
        *selected = (*selected - 1 + count) % count;
    else if (k == KEY_DOWN && count > 0)
        *selected = (*selected + 1) % count;
    else
        return k;
    return -2;
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

// Load the installed list from /apps/apps.cfg + each app.ini.
static int AppsLoadInstalled(void)
{
    g_app_count = 0;

    int len = ReadText(APPS_CFG, g_cfg, APPS_BUF_LEN);
    if (len <= 0) return 0;

    const char* p = g_cfg;
    while (*p && g_app_count < APPS_MAX)
    {
        const char* line_start = p;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '#' || *p == ';' || *p == '\r' || *p == '\n')
        {
            while (*p && *p != '\n') p++;
            if (p == line_start) break;
            continue;
        }

        char id[APP_ID_LEN];
        int n = 0;
        while (*p && *p != '\r' && *p != '\n' && n < APP_ID_LEN - 1)
            id[n++] = *p++;
        id[n] = 0;
        // Consume the line terminator. The old progress guard here broke out
        // before processing the first valid line (p was already at '\n').
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        if (!n) continue;

        // Folder and binary must exist.
        char bin[24];
        JoinPath3(g_path, sizeof(g_path), APPS_DIR, id, "app.ini");
        if (!FileExist(g_path)) continue;
        if (!AppReadManifest(id, g_apps[g_app_count].name, bin,
                             g_apps[g_app_count].open,
                             g_apps[g_app_count].version, APP_NAME_LEN,
                             sizeof(bin), sizeof(g_apps[g_app_count].open),
                             sizeof(g_apps[g_app_count].version)))
            continue;

        sFileInfo info;
        JoinPath3(g_path, sizeof(g_path), APPS_DIR, id, bin);
        if (!FileInfo(g_path, &info) || (info.attr & ATTR_DIR)) continue;

        CopyStr(g_apps[g_app_count].id, id, APP_ID_LEN);
        g_apps[g_app_count].size  = info.size;
        g_apps[g_app_count].wdate = info.wdate;
        g_apps[g_app_count].wtime = info.wtime;
        g_app_count++;
    }
    return g_app_count;
}

static void AppStripExt(const char* name, char* out, int max);

// List every /apps/<id> that has an app.ini (installed or not) plus .PAP
// single-file packages.
static int AppsScanPackages(void)
{
    g_app_count = 0;

    sFile find;
    sFileInfo info;
    FileInit(&find);
    if (!FindOpen(&find, APPS_DIR)) return 0;

    while (g_app_count < APPS_MAX &&
           FindNext(&find, &info, ATTR_DIR_MASK, "*"))
    {
        if (info.namelen == 0 || !(info.attr & ATTR_DIR)) continue;
        if (info.name[0] == '.') continue;

        char id[APP_ID_LEN];
        int n = info.namelen;
        if (n >= APP_ID_LEN) n = APP_ID_LEN - 1;
        memcpy(id, info.name, n);
        id[n] = 0;

        char bin[24];
        if (!AppReadManifest(id, g_apps[g_app_count].name, bin,
                             g_apps[g_app_count].open,
                             g_apps[g_app_count].version, APP_NAME_LEN,
                             sizeof(bin), sizeof(g_apps[g_app_count].open),
                             sizeof(g_apps[g_app_count].version)))
            continue;

        JoinPath3(g_path, sizeof(g_path), APPS_DIR, id, bin);
        sFileInfo fi;
        if (!FileInfo(g_path, &fi) || (fi.attr & ATTR_DIR)) continue;

        CopyStr(g_apps[g_app_count].id, id, APP_ID_LEN);
        g_apps[g_app_count].size  = fi.size;
        g_apps[g_app_count].wdate = fi.wdate;
        g_apps[g_app_count].wtime = fi.wtime;
        g_app_count++;
    }
    FindClose(&find);

    // Single-file PAPP packages (.pap, 8.3 extension).
    if (g_app_count < APPS_MAX)
    {
        FileInit(&find);
        if (FindOpen(&find, APPS_DIR))
        {
            while (g_app_count < APPS_MAX &&
                   FindNext(&find, &info, ATTR_ARCH, "*.PAP"))
            {
                if (info.attr & ATTR_DIR) continue;

                char nm[16];
                int n = info.namelen;
                if (n > 15) n = 15;
                memcpy(nm, info.name, n);
                nm[n] = 0;

                AppEntry* e = &g_apps[g_app_count];
                e->is_papp = True;
                CopyStr(e->papp, nm, sizeof(e->papp));
                AppStripExt(nm, e->id, sizeof(e->id));
                CopyStr(e->name, e->id, sizeof(e->name));
                e->open[0] = 0;
                e->version[0] = 0;
                e->size  = info.size;
                e->wdate = info.wdate;
                e->wtime = info.wtime;
                g_app_count++;
            }
            FindClose(&find);
        }
    }

    return g_app_count;
}

// ---------------------------------------------------------------------------
// Install from SD
// ---------------------------------------------------------------------------

static Bool AppCreateConfig(const char* id)
{
    JoinPath3(g_path, sizeof(g_path), APPS_DIR, id, "config.ini");
    if (FileExist(g_path)) return True;
    const char* hdr = "; PicoPad app config\n";
    return WriteText(g_path, hdr, (int)StrLen(hdr));
}

static void AppStripExt(const char* name, char* out, int max)
{
    const char* base = name;
    for (const char* p = name; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;

    int n = 0;
    while (base[n] && base[n] != '.' && n < max - 1)
    {
        out[n] = base[n];
        n++;
    }
    out[n] = 0;
}

// Extract a .pap (PAPP container) into /apps/<id>/ and register it.
static Bool AppInstallPapp(const char* pappName)
{
    if (!DiskValid())
    {
        SDInit();
        DiskAutoMount();
    }
    if (!DiskValid())
    {
        AppMessage("SD card is not mounted.", "Reinsert it and retry.");
        return False;
    }

    char path[96], id[APP_ID_LEN], dir[96];
    JoinPath(path, sizeof(path), APPS_DIR, pappName);
    AppStripExt(pappName, id, sizeof(id));
    if (!id[0]) return False;
    JoinPath(dir, sizeof(dir), APPS_DIR, id);

    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path))
    {
        AppMessage("Cannot open package.", pappName);
        return False;
    }

    u32 total = FileSize(&f);
    u8 hdr[12];
    Bool ok = (FileRead(&f, hdr, sizeof(hdr)) == sizeof(hdr));
    if (!ok || memcmp(hdr, "PAPP", 4) != 0)
    {
        FileClose(&f);
        AppMessage("Not a PAPP package.", pappName);
        return False;
    }

    u16 ver = (u16)(hdr[4] | (hdr[5] << 8));
    u16 count = (u16)(hdr[6] | (hdr[7] << 8));
    if (ver != 1 || count < 2 || count > 32)
    {
        FileClose(&f);
        AppMessage("Unsupported package.", "Bad header or file count.");
        return False;
    }

    DirCreate(dir);

    u8* buf = (u8*)FrameBuf;

    for (u16 i = 0; i < count && ok; i++)
    {
        u8 toc[40];
        if (!FileSeek(&f, 12u + (u32)i * 40u) ||
            FileRead(&f, toc, sizeof(toc)) != sizeof(toc))
        {
            ok = False;
            break;
        }

        char fn[33];
        memcpy(fn, toc, 32);
        fn[32] = 0;

        u32 off = (u32)toc[32] | ((u32)toc[33] << 8) |
                  ((u32)toc[34] << 16) | ((u32)toc[35] << 24);
        u32 sz  = (u32)toc[36] | ((u32)toc[37] << 8) |
                  ((u32)toc[38] << 16) | ((u32)toc[39] << 24);

        if (!fn[0] || off > total || sz > total - off)
        {
            ok = False;
            break;
        }
        if (strcmp(fn, "CONFIG.INI") == 0 || strcmp(fn, "DATA") == 0)
            continue;

        char outpath[128];
        JoinPath(outpath, sizeof(outpath), dir, fn);

        sFile o;
        FileInit(&o);
        if (!FileCreate(&o, outpath))
        {
            ok = False;
            break;
        }

        if (!FileSeek(&f, off))
        {
            FileClose(&o);
            ok = False;
            break;
        }

        u32 done = 0;
        while (done < sz)
        {
            u32 chunk = sz - done;
            if (chunk > 1024u) chunk = 1024u;

            AppProgress("Extracting", done, sz);
            if (FileRead(&f, buf, chunk) != chunk)
            {
                ok = False;
                break;
            }
            if (FileWrite(&o, buf, chunk) != chunk)
            {
                ok = False;
                break;
            }
            done += chunk;
        }
        FileClose(&o);
    }

    FileClose(&f);

    if (!ok)
    {
        AppMessage("Extract failed (SD write).", pappName);
        return False;
    }

    JoinPath3(g_path, sizeof(g_path), APPS_DIR, id, "app.ini");
    if (!FileExist(g_path))
    {
        AppMessage("Package has no app.ini.", id);
        return False;
    }
    JoinPath3(g_path, sizeof(g_path), APPS_DIR, id, "app.bin");
    if (!FileExist(g_path))
    {
        AppMessage("Package has no app.bin.", id);
        return False;
    }

    if (!AppsCfgAdd(id) || !AppCreateConfig(id))
    {
        DiskAutoMount();
        if (!AppsCfgAdd(id) || !AppCreateConfig(id))
        {
            AppMessage("Files installed, but apps.cfg write failed.", id);
            return False;
        }
    }
    return True;
}

static void AppsFromSdRun(void)
{
    int count = AppsScanPackages();
    if (count == 0)
    {
        AppMessage("No app packages found.",
                   "Put a folder in /apps with app.ini + app.bin");
        return;
    }

    static const char* labels[APPS_MAX];
    static Bool installed[APPS_MAX];
    for (int i = 0; i < count; i++)
    {
        labels[i] = g_apps[i].name;
        installed[i] = AppsCfgHas(g_apps[i].id);
    }

    int selected = 0;
    AppDrawList("FROM SD CARD", "A: install  X: back",
                labels, count, selected, installed);

    while (1)
    {
        int k = AppListKey(&selected, count);
        if (k == -1) continue;
        if (k == -2)
        {
            AppDrawList("FROM SD CARD", "A: install  X: back",
                        labels, count, selected, installed);
            continue;
        }
        if (k == KEY_X) return;

        if (k == KEY_A)
        {
            if (installed[selected])
            {
                AppMessage("Already installed.", g_apps[selected].name);
            }
            else if (g_apps[selected].is_papp)
            {
                if (AppInstallPapp(g_apps[selected].papp))
                {
                    installed[selected] = True;
                    AppMessage("Installed.", g_apps[selected].name);
                }
                else
                {
                    AppMessage("Install failed.", "Package or SD error.");
                }
            }
            else
            {
                if (AppsCfgAdd(g_apps[selected].id) &&
                    AppCreateConfig(g_apps[selected].id))
                {
                    installed[selected] = True;
                    AppMessage("Installed.", g_apps[selected].name);
                }
                else
                {
                    AppMessage("Install failed.", "SD card write error?");
                }
            }
            AppDrawList("FROM SD CARD", "A: install  X: back",
                        labels, count, selected, installed);
        }
    }
}

// ---------------------------------------------------------------------------
// App Store
// ---------------------------------------------------------------------------

#if LAUNCHER_HAS_COMPANION
// ---------------------------------------------------------------------------
// Online store through the companion (HTTPS on the PC side)
// ---------------------------------------------------------------------------

#define STORE_LINE_MAX 160

static StoreItem g_store[STORE_MAX_ITEMS];
static int g_store_count;

static void StoreSend(const char* s)
{
    while (*s) UsbDevCdcWriteChar(*s++);
    UsbDevCdcWriteChar('\r');
    UsbDevCdcWriteChar('\n');
}

static Bool StoreReadLine(char* out, int max, u32 timeout_ms)
{
    u32 start = Millis();
    int n = 0;

    while ((u32)(Millis() - start) < timeout_ms)
    {
        while (UsbDevCdcReadReady() > 0)
        {
            char c = (char)(u8)UsbDevCdcReadChar();
            if (c == '\r') continue;
            if (c == '\n') { out[n] = 0; return True; }
            if (n < max - 1) out[n++] = c;
        }
        WaitMs(2);
    }
    out[n] = 0;
    return False;
}

static int StoreParseInt(const char* s, const char** endp)
{
    int v = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); s++; }
    if (endp) *endp = s;
    return v;
}

static Bool StoreUsbFetchIndex(void)
{
    g_store_count = 0;
    StoreSend("STORE LIST");

    char line[STORE_LINE_MAX];
    u32 start = Millis();

    while ((u32)(Millis() - start) < 10000)
    {
        if (!StoreReadLine(line, sizeof(line), 300)) continue;

        if (!strncmp(line, "ENTRY ", 6))
        {
            if (g_store_count >= STORE_MAX_ITEMS) continue;

            char f[4][48];
            int fi = 0;
            const char* p = line + 6;

            while (fi < 4)
            {
                int n = 0;
                while (*p && *p != '|' && n < 47) f[fi][n++] = *p++;
                f[fi][n] = 0;
                fi++;
                if (*p == '|') { p++; continue; }
                break;
            }
            if (fi < 1 || !f[0][0]) continue;

            StoreItem* it = &g_store[g_store_count];
            CopyStr(it->id, f[0], sizeof(it->id));
            CopyStr(it->version, fi > 1 ? f[1] : "", sizeof(it->version));
            CopyStr(it->name, fi > 3 ? f[3] : f[0], sizeof(it->name));
            g_store_count++;
        }
        else if (!strcmp(line, "END"))
        {
            break;
        }
        else if (!strncmp(line, "ERR", 3))
        {
            break;
        }
    }
    return g_store_count > 0;
}

static int StoreB64(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int StoreDecode(const char* s, u8* out, int max)
{
    u32 v = 0;
    int bits = 0;
    int o = 0;

    for (; *s; s++)
    {
        if (*s == '=') break;
        int d = StoreB64(*s);
        if (d < 0) continue;
        v = (v << 6) | (u32)d;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            if (o < max) out[o++] = (u8)(v >> (u32)bits);
        }
    }
    return o;
}

// Ask the companion for one package, stream it to /apps/<NAME.PAP>.
static Bool StoreUsbDownload(const char* id, char* outName, int max)
{
    char cmd[32];
    strcpy(cmd, "STORE GET ");
    strcat(cmd, id);
    StoreSend(cmd);

    char line[STORE_LINE_MAX];
    u8 buf[128];
    sFile f;
    FileInit(&f);
    Bool open = False;
    Bool got = False;
    char name[16] = "";
    u32 total = 0;
    u32 gotBytes = 0;
    u32 start = Millis();

    while ((u32)(Millis() - start) < 25000)
    {
        if (!StoreReadLine(line, sizeof(line), 500)) continue;

        if (!strncmp(line, "FILE ", 5))
        {
            const char* p = line + 5;
            total = (u32)StoreParseInt(p, &p);
            while (*p == ' ') p++;
            CopyStr(name, p, sizeof(name));

            if (!open && name[0])
            {
                char path[96];
                JoinPath(path, sizeof(path), APPS_DIR, name);
                if (FileCreate(&f, path)) open = True;
            }
        }
        else if (!strncmp(line, "DATA ", 5))
        {
            int n = StoreDecode(line + 5, buf, sizeof(buf));
            if (open && n > 0)
            {
                if (FileWrite(&f, buf, (u32)n) != (u32)n) break;
                gotBytes += (u32)n;
                got = True;
            }
        }
        else if (!strcmp(line, "END"))
        {
            break;
        }
        else if (!strncmp(line, "ERR", 3))
        {
            break;
        }
    }

    if (open) FileClose(&f);

    if (got && outName) CopyStr(outName, name, max);
    return got && (total == 0 || gotBytes == total);
}

static Bool StoreHttpFetchIndex(void)
{
    g_store_count = StoreFetchIndex("http://pico.msuii.dev/assets/apps/index.txt",
                                    g_store, STORE_MAX_ITEMS);
    return g_store_count > 0;
}

static void StoreAppProgress(u32 done, u32 total)
{
    AppProgress("Downloading", done, total);
}

// Download a .pap in 3 KB chunks and install it.
static Bool StoreHttpDownload(const StoreItem* it)
{
    if (!it) return False;

    char papp[16];
    int n = 0;
    for (; it->id[n] && n < 8; n++) papp[n] = it->id[n];
    papp[n++] = '.';
    papp[n++] = 'P';
    papp[n++] = 'A';
    papp[n++] = 'P';
    papp[n] = 0;

    char path[96];
    JoinPath(path, sizeof(path), APPS_DIR, papp);

    if (!StoreDownloadChunks("http://pico.msuii.dev/assets/apps/", it, path,
                             StoreAppProgress))
        return False;

    return AppInstallPapp(papp);
}

static void AppsOnlineRun(void)
{
    if (!NativeWifiLinkUp())
    {
        AppMessage("WiFi is not connected.",
                   "Use SETTINGS > WIFI to connect first.");
        return;
    }

    AppFrame("STORE ONLINE", "X: back");
    DrawText("Fetching catalogue...", 20, 100, g_theme.menu_text);
    DispUpdate();

    if (!StoreHttpFetchIndex())
    {
        AppMessage("Store unavailable.", "http://pico.msuii.dev/assets/apps/");
        return;
    }

    static const char* labels[STORE_MAX_ITEMS];
    for (int i = 0; i < g_store_count; i++) labels[i] = g_store[i].name;

    int selected = 0;
    AppDrawList("STORE ONLINE", "A: install  X: back",
                labels, g_store_count, selected, NULL);

    while (1)
    {
        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(30); continue; }

        if (k == KEY_UP)
        {
            selected = (selected - 1 + g_store_count) % g_store_count;
            AppDrawList("STORE ONLINE", "A: install  X: back",
                        labels, g_store_count, selected, NULL);
        }
        else if (k == KEY_DOWN)
        {
            selected = (selected + 1) % g_store_count;
            AppDrawList("STORE ONLINE", "A: install  X: back",
                        labels, g_store_count, selected, NULL);
        }
        else if (k == KEY_X)
        {
            break;
        }
        else if (k == KEY_A)
        {
            AppFrame("STORE ONLINE", "Downloading...");
            DrawText(g_store[selected].name, 20, 100, g_theme.menu_text);
            DispUpdate();

            if (StoreHttpDownload(&g_store[selected]))
                AppMessage("Installed.", g_store[selected].name);
            else
                AppMessage("Download failed.", "Check WiFi and try again.");

            AppDrawList("STORE ONLINE", "A: install  X: back",
                        labels, g_store_count, selected, NULL);
        }
    }
}

#endif // LAUNCHER_HAS_COMPANION

static void AppsStoreRun(void)
{
    static const char* labels[2] = {
        "From SD card",
        "From internet",
    };
    Bool disabled[2] = { False, !LAUNCHER_HAS_COMPANION };

    int selected = 0;
    AppDrawList("APP STORE", "A: open  X: back",
                labels, 2, selected, disabled);

    while (1)
    {
        int k = AppListKey(&selected, 2);
        if (k == -1) continue;
        if (k == -2)
        {
            AppDrawList("APP STORE", "A: open  X: back",
                        labels, 2, selected, disabled);
            continue;
        }
        if (k == KEY_X) return;
        if (k == KEY_A)
        {
            if (selected == 0)
            {
                AppsFromSdRun();
            }
#if LAUNCHER_HAS_COMPANION
            else
            {
                AppsOnlineRun();
            }
#endif
            AppDrawList("APP STORE", "A: open  X: back",
                        labels, 2, selected, disabled);
        }
    }
}

// ---------------------------------------------------------------------------
// Launch
// ---------------------------------------------------------------------------

static Bool AppProgramFromSd(const char* path, u32 size)
{
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return False;

    const u32 base_off = APP_SLOT_BASE - 0x10000000u;
    const u32 erase = (size + 4095u) & ~4095u;

    NativeWifiPause();

    for (u32 off = 0; off < erase; off += 4096u)
    {
        AppProgress("Installing app", off, erase);
        FlashErase(base_off + off, 4096u);
    }

    u8* buf = (u8*)FrameBuf;
    u32 done = 0;
    while (done < size)
    {
        u32 chunk = size - done;
        if (chunk > 4096u) chunk = 4096u;

        AppProgress("Installing app", done, size);
        if (FileRead(&f, buf, chunk) != chunk)
        {
            FileClose(&f);
            NativeWifiResume();
            return False;
        }

        u32 prog = (chunk + 255u) & ~255u;
        if (prog > chunk) memset(buf + chunk, 0xFF, prog - chunk);
        FlashProgram(base_off + done, buf, prog);
        done += chunk;
    }

    // Verify against the SD file (fresh XIP reads after the cache flush).
    FileSeek(&f, 0);
    done = 0;
    while (done < size)
    {
        u32 chunk = size - done;
        if (chunk > 4096u) chunk = 4096u;
        if (FileRead(&f, buf, chunk) != chunk)
        {
            FileClose(&f);
            NativeWifiResume();
            return False;
        }
        if (memcmp((const void*)(APP_SLOT_BASE + done), buf, chunk) != 0)
        {
            FileClose(&f);
            NativeWifiResume();
            return False;
        }
        done += chunk;
    }

    FileClose(&f);
    AppProgress("Installing app", size, size);
    // Core 1 stays paused: the caller writes the install record next and
    // resumes right before the jump.
    return True;
}

// Ensure the app binary is in the flash slot. force = rewrite even when the
// install record says it is already there.
static Bool AppEnsureInstalled(const AppEntry* e, Bool force)
{
    char bin[24];
    char name[APP_NAME_LEN];
    if (!AppReadManifest(e->id, name, bin, NULL, NULL,
                         sizeof(name), sizeof(bin), 0, 0))
    {
        AppMessage("app.ini missing or unreadable.", e->id);
        return False;
    }

    JoinPath3(g_path, sizeof(g_path), APPS_DIR, e->id, bin);

    sFileInfo info;
    if (!FileInfo(g_path, &info) || (info.attr & ATTR_DIR))
    {
        AppMessage("App binary not found.", g_path);
        return False;
    }

    if (info.size < 8 || info.size > APP_SLOT_SIZE)
    {
        AppMessage("App is empty or too big.", "Limit is 252 KB.");
        return False;
    }

    // Validate the vector table before writing anything.
    {
        sFile f;
        FileInit(&f);
        if (!FileOpen(&f, g_path)) { AppMessage("Cannot open app.", NULL); return False; }
        u32 vt[2];
        Bool ok = (FileRead(&f, vt, sizeof(vt)) == sizeof(vt));
        FileClose(&f);
        if (!ok) { AppMessage("Cannot read app.", NULL); return False; }
        if (vt[0] < 0x20000000u || vt[0] > 0x20042000u || (vt[0] & 3u) ||
            vt[1] < APP_SLOT_BASE || vt[1] >= APP_SLOT_END ||
            (vt[1] & 1u) == 0)
        {
            AppMessage("Not a valid PicoPad app.", "Rebuild with apps_sdk.");
            return False;
        }
    }

    // Remember the SD timestamp so an unchanged app is not rewritten.
    AppEntry cur = *e;
    cur.size  = info.size;
    cur.wdate = info.wdate;
    cur.wtime = info.wtime;

    if (!force && AppRecordMatches(&cur, info.size)) return True;

    if (!AppProgramFromSd(g_path, info.size))
    {
        NativeWifiResume();
        AppMessage("Install failed.", "Flash write or verify error.");
        return False;
    }
    if (!AppRecordWrite(&cur, info.size))
    {
        NativeWifiResume();
        AppMessage("Install record failed.", "App may still run.");
        return False;
    }
    NativeWifiResume();
    return True;
}

// Save the launcher's RAM to a reserved flash area so an app can return to
// the menu without a chip reset. Only 0x20000000..0x20037800 is overwritten by
// apps; the info block and launcher heap above it survive.
static Bool LauncherSnapshotSave(void)
{
    if (NativeWifiPresent() && !NativeWifiPause()) return False;

    const u32 base_off = LAUNCHER_SNAPSHOT_ADDR - 0x10000000u;
    const u8* src = (const u8*)0x20000000u;

    for (u32 off = 0; off < LAUNCHER_SNAPSHOT_SIZE; off += 4096u)
        FlashErase(base_off + off, 4096u);

    for (u32 off = 0; off < LAUNCHER_SNAPSHOT_SIZE; off += 4096u)
    {
        FlashProgram(base_off + off, src + off, 4096u);
        AppProgress("Preparing app", off, LAUNCHER_SNAPSHOT_SIZE);
    }

    NativeWifiResume();

    if (memcmp((const void*)LAUNCHER_SNAPSHOT_ADDR, src, 64) != 0)
    {
        WATCHDOG_SCRATCH[APP_SCRATCH_SNAPSHOT] = 0;
        return False;
    }

    WATCHDOG_SCRATCH[APP_SCRATCH_SNAPSHOT] = APP_SNAPSHOT_VALID;
    return True;
}

static void AppLaunchArgs(const AppEntry* e, const char* arg)
{
    if (!AppEnsureInstalled(e, False)) return;

    // Snapshot the launcher so AppExit() can return here without resetting.
    LauncherSnapshotSave();

    // Handoff block at APP_INFO_ADDR.
    {
        app_info_t* ai = (app_info_t*)APP_INFO_ADDR;
        memset(ai, 0, sizeof(*ai));
        ai->magic = APP_INFO_MAGIC;
        ai->api_version = APP_API_VERSION;
        CopyStr(ai->id, e->id, sizeof(ai->id));
        JoinPath3(ai->config_path, sizeof(ai->config_path),
                  APPS_DIR, e->id, "config.ini");
        JoinPath3(ai->data_path, sizeof(ai->data_path),
                  APPS_DIR, e->id, "data");

        ai->ext_magic = APP_INFO_EXT_MAGIC;
        if (arg) CopyStr(ai->arg_path, arg, sizeof(ai->arg_path));

        ai->theme[APP_TH_BG]     = g_theme.menu_bg;
        ai->theme[APP_TH_PANEL]  = g_theme.menu_bg;
        ai->theme[APP_TH_TITLE]  = g_theme.menu_title;
        ai->theme[APP_TH_TEXT]   = g_theme.menu_text;
        ai->theme[APP_TH_DIM]    = g_theme.menu_dim;
        ai->theme[APP_TH_ACCENT] = g_theme.accent_color;
        ai->theme[APP_TH_OK]     = g_theme.ok_color;
        ai->theme[APP_TH_WARN]   = g_theme.warn_color;
        ai->theme[APP_TH_ERR]    = g_theme.err_color;
        ai->theme[APP_TH_SEL]    = g_theme.menu_sel;
        ai->theme[APP_TH_HL]     = g_theme.menu_hl;

        const char* tn = ThemeActiveName();
        if (!tn || !tn[0]) tn = "default";
        CopyStr(ai->theme_name, tn, sizeof(ai->theme_name));
    }

    RunAppImage(APP_SLOT_BASE);
    AppMessage("App did not start.", "Invalid vector table?");
}

static void AppLaunch(const AppEntry* e)
{
    AppLaunchArgs(e, NULL);
}

// ---------------------------------------------------------------------------
// App manager: reinstall, config editor, remove
// ---------------------------------------------------------------------------

#define CFG_MAX_ITEMS  8
#define CFG_KEY_LEN    24
#define CFG_VAL_LEN    48
#define CFG_ROW_LEN    80

typedef struct {
    char key[CFG_KEY_LEN];
    char val[CFG_VAL_LEN];
} CfgItem;

static CfgItem g_cfg_items[CFG_MAX_ITEMS];
static int     g_cfg_items_count;
static char    g_cfg_rows[CFG_MAX_ITEMS][CFG_ROW_LEN];

static void TrimRight(char* s)
{
    int n = (int)StrLen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = 0;
}

static void AppConfigPath(const char* id, char* out, int max)
{
    JoinPath3(out, max, APPS_DIR, id, "config.ini");
}

static int AppConfigLoad(const char* id)
{
    g_cfg_items_count = 0;

    AppConfigPath(id, g_path, sizeof(g_path));
    if (ReadText(g_path, g_text, APPS_BUF_LEN) < 0) return 0;

    const char* p = g_text;
    while (*p && g_cfg_items_count < CFG_MAX_ITEMS)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '#' || *p == ';')
        {
            while (*p && *p != '\n') p++;
            continue;
        }

        CfgItem* it = &g_cfg_items[g_cfg_items_count];

        int k = 0;
        while (*p && *p != '=' && *p != '\r' && *p != '\n' &&
               k < CFG_KEY_LEN - 1)
            it->key[k++] = *p++;
        it->key[k] = 0;
        TrimRight(it->key);

        if (*p == '=') p++;
        while (*p == ' ' || *p == '\t') p++;

        int v = 0;
        while (*p && *p != '\r' && *p != '\n' && v < CFG_VAL_LEN - 1)
            it->val[v++] = *p++;
        it->val[v] = 0;
        TrimRight(it->val);

        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;

        if (k > 0) g_cfg_items_count++;
    }
    return g_cfg_items_count;
}

static Bool AppConfigSave(const char* id)
{
    AppConfigPath(id, g_path, sizeof(g_path));

    int o = 0;
    const char* hdr = "; PicoPad app config\n";
    for (int i = 0; hdr[i] && o < APPS_BUF_LEN - 1; i++) g_text[o++] = hdr[i];

    for (int i = 0; i < g_cfg_items_count; i++)
    {
        const char* k = g_cfg_items[i].key;
        const char* v = g_cfg_items[i].val;
        for (int j = 0; k[j] && o < APPS_BUF_LEN - 1; j++) g_text[o++] = k[j];
        if (o < APPS_BUF_LEN - 1) g_text[o++] = '=';
        for (int j = 0; v[j] && o < APPS_BUF_LEN - 1; j++) g_text[o++] = v[j];
        if (o < APPS_BUF_LEN - 1) g_text[o++] = '\n';
    }
    return WriteText(g_path, g_text, o);
}

static Bool AppConfirm(const char* l1, const char* l2)
{
    AppFrame("APPS", "A: yes   X: no");
    DrawText(l1, 20, 96, g_theme.menu_text);
    if (l2) DrawText(l2, 20, 118, g_theme.menu_dim);
    DispUpdate();

    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_A) return True;
        if (k == KEY_X) return False;
        WaitMs(30);
    }
}

static void AppConfigRun(const AppEntry* e)
{
    g_cfg_items_count = AppConfigLoad(e->id);
    int selected = 0;

    while (1)
    {
        for (int i = 0; i < g_cfg_items_count; i++)
        {
            char* r = g_cfg_rows[i];
            int o = 0;
            const char* k = g_cfg_items[i].key;
            const char* v = g_cfg_items[i].val;
            for (int j = 0; k[j] && o < CFG_ROW_LEN - 4; j++) r[o++] = k[j];
            r[o++] = ' ';
            r[o++] = '=';
            r[o++] = ' ';
            for (int j = 0; v[j] && o < CFG_ROW_LEN - 1; j++) r[o++] = v[j];
            r[o] = 0;
        }

        static const char* labels[CFG_MAX_ITEMS + 1];
        for (int i = 0; i < g_cfg_items_count; i++) labels[i] = g_cfg_rows[i];
        labels[g_cfg_items_count] = "Add setting";

        int total = g_cfg_items_count + 1;
        if (selected >= total) selected = total - 1;

        AppDrawList("APP CONFIG", "A: edit  Y: delete  X: save",
                    labels, total, selected, NULL);

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(30); continue; }

        if (k == KEY_UP)
        {
            selected = (selected - 1 + total) % total;
            continue;
        }
        if (k == KEY_DOWN)
        {
            selected = (selected + 1) % total;
            continue;
        }
        if (k == KEY_X)
        {
            AppConfigSave(e->id);
            return;
        }

        if (k == KEY_A)
        {
            char val[CFG_VAL_LEN];
            if (selected < g_cfg_items_count)
            {
                CopyStr(val, g_cfg_items[selected].val, sizeof(val));
                if (KeyboardInput("value", val, sizeof(val)))
                {
                    CopyStr(g_cfg_items[selected].val, val, CFG_VAL_LEN);
                    AppConfigSave(e->id);
                }
            }
            else if (g_cfg_items_count < CFG_MAX_ITEMS)
            {
                char key[CFG_KEY_LEN];
                key[0] = 0;
                if (KeyboardInput("key", key, sizeof(key)) && key[0])
                {
                    val[0] = 0;
                    KeyboardInput("value", val, sizeof(val));
                    CopyStr(g_cfg_items[g_cfg_items_count].key, key, CFG_KEY_LEN);
                    CopyStr(g_cfg_items[g_cfg_items_count].val, val, CFG_VAL_LEN);
                    g_cfg_items_count++;
                    AppConfigSave(e->id);
                    selected = g_cfg_items_count - 1;
                }
            }
            continue;
        }

        if (k == KEY_Y)
        {
            if (selected < g_cfg_items_count)
            {
                for (int i = selected; i < g_cfg_items_count - 1; i++)
                    g_cfg_items[i] = g_cfg_items[i + 1];
                g_cfg_items_count--;
                if (selected >= g_cfg_items_count && selected > 0) selected--;
                AppConfigSave(e->id);
            }
            continue;
        }
    }
}

static Bool ExtEq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') a = (char)(a + 'a' - 'A');
    if (b >= 'A' && b <= 'Z') b = (char)(b + 'a' - 'A');
    return a == b;
}

static Bool AppOpenMatch(const char* list, const char* ext)
{
    int elen = (int)StrLen(ext);
    if (elen <= 0) return False;

    const char* p = list;
    while (*p)
    {
        while (*p == ',' || *p == ' ') p++;
        const char* start = p;
        while (*p && *p != ',') p++;
        const char* end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
        if (end > start && start[0] == '.') start++;

        int n = (int)(end - start);
        if (n == elen)
        {
            Bool same = True;
            for (int i = 0; i < n; i++)
                if (!ExtEq(start[i], ext[i])) { same = False; break; }
            if (same) return True;
        }
    }
    return False;
}

static Bool AppsDeleteDirContents(const char* path, int depth)
{
    // Delete one entry at a time and reopen the folder, so the stack stays
    // small (the core-0 stack is only 4 KB).
    if (depth > 4) return False;

    for (int guard = 0; guard < 200; guard++)
    {
        sFile find;
        sFileInfo info;
        char name[24];
        Bool isdir = False;
        Bool got = False;

        FileInit(&find);
        if (!FindOpen(&find, path)) break;

        while (FindNext(&find, &info, ATTR_DIR_MASK, "*"))
        {
            if (info.namelen == 0 || info.name[0] == '.') continue;
            int l = info.namelen;
            if (l > 23) l = 23;
            memcpy(name, info.name, l);
            name[l] = 0;
            isdir = (info.attr & ATTR_DIR) != 0;
            got = True;
            break;
        }
        FindClose(&find);

        if (!got) break;

        char full[128];
        JoinPath(full, sizeof(full), path, name);
        if (isdir) AppsDeleteDirContents(full, depth + 1);
        FileDelete(full);
    }
    return True;
}

static void AppClearData(const AppEntry* e)
{
    AppConfigPath(e->id, g_path, sizeof(g_path));
    FileDelete(g_path);

    JoinPath3(g_path, sizeof(g_path), APPS_DIR, e->id, "data");
    AppsDeleteDirContents(g_path, 0);

    AppMessage("Data cleared.", e->name);
}

// File manager hook: returns True when an app handled the file (or the user
// cancelled the chooser). False means "no app claims it, use the default".
Bool AppOpenWithRun(const char* path)
{
    const char* base = path;
    for (const char* p = path; *p; p++) if (*p == '/') base = p + 1;

    const char* dot = NULL;
    for (const char* p = base; *p; p++) if (*p == '.') dot = p;
    if (!dot || !dot[1]) return False;
    const char* ext = dot + 1;

    int count = AppsLoadInstalled();
    static const char* labels[APPS_MAX + 1];
    AppEntry* map[APPS_MAX];
    int matches = 0;

    labels[0] = "Text editor";
    for (int i = 0; i < count; i++)
    {
        if (!AppOpenMatch(g_apps[i].open, ext)) continue;
        map[matches] = &g_apps[i];
        labels[matches + 1] = g_apps[i].name;
        matches++;
    }
    if (matches == 0) return False;

    int total = matches + 1;
    int selected = 0;

    AppDrawList("OPEN WITH", "A: open  X: cancel",
                labels, total, selected, NULL);

    while (1)
    {
        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(30); continue; }

        if (k == KEY_UP)
        {
            selected = (selected - 1 + total) % total;
            AppDrawList("OPEN WITH", "A: open  X: cancel",
                        labels, total, selected, NULL);
        }
        else if (k == KEY_DOWN)
        {
            selected = (selected + 1) % total;
            AppDrawList("OPEN WITH", "A: open  X: cancel",
                        labels, total, selected, NULL);
        }
        else if (k == KEY_X)
        {
            return True;
        }
        else if (k == KEY_A)
        {
            if (selected == 0) return False;
            char full[96];
            CopyStr(full, path, sizeof(full));
            AppLaunchArgs(map[selected - 1], full);
            return True;
        }
    }
}

static void AppManagerRun(const AppEntry* e)
{
    static const char* labels[] = {
        "Launch",
        "Reinstall",
        "Edit config",
        "Clear data",
        "Remove from My Apps",
    };
    int selected = 0;
    const int total = 5;

    while (1)
    {
        AppDrawList("MANAGE APP", "A: select  X: back",
                    labels, total, selected, NULL);

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(30); continue; }

        if (k == KEY_UP) { selected = (selected + total - 1) % total; continue; }
        if (k == KEY_DOWN) { selected = (selected + 1) % total; continue; }
        if (k == KEY_X) return;

        if (k != KEY_A) continue;

        if (selected == 0)
        {
            AppLaunch(e);   // returns only on failure
        }
        else if (selected == 1)
        {
            if (AppEnsureInstalled(e, True))
                AppMessage("Reinstalled.", e->name);
        }
        else if (selected == 2)
        {
            AppConfigRun(e);
        }
        else if (selected == 3)
        {
            if (AppConfirm("Clear config and data?", e->name))
                AppClearData(e);
        }
        else if (selected == 4)
        {
            if (AppConfirm("Remove from My Apps?", e->name))
            {
                AppsCfgRemove(e->id);
                AppMessage("Removed.", "Files stay on the SD card.");
                return;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Update index (/apps/index.txt)
// ---------------------------------------------------------------------------

typedef struct {
    char id[APP_ID_LEN];
    char version[12];
    char url[64];
    char name[APP_NAME_LEN];
} IndexEntry;

static IndexEntry g_index[APPS_MAX];
static int g_index_count;

static Bool IdEq(const char* a, const char* b)
{
    while (*a && *b)
    {
        char x = *a++;
        char y = *b++;
        if (x >= 'A' && x <= 'Z') x = (char)(x + 'a' - 'A');
        if (y >= 'A' && y <= 'Z') y = (char)(y + 'a' - 'A');
        if (x != y) return False;
    }
    return *a == *b;
}

static void ParseVer(const char* s, int v[3])
{
    v[0] = v[1] = v[2] = 0;
    int i = 0;
    while (*s && i < 3)
    {
        while (*s >= '0' && *s <= '9') { v[i] = v[i] * 10 + (*s - '0'); s++; }
        if (*s == '.') s++;
        i++;
    }
}

// Format: id|version|size|url|name   (# comments allowed)
static int AppsIndexLoad(void)
{
    g_index_count = 0;

    JoinPath(g_path, sizeof(g_path), APPS_DIR, "index.txt");
    if (ReadText(g_path, g_text, APPS_BUF_LEN) < 0) return 0;

    const char* p = g_text;
    while (*p && g_index_count < APPS_MAX)
    {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (*p == '#' || *p == ';')
        {
            while (*p && *p != '\n') p++;
            continue;
        }

        char f[5][64];
        int fi = 0;
        while (fi < 5 && *p && *p != '\r' && *p != '\n')
        {
            int n = 0;
            while (*p && *p != '|' && *p != '\r' && *p != '\n' && n < 63)
                f[fi][n++] = *p++;
            f[fi][n] = 0;
            fi++;
            if (*p == '|') { p++; continue; }
            break;
        }
        while (*p && *p != '\n') p++;

        if (fi < 2) continue;

        IndexEntry* e = &g_index[g_index_count];
        CopyStr(e->id, f[0], sizeof(e->id));
        CopyStr(e->version, f[1], sizeof(e->version));
        CopyStr(e->url, fi > 3 ? f[3] : "", sizeof(e->url));
        CopyStr(e->name, fi > 4 ? f[4] : f[0], sizeof(e->name));
        if (e->id[0]) g_index_count++;
    }
    return g_index_count;
}

static Bool AppsHasUpdate(const AppEntry* e)
{
    for (int i = 0; i < g_index_count; i++)
    {
        if (!IdEq(g_index[i].id, e->id)) continue;

        int a[3], b[3];
        ParseVer(g_index[i].version, a);
        ParseVer(e->version, b);

        for (int j = 0; j < 3; j++)
        {
            if (a[j] > b[j]) return True;
            if (a[j] < b[j]) return False;
        }
        return False;
    }
    return False;
}

static int AppsPrepareLabels(const char** labels)
{
    int count = AppsLoadInstalled();
    AppsIndexLoad();

    labels[0] = "App Store";
    for (int i = 0; i < count; i++)
    {
        CopyStr(g_apps[i].disp, g_apps[i].name, sizeof(g_apps[i].disp));
        if (AppsHasUpdate(&g_apps[i]))
            strcat(g_apps[i].disp, " (update)");
        labels[i + 1] = g_apps[i].disp;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Top-level APPS menu
// ---------------------------------------------------------------------------

void AppsMenuRun(void)
{
#if !LAUNCHER_HAS_APPS
    AppMessage("Apps need the 1 MB layout.", NULL);
    return;
#else
    if (!DiskValid())
    {
        AppMessage("No SD card.", "Insert the card and try again.");
        return;
    }

    // "App Store" is row 0, installed apps follow.
    static const char* labels[APPS_MAX + 1];
    int count = AppsPrepareLabels(labels);

    int total = count + 1;
    int selected = 0;   // App Store first

    AppDrawList("APPS", "A: open  Y: manage  X: back",
                labels, total, selected, NULL);

    while (1)
    {
        int k = AppListKey(&selected, total);
        if (k == -1) continue;
        if (k == -2)
        {
            AppDrawList("APPS", "A: open  Y: manage  X: back",
                        labels, total, selected, NULL);
            continue;
        }
        if (k == KEY_X) return;

        if (k == KEY_Y && selected > 0)
        {
            AppManagerRun(&g_apps[selected - 1]);
            count = AppsPrepareLabels(labels);
            total = count + 1;
            if (selected > count) selected = count;
            AppDrawList("APPS", "A: open  Y: manage  X: back",
                        labels, total, selected, NULL);
            continue;
        }

        if (k == KEY_A)
        {
            if (selected == 0)
            {
                AppsStoreRun();
            }
            else
            {
                AppLaunch(&g_apps[selected - 1]);
            }
            // Reload in case an install changed anything.
            count = AppsPrepareLabels(labels);
            total = count + 1;
            if (selected > count) selected = count;
            AppDrawList("APPS", "A: open  Y: manage  X: back",
                        labels, total, selected, NULL);
        }
    }
#endif
}
