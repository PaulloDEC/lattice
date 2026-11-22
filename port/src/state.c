/* state.c - whole-game-state save / restore (see state.h).
 * The list below is every writable variable of the game modules, from `nm` on their objects (2026-11-21):
 * add a line here whenever a game module gains a global or static that the game changes while playing.
 * Not saved on purpose: hook / callback pointers, the DAT file handle, the timer (run-ahead never waits),
 * load-time constants that never change during a level (sprite streams, key names), and the platform layer. */
#include "state.h"
#include "camera.h"
#include "effects.h"
#include "enemy.h"
#include "frame.h"
#include "input.h"
#include "level.h"
#include "loader.h"
#include "loop.h"
#include "map.h"
#include "player.h"
#include "sprite.h"
#include "stars.h"
#include "ui.h"
#include <string.h>

typedef struct { void *p; size_t n; } Region;
#define MAX_REGIONS 96
static Region s_reg[MAX_REGIONS];
static int s_nreg;
static size_t s_total;

static void add(void *p, size_t n) { if (s_nreg < MAX_REGIONS) { s_reg[s_nreg].p = p; s_reg[s_nreg].n = n; s_nreg++; s_total += n; } }
#define ADD(x) add(&(x), sizeof(x))

static void init(void)
{
    if (s_nreg) return;
    ADD(g_cam); ADD(g_view_ext);
    ADD(g_fx);
    ADD(g_en); ADD(g_shots); ADD(g_enemy_sound_deadline); ADD(g_boss_phase); ADD(g_kills); ADD(g_enemy_fire_chance); ADD(g_difficulty);
    ADD(g_mv); ADD(g_joy); ADD(g_frame); ADD(g_shot_block_tile); ADD(g_demo_playback);
    ADD(g_in);
    ADD(g_spawn_groups); ADD(g_levers); ADD(g_doors1); ADD(g_doors2); ADD(g_lv); ADD(g_teleporters); ADD(g_tile_items); ADD(g_hints);
    ADD(g_demo_buf); ADD(g_demo_len); ADD(g_enemies_total); ADD(g_sprite_ids); ADD(g_episode);
    ADD(g_score_saved); ADD(g_music_on); ADD(g_sound_on);
    ADD(g_map); ADD(g_map_view_rng);
    ADD(g_proj); ADD(g_player);
    ADD(g_spr);
    ADD(g_star_ang); ADD(g_star_rad); ADD(g_star_br); ADD(g_star_spd); ADD(g_star_spd2);
    ADD(g_exit_clock);
    rng_state(add); loader_state(add); video_state(add); vga_state(add);
}

size_t state_size(void) { init(); return s_total; }
void state_save(void *buf)
{
    init();
    uint8_t *o = buf;
    for (int i = 0; i < s_nreg; i++) { memcpy(o, s_reg[i].p, s_reg[i].n); o += s_reg[i].n; }
}
void state_restore(const void *buf)
{
    init();
    const uint8_t *o = buf;
    for (int i = 0; i < s_nreg; i++) { memcpy(s_reg[i].p, o, s_reg[i].n); o += s_reg[i].n; }
}
