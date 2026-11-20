// ****************************************************************************
//
//                  PicoDeck — theme support (layout engine)
//
// ****************************************************************************

#include "../include.h"
#include "theme.h"

Theme g_theme;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void CopyStr(char* dst, int dstlen, const char* src)
{
    int n = 0;
    while (src[n] && n < dstlen - 1) { dst[n] = src[n]; n++; }
    dst[n] = 0;
}

static char* Trim(char* s)
{
    while (*s == ' ' || *s == '\t') s++;
    int n = (int)StrLen(s);
    while (n > 0 && (s[n-1] == ' ' || s[n-1] == '\t' ||
                     s[n-1] == '\r' || s[n-1] == '\n')) s[--n] = 0;
    return s;
}

static int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static Bool StrEqI(const char* a, const char* b)
{
    while (*a && *b)
    {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return False;
    }
    return (*a == 0 && *b == 0);
}

// Named colours. Kept as a small table so themes stay readable.
static Bool ColorNamed(const char* s, u16* out)
{
    static const char* const names[] = {
        "black", "white", "gray", "ltgray", "dkgray", "red", "green", "blue",
        "yellow", "cyan", "magenta", "orange", "purple", "pink", "brown"
    };
    static const u8 rgb[][3] = {
        {0,0,0}, {255,255,255}, {128,128,128}, {192,192,192}, {64,64,64},
        {255,0,0}, {0,255,0}, {0,0,255}, {255,255,0}, {0,255,255},
        {255,0,255}, {255,165,0}, {128,0,128}, {255,192,203}, {165,42,42}
    };
    for (int i = 0; i < (int)(sizeof(names) / sizeof(names[0])); i++)
    {
        if (StrEqI(s, names[i]))
        {
            *out = RGBTO16(rgb[i][0], rgb[i][1], rgb[i][2]);
            return True;
        }
    }
    return False;
}

// ---------------------------------------------------------------------------
// Colour variables (set name=RRGGBB, referenced as $name)
// ---------------------------------------------------------------------------

#define THEME_VAR_MAX  16
#define THEME_VAR_NAME 12

typedef struct {
    char name[THEME_VAR_NAME];
    u16  color;
} ThemeVar;

static ThemeVar g_vars[THEME_VAR_MAX];
static int      g_var_n;

static Bool VarGet(const char* name, u16* out)
{
    for (int i = 0; i < g_var_n; i++)
        if (StrComp(g_vars[i].name, name) == 0) { *out = g_vars[i].color; return True; }
    return False;
}

static void VarSet(const char* name, u16 color)
{
    for (int i = 0; i < g_var_n; i++)
        if (StrComp(g_vars[i].name, name) == 0) { g_vars[i].color = color; return; }
    if (g_var_n >= THEME_VAR_MAX) return;
    CopyStr(g_vars[g_var_n].name, THEME_VAR_NAME, name);
    g_vars[g_var_n].color = color;
    g_var_n++;
}

static u16 ParseColor(const char* s, u16 def)
{
    if (*s == '#') s++;
    if (*s == '$')
    {
        u16 v;
        return VarGet(s + 1, &v) ? v : def;
    }

    u16 named;
    if (ColorNamed(s, &named)) return named;

    u32 v = 0;
    int n = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s && n < 6)
    {
        int d = HexVal(*s);
        if (d < 0) break;
        v = (v << 4) | (u32)d;
        n++;
        s++;
    }
    if (n == 3)     // RGB shorthand (F0C)
    {
        u32 r = (v >> 8) & 0xF, g = (v >> 4) & 0xF, b = v & 0xF;
        return RGBTO16((u8)(r * 17), (u8)(g * 17), (u8)(b * 17));
    }
    if (n != 6 || *s != 0) return def;
    return RGBTO16((u8)((v >> 16) & 0xFF), (u8)((v >> 8) & 0xFF), (u8)(v & 0xFF));
}

static int ParseInt(const char* s)
{
    int sign = 1, base = 10;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }

    int v = 0;
    while (1)
    {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f') d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F') d = *s - 'A' + 10;
        else break;
        v = v * base + d;
        s++;
    }
    return v * sign;
}

