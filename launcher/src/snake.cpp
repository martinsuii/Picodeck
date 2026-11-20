// ****************************************************************************
//
//                  PicoDeck — fun apps: Snake
//
//   Classic snake game. Steer with the D-pad, eat apples, don't hit the
//   walls or your own tail. Speed ramps up with the score.
//
//     Arrows  turn
//     A       pause / resume
//     X       exit
//
//   The playfield is redrawn incrementally (erase tail, draw head), so a
//   tick only pushes a couple of small rectangles to the display.
//
// ****************************************************************************

#include "../include.h"
#include "snake.h"
#include "theme.h"

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------

#define CELL          8
#define COLS          40
#define ROWS          24
#define FIELD_Y       28
#define FIELD_H       (ROWS * CELL)
#define MAX_LEN       (COLS * ROWS)

#define FIELD_BG      RGBTO16(6, 20, 10)
#define FIELD_EDGE    RGBTO16(30, 90, 50)
#define SNAKE_BODY    RGBTO16(90, 220, 110)
#define SNAKE_HEAD    RGBTO16(200, 255, 210)
#define FOOD_COL      RGBTO16(240, 70, 60)
#define OVERLAY_BG    RGBTO16(8, 10, 8)

// Direction codes: 0=up, 1=down, 2=left, 3=right. Opposite = dir ^ 1.
static const s8 DirDx[4] = {  0,  0, -1,  1 };
static const s8 DirDy[4] = { -1,  1,  0,  0 };

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static u16  g_body[MAX_LEN];   // packed x | (y << 8), g_body[0] = tail
static int  g_len;
static int  g_dir;             // current direction
static u8   g_turn[2];         // queued turns (max 2, for fast cornering)
static int  g_turn_n;
static int  g_food_x, g_food_y;
static u32  g_rng;
static int  g_score;
static int  g_hi;              // best score this session

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline int CellX(u16 c) { return c & 0xFF; }
static inline int CellY(u16 c) { return (int)(c >> 8); }
static inline u16 MakeCell(int x, int y) { return (u16)(x | (y << 8)); }

