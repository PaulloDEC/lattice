/* frame.c - pieces of the level frame loop (see frame.h) */
#include <string.h>
#include "frame.h"
#include "input.h"
#include "level.h"
#include "enemy.h"
#include "effects.h"
#include "camera.h"
#include "sprite.h"
#include "rng.h"
#include "map.h"

Joy g_joy;
int16_t g_joy_enabled, g_demo_playback;
const uint8_t *g_demo_data;
FrameState g_frame;

void frame_controls(int mode)
{
    if (g_frame.local_c) g_frame.local_c--;
    if (mode == 1) { g_frame.rec[3] = g_frame.rec[4] = g_frame.rec[0] = g_frame.rec[1] = g_frame.rec[2] = 0; }
    if (mode == 2) {
        memcpy(g_frame.rec, g_demo_data + g_frame.demo_pos * 6 + 2, 6);
        g_in.fire = g_frame.rec[3]; g_in.jump = g_frame.rec[4]; g_in.left = g_frame.rec[1];
        g_in.right = g_frame.rec[2]; g_in.up = g_frame.rec[0];
        g_in.latch_fire = g_in.fire ? 0 : 1;
        g_in.latch_up = g_in.up ? 0 : 1;
        g_frame.demo_pos++;
    }
    g_frame.fire_held = (g_in.latch_fire == 0);
    g_frame.up_held = (g_in.latch_up == 0);
    if (!g_lv.teleport && !g_lv.death_timer && !g_lv.finish_timer && g_joy_enabled && !g_demo_playback) {
        g_in.down = g_in.up = g_in.right = g_in.left = 0;
        if (g_joy.left) g_in.left = 1;
        if (g_joy.right) g_in.right = 1;
        if (g_joy.up && g_in.latch_up) { g_in.up2 = 1; g_in.up = 1; g_in.latch_up = 0; g_frame.up_held = 1; }
        if (!g_joy.up) { g_in.latch_up = 1; g_frame.up_held = 0; g_in.up2 = 0; }
        if (g_joy.down) g_in.down = 1;
        int fire = g_joy.swap ? g_joy.btn2 : g_joy.btn1;
        int jump = g_joy.swap ? g_joy.btn1 : g_joy.btn2;
        g_frame.fire_held = fire ? 1 : 0;
        if (!fire) { g_in.fire = 0; g_in.latch_fire = 1; }
        if (fire && g_in.latch_fire) { g_in.fire = 1; g_in.latch_fire = 0; }
        if (!jump) { g_in.jump = 0; g_in.latch_jump = 1; }
        if (jump && g_in.latch_jump) { g_in.jump = 1; g_in.latch_jump = 0; }
    }
    /* fire */
    if (!g_lv.teleport && g_in.fire) {
        if (mode == 1) g_frame.rec[3] = 1;
        if (!g_lv.potion_timer) g_in.fire = 0;
        g_shots.cursor = 0;
        while (g_shots.cursor < 10 && g_shots.x[g_shots.cursor] != -99) g_shots.cursor++;
        if (g_shots.cursor < 10 && g_shots.count < g_lv.mana) {
            int s = g_shots.cursor;
            g_shots.count++;
            if (g_lv.ce94) { g_lv.ce94--; g_shots.power[s] = 1; } else g_shots.power[s] = 0;
            if (g_frame.up_held) {
                g_shots.up[s] = 1; g_shots.x[s] = (int16_t)(g_cam.pscreen_x + 1); g_fx.e_y[s] = g_cam.pscreen_y;
            } else {
                g_shots.up[s] = 0;
                g_shots.x[s] = (int16_t)(g_lv.facing ? g_cam.pscreen_x - 2 : g_cam.pscreen_x + 2);
                g_fx.e_y[s] = (int16_t)(g_cam.pscreen_y + g_spr.hdr[0][25]);
            }
            g_shots.dir[s] = g_lv.facing;
            g_shots.cursor++;
            if (g_shots.cursor > 4) g_shots.cursor = 0;
            game_sound(g_lv.potion_timer ? 0xD : 0);
        }
    }
    if (g_lv.potion_timer) g_lv.potion_timer--;
    if (g_lv.potion_timer == 1) { g_lv.potion_timer = 0; g_lv.mana = g_lv.potion_saved; }
}