// Split a comma-separated element value into fields.
static int SplitFields(char* s, char fields[][THEME_TEXT_LEN], int max)
{
    int nf = 0;
    while (*s && nf < max)
    {
        // Skip whitespace around the separator so "a, b, c" works.
        while (*s == ' ' || *s == '\t') s++;

        char* dst = fields[nf];
        int i = 0;
        while (*s && *s != ',' && i < THEME_TEXT_LEN - 1) dst[i++] = *s++;

        while (i > 0 && (dst[i-1] == ' ' || dst[i-1] == '\t')) i--;
        dst[i] = 0;

        nf++;
        if (*s == ',') s++;
    }
    return nf;
}

// ---------------------------------------------------------------------------
// Element parsing
// ---------------------------------------------------------------------------

static u8 ParseAlign(const char* s)
{
    if (StrEqI(s, "center") || StrEqI(s, "centre") || StrEqI(s, "c")) return 1;
    if (StrEqI(s, "right") || StrEqI(s, "r")) return 2;
    int v = ParseInt(s);
    return (u8)(v < 0 ? 0 : (v > 2 ? 2 : v));
}

static Bool ParseElem(char* val, ThemeElem* e)
{
    char f[8][THEME_TEXT_LEN];
    int nf = SplitFields(val, f, 8);
    if (nf < 1) return False;

    const char* type = f[0];
    memset(e, 0, sizeof(*e));
    e->scale = 1;
    e->color = COL_WHITE;

    if (StrComp(type, "text") == 0 && nf >= 5)
    {
        e->type = TE_TEXT;
        e->x = ParseInt(f[1]);
        e->y = ParseInt(f[2]);
        CopyStr(e->text, THEME_TEXT_LEN, f[3]);
        e->color = ParseColor(f[4], COL_WHITE);
        if (nf >= 6) e->scale = (u8)(ParseInt(f[5]) >= 2 ? 2 : 1);
        if (nf >= 7) e->w = ParseAlign(f[6]);
    }
    else if (StrComp(type, "blink") == 0 && nf >= 6)
    {
        e->type = TE_BLINK;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        CopyStr(e->text, THEME_TEXT_LEN, f[3]);
        e->color = ParseColor(f[4], COL_WHITE);
        e->h = ParseInt(f[5]);
        if (nf >= 7) e->scale = (u8)(ParseInt(f[6]) >= 2 ? 2 : 1);
        if (nf >= 8) e->w = ParseAlign(f[7]);
    }
    else if (StrComp(type, "marquee") == 0 && nf >= 5)
    {
        e->type = TE_MARQUEE;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        CopyStr(e->text, THEME_TEXT_LEN, f[3]);
        e->color = ParseColor(f[4], COL_WHITE);
        if (nf >= 6) e->h = ParseInt(f[5]);
        if (nf >= 7) e->w = ParseInt(f[6]);
    }
    else if (StrComp(type, "rect") == 0 && nf >= 6)
    {
        e->type = TE_RECT;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->w = ParseInt(f[3]); e->h = ParseInt(f[4]);
        e->color = ParseColor(f[5], COL_WHITE);
    }
    else if (StrComp(type, "frame") == 0 && nf >= 6)
    {
        e->type = TE_FRAME;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->w = ParseInt(f[3]); e->h = ParseInt(f[4]);
        e->color = ParseColor(f[5], COL_WHITE);
        if (nf >= 7) { int th = ParseInt(f[6]); e->scale = (u8)(th < 1 ? 1 : (th > 8 ? 8 : th)); }
    }
    else if (StrComp(type, "line") == 0 && nf >= 6)
    {
        e->type = TE_LINE;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->w = ParseInt(f[3]); e->h = ParseInt(f[4]);
        e->color = ParseColor(f[5], COL_WHITE);
        if (nf >= 7) { int th = ParseInt(f[6]); e->scale = (u8)(th < 1 ? 1 : (th > 8 ? 8 : th)); }
    }
    else if (StrComp(type, "circle") == 0 && nf >= 5)
    {
        e->type = TE_CIRCLE;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        int r = ParseInt(f[3]);
        if (r < 0) r = 0;
        if (r > HEIGHT) r = HEIGHT;
        e->w = r;
        e->color = ParseColor(f[4], COL_WHITE);
    }
    else if (StrComp(type, "disc") == 0 && nf >= 5)
    {
        e->type = TE_DISC;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        int r = ParseInt(f[3]);
        if (r < 0) r = 0;
        if (r > HEIGHT) r = HEIGHT;
        e->w = r;
        e->color = ParseColor(f[4], COL_WHITE);
    }
    else if (StrComp(type, "duck") == 0 && nf >= 3)
    {
        e->type = TE_DUCK;
        e->x = ParseInt(f[1]);
        e->y = ParseInt(f[2]);
    }
    else if (StrComp(type, "bar") == 0 && nf >= 6)
    {
        e->type = TE_BAR;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->w = ParseInt(f[3]); e->h = ParseInt(f[4]);
        e->color = ParseColor(f[5], COL_GREEN);
    }
    else if (StrComp(type, "pct") == 0 && nf >= 4)
    {
        e->type = TE_PCT;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->color = ParseColor(f[3], COL_WHITE);
    }
    else if (StrComp(type, "spinner") == 0 && nf >= 4)
    {
        e->type = TE_SPINNER;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->color = ParseColor(f[3], COL_LTGRAY);
    }
    else if (StrComp(type, "status") == 0 && nf >= 3)
    {
        e->type = TE_STATUS;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
    }
    else if (StrComp(type, "usb") == 0 && nf >= 3)
    {
        e->type = TE_USB; e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
    }
    else if (StrComp(type, "sd") == 0 && nf >= 3)
    {
        e->type = TE_SD; e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
    }
    else if (StrComp(type, "battery") == 0 && nf >= 3)
    {
        e->type = TE_BATTERY; e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
    }
    else if (StrComp(type, "weather") == 0 && nf >= 3)
    {
        e->type = TE_WEATHER; e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->color = (nf >= 4) ? ParseColor(f[3], COL_LTGRAY) : COL_LTGRAY;
    }
    else if (StrComp(type, "time") == 0 && nf >= 4)
    {
        e->type = TE_TIME;
        e->x = ParseInt(f[1]); e->y = ParseInt(f[2]);
        e->color = ParseColor(f[3], COL_WHITE);
        if (nf >= 5) e->scale = (u8)(ParseInt(f[4]) >= 2 ? 2 : 1);
        if (nf >= 6) e->w = ParseAlign(f[5]);
    }
    else
    {
        return False;
    }
    return True;
}

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------

