// ****************************************************************************
//
//                  PicoDeck — "Update from SD"
//
//   Finds a launcher update UF2 on the SD card, checks its version (warns if
//   not newer, but lets you continue), then reflashes the launcher in place.
//
//   Self-update safety: the erase + program must not run from the flash being
//   overwritten. FlashErase()/the bootrom wrappers are NOFLASH (RAM-resident),
//   so the whole apply routine is NOFLASH: it erases and programs via the
//   bootrom directly (avoiding the flash-resident Boot2Check which would be
//   erased mid-update). The USR LED toggles as a progress/debug indicator.
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "sd_upgrade.h"
#include "uf2_parser.h"
#include "theme.h"

extern FRAMETYPE ALIGNED FrameBuf[FRAMESIZE];

#define STAGE_CAP   (FRAMESIZE * (int)sizeof(FRAMETYPE))
#define MAX_UPDATES 8
#define NAME_LEN    20
#define VER_LEN     16

#define UPD_LED_PIN 22   // active-low USR LED

static char g_upd_names[MAX_UPDATES][NAME_LEN];
static char g_upd_vers[MAX_UPDATES][VER_LEN];
static int  g_upd_count;

static u32 ReadLE32(const u8* p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// ---------------------------------------------------------------------------
// USR LED (active-low)
// ---------------------------------------------------------------------------

static void UpdLedInit(void)
{
    GPIO_Init(UPD_LED_PIN);
    GPIO_DirOut(UPD_LED_PIN);
    GPIO_Out1(UPD_LED_PIN);   // off
}

static void UpdLedOn(void)  { GPIO_Out0(UPD_LED_PIN); }
static void UpdLedOff(void) { GPIO_Out1(UPD_LED_PIN); }

// ---------------------------------------------------------------------------
// Version helpers
// ---------------------------------------------------------------------------

static Bool VerParse(const char* s, int* a, int* b, int* c)
{
    int v[3] = { 0, 0, 0 };
    int idx = 0;
    while (*s && idx < 3)
    {
        if (*s < '0' || *s > '9') return False;
        while (*s >= '0' && *s <= '9') { v[idx] = v[idx] * 10 + (*s - '0'); s++; }
        if (idx < 2) { if (*s != '.') return False; s++; }
        idx++;
    }
    if (*s != 0) return False;
    *a = v[0]; *b = v[1]; *c = v[2];
    return True;
}

static int VerCmp(int a1, int b1, int c1, int a2, int b2, int c2)
{
    if (a1 != a2) return a1 < a2 ? -1 : 1;
    if (b1 != b2) return b1 < b2 ? -1 : 1;
    if (c1 != c2) return c1 < c2 ? -1 : 1;
    return 0;
}

// Read the "AMTV<version>" tag from the staged image. There is another
// literal "AMTV" elsewhere in the image (uf2_inspect's tag string), so keep
// scanning until a tag is followed by at least one digit/dot.
static Bool FindVersion(const u8* stage, u32 used, char* ver)
{
    for (u32 i = 0; i + 5 < used; i++)
    {
        if (stage[i] == 'A' && stage[i+1] == 'M' &&
            stage[i+2] == 'T' && stage[i+3] == 'V')
        {
            int n = 0;
            u32 j = i + 4;
            while (j < used && n < VER_LEN - 1)
            {
                char c = (char)stage[j];
                if ((c >= '0' && c <= '9') || c == '.') { ver[n++] = c; j++; }
                else break;
            }
            ver[n] = 0;
            if (n > 0) return True;
            // Not the version tag (e.g. a plain "AMTV" string): keep looking.
        }
    }
    return False;
}

// ---------------------------------------------------------------------------
// Staging
// ---------------------------------------------------------------------------

// Stage a launcher-update UF2 into the frame buffer. Returns True if it is a
// valid launcher image (all blocks in the launcher region, RP2040 family).
static Bool StageUf2(const char* path, u32* p_used)
{
    sFile f;
    FileInit(&f);
    if (!FileOpen(&f, path)) return False;

    u8* stage = (u8*)FrameBuf;
    // Seed the staging area with erased-flash value so pages not present in
    // the UF2 (gaps between regions) are programmed as 0xFF rather than stale
    // frame-buffer pixels.
    memset(stage, 0xFF, STAGE_CAP);
    static u8 rbuf[512];
    u32 base = 0, used = 0;
    Bool base_set = False, ok = True;

    while (ok)
    {
        u32 n = FileRead(&f, rbuf, sizeof(rbuf));
        if (n == 0) break;

        for (u32 b = 0; b + UF2_BLOCK_SIZE <= n; b += UF2_BLOCK_SIZE)
        {
            const u8* block = rbuf + b;
            if (ReadLE32(block + 0) != UF2_MAGIC_START_0) continue;
            if (ReadLE32(block + 4) != UF2_MAGIC_START_1) continue;

            u32 target = ReadLE32(block + 12);
            u32 psize  = ReadLE32(block + 16);
            u32 fam    = ReadLE32(block + 28);

            if (psize > UF2_PAYLOAD_SIZE || fam != FAMILY_RP2040 ||
                target < LAUNCHER_FLASH_BASE || target >= LAUNCHER_FLASH_END)
            {
                ok = False;
                break;
            }
            if (!base_set) { base = target; base_set = True; }
            if (target < base) { ok = False; break; }

            u32 off = target - base;
            if (off + psize > (u32)STAGE_CAP) { ok = False; break; }

            memcpy(stage + off, block + 32, psize);
            if (off + psize > used) used = off + psize;
        }

        if (n < sizeof(rbuf)) break;
    }
    FileClose(&f);

    if (!ok || !base_set || base != LAUNCHER_FLASH_BASE || used == 0)
        return False;

    *p_used = used;
    return True;
}

// ---------------------------------------------------------------------------
// RAM-resident apply: erase + program + verify + wait-for-reboot (never
// returns). Everything here must stay in RAM (no flash calls) because the
// launcher flash region is erased and reprogrammed below.
// ---------------------------------------------------------------------------

static void NOFLASH(SdApplyAndReset)(u32 off, const u8* data,
                                     u32 size, u32 erase_size)
{
    __asm volatile ("cpsid i" ::: "memory");

    // Erase — mirror sdk_flash.c FlashErase (64KB block erase 0xD8, the
    // bootrom handles any sub-block tail via 4KB sector erase), but without
    // Boot2Check (boot2 at 0x0 is untouched, and this code is RAM-resident).
    FlashInternal();
    FlashExitXip();
    {
        u32 addr = off;
        u32 count = erase_size;
        if (((addr & 0xffff) == 0x8000) && (count >= 0x8000))
        {
            RomFlashErase(addr, 0x8000u, 0x8000u, 0x52u);   // 32 KB block
            addr += 0x8000u;
            count -= 0x8000u;
        }
        if (count > 0)
            RomFlashErase(addr, count, 0x10000u, 0xD8u);    // 64 KB block
    }
    FlashFlush();
    FlashEnterXip();
    sio_hw->gpio_togl = (1u << 22);

    // Program the recovery image first, then the launcher. If power is lost
    // during the launcher write, boot2 still takes us into a valid recovery
    // menu. (The staged image starts at LAUNCHER_FLASH_BASE.)
    const u32 rec_off = LAUNCHER_RECOVERY_BASE - LAUNCHER_FLASH_BASE;
    u32 la_size  = (size > rec_off) ? rec_off : size;
    u32 rec_size = (size > rec_off) ? (size - rec_off) : 0;

    FlashInternal();
    FlashExitXip();
    if (rec_size)
        RomFlashProgram(off + rec_off, data + rec_off, rec_size);
    RomFlashProgram(off, data, la_size);
    FlashFlush();
    FlashEnterXip();

    // Verify both parts against the staged image. (Cache was flushed above,
    // so reads are fresh.)
    u32 bad = 0;
    {
        const u8* fl = (const u8*)(0x10000000u + off);
        for (u32 i = 0; i < la_size; i++)
            if (fl[i] != data[i]) bad++;
        if (rec_size)
        {
            const u8* fr = (const u8*)(0x10000000u + off + rec_off);
            for (u32 i = 0; i < rec_size; i++)
                if (fr[i] != data[rec_off + i]) bad++;
        }
    }

    // Report via USR LED (active-low, GPIO22):
    //   solid ON  = verify OK
    //   blinking  = verify FAILED
    if (bad == 0)
        GPIO_Out0(UPD_LED_PIN);               // LED on
    else
        GPIO_Out1(UPD_LED_PIN);               // LED off (then blink)

    // A warm reboot (watchdog reset or boot2 re-run) cannot reliably boot the
    // freshly-flashed launcher: the external flash/display are left in a state
    // the bootrom/boot2 can't recover from without a full power cycle. So
    // report the result on the LED (blink on failure) and wait for the user to
    // power the device off and back on.
    while (1)
    {
        if (bad != 0)
        {
            volatile u32 d = 8000000; while (d--) __asm volatile ("" ::: "memory");
            sio_hw->gpio_togl = (1u << 22);
        }
    }
}

// ---------------------------------------------------------------------------
// UI (themed to match the launcher's submenu style)
// ---------------------------------------------------------------------------

#define UPD_TITLE_Y    30
#define UPD_DIVIDER_Y  64
#define UPD_TOP_Y      80
#define UPD_LINE_H     20
#define UPD_FOOTER_Y   210

static int UpdTextWidth(const char* s)  { return (int)StrLen(s) * FONTW; }
static int UpdTextWidth2(const char* s) { return (int)StrLen(s) * FONTW * 2; }

static void UpdCenteredText(const char* s, int y, u16 color)
{
    DrawText(s, (WIDTH - UpdTextWidth(s)) / 2, y, color);
}

static void UpdCenteredText2(const char* s, int y, u16 color)
{
    DrawText2(s, (WIDTH - UpdTextWidth2(s)) / 2, y, color);
}

// Common frame: themed background, centred title, divider, footer.
static void UpdFrame(const char* title, const char* footer)
{
    DrawClearCol(g_theme.menu_bg);
    UpdCenteredText2(title, UPD_TITLE_Y, g_theme.menu_title);
    DrawRect(40, UPD_DIVIDER_Y, WIDTH - 80, 1, g_theme.menu_dim);
    DrawRect(20, UPD_FOOTER_Y - 8, WIDTH - 40, 1, g_theme.menu_dim);
    if (footer) DrawText(footer, 20, UPD_FOOTER_Y, g_theme.menu_dim);
}

static void Msg(const char* l1, const char* l2)
{
    UpdFrame("update from sd", "X: Back");
    if (l1) UpdCenteredText(l1, 100, g_theme.menu_text);
    if (l2) UpdCenteredText(l2, 120, g_theme.menu_dim);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

// Scan the SD root for .uf2 files that are valid launcher updates.
static void ScanUpdates(void)
{
    g_upd_count = 0;

    sFile find;
    sFileInfo info;
    FileInit(&find);
    if (!FindOpen(&find, "/")) return;

    while (g_upd_count < MAX_UPDATES && FindNext(&find, &info, ATTR_FILE_MASK, "*"))
    {
        if (info.attr & ATTR_DIR) continue;
        int nl = info.namelen;
        if (nl < 5) continue;
        if (!(info.name[nl-4]=='.' &&
              (info.name[nl-3]=='U'||info.name[nl-3]=='u') &&
              (info.name[nl-2]=='F'||info.name[nl-2]=='f') &&
              (info.name[nl-1]=='2'))) continue;

        char path[NAME_LEN + 2];
        strcpy(path, "/");
        int n = 0;
        while (info.name[n] && n < NAME_LEN - 1) { path[1 + n] = info.name[n]; n++; }
        path[1 + n] = 0;

        u32 used;
        if (!StageUf2(path, &used)) continue;
        if (!FindVersion((const u8*)FrameBuf, used, g_upd_vers[g_upd_count]))
            strcpy(g_upd_vers[g_upd_count], "?");
        strcpy(g_upd_names[g_upd_count], path);
        g_upd_count++;
    }
    FindClose(&find);
}

static int PickUpdate(void)
{
    // Prefer a file named update.uf2 / UPDATE.UF2.
    for (int i = 0; i < g_upd_count; i++)
    {
        const char* p = g_upd_names[i];
        while (*p == '/') p++;
        if ((p[0]=='U'||p[0]=='u') && (p[1]=='P'||p[1]=='p') &&
            (p[2]=='D'||p[2]=='d') && (p[3]=='A'||p[3]=='a') &&
            (p[4]=='T'||p[4]=='t') && (p[5]=='E'||p[5]=='e'))
            return i;
    }
    if (g_upd_count == 1) return 0;
    return -1;   // let the user pick
}

// Stage path into FrameBuf and apply it (never returns on success).
static void ApplyUpdate(const char* path)
{
    UpdLedOn();

    // Draw the "updating..." screen FIRST and blit it to the ST7789 (whose
    // GRAM keeps it visible). Then stage the update into FrameBuf. Staging
    // MUST happen after drawing: it overwrites FrameBuf, and SdApplyAndReset
    // programs FrameBuf — if we drew after staging, we'd flash the screen
    // pixels instead of the update.
    UpdFrame("update from sd", "Power off/on to reboot");
    DrawText("updating launcher...", 60, 90, g_theme.menu_text);
    DrawText("do not power off!", 60, 110, g_theme.menu_text);
    DrawText("verifying after flash", 60, 132, g_theme.menu_dim);
    DrawText("usb led:", 60, 152, g_theme.menu_dim);
    DrawText("  on    = verify ok", 60, 170, g_theme.menu_text);
    DrawText("  blink = verify failed", 60, 188, g_theme.menu_text);
    DispUpdate();
    WaitMs(300);

    u32 used;
    if (!StageUf2(path, &used))
    {
        Msg("Failed to read update.", path);
        UpdLedOff();
        return;
    }

    // The update is now staged in RAM — delete the source .uf2 so it isn't
    // detected again on the next scan. (This must happen before the flash:
    // after SdApplyAndReset erases the launcher, the FAT code is gone.)
    FileDelete(path);

    u32 erase_size = (used + 4095u) & ~4095u;
    NativeWifiPause();
    SdApplyAndReset(LAUNCHER_FLASH_BASE - 0x10000000u,
                    (const u8*)FrameBuf, used, erase_size);
}

// Download a launcher update from an HTTP server on the network, save it to
// /conf/update.uf2, then apply it after confirmation.

void SdUpgradeRun(void)
{
    UpdLedInit();

    if (!DiskAutoMount())
    {
        Msg("SD card not found.", NULL);
        return;
    }

    ScanUpdates();
    if (g_upd_count == 0)
    {
        Msg("No update found.", "Copy an update .uf2 to the SD root.");
        return;
    }

    // Choose which update to apply.
    int sel = PickUpdate();
    if (sel < 0)
    {
        // multiple candidates — let the user pick
        sel = 0;
        int top = 0;
        Bool need_redraw = True;
        while (1)
        {
            if (sel < top) top = sel;
            if (sel >= top + 6) top = sel - 5;
            if (top < 0) top = 0;
            if (need_redraw)
            {
                UpdFrame("update from sd", "A: Select  X: Back");
                for (int i = 0; i < 6; i++)
                {
                    int idx = top + i;
                    if (idx >= g_upd_count) break;
                    int y = UPD_TOP_Y + i * UPD_LINE_H;
                    Bool cur = (idx == sel);
                    u16 color = cur ? g_theme.menu_sel : g_theme.menu_text;
                    if (cur)
                        DrawRect(54, y - 2, WIDTH - 108, FONTH + 4, g_theme.menu_hl);
                    DrawText(cur ? ">" : " ", 30, y, color);
                    DrawText(g_upd_vers[idx], 60, y, color);
                    DrawText(g_upd_names[idx] + 1, 100, y,
                             cur ? color : g_theme.menu_dim);
                }
                DispUpdate();
                need_redraw = False;
            }
            u8 k = KeyGet();
            if (k == NOKEY) { WaitMs(20); continue; }
            if (k == KEY_UP) { if (sel > 0) { sel--; need_redraw = True; } }
            else if (k == KEY_DOWN) { if (sel < g_upd_count - 1) { sel++; need_redraw = True; } }
            else if (k == KEY_A) break;
            else if (k == KEY_X) return;
        }
    }

    // Version check.
    int na, nb, nc;
    Bool parsed = VerParse(g_upd_vers[sel], &na, &nb, &nc);
    int cmp = parsed ? VerCmp(na, nb, nc, LAUNCHER_VERSION_MAJOR,
                              LAUNCHER_VERSION_MINOR, LAUNCHER_VERSION_PATCH)
                     : -1;

    // Warn if not newer, but allow continuing.
    {
        char cur[40];
        strcpy(cur, "current: v"); strcat(cur, LAUNCHER_VERSION_STR);
        char upd[40];
        strcpy(upd, "update:  v"); strcat(upd, g_upd_vers[sel]);

        UpdFrame("update from sd", "A: Update  X: Cancel");
        DrawText(cur, 60, 90, g_theme.menu_text);
        DrawText(upd, 60, 110, g_theme.menu_text);
        if (!parsed)
            DrawText("! cannot read update version", 60, 138, g_theme.menu_dim);
        else if (cmp <= 0)
            DrawText("! version is not newer (A: force)", 60, 138, g_theme.menu_dim);
        else
            DrawText("newer version", 60, 138, g_theme.menu_sel);
        DrawText("reflash the launcher?", 60, 160, g_theme.menu_text);
        DrawText("do NOT power off during update", 60, 180, g_theme.menu_dim);
        DispUpdate();

        while (1)
        {
            u8 k = KeyGet();
            if (k == KEY_X) return;
            if (k == KEY_A) break;
            WaitMs(20);
        }
    }

    // Final stage: stage + apply (never returns on success).
    ApplyUpdate(g_upd_names[sel]);
}
