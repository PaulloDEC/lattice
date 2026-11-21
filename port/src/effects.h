/*
 * effects.h - Hocus Pocus world-anchored effect pools (code segment 0BA5).
 *
 *   B  "score tag" popups   spawn 0BA5:3684   update 0BA5:36F0   10 slots
 *   C  9-frame puff effect  spawn 0BA5:2174   update 0BA5:1FAA  8 slot ring
 *   D  falling sparkles     spawn 0BA5:21C6   update 0BA5:20B1  20 slot ring
 *   A  spark bursts         spawn 0BA5:2F1C   update 0BA5:2FF3  8 bursts x 16 particles
 *
 * Field layouts are identical to the original DS memory (16-bit words) so the pools can be
 * compared byte-for-byte against the original code and the camera routine can shift them.
 * Units: x is in 4-pixel byte columns, y in pixels.
 * The sprite blitter (0BA5:249E) is not ported yet: B and C updates call it through a callback
 *     draw(mode, sprite, x, y, variant, frame)
 * where `sprite` is the game's sprite handle word (DS:C296 for B, DS:C44E for C).
 */
#ifndef HOCUS_EFFECTS_H
#define HOCUS_EFFECTS_H
#include <stdint.h>

typedef void (*fx_draw_fn)(int mode, int sprite, int x, int y, int variant, int frame);

typedef struct FxState {
    int16_t b[10][5];   /* DS:CE04  x, y, frame, life(0=free), variant            */
    int16_t c[8][4];    /* DS:CDC2  x, y, counter(0=free), variant                */
    int16_t d_t[20];    /* DS:CF58  age                                           */
    int16_t d_y[20];    /* DS:CF80                                                */
    int16_t d_x[20];    /* DS:CFA8  -99 = free                                    */
    int16_t c_ring;     /* DS:CE02                                                */
    int16_t d_ring;     /* DS:2ACC                                                */
    int16_t sprite_b;   /* DS:C296                                                */
    int16_t sprite_c;   /* DS:C44E                                                */
    /* ---- A: spark bursts (debris with gravity), positions in PIXELS ---------------------- */
    int8_t  a_flags[8];       /* DS:CA38  frames left (16 at spawn), <=0 = idle              */
    int8_t  a_vel[8][16][3];  /* DS:CA40  per particle: vx, vy (+1/frame), colour            */
    int16_t a_pos[8][16][2];  /* DS:CBC0  per particle: x, y                                 */
    int16_t a_ring;           /* DS:CDC0                                                     */
    /* ---- E: player shots (only the Y array is ported; the camera shifts it) ------------- */
    int16_t e_y[10];          /* DS:CEDA  shot Y, pixels (x at CEEE, power at CE9A: not ported yet) */
} FxState;

extern FxState g_fx;
void fx_level_reset(void);                 /* exactly what the level start clears (0BA5:4549..) */
void fx_reset(void);                       /* all pools empty (as the level start does) */
void fx_set_draw_callback(fx_draw_fn fn);
void fx_set_solid_callback(fx_draw_fn fn);                 /* hook for the silhouette blitter 0BA5:2896 */
void fx_draw_solid(int a0, int a1, int x, int y, int a4, int a5);
void fx_draw(int a0, int a1, int x, int y, int a4, int a5);   /* same hook, for other modules (0BA5:249E calls) */

void fx_spawn_score(int x, int y, int type);   /* 3684: type 0-4 -> variant 0, 5-9 -> variant 1 */
void fx_update_scores(void);                    /* 36F0 */
void fx_spawn_puff(int x, int y);               /* 2174 (uses the RNG) */
void fx_update_puffs(void);                     /* C update */
void fx_spawn_sparkle(int x, int y);            /* 21C6 */
void fx_update_sparkles(void);
void fx_spawn_burst(int x, int y);              /* 2F1C: x, y in pixels (uses the RNG: 4 calls per particle) */
void fx_update_bursts(void);                    /* 2FF3 */                  /* 20B1 (draws to the current draw page, uses the RNG) */
#endif