static void AddElem(ThemeElem* arr, int* n, u8 type, int x, int y,
                    int w, int h, const char* text, u16 color, u8 scale)
{
    if (*n >= THEME_MAX_ELEMS) return;
    ThemeElem* e = &arr[*n];
    memset(e, 0, sizeof(*e));
    e->type = type;
    e->x = x; e->y = y; e->w = w; e->h = h;
    if (text) CopyStr(e->text, THEME_TEXT_LEN, text);
    e->color = color;
    e->scale = scale;
    (*n)++;
}

static void ThemeDefaults(void)
{
    memset(&g_theme, 0, sizeof(g_theme));

    CopyStr(g_theme.duck[0], THEME_DUCK_LEN, "         __");
    CopyStr(g_theme.duck[1], THEME_DUCK_LEN, "   ___( o)>");
    CopyStr(g_theme.duck[2], THEME_DUCK_LEN, "   \\ <_. )");
    CopyStr(g_theme.duck[3], THEME_DUCK_LEN, "    `---'");
    g_theme.duck_color = COL_YELLOW;
    g_theme.bg_color = COL_BLACK;

    AddElem(g_theme.boot, &g_theme.boot_n, TE_TEXT, -1, 46, 0, 0,
            "PicoDeck", COL_YELLOW, 2);
    AddElem(g_theme.boot, &g_theme.boot_n, TE_TEXT, -1, 70, 0, 0,
            "Launcher", COL_YELLOW, 2);
    AddElem(g_theme.boot, &g_theme.boot_n, TE_BAR, 40, 150, WIDTH - 80, 12,
            "", COL_GREEN, 1);
    AddElem(g_theme.boot, &g_theme.boot_n, TE_PCT, -1, 172, 0, 0,
            "", COL_WHITE, 1);
    AddElem(g_theme.boot, &g_theme.boot_n, TE_SPINNER, 24, 152, 0, 0,
            "", COL_LTGRAY, 1);

    AddElem(g_theme.saver, &g_theme.saver_n, TE_TEXT, -1, 14, 0, 0,
            "PicoDeck", COL_GRAY, 1);
    AddElem(g_theme.saver, &g_theme.saver_n, TE_DUCK, 0, 54, 0, 0,
            "", COL_YELLOW, 1);
    AddElem(g_theme.saver, &g_theme.saver_n, TE_STATUS, 40, 130, 0, 0,
            "", COL_WHITE, 1);
    AddElem(g_theme.saver, &g_theme.saver_n, TE_TIME, -1, 182, 0, 0,
            "", COL_WHITE, 1);
    AddElem(g_theme.saver, &g_theme.saver_n, TE_WEATHER, 20, 198, 0, 0,
            "", COL_LTGRAY, 1);
    AddElem(g_theme.saver, &g_theme.saver_n, TE_TEXT, -1, 224, 0, 0,
            "Press any button", COL_GRAY, 1);

    CopyStr(g_theme.home_title1, THEME_TITLE_LEN, "PicoDeck");
    CopyStr(g_theme.home_title2, THEME_TITLE_LEN, "Launcher");
    g_theme.menu_bg    = COL_BLACK;
    g_theme.menu_title = COL_YELLOW;
    g_theme.menu_text  = COL_WHITE;
    g_theme.menu_sel   = COL_YELLOW;
    g_theme.menu_hl    = RGBTO16(40, 40, 0);
    g_theme.menu_dim   = COL_GRAY;

    g_theme.home_time_show     = True;
    g_theme.home_time_color    = COL_LTGRAY;
    g_theme.home_time_x        = -1;
    g_theme.home_time_y        = -1;
    g_theme.home_weather_show  = True;
    g_theme.home_weather_color = COL_LTGRAY;

    // Status palette.
    g_theme.ok_color     = COL_GREEN;
    g_theme.warn_color   = COL_YELLOW;
    g_theme.err_color    = COL_RED;
    g_theme.accent_color = COL_YELLOW;

    // Menu layout (home + submenus).
    g_theme.menu_title_scale = 2;
    g_theme.menu_title_y     = 16;
    g_theme.menu_title2_y    = 36;
    g_theme.menu_sub_title_y = 30;
    g_theme.menu_divider_y   = 64;
    g_theme.menu_start_y     = 80;
    g_theme.menu_line_h      = 20;
    g_theme.menu_footer_y    = 210;
    g_theme.menu_sel_style   = 0;
    g_theme.menu_show_footer = True;
    g_theme.menu_show_status = True;
    CopyStr(g_theme.menu_help, THEME_TEXT_LEN, "A: Open   X: Back");

    // Timing.
    g_theme.boot_ms           = 2000;
    g_theme.boot_show_version = True;
    g_theme.saver_timeout     = 30;
    g_theme.saver_speed       = 4;
    g_theme.saver_backlight   = 255;
}