static u32 RandNext(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

static void DrawCell(int x, int y, COLTYPE col)
{
    DrawRect(x * CELL + 1, FIELD_Y + y * CELL + 1, CELL - 2, CELL - 2, col);
}

static void DrawFood(void)
{
    DrawCell(g_food_x, g_food_y, FOOD_COL);
    DrawRect(g_food_x * CELL + 3, FIELD_Y + g_food_y * CELL + 3, 2, 2,
             COL_LTRED);
}

static void DrawHeader(void)
{
    DrawText2("SNAKE", 8, 6, g_theme.menu_title);

    char num[16];
    char buf[32];
    strcpy(buf, "SCORE ");
    DecNum(num, (s32)g_score, 0);
    strcat(buf, num);
    if (g_hi > g_score)
    {
        strcat(buf, "  BEST ");
        DecNum(num, (s32)g_hi, 0);
        strcat(buf, num);
    }

    // Clear the score area first — the text grows and DrawText doesn't paint
    // a background, so stale glyphs from the old score could linger.
    DrawRect(100, 2, WIDTH - 100, FONTH + 6, g_theme.menu_bg);
    DrawText(buf, WIDTH - 8 - (int)StrLen(buf) * FONTW, 6, g_theme.menu_text);
}

static void DrawField(void)
{
    DrawClearCol(g_theme.menu_bg);
    DrawHeader();

    DrawRect(0, FIELD_Y - 1, WIDTH, 1, FIELD_EDGE);
    DrawRect(0, FIELD_Y, WIDTH, FIELD_H, FIELD_BG);
    DrawRect(0, FIELD_Y + FIELD_H, WIDTH, 1, FIELD_EDGE);

    const char* hint = "Arrows: turn   A: pause   X: exit";
    DrawText(hint, (WIDTH - (int)StrLen(hint) * FONTW) / 2,
             FIELD_Y + FIELD_H + 7, g_theme.menu_dim);
}

static void DrawSnake(void)
{
    for (int i = 0; i < g_len; i++)
    {
        u16 c = g_body[i];
        DrawCell(CellX(c), CellY(c),
                 (i == g_len - 1) ? SNAKE_HEAD : SNAKE_BODY);
    }
    DrawFood();
}

// Draw a centred box over the playfield. l2 may be NULL.
static void DrawOverlay(const char* title, const char* l1, const char* l2,
                        COLTYPE title_col)
{
    int w = 250;
    int h = l2 ? 92 : 74;
    int x = (WIDTH - w) / 2;
    int y = (HEIGHT - h) / 2;

    DrawRect(x, y, w, h, OVERLAY_BG);
    DrawFrame(x, y, w, h, FIELD_EDGE);
    DrawText2(title, (WIDTH - (int)StrLen(title) * FONTW * 2) / 2, y + 10,
              title_col);
    DrawText(l1, (WIDTH - (int)StrLen(l1) * FONTW) / 2, y + 38,
             g_theme.menu_text);
    if (l2)
        DrawText(l2, (WIDTH - (int)StrLen(l2) * FONTW) / 2, y + 56,
                 g_theme.menu_dim);
}

// Find a free cell for the next apple. Returns False if the board is full.
static Bool SpawnFood(void)
{
    int free_cells = MAX_LEN - g_len;
    if (free_cells <= 0) return False;

    for (int attempt = 0; attempt < 4000; attempt++)
    {
        int x = (int)(RandNext() % COLS);
        int y = (int)(RandNext() % ROWS);

        Bool used = False;
        for (int i = 0; i < g_len; i++)
        {
            if (CellX(g_body[i]) == x && CellY(g_body[i]) == y)
            {
                used = True;
                break;
            }
        }
        if (!used) { g_food_x = x; g_food_y = y; return True; }
    }

    // Dense board fallback: first free cell in reading order.
    for (int y = 0; y < ROWS; y++)
    {
        for (int x = 0; x < COLS; x++)
        {
            Bool used = False;
            for (int i = 0; i < g_len; i++)
            {
                if (CellX(g_body[i]) == x && CellY(g_body[i]) == y)
                {
                    used = True;
                    break;
                }
            }
            if (!used) { g_food_x = x; g_food_y = y; return True; }
        }
    }
    return False;
}

// ---------------------------------------------------------------------------
// Game logic
// ---------------------------------------------------------------------------

static void SnakeReset(void)
{
    g_len = 3;
    int cx = COLS / 2;
    int cy = ROWS / 2;
    g_body[0] = MakeCell(cx - 2, cy);
    g_body[1] = MakeCell(cx - 1, cy);
    g_body[2] = MakeCell(cx, cy);
    g_dir = 3;            // right
    g_turn_n = 0;
    g_score = 0;

    SpawnFood();
    DrawField();
    DrawSnake();
    DispUpdate();
}

// Advance one tick. Returns False on collision.
static Bool SnakeStep(void)
{
    if (g_turn_n > 0)
    {
        g_dir = g_turn[0];
        g_turn[0] = g_turn[1];
        g_turn_n--;
    }

    u16 head_cell = g_body[g_len - 1];
    int hx = CellX(head_cell) + DirDx[g_dir];
    int hy = CellY(head_cell) + DirDy[g_dir];

    if (hx < 0 || hx >= COLS || hy < 0 || hy >= ROWS) return False;

    Bool eat = (hx == g_food_x && hy == g_food_y);
    u16 head = MakeCell(hx, hy);

    // Board completely filled: nothing left to eat.
    if (eat && g_len >= MAX_LEN) return False;

    // Self-collision. When not growing, the tail cell is vacated this tick,
    // so moving into it is legal.
    int check = eat ? g_len : g_len - 1;
    for (int i = 0; i < check; i++)
        if (g_body[i] == head) return False;

    if (eat)
    {
        g_len++;
        g_body[g_len - 1] = head;

        g_score += 10;
        if (g_score > g_hi) g_hi = g_score;

        DrawCell(CellX(g_body[g_len - 2]), CellY(g_body[g_len - 2]),
                 SNAKE_BODY);
        DrawCell(hx, hy, SNAKE_HEAD);
        DrawHeader();

        if (!SpawnFood()) return False;   // board filled: no apple left
        DrawFood();
    }
    else
    {
        // Erase the old tail before it is overwritten in the array.
        DrawCell(CellX(g_body[0]), CellY(g_body[0]), FIELD_BG);

        for (int i = 1; i < g_len; i++) g_body[i - 1] = g_body[i];
        g_body[g_len - 1] = head;

        DrawCell(CellX(g_body[g_len - 2]), CellY(g_body[g_len - 2]),
                 SNAKE_BODY);
        DrawCell(hx, hy, SNAKE_HEAD);
    }

    return True;
}

static int TickMs(void)
{
    int t = 140 - (g_score / 10) * 5;
    return (t < 60) ? 60 : t;
}

// Pause until A (resume) or X (exit). Returns False when the user exits.
static Bool SnakePause(void)
{
    DrawOverlay("PAUSED", "A: resume   X: exit", NULL, g_theme.menu_title);
    DispUpdate();

    // Drop the A that got us here so it doesn't resume immediately.
    while (KeyGet() != NOKEY) {}

    while (1)
    {
        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }
        if (k == KEY_X) return False;
        if (k == KEY_A) break;
    }

    DrawField();
    DrawSnake();
    DispUpdate();
    return True;
}

