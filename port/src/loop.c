#include "loop.h"
#include "ui.h"
#include "loader.h"
#include "dat.h"
#include "level.h"
#include "map.h"
#include "enemy.h"
#include "player.h"
#include "camera.h"
#include "frame.h"
#include "effects.h"
#include "input.h"
#include "video.h"
#include "sprite.h"
#include "timer.h"
#include <stdlib.h>
#include <string.h>

LoopHooks g_loop_hooks;
int16_t g_game_speed, g_music_on, g_sound_on, g_sound_hw;
uint32_t g_score_saved;            /* DS:704E  score at the start of the level (restored when the level is replayed) */

#define PAGE3 (3 * VIDEO_PAGE_STRIDE)
#define HK(f) (g_loop_hooks.f)

static void draw_world(void)
{
    uint16_t dest = video_draw_offset();
    uint16_t moff = (uint16_t)(g_cam.cam_row * 240 + g_cam.cam_col);
    if (g_cam.col_odd) map_draw_odd(dest, g_spr.src_off, g_map.fg, g_map.bg, moff);
    else map_draw_even(dest, g_spr.src_off, g_map.fg, g_map.bg, moff);
    uint16_t bsrc = (uint16_t)(PAGE3 + 0x7D0);
    if (g_cam.col_odd) backdrop_draw_odd(dest, bsrc, g_map.fg, g_map.bg, moff, g_cam.cam_x);
    else backdrop_draw_even(dest, bsrc, g_map.fg, g_map.bg, moff, g_cam.cam_x);
}

static void cb_draw(int a0, int a1, int a2, int a3, int a4, int a5) { sprite_draw(a0, a1, a2, a3, a4, a5); }
static void cb_solid(int a0, int a1, int a2, int a3, int a4, int a5) { sprite_solid(1, a0, a1, a2, a3, a4, a5); }
void loop_install_defaults(void) { fx_set_draw_callback(cb_draw); fx_set_solid_callback(cb_solid); sprite_init(); }

static void settings_changed(void) { if (HK(save_settings)) HK(save_settings)(); }
static void say(const char *t) { if (HK(message)) HK(message)(t); }

static int s_run_mode;

/* Run-ahead (Enhanced display smoothing, port only): one tick's game logic and drawing exactly as level_run's
 * frame body does it, but without the joystick poll, the pause picture, menus, hot keys and exit handling.
 * The caller saves the whole state first, makes the platform callbacks silent and restores everything after;
 * the drawing is recorded through the tick_begin hook and g_rec_* (sprite.h). */
void level_tick_ahead(void)
{
    int mode = s_run_mode;
    camera_update();
    video_set_latch_mode(1); video_set_map_mask(0xF);
    if (!g_lv.teleport) level_player_tiles();
    if (HK(tick_begin)) HK(tick_begin)();
    if (g_lv.flash) level_flash(); else draw_world();
    hud_draw();
    level_end_states();
    player_draw();
    enemy_shots_collide(); player_hit_by_projectiles(); player_hit_by_enemies(); enemy_spawn_pending();
    projectiles_update(); enemies_update(); enemy_despawn_offscreen();
    fx_update_bursts(); fx_update_scores(); fx_update_puffs(); fx_update_sparkles();
    if (g_frame.anim_toggle) map_animate();
    g_frame.anim_toggle = !g_frame.anim_toggle;
    frame_controls(mode);
    shots_update();
    player_move(mode);
}

