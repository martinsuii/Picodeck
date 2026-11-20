// ****************************************************************************
//
//             PicoDeck — USB CDC debug shell
//
//   Connect to PicoPad as a USB CDC serial device (= COMx on Windows,
//   /dev/ttyACMx on Linux/macOS), 115200 8N1. Type 'help' for commands.
//
// ****************************************************************************

#include "../include.h"
#include "native_wifi.h"
#if LAUNCHER_HAS_DEBUG_SHELL
#include "debug_shell.h"
#include "uf2_parser.h"
#include "arcade_patches.h"
#include "flasher.h"

extern "C" void RuntimeTerm(void);
extern FRAMETYPE ALIGNED FrameBuf[FRAMESIZE];
extern "C" u8 __bss_end__[];
extern "C" u8 __StackLimit[];
extern "C" u8 __flash_binary_end[];

// CRC32 (IEEE 802.3, reflected) for the `crc` command.
static u32 crc32_buf(const u8* p, u32 len)
{
    u32 crc = 0xFFFFFFFFu;
    while (len--)
    {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (u32)(-(int)(crc & 1)));
    }
    return ~crc;
}

// ---------------------------------------------------------------------------
// Output helpers (printf goes to USB CDC via PicoLibSDK USE_USB_STDIO)
// ---------------------------------------------------------------------------

static void p_str(const char* s)
{
    while (*s) UsbDevCdcWriteChar(*s++);
}

static void p_hex_byte(u8 b)
{
    char h[3];
    h[0] = (char)((b >> 4) < 10 ? '0' + (b >> 4) : 'a' + (b >> 4) - 10);
    h[1] = (char)((b & 0xF) < 10 ? '0' + (b & 0xF) : 'a' + (b & 0xF) - 10);
    h[2] = 0;
    p_str(h);
}

static void p_hex32(u32 v)
{
    p_str("0x");
    for (int i = 7; i >= 0; i--) {
        u32 nib = (v >> (i * 4)) & 0xF;
        char c = (char)(nib < 10 ? '0' + nib : 'a' + nib - 10);
        UsbDevCdcWriteChar(c);
    }
}

static void p_dec(u32 v)
{
    char buf[12];
    DecNum(buf, v, 0);
    p_str(buf);
}

static void p_eol(void)
{
    p_str("\r\n");
}

// ---------------------------------------------------------------------------
// Command parsing helpers
// ---------------------------------------------------------------------------

// Skip leading whitespace, return pointer
static char* skip_ws(char* s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

// Parse hex number, advance pointer. Returns 0 on no digits.
static u32 parse_hex(char** ps)
{
    char* s = skip_ws(*ps);
    // optional 0x prefix
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    u32 v = 0;
    int digits = 0;
    while (1) {
        char c = *s;
        u32 d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | d;
        s++;
        digits++;
    }
    *ps = s;
    return digits ? v : 0;
}

// Parse decimal number
static u32 parse_dec(char** ps)
{
    char* s = skip_ws(*ps);
    u32 v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
        digits++;
    }
    *ps = s;
    return digits ? v : 0;
}

// Get rest of line (= argument string)
static char* rest_of_line(char* s)
{
    return skip_ws(s);
}

// ---------------------------------------------------------------------------
// Hex dump
// ---------------------------------------------------------------------------

