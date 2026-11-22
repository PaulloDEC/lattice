#include "stars.h"
#include "video.h"
#include "rng.h"
#include <string.h>

int32_t  g_trig[360][2];
uint16_t g_star_ang[STAR_COUNT], g_star_rad[STAR_COUNT];
uint8_t  g_star_spd[STAR_COUNT], g_star_br[STAR_COUNT], g_star_spd2[STAR_COUNT];

int g_star_wide_extra;
uint8_t g_star_side[200][STAR_SIDE_W];
unsigned g_star_side_steps;
#define GHOSTS (STAR_COUNT * 2)
static struct { uint16_t ang, rad; uint8_t spd, br, spd2, live; } s_ghost[GHOSTS];
void stars_side_clear(void) { memset(g_star_side, 0, sizeof g_star_side); memset(s_ghost, 0, sizeof s_ghost); }

void stars_set_table(const uint8_t e[2880])
{
    for (int i = 0; i < 360; i++)
        for (int k = 0; k < 2; k++) {
            float f; memcpy(&f, e + i * 8 + k * 4, 4);
            long double v = (long double)f * 1000000.0L;                  /* x87: fmul dword, then the truncating __ftol */
            g_trig[i][k] = (int32_t)(int64_t)v;
        }
}

static void pos(int i, int *x, int *y)
{
    int32_t r = (int16_t)g_star_rad[i];
    *x = (int16_t)(((int32_t)((uint32_t)g_trig[g_star_ang[i]][0] * (uint32_t)r)) / 1000000 + 0xA0);
    *y = (int16_t)(((int32_t)((uint32_t)g_trig[g_star_ang[i]][1] * (uint32_t)r)) / 1000000 + 0x70);
}
static int outside(int x, int y) { return x < 0 || x > 0x13F || y < 0 || y > 0xC7; }
static void gpos(int g, int *x, int *y)          /* pos() for a ghost */
{
    int32_t r = (int16_t)s_ghost[g].rad;
    *x = (int16_t)(((int32_t)((uint32_t)g_trig[s_ghost[g].ang][0] * (uint32_t)r)) / 1000000 + 0xA0);
    *y = (int16_t)(((int32_t)((uint32_t)g_trig[s_ghost[g].ang][1] * (uint32_t)r)) / 1000000 + 0x70);
}
static int side_ok(int x, int y) { int sx = x + g_star_wide_extra; return y >= 0 && y <= 0xC7 && sx >= 0 && sx < STAR_SIDE_W && (x < 0 || x > 0x13F); }
static void ghosts_step(void)                     /* the side copies: move outwards, gone at the wide edge */
{
    for (int g = 0; g < GHOSTS; g++) {
        if (!s_ghost[g].live) continue;
        int x, y; gpos(g, &x, &y);
        if (side_ok(x, y)) g_star_side[y][x + g_star_wide_extra] = 0;
        s_ghost[g].rad = (uint16_t)(s_ghost[g].rad + s_ghost[g].spd);
        s_ghost[g].br = (uint8_t)(s_ghost[g].br + s_ghost[g].spd2); if (s_ghost[g].br > 0x7F) s_ghost[g].br = 0x7F;
        gpos(g, &x, &y);
        if (!side_ok(x, y)) { s_ghost[g].live = 0; continue; }
        g_star_side[y][x + g_star_wide_extra] = (uint8_t)(0xF0 + (s_ghost[g].br >> 3));
    }
}

static void respawn_pos(int i, int *x, int *y)
{
    do {
        g_star_ang[i] = (uint16_t)rng_next(0x168);
        g_star_rad[i] = (uint16_t)rng_next(0xA0);
        pos(i, x, y);
    } while (outside(*x, *y));
}
static void respawn_motion(int i)
{
    g_star_spd[i] = (uint8_t)(rng_next(5) + 1);
    g_star_br[i] = 0;
    g_star_spd2[i] = (uint8_t)(rng_next(5) + 1);
}

void stars_init(void)
{
    for (int i = 0; i < STAR_COUNT; i++) { int x, y; respawn_pos(i, &x, &y); respawn_motion(i); }
}

void stars_step(void)
{
    for (int i = 0; i < STAR_COUNT; i++) {
        int x, y;
        pos(i, &x, &y);
        if (video_get_pixel(x, y) >= 0xF0) video_put_pixel(x, y, 0);
        g_star_rad[i] = (uint16_t)(g_star_rad[i] + g_star_spd[i]);
        g_star_br[i] = (uint8_t)(g_star_br[i] + g_star_spd2[i]);
        if (g_star_br[i] > 0x7F) g_star_br[i] = 0x7F;
        pos(i, &x, &y);
        if (outside(x, y)) {
            if (g_star_wide_extra && side_ok(x, y))       /* port, wide view: it carries on at the side */
                for (int g = 0; g < GHOSTS; g++)
                    if (!s_ghost[g].live) {
                        s_ghost[g].ang = g_star_ang[i]; s_ghost[g].rad = g_star_rad[i]; s_ghost[g].spd = g_star_spd[i];
                        s_ghost[g].br = g_star_br[i]; s_ghost[g].spd2 = g_star_spd2[i]; s_ghost[g].live = 1;
                        g_star_side[y][x + g_star_wide_extra] = (uint8_t)(0xF0 + (g_star_br[i] >> 3));
                        break;
                    }
            respawn_pos(i, &x, &y); respawn_motion(i);
        }
        if (video_get_pixel(x, y) == 0) video_put_pixel(x, y, 0xF0 + (g_star_br[i] >> 3));
    }
    if (g_star_wide_extra) { ghosts_step(); g_star_side_steps++; }
    video_wait_vsync();
}
