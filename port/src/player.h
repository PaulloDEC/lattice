/*
 * player.h - player state touched by damage checks (original 0BA5).  Names inferred.
 */
#ifndef HOCUS_PLAYER_H
#define HOCUS_PLAYER_H
#include <stdint.h>

typedef struct Player {
    int16_t health;    /* DS:704C  0..100, 0 = dead (cheat sets 100)                         */
    int16_t invuln;    /* DS:CE74  invulnerability frames after a hit (20), counted down      */
    int16_t shield;    /* DS:CE8E  power-up timer (400) / 0x7530 = permanent; blocks all damage */
} Player;
extern Player g_player;

/* Second object pool, DS:BE84..BF54 (8 slots; type -1 = free): enemy projectiles.  Each array
 * is 8 int16 words, 16 bytes apart; the DS offset is given per field. */
#define PROJ_SLOTS 8
typedef struct Projectiles {
    int16_t aux[PROJ_SLOTS];     /* BE74 cleared at spawn, not used by 0A81 (?) */
    int16_t homing[PROJ_SLOTS];  /* BE84 non-zero: drifts 1 column per frame toward the player */
    int16_t vy[PROJ_SLOTS];      /* BE94 added to y every frame                                */
    int16_t speed[PROJ_SLOTS];   /* BEA4 columns per frame (direction by dir)                  */
    int16_t sheet[PROJ_SLOTS];   /* BEB4 sprite slot 0..9                                      */
    int16_t dir[PROJ_SLOTS];     /* BEC4 0 = moves right, 1 = left (also the sprite variant)   */
    int16_t h[PROJ_SLOTS];       /* BED4 box height */
    int16_t w[PROJ_SLOTS];       /* BEE4 box width  */
    int16_t y[PROJ_SLOTS];       /* BEF4 y in pixels */
    int16_t x[PROJ_SLOTS];       /* BF04 x in 4-pixel columns */
    int16_t type[PROJ_SLOTS];    /* BF14 -1 = free */
    int16_t last_frame[PROJ_SLOTS]; /* BF24 animation runs frame 0..last_frame then holds */
    int16_t frame[PROJ_SLOTS];   /* BF34 */
    int16_t jitter[PROJ_SLOTS];  /* BF44 non-zero: y wobbles by rng(2)-rng(2) per frame */
    int16_t delay[PROJ_SLOTS];   /* BF54 start delay: counts down, harmless and invisible meanwhile */
} Projectiles;
extern Projectiles g_proj;

/* damage per hit by difficulty (DS:2C36): -4, -12, -16, 0 */
extern const int16_t g_damage_by_difficulty[4];

void player_hit_by_projectiles(void);  /* 0BA5:0006 */
void player_hit_by_enemies(void);      /* 0BA5:00D3 */
/* 0BA5:0922 - spawn a projectile at (x, y); aim != 0: point it at the player's side and offset x by the
 * sprite width; `delay` frames before it is live; def = enemy definition index (speed, vy, homing, jitter);
 * sheet = sprite slot.  Returns 1, or 0 when all 8 slots are used. */
int projectile_spawn(int x, int y, int aim, int delay, int def, int sheet);
void projectiles_update(void);         /* 0BA5:0A81 move, draw, wall-hit */
#endif
