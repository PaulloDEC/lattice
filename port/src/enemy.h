/*
 * enemy.h - Hocus Pocus enemy slots (original 0BA5, tables at DS:BF64..C124, 8 slots).
 *
 * Field meanings are INFERRED from the code that uses them (marked "?"); names carry the original
 * DS offset so they can be cross-checked.  A slot is free when type == -1 (DS:BFD4).
 * Enemy definitions (DS:A552, 12 words each, from map file 7): w0 sprite sheet id (-1 = none),
 * w1 hit points (+ 2 x difficulty), w4/w5/w6/w7/w8/w10 copied to the slot, w11 behaviour type 0..8.
 */
#ifndef HOCUS_ENEMY_H
#define HOCUS_ENEMY_H
#include <stdint.h>

#define ENEMY_SLOTS 8
typedef struct Enemies {
    int16_t bf64[ENEMY_SLOTS];  /* ? 0 at spawn                                   */
    int16_t hp_max[ENEMY_SLOTS];/* BF74 hit points at spawn                       */
    int16_t bf84[ENEMY_SLOTS];  /* ? 20 at spawn (counter)                        */
    int16_t bf94[ENEMY_SLOTS];  /* ? 0                                            */
    int16_t bfa4[ENEMY_SLOTS];  /* ? -99                                          */
    int16_t bfb4[ENEMY_SLOTS];  /* ? 1                                            */
    int16_t bfc4[ENEMY_SLOTS];  /* ? 1                                            */
    int16_t type[ENEMY_SLOTS];  /* BFD4 behaviour type (def word 11); -1 = free   */
    int16_t def[ENEMY_SLOTS];   /* BFE4 definition index                          */
    int16_t sheet[ENEMY_SLOTS]; /* BFF4 sprite slot (= def + 4)                   */
    int16_t cell[ENEMY_SLOTS];  /* C004 map cell index it was spawned from        */
    int16_t flash[ENEMY_SLOTS]; /* C014 hit-flash counter?                        */
    int16_t hp[ENEMY_SLOTS];    /* C024 current hit points                        */
    int16_t c034[ENEMY_SLOTS];  /* ? 0                                            */
    int16_t facing[ENEMY_SLOTS];/* C044 1 = player is to the left of it           */
    int16_t box_h[ENEMY_SLOTS]; /* C054 sheet header word 14 (hit box height?)    */
    int16_t box_w[ENEMY_SLOTS]; /* C064 sheet header word 13 (hit box width?)     */
    int16_t timer[ENEMY_SLOTS]; /* C074 behaviour timer (random at spawn)         */
    int16_t vy[ENEMY_SLOTS];    /* C084 vertical speed?                           */
    int16_t vx[ENEMY_SLOTS];    /* C094 horizontal speed / direction?             */
    int16_t y[ENEMY_SLOTS];     /* C0A4 y in pixels                               */
    int16_t x[ENEMY_SLOTS];     /* C0B4 x in 4-pixel columns                      */
    int16_t c0c4[ENEMY_SLOTS];  /* def word 10 (type 8: random 10..19)            */
    int16_t c0d4[ENEMY_SLOTS];  /* def word 4                                     */
    int16_t c0e4[ENEMY_SLOTS];  /* def word 8                                     */
    int16_t c0f4[ENEMY_SLOTS];  /* def word 5                                     */
    int16_t c104[ENEMY_SLOTS];  /* def word 7                                     */
    int16_t c114[ENEMY_SLOTS];  /* def word 6                                     */
    int16_t c124[ENEMY_SLOTS];  /* C124 'shot in flight' flag (set when firing, cleared at the end of the fire anim) */
    int16_t pending[32];        /* DS:C134 map cells waiting to spawn (see enemy_spawn_pending); kept
                                 * inside this struct because the original's slot-10 bug in 126C
                                 * indexes past c124 into this list */
} Enemies;

extern Enemies g_en;
extern const int16_t *g_enemy_defs;   /* DS:A552, 12 words per definition          */
extern int16_t g_difficulty;          /* DS:706A                                    */
extern uint32_t g_enemy_sound_deadline; /* DS:CFD6:CFD4 (tick before the next spawn sound) */

/* sound effect hook: original 136B:000B(id) (the sound module is not ported yet) */
typedef void (*sound_fn)(int id);
void game_set_sound_callback(sound_fn fn);
void game_sound(int id);

#define ENEMY_PENDING 32
#define MAP_COLS 240
#define g_pending_cell (g_en.pending)            /* DS:C134 map cells waiting to spawn, -1 = free */
extern int16_t *g_map_info;                    /* DS:705A info layer (map file 12), 240x60 words */
void enemy_spawn_pending(void);                /* 0BA5:2E9D */
/* Player shot pool (DS:CEEE x, CEDA y = g_fx.e_y, CE9A power, CEAE count). x == -99 = free. */
#define SHOT_SLOTS 10
typedef struct Shots {
    int16_t x[SHOT_SLOTS];      /* CEEE  x in screen units (compared with the enemy box) */
    int16_t power[SHOT_SLOTS];  /* CE9A  non-zero = piercing shot (survives hits, always kills) */
    int16_t count;              /* CEAE  live shots */
    int16_t up[SHOT_SLOTS];     /* CEC6  1 = flies upwards                                  */
    int16_t dir[SHOT_SLOTS];    /* CEB2  facing at fire time (0 = right, 1 = left)          */
    int16_t cursor;             /* CEB0  scratch: slot found by the fire code               */
} Shots;
extern Shots g_shots;
extern int16_t g_kills;         /* DS:CE6C enemies killed this level */

void enemy_shots_collide(void);   /* 0BA5:0267 shots vs enemies */
void enemy_kill_all(void);        /* 0BA5:01B6 destroy every enemy (smart bomb) */
void enemy_despawn_offscreen(void);/* 0BA5:0439 free enemies far outside the view (hp_max < 500) */
/* 0BA5:0C5B - boss health bar (enemies with hp_max > 20): 3 scanlines, length trunc(hp * 78 / hp_max)
 * pixels in x87 extended precision, colour 0x6F - i/10.  Uses all 4 planes, latch off. */
void enemy_draw_bar(int slot);
/* 0BA5:0CE8 - per-frame update of a type 8 enemy (hovering shooter): boss bar, spawn dust while
 * bf84 counts down, then draw (flash = silhouette), wiggle around its anchor (vx = spawn x), shoot. */
void enemy_update_type8(int slot);
/* 0BA5:1019 - the final boss (type 0x63), always slot 0.  Same spawn-dust / draw / flash as type 8, then a
 * 3-state attack cycle on timer c0f4 (state in c0d4: 0 idle, 1 wound up, 2 shot fired, >= 3 dying) and, when hp
 * drops below the phase threshold, it dies and re-spawns (def 0) at the next phase's position. */
extern int16_t g_boss_phase;   /* DS:CF46 0..4 */
void enemy_update_boss(void);
/* 0BA5:126C - per-frame update of all 8 enemy slots (dispatches type 8 and the boss, moves / turns /
 * fires / animates / draws the others).  See HOCUS_NOTES.md section 28 for the original quirks kept. */
extern int16_t g_enemy_fire_chance;   /* DS:7042 enemies fire with chance 1 / this per frame */
void enemies_update(void);
void enemy_reset(void);   /* all slots free (type = -1), as the level start does */
/* 0BA5:04D3 - spawn enemy definition `def` at x (4-px columns), y (pixels) for map cell `cell`.
 * Returns 1, or 0 when all 8 slots are used. */
int enemy_spawn(int def, int x, int y, int cell);
#endif
