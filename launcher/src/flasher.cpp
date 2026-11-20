// ****************************************************************************
//
//                  PicoDeck — flasher
//
//   Flow:
//     Phase 1: erase entire game region (0x10000000..0x100E0000)
//     Phase 2: stream UF2 payload to flash in one big batch via FrameBuf
//     Phase 3: post-flash patches (scan whole region for ST7789 + ENC16 sigs)
//     Phase 4: write LUT, CF2, and re-write boot2
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#include "flasher.h"
#include "uf2_parser.h"
#include "arcade_patches.h"

extern "C" void GoToAppRam(void);
extern "C" void RuntimeTerm(void);
extern "C" void KeyWaitNoPressed(void);
extern "C" Bool IsGameValid(void);   // defined in menu.cpp
extern FRAMETYPE ALIGNED FrameBuf[FRAMESIZE];

#define STAGING_BYTES_MAX  (FRAMESIZE * sizeof(FRAMETYPE))
#define SECTOR_SIZE  4096u

// Sector staging buffer — dedicated .bss, can't reuse FrameBuf because the
// flash-progress UI overwrites it.
static u8 g_sector_buf[SECTOR_SIZE];

// Copy of the launcher's live boot2, captured from sector 0 before we erase
// the game region. This is layout-independent: the running boot2 is by
// definition the correct one, so restoring it can never point at a stale
// launcher address (which a hardcoded blob would after a layout change).
static u8 g_boot2_copy[256];

static void CaptureBoot2(void)
{
    memcpy(g_boot2_copy, (const void*)0x10000000u, sizeof(g_boot2_copy));
}

// ---------------------------------------------------------------------------
// Protected regions (= our boot2 + launcher + recovery + settings + CF2)
// ---------------------------------------------------------------------------

typedef struct { u32 start, end; } Region;

static const Region PROTECTED[] = {
    {0x10000000u, 0x10000100u},                 // patched boot2
    {LAUNCHER_FLASH_BASE, LAUNCHER_RECOVERY_BASE},  // launcher image
    {LAUNCHER_RECOVERY_BASE, LAUNCHER_STATE_ADDR},  // recovery image
    {LAUNCHER_STATE_ADDR, LAUNCHER_FLASH_END},      // settings + CF2
};
#define PROTECTED_COUNT (sizeof(PROTECTED) / sizeof(PROTECTED[0]))

Bool FlasherIsProtected(u32 addr, u32 size)
{
    u32 end = addr + size;
    for (u32 i = 0; i < PROTECTED_COUNT; i++) {
        if (end > PROTECTED[i].start && addr < PROTECTED[i].end) return True;
    }
    return False;
}

// ---------------------------------------------------------------------------
// RAM bounds
// ---------------------------------------------------------------------------

#define RAM_BASE  0x20000000u
#define RAM_END   0x20040000u

static Bool IsRamRangeOk(u32 addr, u32 size)
{
    return (addr >= RAM_BASE) && ((u64)addr + size <= RAM_END);
}

// ---------------------------------------------------------------------------
// Progress bar
// ---------------------------------------------------------------------------

static void DrawProgress(const char* title, u32 done, u32 total)
{
    DrawClear();
    int title_x = (WIDTH - (int)strlen(title) * FONTW * 2) / 2;
    DrawText2(title, title_x, 30, COL_YELLOW);

    int bar_x = 30, bar_y = 110, bar_w = WIDTH - 60, bar_h = 18;
    DrawRect(bar_x, bar_y, bar_w, bar_h, COL_GRAY);
    if (total > 0) {
        int fill = (int)((u64)bar_w * done / total);
        if (fill > bar_w) fill = bar_w;
        if (fill > 1) DrawRect(bar_x + 1, bar_y + 1, fill - 1, bar_h - 2, COL_GREEN);
    }

    char pct_buf[8];
    u32 pct = (total > 0) ? (u32)((u64)done * 100 / total) : 0;
    DecNum(pct_buf, pct, 0);
    strcat(pct_buf, "%");
    int pct_x = (WIDTH - (int)strlen(pct_buf) * FONTW) / 2;
    DrawText(pct_buf, pct_x, 140, COL_WHITE);

    DispUpdate();
}

// ---------------------------------------------------------------------------
// LE helper
// ---------------------------------------------------------------------------