static void hexdump(const u8* data, u32 base, u32 len)
{
    u32 i = 0;
    while (i < len) {
        p_hex32(base + i);
        p_str(":  ");
        u32 n = (len - i < 16) ? (len - i) : 16;
        for (u32 k = 0; k < n; k++) {
            p_hex_byte(data[i + k]);
            p_str(" ");
        }
        // padding if last row not full
        for (u32 k = n; k < 16; k++) p_str("   ");
        p_str(" |");
        for (u32 k = 0; k < n; k++) {
            char c = data[i + k];
            UsbDevCdcWriteChar((c >= 32 && c < 127) ? c : '.');
        }
        p_str("|");
        p_eol();
        i += n;
    }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

static void cmd_help(void)
{
    p_str("Commands (<addr>/<offset>/<len> are HEX unless noted):\r\n");
    p_str("  info                              launcher + game info\r\n");
    p_str("  battery                           battery voltage + temp\r\n");
    p_str("  sys                               CPU / RAM / USB info\r\n");
    p_str("  sd                                SD card info\r\n");
    p_str("  ls <path>                         list directory\r\n");
    p_str("  filesize <path>                   show file size\r\n");
    p_str("  cat <path>                        print a text file\r\n");
    p_str("  mkdir <path>                      create directory\r\n");
    p_str("  rm <path>                         delete file/folder\r\n");
    p_str("  mv <src> <dst>                    rename/move\r\n");
    p_str("  crc <path>                        CRC32 of a file\r\n");
    p_str("  dump <addr> <len>                 hex+ascii memory\r\n");
    p_str("  erase <addr>                      erase 4 KB flash sector\r\n");
    p_str("  gpio <pin> <0|1>                  set a GPIO output\r\n");
    p_str("  readraw <path> <offset> <len>     read raw bytes (max 256)\r\n");
    p_str("  readuf2 <path> <block>            read one UF2 block (decimal)\r\n");
    p_str("  scan <path> <hex bytes>           find sequence in file\r\n");
    p_str("  fscan <hex bytes>                 find sequence in flashed game\r\n");
    p_str("  verify <path>                     check UF2 block_no's match\r\n");
    p_str("  flash <path>                      run full flasher on UF2\r\n");
    p_str("  echo <text>                       print text\r\n");
    p_str("  clear                             clear the terminal\r\n");
    p_str("  bootsel                           reboot to USB MSC bootsel\r\n");
    p_str("  reset                             soft reset\r\n");
    p_str("  exit                              return to menu\r\n");
    p_str("Line editing: arrows, Home/End, Tab-complete, Up/Down history,\r\n");
    p_str("Ctrl+U clear line, Ctrl+C cancel, Ctrl+L clear screen.\r\n");
}

static void cmd_info(void)
{
    p_str("PicoDeck debug shell\r\n");
    p_str("  game region: 0x10000100..");
    p_hex32(LAUNCHER_GAME_HI - 1);
    p_eol();
    p_str("  launcher: ");
    p_hex32(LAUNCHER_FLASH_BASE);
    p_str("..");
    p_hex32((u32)__flash_binary_end);
    p_eol();
    p_str("  recovery: ");
    p_hex32(LAUNCHER_RECOVERY_BASE);
    p_str("..");
    p_hex32(LAUNCHER_STATE_ADDR - 1);
    p_eol();
    p_str("  settings: ");
    p_hex32(LAUNCHER_STATE_ADDR);
    p_eol();
    p_str("  CF2: ");
    p_hex32(LAUNCHER_CF2_ADDR);
    p_eol();
    if (*(const volatile u32*)0x100001C0u == 0x44415050u) {
        p_str("  flashed: PicoLibSDK app/loader (PPAD magic @ 0x100001C0)\r\n");
    } else {
        char name[40];
        if (PicoDeckReadName(name, sizeof(name))) {
            p_str("  game name: ");
            p_str(name);
            p_eol();
        } else {
            p_str("  game name: (not found)\r\n");
        }
    }
}

static void cmd_battery(void)
{
    int mv = LauncherBatMv();
    int pct = (mv - BATTERY_EMPTY_INT) * 100
              / (BATTERY_FULL_INT - BATTERY_EMPTY_INT);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    p_str("Battery: ");
    p_dec((u32)mv);
    p_str(" mV  ");
    p_dec((u32)pct);
    p_str("%\r\n");

    float t = ADC_Temp();
    int t10 = (int)(t * 10.0f + (t >= 0.0f ? 0.5f : -0.5f));
    int fr = t10 % 10;
    if (fr < 0) fr = -fr;
    p_str("Temp: ");
    p_dec((u32)(t10 / 10));
    p_str(".");
    p_dec((u32)fr);
    p_str(" C\r\n");
}

static void cmd_sys(void)
{
    p_str("CPU: ");
    p_dec(ClockGetHz(clk_sys) / 1000000u);
    p_str(" MHz\r\n");

    u32 free_ram = (u32)(__StackLimit - __bss_end__);
    p_str("RAM free: ");
    p_dec(free_ram / 1024u);
    p_str(" KB\r\n");

    p_str("Launcher flash: ");
    p_dec((u32)(__flash_binary_end - (u8*)LAUNCHER_FLASH_BASE) / 1024u);
    p_str(" KB\r\n");

    p_str("USB CDC: ");
    p_str(UsbDevCdcIsMounted() ? "mounted" : "not mounted");
    p_eol();
}

static void cmd_sd(void)
{
    if (!DiskValid())
    {
        p_str("No SD card\r\n");
        return;
    }
    p_str("SD: ");
    p_str(GetSDName());
    p_eol();
    p_str("  size: ");
    p_dec(SDMediaSize() / 2048u);
    p_str(" MB\r\n");
    p_str("  free: ");
    p_dec(DiskFreeKB() / 1024u);
    p_str(" MB\r\n");
}

static void cmd_gpio(char* args)
{
    char* p = args;
    u32 pin = parse_dec(&p);
    u32 val = parse_dec(&p);
    if (pin > 29) { p_str("Usage: gpio <pin 0..29> <0|1>\r\n"); return; }
    GPIO_Init((int)pin);
    GPIO_DirOut((int)pin);
    GPIO_Out((int)pin, (int)val);
    p_str("GPIO");
    p_dec(pin);
    p_str(" = ");
    p_dec(val);
    p_eol();
}

static void cmd_crc(char* args)
{
    char* path = skip_ws(args);
    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) { p_str("Cannot open "); p_str(path); p_eol(); return; }

    u32 size = FileSize(&file);
    u32 crc = 0xFFFFFFFFu;
    u32 total = 0;
    u8 buf[512];
    while (total < size)
    {
        u32 n = FileRead(&file, buf, sizeof(buf));
        if (n == 0) break;
        for (u32 i = 0; i < n; i++)
        {
            crc ^= buf[i];
            for (int k = 0; k < 8; k++)
                crc = (crc >> 1) ^ (0xEDB88320u & (u32)(-(int)(crc & 1)));
        }
        total += n;
    }
    FileClose(&file);

    p_str("CRC32 ");
    p_hex32(~crc);
    p_str("  (");
    p_dec(total);
    p_str(" bytes)\r\n");
}

