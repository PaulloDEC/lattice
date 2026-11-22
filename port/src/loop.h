/*
 * loop.h - the level function (original 0BA5:453A) assembled from the ported pieces: setup, then one iteration per game
 * frame (page flip, timer wait, input, camera, tile interaction, map + backdrop draw, HUD, end states, player, entity
 * updates, controls, shots, movement, system keys), then the exit sequences.  Everything that is user interface or
 * file / sound system stays behind LoopHooks (all members optional; NULL = do nothing / return 0).
 */
#ifndef HOCUS_LOOP_H
#define HOCUS_LOOP_H
#include <stdint.h>

typedef struct LoopHooks {
    void (*poll_joystick)(void);           /* 1392:081D  fills g_joy */
    void (*input_off)(void);               /* 1392:0254  unhook the keyboard (before modal screens) */
    void (*wait_key)(void);                /* 5429..5447 "press a key" wait (key or button, then drain the key buffer) */
    void (*pause_game)(void);              /* 0BA5:3751  P key */
    int  (*quit_menu)(void);               /* 06B8:4600  ESC menu: 1 = new game of this level, 2 = quit, 3 = restart level, else continue */
    void (*menu_f2)(void);                 /* 06B8:3048 */
    int  (*menu_f3)(void);                 /* 06B8:30DE  load game; non-zero = loaded, restart the setup */
    int  (*menu_f10)(void);                /* 06B8:3187  non-zero = quit */
    void (*menu_f1)(int arg);              /* 06B8:44CC(1) */
    void (*menu_c)(void);                  /* 06B8:2FA8  calibrate (only when a joystick is enabled) */
    void (*menu_v)(void);                  /* 06B8:1744 */
    void (*message)(const char *text);     /* 0BA5:37FC  one line status message */
    void (*music_off)(void);               /* 136B:0261 */
    void (*save_settings)(void);           /* 068C:01EE  write the 0x3DE byte settings block (music / sound flags) */
    void (*save_demo)(int bytes);          /* 068C:01EE  write g_demo_buf (mode 1) */
    int  (*end_screen)(int finished, int goal, int collected);   /* 06B8:4889: the level complete / died screen; returns 1 to continue to the next level */
    void (*music)(int entry, int loop);    /* 136B:01FF (also used by the setup via g_level_hooks) */
    void (*palette_entry)(int entry, int slot);  /* 05D8:0AFF without applying (used after the fade-out) */
    void (*release)(uint32_t clock);       /* 0:3846  free level memory */
    int  (*frame)(int n);                  /* top of every frame (original 0BA5:4D47); a non-zero result aborts level_run with -2 (tests, window close) */
    /* Enhanced display smoothing (port only, NULL = nothing): */
    void (*level_start)(void);             /* after every level setup (new level, restart, load) */
    void (*level_end)(void);               /* level_run is returning */
    void (*tick_begin)(void);              /* the frame's drawing starts (camera updated, before the world draw) */
    void (*tick_nosmooth)(void);           /* this frame must be shown as drawn (pause screen drawn into it) */
    void (*tick_shown)(void);              /* the finished frame was just shown; the tick wait follows */
    void (*tick_waited)(void);             /* the tick wait is over; the next frame's logic runs */
} LoopHooks;
extern LoopHooks g_loop_hooks;

extern int16_t g_game_speed;         /* DS:790E  index into the tick table */
extern int16_t g_music_on;           /* DS:48CC */
extern int16_t g_sound_on;           /* DS:48CA */
extern uint32_t g_score_saved;       /* DS:704E */
extern int16_t g_sound_hw;           /* DS:643E | DS:643C: a sound device is present */

/* Wires the effect / enemy / projectile drawing to the sprite blitters (the effect modules draw through callbacks). */
void loop_install_defaults(void);

/* Runs one level: mode 0 normal, 1 record a demo, 2 play a demo; pause != 0 shows the "press a key" screen before the
 * first frame.  Returns the original's result: 1 level finished, 0 died, -1 quit. */
void level_tick_ahead(void);   /* run-ahead: one silent tick (see loop.c); level_run must be active */
int level_run(int mode, int pause);
#endif
