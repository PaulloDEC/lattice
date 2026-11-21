/*
 * frame.h - the per-frame body of the level loop (original 0BA5:453A, 4D67..).  Built up piece by piece;
 * every piece has a differential test against the original code run between two addresses.
 */
#ifndef HOCUS_FRAME_H
#define HOCUS_FRAME_H
#include <stdint.h>

typedef struct Joy {           /* joystick state words, filled by the poll (1392:081D) */
    int16_t left;              /* 78F2 */
    int16_t right;             /* 78F0 */
    int16_t up;                /* 78EE */
    int16_t down;              /* 78EC */
    int16_t swap;              /* 78EA  non-zero: buttons swapped (button 2 fires) */
    int16_t btn1;              /* 78FA */
    int16_t btn2;              /* 78F8 */
    int16_t scroll_down;       /* 78F6  look-down request (stick / key) */
    int16_t scroll_up;         /* 78F4  look-up request */
} Joy;
extern Joy g_joy;
extern int16_t g_joy_enabled;       /* DS:48CE */
extern int16_t g_demo_playback;     /* DS:790C  1 while a demo plays (mode 2) */
extern const uint8_t *g_demo_data;  /* DS:48C6  recorded frames, 6 bytes each, after a 2-byte header */

typedef struct FrameState {
    uint8_t rec[6];       /* CF40..CF45  demo record of this frame: up, left, right, fire, jump */
    int16_t demo_pos;     /* CF3E  frame counter into the demo */
    uint8_t fire_held;    /* CF49  fire button is down */
    uint8_t up_held;      /* CF48  up is down */
    uint8_t local_c;      /* bp-0xC  counted down once per frame (purpose not yet known) */
    uint8_t anim_toggle;  /* bp-0xD  alternates every frame; the tile animation runs on one parity */
    uint8_t jump_end;     /* bp-0xE  set on the frame a jump ends (cleared at the start of player_move) */
    uint8_t landed;       /* bp-0xF  set when the player walked off a ledge; the landing sound plays on touch-down */
} FrameState;
extern FrameState g_frame;

/* 0BA5:54AC..5824: record / play back demo input, apply joystick, fire a player shot, tick the potion timer.
 * mode: 0 normal, 1 recording a demo, 2 playing one back. */
void frame_controls(int mode);
/* Player movement state (all DS variables of the 5F6A..6700 block) */
typedef struct MoveState {
    int16_t fall;            /* CF08  1 while falling */
    int16_t jump_idx;        /* CF16  index into the jump table */
    int16_t jump_len;        /* CF12  entries in the table */
    int16_t jump_peak;       /* CF14  index from which landing on a floor ends the jump */
    const int16_t *jump_tab; /* CF18:CF1A  the active jump table (DS:2C10 normal, DS:2BDE after the item-4 power-up) */
    int16_t step_r_up;       /* CF1E  may step right and up one tile */
    int16_t step_l_up;       /* CF20  may step left and up one tile */
    int16_t free_r;          /* CF22  right side free */
    int16_t free_l;          /* CF24  left side free */
    int16_t moved;           /* CF1C  walked this frame (drives the walk animation) */
    int16_t look_off;        /* CF34  never set non-zero inside this block */
    int16_t plat_down;       /* CF54  frames down is held, 0..10 */
    int16_t plat_up;         /* CF56  frames up is held, 0..10 */
    int16_t plat_tile_a;     /* CF4A  tile id of the right half of a lift platform */
    int16_t plat_tile_b;     /* CF4C  tile id of the left half (-1: no lifts in this level) */
} MoveState;
extern MoveState g_mv;
extern const int16_t g_jump_tab_normal[19];   /* DS:2C10 */
extern const int16_t g_jump_tab_power[25];    /* DS:2BDE */
/* 0BA5:5F6A..6700: walk / turn / step up, jump arc, fall, lift platforms, teleport slide, look up / down margin. */
void player_move(int mode);
/* Blitter hook for the 0BA5 sprite routines called by the frame code.  `which` is the original address
 * (0x249E, 0x2896, 0x27E2, 0x26B3, 0x25FF, 0x23BA); a0..a5 are the arguments in call order (the 5-argument routines
 * 27E2 / 25FF / 23BA leave a5 = 0).  The default hook calls the sprite.c blitters. */
typedef void (*blit_fn)(int which, int a0, int a1, int a2, int a3, int a4, int a5);
void frame_set_blit_hook(blit_fn fn);
/* 0BA5:50B1..53FF: draw the player (frame choice, blink when invulnerable / potion, teleport animation), the item-4
 * sparkles, the shot-counter icons above the player and the shield countdown. */
void player_draw(void);
/* External routines called by the level loop: 1 = 1392:0195 (stops the level music), 2 = 0BA5:01B6 (destroy every enemy).
 * The default hook ignores 1 and calls enemy_kill_all() for 2. */
typedef void (*event_fn)(int ev);
void frame_set_event_hook(event_fn fn);
/* 0BA5:4E8B..50B1: start / run the death countdown (health 0) and the level-complete countdown (all crystals
 * collected) with their explosion effects. */
void level_end_states(void);
/* 0BA5:220F: the screen flash (level.flash != 0): fills the draw page with colour 0x77 - flash and counts down. */
void level_flash(void);
extern uint8_t g_shot_block_tile;  /* DS:6D6B */
/* 0BA5:5824..5F62: move / draw / collide the player shots. */
void shots_update(void);
#endif