static void cmd_mkdir(char* args)
{
    char* path = skip_ws(args);
    p_str(DirCreate(path) ? "OK\r\n" : "Failed\r\n");
}

static void cmd_rm(char* args)
{
    char* path = skip_ws(args);
    p_str(FileDelete(path) ? "OK\r\n" : "Failed\r\n");
}

static void cmd_mv(char* args)
{
    char* p = args;
    char* src = skip_ws(p);
    char* sp = src;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    if (!*sp) { p_str("Usage: mv <src> <dst>\r\n"); return; }
    *sp = 0;
    char* dst = skip_ws(sp + 1);
    p_str(FileMove(src, dst) ? "OK\r\n" : "Failed\r\n");
}

static void cmd_cat(char* args)
{
    char* path = skip_ws(args);
    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) { p_str("Cannot open "); p_str(path); p_eol(); return; }

    u8 buf[128];
    u32 n;
    while ((n = FileRead(&file, buf, sizeof(buf))) > 0)
    {
        for (u32 i = 0; i < n; i++)
        {
            char c = (char)buf[i];
            if (c == '\n') p_eol();
            else if (c == '\r') continue;
            else if (c >= 32 && c < 127) UsbDevCdcWriteChar(c);
            else UsbDevCdcWriteChar('.');
        }
    }
    FileClose(&file);
    p_eol();
}

static void cmd_echo(char* args)
{
    p_str(skip_ws(args));
    p_eol();
}

static void cmd_clear(void)
{
    p_str("\x1b[2J\x1b[H");
}

// Scan the flashed game region for a hex byte sequence.
// Usage: fscan EF 03 03 80 02 CF 07   (or concatenated hex)
static void cmd_fscan(char* args)
{
    char* p = args;
    u8 needle[32];
    int needle_len = 0;
    char* h = skip_ws(p);
    while (*h && needle_len < 32)
    {
        while (*h == ' ' || *h == '\t') h++;
        if (!*h) break;
        u32 b = 0;
        int got = 0;
        while (*h && got < 2)
        {
            char c = *h;
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            b = (b << 4) | (u32)d;
            h++;
            got++;
        }
        if (got == 0) break;
        needle[needle_len++] = (u8)b;
    }
    if (needle_len == 0) { p_str("Usage: fscan <hex bytes>\r\n"); return; }

    p_str("Flash scan (");
    p_dec(needle_len);
    p_str(" B):");
    for (int i = 0; i < needle_len; i++) { p_str(" "); p_hex_byte(needle[i]); }
    p_eol();

    const u8* base = (const u8*)0x10000100u;
    u32 size = LAUNCHER_GAME_HI - 0x10000100u;
    int matches = 0;
    for (u32 off = 0; off + (u32)needle_len <= size; off++)
    {
        if (memcmp(base + off, needle, (u32)needle_len) == 0)
        {
            u32 addr = 0x10000100u + off;
            p_str("  match @ "); p_hex32(addr); p_eol();
            hexdump(base + (off & ~0xFu), addr & ~0xFu, 32);
            if (++matches >= 8) { p_str("  (stopping)\r\n"); break; }
        }
    }
    p_str("Done. "); p_dec(matches); p_str(" matches\r\n");
}

