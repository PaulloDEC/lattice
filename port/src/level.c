/* level.c - player / tile interaction, port of 0BA5:39F3.  See level.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "level.h"
#include "input.h"
#include "camera.h"
#include "effects.h"
#include "map.h"
#include "enemy.h"
#include "player.h"
#include "rng.h"
#include "sprite.h"
#include "video.h"
#include "vga.h"

LevelState g_lv;
TileItem   g_tile_items[23] = {      /* the original's initial data (DS:2628); names omitted */
    {100,0,0,0,{0}},{250,0,0,0,{0}},{500,0,0,0,{0}},{1000,0,0,0,{0}},{0,10,0,0,{0}},{0,0,0,1,{0}},{0,0,0,2,{0}},{0,0,1,0,{0}},
    {0,0,0,3,{0}},{0,0,0,4,{0}},{0,0,0,5,{0}},{0,0,0,6,{0}},{0,0,0,7,{0}},{0,0,0,8,{0}},{0,0,0,9,{0}},{0,0,0,10,{0}},{0,0,0,11,{0}},
    {0,0,0,12,{0}},{0,0,0,0,{0}},{0,0,0,0,{0}},{0,0,0,0,{0}},{0,0,0,0,{0}},{0,0,0,0,{0}} };
Teleporter g_teleporters[10];
Lever      g_levers[23];
Door       g_doors1[25], g_doors2[25];
Hint       g_hints[10];
int16_t    g_spawn_groups[250][16];

static hint_fn hint_cb;
void level_set_hint_callback(hint_fn fn) { hint_cb = fn; }

#define MAP_CELLS (MAP_COLS * 60)
/* out-of-range cells (the original reads whatever lies past the layer) count as "nothing" / are ignored */
static int16_t info_get(int c) { c &= 0xFFFF; return (c < MAP_CELLS) ? g_map_info[c] : 0x7530; }
static void info_set(int c, int16_t v) { c &= 0xFFFF; if (c < MAP_CELLS) g_map_info[c] = v; }
static uint8_t layer_get(const uint8_t *l, int c) { c &= 0xFFFF; return (l && c < MAP_CELLS) ? l[c] : 0xFF; }
static void layer_set(uint8_t *l, int c, uint8_t v) { c &= 0xFFFF; if (l && c < MAP_CELLS) l[c] = v; }

/* the rectangle fill shared by levers and doors: for every cell, row-major */
static void open_rect(int c0, int r0, int c1, int r1, int mode /* 0: remove fg, 1: swap in alt */)
{
    for (int r = r0; r1 >= r; r++)
        for (int c = c0; c1 >= c; c++) {
            int cell = (int16_t)(r * MAP_COLS) + c;
            if (mode == 0) { if (layer_get(g_map.fg, cell) != 0xFF) layer_set(g_map.dyn, cell, 1); }
            else           { if (layer_get(g_map.alt, cell) != 0xFF) layer_set(g_map.dyn, cell, 2); }
        }
}

