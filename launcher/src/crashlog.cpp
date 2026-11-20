// ****************************************************************************
//
//                  PicoDeck — crash log writer
//
// ****************************************************************************

#include "../include.h"
#include "crashlog.h"

static char g_path[32];

static int Append(char* buf, int o, int max, const char* s)
{
    while (*s && o < max - 1) buf[o++] = *s++;
    buf[o] = 0;
    return o;
}

static void Put3(char* p, int v)
{
    p[0] = (char)('0' + (v / 100) % 10);
    p[1] = (char)('0' + (v / 10) % 10);
    p[2] = (char)('0' + v % 10);
}

Bool CrashLogSave(const char* title, const char* body)
{
    g_path[0] = 0;

    // A crash can happen before the SD was mounted (or after SDTerm), even
    // though the card is physically present. Try a fresh mount before
    // declaring there is no card.
    if (!DiskValid())
    {
        SDInit();
        DiskAutoMount();
    }
    if (!DiskValid()) return False;

    DirCreate("/crash");

    char path[32];
    Bool found = False;

    for (int i = 1; i < 1000; i++)
    {
        strcpy(path, "/crash/CRASH");
        Put3(path + 12, i);
        strcat(path, ".TXT");
        if (!FileExist(path)) { found = True; break; }
    }
    if (!found) strcpy(path, "/crash/CRASH999.TXT");

    char buf[512];
    int o = 0;
    o = Append(buf, o, sizeof(buf), title ? title : "CRASH");
    o = Append(buf, o, sizeof(buf), "\nPicoDeck ");
    o = Append(buf, o, sizeof(buf), LAUNCHER_VERSION_STR);
    o = Append(buf, o, sizeof(buf), "\n");
    if (body) o = Append(buf, o, sizeof(buf), body);
    o = Append(buf, o, sizeof(buf), "\n");

    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, path)) return False;
    Bool ok = (FileWrite(&f, buf, (u32)o) == (u32)o);
    FileClose(&f);

    if (!ok) return False;

    strncpy(g_path, path, sizeof(g_path) - 1);
    g_path[sizeof(g_path) - 1] = 0;
    return True;
}

const char* CrashLogLastPath(void)
{
    return g_path;
}
