#include "map.h"
#include "vga.h"
#include "video.h"

/* tile selection: layer1, else layer2, else -1 */
static int pick(const uint8_t *l1, const uint8_t *l2, uint16_t bx)
{
    if (l1[bx] != 0xFF) return l1[bx];
    if (l2[bx] != 0xFF) return l2[bx];
    return -1;
}

/* copy `w` bytes of each of 16 rows (source row stride 4, from byte `sx`) to dest rows of stride 80 */
static void cell(uint16_t dest, uint16_t src_off, int tile, int sx, int w)
{
    uint16_t s = (uint16_t)(src_off + (uint16_t)(tile << 6) + sx);
    for (int r = 0; r < 16; r++) {
        for (int k = 0; k < w; k++)
            vga_write((uint16_t)(dest + r * 80 + k), vga_read((uint16_t)(s + k)));
        s = (uint16_t)(s + 4);
    }
}

void map_draw_even(uint16_t dest, uint16_t src_off, const uint8_t *l1, const uint8_t *l2, uint16_t bx)
{
    for (int row = 0; row < 10; row++) {
        for (int c = 0; c < 20; c++) {
            int t = pick(l1, l2, bx);
            if (t >= 0) cell(dest, src_off, t, 0, 4);
            dest = (uint16_t)(dest + 4); bx++;
        }
        dest = (uint16_t)(dest + 0x4B0);
        bx = (uint16_t)(bx + 0xDC);
    }
}

void map_draw_odd(uint16_t dest, uint16_t src_off, const uint8_t *l1, const uint8_t *l2, uint16_t bx)
{
    for (int row = 0; row < 10; row++) {
        int t = pick(l1, l2, bx);
        if (t >= 0) cell(dest, src_off, t, 2, 2);
        dest = (uint16_t)(dest + 2); bx++;
        for (int c = 0; c < 19; c++) {
            t = pick(l1, l2, bx);
            if (t >= 0) cell(dest, src_off, t, 0, 4);
            dest = (uint16_t)(dest + 4); bx++;
        }
        t = pick(l1, l2, bx);
        if (t >= 0) cell(dest, src_off, t, 0, 2);
        dest = (uint16_t)(dest + 2);
        dest = (uint16_t)(dest + 0x4B0);
        bx = (uint16_t)(bx + 0xDC);
    }
}

/* ------------------------------------------------------------------------------------------ */
/* Backdrop.  D = dest offset, S = source offset (both relative to the argument bases), x = the
 * source column (0..0x4F) inside the 80-byte backdrop row.                                      */
typedef struct { uint16_t D, S; int16_t x; uint16_t dest0, src0; } Bd;

static void bd_rows(Bd *b, int w, int stride_unused)
{
    (void)stride_unused;
    for (int r = 0; r < 16; r++)
        for (int k = 0; k < w; k++)
            vga_write((uint16_t)(b->dest0 + b->D + r * 80 + k), vga_read((uint16_t)(b->src0 + b->S + r * 80 + k)));
}

/* w single-byte passes, used when the cell straddles the right edge of the backdrop row */
static void bd_slow(Bd *b, int w)
{
    for (int i = 0; i < w; i++) {
        bd_rows(b, 1, 0);
        b->D = (uint16_t)(b->D + 1); b->S = (uint16_t)(b->S + 1);
        b->x++;
        if (b->x == 0x50) { b->x = 0; b->S = (uint16_t)(b->S - 0x50); }
    }
}

void backdrop_draw_even(uint16_t dest0, uint16_t src0, const uint8_t *l1, const uint8_t *l2, uint16_t m, int cam_x)
{
    Bd b; b.dest0 = dest0; b.src0 = src0; b.x = (int16_t)(cam_x % 0x50); b.D = 0; b.S = (uint16_t)b.x;
    video_set_latch_mode(1); video_set_map_mask(0xF);      /* the originals program these themselves */
    for (int row = 0; row < 10; row++) {
        for (int c = 0; c < 20; c++) {
            if (l1[m] == 0xFF && l2[m] == 0xFF) {
                if (b.x <= 0x4C) {
                    bd_rows(&b, 4, 0);
                    b.D = (uint16_t)(b.D + 4); b.S = (uint16_t)(b.S + 4); b.x = (int16_t)(b.x + 4);
                    if (b.x >= 0x50) { b.x = 0; b.S = (uint16_t)(b.S - 0x50); }
                } else bd_slow(&b, 4);
            } else {
                b.D = (uint16_t)(b.D + 4); b.S = (uint16_t)(b.S + 4); b.x = (int16_t)(b.x + 4);
                if (b.x >= 0x50) { b.x = (int16_t)(b.x - 0x50); b.S = (uint16_t)(b.S - 0x50); }
            }
            m++;
        }
        b.D = (uint16_t)(b.D + 0x4B0); b.S = (uint16_t)(b.S + 0x500); m = (uint16_t)(m + 0xDC);
    }
}