/* ---- player shot pool update: 0BA5:5824..5F62 ------------------------------------------------------------------ */
uint8_t g_shot_block_tile;   /* DS:6D6B  foreground tile id that a shot destroys */

static uint8_t fg_rd(int idx) { return (g_map.fg && (uint16_t)idx < 240 * 60) ? g_map.fg[(uint16_t)idx] : 0xFF; }
static void fg_wr(int idx, uint8_t v) { if (g_map.fg && (uint16_t)idx < 240 * 60) g_map.fg[(uint16_t)idx] = v; }
static void shot_free(int s) { g_shots.x[s] = -99; g_shots.count--; }

/* shot s hit a breakable tile at idx: clear it, spawn a puff at (px, py), free the shot. Returns 1 on hit. */
static int shot_tile_hit(int s, int idx, int px, int py)
{
    if (fg_rd(idx) != g_shot_block_tile) return 0;
    fg_wr(idx, 0xFF);
    fx_spawn_puff(px, py);
    shot_free(s);
    return 1;
}

void shots_update(void)
{
    int c2 = 2 * g_cam.col_odd;
    for (int s = 0; s < SHOT_SLOTS; s++) {
        if (g_shots.x[s] == -99) continue;
        int x = g_shots.x[s], y = g_fx.e_y[s];
        uint16_t di = (uint16_t)((int16_t)((y / 16 + g_cam.cam_row) * 240) + x / 4 + g_cam.cam_col);
        if (g_shots.up[s]) {
            if (g_shots.dir[s]) di++;
            int16_t d = (int16_t)(di - 0xF0);
            if (d < 0) d += 0xF0;
            di = (uint16_t)d;
            uint16_t p = (uint16_t)(di - g_lv.facing);
            if (shot_tile_hit(s, p, x - 1 - c2, y - 15)) continue;
            if (shot_tile_hit(s, p + 1, x + 3 - c2, y - 15)) continue;
            if (fg_rd(di) != 0xFF) { fx_spawn_burst(x * 4 + 8, y + 2); shot_free(s); continue; }
            g_fx.e_y[s] = (int16_t)(y - 0x10);
            y = g_fx.e_y[s];
            if (x > 0x4F || y < 0) { shot_free(s); continue; }
            g_rec_tag = TAG_SHOT + s;
            fx_draw(0, (int16_t)g_spr.hdr[0][23], x, y, g_shots.dir[s],
                    (int16_t)(g_spr.hdr[0][26] + (g_shots.power[s] ? 3 : 1)));
            g_rec_tag = 0;
            if (rng_next(10) > 2) fx_spawn_sparkle(x + 2 * g_lv.facing, y + 5);
            continue;
        }
        di++;
        if (!g_shots.dir[s]) {                       /* flying right */
            if (shot_tile_hit(s, di,        x + 2 - c2, y - 8)) continue;
            if (shot_tile_hit(s, di + 0xF0, x + 2 - c2, y + 8)) continue;
            if (shot_tile_hit(s, di + 1,    x + 6 - c2, y - 8)) continue;
            if (shot_tile_hit(s, di + 0xF1, x + 6 - c2, y + 8)) continue;
            if (fg_rd(di) != 0xFF || fg_rd(di + 1) != 0xFF) { fx_spawn_burst(x * 4 + 0x18, y + 0xA); shot_free(s); continue; }
            x += 4; g_shots.x[s] = (int16_t)x;
            if (x > 0x4F || y < 0) { shot_free(s); continue; }
            g_rec_tag = TAG_SHOT + s;
            fx_draw(0, (int16_t)g_spr.hdr[0][23], x, y, g_shots.dir[s],
                    (int16_t)(g_spr.hdr[0][26] + (g_shots.power[s] ? 2 : 0)));
            g_rec_tag = 0;
            if (rng_next(10) > 2) fx_spawn_sparkle(x, y + 5);
        } else {                                      /* flying left */
            if (shot_tile_hit(s, di,        x - c2,     y - 8)) continue;
            if (shot_tile_hit(s, di + 0xF0, x - c2,     y + 8)) continue;
            if (shot_tile_hit(s, di - 1,    x - 2 - c2, y - 8)) continue;
            if (shot_tile_hit(s, di + 0xEF, x - 2 - c2, y + 8)) continue;
            if (fg_rd(di - 1) != 0xFF || fg_rd(di) != 0xFF) { fx_spawn_burst(x * 4 + 1, y + 2); shot_free(s); continue; }
            x -= 4; g_shots.x[s] = (int16_t)x;
            if (x < -3 || y < 0) { shot_free(s); continue; }
            g_rec_tag = TAG_SHOT + s;
            fx_draw(0, (int16_t)g_spr.hdr[0][23], x, y, g_shots.dir[s],
                    (int16_t)(g_spr.hdr[0][26] + (g_shots.power[s] ? 2 : 0)));
            g_rec_tag = 0;
            if (rng_next(10) > 2) fx_spawn_sparkle(x, y + 5);
        }
    }
}