static u32 ReadLE32(const u8* p)
{
    return ((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

// ---------------------------------------------------------------------------
// RAM mode
// ---------------------------------------------------------------------------

FlashResult FlasherRunRam(const char* path, const Uf2Info* info)
{
    if (!IsRamRangeOk(info->target_addr, info->total_size_bytes))
        return FLASH_ERR_TOO_BIG;
    if (info->total_size_bytes > STAGING_BYTES_MAX)
        return FLASH_ERR_TOO_BIG;
    if (info->family_id != 0 && info->family_id != FAMILY_RP2040)
        return FLASH_ERR_INVALID;

    DrawClear();
    DrawText2("Loading to RAM", 50, 60, COL_YELLOW);
    DrawText("Reading UF2 from SD...", 60, 110, COL_LTGRAY);
    DispUpdate();

    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) return FLASH_ERR_OPEN;

    u8* staging = (u8*)FrameBuf;
    u32 base = info->target_addr;

    u8 block[UF2_BLOCK_SIZE];
    while (1) {
        u32 n = FileRead(&file, block, UF2_BLOCK_SIZE);
        if (n == 0) break;
        if (n != UF2_BLOCK_SIZE) { FileClose(&file); return FLASH_ERR_INVALID; }
        if (ReadLE32(block + 0) != UF2_MAGIC_START_0) continue;
        if (ReadLE32(block + 4) != UF2_MAGIC_START_1) continue;

        u32 target_addr  = ReadLE32(block + 12);
        u32 payload_size = ReadLE32(block + 16);

        if (payload_size > UF2_PAYLOAD_SIZE) { FileClose(&file); return FLASH_ERR_INVALID; }
        if (Uf2ClassifyAddr(target_addr) != UF2_TARGET_RAM) { FileClose(&file); return FLASH_ERR_INVALID; }
        if (!IsRamRangeOk(target_addr, payload_size)) { FileClose(&file); return FLASH_ERR_TOO_BIG; }
        if (target_addr < base) { FileClose(&file); return FLASH_ERR_INVALID; }

        u32 offset = target_addr - base;
        if (offset + payload_size > STAGING_BYTES_MAX) { FileClose(&file); return FLASH_ERR_TOO_BIG; }

        memcpy(staging + offset, &block[32], payload_size);

        if (KeyGet() == KEY_X) { FileClose(&file); return FLASH_ERR_INTERRUPTED; }
    }
    FileClose(&file);

    KeyWaitNoPressed();
    NativeWifiShutdown();
    RuntimeTerm();

    __asm volatile ("cpsid i" ::: "memory");

    {
        volatile u8* d = (volatile u8*)base;
        const u8* s = staging;
        u32 n = info->total_size_bytes;
        for (u32 i = 0; i < n; i++) d[i] = s[i];
    }
    GoToAppRam();
    while (1) {}
}

// ---------------------------------------------------------------------------
// Flash mode helpers
// ---------------------------------------------------------------------------

// Scan whole game region for `sig`; if found, splice `payload` at that address
// (= read containing sector, splice in RAM, erase, re-write). Different
// MakeCode runtime builds place the patch sites at different flash offsets,
// so a hardcoded address is unreliable.
static Bool FlashPatchScan(const u8* sig, u32 sig_len,
                            const u8* payload, u32 slot_len)
{
    const u8* fbase = (const u8*)0x10000100u;
    u32 fsize = GAME_ERASE_HI - 0x10000100u;
    for (u32 off = 0; off + sig_len <= fsize; off += 1) {
        if (memcmp(fbase + off, sig, sig_len) != 0) continue;
        u32 found = 0x10000100u + off;
        u32 sec = found & ~(SECTOR_SIZE - 1);
        u32 sec_off = found - sec;
        if (sec_off + slot_len > SECTOR_SIZE) return False;  // slot would cross
        memcpy(g_sector_buf, (const void*)sec, SECTOR_SIZE);
        memcpy(g_sector_buf + sec_off, payload, slot_len);
        FlashErase(sec - 0x10000000u, SECTOR_SIZE);
        FlashProgram(sec - 0x10000000u, g_sector_buf, SECTOR_SIZE);
        return True;
    }
    return False;
}

// Swap the physical Y and B buttons in the game's key table (KeyGpioTab).
// Makes the Y button report as B inside MakeCode Arcade games.
static Bool FlashPatchSwapYB(void)
{
    const u8 sig[KEY_NUM] = {
        BTN_UP_PIN, BTN_LEFT_PIN, BTN_RIGHT_PIN, BTN_DOWN_PIN,
        BTN_X_PIN, BTN_Y_PIN, BTN_A_PIN, BTN_B_PIN
    };
    u8 patched[KEY_NUM] = {
        BTN_UP_PIN, BTN_LEFT_PIN, BTN_RIGHT_PIN, BTN_DOWN_PIN,
        BTN_X_PIN, BTN_B_PIN, BTN_A_PIN, BTN_Y_PIN
    };
    return FlashPatchScan(sig, KEY_NUM, patched, KEY_NUM);
}

// Build a 4 KB sector with `data` at offset 0 and `fill` padding, then flash it.
static void FlashSectorAt(u32 sector_addr, const u8* data, u32 len, u8 fill)
{
    memset(g_sector_buf, fill, SECTOR_SIZE);
    memcpy(g_sector_buf, data, len);
    FlashErase(sector_addr - 0x10000000u, SECTOR_SIZE);
    FlashProgram(sector_addr - 0x10000000u, g_sector_buf, SECTOR_SIZE);
}

// Re-write sector 0 with our patched boot2 (preserves the rest of the sector
// from the just-flashed game vector table).
static void RewriteBoot2Sector(void)
{
    memcpy(g_sector_buf, (const void*)0x10000000u, SECTOR_SIZE);
    memcpy(g_sector_buf, g_boot2_copy, sizeof(g_boot2_copy));
    FlashErase(0, SECTOR_SIZE);
    FlashProgram(0, g_sector_buf, SECTOR_SIZE);
}

// ---------------------------------------------------------------------------
// Flash mode — main flow
// ---------------------------------------------------------------------------

FlashResult FlasherWriteFlash(const char* path, const Uf2Info* info,
                              const FlashOptions* opts)
{
    if (info->kind != UF2_TARGET_FLASH) return FLASH_ERR_INVALID;
    // Reject UF2s for other MCUs (= protect against drag-drop of wrong file).
    if (info->family_id != 0 && info->family_id != FAMILY_RP2040) {
        return FLASH_ERR_INVALID;
    }

    // The 256 KB app strip above GAME_ERASE_HI must never be erased by a
    // game/module flash. Reject images that would cross into it.
    if ((u64)0x10000000u + info->total_size_bytes > GAME_ERASE_HI)
        return FLASH_ERR_TOO_BIG;

    // Save the live boot2 before Phase 1 erases sector 0.
    CaptureBoot2();

    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) return FLASH_ERR_OPEN;

    // Core 1 may be running the WiFi module from this region. Park it in RAM
    // before the first erase; a game flash removes the module entirely, so it
    // is never resumed.
    NativeWifiPause();

    // Phase 1: tail-erase only the region we're about to write, rounded up
    // to 64 KB chunks. PicoLibSDK FlashErase picks the 64KB-block flash
    // command for that size — much faster than per-sector erases.
    // Note: LUT (0x10080000) and CF2 (0x100FF000) sectors are erased
    // separately during their own write phase.
    {
        const u32 ERASE_CHUNK = 64u * 1024u;
        u32 erase_size = (info->total_size_bytes + ERASE_CHUNK - 1)
                          & ~(ERASE_CHUNK - 1);
        const u32 max_size = GAME_ERASE_HI - 0x10000000u;
        if (erase_size > max_size) erase_size = max_size;
        for (u32 off = 0; off < erase_size; off += ERASE_CHUNK) {
            FlashErase(off, ERASE_CHUNK);
            DrawProgress("Erasing", off + ERASE_CHUNK, erase_size);
        }
    }

    // Put boot2 back *before* the game. If power is lost during the game
    // write, boot2 is already valid and recovery still boots. (The stream
    // below writes 0xFF over this block, which is a no-op on flash.)
    if (opts->preserve_launcher)
    {
        FlashProgram(0, g_boot2_copy, sizeof(g_boot2_copy));
        if (memcmp((const void*)0x10000000u, g_boot2_copy,
                   sizeof(g_boot2_copy)) != 0)
        {
            FlashProgram(0, g_boot2_copy, sizeof(g_boot2_copy));
            if (memcmp((const void*)0x10000000u, g_boot2_copy,
                       sizeof(g_boot2_copy)) != 0)
            {
                FileClose(&file);
                return FLASH_ERR_WRITE;
            }
        }
    }

    // Phase 2: stream payload to flash in 16 KB batches via FrameBuf.
    // Batch size balances progress granularity (~18 updates for ~290 KB)
    // with FlashProgram throughput. DrawProgress fills FrameBuf with pixels;
    // we then overwrite that RAM with batch payload — display has already
    // DMA'd the previous progress frame, so no visible glitch.
    #define BATCH_BYTES (16u * 1024u)
    u8* batch_buf = (u8*)FrameBuf;
    u32 file_pos = 32;  // payload start of block 0
    u32 cur_target = 0x10000000u;
    u32 total_payload = info->num_blocks * UF2_PAYLOAD_SIZE;
    u32 end_target = 0x10000000u + total_payload;
    u32 done_bytes = 0;

    while (cur_target < end_target) {
        DrawProgress("Flashing", done_bytes, total_payload);

        u32 batch_off = 0;
        u32 batch_start = cur_target;
        while (batch_off < BATCH_BYTES && cur_target < end_target) {
            Bool skip = opts->preserve_launcher
                && (cur_target == 0x10000000u
                    || (cur_target >= LAUNCHER_FLASH_BASE
                        && cur_target < LAUNCHER_FLASH_END));
            if (skip) {
                memset(batch_buf + batch_off, 0xFF, UF2_PAYLOAD_SIZE);
            } else {
                FileSeek(&file, file_pos);
                if (FileRead(&file, batch_buf + batch_off, UF2_PAYLOAD_SIZE)
                    != UF2_PAYLOAD_SIZE) {
                    FileClose(&file);
                    return FLASH_ERR_INVALID;
                }
            }
            batch_off += UF2_PAYLOAD_SIZE;
            cur_target += UF2_PAYLOAD_SIZE;
            file_pos += UF2_BLOCK_SIZE;
        }

        FlashProgram(batch_start - 0x10000000u, batch_buf, batch_off);
        done_bytes += batch_off;
    }
    FileClose(&file);
    DrawProgress("Flashing", total_payload, total_payload);

    // Phase 3: scan-based patches + boot2 rewrite + LUT + CF2.
    Bool st7789_ok = False, palette_ok = False, swap_ok = False;
    if (opts->apply_st7789_init) {
        st7789_ok = FlashPatchScan(PICODECK_ILI9341_SIG, PICODECK_ILI9341_SIG_LEN,
                                    PICODECK_ST7789_INIT, PICODECK_ILI9341_INIT_SLOT);
    }
    if (opts->apply_palette_lut) {
        palette_ok = FlashPatchScan(PICODECK_ENC16_SIG, PICODECK_ENC16_SIG_LEN,
                                     PICODECK_PALETTE_PATCH, PICODECK_ENC16_LOOP_LEN);
    }
    if (opts->swap_yb) {
        swap_ok = FlashPatchSwapYB();
    }
    // boot2 was written first and verified above.

    if (opts->apply_palette_lut) {
        FlashSectorAt(PICODECK_LUT_ADDR, PICODECK_PALETTE_LUT, PICODECK_LUT_LEN, 0xFF);
    }
    if (opts->apply_cf2) {
        PicoDeckBuildCf2(g_sector_buf);
        FlashErase(PICODECK_CF2_ADDR - 0x10000000u, SECTOR_SIZE);
        FlashProgram(PICODECK_CF2_ADDR - 0x10000000u, g_sector_buf, SECTOR_SIZE);
    }

    // Final safety check: never reboot into a broken boot2.
    if (opts->preserve_launcher &&
        memcmp((const void*)0x10000000u, g_boot2_copy,
               sizeof(g_boot2_copy)) != 0)
    {
        FlashProgram(0, g_boot2_copy, sizeof(g_boot2_copy));
        if (memcmp((const void*)0x10000000u, g_boot2_copy,
                   sizeof(g_boot2_copy)) != 0)
            return FLASH_ERR_WRITE;
    }

    // Done screen — auto-reset after a short window so the player can read
    // the patch checklist. Holding B during the window pauses the screen.
    DrawClear();
    DrawText2("Flash done", 80, 14, COL_GREEN);
    int y = 60;
    auto line = [&](const char* label, Bool requested, Bool ok) {
        u16 col;
        const char* prefix;
        if (!requested)   { col = COL_GRAY;  prefix = "[ -] "; }
        else if (ok)      { col = COL_GREEN; prefix = "[OK] "; }
        else              { col = COL_RED;   prefix = "[--] "; }
        char buf[64];
        strcpy(buf, prefix);
        strcat(buf, label);
        DrawText(buf, 30, y, col);
        y += 14;
    };
    line("ST7789 init",     opts->apply_st7789_init, st7789_ok);
    line("Palette stub",    opts->apply_palette_lut, palette_ok);
    line("LUT @ 0x10080000", opts->apply_palette_lut, True);
    line("CF2 @ 0x100FF000", opts->apply_cf2,         True);
    line("Swap Y/B buttons", opts->swap_yb,           swap_ok);
    if (opts->apply_st7789_init && !st7789_ok) {
        DrawText("WARNING: ST7789 sig not matched —", 20, y + 8, COL_YELLOW);
        DrawText("display may stay black.", 20, y + 22, COL_YELLOW);
    }
    DrawText("Restarting in 2s    Hold X to stay",
             (WIDTH - 35*FONTW)/2, 215, COL_LTGRAY);
    DispUpdate();

    // Wait up to 2 s, then auto-reset. If user holds B, stay until release.
    for (int t = 0; t < 100; t++) {
        if (KeyPressed(KEY_X)) {
            while (KeyPressed(KEY_X)) WaitMs(50);
            while (!KeyPressed(KEY_A) && !KeyPressed(KEY_X)) WaitMs(50);
            break;
        }
        WaitMs(20);
    }

    KeyWaitNoPressed();
    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}  // unreachable
}

