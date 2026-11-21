// ****************************************************************************
//
//                  PicoDeck — credential manager
//
// ****************************************************************************

#include "../include.h"
#include "cred_app.h"
#include "theme.h"
#include "qrcode.h"

#define CRED_FILE   "/Conf/cred.txt"
#define CRED_TOKEN_LEN 40
#define CRED_URL_LEN   96

// ---------------------------------------------------------------------------
// Token storage
// ---------------------------------------------------------------------------

static Bool CredLoad(char* out, int max)
{
    out[0] = 0;

    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, CRED_FILE)) return False;

    char buf[128];
    int n = (int)FileRead(&f, buf, sizeof(buf) - 1);
    FileClose(&f);
    if (n <= 0) return False;
    buf[n] = 0;

    // Find a "token=..." line. Bounded so a short file cannot read past the
    // end of the buffer.
    int len = (int)StrLen(buf);
    int i = 0;
    while (i + 6 <= len)
    {
        Bool match = (buf[i]   == 't' || buf[i]   == 'T') &&
                     (buf[i+1] == 'o' || buf[i+1] == 'O') &&
                     (buf[i+2] == 'k' || buf[i+2] == 'K') &&
                     (buf[i+3] == 'e' || buf[i+3] == 'E') &&
                     (buf[i+4] == 'n' || buf[i+4] == 'N') &&
                     (buf[i+5] == '=');
        if (match)
        {
            i += 6;
            int o = 0;
            while (i < len && buf[i] != '\r' && buf[i] != '\n' && o < max - 1)
                out[o++] = buf[i++];
            out[o] = 0;
            return out[0] != 0;
        }
        while (i < len && buf[i] != '\n') i++;
        i++;
    }
    return False;
}

static Bool CredSave(const char* token)
{
    DirCreate("/Conf");

    sFile f;
    FileInit(&f);
    if (!FileCreate(&f, CRED_FILE)) return False;
    FileWrite(&f, "token=", 6);
    FileWrite(&f, token, (u32)StrLen(token));
    FileWrite(&f, "\n", 1);
    FileClose(&f);
    return True;
}

// ---------------------------------------------------------------------------
// Token generation
// ---------------------------------------------------------------------------

// xorshift64 seeded from the free-running timer. The server enforces token
// uniqueness, so a duplicate here is rejected instead of being a security
// problem.
static u32 CredRand(void)
{
    static u64 s = 0;
    if (s == 0)
    {
        u64 t = (u64)Time();
        t ^= ((u64)Millis() << 32);
        s = t ^ 0x9E3779B97F4A7C15ull;
        if (s == 0) s = 0x123456789ABCDEFull;
    }
    s ^= s << 13;
    s ^= s >> 7;
    s ^= s << 17;
    return (u32)(s >> 16);
}

static void CredHex4(char* out)
{
    static const char* hex = "0123456789ABCDEF";
    u32 v = CredRand();
    for (int i = 0; i < 4; i++)
    {
        out[i] = hex[(v >> (4 * (3 - i))) & 0xF];
    }
}

static void CredGenerate(char* out)
{
    int n = 0;
    const char* prefix = "PICO-DEV-";
    for (int i = 0; prefix[i]; i++) out[n++] = prefix[i];

    for (int g = 0; g < 4; g++)
    {
        CredHex4(out + n);
        n += 4;
        if (g < 3) out[n++] = '-';
    }
    out[n] = 0;
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

static void CredTitle(const char* title)
{
    DrawClearCol(g_theme.menu_bg);
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, 8,
              g_theme.menu_title);
    DrawRect(10, 28, WIDTH - 20, 1, g_theme.menu_dim);
}

static void CredShowToken(const char* token)
{
    CredTitle("CREDENTIAL MANAGER");

    DrawText("Register at pico.msuii.dev", 16, 38, g_theme.menu_text);

    char url[CRED_URL_LEN];
    int n = 0;
    const char* base = "https://pico.msuii.dev/register?token=";
    for (int i = 0; base[i] && n < CRED_URL_LEN - CRED_TOKEN_LEN - 2; i++)
        url[n++] = base[i];
    for (int i = 0; token[i] && n < CRED_URL_LEN - 1; i++) url[n++] = token[i];
    url[n] = 0;

    const u8* matrix = NULL;
    int size = 0;
    int y0 = 56;
    if (QrEncode(url, &matrix, &size))
    {
        int scale = 3;
        int px = size * scale;
        if (px > 132) { scale = 2; px = size * scale; }
        int x0 = (WIDTH - px) / 2;
        for (int r = 0; r < size; r++)
            for (int c = 0; c < size; c++)
                if (matrix[r * size + c])
                    DrawRect(x0 + c * scale, y0 + r * scale, scale, scale,
                             g_theme.menu_text);
        y0 += px + 8;
    }
    else
    {
        y0 += 16;
    }

    DrawText(token, (WIDTH - (int)StrLen(token) * FONTW) / 2, y0,
             g_theme.ok_color);
    DrawText("Scan, or type the token in.", 16, y0 + 18, g_theme.menu_dim);
    DrawText("A: new token   X: back", 10, 224, g_theme.menu_dim);
    DispUpdate();
}

static void CredNoToken(const char* note)
{
    CredTitle("CREDENTIAL MANAGER");
    DrawText("No token yet.", 16, 52, g_theme.menu_text);
    DrawText("A token lets you register a", 16, 80, g_theme.menu_text);
    DrawText("developer account at", 16, 94, g_theme.menu_text);
    DrawText("pico.msuii.dev.", 16, 108, g_theme.menu_text);
    DrawText("Each token works once.", 16, 136, g_theme.menu_dim);
    if (note) DrawText(note, 16, 158, g_theme.err_color);
    DrawText("A: generate   X: back", 10, 224, g_theme.menu_dim);
    DispUpdate();
}

static Bool CredConfirm(const char* token)
{
    CredTitle("CREDENTIAL MANAGER");
    DrawText("Replace this token?", 16, 56, g_theme.menu_text);
    DrawText(token, 16, 76, g_theme.menu_dim);
    DrawText("A registered token cannot be", 16, 104, g_theme.menu_dim);
    DrawText("used again, even if replaced.", 16, 118, g_theme.menu_dim);
    DrawText("A: replace   X: keep", 10, 224, g_theme.menu_dim);
    DispUpdate();

    while (1)
    {
        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(40); continue; }
        if (k == KEY_A) return True;
        if (k == KEY_X) return False;
    }
}

void CredentialRun(void)
{
    if (!DiskAutoMount())
    {
        CredTitle("CREDENTIAL MANAGER");
        DrawText("SD card not found.", 16, 80, g_theme.menu_text);
        DrawText("X: back", 10, 224, g_theme.menu_dim);
        DispUpdate();
        while (KeyGet() != KEY_X) WaitMs(40);
        return;
    }

    char token[CRED_TOKEN_LEN];
    Bool have = CredLoad(token, sizeof(token));
    const char* note = NULL;

    while (1)
    {
        if (have) CredShowToken(token);
        else      CredNoToken(note);

        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(30); continue; }
        if (k == KEY_X) return;

        if (k != KEY_A) continue;

        if (have && !CredConfirm(token))
        {
            note = NULL;
            continue;
        }

        char next[CRED_TOKEN_LEN];
        CredGenerate(next);
        if (CredSave(next))
        {
            for (int i = 0; i < CRED_TOKEN_LEN; i++) token[i] = next[i];
            have = True;
            note = NULL;
        }
        else
        {
            note = "Could not write /Conf/cred.txt.";
        }
    }
}