/* ---- player movement: 0BA5:5F6A..6700 ------------------------------------------------------------------------- */
MoveState g_mv;
const int16_t g_jump_tab_normal[19] = {-16, -8, -8, -4, -4, -4, -2, -1, -1, 0, 1, 1, 2, 4, 4, 4, 8, 8, 16};
const int16_t g_jump_tab_power[25]  = {-16, -16, -16, -16, -8, -8, -4, -4, -4, -2, -1, -1, 0, 1, 1, 2, 4, 4, 4, 8, 8, 16, 16, 16, 16};

static int fgfree(int idx) { return fg_rd(idx) == 0xFF; }
static uint16_t cell_at(int row, int x2) { return (uint16_t)(row * 240 + x2); }
static void walk_anim(void)
{
    g_mv.moved = 1;
    g_lv.ce92++;
    if (g_lv.ce92 > (int16_t)g_spr.hdr[0][18]) g_lv.ce92 = (int16_t)g_spr.hdr[0][17];
}

void player_move(int mode)
{
    int16_t *py = &g_cam.player_y, *px = &g_cam.player_x;
    if (!g_lv.teleport) {
        /* start a jump */
        if (g_in.jump && !g_cam.vfreeze && !g_cam.vscroll && !g_mv.fall) {
            if (mode == 1) g_frame.rec[4] = 1;
            game_sound(0xF);
            g_in.jump = 0; g_cam.vfreeze = 1; g_mv.jump_idx = 0;
            if (!g_lv.item4_flag) { g_mv.jump_tab = g_jump_tab_normal; g_mv.jump_peak = 9; g_mv.jump_len = 0x13; }
            else { g_lv.item4_flag = 0; g_mv.jump_tab = g_jump_tab_power; g_mv.jump_peak = 0xC; g_mv.jump_len = 0x19; game_sound(5); }
        }
        g_frame.jump_end = 0;
        /* advance the jump arc */
        if (g_cam.vfreeze && !g_lv.death_timer && !g_lv.finish_timer) {
            if (g_mv.jump_idx < g_mv.jump_len) {
                *py = (int16_t)(*py + g_mv.jump_tab[g_mv.jump_idx]);
                g_cam.player_row = (int16_t)(*py / 16);
                uint16_t di = cell_at(g_cam.player_row, (*px + 1) / 2);
                if (!fgfree(di)) {
                    g_mv.jump_idx = (int16_t)(g_mv.jump_len - 1 - g_mv.jump_idx);
                    *py = (int16_t)(*py + g_mv.jump_tab[g_mv.jump_idx]);
                    g_cam.player_row = (int16_t)(*py / 16);
                }
                g_mv.jump_idx++;
                if (g_mv.jump_idx >= g_mv.jump_len) { g_cam.vfreeze = 0; g_frame.jump_end = 1; }
            } else { g_cam.vfreeze = 0; g_frame.jump_end = 1; }
        }
        /* floor under the player */
        uint16_t di = (uint16_t)(cell_at(g_cam.player_row, (*px + 1) / 2) + 0x1E0);
        if (!fgfree(di)) {
            if (g_cam.vfreeze && g_mv.jump_idx >= g_mv.jump_peak) {
                g_cam.vfreeze = 0; *py = (int16_t)(g_cam.player_row << 4); game_sound(0xF);
            }
            g_mv.fall = 0;
            if (g_frame.landed) { g_frame.landed = 0; game_sound(0xF); }
        } else if (!g_cam.vfreeze && !g_lv.death_timer && !g_lv.finish_timer) {
            *py = (int16_t)(*py + 0x10);
            if (fgfree((uint16_t)(di + 0xF0))) { g_mv.fall = 1; g_frame.landed = 1; }
        }
        if (g_frame.jump_end && !g_mv.fall) game_sound(0xF);
        /* side clearance flags */
        g_mv.step_r_up = g_mv.step_l_up = g_mv.free_r = g_mv.free_l = 0;
        int rem = *py % 16;
        for (int side = 0; side < 2; side++) {
            di = cell_at(g_cam.player_row, side == 0 ? *px / 2 : (*px + 2) / 2);
            int free = fgfree(di) && fgfree((uint16_t)(di + 0xF0)) && (rem == 0 || fgfree((uint16_t)(di + 0x1E0)));
            int step = fgfree((uint16_t)(di - 0xF0)) && fgfree(di) && !fgfree((uint16_t)(di + 0xF0));
            if (side == 0) { g_mv.free_l = free; g_mv.step_l_up = step; }
            else           { g_mv.free_r = free; g_mv.step_r_up = step; }
        }
        if (g_mv.fall || g_cam.vfreeze) { g_mv.step_r_up = 0; g_mv.step_l_up = 0; }
        g_mv.moved = 0;
        /* left */
        if (g_in.left && *px != 0) {
            if (mode == 1) g_frame.rec[1] = 1;
            if (!g_lv.facing) g_lv.facing = 1;
            else if (g_mv.free_l) { (*px)--; walk_anim(); }
            else if (g_mv.step_l_up) { (*px)--; *py = (int16_t)(*py - 0x10); walk_anim(); }
        }
        /* right */
        if (g_in.right && *px < 0x1DD) {
            if (mode == 1) g_frame.rec[2] = 1;
            if (g_lv.facing) g_lv.facing = 0;
            else if (g_mv.free_r) { (*px)++; walk_anim(); }
            else if (g_mv.step_r_up) { (*px)++; *py = (int16_t)(*py - 0x10); walk_anim(); }
        }
        /* lift platforms */
        if (!g_lv.hint_shown && g_mv.plat_tile_b != -1) {
            di = (uint16_t)(cell_at(g_cam.player_row, (*px + 1) / 2) + 0x1E0);
            int t = fg_rd(di);
            if (t == g_mv.plat_tile_b || t == g_mv.plat_tile_a) {
                if (t == g_mv.plat_tile_a) di--;
                if (g_in.up2 || g_in.up) {
                    if (mode == 1) g_frame.rec[0] = 1;
                    di = (uint16_t)(di - 0x2D0);
                    g_in.up = 0; g_in.latch_up = 1;
                    if (fgfree(di) && fgfree((uint16_t)(di + 1))) {
                        *py = (int16_t)(*py - 0x10);
                        di = (uint16_t)(di + 0x2D0);
                        uint16_t t2 = (uint16_t)(di - 0xF0);
                        fg_wr(t2, fg_rd(di)); fg_wr(di, 0xFF);
                        fg_wr(t2 + 1, fg_rd(di + 1)); fg_wr(di + 1, 0xFF);
                        g_mv.plat_up = 0;
                    }
                } else if (g_in.down) {
                    di = (uint16_t)(di + 0xF0);
                    if (fgfree(di) && fgfree((uint16_t)(di + 1))) {
                        *py = (int16_t)(*py + 0x10);
                        uint16_t t2 = (uint16_t)(di - 0xF0);
                        fg_wr(di, fg_rd(t2)); fg_wr(t2, 0xFF);
                        fg_wr(di + 1, fg_rd(t2 + 1)); fg_wr(t2 + 1, 0xFF);
                        g_mv.plat_down = 0;
                    }
                }
            }
        }
        if (!g_mv.moved && !g_frame.fire_held) g_lv.ce92 = (int16_t)g_spr.hdr[0][15];
    } else {
        /* teleport slide */
        if (g_lv.tele_col2 == *px && g_lv.tele_row_px == *py) {
            if (g_lv.teleport == 0x19) game_sound(5);
            if (g_lv.teleport == 0x28) g_lv.teleport = 0; else g_lv.teleport++;
        } else if (g_lv.teleport < 0x19) g_lv.teleport++;
        if (g_lv.teleport == 0x19) {
            if (*py < g_lv.tele_row_px) *py = (int16_t)(*py + 0x10);
            if (*py > g_lv.tele_row_px) *py = (int16_t)(*py - 0x10);
            if (*px < g_lv.tele_col2) (*px)++;
            if (*px > g_lv.tele_col2) (*px)--;
        }
    }
    /* look up / down margin */
    if (g_in.up2) { if (++g_mv.plat_up > 10) g_mv.plat_up = 10; } else g_mv.plat_up = 0;
    if (g_in.down) { if (++g_mv.plat_down > 10) g_mv.plat_down = 10; } else g_mv.plat_down = 0;
    if ((g_joy.scroll_down || g_in.scroll_down || g_mv.plat_down) && g_cam.vmargin > 0 && g_mv.look_off == 0) { g_cam.vmargin--; g_mv.look_off = 0; }
    if ((g_joy.scroll_up || g_in.scroll_up || g_mv.plat_up) && g_cam.vmargin < 8 && g_mv.look_off == 0) { g_cam.vmargin++; g_mv.look_off = 0; }
    if (!g_joy.scroll_down && !g_joy.scroll_up && !g_in.scroll_down && !g_in.scroll_up && g_mv.plat_up != 10 && g_mv.plat_down != 10) g_cam.vmargin = 5;
}

