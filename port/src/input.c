#include "input.h"
#include <string.h>

InputState g_in;
uint8_t g_binding[ACT_COUNT];

/* DS:14FE - indices selectable in the "Define key controls" menu */
const uint8_t g_key_scancode[KEY_TABLE_SIZE] = {
    0x2A, 0x36, 0x1D, 0x38, 0x3A, 0x39, 0x1C, 0x52, 0x53,
    0x47, 0x4F, 0x49, 0x51, 0x48, 0x50, 0x4B, 0x4D, 0x4C
};
/* Names are the game's own strings where they appear in the EXE; the five
 * short ones (Ctrl, Alt, Caps, Home, End) are inferred from the scancodes. */
const char *const g_key_name[KEY_TABLE_SIZE] = {
    "Left shift", "Right shift", "Ctrl", "Alt", "Caps", "Space", "Enter",
    "Insert", "Delete", "Home", "End", "Page up", "Page down",
    "Up arrow", "Down arrow", "Left arrow", "Right arrow", "Cursor 5"
};

static input_cheat_cb cheat_cb;
void input_set_cheat_callback(input_cheat_cb cb) { cheat_cb = cb; }

void input_init_defaults(void)
{
    /* DS:1558..155F as shipped: Left,Right,Jump,Fire,Up,Down,ScrollUp,ScrollDown */
    static const uint8_t d[ACT_COUNT] = { 15, 16, 2, 3, 13, 14, 11, 12 };
    memcpy(g_binding, d, sizeof d);
}

void input_reset(void)
{
    g_in.up = g_in.up2 = g_in.down = g_in.left = g_in.right = 0;
    g_in.jump = g_in.fire = 0;
    g_in.scroll_up = g_in.scroll_down = g_in.esc = 0;
    g_in.latch_jump = g_in.latch_fire = g_in.latch_up = 1;
    g_in.hot_v = g_in.hot_s = g_in.hot_m = g_in.hot_c = 0;
    g_in.f1 = g_in.f10 = g_in.f3 = g_in.f2 = 0;
    g_in.pause = 0;
}

static void slot_event(int a, int down)
{
    switch (a) {
    case ACT_LEFT:        g_in.left  = down; break;
    case ACT_RIGHT:       g_in.right = down; break;
    case ACT_DOWN:        g_in.down  = down; break;
    case ACT_SCROLL_UP:   g_in.scroll_up   = down; break;
    case ACT_SCROLL_DOWN: g_in.scroll_down = down; break;
    case ACT_JUMP:
        if (down) { if (g_in.latch_jump) { g_in.jump = 1; g_in.latch_jump = 0; } }
        else      { g_in.jump = 0; g_in.latch_jump = 1; }
        break;
    case ACT_FIRE:
        if (down) { if (g_in.latch_fire) { g_in.fire = 1; g_in.latch_fire = 0; } }
        else      { g_in.fire = 0; g_in.latch_fire = 1; }
        break;
    case ACT_UP:
        if (down) { if (g_in.latch_up) { g_in.up = g_in.up2 = 1; g_in.latch_up = 0; } }
        else      { g_in.up = g_in.up2 = 0; g_in.latch_up = 1; }
        break;
    }
}

static void bound_key(int a, uint8_t sc)
{
    uint8_t want = g_key_scancode[g_binding[a]];
    if (sc == want)                        slot_event(a, 1);
    else if (sc == (uint8_t)(want + 0x80)) slot_event(a, 0);
}

void input_action(int action, int down)
{
    if (action >= 0 && action < ACT_COUNT)
        slot_event(action, down ? 1 : 0);
}

void input_scancode(uint8_t sc)
{
    g_in.last_scancode = sc;

    /* Guarded block (skipped while suppress_bindings): any_key + the six
     * movement/combat actions, in the original order. */
    if (!g_in.suppress_bindings) {
        static const uint8_t order[6] = {
            ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_FIRE };
        if (sc < 0x80) g_in.any_key = 1;
        for (int i = 0; i < 6; i++) bound_key(order[i], sc);
    }

    /* scancode-sum detector: original checks 5BA (then resets), 3D6, 378, 4D8 */
    if (sc == 0x21 || sc == 0x10 || sc == 0x30) g_in.sum = 0;
    g_in.sum = (uint16_t)(g_in.sum + sc);
    if (g_in.sum == 0x5BA) { g_in.sum = 0; if (cheat_cb) cheat_cb(0x5BA); }
    if (g_in.sum == 0x3D6 && cheat_cb) cheat_cb(0x3D6);
    if (g_in.sum == 0x378 && cheat_cb) cheat_cb(0x378);
    if (g_in.sum == 0x4D8 && cheat_cb) cheat_cb(0x4D8);

    /* fixed keys */
    if (sc == 0x19) g_in.pause = 1;
    /* scroll keys come after the pause check and are NOT suppressed */
    bound_key(ACT_SCROLL_UP, sc);
    bound_key(ACT_SCROLL_DOWN, sc);
    if (sc == 0x01) g_in.esc = 1;
    if (sc == 0x81) g_in.esc = 0;
    if (sc == 0x3C) g_in.f2 = 1;
    if (sc == 0x3D) g_in.f3 = 1;
    if (sc == 0x44) g_in.f10 = 1;
    if (sc == 0x3B) g_in.f1 = 1;
    if (sc == 0x2E) g_in.hot_c = 1;
    if (sc == 0x32) g_in.hot_m = 1;
    if (sc == 0x1F) g_in.hot_s = 1;
    if (sc == 0x2F) g_in.hot_v = 1;
}
