#include "enemy.h"
#include <stddef.h>
#include "camera.h"
#include "map.h"
#include "video.h"
#include "vga.h"
#include "effects.h"
#include "rng.h"
#include "player.h"
#include "rng.h"
#include "sprite.h"
#include "timer.h"

Enemies g_en;
const int16_t *g_enemy_defs;
int16_t g_difficulty;
uint32_t g_enemy_sound_deadline;

Shots g_shots;
int16_t g_kills;
int16_t *g_map_info;

static sound_fn snd;
void game_set_sound_callback(sound_fn fn) { snd = fn; }
void game_sound(int id) { if (snd) snd(id); }

#define I16(v) ((int16_t)(v))

void enemy_reset(void)
{
    for (int i = 0; i < ENEMY_SLOTS; i++) g_en.type[i] = -1;
}

static int16_t abs16(int16_t v) { int16_t s = I16(v >> 15); return I16((v ^ s) - s); }

/* vx = r(2) - r(2); vy = (r(2) - r(2)) << 2; types 2, 3 repeat while both are zero;
 * type 6 repeats while vx alone is zero (original 0BA5:0827 only tests vx) */
static void random_velocity(int s, int vx_only)
{
    do {
        int a = rng_next(2); int b = rng_next(2);
        g_en.vx[s] = I16(a - b);
        a = rng_next(2); b = rng_next(2);
        g_en.vy[s] = I16((a - b) << 2);
    } while (g_en.vx[s] == 0 && (vx_only || g_en.vy[s] == 0));
}

int enemy_spawn(int def, int x, int y, int cell)
{
    int s;
    for (s = 0; s < ENEMY_SLOTS; s++) if (g_en.type[s] == -1) break;
    if (s == ENEMY_SLOTS) return 0;

    int16_t di = I16(def), xx = I16(x), yy = I16(y);
    int16_t sheet = I16(di + 4);
    const int16_t *d = g_enemy_defs + di * 12;
    g_en.bf64[s] = 0; g_en.bf84[s] = 0x14; g_en.c124[s] = 0;
    g_en.c0d4[s] = d[4]; g_en.sheet[s] = sheet; g_en.def[s] = di; g_en.type[s] = d[11];
    g_en.c114[s] = d[6]; g_en.c104[s] = d[7]; g_en.c0e4[s] = d[8]; g_en.c0f4[s] = d[5]; g_en.c0c4[s] = d[10];
    g_en.x[s] = xx; g_en.y[s] = yy;
    int in = (sheet >= 0 && sheet < SPR_SLOTS);
    g_en.box_h[s] = in ? (int16_t)g_spr.hdr[sheet][14] : 0;
    int16_t hp = d[1];
    if (hp >= 0) hp = I16(hp + (g_difficulty << 1));
    g_en.hp[s] = hp; g_en.hp_max[s] = hp;
    g_en.box_w[s] = in ? (int16_t)g_spr.hdr[sheet][13] : 0;
    g_en.facing[s] = (xx > g_cam.player_xcol) ? 1 : 0;
    if (g_en.type[s] == 2) g_en.facing[s] = 0;
    g_en.c034[s] = 0; g_en.flash[s] = 0;
    g_en.cell[s] = I16(cell);
    g_en.bfc4[s] = 1; g_en.bfb4[s] = 1; g_en.bf94[s] = 0; g_en.bfa4[s] = -99;

    int near = abs16(I16(xx - g_cam.player_xcol)) < 0x32 && abs16(I16(yy - g_cam.player_y)) < 0x64;
    uint32_t now = timer_clock();
    if (now > g_enemy_sound_deadline && near) {
        game_sound(0xE);
        g_enemy_sound_deadline = now + 5;
    }

    switch (g_en.type[s]) {
    case 8: g_en.vx[s] = xx; g_en.c0c4[s] = I16(rng_next(10) + 10); break;
    case 2: case 3: case 6:
        random_velocity(s, g_en.type[s] == 6); g_en.timer[s] = I16(rng_next(0x14)); break;
    case 0: case 1: case 7:
        g_en.vx[s] = g_en.facing[s] ? -1 : 1; g_en.vy[s] = 0; break;
    case 4:
        g_en.vx[s] = 0; g_en.vy[s] = 0; g_en.facing[s] = 0; g_en.c034[s] = 0; game_sound(0xB); break;
    case 5:
        g_en.vx[s] = 0; g_en.vy[s] = 0; g_en.c034[s] = 0; break;
    default: break;
    }
    return 1;
}

