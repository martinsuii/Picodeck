// ****************************************************************************
//
//                  PicoDeck — QR code generator
//
//   Minimal QR encoder: byte mode, error correction level L, versions 1..5
//   (single RS block), fixed mask 0. Good enough for short "WIFI:..." strings.
//   Untested against a reference decoder.
//
// ****************************************************************************

#include "../include.h"
#include "qrcode.h"

#define QR_VMAX 5

static const u8 g_qr_total[QR_VMAX + 1] = { 0, 26, 44, 70, 100, 134 };
static const u8 g_qr_ecc[QR_VMAX + 1]   = { 0,  7, 10, 15,  20,  26 };

static u8  g_matrix[QR_MAX_SIZE * QR_MAX_SIZE];
static u8  g_bits[1088];
static u8  g_data[128];
static u8  g_ecc[64];
static u8  g_gen[64];
static u8  g_codewords[200];
static int g_bitlen;
static int g_size;
static int g_total_cw;

// ---------------------------------------------------------------------------
// GF(256) — computed on the fly (no 768-byte tables)
// ---------------------------------------------------------------------------

static u8 GfMul(u8 a, u8 b)
{
    u8 r = 0;
    for (int i = 0; i < 8; i++)
    {
        if (b & 1) r ^= a;
        b >>= 1;
        u8 hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1D;
    }
    return r;
}

static u8 GfPow(int n)
{
    u8 r = 1;
    for (int i = 0; i < n; i++) r = GfMul(r, 2);
    return r;
}

static void RsEncode(const u8* data, int data_len, u8* ecc, int ecc_len)
{
    g_gen[0] = 1;
    int glen = 1;
    for (int i = 0; i < ecc_len; i++)
    {
        u8 a = GfPow(i);
        for (int j = glen; j > 0; j--)
            g_gen[j] = (u8)(g_gen[j - 1] ^ GfMul(g_gen[j], a));
        g_gen[0] = GfMul(g_gen[0], a);
        glen++;
    }

    for (int i = 0; i < ecc_len; i++) ecc[i] = 0;
    for (int i = 0; i < data_len; i++)
    {
        u8 factor = (u8)(data[i] ^ ecc[0]);
        for (int j = 0; j < ecc_len - 1; j++)
            ecc[j] = (u8)(ecc[j + 1] ^ GfMul(g_gen[ecc_len - 1 - j], factor));
        ecc[ecc_len - 1] = GfMul(g_gen[0], factor);
    }
}

// ---------------------------------------------------------------------------
// Bit buffer
// ---------------------------------------------------------------------------

static void BitsAppend(u32 value, int count)
{
    for (int i = count - 1; i >= 0; i--)
        if (g_bitlen < (int)sizeof(g_bits))
            g_bits[g_bitlen++] = (u8)((value >> i) & 1);
}

// ---------------------------------------------------------------------------
// Matrix helpers
// ---------------------------------------------------------------------------

static void MSet(int r, int c, u8 v)
{
    if (r >= 0 && r < g_size && c >= 0 && c < g_size)
        g_matrix[r * g_size + c] = v;
}

static void PlaceFinder(int r, int c)
{
    for (int dr = -1; dr <= 7; dr++)
        for (int dc = -1; dc <= 7; dc++)
        {
            int rr = r + dr, cc = c + dc;
            if (rr < 0 || rr >= g_size || cc < 0 || cc >= g_size) continue;
            int in = (dr >= 0 && dr <= 6 && dc >= 0 && dc <= 6);
            int ring = (dr == 0 || dr == 6 || dc == 0 || dc == 6);
            int core = (dr >= 2 && dr <= 4 && dc >= 2 && dc <= 4);
            MSet(rr, cc, (u8)(in && (ring || core)));
        }
}

static void PlaceAlignment(int r, int c)
{
    for (int dr = -2; dr <= 2; dr++)
        for (int dc = -2; dc <= 2; dc++)
        {
            int ring = (dr == -2 || dr == 2 || dc == -2 || dc == 2);
            MSet(r + dr, c + dc, (u8)((dr == 0 && dc == 0) || ring));
        }
}

// ---------------------------------------------------------------------------
// Format info (15 bits, BCH) — mask 0 only
// ---------------------------------------------------------------------------

static u16 FormatInfo(void)
{
    u32 data = 0x01u << 3;            // ECC level L, mask 0
    u32 bch = data << 10;
    u32 gen = 0x537;
    for (int i = 14; i >= 10; i--)
        if (bch & (1u << i)) bch ^= gen << (i - 10);
    return (u16)(((data << 10) | bch) ^ 0x5412);
}