static void cmd_dump(char* args)
{
    char* p = args;
    u32 addr = parse_hex(&p);
    u32 len = parse_hex(&p);
    if (len == 0) len = 64;
    if (len > 4096) len = 4096;
    p_str("Dump from ");
    p_hex32(addr);
    p_str(" length ");
    p_dec(len);
    p_eol();
    hexdump((const u8*)addr, addr, len);
}

static void cmd_erase(char* args)
{
    char* p = args;
    u32 addr = parse_hex(&p);
    u32 sec_addr = addr & ~0xFFFu;
    p_str("Erasing sector ");
    p_hex32(sec_addr);
    p_eol();
    NativeWifiPause();
    FlashErase(sec_addr - 0x10000000u, 4096);
    NativeWifiResume();
    p_str("Done\r\n");
}

static void cmd_readuf2(char* args)
{
    char* p = args;
    char* path = skip_ws(p);
    // find space
    char* sp = path;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    if (!*sp) { p_str("Usage: readuf2 <path> <block>\r\n"); return; }
    *sp = 0;
    p = sp + 1;
    u32 block_no = parse_dec(&p);

    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) {
        p_str("Cannot open ");
        p_str(path); p_eol();
        return;
    }
    u8 buf[512];
    FileSeek(&file, block_no * 512);
    u32 n = FileRead(&file, buf, 512);
    FileClose(&file);

    p_str("Read ");
    p_dec(n);
    p_str(" bytes from block ");
    p_dec(block_no);
    p_str(" (file pos ");
    p_hex32(block_no * 512);
    p_str(")\r\n");
    if (n > 0) {
        // Parse UF2 header
        u32 m0 = ((u32)buf[0]) | ((u32)buf[1] << 8) | ((u32)buf[2] << 16) | ((u32)buf[3] << 24);
        u32 ta = ((u32)buf[12]) | ((u32)buf[13] << 8) | ((u32)buf[14] << 16) | ((u32)buf[15] << 24);
        u32 ps = ((u32)buf[16]) | ((u32)buf[17] << 8) | ((u32)buf[18] << 16) | ((u32)buf[19] << 24);
        u32 bn = ((u32)buf[20]) | ((u32)buf[21] << 8) | ((u32)buf[22] << 16) | ((u32)buf[23] << 24);
        p_str("  magic0=");  p_hex32(m0);
        p_str("  target="); p_hex32(ta);
        p_str("  psize=");  p_dec(ps);
        p_str("  block_no="); p_dec(bn);
        p_eol();
        hexdump(buf, 0, (n > 64 ? 64 : n));
    }
}

static void cmd_readraw(char* args)
{
    char* p = args;
    char* path = skip_ws(p);
    char* sp = path;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    if (!*sp) { p_str("Usage: readraw <path> <offset> <len>\r\n"); return; }
    *sp = 0;
    p = sp + 1;
    u32 offset = parse_hex(&p);
    u32 len = parse_hex(&p);
    if (len == 0 || len > 256) len = 64;

    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) {
        p_str("Cannot open ");
        p_str(path); p_eol();
        return;
    }
    u8 buf[256];
    FileSeek(&file, offset);
    u32 n = FileRead(&file, buf, len);
    FileClose(&file);

    p_str("Read ");
    p_dec(n);
    p_str(" bytes from offset ");
    p_hex32(offset);
    p_eol();
    hexdump(buf, offset, n);
}

static void cmd_filesize(char* args)
{
    char* path = skip_ws(args);
    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) {
        p_str("Cannot open ");
        p_str(path); p_eol();
        return;
    }
    u32 size = FileSize(&file);
    FileClose(&file);
    p_str("File size: ");
    p_dec(size);
    p_str(" (");
    p_hex32(size);
    p_str(")\r\n");
}

static void cmd_ls(char* args)
{
    char* path = skip_ws(args);
    if (path[0] == 0) path = (char*)"/";
    sFile find;
    sFileInfo info;
    FileInit(&find);
    if (!FindOpen(&find, path)) {
        p_str("Cannot open ");
        p_str(path); p_eol();
        return;
    }
    p_str("Listing ");
    p_str(path);
    p_eol();
    int count = 0;
    while (FindNext(&find, &info, ATTR_DIR_MASK, "*")) {
        if (info.namelen == 0) continue;
        if (info.name[0] == '.') continue;
        if (info.attr & ATTR_DIR) p_str("  [D] ");
        else                       p_str("      ");
        for (int i = 0; i < info.namelen; i++) UsbDevCdcWriteChar(info.name[i]);
        if (!(info.attr & ATTR_DIR)) {
            p_str("\t");
            p_dec(info.size);
        }
        p_eol();
        count++;
    }
    FindClose(&find);
    p_str("Total: ");
    p_dec(count);
    p_str(" entries\r\n");
}