/* 0BA5:2E9D: try to spawn every pending cell.  Info word 0x7530 = no enemy; otherwise
 * word - 106 is the enemy definition.  A successful spawn frees the pending entry. */
void enemy_spawn_pending(void)
{
    for (int i = 0; i < ENEMY_PENDING; i++) {
        int16_t cell = g_pending_cell[i];
        if (cell == -1) continue;
        int16_t info = g_map_info[cell];
        int16_t row = I16(cell / MAP_COLS), col = I16(cell % MAP_COLS);   /* signed idiv, as the original */
        if (info == 0x7530) continue;
        if (enemy_spawn(I16(info - 106), I16(col << 2), I16(row << 4), cell)) g_pending_cell[i] = -1;
    }
}

/* enemy centre in pixels for the spark bursts; box edges in screen units */
typedef struct { int16_t x0, x1, y0, y1, cx, cy; } Box;
static Box enemy_box(int s)
{
    Box b;
    b.x0 = I16(g_en.x[s] - (g_cam.cam_x << 1));
    b.x1 = I16(b.x0 + g_en.box_w[s] - 1);
    b.y0 = I16(g_en.y[s] - (g_cam.cam_row << 4));
    b.y1 = I16(b.y0 + g_en.box_h[s] - 1);
    b.cx = I16((b.x0 + g_en.box_w[s] / 2) << 2);      /* C division truncates like cdq/sub/sar */
    b.cy = I16(b.y0 + g_en.box_h[s] / 2);
    return b;
}

static void enemy_die(int s)
{
    g_en.type[s] = -1;
    g_map_info[g_en.cell[s]] = 0x7530;   /* the map cell is cleared for good */
    g_kills++;
}

void enemy_kill_all(void)
{
    for (int s = 0; s < ENEMY_SLOTS; s++) {
        if (g_en.type[s] == -1) continue;
        Box b = enemy_box(s);
        fx_spawn_burst(b.cx, b.cy);
        game_sound(0xA);
        enemy_die(s);
    }
}

void enemy_shots_collide(void)
{
    for (int s = 0; s < ENEMY_SLOTS; s++) {
        if (g_en.type[s] == -1) continue;
        if (g_en.bf84[s] != 0) continue;
        Box b = enemy_box(s);
        for (int d = 0; d < SHOT_SLOTS; d++) {
            int16_t sx = g_shots.x[d], sy = I16(g_fx.e_y[d] + 2);
            if (sx == -99) continue;
            if (sx < b.x0 || sx > b.x1 || sy < b.y0 || sy > b.y1) continue;
            fx_spawn_burst(b.cx, b.cy);
            if (g_shots.power[d] == 0) { g_shots.x[d] = -99; g_shots.count--; }
            if (g_en.hp[s] == -2) { g_en.hp[s] = -1; enemy_kill_all(); continue; }
            if (g_en.hp[s] == -1) continue;
            if (g_en.hp[s] != 0 && g_shots.power[d] == 0) { g_en.hp[s]--; g_en.flash[s] = 10; continue; }
            game_sound(0xA);
            fx_spawn_burst(b.cx, b.cy);
            fx_spawn_burst(b.cx, b.cy);
            fx_spawn_burst(b.cx, b.cy);   /* 4 bursts in all with the hit burst above */
            enemy_die(s);     /* the shot loop goes on with the same box, as in the original */
        }
    }
}

void enemy_despawn_offscreen(void)
{
    for (int s = 0; s < ENEMY_SLOTS; s++) {
        if (g_en.type[s] == -1) continue;
        int16_t left = I16((g_cam.cam_x << 1) - 0x32), right = I16((g_cam.cam_x << 1) + 0x78);
        int16_t top = I16((g_cam.cam_row << 4) - 0x78), bottom = I16((g_cam.cam_row << 4) + 0x1FE);
        int off = g_en.x[s] < left || g_en.x[s] > right || g_en.y[s] < top || g_en.y[s] > bottom;
        if (off && g_en.hp_max[s] < 500) { g_en.type[s] = -1; g_en.cell[s] = -1; }
    }
}

