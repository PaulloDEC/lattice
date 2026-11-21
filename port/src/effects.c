#include "effects.h"
#include "rng.h"
#include "video.h"
#include "vga.h"
#include <string.h>

FxState g_fx;
static fx_draw_fn draw_cb;

void fx_set_draw_callback(fx_draw_fn fn) { draw_cb = fn; }
static void draw(int mode, int sprite, int x, int y, int variant, int frame)
{
    if (draw_cb) draw_cb(mode, sprite, x, y, variant, frame);
}
void fx_draw(int a0, int a1, int x, int y, int a4, int a5) { draw(a0, a1, x, y, a4, a5); }
static fx_draw_fn solid_cb;
void fx_set_solid_callback(fx_draw_fn fn) { solid_cb = fn; }
void fx_draw_solid(int a0, int a1, int x, int y, int a4, int a5) { if (solid_cb) solid_cb(a0, a1, x, y, a4, a5); }

void fx_reset(void)
{
    int16_t sb = g_fx.sprite_b, sc = g_fx.sprite_c;
    memset(&g_fx, 0, sizeof g_fx);
    for (int i = 0; i < 20; i++) g_fx.d_x[i] = -99;
    g_fx.sprite_b = sb; g_fx.sprite_c = sc;
}

/* The level start (0BA5:4549..) clears only these fields; the other pools keep stale contents that the update
 * routines never reach (their liveness fields are zero). */
void fx_level_reset(void)
{
    memset(g_fx.a_flags, 0xFF, sizeof g_fx.a_flags); g_fx.a_ring = 0; g_fx.c_ring = 0;
    for (int i = 0; i < 8; i++) g_fx.c[i][2] = 0;
    for (int i = 0; i < 10; i++) g_fx.b[i][3] = 0;
    for (int i = 0; i < 20; i++) g_fx.d_x[i] = -99;
}

/* ---- B: score tags ------------------------------------------------------------------- */
void fx_spawn_score(int x, int y, int type)
{
    int16_t cx = (int16_t)type;
    for (int i = 0; i < 10; i++) {
        if (g_fx.b[i][3] != 0) continue;
        if (cx > 4) { g_fx.b[i][4] = 1; cx = (int16_t)(cx - 5); }
        else          g_fx.b[i][4] = 0;
        g_fx.b[i][0] = (int16_t)x;
        g_fx.b[i][1] = (int16_t)y;
        g_fx.b[i][2] = cx;
        g_fx.b[i][3] = 0x11;
        return;
    }
}

void fx_update_scores(void)
{
    for (int i = 0; i < 10; i++) {
        if (g_fx.b[i][3] == 0) continue;
        draw(1, g_fx.sprite_b, g_fx.b[i][0], g_fx.b[i][1], g_fx.b[i][4], g_fx.b[i][2]);
        g_fx.b[i][1] = (int16_t)(g_fx.b[i][1] - 2);
        g_fx.b[i][3] = (int16_t)(g_fx.b[i][3] - 1);
    }
}

/* ---- C: puffs --------------------------------------------------------------------------- */
void fx_spawn_puff(int x, int y)
{
    int s = g_fx.c_ring;
    g_fx.c[s][0] = (int16_t)x;
    g_fx.c[s][1] = (int16_t)y;
    g_fx.c[s][2] = 9;
    g_fx.c[s][3] = (int16_t)rng_next(2);
    g_fx.c_ring++;
    if (g_fx.c_ring >= 8) g_fx.c_ring = 0;
}

void fx_update_puffs(void)
{
    video_set_latch_mode(0);
    for (int i = 0; i < 8; i++) {
        int16_t n = g_fx.c[i][2];
        if (n == 0) continue;
        int frame = -1;                 /* counter 9,1 -> 0   8,2 -> 1   7,3 -> 2   6,4 -> 3   5 -> 4 */
        if (n == 9 || n == 1) frame = 0;
        else if (n == 8 || n == 2) frame = 1;
        else if (n == 7 || n == 3) frame = 2;
        else if (n == 6 || n == 4) frame = 3;
        else if (n == 5) frame = 4;
        if (frame >= 0)
            draw(3, g_fx.sprite_c, g_fx.c[i][0], g_fx.c[i][1], g_fx.c[i][3], frame);
        g_fx.c[i][2] = (int16_t)(n - 1);
    }
}