static void PlaceFormat(void)
{
    u16 fi = FormatInfo();
    int s = g_size;
    for (int i = 0; i < 6; i++) MSet(i, 8, (u8)((fi >> i) & 1));
    MSet(7, 8, (u8)((fi >> 6) & 1));
    MSet(8, 8, (u8)((fi >> 7) & 1));
    MSet(8, 7, (u8)((fi >> 8) & 1));
    for (int i = 9; i < 15; i++) MSet(8, 14 - i, (u8)((fi >> i) & 1));
    for (int i = 0; i < 8; i++) MSet(s - 1 - i, 8, (u8)((fi >> i) & 1));
    for (int i = 8; i < 15; i++) MSet(8, s - 15 + i, (u8)((fi >> i) & 1));
}

// ---------------------------------------------------------------------------
// Build function patterns
// ---------------------------------------------------------------------------

static void BuildFunctionPatterns(void)
{
    for (int i = 0; i < g_size * g_size; i++) g_matrix[i] = 2;  // 2 = unset

    PlaceFinder(0, 0);
    PlaceFinder(0, g_size - 7);
    PlaceFinder(g_size - 7, 0);

    for (int i = 8; i < g_size - 8; i++)
    {
        MSet(6, i, (u8)((i & 1) == 0));
        MSet(i, 6, (u8)((i & 1) == 0));
    }

    if (g_size >= 25)
        PlaceAlignment(g_size - 7, g_size - 7);

    // reserve format areas
    for (int i = 0; i < 9; i++) { MSet(i, 8, 0); MSet(8, i, 0); }
    for (int i = 0; i < 8; i++) MSet(g_size - 1 - i, 8, 0);
    for (int i = 0; i < 8; i++) MSet(8, g_size - 8 + i, 0);
}

// ---------------------------------------------------------------------------
// Data placement (zigzag) + mask 0
// ---------------------------------------------------------------------------

static void PlaceData(void)
{
    int total_cw = g_total_cw;
    int bit = 0;
    int col = g_size - 1;

    while (col > 0)
    {
        if (col == 6) col--;
        for (int pass = 0; pass < 2; pass++)
        {
            int row = g_size - 1;
            while (row >= 0)
            {
                for (int c = 0; c < 2; c++)
                {
                    int cc = col - c;
                    if (g_matrix[row * g_size + cc] == 2)
                    {
                        u8 v = 0;
                        if (bit < total_cw * 8)
                        {
                            int cw = bit / 8, b = bit % 8;
                            v = (u8)((g_codewords[cw] >> (7 - b)) & 1);
                        }
                        // mask 0: flip if (row+cc) even
                        if ((row + cc) % 2 == 0) v ^= 1;
                        MSet(row, cc, v);
                        bit++;
                    }
                }
                row -= 2;
            }
        }
        col -= 2;
    }
}

// ---------------------------------------------------------------------------
// Encode
// ---------------------------------------------------------------------------

Bool QrEncode(const char* text, const u8** matrix, int* size)
{
    int len = (int)StrLen(text);

    int version = 0;
    for (int v = 1; v <= QR_VMAX; v++)
    {
        int data_cw = g_qr_total[v] - g_qr_ecc[v];
        if (8 * len + 12 <= data_cw * 8) { version = v; break; }
    }
    if (version == 0) return False;

    int data_cw = g_qr_total[version] - g_qr_ecc[version];
    int ecc_cw  = g_qr_ecc[version];
    g_total_cw = g_qr_total[version];
    g_size = 17 + 4 * version;

    // Data bit stream
    g_bitlen = 0;
    BitsAppend(0x4, 4);
    BitsAppend((u32)len, 8);
    for (int i = 0; i < len; i++) BitsAppend((u32)(u8)text[i], 8);

    int capacity_bits = data_cw * 8;
    if (g_bitlen < capacity_bits)
        BitsAppend(0, (capacity_bits - g_bitlen < 4) ? (capacity_bits - g_bitlen) : 4);
    while (g_bitlen % 8 != 0) BitsAppend(0, 1);

    u8 pad[2] = { 0xEC, 0x11 };
    int pi = 0;
    while (g_bitlen < capacity_bits) { BitsAppend(pad[pi], 8); pi ^= 1; }

    for (int i = 0; i < data_cw; i++)
    {
        u8 b = 0;
        for (int j = 0; j < 8; j++) b = (u8)((b << 1) | g_bits[i * 8 + j]);
        g_data[i] = b;
    }

    RsEncode(g_data, data_cw, g_ecc, ecc_cw);
    for (int i = 0; i < data_cw; i++) g_codewords[i] = g_data[i];
    for (int i = 0; i < ecc_cw; i++)  g_codewords[data_cw + i] = g_ecc[i];

    BuildFunctionPatterns();
    PlaceData();
    PlaceFormat();
    MSet(8, g_size - 8, 1);   // dark module

    *matrix = g_matrix;
    *size = g_size;
    return True;
}
