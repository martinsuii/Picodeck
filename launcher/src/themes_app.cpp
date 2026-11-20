// ****************************************************************************
//
//                  PicoDeck — Themes app
//
//   Lists "Default" plus every theme file in /themes and sets the active one.
//   The launcher always uses the default unless a theme is picked here.
//
// ****************************************************************************

#include "../include.h"
#include "themes_app.h"
#include "theme.h"

#define TH_VISIBLE 9

static void ThMessage(const char* l1, const char* l2)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2("Themes", (WIDTH - 6 * FONTW * 2) / 2, 30, g_theme.menu_title);
    if (l1) DrawText(l1, 20, 100, g_theme.menu_text);
    if (l2) DrawText(l2, 20, 120, g_theme.menu_dim);
    DrawText("Press X to return", 20, 210, g_theme.menu_dim);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
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

        Bool is_active = (idx == 0) ? (!active || !active[0])
                                    : (active && StrComp(labels[idx], active) == 0);

        DrawText(sel ? ">" : " ", 12, y,
                 sel ? g_theme.menu_sel : g_theme.menu_text);
        DrawText(labels[idx], 28, y,
                 sel ? g_theme.menu_sel : g_theme.menu_text);
        if (is_active) DrawText("*", WIDTH - 20, y, g_theme.ok_color);
    }

    DrawText("A: Set theme   X: Back", 10, 224, g_theme.menu_dim);
    DispUpdate();
}

void ThemesRun(void)
{
    if (!DiskAutoMount())
    {
        ThMessage("SD card not found.", "Insert a card and try again.");
        return;
    }

    static char names[THEME_MAX_FILES][THEME_NAME_LEN];
    int n = ThemeList(names, THEME_MAX_FILES);

    // Build a label list: index 0 = "Default", then theme names.
    static const char* labels[THEME_MAX_FILES + 1];
    labels[0] = "Default";
    for (int i = 0; i < n; i++) labels[i + 1] = names[i];
    int count = n + 1;

    int selected = 0;
    {
        const char* act = ThemeActiveName();
        if (act && act[0])
            for (int i = 0; i < n; i++)
                if (StrComp(names[i], act) == 0) { selected = i + 1; break; }
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
            if (selected == 0) ThemeSetActive("");
            else               ThemeSetActive(names[selected - 1]);
            need_redraw = True;
            break;
        case KEY_X:
            return;
        default:
            break;
        }
    }
}