void level_player_tiles(void)
{
    if (g_player.invuln) g_player.invuln--;
    for (int i = -1; i <= 1; i++) {
        int cell = (int16_t)((int16_t)(g_cam.player_row * 240) + (g_cam.player_x + 1) / 2 + i);
        int sx = (int16_t)(g_cam.pscreen_x - 1);
        int sy = (int16_t)(g_cam.pscreen_y - 16);
        g_lv.hint_shown = 0;
        for (int k = 0; k < 2; k++, cell = (int16_t)(cell + 240), sy = (int16_t)(sy + 16)) {
            int v = info_get(cell);
            int keep = 0;
            if (v == 0x7530) continue;
            if (v < 0x17) {
                if (v < 0) continue;      /* cannot occur (the original would index before the table) */
                const TileItem *t = &g_tile_items[v];
                int kind = t->kind;
                if (t->health && g_player.health < 100) { game_sound(1); goto common; }
                if (t->health) { keep = 1; goto common; }
                if (t->points) {
                    g_lv.collected++;
                    if (g_lv.collected == g_lv.goal) { g_lv.complete = 1; game_sound(9); }
                    switch (t->points) {
                        case 100:  fx_spawn_score(sx, sy, 0); break;
                        case 250:  fx_spawn_score(sx, sy, 1); break;
                        case 500:  fx_spawn_score(sx, sy, 2); break;
                        case 1000: fx_spawn_score(sx, sy, 3); break;
                        case 5000: fx_spawn_score(sx, sy, 4); break;
                    }
                    game_sound(rng_next(2) + 2);
                    goto common;
                }
                switch (kind) {
                case 1:
                    g_lv.lives++; g_lv.flash = 8; game_sound(4);
                    break;
                case 2:
                    keep = 1;
                    if (!g_player.invuln && !g_player.shield && i == 0) {
                        g_player.health += g_damage_by_difficulty[g_difficulty]; g_player.invuln = 20; game_sound(8);
                    }
                    break;
                case 9:
                    if (!g_player.invuln && !g_player.shield && i == 0) {
                        g_player.health += g_damage_by_difficulty[g_difficulty]; g_player.invuln = 10; game_sound(8);
                    }
                    keep = 1;
                    break;
                case 3:
                    g_player.shield = 400;
                    fx_spawn_puff(g_cam.pscreen_x - 1, g_cam.pscreen_y);
                    fx_spawn_puff(g_cam.pscreen_x + 4, g_cam.pscreen_y);
                    fx_spawn_puff(g_cam.pscreen_x - 1, g_cam.pscreen_y + 16);
                    fx_spawn_puff(g_cam.pscreen_x + 4, g_cam.pscreen_y + 16);
                    g_player.invuln = 0; game_sound(5); fx_spawn_score(sx, sy, 8);
                    break;
                case 4:
                    if (!g_lv.item4_flag && i == 0) {
                        g_lv.item4_flag = 1; g_lv.item4_timer = 0x19; game_sound(9); fx_spawn_score(sx, sy, 7);
                    } else keep = 1;
                    break;
                case 5:
                    if (i != 0) keep = 1;
                    else {
                        if (!g_lv.potion_timer) { g_lv.potion_saved = g_lv.mana; g_lv.mana = 10; }
                        g_lv.potion_timer = 0x258; game_sound(9); fx_spawn_score(sx, sy, 9);
                    }
                    break;
                case 7: g_lv.key1 = 1; game_sound(9); fx_spawn_score(sx, sy, 5); break;
                case 8: g_lv.key2 = 1; game_sound(9); fx_spawn_score(sx, sy, 5); break;
                case 10:
                    keep = 1;
                    if (g_in.up) {
                        for (int j = 0; j < 10; j++) {
                            if (g_hints[j].row == g_cam.player_row && abs(g_hints[j].col - g_cam.player_tile) < 3) {
                                video_set_latch_mode(0); video_set_map_mask(0xF);
                                game_sound(5);
                                if (hint_cb) hint_cb(j);
                                g_lv.hint_shown = 1;
                                video_set_latch_mode(1); video_set_map_mask(0xF);
                            }
                        }
                        g_in.up = 0;
                        sx = 10;   /* the original reuses the screen-x local as the loop counter: it is 10 afterwards */
                    }
                    break;
                case 11:
                    if (i != 0) keep = 1;
                    else { g_lv.ce94 += 3; game_sound(9); g_lv.ce98 = 1; g_lv.ce96 = 5; }
                    break;
                case 12:
                    g_lv.check_x = g_cam.player_x; g_lv.check_y = g_cam.player_y; keep = 1;
                    info_set(cell, 0x7530); layer_set(g_map.bg, cell, g_lv.tile_blank);
                    info_set(cell + 1, 0x7530); layer_set(g_map.bg, cell + 1, g_lv.tile_blank);
                    game_sound(9);
                    break;
                }
            common:
                if (t->mana) game_sound(9);
                g_lv.mana = (int16_t)(g_lv.mana + t->mana);
                if (g_lv.mana > 10) g_lv.mana = 10;
                if (g_lv.potion_timer) {
                    g_lv.potion_saved = (int16_t)(g_lv.potion_saved + t->mana);
                    if (g_lv.potion_saved > 10) g_lv.potion_saved = 10;
                }
                g_lv.score += (uint32_t)(int32_t)t->points;
                g_player.health = (int16_t)(g_player.health + t->health);
                if (!keep) { info_set(cell, 0x7530); layer_set(g_map.bg, cell, g_lv.tile_blank); }
            } else if (v < 0x21) {                                   /* teleporter */
                if (g_teleporters[v - 23].from == cell && i == 0) {
                    int dest = g_teleporters[v - 23].to;
                    g_lv.teleport = 1;
                    g_lv.tele_row_px = (int16_t)((dest / 240) << 4);
                    g_lv.tele_col2 = (int16_t)((dest % 240) << 1);
                    g_lv.ce92 = g_spr.hdr[0][15];
                    g_cam.vfreeze = 0;
                    g_cam.player_y = (int16_t)((g_cam.player_y / 16) << 4);
                    info_set(cell, 0x7530); layer_set(g_map.bg, cell, g_lv.tile_blank);
                    game_sound(5); fx_spawn_score(sx, sy, 6);
                }
            } else if (v < 0x38) {                                   /* lever */
                if (g_in.up && i == 0) {
                    g_in.up = 0;
                    uint8_t cur = layer_get(g_map.bg, cell);
                    layer_set(g_map.bg, cell, cur == g_lv.tile_lever_a ? g_lv.tile_lever_b : g_lv.tile_lever_a);
                    Lever *lv = &g_levers[v - 0x21];
                    int j;
                    for (j = 0; j < 4; j++) if (lv->cell[j] != -1) { j = 99; break; }
                    if (j == 99) {
                        int ok = 1;
                        for (j = 0; j < 4; j++) {
                            if (lv->cell[j] == -1) continue;
                            if (lv->expect[j] == g_lv.tile_lever_a && layer_get(g_map.bg, lv->cell[j]) != g_lv.tile_lever_a) ok = 0;
                            if (lv->expect[j] == g_lv.tile_lever_b && layer_get(g_map.bg, lv->cell[j]) != g_lv.tile_lever_b) ok = 0;
                        }
                        if (ok) {
                            game_sound(7); g_map.timer = 0x19;
                            open_rect(lv->col0, lv->row0, lv->col1, lv->row1, lv->mode != 0);
                            for (j = 0; j < 4; j++) lv->cell[j] = -1;
                        } else game_sound(6);
                    } else game_sound(6);
                }
            } else if (v < 0x6a) {                                   /* doors */
                const Door *d = (v < 0x51) ? &g_doors1[v - 0x38] : &g_doors2[v - 0x51];
                int mode = (v < 0x51) ? 1 : 0;                       /* type 1 swaps in the alternate tile, type 2 removes the fg */
                int use = 0;
                if (d->kind == 0) use = 1;
                if (d->kind == 1 && g_lv.key1) use = 2;
                if (d->kind == 2 && g_lv.key2) use = 3;
                if (use) {
                    game_sound(7); g_map.timer = 10;
                    open_rect(d->col0, d->row0, d->col1, d->row1, mode);
                    info_set(cell, 0x7530);
                    layer_set(g_map.bg, cell, d->kind == 0 ? d->tile : g_lv.tile_blank);
                    if (use > 1)
                        fx_spawn_puff(((cell % 240) - g_cam.cam_col) << 2, ((cell / 240) - g_cam.cam_row) << 4);
                }
                if (use == 2) g_lv.key1 = 0;
                if (use == 3) g_lv.key2 = 0;
            } else if (v >= 0x74 && v <= 0x16d && i == 0) {         /* spawn trigger */
                for (int m = 0; m < 8; m++) {
                    int c = g_spawn_groups[v - 0x74][8 + m];   /* record = 8 words of definitions, then the 8 map cells */
                    if (info_get(c) == 0x7530) continue;
                    for (int j = 0; j < 32; j++) {
                        if (g_en.pending[j] != -1) continue;
                        int ok = 1;
                        for (int q = 0; q < 8; q++) if (g_en.cell[q] == c) ok = 0;
                        for (int q = 0; q < 32; q++) if (g_en.pending[q] == c) ok = 0;
                        if (ok) { g_en.pending[j] = c; break; }
                    }
                }
            }
        }
    }
}

