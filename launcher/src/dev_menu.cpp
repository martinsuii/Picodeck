// ****************************************************************************
//
//                  PicoDeck — hidden developer mode
//
//   Calculator code 789789 -> this menu. Toggling it writes or removes an
//   empty /Conf/dev.txt. The marker is the only state: no flash flag, so
//   deleting the file on a PC turns developer mode off again.
//
// ****************************************************************************

#include "../include.h"
#include "dev_menu.h"
#include "theme.h"

static const char* const DevPaths[3] = {
    "/Conf/dev.txt",
    "/conf/dev.txt",
    "/Conf/DEV.TXT",
};

static const char* DevFindPath(void)
{
    for (int i = 0; i < 3; i++)
        if (FileExist(DevPaths[i])) return DevPaths[i];
    return NULL;
}

Bool DevModeEnabled(void)
{
    if (!DiskMounted() && !DiskAutoMount()) return False;
    return DevFindPath() != NULL;
}

static Bool DevEnable(void)
{
    DirCreate("/Conf");

    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, DevPaths[0])) return False;
    FileClose(&f);
    return True;
}

static void DevDisable(void)
{
    for (int i = 0; i < 3; i++)
        if (FileExist(DevPaths[i])) FileDelete(DevPaths[i]);
}

static void DevRender(Bool enabled, const char* note)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2("DEVELOPER MODE", (WIDTH - 14 * FONTW * 2) / 2, 12,
              g_theme.menu_title);
    DrawRect(10, 40, WIDTH - 20, 1, g_theme.menu_dim);

    DrawText("State:", 16, 56, g_theme.menu_dim);
    DrawText(enabled ? "ON" : "OFF", 90, 56,
             enabled ? g_theme.ok_color : g_theme.menu_text);

    DrawText("Marker:", 16, 80, g_theme.menu_dim);
    DrawText("/Conf/dev.txt", 90, 80, g_theme.menu_text);

    DrawText("While this is on, SETTINGS", 16, 112, g_theme.menu_text);
    DrawText("shows the Credential manager.", 16, 126, g_theme.menu_text);
    DrawText("Turn it off again to hide it.", 16, 140, g_theme.menu_text);

    if (note) DrawText(note, 16, 168, g_theme.menu_dim);

    DrawText("A: toggle   X: back", 10, 224, g_theme.menu_dim);
    DispUpdate();
}

void DevMenuRun(void)
{
    if (!DiskAutoMount())
    {
        DrawClearCol(g_theme.menu_bg);
        DrawText2("DEVELOPER MODE", (WIDTH - 14 * FONTW * 2) / 2, 12,
                  g_theme.menu_title);
        DrawText("SD card not found.", 16, 100, g_theme.menu_text);
        DrawText("Insert a card and try again.", 16, 120, g_theme.menu_dim);
        DrawText("X: back", 10, 224, g_theme.menu_dim);
        DispUpdate();
        while (KeyGet() != KEY_X) WaitMs(40);
        return;
    }

    Bool enabled = DevModeEnabled();
    const char* note = NULL;

    while (1)
    {
        DevRender(enabled, note);

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(30); continue; }

        if (k == KEY_X) return;

        if (k == KEY_A)
        {
            if (enabled)
            {
                DevDisable();
                enabled = False;
                note = "Developer mode disabled.";
            }
            else if (DevEnable())
            {
                enabled = True;
                note = "Developer mode enabled.";
            }
            else
            {
                note = "Could not write /Conf/dev.txt.";
            }
        }
    }
}
