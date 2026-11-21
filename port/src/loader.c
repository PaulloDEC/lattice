#include "loader.h"
#include "dat.h"
#include "level.h"
#include "map.h"
#include "enemy.h"
#include "camera.h"
#include "frame.h"
#include "video.h"
#include "sprite.h"
#include "player.h"
#include "rng.h"
#include "timer.h"
#include <stdlib.h>
#include <string.h>

#define CELLS 14400
int16_t g_episode;
int16_t g_enemies_total;
int16_t g_level_start[4];

static uint8_t s_tiles[724];                 /* DS:6D68: 4 constants, then 240 x {first, last, type} */
static uint8_t s_dyn[CELLS], s_alt[CELLS], s_fg[CELLS], s_bg[CELLS];
static int16_t s_info[CELLS];
static int16_t s_defs[120];

_Static_assert(sizeof(Teleporter) * 10 == 40, "teleporter table");
_Static_assert(sizeof(Lever) * 23 == 506, "lever table");
_Static_assert(sizeof(Door) * 25 == 300, "door table");
_Static_assert(sizeof(Hint) * 10 == 5040, "hint table");
_Static_assert(sizeof(g_spawn_groups) == 8000, "spawn table");

static int rd(int type, int base, void *dst, uint32_t cap) { return dat_read(DAT_MAP + 36 * type + base, dst, cap) ? 0 : -1; }

int level_load_data(void)
{
    int idx = g_episode * 10 + g_lv.level_icon;
    if (idx < 0 || idx >= 40) return -1;
    int base = g_lvl_map_base[idx];
    if (rd(0, base, g_level_start, sizeof g_level_start) || rd(1, base, s_tiles, sizeof s_tiles) || rd(2, base, g_hints, sizeof g_hints) ||
        rd(3, base, g_teleporters, sizeof g_teleporters) || rd(4, base, g_levers, sizeof g_levers) || rd(5, base, g_doors1, sizeof g_doors1) ||
        rd(6, base, g_doors2, sizeof g_doors2) || rd(7, base, s_defs, sizeof s_defs) || rd(8, base, g_spawn_groups, sizeof g_spawn_groups) ||
        rd(9, base, s_bg, CELLS) || rd(10, base, s_fg, CELLS) || rd(11, base, s_alt, CELLS) || rd(12, base, s_info, sizeof s_info)) return -1;
    g_enemy_fire_chance = g_level_start[3];
    g_lv.tile_blank = s_tiles[0]; g_lv.tile_lever_a = s_tiles[1]; g_lv.tile_lever_b = s_tiles[2]; g_shot_block_tile = s_tiles[3];
    g_map.anim = s_tiles + 4; g_map.dyn = s_dyn; g_map.alt = s_alt; g_map.fg = s_fg; g_map.bg = s_bg;
    g_map_info = s_info; g_enemy_defs = s_defs;
    memset(s_dyn, 0, CELLS);
    g_cam.player_x = (int16_t)(g_level_start[1] * 2); g_cam.player_y = (int16_t)(g_level_start[2] << 4);
    g_lv.check_x = g_cam.player_x; g_lv.check_y = g_cam.player_y;
    g_lv.collected = 0; g_lv.goal = 0;
    for (int i = 0; i < CELLS; i++) { int v = s_info[i]; if (v >= 0 && v <= 0x16 && g_tile_items[v].points != 0) g_lv.goal++; }
    g_lv.complete = 0; g_kills = 0; g_enemies_total = 0;
    for (int i = 0; i < CELLS; i++) { int v = s_info[i]; if (v >= 0x6A && v <= 0x73 && s_defs[(v - 0x6A) * 12 + 1] != -1) g_enemies_total++; }
    return 0;
}

LevelHooks g_level_hooks;
int16_t g_sprite_ids[10];
int16_t g_demo_len;
uint8_t g_demo_buf[0x4000];

static void d_music(int entry, int loop) { (void)entry; (void)loop; }
static void d_pcx(int entry, int apply) { uint32_t n; uint8_t *p = dat_load(entry, &n); if (p) { video_load_pcx(p, n, apply); free(p); } }
static void d_pal(int entry, int slot, int apply) { uint32_t n; uint8_t *p = dat_load(entry, &n); if (p) { video_load_palette(p, n, slot, apply); free(p); } }
static void d_img(int entry, int x, int y) { uint32_t n; uint8_t *p = dat_load(entry, &n); if (p) { video_load_img(p, n, x, y); free(p); } }
static void d_sprites(const int16_t ids[10]) { uint32_t n; uint8_t *p = dat_load(DAT_SPRITES, &n); if (p) { sprite_load_sheets(p, n, ids); free(p); } }
static void d_input(void) { }
static void d_demo(int entry) { dat_read(entry, g_demo_buf, sizeof g_demo_buf); }