static void cmd_verify(char* args)
{
    char* path = skip_ws(args);
    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) {
        p_str("Cannot open ");
        p_str(path); p_eol();
        return;
    }
    u32 file_size = FileSize(&file);
    u32 expected_blocks = file_size / 512;
    p_str("Verifying ");
    p_str(path);
    p_str(" (");
    p_dec(expected_blocks);
    p_str(" blocks)...\r\n");

    u8 block[512];
    u32 mismatches = 0;
    for (u32 i = 0; i < expected_blocks; i++) {
        FileSeek(&file, i * 512);
        u32 n = FileRead(&file, block, 512);
        if (n != 512) {
            p_str("  ! Read failed at block ");
            p_dec(i); p_eol();
            break;
        }
        u32 m0 = ((u32)block[0]) | ((u32)block[1] << 8) | ((u32)block[2] << 16) | ((u32)block[3] << 24);
        if (m0 != 0x0A324655) continue;
        u32 bn = ((u32)block[20]) | ((u32)block[21] << 8) | ((u32)block[22] << 16) | ((u32)block[23] << 24);
        if (bn != i) {
            p_str("  MISMATCH at file pos block ");
            p_dec(i);
            p_str(": got block_no ");
            p_dec(bn);
            u32 ta = ((u32)block[12]) | ((u32)block[13] << 8) | ((u32)block[14] << 16) | ((u32)block[15] << 24);
            p_str(" target ");
            p_hex32(ta);
            p_eol();
            // dump first 32 bytes for inspection
            hexdump(block, i * 512, 32);
            mismatches++;
            if (mismatches > 5) {
                p_str("  ... (more mismatches, stopping)\r\n");
                break;
            }
        }
        // Heartbeat every 100 blocks
        if ((i % 100) == 0) {
            p_str("  block "); p_dec(i); p_str(" ok\r\n");
        }
    }
    FileClose(&file);
    p_str("Verify complete. Mismatches: ");
    p_dec(mismatches);
    p_eol();
}

// Scan a file for a hex byte sequence, print all match positions.
// Usage:  scan <path> <hex bytes>
//   e.g.  scan /arcade/falling.uf2 EF 03 03 80 02 CF 03
//   or    scan /arcade/falling.uf2 EF03038002CF03
static void cmd_scan(char* args)
{
    char* p = args;
    char* path = skip_ws(p);
    char* sp = path;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;
    if (!*sp) { p_str("Usage: scan <path> <hex>\r\n"); return; }
    *sp = 0;
    p = sp + 1;

    // Parse needle (= sequence of hex bytes; whitespace optional)
    u8 needle[32];
    int needle_len = 0;
    char* h = skip_ws(p);
    while (*h && needle_len < 32) {
        // Skip whitespace between bytes
        while (*h == ' ' || *h == '\t') h++;
        if (!*h) break;
        u32 b = 0;
        int got = 0;
        while (*h && got < 2) {
            char c = *h;
            int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            b = (b << 4) | d;
            h++;
            got++;
        }
        if (got == 0) break;
        needle[needle_len++] = (u8)b;
    }
    if (needle_len == 0) { p_str("No hex bytes\r\n"); return; }

    p_str("Needle (");
    p_dec(needle_len);
    p_str(" B):");
    for (int i = 0; i < needle_len; i++) { p_str(" "); p_hex_byte(needle[i]); }
    p_eol();

    sFile file;
    FileInit(&file);
    if (!FileOpen(&file, path)) { p_str("Cannot open\r\n"); return; }
    u32 file_size = FileSize(&file);

    // Streaming scan with overlap so a needle on chunk boundary still matches
    static u8 chunk[4096 + 32];
    u32 base = 0;
    int matches = 0;
    int prev_tail = 0;

    while (base < file_size) {
        u32 to_read = (file_size - base < 4096) ? file_size - base : 4096;
        FileSeek(&file, base);
        u32 n = FileRead(&file, chunk + prev_tail, to_read);
        if (n == 0) break;
        u32 chunk_len = prev_tail + n;

        for (u32 i = 0; i + needle_len <= chunk_len; i++) {
            if (memcmp(chunk + i, needle, needle_len) == 0) {
                u32 abs_pos = (base - prev_tail) + i;
                p_str("  match @ ");
                p_hex32(abs_pos);
                // Translate to flash addr if mod 512 == [32..287] (= payload)
                u32 in_block = abs_pos & 511;
                if (in_block >= 32 && in_block < 288) {
                    u32 block_no = abs_pos >> 9;
                    u32 flash = 0x10000000u + block_no * 256u + (in_block - 32);
                    p_str("  -> flash ");
                    p_hex32(flash);
                }
                p_eol();
                matches++;
                if (matches >= 16) {
                    p_str("  (16+ matches, stopping)\r\n");
                    FileClose(&file);
                    return;
                }
            }
        }

        prev_tail = needle_len - 1;
        if (chunk_len > (u32)prev_tail) {
            memmove(chunk, chunk + chunk_len - prev_tail, prev_tail);
        }
        base += n;
        // Heartbeat every 64 KB
        if ((base & 0xFFFF) == 0) {
            p_str("  ... ");
            p_hex32(base);
            p_eol();
        }
    }
    FileClose(&file);
    p_str("Done. ");
    p_dec(matches);
    p_str(" matches\r\n");
}