static void ThemeRecompute(void)
{
    int maxlen = 0;
    for (int i = 0; i < THEME_DUCK_LINES; i++)
    {
        int n = (int)StrLen(g_theme.duck[i]);
        if (n > maxlen) maxlen = n;
    }
    if (maxlen < 1) maxlen = 1;
    g_theme.duck_w = maxlen * FONTW;
    if (g_theme.duck_w > WIDTH) g_theme.duck_w = WIDTH;
}

// ---------------------------------------------------------------------------
// File parsing
// ---------------------------------------------------------------------------

// Look for a boolean key anywhere in the file without modifying the buffer.
// Used for the append flags, which must work even when they appear after the
// element lines they affect.
static Bool FileFlag(const char* buf, const char* want)
{
    for (const char* p = buf; *p; )
    {
        const char* line = p;
        while (*p && *p != '\n') p++;

        const char* a = line;
        while (a < p && (*a == ' ' || *a == '\t')) a++;

        const char* b = want;
        while (*b && a < p && *a == *b) { a++; b++; }

        if (!*b)
        {
            while (a < p && (*a == ' ' || *a == '\t')) a++;
            if (a < p && *a == '=')
            {
                a++;
                while (a < p && (*a == ' ' || *a == '\t')) a++;
                return (a < p && *a != '0');
            }
        }

        if (*p == '\n') p++;
    }
    return False;
}