#define H(f) (g_level_hooks.f ? g_level_hooks.f : d_##f)

int level_setup(int mode)
{
    int epi = g_episode, lvl = g_lv.level_icon, ix = epi * 10 + lvl;
    if (ix < 0 || ix >= 40) return -1;
    g_lv.mana = 1;
    g_mv.plat_tile_b = g_lvl_plat_tiles[epi * 20 + lvl * 2];
    g_mv.plat_tile_a = g_lvl_plat_tiles[epi * 20 + lvl * 2 + 1];
    g_frame.fire_held = 0; g_frame.up_held = 0; g_frame.demo_pos = 0;
    g_demo_playback = (mode == 2);
    video_set_draw_page(0); video_clear_draw_page();
    video_set_draw_page(1); video_clear_draw_page();
    video_show_page_now(1);
    H(music)(0x258 + g_lvl_music[epi * 9 + lvl], 1);
    g_frame.anim_toggle = 0;
    g_lv.flash = 0; g_lv.teleport = 0; g_player.invuln = 0; g_shots.count = 0; g_player.shield = 0;
    g_lv.item4_flag = 0; g_lv.potion_timer = 0; g_lv.death_timer = 0; g_frame.landed = 0;
    g_lv.key1 = 0; g_lv.key2 = 0; g_lv.item4_timer = 0; g_lv.ce94 = 0; g_mv.moved = 0;
    g_cam.vmargin = 5; g_mv.look_off = 0; g_shots.cursor = 0;
    for (int i = 0; i < SHOT_SLOTS; i++) g_shots.x[i] = -99;
    fx_level_reset(); enemy_reset();
    for (int i = 0; i < ENEMY_PENDING; i++) g_en.pending[i] = -1;
    for (int i = 0; i < ENEMY_SLOTS; i++) g_en.cell[i] = -1;
    for (int i = 0; i < PROJ_SLOTS; i++) g_proj.type[i] = -1;
    if (level_load_data()) return -1;
    video_set_draw_page(3);
    H(pcx)(DAT_TILESET + g_lvl_tileset[ix], 1);
    video_get_palette(video_target_palette(), 0, 256);
    sprite_pack_tiles();
    static const int16_t base_ids[4] = {0, 1, 3, 2};
    for (int i = 0; i < 4; i++) g_sprite_ids[i] = base_ids[i];
    for (int i = 0; i < 6; i++) { int16_t w0 = g_enemy_defs[i * 12]; g_sprite_ids[4 + i] = (w0 != -1) ? w0 : -1; }
    H(sprites)(g_sprite_ids);
    video_set_draw_page(3);
    video_adjust_draw_offset(0x7D0);
    H(pcx)(DAT_BACKDROP + g_lvl_backdrop[ix], 0);
    video_adjust_draw_offset(-0x7D0);
    H(pal)(DAT_PAL_LO, 0, 1);
    H(pal)(DAT_BACKDROP_PAL + g_lvl_backdrop[ix], 128, 1);
    H(img)(11, 0, 185); H(img)(14, 28, 185); H(img)(15, 60, 185);
    video_pal_fade_out(1);
    video_set_draw_page(0); H(img)(12, 0, 160);
    video_set_draw_page(1); H(img)(12, 0, 160);
    video_set_draw_page(0); H(img)(12, 0, 160);
    video_set_draw_page(1); H(img)(12, 0, 160);
    level_count_items();
    g_lv.lives = 0; g_player.health = 0x64;
    g_cam.player_x = (int16_t)(g_level_start[1] * 2); g_cam.player_y = (int16_t)(g_level_start[2] << 4);
    g_lv.ce92 = (int16_t)g_spr.hdr[0][15];
    g_lv.facing = 0; g_cam.vfreeze = 0; g_mv.fall = 0;
    g_cam.snap = 1; camera_update(); g_cam.snap = 0;
    if (mode == 2) { H(demo)(0x12 + lvl / 2); g_demo_len = (int16_t)(g_demo_buf[0] | g_demo_buf[1] << 8); g_demo_data = g_demo_buf; }
    if (epi == 3 && lvl == 8) {
        g_boss_phase = 0;
        enemy_spawn(0, g_boss_init[0] << 2, g_boss_init[1] << 4, g_boss_init[2]);
        g_en.facing[0] = g_boss_init[3]; g_en.vx[0] = 0; g_en.c0c4[0] = 0x64; g_en.c0f4[0] = (int16_t)(rng_next(20) + 10); g_en.c0d4[0] = 0;
    }
    H(input)();
    rng_reset();
    video_set_draw_page(1); video_show_page_now(0);
    video_pal_apply();
    timer_reset(); g_enemy_sound_deadline = 0;
    return 0;
}