/* ---- HUD ---------------------------------------------------------------------------------------- */
#define PAGE3 (3 * VIDEO_PAGE_STRIDE)       /* DS:645C: the page holding the HUD icons */

static void icon_copy(uint16_t dest, uint16_t src, int rows)
{
    uint16_t d = (uint16_t)(video_draw_offset() + 0), cx = src, dx = dest;
    for (int r = 0; r < rows; r++) {
        vga_write((uint16_t)(d + dx), vga_read((uint16_t)(PAGE3 + cx))); dx++; cx++;
        vga_write((uint16_t)(d + dx), vga_read((uint16_t)(PAGE3 + cx)));
        dx = (uint16_t)(dx + 0x4f); cx = (uint16_t)(cx + 0x4f);
    }
}
void hud_icon8(int dest, int icon)  { icon_copy((uint16_t)dest, (uint16_t)(2 * icon + 0x39D0), 8); }
void hud_icon11(int dest, int icon) { icon_copy((uint16_t)dest, (uint16_t)(2 * icon + 0x39E6), 11); }

void level_count_items(void)
{
    g_lv.items_left = 0;
    for (int c = 0; c < 0x3840; c++) {
        int16_t v = g_map_info[c];
        if (v >= 0x17) continue;
        if (v >= 0 && g_tile_items[v].kind == 1) g_lv.items_left++;
    }
}