static void ThemeParse(char* buf)
{
    g_var_n = 0;

    // A theme's first "boot=" / "saver=" replaces the built-in list unless the
    // theme explicitly asks to append.
    Bool boot_append  = FileFlag(buf, "boot_append");
    Bool saver_append = FileFlag(buf, "saver_append");
    Bool boot_started = False, saver_started = False;

    char* p = buf;
    while (*p)
    {
        char* line = p;
        while (*p && *p != '\n') p++;
        if (*p == '\n') { *p = 0; p++; }

        line = Trim(line);
        if (*line == 0 || *line == '#' || *line == ';') continue;

        char* eq = line;
        while (*eq && *eq != '=') eq++;
        if (*eq != '=') continue;
        *eq = 0;
        char* key = Trim(line);
        char* raw = eq + 1;
        char* val = Trim(eq + 1);

        // Strip trailing " ; comment" from normal values. Mascot art is left
        // untouched because indentation and punctuation are significant there.
        if (!(key[0] == 'd' && key[1] == 'u' && key[2] == 'c' && key[3] == 'k'))
        {
            for (char* c = val; *c; c++)
            {
                if (*c == ';' && (c == val || c[-1] == ' ' || c[-1] == '\t'))
                {
                    *c = 0;
                    val = Trim(val);
                    break;
                }
            }
        }

        if      (StrComp(key, "bg") == 0)         g_theme.bg_color = ParseColor(val, g_theme.bg_color);
        else if (StrComp(key, "duck_color") == 0) g_theme.duck_color = ParseColor(val, g_theme.duck_color);
        else if (StrComp(key, "menu_bg") == 0)    g_theme.menu_bg = ParseColor(val, g_theme.menu_bg);
        else if (StrComp(key, "menu_title") == 0) g_theme.menu_title = ParseColor(val, g_theme.menu_title);
        else if (StrComp(key, "menu_text") == 0)  g_theme.menu_text = ParseColor(val, g_theme.menu_text);
        else if (StrComp(key, "menu_sel") == 0)   g_theme.menu_sel = ParseColor(val, g_theme.menu_sel);
        else if (StrComp(key, "menu_hl") == 0)    g_theme.menu_hl = ParseColor(val, g_theme.menu_hl);
        else if (StrComp(key, "menu_dim") == 0)   g_theme.menu_dim = ParseColor(val, g_theme.menu_dim);
        else if (StrComp(key, "accent") == 0)     g_theme.accent_color = ParseColor(val, g_theme.accent_color);
        else if (StrComp(key, "ok") == 0)         g_theme.ok_color = ParseColor(val, g_theme.ok_color);
        else if (StrComp(key, "warn") == 0)       g_theme.warn_color = ParseColor(val, g_theme.warn_color);
        else if (StrComp(key, "err") == 0)        g_theme.err_color = ParseColor(val, g_theme.err_color);
        else if (StrComp(key, "home_title1") == 0) CopyStr(g_theme.home_title1, THEME_TITLE_LEN, val);
        else if (StrComp(key, "home_title2") == 0) CopyStr(g_theme.home_title2, THEME_TITLE_LEN, val);
        else if (StrComp(key, "home_time") == 0)    g_theme.home_time_show = (ParseInt(val) != 0);
        else if (StrComp(key, "home_time_color") == 0)    g_theme.home_time_color = ParseColor(val, g_theme.home_time_color);
        else if (StrComp(key, "home_time_x") == 0)        g_theme.home_time_x = ParseInt(val);
        else if (StrComp(key, "home_time_y") == 0)        g_theme.home_time_y = ParseInt(val);
        else if (StrComp(key, "home_weather") == 0) g_theme.home_weather_show = (ParseInt(val) != 0);
        else if (StrComp(key, "home_weather_color") == 0) g_theme.home_weather_color = ParseColor(val, g_theme.home_weather_color);
        else if (StrComp(key, "menu_title_scale") == 0) { int v = ParseInt(val); g_theme.menu_title_scale = (u8)(v >= 2 ? 2 : 1); }
        else if (StrComp(key, "menu_title_y") == 0)   g_theme.menu_title_y = ParseInt(val);
        else if (StrComp(key, "menu_title2_y") == 0)  g_theme.menu_title2_y = ParseInt(val);
        else if (StrComp(key, "menu_sub_title_y") == 0) g_theme.menu_sub_title_y = ParseInt(val);
        else if (StrComp(key, "menu_divider_y") == 0) g_theme.menu_divider_y = ParseInt(val);
        else if (StrComp(key, "menu_start_y") == 0)   g_theme.menu_start_y = ParseInt(val);
        else if (StrComp(key, "menu_line_h") == 0)    { int v = ParseInt(val); if (v < 8) v = 8; if (v > 64) v = 64; g_theme.menu_line_h = v; }
        else if (StrComp(key, "menu_footer_y") == 0)  { int v = ParseInt(val); if (v < 0) v = 0; if (v > HEIGHT - 8) v = HEIGHT - 8; g_theme.menu_footer_y = v; }
        else if (StrComp(key, "menu_sel_style") == 0) { int v = ParseInt(val); if (v < 0) v = 0; if (v > 3) v = 3; g_theme.menu_sel_style = (u8)v; }
        else if (StrComp(key, "menu_show_footer") == 0) g_theme.menu_show_footer = (ParseInt(val) != 0);
        else if (StrComp(key, "menu_show_status") == 0) g_theme.menu_show_status = (ParseInt(val) != 0);
        else if (StrComp(key, "menu_help") == 0) CopyStr(g_theme.menu_help, THEME_TEXT_LEN, val);
        else if (StrComp(key, "boot_ms") == 0) { int v = ParseInt(val); if (v < 100) v = 100; if (v > 60000) v = 60000; g_theme.boot_ms = (u16)v; }
        else if (StrComp(key, "boot_show_version") == 0) g_theme.boot_show_version = (ParseInt(val) != 0);
        else if (StrComp(key, "saver_timeout") == 0) { int v = ParseInt(val); if (v < 0) v = 0; if (v > 3600) v = 3600; g_theme.saver_timeout = (u16)v; }
        else if (StrComp(key, "saver_speed") == 0) { int v = ParseInt(val); if (v < 1) v = 1; if (v > 40) v = 40; g_theme.saver_speed = (u8)v; }
        else if (StrComp(key, "saver_backlight") == 0) { int v = ParseInt(val); if (v < 0) v = 0; if (v > 255) v = 255; g_theme.saver_backlight = (u8)v; }
        // Mascot lines are taken literally after '=' so indentation survives.
        else if (StrComp(key, "duck1") == 0) CopyStr(g_theme.duck[0], THEME_DUCK_LEN, raw);
        else if (StrComp(key, "duck2") == 0) CopyStr(g_theme.duck[1], THEME_DUCK_LEN, raw);
        else if (StrComp(key, "duck3") == 0) CopyStr(g_theme.duck[2], THEME_DUCK_LEN, raw);
        else if (StrComp(key, "duck4") == 0) CopyStr(g_theme.duck[3], THEME_DUCK_LEN, raw);
        else if (key[0] == 's' && key[1] == 'e' && key[2] == 't' && key[3] == ' ')
            VarSet(Trim(key + 4), ParseColor(val, 0));
        else if (StrComp(key, "boot") == 0)
        {
            if (!boot_started) { boot_started = True; if (!boot_append) g_theme.boot_n = 0; }
            ThemeElem e;
            if (ParseElem(val, &e) && g_theme.boot_n < THEME_MAX_ELEMS)
                g_theme.boot[g_theme.boot_n++] = e;
        }
        else if (StrComp(key, "saver") == 0)
        {
            if (!saver_started) { saver_started = True; if (!saver_append) g_theme.saver_n = 0; }
            ThemeElem e;
            if (ParseElem(val, &e) && g_theme.saver_n < THEME_MAX_ELEMS)
                g_theme.saver[g_theme.saver_n++] = e;
        }
    }
    ThemeRecompute();
}