// Run the full flasher (= same flow as menu LOAD → flash). Performs
// erase + sector-batched flash + scan-based patches + watchdog reset.
static void cmd_flash(char* args)
{
    char* path = skip_ws(args);
    if (!*path) { p_str("Usage: flash <path>\r\n"); return; }

    Uf2Info info;
    if (!Uf2ReadInfo(path, &info)) {
        p_str("Cannot parse UF2\r\n");
        return;
    }
    p_str("UF2: target="); p_hex32(info.target_addr);
    p_str(" blocks=");     p_dec(info.num_blocks);
    p_str(" size=");       p_dec(info.total_size_bytes);
    p_eol();

    if (info.kind != UF2_TARGET_FLASH) {
        p_str("Not a FLASH UF2 — refused\r\n");
        return;
    }

    FlashOptions opts;
    opts.preserve_launcher = True;
    opts.apply_st7789_init = True;
    opts.apply_palette_lut = True;
    opts.apply_cf2 = True;
    opts.swap_yb = False;

    p_str("Flashing... (resets on done; press A on device to confirm)\r\n");
    FlashResult r = FlasherWriteFlash(path, &info, &opts);
    p_str("Flash result code: ");
    p_dec((u32)r);
    p_eol();
}

static void cmd_bootsel(void)
{
    p_str("Rebooting to BOOTSEL...\r\n");
    WaitMs(50);
    reset_usb_boot(0, 0);
    while (1) {}
}

static void cmd_reset(void)
{
    p_str("Resetting...\r\n");
    WaitMs(50);
    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}
}

// ---------------------------------------------------------------------------
// Shell main loop
// ---------------------------------------------------------------------------

#define LINE_MAX 128

static void process_line(char* line)
{
    char* p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == 0) return;

    // Find first word (= command)
    char* cmd = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    char saved = *p;
    *p = 0;
    char* args = saved ? p + 1 : p;

    #define EQ(s) (StrComp(cmd, (s)) == 0)
    if (EQ("help") || EQ("?")) cmd_help();
    else if (EQ("info")) cmd_info();
    else if (EQ("battery") || EQ("bat")) cmd_battery();
    else if (EQ("sys")) cmd_sys();
    else if (EQ("sd")) cmd_sd();
    else if (EQ("dump")) cmd_dump(args);
    else if (EQ("erase")) cmd_erase(args);
    else if (EQ("gpio")) cmd_gpio(args);
    else if (EQ("crc")) cmd_crc(args);
    else if (EQ("readuf2")) cmd_readuf2(args);
    else if (EQ("readraw")) cmd_readraw(args);
    else if (EQ("filesize")) cmd_filesize(args);
    else if (EQ("ls") || EQ("dir")) cmd_ls(args);
    else if (EQ("cat")) cmd_cat(args);
    else if (EQ("mkdir")) cmd_mkdir(args);
    else if (EQ("rm")) cmd_rm(args);
    else if (EQ("mv")) cmd_mv(args);
    else if (EQ("verify")) cmd_verify(args);
    else if (EQ("scan")) cmd_scan(args);
    else if (EQ("fscan")) cmd_fscan(args);
    else if (EQ("flash")) cmd_flash(args);
    else if (EQ("echo")) cmd_echo(args);
    else if (EQ("clear") || EQ("cls")) cmd_clear();
    else if (EQ("bootsel")) cmd_bootsel();
    else if (EQ("reset")) cmd_reset();
    else if (EQ("exit") || EQ("quit")) {
        extern Bool g_shell_exit;
        g_shell_exit = True;
    }
    else {
        p_str("Unknown command: ");
        p_str(cmd); p_eol();
        p_str("Try 'help'\r\n");
    }
    #undef EQ
}

