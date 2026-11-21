// ****************************************************************************
//
//                  PicoDeck — Themes app
//
//   Lists "Default" plus every theme file in /themes and sets the active one.
//   "Install from internet" pulls a theme from
//   http://pico.msuii.dev/assets/themes/ and sets it.
//
//   The launcher always uses the default unless a theme is picked here.
//
// ****************************************************************************

#include "../include.h"
#include "themes_app.h"
#include "theme.h"
#include "native_wifi.h"
#include "store_net.h"

#define TH_VISIBLE  9
#define TH_STORE_BASE "http://pico.msuii.dev/assets/themes/"
#define TH_STORE_INDEX TH_STORE_BASE "index.txt"
#define TH_MAX_BYTES (64u * 1024u)

static void ThMessageEx(const char* title, const char* l1, const char* l2)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 30,
              g_theme.menu_title);
    if (l1) DrawText(l1, 20, 100, g_theme.menu_text);
    if (l2) DrawText(l2, 20, 120, g_theme.menu_dim);
    DrawText("Press X to return", 20, 210, g_theme.menu_dim);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

static void ThMessage(const char* l1, const char* l2)
{
    ThMessageEx("Themes", l1, l2);
}

static void ThProgress(u32 done, u32 total)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2("Themes", (WIDTH - 6 * FONTW * 2) / 2, 12, g_theme.menu_title);
    DrawText("Downloading theme...", 20, 74, g_theme.menu_text);

    int bx = 20, by = 110, bw = WIDTH - 40, bh = 16;
    DrawRect(bx, by, bw, bh, g_theme.menu_dim);
    if (total > 0)
    {
        int fill = (int)((u64)bw * done / total);
        if (fill > bw - 2) fill = bw - 2;
        if (fill > 0) DrawRect(bx + 1, by + 1, fill, bh - 2, g_theme.ok_color);
    }
    DispUpdate();
}

static void ThRender(const char* const* labels, int count, int selected,
                     int top, const char* active)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2("Themes", (WIDTH - 6 * FONTW * 2) / 2, 12, g_theme.menu_title);

    char cur[48];
    strcpy(cur, "Active: ");
    strcat(cur, (active && active[0]) ? active : "Default");
    DrawText(cur, 10, 40, g_theme.menu_dim);
    DrawRect(10, 56, WIDTH - 20, 1, g_theme.menu_dim);

    for (int i = 0; i < TH_VISIBLE; i++)
    {
        int idx = top + i;
        if (idx >= count) break;

        int y = 66 + i * 16;
        Bool sel = (idx == selected);
        if (sel)
            DrawRect(8, y - 2, WIDTH - 16, FONTH + 4, g_theme.menu_hl);

        // Row 0 is the store entry, row 1 is "Default", 2.. are files.
        Bool is_active = False;
        if (idx == 1) is_active = (!active || !active[0]);
        else if (idx >= 2 && active && active[0])
            is_active = (StrComp(labels[idx], active) == 0);

        DrawText(sel ? ">" : " ", 12, y,
                 sel ? g_theme.menu_sel : g_theme.menu_text);
        DrawText(labels[idx], 28, y,
                 sel ? g_theme.menu_sel : g_theme.menu_text);
        if (is_active) DrawText("*", WIDTH - 20, y, g_theme.ok_color);
    }

    DrawText("A: select   X: back", 10, 224, g_theme.menu_dim);
    DispUpdate();
}