#define THEME_ACTIVE_CFG  "/themes/active.cfg"

static char g_active[THEME_NAME_LEN] = "";

const char* ThemeActiveName(void) { return g_active; }

// Load /themes/<name>.
static void ThemeLoadByName(const char* name)
{
    char path[64];
    strcpy(path, "/themes/");
    int n = (int)StrLen(path);
    int i = 0;
    while (name[i] && n < (int)sizeof(path) - 1) path[n++] = name[i++];
    path[n] = 0;

    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return;

    static char buf[4096];
    u32 rd = FileRead(&f, buf, sizeof(buf) - 1);
    FileClose(&f);
    buf[rd] = 0;

    ThemeParse(buf);
}

static void ThemeReadActive(void)
{
    g_active[0] = 0;

    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, THEME_ACTIVE_CFG)) return;

    char buf[THEME_NAME_LEN];
    u32 n = FileRead(&f, buf, THEME_NAME_LEN - 1);
    FileClose(&f);
    buf[n] = 0;

    char* s = Trim(buf);
    CopyStr(g_active, THEME_NAME_LEN, s);
}

void ThemeLoad(void)
{
    ThemeDefaults();

    if (!DiskAutoMount()) return;

    ThemeReadActive();
    if (g_active[0]) ThemeLoadByName(g_active);
}