// ---------------------------------------------------------------------------
// Line editing / history / tab completion
// ---------------------------------------------------------------------------

#define HIST_MAX 8
static char g_hist[HIST_MAX][LINE_MAX];
static int  g_hist_n = 0;
static int  g_hist_idx = 0;
static char g_hist_cur[LINE_MAX];

static const char* const ShellCmds[] = {
    "help", "info", "battery", "sys", "sd", "ls", "filesize", "cat",
    "mkdir", "rm", "mv", "crc", "dump", "fscan", "erase", "gpio", "readraw",
    "readuf2", "scan", "verify", "flash", "echo", "clear", "bootsel",
    "reset", "exit"
};
#define SHELL_CMD_N ((int)(sizeof(ShellCmds) / sizeof(ShellCmds[0])))

// Redraw the prompt + line and leave the cursor at `pos`.
static void shell_redraw(const char* line, int len, int pos)
{
    p_str("\r> ");
    for (int i = 0; i < len; i++) UsbDevCdcWriteChar(line[i]);
    p_str("\x1b[K");                       // erase to end of line
    for (int i = len; i > pos; i--) p_str("\b");
}

static int shell_read_timeout(u32 ms)
{
    u32 start = Millis();
    while (UsbDevCdcReadReady() == 0)
    {
        if ((u32)(Millis() - start) > ms) return -1;
        WaitMs(1);
    }
    return (u8)UsbDevCdcReadChar();
}

static void shell_hist_add(const char* line)
{
    if (line[0] == 0) return;
    if (g_hist_n > 0 && StrComp(g_hist[g_hist_n - 1], line) == 0) return;

    if (g_hist_n < HIST_MAX)
    {
        strcpy(g_hist[g_hist_n], line);
        g_hist_n++;
    }
    else
    {
        for (int i = 1; i < HIST_MAX; i++) strcpy(g_hist[i - 1], g_hist[i]);
        strcpy(g_hist[HIST_MAX - 1], line);
    }
    g_hist_idx = g_hist_n;
}

static void shell_tab(char* line, int* plen, int* ppos)
{
    int len = *plen;
    if (len == 0) return;

    // Only complete the first word.
    int i;
    for (i = 0; i < len && line[i] != ' '; i++) {}
    if (i < len) return;

    int matches = 0, last = -1;
    for (int c = 0; c < SHELL_CMD_N; c++)
    {
        const char* cmd = ShellCmds[c];
        int k = 0;
        while (k < len && cmd[k] && cmd[k] == line[k]) k++;
        if (k == len) { matches++; last = c; }
    }

    if (matches == 1)
    {
        strcpy(line, ShellCmds[last]);
        *plen = (int)StrLen(line);
        line[*plen] = ' ';
        (*plen)++;
        line[*plen] = 0;
        *ppos = *plen;
        shell_redraw(line, *plen, *ppos);
    }
    else if (matches > 1)
    {
        p_eol();
        for (int c = 0; c < SHELL_CMD_N; c++)
        {
            const char* cmd = ShellCmds[c];
            int k = 0;
            while (k < len && cmd[k] && cmd[k] == line[k]) k++;
            if (k == len) { p_str(cmd); p_str("  "); }
        }
        p_eol();
        shell_redraw(line, *plen, *ppos);
    }
}

Bool g_shell_exit = False;