// ---------------------------------------------------------------------------
// "Flasher trasher" — erase the flashed game
// ---------------------------------------------------------------------------

void FlasherTrashGame(void)
{
    // Nothing to do if no game is flashed.
    if (!IsGameValid())
    {
        DrawClear();
        DrawText2("Flasher trasher", (WIDTH - 15 * FONTW * 2) / 2, 30, COL_RED);
        DrawText("No game flashed.", 20, 95, COL_WHITE);
        DrawText("There is nothing to remove.", 20, 115, COL_LTGRAY);
        DrawText("Press X to return", 20, 205, COL_GRAY);
        DispUpdate();
        while (KeyGet() != KEY_X) WaitMs(50);
        return;
    }

    // Confirmation prompt, showing which game will be removed.
    char name[32];
    Bool have_name = PicoDeckReadName(name, sizeof(name));

    DrawClear();
    DrawText2("Flasher trasher", (WIDTH - 15 * FONTW * 2) / 2, 30, COL_RED);
    if (have_name)
    {
        char buf[48];
        strcpy(buf, "Game: ");
        strcat(buf, name);
        DrawText(buf, 20, 82, COL_GREEN);
    }
    else
    {
        DrawText("Game: (unknown)", 20, 82, COL_GREEN);
    }
    DrawText("Remove the flashed game?", 20, 110, COL_WHITE);
    DrawText("The launcher is kept.", 20, 130, COL_LTGRAY);
    DrawText("This cannot be undone.", 20, 145, COL_LTGRAY);
    DrawText("A: Confirm    X: Cancel", 20, 205, COL_LTGRAY);
    DispUpdate();

    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_X) return;
        if (k == KEY_A) break;
        WaitMs(20);
    }

    // The WiFi module may be running from this region. Stop core 1 before the
    // first erase so it cannot execute erased flash or disturb the flash op.
    NativeWifiShutdown();

    // Sector 0 holds boot2 *and* the game vector table. Never erase it:
    // boot2 is the only path to recovery. Erase from sector 1 upwards.
    {
        // FlashErase/FlashProgram take offsets from XIP_BASE, not absolute
        // addresses. Sector 1 starts at offset 0x1000.
        const u32 start = 0x1000u;
        const u32 end = GAME_ERASE_HI - 0x10000000u;
        u32 total = end - start;
        for (u32 off = start; off < end; )
        {
            u32 chunk = (end - off > 0x10000u) ? 0x10000u : (end - off);
            FlashErase(off, chunk);
            off += chunk;
            DrawProgress("Trashing game", off - start, total);
        }
    }

    // Invalidate the game vector page without erasing it. Flash programming
    // can only clear bits, so zeros wipe SP/entry and IsGameValid() says
    // "no game". boot2 stays untouched.
    memset(g_sector_buf, 0, 0x100);
    FlashProgram(0x100, g_sector_buf, 0x100);

    DrawClear();
    DrawText2("Game removed", (WIDTH - 12 * FONTW * 2) / 2, 90, COL_GREEN);
    DrawText("Restarting...", 100, 130, COL_LTGRAY);
    DispUpdate();
    WaitMs(1200);

    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}  // unreachable
}