int level_run(int mode, int pause)
{
    s_run_mode = mode;
    int ret = 0;                       /* bp-8 */
    int frame_no = 0;
    g_frame.local_c = 0;               /* bp-0xC (set once, not by a restart) */
    for (;;) {                         /* 4549: restart point */
        if (level_setup(mode)) return -1;
        if (HK(level_start)) HK(level_start)();
        g_fx.sprite_b = (int16_t)g_spr.hdr[1][13];    /* DS:C296 / C44E are words of the sprite headers (slots 1 and 3, word 13) */
        g_fx.sprite_c = (int16_t)g_spr.hdr[3][13];
        int exit_code = 0;             /* bp-0xB */
        while (!exit_code) {           /* 4D47 */
            if (HK(frame) && HK(frame)(frame_no++)) return -2;
            video_set_draw_page(video_draw_page() == 0);
            video_show_page_now(video_visible_page() == 0);
            if (HK(tick_shown)) HK(tick_shown)();
            timer_frame_wait(g_game_speed);
            if (HK(tick_waited)) HK(tick_waited)();
            if (g_joy_enabled && !g_demo_playback && HK(poll_joystick)) HK(poll_joystick)();
            camera_update();
            video_set_latch_mode(1); video_set_map_mask(0xF);
            if (!g_lv.teleport) level_player_tiles();
            if (HK(tick_begin)) HK(tick_begin)();
            if (g_lv.flash) level_flash(); else draw_world();
            hud_draw();
            level_end_states();
            player_draw();
            if (pause) {
                if (HK(tick_nosmooth)) HK(tick_nosmooth)();
                if (HK(input_off)) HK(input_off)();
                if (g_level_hooks.img) g_level_hooks.img(16, 12, 46);
                else { uint32_t n; uint8_t *p = dat_load(16, &n); if (p) { video_load_img(p, n, 12, 46); free(p); } }
                video_show_page(video_visible_page() == 0);
                if (HK(wait_key)) HK(wait_key)();
                if (g_level_hooks.input) g_level_hooks.input();
                pause = 0;
                video_show_page(video_visible_page() == 0);
            }
            enemy_shots_collide(); player_hit_by_projectiles(); player_hit_by_enemies(); enemy_spawn_pending();
            projectiles_update(); enemies_update(); enemy_despawn_offscreen();
            fx_update_bursts(); fx_update_scores(); fx_update_puffs(); fx_update_sparkles();
            if (g_frame.anim_toggle) map_animate();
            g_frame.anim_toggle = !g_frame.anim_toggle;
            frame_controls(mode);
            shots_update();
            player_move(mode);
            /* ---- system keys 6700..694D ---- */
            if (g_in.pause && g_frame.local_c == 0) { g_in.pause = 0; if (HK(pause_game)) HK(pause_game)(); g_frame.local_c = 2; }
            if (mode == 2 && g_frame.demo_pos >= g_demo_len) exit_code = 1;
            if (g_in.esc && g_frame.local_c == 0) {
                g_frame.local_c = 2;
                if (mode > 0) exit_code = 1;
                else {
                    int si = HK(quit_menu) ? HK(quit_menu)() : 0;
                    if (si == 1) { g_lv.death_timer = 0; g_lv.finish_timer = 0; exit_code = 2; }
                    if (si == 2) { ret = -1; exit_code = 1; }
                    if (si == 3) { g_lv.death_timer = 0; g_lv.finish_timer = 0; exit_code = 3; }
                }
            }
            if (g_in.f2) { if (HK(menu_f2)) HK(menu_f2)(); g_in.f2 = 0; }
            if (g_in.f3) {
                if (HK(menu_f3) && HK(menu_f3)()) { if (HK(input_off)) HK(input_off)(); goto restart; }
                g_in.f3 = 0;
            }
            if (g_in.f10) { if (HK(menu_f10) && HK(menu_f10)()) { ret = -1; exit_code = 1; } g_in.f10 = 0; }
            if (g_in.f1) { if (HK(menu_f1)) HK(menu_f1)(1); g_in.f1 = 0; }
            if (g_in.hot_c) { if (g_joy_enabled && HK(menu_c)) HK(menu_c)(); g_in.hot_c = 0; }
            if (g_in.hot_v) { if (HK(menu_v)) HK(menu_v)(); g_in.hot_v = 0; }
            if (g_in.hot_m && g_frame.local_c == 0) {
                g_frame.local_c = 2; g_in.hot_m = 0;
                g_music_on = !g_music_on;
                if (g_sound_hw) {
                    if (!g_music_on) { if (HK(music_off)) HK(music_off)(); say("Music is now off"); }
                    else { say("Music is now on"); if (HK(music)) HK(music)(0x258 + g_lvl_music[g_episode * 9 + g_lv.level_icon], 1); }
                }
                if (!g_sound_hw) g_music_on = 0;
                settings_changed();
            }
            if (g_in.hot_s && g_frame.local_c == 0) {
                g_frame.local_c = 2; g_in.hot_s = 0;
                g_sound_on = !g_sound_on; settings_changed();
                say(g_sound_on ? "Sound effects are now on" : "Sound effects are now off");
            }
            if (g_lv.death_timer == 1) exit_code = 1;
            if (g_lv.finish_timer == 1) exit_code = 1;
            if (mode == 1) {
                if (g_in.up) g_frame.rec[0] = 1;
                size_t at = 2 + (size_t)(uint16_t)g_frame.demo_pos * 6;
                if (at + 6 <= sizeof g_demo_buf) memcpy(g_demo_buf + at, g_frame.rec, 6);
                g_frame.demo_pos++;
            }
        }
        /* ---- exit sequences 694D..6A4D ---- */
        if (HK(input_off)) HK(input_off)();
        g_exit_clock = timer_clock();
        if (mode == 1) {
            g_demo_buf[0] = (uint8_t)g_frame.demo_pos; g_demo_buf[1] = (uint8_t)((uint16_t)g_frame.demo_pos >> 8);
            if (HK(save_demo)) HK(save_demo)((uint16_t)g_frame.demo_pos * 6 + 2);
        }
        video_pal_fade_out(0x14);
        if (exit_code != 2) {
            if (HK(palette_entry)) { HK(palette_entry)(DAT_PAL_LO, 0); HK(palette_entry)(DAT_PAL_HI, 128); }
            if (HK(music)) HK(music)(0x25A, 1);
        }
        if (g_lv.finish_timer == 1) {
            if (HK(end_screen) && HK(end_screen)(1, g_lv.goal, g_lv.collected) == 1) exit_code = 2;
            ret = 1;
        }
        if (g_lv.death_timer == 1) {
            if (HK(end_screen)) HK(end_screen)(0, g_lv.goal, g_lv.collected);
            ret = 0;
        }
        if (exit_code == 2) { g_lv.score = g_score_saved; g_lv.mana = 1; continue; }
        if (exit_code == 3) continue;
        if (HK(release)) HK(release)(timer_clock());
        return ret;
    restart: ;
    }
}