void enemy_draw_bar(int slot)
{
    video_set_latch_mode(0);
    video_set_map_mask(0xF);
    int16_t hp = g_en.hp[slot], hpmax = g_en.hp_max[slot];
    /* The original does FILD hp; FILD hp_max; FDIVP; FMUL 78.0f; ftol (truncate) in x87 80-bit
     * extended precision and keeps the low 16 bits.  long double is 80-bit on x86 gcc/MinGW. */
    int16_t len = 0;
    if (hpmax != 0) {
        long double v = ((long double)hp / (long double)hpmax) * 78.0L;
        if (v > -9.0e18L && v < 9.0e18L) len = (int16_t)(int64_t)v;   /* out of range = x87 "indefinite", low bits 0 */
    }
    for (int i = 0; i < len; i++) {
        uint8_t c = (uint8_t)(0x6F - i / 10);
        uint16_t base = video_draw_offset();
        vga_write((uint16_t)(base + i + 0x2F81), c);
        vga_write((uint16_t)(base + i + 0x2FD1), c);
        vga_write((uint16_t)(base + i + 0x3021), c);
    }
}

/* spawn animation shared by type 8 and the boss: 1 in 5 frames a dust puff near the centre, bf84-- */
/* flat access to the enemy table: slot indexes past 7 run into the next array, as in DS memory */
#define EF(field, s) (((int16_t *)&g_en)[offsetof(Enemies, field) / 2 + (s)])

static void spawn_dust(int s, int16_t sx, int16_t sy)
{
    int16_t cx = I16(sx + EF(box_w, s) / 2), cy = I16(sy + EF(box_h, s) / 2);
    if (rng_next(5) == 0) {
        int a = rng_next(12), b = rng_next(12);
        int16_t py = I16(cy + a - b);
        a = rng_next(3); b = rng_next(3);
        fx_spawn_puff(I16(cx + a - b), py);
    }
    EF(bf84, s)--;
}

void enemy_update_type8(int s)
{
    int16_t sheet = g_en.sheet[s];
    int16_t wrap = (sheet >= 0 && sheet < SPR_SLOTS) ? (int16_t)g_spr.hdr[sheet][13] : 0;
    enemy_draw_bar(s);
    int16_t sx = I16(g_en.x[s] - (g_cam.cam_x << 1));
    int16_t sy = I16(g_en.y[s] - (g_cam.cam_row << 4));
    if (g_en.bf84[s] != 0) { spawn_dust(s, sx, sy); return; }   /* spawning: dust around the centre */
    int16_t frame = g_en.c034[s], anim, di;
    if (frame < 2) { g_en.facing[s] = 0; di = frame; } else { g_en.facing[s] = 1; di = I16(frame - 2); }
    anim = g_en.facing[s];
    if (g_en.flash[s] % 2 != 0) fx_draw_solid(sheet, wrap, sx, sy, anim, di);     /* 2896 */
    else                        fx_draw(sheet, wrap, sx, sy, anim, di);           /* 249E */
    if (g_en.flash[s] != 0) g_en.flash[s]--;
    if (g_en.c034[s] == 1 && rng_next(10) == 0 && g_en.x[s] > g_cam.player_xcol) {
        projectile_spawn(g_en.x[s], g_en.y[s], 1, 0, g_en.def[s], sheet);
        game_sound(0xC);
    }
    if (g_en.c0c4[s] == 0) {
        g_en.c0c4[s] = I16(rng_next(10) + 10);
        int16_t anchor = g_en.vx[s];
        if (g_en.x[s] < anchor) {
            int d = anchor - g_en.x[s];
            if (d >= 5 || rng_next(2) == 0) { g_en.x[s]++; g_en.c034[s]--; }
            else                            { g_en.x[s]--; g_en.c034[s]++; }
        } else {
            int d = g_en.x[s] - anchor;
            if (d >= 5 || rng_next(2) == 0) { g_en.x[s]--; g_en.c034[s]++; }
            else                            { g_en.x[s]++; g_en.c034[s]--; }
        }
    }
    if (g_en.c034[s] > 3) g_en.c034[s] = 0;
    if (g_en.c034[s] < 0) g_en.c034[s] = 3;
    g_en.c0c4[s]--;
}

/* DS:2BB6..2BE0 read as one flat block, exactly as the original indexes it (phase 4 reads past the end of
 * the x/y/cell/facing arrays into the next table; the game never reaches phase 4):
 *   x[ph] = t[ph], y[ph] = t[4+ph], cell[ph] = t[8+ph], facing[ph] = t[12+ph], hp[ph] = t[15+ph], threshold[ph] = t[16+ph] */