// ---------------------------------------------------------------------------
// "Flash backup" — dump the flashed game to a UF2 on the SD card
// ---------------------------------------------------------------------------

static void WriteLE32(u8* p, u32 v)
{
    p[0] = (u8)v;
    p[1] = (u8)(v >> 8);
    p[2] = (u8)(v >> 16);
    p[3] = (u8)(v >> 24);
}

// Find the end (exclusive) of the used game region, rounded up to a 4 KB
// sector. Returns 0 if the region is empty.
static u32 FlashBackupEnd(void)
{
    u32 addr = GAME_ERASE_HI - 256u;
    while (1)
    {
        const u8* p = (const u8*)addr;
        Bool empty = True;
        for (u32 i = 0; i < 256; i++)
        {
            if (p[i] != 0xFF) { empty = False; break; }
        }
        if (!empty)
        {
            u32 end = (addr + SECTOR_SIZE) & ~(SECTOR_SIZE - 1u);
            if (end > GAME_ERASE_HI) end = GAME_ERASE_HI;
            return end;
        }
        if (addr == 0x10000000u) break;
        addr -= 256u;
    }
    return 0;
}

static void FlashBackupMessage(const char* l1, const char* l2)
{
    DrawClear();
    DrawText2("Flash backup", (WIDTH - 12 * FONTW * 2) / 2, 30, COL_YELLOW);
    if (l1) DrawText(l1, 20, 100, COL_WHITE);
    if (l2) DrawText(l2, 20, 120, COL_LTGRAY);
    DrawText("Press X to return", 20, 205, COL_GRAY);
    DispUpdate();
    while (KeyGet() != KEY_X) WaitMs(50);
}