int ThemeList(char names[][THEME_NAME_LEN], int max)
{
    int count = 0;
    const char* dirs[2] = { "/themes", "/Themes" };

    for (int d = 0; d < 2 && count < max; d++)
    {
        sFile find;
        sFileInfo info;
        FileInit(&find);
        if (!FindOpen(&find, dirs[d])) continue;

        while (count < max && FindNext(&find, &info, ATTR_FILE_MASK, "*"))
        {
            if (info.attr & ATTR_DIR) continue;
            int nl = info.namelen;
            if (nl < 5) continue;

            char e2 = info.name[nl-3], e3 = info.name[nl-2], e4 = info.name[nl-1];
            Bool is_ini = ((e2=='I'||e2=='i') && (e3=='N'||e3=='n') && (e4=='I'||e4=='i'));
            Bool is_thm = ((e2=='T'||e2=='t') && (e3=='H'||e3=='h') && (e4=='M'||e4=='m'));
            if (!is_ini && !is_thm) continue;
            if ((int)StrLen(info.name) == 10 &&
                (info.name[0]=='A'||info.name[0]=='a') &&
                (info.name[1]=='C'||info.name[1]=='c') &&
                (info.name[2]=='T'||info.name[2]=='t') &&
                (info.name[3]=='I'||info.name[3]=='i') &&
                (info.name[4]=='V'||info.name[4]=='v') &&
                (info.name[5]=='E'||info.name[5]=='e')) continue;   // skip active.cfg

            int n = (nl < THEME_NAME_LEN - 1) ? nl : THEME_NAME_LEN - 1;
            memcpy(names[count], info.name, n);
            names[count][n] = 0;
            count++;
        }
        FindClose(&find);
    }
    return count;
}

void ThemeSetActive(const char* name)
{
    if (name && name[0])
    {
        sFile f;
        FileInit(&f);
        if (FileCreate(&f, THEME_ACTIVE_CFG))
        {
            FileWrite(&f, name, (u32)StrLen(name));
            FileWrite(&f, "\n", 1);
            FileClose(&f);
        }
    }
    else
    {
        FileDelete(THEME_ACTIVE_CFG);
    }

    ThemeLoad();
}

// ---------------------------------------------------------------------------
// Element rendering (shared by boot splash + screensaver)
// ---------------------------------------------------------------------------

static void DrawTextAligned(const char* s, int x, int y, int align,
                            u16 col, u8 scale)
{
    int w = (int)StrLen(s) * FONTW * scale;
    if (x < 0 || align == 1) x = (WIDTH - w) / 2;
    else if (align == 2)     x = (x > 0 ? x : WIDTH - 8) - w;

    if (scale >= 2) DrawText2(s, x, y, col);
    else            DrawText(s, x, y, col);
}

static void DrawMarquee(const ThemeElem* e, u32 t, u16 col)
{
    int len = (int)StrLen(e->text);
    if (len == 0) return;

    int vis = (e->w > 0 ? e->w : WIDTH - e->x) / FONTW;
    if (vis < 1) vis = 1;
    if (vis > THEME_TEXT_LEN - 1) vis = THEME_TEXT_LEN - 1;
    if (vis >= len) { DrawText(e->text, e->x, e->y, col); return; }

    int step = e->h > 0 ? e->h : 150;
    int span = len + 3;
    int start = (int)((t / (u32)step) % (u32)span);
    if (start > len) return;

    int n = len - start;
    if (n > vis) n = vis;

    char buf[THEME_TEXT_LEN];
    memcpy(buf, e->text + start, n);
    buf[n] = 0;
    DrawText(buf, e->x, e->y, col);
}