static const int16_t boss_tab[21] = { 128, 128, 113, 128,  54, 48, 42, 36,  13088, 11648, 10193, 8768,
                                      1, 1, 0,  1,  600, 400, 200, -1, -16 };
int16_t g_boss_phase;

void enemy_update_boss(void)
{
    const int s = 0;
    int16_t sheet = g_en.sheet[s];
    int16_t wrap = (sheet >= 0 && sheet < SPR_SLOTS) ? (int16_t)g_spr.hdr[sheet][13] : 0;
    enemy_draw_bar(s);
    if (g_en.bf84[s] != 0) {
        spawn_dust(s, I16(g_en.x[s] - (g_cam.cam_x << 1)), I16(g_en.y[s] - (g_cam.cam_row << 4)));
        return;
    }
    g_en.facing[s] = g_en.x[s] > g_cam.player_xcol ? 1 : 0;
    int16_t sx = I16(g_en.x[s] - (g_cam.cam_x << 1)), sy = I16(g_en.y[s] - (g_cam.cam_row << 4));
    if (g_en.flash[s] % 2 != 0) fx_draw_solid(sheet, wrap, sx, sy, g_en.facing[s], g_en.c034[s]);
    else                        fx_draw(sheet, wrap, sx, sy, g_en.facing[s], g_en.c034[s]);
    if (g_en.flash[s] != 0) g_en.flash[s]--;

    if (g_en.c0f4[s] == 0) {
        if (g_en.c0d4[s] == 0)      { g_en.c034[s] = 1; g_en.c0d4[s] = 1; g_en.c0f4[s] = 4; }
        else if (g_en.c0d4[s] == 1) {
            g_en.c0d4[s] = 2; g_en.c0f4[s] = 1;
            projectile_spawn(g_en.x[s], g_en.y[s], 1, 0, g_en.def[s], sheet);
        } else if (g_en.c0d4[s] == 2) {
            g_en.c034[s] = 0; g_en.c0d4[s] = 0; g_en.c0f4[s] = I16(rng_next(0x2D) + 5);
        }
    } else {
        g_en.c0f4[s]--;
    }

    int ph = g_boss_phase;
    if (!(boss_tab[16 + ph] > g_en.hp[s])) return;       /* hp still above this phase's threshold */
    if (ph >= 4) return;
    if (g_en.c0d4[s] < 3) {                              /* start dying */
        g_en.c0d4[s] = 3; g_en.c0f4[s] = 0x14; g_en.c034[s] = 2; game_sound(5);
        return;
    }
    if (g_en.c0f4[s] != 0) return;                       /* still dying */
    ph = ++g_boss_phase;
    g_en.type[s] = -1;
    enemy_spawn(0, I16(boss_tab[ph] << 2), I16(boss_tab[4 + ph] << 4), boss_tab[8 + ph]);
    g_en.hp[s] = boss_tab[15 + ph];
    g_en.facing[s] = boss_tab[12 + ph];
    g_en.vx[s] = 0;
    g_en.c0f4[s] = I16(rng_next(0x32) + 5);
    g_en.c0d4[s] = 0;
}

int16_t g_enemy_fire_chance = 1;

/* foreground layer byte; outside the layer the original reads whatever follows it in memory (never
 * with valid levels); here that counts as empty (0xFF) */
static uint8_t fg_at(uint16_t idx) { return (g_map.fg && idx < 240 * 60) ? g_map.fg[idx] : 0xFF; }
static int16_t hdrw(uint8_t sb, int k) { return sb < SPR_SLOTS ? (int16_t)g_spr.hdr[sb][k] : 0; }