void FlasherBackupRun(void)
{
    if (!IsGameValid())
    {
        FlashBackupMessage("No game flashed.", "There is nothing to back up.");
        return;
    }
    if (!DiskAutoMount())
    {
        FlashBackupMessage("SD card not found.", "Insert a card and try again.");
        return;
    }

    u32 end = FlashBackupEnd();
    if (end <= 0x10000000u)
    {
        FlashBackupMessage("Nothing to back up.", NULL);
        return;
    }

    // Make sure /backups exists (ignore failure — it may already exist).
    DirCreate("/backups");

    // Pick a free /backups/BACKUPnn.UF2 name.
    char path[64];
    int idx = 1;
    for (; idx <= 99; idx++)
    {
        strcpy(path, "/backups/BACKUP");
        if (idx < 10) strcat(path, "0");
        char n[8];
        DecNum(n, idx, 0);
        strcat(path, n);
        strcat(path, ".UF2");

        sFile test;
        FileInit(&test);
        if (!FileOpen(&test, path)) break;   // name is free
        FileClose(&test);
    }
    if (idx > 99)
    {
        FlashBackupMessage("Too many backups.", "Delete some and retry.");
        return;
    }

    sFile out;
    FileInit(&out);
    if (!FileCreate(&out, path))
    {
        FlashBackupMessage("Cannot create file:", path);
        return;
    }

    u32 total_pages = (end - 0x10000000u) / 256u;
    u8 block[UF2_BLOCK_SIZE];

    for (u32 i = 0; i < total_pages; i++)
    {
        u32 target = 0x10000000u + i * 256u;

        WriteLE32(block + 0,  UF2_MAGIC_START_0);
        WriteLE32(block + 4,  UF2_MAGIC_START_1);
        WriteLE32(block + 8,  UF2_FLAG_FAMILY_ID);
        WriteLE32(block + 12, target);
        WriteLE32(block + 16, 256u);
        WriteLE32(block + 20, i);
        WriteLE32(block + 24, total_pages);
        WriteLE32(block + 28, FAMILY_RP2040);

        memcpy(block + 32, (const void*)target, 256);
        memset(block + 32 + 256, 0, 476 - 256);
        WriteLE32(block + 32 + 476, UF2_MAGIC_END);

        if (FileWrite(&out, block, UF2_BLOCK_SIZE) != UF2_BLOCK_SIZE)
        {
            FileClose(&out);
            FlashBackupMessage("SD write failed.", path);
            return;
        }

        if ((i & 0x3F) == 0) DrawProgress("Backing up", i, total_pages);
    }

    FileClose(&out);
    DrawProgress("Backing up", total_pages, total_pages);

    FlashBackupMessage("Backup saved:", path);
}
