// ****************************************************************************
//
//                  PicoDeck — Button test
//
//   Shows the live state of all eight PicoPad buttons and latches each one the
//   first time it is seen, so a full set can be checked quickly. Useful after
//   assembly, a shell swap or a repair.
//
//   Controls:
//     any button  : highlight / mark as tested
//     LEFT + A    : hold for 1.2 s to exit
//
// ****************************************************************************

#include "../include.h"
#include "button_test.h"
#include "theme.h"

#define BT_EXIT_HOLD_MS   1200

// Display order: D-pad on the left, action buttons on the right.
// Index order must match the bit positions used by the mask below.
static const u8 BtnKeys[8] = {
    KEY_UP, KEY_LEFT, KEY_RIGHT, KEY_DOWN,
    KEY_A,  KEY_B,   KEY_X,     KEY_Y,
};
static const char* const BtnLabels[8] = {
    "UP", "LEFT", "RIGHT", "DOWN",
    "A",  "B",    "X",     "Y",
};

// Layout
#define BT_TITLE_Y    10
#define BT_DIV_Y      32
#define BT_STATUS_Y   38
#define BT_GRID_Y     56
#define BT_CELL_W     136
#define BT_CELL_H     32
#define BT_CELL_STEP  36
#define BT_COL1_X     16
#define BT_COL2_X     168
#define BT_FOOT_DIV_Y 204
#define BT_HINT_Y     210
#define BT_BAR_X      10
#define BT_BAR_Y      224
#define BT_BAR_W      300
#define BT_BAR_H      10

static void DrawCentered(const char* s, int y, u16 col)
{
    DrawText(s, (WIDTH - (int)StrLen(s) * FONTW) / 2, y, col);
}

static void DrawCell(int idx, Bool pressed, Bool tested)
{
    int col = idx / 4;
    int row = idx % 4;
    int x = (col == 0) ? BT_COL1_X : BT_COL2_X;
    int y = BT_GRID_Y + row * BT_CELL_STEP;

    if (pressed)
    {
        DrawRect(x, y, BT_CELL_W, BT_CELL_H, g_theme.menu_hl);
        DrawFrameW(x, y, BT_CELL_W, BT_CELL_H, g_theme.menu_sel, 2);
    }
    else
    {
        DrawFrame(x, y, BT_CELL_W, BT_CELL_H, g_theme.menu_dim);
    }

    int tx = x + (BT_CELL_W - (int)StrLen(BtnLabels[idx]) * FONTW) / 2;
    int ty = y + (BT_CELL_H - FONTH) / 2;
    DrawText(BtnLabels[idx], tx, ty,
             pressed ? g_theme.menu_sel : g_theme.menu_text);

    // Latch dot: this button has been pressed at least once.
    if (tested)
        DrawRect(x + BT_CELL_W - 10, y + 5, 4, 4,
                 pressed ? g_theme.menu_text : g_theme.ok_color);
}

static void DrawExitBar(void)
{
    DrawFrame(BT_BAR_X, BT_BAR_Y, BT_BAR_W, BT_BAR_H, g_theme.menu_dim);
    DrawRect(BT_BAR_X + 2, BT_BAR_Y + 2, BT_BAR_W - 4, BT_BAR_H - 4,
             g_theme.menu_bg);
}

static void DrawExitFill(u32 held)
{
    int inner_w = BT_BAR_W - 4;
    int fill = (int)((u32)inner_w * held / BT_EXIT_HOLD_MS);
    if (fill > inner_w) fill = inner_w;

    DrawRect(BT_BAR_X + 2, BT_BAR_Y + 2, inner_w, BT_BAR_H - 4,
             g_theme.menu_bg);
    if (fill > 0)
        DrawRect(BT_BAR_X + 2, BT_BAR_Y + 2, fill, BT_BAR_H - 4,
                 g_theme.ok_color);
}

static void RenderAll(u8 pressed_mask, u8 seen_mask, const char* last_label)
{
    DrawClearCol(g_theme.menu_bg);

    DrawText2("Button Test", (WIDTH - 11 * FONTW * 2) / 2, BT_TITLE_Y,
              g_theme.menu_title);
    DrawRect(20, BT_DIV_Y, WIDTH - 40, 1, g_theme.menu_dim);

    // Status line: tested count + last press, or the all-good message.
    if (seen_mask == 0xFF)
    {
        DrawCentered("All 8 buttons OK!", BT_STATUS_Y, g_theme.ok_color);
    }
    else
    {
        int tested = 0;
        for (int i = 0; i < 8; i++) if (seen_mask & (1u << i)) tested++;

        char buf[40];
        char num[4];
        strcpy(buf, "Tested ");
        DecNum(num, tested, 0);
        strcat(buf, num);
        strcat(buf, "/8   Last:");
        if (last_label)
        {
            strcat(buf, " ");
            strcat(buf, last_label);
        }
        DrawCentered(buf, BT_STATUS_Y, g_theme.menu_text);
    }

    for (int i = 0; i < 8; i++)
        DrawCell(i, (pressed_mask & (1u << i)) != 0,
                    (seen_mask & (1u << i)) != 0);

    DrawRect(20, BT_FOOT_DIV_Y, WIDTH - 40, 1, g_theme.menu_dim);
    DrawCentered("Hold LEFT + A to exit", BT_HINT_Y, g_theme.menu_dim);
    DrawExitBar();

    DispUpdate();
}

void ButtonTestRun(void)
{
    KeyFlush();   // drop the A press that opened this tool

    u8 seen_mask    = 0;
    u8 last_mask    = 0xFF;
    u8 last_seen    = 0xFF;
    const char* last_label = 0;

    Bool holding = False;
    u32 hold_start = 0;
    u32 last_fill = 0xFFFFFFFFu;

    while (1)
    {
        u8 mask = 0;
        for (int i = 0; i < 8; i++)
            if (KeyPressed(BtnKeys[i])) mask |= (u8)(1u << i);

        u8 new_seen = seen_mask | mask;

        if (new_seen != seen_mask)
        {
            u8 fresh = (u8)(new_seen & ~seen_mask);
            for (int i = 7; i >= 0; i--)
                if (fresh & (1u << i)) { last_label = BtnLabels[i]; break; }
            seen_mask = new_seen;
        }

        Bool exit_hold = ((mask & (1u << 1)) != 0) &&   // LEFT
                         ((mask & (1u << 4)) != 0);     // A

        // Full redraw only when something visual changed.
        if (mask != last_mask || seen_mask != last_seen)
        {
            last_mask = mask;
            last_seen = seen_mask;
            RenderAll(mask, seen_mask, last_label);
            last_fill = 0xFFFFFFFFu;
        }

        if (exit_hold)
        {
            if (!holding)
            {
                holding = True;
                hold_start = Millis();
            }

            u32 held = (u32)(Millis() - hold_start);
            if (held >= BT_EXIT_HOLD_MS) break;

            if (held != last_fill)
            {
                last_fill = held;
                DrawExitFill(held);
                DispUpdate();
            }
        }
        else if (holding)
        {
            holding = False;
            last_fill = 0;
            DrawExitFill(0);
            DispUpdate();
        }

        WaitMs(20);
    }

    // Let go of the combo before returning, otherwise the buffered repeats
    // would be processed by the menu. Cap the wait so a stuck contact cannot
    // trap the user here.
    u32 rel = Millis();
    while ((KeyPressed(KEY_LEFT) || KeyPressed(KEY_A)) &&
           (u32)(Millis() - rel) < 3000)
        WaitMs(20);

    KeyFlush();
}