/* ---- D: falling sparkles ---------------------------------------------------------------- */
void fx_spawn_sparkle(int x, int y)
{
    int s = g_fx.d_ring;
    if (g_fx.d_x[s] == -99) {           /* only a free slot is filled ... */
        g_fx.d_x[s] = (int16_t)x;
        g_fx.d_y[s] = (int16_t)y;
        g_fx.d_t[s] = 0;
    }
    g_fx.d_ring++;                      /* ... but the ring always advances (21FC) */
    if (g_fx.d_ring >= 20) g_fx.d_ring = 0;
}

void fx_update_sparkles(void)
{
    video_set_latch_mode(0);
    for (int i = 0; i < 20; i++) {
        if (g_fx.d_x[i] >= 0 && g_fx.d_x[i] < 0x50 && g_fx.d_y[i] < 0xA0 && g_fx.d_y[i] >= 0) {
            int plane = rng_next(4);
            video_set_map_mask((uint8_t)(1u << (plane & 7)));
            if (rng_next(2) == 0) {
                uint8_t color = (uint8_t)(g_fx.d_t[i] / 4 + 0x78);
                uint16_t off = (uint16_t)(video_draw_offset() + (uint16_t)(g_fx.d_y[i] * 80) + g_fx.d_x[i]);
                vga_write(off, color);
            }
        }
        g_fx.d_y[i]++;
        g_fx.d_t[i]++;
        if (g_fx.d_t[i] >= 0x10) g_fx.d_x[i] = -99;
    }
}

/* ---- A: spark bursts --------------------------------------------------------------------- */
void fx_spawn_burst(int x, int y)
{
    int b = g_fx.a_ring;
    g_fx.a_flags[b] = 0x10;
    for (int p = 0; p < 16; p++) {
        g_fx.a_pos[b][p][0] = (int16_t)x;
        int r1 = rng_next(10);
        int r2 = rng_next(10);
        g_fx.a_vel[b][p][0] = (int8_t)(r1 - r2);              /* sideways speed -9..9 */
        g_fx.a_pos[b][p][1] = (int16_t)y;
        g_fx.a_vel[b][p][1] = (int8_t)-(int8_t)(rng_next(7) + 5);   /* upward speed -5..-11 */
        g_fx.a_vel[b][p][2] = (int8_t)(rng_next(2) == 0 ? 0x70 : 0x60);
    }
    g_fx.a_ring++;
    if (g_fx.a_ring >= 8) g_fx.a_ring = 0;
}

void fx_update_bursts(void)
{
    video_set_latch_mode(0);
    for (int b = 0; b < 8; b++) {
        if (g_fx.a_flags[b] <= 0) continue;
        for (int p = 0; p < 16; p++) {
            g_fx.a_pos[b][p][1] = (int16_t)(g_fx.a_pos[b][p][1] + g_fx.a_vel[b][p][1]);
            g_fx.a_vel[b][p][1] = (int8_t)(g_fx.a_vel[b][p][1] + 1);          /* gravity */
            g_fx.a_pos[b][p][0] = (int16_t)(g_fx.a_pos[b][p][0] + g_fx.a_vel[b][p][0]);
            /* The original also has `if (!flags % 4) colour++` here (3071-308D).  It is a
             * compiled operator-precedence slip: inside this branch flags > 0 so it never
             * fires.  Kept out deliberately. */
            int x = g_fx.a_pos[b][p][0], y = g_fx.a_pos[b][p][1];
            if (x >= 0 && x <= 0x13F && y >= 0 && y <= 0x9F) {
                video_set_map_mask((uint8_t)(1u << (x % 4)));
                vga_write((uint16_t)(video_draw_offset() + (uint16_t)(y * 80) + (x >> 2)),
                          (uint8_t)g_fx.a_vel[b][p][2]);
            }
        }
        g_fx.a_flags[b] = (int8_t)(g_fx.a_flags[b] - 1);
    }
}