/* ---- player sprite, sparkles, shot indicators: 0BA5:50B1..53FF ---------------------------------------------------- */
#include "player.h"
static void default_blit(int which, int a0, int a1, int a2, int a3, int a4, int a5)
{
    switch (which) {
    case 0x249E: sprite_draw(a0, a1, a2, a3, a4, a5); break;
    case 0x2896: sprite_solid(1, a0, a1, a2, a3, a4, a5); break;
    case 0x26B3: sprite_solid(0, a0, a1, a2, a3, a4, a5); break;
    case 0x27E2: sprite_solid_raw(1, a0, a1, a2, a3, a4); break;
    case 0x25FF: sprite_solid_raw(0, a0, a1, a2, a3, a4); break;
    case 0x23BA: sprite_draw_raw(a0, a1, a2, a3, a4); break;
    }
}
static blit_fn s_blit = default_blit;
void frame_set_blit_hook(blit_fn fn) { s_blit = fn ? fn : default_blit; }

void player_draw(void)
{
    int x = g_cam.pscreen_x, y = g_cam.pscreen_y, dir = g_lv.facing;
    if (g_player.shield) {
        if (--g_player.shield == 0) game_sound(5);
        return;
    }
    if (!g_lv.teleport) {
        if (g_frame.fire_held) {
            if (g_frame.up_held && !g_mv.moved) g_lv.ce92 = (int16_t)(g_spr.hdr[0][21] + 1);
            else if (!g_mv.moved) g_lv.ce92 = (int16_t)g_spr.hdr[0][21];
        }
        if (g_cam.vfreeze) g_lv.ce92 = (int16_t)g_spr.hdr[0][19];
        if (g_cam.vfreeze && g_mv.jump_idx > g_mv.jump_peak) g_lv.ce92 = (int16_t)g_spr.hdr[0][20];
        if (g_mv.fall) g_lv.ce92 = (int16_t)g_spr.hdr[0][20];
        int fr = g_lv.ce92, wrap = (int16_t)g_spr.hdr[0][13];
        g_rec_tag = TAG_PLAYER;
        if (y < 0) s_blit(0x249E, 0, wrap, x, y, dir, fr);
        if (g_player.invuln % 2 != 0) {
            if (y < 0) s_blit(0x2896, 0, wrap, x, y, dir, fr); else s_blit(0x27E2, 0, x, y, dir, fr, 0);
        } else if (g_lv.potion_timer % 2 != 0) {
            if (y < 0) s_blit(0x26B3, 0, wrap, x, y, dir, fr); else s_blit(0x25FF, 0, x, y, dir, fr, 0);
        } else {
            if (y < 0) s_blit(0x249E, 0, wrap, x, y, dir, fr); else s_blit(0x23BA, 0, x, y, dir, fr, 0);
        }
    } else {
        int t = g_lv.teleport, k = -1, yy = y;
        if (t < 5) k = 0; else if (t < 0xA) k = 1; else if (t < 0xF) k = 2; else if (t < 0x14) k = 3;
        else if (t <= 0x19) { int r1 = rng_next(2); int r2 = rng_next(2); yy = y + r1 - r2; k = 4; }
        else if (t < 0x1E) k = 3; else if (t < 0x23) k = 2; else if (t < 0x28) k = 1;
        g_rec_tag = TAG_TELEPORT;
        if (k >= 0) s_blit(0x23BA, 2, x, yy, dir, k, 0);
    }
    g_rec_tag = 0;
    if (g_lv.item4_timer) {
        int r1 = rng_next(10); int r2 = rng_next(7);
        fx_spawn_puff(x + r2 - 2, y + r1 + 0x14);
        if (!g_lv.item4_flag) g_lv.item4_timer--;
    }
    if (g_lv.ce94) {
        if (g_lv.ce98) {
            int n = g_lv.ce94 > 3 ? 3 : g_lv.ce94;
            for (int i = 0; i < n; i++) {
                g_rec_tag = TAG_PICON + i;
                s_blit(0x249E, 0, (int16_t)g_spr.hdr[0][23], x + 1, y - 0xE - i * 0xE, dir, (int16_t)(g_spr.hdr[0][26] + 2));
                g_rec_tag = 0;
            }
        }
        if (--g_lv.ce96 == 0) { g_lv.ce96 = 5; g_lv.ce98 = g_lv.ce98 ? 0 : 1; }
    }
}