void hud_draw(void)
{
    char buf[16];
    int di, si;
    if (g_lv.complete) {
        hud_icon8(0x392a, g_lv.complete < 10 ? 10 : g_lv.level_icon + 1);
        g_lv.complete++;
        if (g_lv.complete > 0x14) g_lv.complete = 1;
    } else hud_icon8(0x392a, g_lv.level_icon + 1);
    snprintf(buf, sizeof buf, "%d", (int)(int32_t)g_lv.score);
    di = (int)strlen(buf);
    for (si = 0; si < di; si++) hud_icon8(0x389a - di + 2 * si, (uint8_t)buf[si] - 0x30);
    if (g_player.health > 100) g_player.health = 100;
    if (g_player.health < 0) g_player.health = 0;
    snprintf(buf, sizeof buf, "%d", g_player.health);
    di = (int)strlen(buf);
    for (si = 0; si < 3; si++) hud_icon8(0x38a6 + 2 * si, 10);
    for (si = 0; si < di; si++) hud_icon8(0x38a9 - di + 2 * si, (uint8_t)buf[si] - 0x30);
    snprintf(buf, sizeof buf, "%d", g_lv.lives);
    hud_icon8(0x38b7, (uint8_t)buf[0] - 0x30);
    snprintf(buf, sizeof buf, "%d", g_lv.items_left);
    hud_icon8(0x38bb, (uint8_t)buf[0] - 0x30);
    hud_icon11(0x3875, 2);
    hud_icon11(0x3877, 2);
    if (g_lv.key1 + g_lv.key2 > 1) { hud_icon11(0x3875, 0); hud_icon11(0x3877, 1); }
    if (g_lv.key1 + g_lv.key2 == 1) hud_icon11(0x3876, g_lv.key1 ? 0 : 1);
}