void backdrop_draw_odd(uint16_t dest0, uint16_t src0, const uint8_t *l1, const uint8_t *l2, uint16_t m, int cam_x)
{
    Bd b; b.dest0 = dest0; b.src0 = src0; b.x = (int16_t)(cam_x % 0x50); b.D = 0; b.S = (uint16_t)b.x;
    video_set_latch_mode(1); video_set_map_mask(0xF);      /* the originals program these themselves */
    for (int row = 0; row < 10; row++) {
        /* first half cell (2 bytes) */
        if (l1[m] == 0xFF && l2[m] == 0xFF) {
            if (b.x <= 0x4E) {
                bd_rows(&b, 2, 0);
                b.D = (uint16_t)(b.D + 2); b.S = (uint16_t)(b.S + 2); b.x = (int16_t)(b.x + 2);
                if (b.x >= 0x50) { b.x = 0; b.S = (uint16_t)(b.S - 0x50); }
            } else bd_slow(&b, 2);
        } else {
            b.D = (uint16_t)(b.D + 2); b.S = (uint16_t)(b.S + 2);
            for (int i = 0; i < 2; i++) { b.x++; if (b.x == 0x50) { b.x = 0; b.S = (uint16_t)(b.S - 0x50); } }
        }
        m++;
        for (int c = 0; c < 19; c++) {            /* 19 whole cells */
            if (l1[m] == 0xFF && l2[m] == 0xFF) {
                if (b.x <= 0x4C) {
                    bd_rows(&b, 4, 0);
                    b.D = (uint16_t)(b.D + 4); b.S = (uint16_t)(b.S + 4); b.x = (int16_t)(b.x + 4);
                    if (b.x >= 0x50) { b.x = 0; b.S = (uint16_t)(b.S - 0x50); }
                } else bd_slow(&b, 4);
            } else {
                b.D = (uint16_t)(b.D + 4); b.S = (uint16_t)(b.S + 4); b.x = (int16_t)(b.x + 4);
                if (b.x >= 0x50) { b.x = (int16_t)(b.x - 0x50); b.S = (uint16_t)(b.S - 0x50); }
            }
            m++;
        }
        /* last half cell (2 bytes) */
        if (l1[m] == 0xFF && l2[m] == 0xFF) {
            if (b.x <= 0x4E) {
                bd_rows(&b, 2, 0);
                b.D = (uint16_t)(b.D + 2); b.S = (uint16_t)(b.S + 2); b.x = (int16_t)(b.x + 2);
                if (b.x >= 0x50) { b.x = 0; b.S = (uint16_t)(b.S - 0x50); }
            } else bd_slow(&b, 2);
        } else {
            b.D = (uint16_t)(b.D + 2); b.S = (uint16_t)(b.S + 2); b.x = (int16_t)(b.x + 2);
            if (b.x > 0x50) { b.x = (int16_t)(b.x - 0x50); b.S = (uint16_t)(b.S - 0x50); }   /* '>' not '>=' (original) */
        }
        b.D = (uint16_t)(b.D + 0x4B0); b.S = (uint16_t)(b.S + 0x500); m = (uint16_t)(m + 0xDC);
    }
}

/* ------------------------------------------------------------------------------------------ */
#include "camera.h"
#include "effects.h"
#include "rng.h"

MapState g_map;

void map_animate(void)
{
    g_map.timer--;
    uint16_t di = (uint16_t)(g_cam.cam_row * MAP_WIDTH + g_cam.cam_col);
    for (int r = 0; r < 10; r++) {
        for (int c = 0; c < 21; c++, di++) {
            int si = g_map.dyn[di];
            if (si != 0) {
                int act = 0;
                if (g_map.timer == 0) act = 1;
                else act = (rng_next(3) == 0);
                if (act && (si == 1 || si == 2)) {
                    if (si == 1) g_map.fg[di] = 0xFF; else g_map.fg[di] = g_map.alt[di];
                    g_map.dyn[di] = 0;
                    if (g_map.timer != 0 && c < 0x14)
                        fx_spawn_puff((c << 2) - (g_cam.col_odd << 1), r << 4);
                }
            }
            int t = g_map.bg[di];
            if (t < 0xF0) {
                const uint8_t *a = g_map.anim + t * 3;
                int v = t;
                if (a[2] == 0) continue;
                if (a[2] == 1) {
                    v = t + 1; if (a[1] < v) v = a[0];
                } else if (a[2] == 2) {
                    if (a[0] != t) { v = t + 1; if (a[1] < v) v = a[0]; }
                    else if (rng_next(20) == 0) v = t + 1;
                }
                g_map.bg[di] = (uint8_t)v;
            }
        }
        di = (uint16_t)(di + 0xDB);
    }
}
