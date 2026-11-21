#include "player.h"
#include "camera.h"
#include "enemy.h"
#include "effects.h"
#include "map.h"
#include "rng.h"
#include "sprite.h"

#define I16(v) ((int16_t)(v))

Player g_player;
Projectiles g_proj;
const int16_t g_damage_by_difficulty[4] = { -4, -12, -16, 0 };

static int16_t damage(void) { return g_damage_by_difficulty[g_difficulty & 3]; }

typedef struct { int16_t px_hi, px_lo, py_hi, py_lo; } Body;
static Body body(void)
{
    Body b = { I16(g_cam.player_xcol + 3), I16(g_cam.player_xcol + 2), I16(g_cam.player_y + 0x19), I16(g_cam.player_y + 6) };
    return b;
}

void player_hit_by_projectiles(void)
{
    Body b = body();
    for (int i = 0; i < PROJ_SLOTS; i++) {
        if (g_proj.type[i] == -1 || g_proj.delay[i] != 0) continue;
        if (g_player.invuln != 0 || g_player.shield != 0) continue;
        if (g_proj.x[i] > b.px_hi) continue;
        if (I16(g_proj.x[i] + g_proj.w[i] - 1) < b.px_lo) continue;
        if (g_proj.y[i] > b.py_hi) continue;
        if (I16(g_proj.y[i] + g_proj.h[i] - 1) < b.py_lo) continue;
        g_player.health = I16(g_player.health + damage());
        g_player.invuln = 0x14;
        game_sound(8);
        g_proj.type[i] = -1;
    }
}

void player_hit_by_enemies(void)
{
    Body b = body();
    for (int i = 0; i < ENEMY_SLOTS; i++) {
        if (g_en.type[i] == -1 || g_player.shield != 0 || g_en.bf84[i] != 0) continue;
        if (g_en.x[i] > b.px_hi) continue;
        if (I16(g_en.x[i] + g_en.box_w[i] - 1) < b.px_lo) continue;
        if (g_en.y[i] > b.py_hi) continue;
        if (I16(g_en.y[i] + g_en.box_h[i] - 1) < b.py_lo) continue;
        if (g_en.hp_max[i] > 0x14 && g_difficulty != 3) { g_player.health = 0; continue; }  /* lethal contact */
        if (g_player.invuln != 0) continue;
        g_player.health = I16(g_player.health + damage());
        g_player.invuln = 0x14;
        game_sound(8);
    }
}

/* 0BA5:0A81.  Out-of-range tile lookups read garbage in the original (it never happens with
 * valid data); here they count as empty. */
void projectiles_update(void)
{
    for (int i = 0; i < PROJ_SLOTS; i++) {
        if (g_proj.type[i] == -1) continue;
        if (g_proj.delay[i] != 0) { g_proj.delay[i]--; continue; }
        int16_t sx = I16(g_proj.x[i] - (g_cam.cam_x << 1));
        int16_t sy = I16(g_proj.y[i] - (g_cam.cam_row << 4));
        int off = 0;
        if (sx < -10 || sx > 0x50 || sy < -0x14 || sy > 0xA0) off = 1;
        int row = g_proj.y[i] / 16, col = g_proj.x[i] / 4;          /* signed, truncating */
        int idx = row * 240 + col;
        if (g_map.fg && idx >= 0 && idx < 240 * 60 && g_map.fg[idx] != 0xFF) off = 2;
        if (off) {
            g_proj.type[i] = -1;
            if (off == 2) fx_spawn_burst(I16((sx << 2) + 8), I16(sy + 8));
            continue;
        }
        int sh = g_proj.sheet[i];
        g_rec_tag = TAG_PROJ + i;
        fx_draw(sh, (sh >= 0 && sh < SPR_SLOTS) ? (int16_t)g_spr.hdr[sh][13] : 0, sx, sy, g_proj.dir[i], g_proj.frame[i]);
        g_rec_tag = 0;
        if (g_proj.frame[i] < g_proj.last_frame[i]) g_proj.frame[i]++;
        if (g_proj.dir[i] == 0) g_proj.x[i] = I16(g_proj.x[i] + g_proj.speed[i]);
        else                    g_proj.x[i] = I16(g_proj.x[i] - g_proj.speed[i]);
        if (g_proj.homing[i]) {
            if (g_proj.x[i] < g_cam.player_xcol) g_proj.x[i]++;
            else if (g_proj.x[i] > g_cam.player_xcol) g_proj.x[i]--;
        }
        g_proj.y[i] = I16(g_proj.y[i] + g_proj.vy[i]);
        if (g_proj.jitter[i]) {
            int a = rng_next(2), b = rng_next(2);
            g_proj.y[i] = I16(g_proj.y[i] + (a - b));
        }
    }
}

int projectile_spawn(int x, int y, int aim, int delay, int def, int sheet)
{
    int i;
    for (i = 0; i < PROJ_SLOTS; i++) if (g_proj.type[i] == -1) break;
    if (i == PROJ_SLOTS) return 0;
    const int16_t *d = g_enemy_defs + def * 12;
    const uint16_t *h = (sheet >= 0 && sheet < SPR_SLOTS) ? g_spr.hdr[sheet] : g_spr.hdr[0];
    int16_t off = 0;
    g_proj.delay[i] = I16(delay);
    g_proj.dir[i] = 0;
    g_proj.jitter[i] = d[9];
    if (aim) {
        if (I16(x) <= g_cam.player_xcol) { off = (int16_t)h[13]; g_proj.dir[i] = 0; }
        else { off = 0; g_proj.dir[i] = 1; }
    }
    g_proj.w[i] = (int16_t)h[23];
    g_proj.h[i] = I16((int16_t)h[24] + 3);
    g_proj.frame[i] = (int16_t)h[26];
    g_proj.last_frame[i] = (int16_t)h[27];
    g_proj.type[i] = 0x7530;
    game_sound(0xD);
    g_proj.x[i] = I16(x + off);
    g_proj.y[i] = I16(y + (int16_t)h[25]);
    g_proj.sheet[i] = I16(sheet);
    g_proj.speed[i] = d[2];
    g_proj.vy[i] = d[3];
    g_proj.homing[i] = d[5];
    g_proj.aux[i] = 0;
    return 1;
}