// Online theme list. Returns the slug that was installed, or "".
static void ThOnlineRun(char* installed)
{
    installed[0] = 0;

    if (!NativeWifiLinkUp())
    {
        ThMessage("WiFi is not connected.",
                  "Use SETTINGS > WIFI to connect first.");
        return;
    }

    DrawClearCol(g_theme.menu_bg);
    DrawText2("THEME STORE", (WIDTH - 11 * FONTW * 2) / 2, 12, g_theme.menu_title);
    DrawText("Fetching catalogue...", 20, 100, g_theme.menu_text);
    DispUpdate();

    static StoreItem items[STORE_MAX_ITEMS];
    int count = StoreFetchIndex(TH_STORE_INDEX, items, STORE_MAX_ITEMS);
    if (count <= 0)
    {
        ThMessage("Theme store unavailable.", "Check WiFi and try again.");
        return;
    }

    static const char* labels[STORE_MAX_ITEMS];
    for (int i = 0; i < count; i++) labels[i] = items[i].name;

    int selected = 0, top = 0;
    Bool redraw = True;

    while (1)
    {
        if (selected < top) top = selected;
        if (selected >= top + TH_VISIBLE) top = selected - TH_VISIBLE + 1;

        if (redraw)
        {
            DrawClearCol(g_theme.menu_bg);
            DrawText2("THEME STORE", (WIDTH - 11 * FONTW * 2) / 2, 12,
                      g_theme.menu_title);
            DrawRect(10, 40, WIDTH - 20, 1, g_theme.menu_dim);
            for (int i = 0; i < TH_VISIBLE; i++)
            {
                int idx = top + i;
                if (idx >= count) break;
                int y = 52 + i * 16;
                Bool sel = (idx == selected);
                if (sel)
                    DrawRect(8, y - 2, WIDTH - 16, FONTH + 4, g_theme.menu_hl);
                DrawText(sel ? ">" : " ", 12, y,
                         sel ? g_theme.menu_sel : g_theme.menu_text);
                DrawText(labels[idx], 28, y,
                         sel ? g_theme.menu_sel : g_theme.menu_text);
            }
            DrawText("A: install   X: back", 10, 224, g_theme.menu_dim);
            DispUpdate();
            redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        if (k == KEY_UP)
        {
            selected = (selected - 1 + count) % count;
            redraw = True;
        }
        else if (k == KEY_DOWN)
        {
            selected = (selected + 1) % count;
            redraw = True;
        }
        else if (k == KEY_X)
        {
            return;
        }
        else if (k == KEY_A)
        {
            StoreItem* it = &items[selected];

            if (it->size == 0 || it->size > TH_MAX_BYTES)
            {
                ThMessage("That theme is too large.", "64 KB is the limit.");
                redraw = True;
                continue;
            }

            // <id>.THM must fit THEME_NAME_LEN (15 chars + NUL).
            char name[STORE_ID_LEN + 4];
            int n = 0;
            for (; it->id[n] && n < 11; n++) name[n] = it->id[n];
            name[n++] = '.';
            name[n++] = 'T';
            name[n++] = 'H';
            name[n++] = 'M';
            name[n] = 0;

            char path[64];
            strcpy(path, "/themes/");
            strcat(path, name);

            if (StoreDownloadChunks(TH_STORE_BASE, it, path, ThProgress))
            {
                ThemeSetActive(name);
                strcpy(installed, name);
                ThMessageEx("THEME STORE", "Installed and selected.", name);
                return;
            }

            ThMessage("Download failed.", "Check WiFi and try again.");
            redraw = True;
        }
    }
}

void ThemesRun(void)
{
    if (!DiskAutoMount())
    {
        ThMessage("SD card not found.", "Insert a card and try again.");
        return;
    }

    static char names[THEME_MAX_FILES][THEME_NAME_LEN];
    static const char* labels[THEME_MAX_FILES + 2];

    int n = ThemeList(names, THEME_MAX_FILES);

    // Row 0 is the store entry, 1 is Default, 2.. are files.
    labels[0] = "+ Install from internet";
    labels[1] = "Default";
    for (int i = 0; i < n; i++) labels[i + 2] = names[i];
    int count = n + 2;

    int selected = 1;
    {
        const char* act = ThemeActiveName();
        if (act && act[0])
            for (int i = 0; i < n; i++)
                if (StrComp(names[i], act) == 0) { selected = i + 2; break; }
    }

    int top = 0;
    Bool need_redraw = True;

    while (1)
    {
        if (selected < top) top = selected;
        if (selected >= top + TH_VISIBLE) top = selected - TH_VISIBLE + 1;
        if (top < 0) top = 0;

        if (need_redraw)
        {
            ThRender(labels, count, selected, top, ThemeActiveName());
            need_redraw = False;
        }

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }

        switch (k)
        {
        case KEY_UP:
            selected = (selected - 1 + count) % count;
            need_redraw = True;
            break;
        case KEY_DOWN:
            selected = (selected + 1) % count;
            need_redraw = True;
            break;
        case KEY_A:
            if (selected == 0)
            {
                char installed[STORE_ID_LEN + 4];
                ThOnlineRun(installed);
                if (installed[0])
                {
                    // Rescan so the new file appears in the list and is
                    // selected without leaving the app.
                    n = ThemeList(names, THEME_MAX_FILES);
                    for (int i = 0; i < n; i++) labels[i + 2] = names[i];
                    count = n + 2;
                    for (int i = 0; i < n; i++)
                        if (StrComp(names[i], installed) == 0)
                        {
                            selected = i + 2;
                            break;
                        }
                    top = 0;
                }
            }
            else if (selected == 1)
            {
                ThemeSetActive("");
            }
            else
            {
                ThemeSetActive(names[selected - 2]);
            }
            need_redraw = True;
            break;
        case KEY_X:
            return;
        default:
            break;
        }
    }
}