void ThemeDrawElem(const ThemeElem* e, const ThemeDrawCtx* c)
{
    switch (e->type)
    {
    case TE_TEXT:
        DrawTextAligned(e->text, e->x, e->y, e->w, e->color, e->scale);
        break;

    case TE_BLINK:
    {
        int period = e->h > 0 ? e->h : 500;
        if (((int)(c->t / (u32)period) & 1) == 0)
            DrawTextAligned(e->text, e->x, e->y, e->w, e->color, e->scale);
        break;
    }

    case TE_MARQUEE:
        DrawMarquee(e, c->t, e->color);
        break;

    case TE_DUCK:
        for (int i = 0; i < THEME_DUCK_LINES; i++)
            DrawText(g_theme.duck[i], c->duck_x + e->x, e->y + i * 12,
                     g_theme.duck_color);
        break;

    case TE_BAR:
    {
        DrawFrame(e->x, e->y, e->w, e->h, COL_GRAY);
        int fill = e->w - 4;
        if (!c->saver)
        {
            u32 dur = g_theme.boot_ms ? g_theme.boot_ms : 2000u;
            fill = (int)((u32)(e->w - 4) * c->t / dur);
        }
        if (fill > 0) DrawRect(e->x + 2, e->y + 2, fill, e->h - 4, e->color);
        break;
    }

    case TE_PCT:
    {
        int pct = 100;
        if (!c->saver)
        {
            u32 dur = g_theme.boot_ms ? g_theme.boot_ms : 2000u;
            pct = (int)(c->t * 100u / dur);
            if (pct > 100) pct = 100;
        }
        char b[8];
        DecNum(b, (u32)pct, 0);
        strcat(b, "%");
        DrawTextAligned(b, e->x, e->y, e->w, e->color, 1);
        break;
    }

    case TE_SPINNER:
    {
        const char* spin = "|/-\\";
        char s[2];
        s[0] = spin[c->frame & 3];
        s[1] = 0;
        DrawText(s, e->x, e->y, e->color);
        break;
    }

    case TE_RECT:
        DrawRect(e->x, e->y, e->w, e->h, e->color);
        break;

    case TE_FRAME:
        if (e->scale > 1) DrawFrameW(e->x, e->y, e->w, e->h, e->color, e->scale);
        else              DrawFrame(e->x, e->y, e->w, e->h, e->color);
        break;

    case TE_LINE:
        if (e->scale > 1) DrawLineW(e->x, e->y, e->w, e->h, e->color, e->scale, True);
        else              DrawLine(e->x, e->y, e->w, e->h, e->color);
        break;

    case TE_CIRCLE:
        DrawCircle(e->x, e->y, e->w, e->color);
        break;

    case TE_DISC:
        DrawFillCircle(e->x, e->y, e->w, e->color);
        break;

    case TE_STATUS:
        DrawText("USB", e->x, e->y, g_theme.menu_dim);
        DrawText(c->usb ? c->usb : "-", e->x + 100, e->y, c->usb_col);
        DrawText("SD", e->x, e->y + 22, g_theme.menu_dim);
        DrawText(c->sd ? c->sd : "-", e->x + 100, e->y + 22, c->sd_col);
        DrawText("Battery", e->x, e->y + 44, g_theme.menu_dim);
        DrawText(c->batt ? c->batt : "-", e->x + 100, e->y + 44, c->batt_col);
        break;

    case TE_USB:
        if (c->usb) DrawText(c->usb, e->x, e->y, c->usb_col);
        break;

    case TE_SD:
        if (c->sd) DrawText(c->sd, e->x, e->y, c->sd_col);
        break;

    case TE_BATTERY:
        if (c->batt) DrawText(c->batt, e->x, e->y, c->batt_col);
        break;

    case TE_WEATHER:
    case TE_TIME:
        break;

    default:
        break;
    }
}

void ThemeDrawList(const ThemeElem* list, int n, const ThemeDrawCtx* c)
{
    for (int i = 0; i < n; i++) ThemeDrawElem(&list[i], c);
}