/* ---- death / level complete countdowns: 0BA5:4E8B..50B1 ------------------------------------------------------------ */
static void default_event(int ev) { if (ev == 2) enemy_kill_all(); }
static event_fn s_event = default_event;
void frame_set_event_hook(event_fn fn) { s_event = fn ? fn : default_event; }

static void death_blast(void)
{
    int x = g_cam.pscreen_x, y = g_cam.pscreen_y;
    for (int k = 0; k < 2; k++) {
        int a = rng_next(10), b = rng_next(10), c = rng_next(10), d = rng_next(10);
        fx_spawn_burst(x * 4 + c + 0xC - d, y + a + 0x10 - b);
        int e = rng_next(12), f = rng_next(12), g = rng_next(3), h = rng_next(3);
        fx_spawn_puff(x + g - h, y + e - f);
    }
    game_sound(0xA);
}

void level_end_states(void)
{
    if (g_player.health == 0 && !g_lv.death_timer) {
        g_lv.death_timer = 0x5A; g_player.shield = 0x7530; s_event(1);
    }
    if (g_lv.death_timer) {
        g_lv.death_timer--;
        int r = rng_next(4);
        if ((r == 0 && g_lv.death_timer > 0x19) || g_lv.death_timer == 0x59) death_blast();
    }
    if (g_lv.items_left == g_lv.lives && !g_lv.finish_timer) {
        g_lv.finish_timer = 0x5A; g_player.shield = 0x7530; s_event(1); s_event(2);
    }
    if (g_lv.finish_timer) {
        g_lv.finish_timer--;
        if (rng_next(7) == 0 && g_lv.finish_timer > 0x19) {
            for (int k = 0; k < 2; k++) {
                int a = rng_next(12), b = rng_next(12), c = rng_next(3), d = rng_next(3);
                fx_spawn_puff(g_cam.pscreen_x + c - d, g_cam.pscreen_y + a - b);
            }
            game_sound(9);
        }
    }
}

/* ---- screen flash: 0BA5:220F ---------------------------------------------------------------------------------- */
#include "vga.h"
#include "video.h"
void level_flash(void)
{
    video_set_latch_mode(0);
    vga_memset(video_draw_offset(), 0x3200, (uint8_t)(0x77 - g_lv.flash));
    g_lv.flash--;
    video_set_latch_mode(1);
}