// Show the score screen. Returns True for "play again", False for "exit".
static Bool SnakeGameOver(void)
{
    char num[16];
    char buf[48];
    strcpy(buf, "Score ");
    DecNum(num, (s32)g_score, 0);
    strcat(buf, num);
    strcat(buf, "   Best ");
    DecNum(num, (s32)g_hi, 0);
    strcat(buf, num);

    DrawOverlay("GAME OVER", buf, "A: play again   X: exit", COL_RED);
    DispUpdate();

    while (KeyGet() != NOKEY) {}
    while (1)
    {
        u8 k = KeyGet();
        if (k == NOKEY) { WaitMs(20); continue; }
        if (k == KEY_X) return False;
        if (k == KEY_A) return True;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

void SnakeRun(void)
{
    // Start screen
    DrawClearCol(g_theme.menu_bg);
    DrawText2("SNAKE", (WIDTH - 5 * FONTW * 2) / 2, 46, g_theme.menu_title);
    DrawText("Eat the apples, avoid the walls", 20, 106, g_theme.menu_text);
    DrawText("and your own tail.", 20, 122, g_theme.menu_text);

    if (g_hi > 0)
    {
        char num[16];
        char buf[32];
        strcpy(buf, "Best score: ");
        DecNum(num, (s32)g_hi, 0);
        strcat(buf, num);
        DrawText(buf, (WIDTH - (int)StrLen(buf) * FONTW) / 2, 150,
                 g_theme.menu_sel);
    }

    const char* hint = "Arrows: turn   A: start   X: exit";
    DrawText(hint, (WIDTH - (int)StrLen(hint) * FONTW) / 2, 190,
             g_theme.menu_dim);
    DispUpdate();

    while (1)
    {
        u8 k = KeyGet();
        if (k == KEY_X) return;
        if (k == KEY_A) break;
        WaitMs(20);
    }

    g_rng = Time() ^ 0x9E3779B9u;
    if (g_rng == 0) g_rng = 1;
    SnakeReset();

    while (KeyGet() != NOKEY) {}
    u32 last = Time();

    while (1)
    {
        // Drain all queued input so fast cornering is not lost.
        while (1)
        {
            u8 k = KeyGet();
            if (k == NOKEY) break;

            if (k == KEY_X) return;

            if (k == KEY_A)
            {
                if (!SnakePause()) return;
                last = Time();      // resume without a catch-up tick
                continue;
            }

            int nd = -1;
            if (k == KEY_UP)         nd = 0;
            else if (k == KEY_DOWN)  nd = 1;
            else if (k == KEY_LEFT)  nd = 2;
            else if (k == KEY_RIGHT) nd = 3;
            if (nd < 0) continue;

            // Ignore reversals and duplicates, queue up to two turns.
            int cur = (g_turn_n > 0) ? g_turn[g_turn_n - 1] : g_dir;
            if (nd == cur || nd == (cur ^ 1)) continue;
            if (g_turn_n < 2) g_turn[g_turn_n++] = (u8)nd;
        }

        u32 now = Time();
        u32 tick_us = (u32)TickMs() * 1000u;
        if ((u32)(now - last) >= tick_us)
        {
            last += tick_us;

            if (!SnakeStep())
            {
                if (!SnakeGameOver()) return;
                SnakeReset();
                while (KeyGet() != NOKEY) {}
                last = Time();
            }
            DispUpdate();
        }

        WaitMs(2);
    }
}