void DebugShellRun(void)
{
    g_shell_exit = False;

    // Initialize USB device with CDC class — without this the USB stack
    // never enumerates and host doesn't see a serial port.
    // (RuntimeInit only auto-inits this when USE_USB_STDIO=1, which we
    //  disable to allow direct UsbDevCdc* calls without printf wrappers.)
    UsbDevInit(&UsbDevCdcSetupDesc);

    // Show on display (raw / monochrome)
    DrawClear();
    DrawText("debug shell", 8, 8, COL_WHITE);
    DrawText("USB serial 115200 8N1", 8, 22, COL_GRAY);
    DrawRect(8, 34, WIDTH - 16, 1, COL_GRAY);
    DrawText("waiting for host...", 8, 48, COL_WHITE);
    DrawText("x: abort", 8, HEIGHT - 14, COL_GRAY);
    DispUpdate();

    // Mount SD if not already
    DiskAutoMount();

    // Wait for host enumeration + DTR (terminal opens the port).
    // Show progress dots and allow X to bail out.
    {
        int dots = 0;
        while (!UsbDevCdcIsMounted()) {
            if (KeyGet() == KEY_X) return;
            WaitMs(100);
            if ((++dots % 5) == 0) {
                DrawTextBg(" ", 8 + (dots/5)*8 % 200, 64, COL_WHITE, COL_BLACK);
                DispUpdate();
            }
        }
    }

    DrawTextBg("connected                ", 8, 48, COL_WHITE, COL_BLACK);
    DispUpdate();

    char line[LINE_MAX];
    int len = 0, pos = 0;
    g_hist_n = 0;
    g_hist_idx = 0;

    // Drain any garbage chars sent during enumeration
    while (UsbDevCdcReadReady() > 0) UsbDevCdcReadChar();

    p_str("\r\n--- PicoDeck debug shell ---\r\n");
    p_str("Type 'help' for commands.\r\n> ");

    while (!g_shell_exit)
    {
        // Allow X button to exit
        if (KeyGet() == KEY_X) break;

        while (UsbDevCdcReadReady() > 0)
        {
            char c = (char)(u8)UsbDevCdcReadChar();

            if (c == '\r' || c == '\n')
            {
                line[len] = 0;
                p_eol();
                if (len > 0)
                {
                    shell_hist_add(line);
                    process_line(line);
                }
                len = 0; pos = 0;
                g_hist_idx = g_hist_n;
                p_str("> ");
            }
            else if (c == 0x7F || c == 0x08)          // backspace
            {
                if (pos > 0)
                {
                    for (int i = pos - 1; i < len - 1; i++) line[i] = line[i + 1];
                    len--; pos--;
                    shell_redraw(line, len, pos);
                }
            }
            else if (c == '\t')                        // tab completion
            {
                shell_tab(line, &len, &pos);
            }
            else if (c == 0x1B)                        // escape sequence
            {
                int c1 = shell_read_timeout(20);
                int c2 = shell_read_timeout(20);
                if (c1 == '[')
                {
                    if (c2 == 'A')                     // up: history
                    {
                        if (g_hist_n > 0 && g_hist_idx > 0)
                        {
                            if (g_hist_idx == g_hist_n) strcpy(g_hist_cur, line);
                            g_hist_idx--;
                            strcpy(line, g_hist[g_hist_idx]);
                            len = pos = (int)StrLen(line);
                            shell_redraw(line, len, pos);
                        }
                    }
                    else if (c2 == 'B')                // down: history
                    {
                        if (g_hist_idx < g_hist_n)
                        {
                            g_hist_idx++;
                            if (g_hist_idx == g_hist_n) strcpy(line, g_hist_cur);
                            else strcpy(line, g_hist[g_hist_idx]);
                            len = pos = (int)StrLen(line);
                            shell_redraw(line, len, pos);
                        }
                    }
                    else if (c2 == 'C') { if (pos < len) { UsbDevCdcWriteChar(line[pos]); pos++; } }
                    else if (c2 == 'D') { if (pos > 0) { pos--; p_str("\b"); } }
                    else if (c2 == 'H') { while (pos > 0) { pos--; p_str("\b"); } }
                    else if (c2 == 'F') { while (pos < len) { UsbDevCdcWriteChar(line[pos]); pos++; } }
                }
            }
            else if (c == 0x03)                        // Ctrl+C
            {
                p_str("^C"); p_eol();
                len = 0; pos = 0;
                p_str("> ");
            }
            else if (c == 0x15)                        // Ctrl+U
            {
                len = 0; pos = 0;
                shell_redraw(line, len, pos);
            }
            else if (c == 0x0C)                        // Ctrl+L
            {
                cmd_clear();
                shell_redraw(line, len, pos);
            }
            else if (c >= 32 && c < 127 && len < LINE_MAX - 1)
            {
                for (int i = len; i > pos; i--) line[i] = line[i - 1];
                line[pos] = c;
                len++; pos++;
                shell_redraw(line, len, pos);
            }
        }

        WaitMs(10);
    }

    p_str("\r\nResetting...\r\n");
    WaitMs(100);

    // Watchdog reset for clean USB state — same as MSC screen.
    // After reset, scratch[0] is not LAUNCHER_BOOT_MAGIC → menu shown.
    RuntimeTerm();
    WatchdogSetup(0, False);
    while (1) {}
}

#endif // LAUNCHER_HAS_DEBUG_SHELL