void enemies_update(void)
{
    /* These four locals are only computed on the non-jumping path; on other paths the original uses
     * whatever the previous slot left in them (stale stack values; 0 before the first slot here). */
    int16_t blocked = 0, cell = 0;
    uint8_t wcells = 0, hcells = 0, sb = 0;

    for (int s = 0; s < ENEMY_SLOTS; s++) {
        if (EF(type, s) == -1) continue;
        if (EF(type, s) == 8) { enemy_update_type8(s); continue; }
        if (EF(type, s) == 0x63) { enemy_update_boss(); continue; }
        if (EF(hp_max, s) > 0x14) enemy_draw_bar(s);
        if (EF(bf84, s) != 0) goto dust;
        sb = (uint8_t)EF(sheet, s);
        if (EF(bf64, s) != 0) { EF(bf64, s)--; goto fire_check; }     /* firing pose countdown */
        if (EF(bfb4, s) != 0) { EF(bfb4, s)--; goto fire_check; }     /* move delay countdown  */
        {
            int16_t ox = EF(x, s), oy = EF(y, s);
            if (EF(bfa4, s) != -99) {                                 /* mid-jump: vy accelerates to 7 */
                EF(y, s) = I16(EF(y, s) + EF(bfa4, s));
                EF(bfa4, s)++;
                if (EF(bfa4, s) == 7) { EF(bfa4, s) = -99; EF(c034, s) = 0; }
            } else {
                EF(x, s) = I16(EF(x, s) + EF(vx, s));
                EF(y, s) = I16(EF(y, s) + EF(vy, s));
                int16_t x = EF(x, s), y = EF(y, s);
                cell = I16((y / 16) * 240 + x / 4);
                if (EF(facing, s) == 0) cell++;
                wcells = (uint8_t)(EF(box_w, s) / 4); if (x % 4 != 0) wcells++;
                hcells = (uint8_t)(EF(box_h, s) / 16); if (y % 16 != 0) hcells++;
                blocked = 0;
                for (int r = 0; r < hcells; r++) {
                    uint16_t di = (uint16_t)((y / 16 + r) * 240 + x / 4);
                    for (int c = 0; c < wcells; c++, di++) if (fg_at(di) != 0xFF) blocked = 1;
                }
            }
            int16_t t = EF(type, s);
            if (t == 2 || t == 3 || t == 6) {                        /* flyers */
                if (EF(timer, s) <= 0) blocked = 1;
                if (t == 6) EF(facing, s) = EF(x, s) > g_cam.player_xcol ? 1 : 0;
                if (blocked) {
                    EF(x, s) = ox; EF(y, s) = oy;
                    EF(timer, s) = I16(rng_next(0x1E) + 5);
                    do {
                        int a, b;
                        if (t == 2 && EF(c0f4, s) != 0)               /* type 2 with def w5: drift toward the player */
                            EF(vx, s) = EF(x, s) > g_cam.player_xcol ? I16(-rng_next(2)) : I16(rng_next(2));
                        else { a = rng_next(2); b = rng_next(2); EF(vx, s) = I16(a - b); }
                        a = rng_next(2); b = rng_next(2); EF(vy, s) = I16((a - b) << 2);
                        if (t == 6) EF(vx, s) = I16(EF(vx, s) * 2);
                    } while (t == 6 ? EF(vx, s) == 0 : (EF(vx, s) == 0 && EF(vy, s) == 0));
                    if (t == 3) EF(facing, s) = EF(x, s) > g_cam.player_xcol ? 1 : 0;
                }
                EF(timer, s)--;
            } else if (t == 0 || t == 1 || (t == 7 && EF(bfa4, s) != -99)) {   /* walkers: turn at ledges */
                uint16_t di = (uint16_t)(cell + hcells * 240);
                if (EF(facing, s) == 0) di = (uint16_t)(di + wcells - 2);
                if (fg_at(di) == 0xFF) blocked = 1;
                if (blocked) {
                    EF(x, s) = ox; EF(y, s) = oy;
                    EF(facing, s) = !EF(facing, s);
                    EF(vx, s) = EF(facing, s) == 0 ? 1 : -1;
                }
                if (t == 0) {
                    int towards = (EF(x, s) <= g_cam.player_xcol && EF(facing, s) == 0) ||
                                  (EF(x, s) > g_cam.player_xcol && EF(facing, s) != 0);
                    if (towards && EF(c104, s) != 0 && rng_next(g_enemy_fire_chance) == 0) {
                        if (hdrw(sb, 21) != -1) EF(c034, s) = hdrw(sb, 21);
                        EF(bf64, s) = 9;                              /* firing pose for 9 frames */
                    } else if (rng_next(0x14) == 0) {
                        EF(facing, s) = EF(x, s) > g_cam.player_xcol ? 1 : 0;
                        EF(vx, s) = EF(facing, s) == 0 ? 1 : -1;
                    }
                } else if (t == 7) {
                    if (rng_next(0x14) == 0) {
                        EF(facing, s) = EF(x, s) > g_cam.player_xcol ? 1 : 0;
                        EF(vx, s) = EF(facing, s) == 0 ? 1 : -1;
                    }
                } else {                                              /* type 1 */
                    if (rng_next(0x14) == 0) {
                        EF(facing, s) = EF(x, s) > g_cam.player_xcol ? 1 : 0;
                        EF(bf94, s) = I16(rng_next(0xF) + 10);
                        if (hdrw(sb, 21) != -1) EF(c034, s) = hdrw(sb, 21);
                        EF(vx, s) = EF(facing, s) == 0 ? 1 : -1;
                    }
                }
            } else if (t == 7) {
                /* Type 7 on the ground: jump when a shot comes close.  Original bugs kept: the loop
                 * reuses the slot index as its counter (so shot i is compared with enemy i, may set the
                 * jump of the wrong slot, and the rest of this frame runs with slot 10 = the next arrays'
                 * slot 2, which also ends the loop), and the test is abs(shot_x - x < 10). */
                for (s = 0; s < SHOT_SLOTS; s++)
                    if (g_shots.x[s] != -99 && I16(g_shots.x[s] - EF(x, s)) < 10) EF(bfa4, s) = -6;
            } else if (t == 5) {
                EF(facing, s) = EF(x, s) > g_cam.player_xcol ? 1 : 0;
            }
            /* type 4 and others: nothing */

            /* animation: advance, then wrap inside the current animation range */
            EF(c034, s)++;
            if (EF(bf94, s) != 0) {
                if (hdrw(sb, 21) != -1) { if (hdrw(sb, 22) < EF(c034, s)) EF(c034, s) = hdrw(sb, 21); }
                else                    { if (hdrw(sb, 18) < EF(c034, s)) EF(c034, s) = hdrw(sb, 17); }
            } else if (EF(c124, s) != 0) {
                if (hdrw(sb, 22) < EF(c034, s)) { EF(c124, s) = 0; EF(c034, s) = hdrw(sb, 17); }
            } else {
                if (hdrw(sb, 18) < EF(c034, s)) EF(c034, s) = hdrw(sb, 17);
            }
            if (EF(bf94, s) == 0) EF(bfb4, s) = EF(bfc4, s);
            else { EF(bfb4, s) = 0; EF(bf94, s)--; }
        }
    fire_check:
        {
            int fire = 0;
            if (EF(c104, s) != 0 && rng_next(g_enemy_fire_chance) == 0 && EF(type, s) != 0) fire = 1;
            else if (EF(bf64, s) == 1) fire = 1;
            if (fire) {
                int16_t delay = 1;           /* original: hdr22 - hdr22 + 1 */
                int ok = 0;
                EF(c124, s) = 1;
                const int16_t *d = g_enemy_defs + EF(def, s) * 12;
                if (d[2] != 0) {
                    if (EF(c114, s) != 0) {                      /* only when facing the player */
                        if (EF(type, s) == 5) delay = I16(hdrw(sb, 22) - hdrw(sb, 21));
                        if ((EF(x, s) <= g_cam.player_xcol && EF(facing, s) == 0) ||
                            (EF(x, s) > g_cam.player_xcol && EF(facing, s) != 0))
                            ok = projectile_spawn(EF(x, s), EF(y, s), 1, delay, EF(def, s), sb);
                    } else {
                        ok = projectile_spawn(EF(x, s), EF(y, s), 1, delay, EF(def, s), sb);
                    }
                } else {                                         /* speed 0: dropped below the enemy */
                    ok = projectile_spawn(I16(EF(x, s) + EF(c0d4, s)), I16(EF(y, s) + EF(box_h, s)), 0, delay, EF(def, s), sb);
                }
                if (!ok) EF(c124, s) = 0;
                if (hdrw(sb, 21) != -1) EF(c034, s) = hdrw(sb, 21);
            }
        }
        if (EF(bfa4, s) != -99) EF(c034, s) = hdrw(sb, 19);    /* jump frame */
        {
            int16_t sx = I16(EF(x, s) - (g_cam.cam_x << 1)), sy = I16(EF(y, s) - (g_cam.cam_row << 4));
            if (EF(flash, s) % 2 != 0) fx_draw_solid(sb, hdrw(sb, 13), sx, sy, EF(facing, s), EF(c034, s));
            else                       fx_draw(sb, hdrw(sb, 13), sx, sy, EF(facing, s), EF(c034, s));
            if (EF(flash, s) != 0) EF(flash, s)--;
        }
    dust:
        if (EF(bf84, s) != 0)
            spawn_dust(s, I16(EF(x, s) - (g_cam.cam_x << 1)), I16(EF(y, s) - (g_cam.cam_row << 4)));
    }
}
