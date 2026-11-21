/*
 * interp.h - Enhanced display smoothing ("Smooth: display").
 *
 * The game logic keeps its original tick (17.5 / 20 / 23.3 per second); only the picture changes.
 * Every tick, while the original routines draw the frame into VGA memory as always, the draws are also
 * recorded (camera, the visible map cells, every sprite blit and effect pixel, tagged with the object that
 * drew it - see g_rec_* in sprite.h).  Between ticks a renderer redraws the play area at the display's
 * refresh rate, blending each tagged object and the camera between the previous and the latest tick, at
 * 1-pixel precision.  The status panel (rows 160..199) is taken from the VGA page as drawn.
 * Frames that can't be smoothed (screen flash, the "press a key" picture, menus, fades) show the VGA page.
 * The latest tick is reached exactly when the next one arrives, so the picture runs up to one tick behind.
 * With smoothing off nothing here affects what is shown.
 */
#ifndef HOCUS_INTERP_H
#define HOCUS_INTERP_H
#include <stdint.h>

void interp_install(void);          /* sets the recording hooks and the LoopHooks level_start / tick_* entries */
void interp_set_enabled(int on);    /* also switches the steady tick schedule (timer_set_steady) */
int  interp_enabled(void);
int  interp_ready(void);            /* inside a level's tick wait with a recorded frame to show */

/* Draw the play area into out (320x200 indexed; rows 160.. are the VGA page) for the current time.
 * Returns 0 when this moment must show the VGA page instead (out untouched). */
int  interp_render(uint8_t out[320 * 200]);
int  interp_render_at(uint8_t out[320 * 200], double alpha);   /* alpha 0 = start of the blend, 1 = its end */
int  interp_render_latest(uint8_t out[320 * 200]);              /* the latest real tick as drawn (tests) */

/* Run-ahead: 0 = blend the previous tick into the latest (a tick behind); 1 = blend the latest into the next
 * tick, run in advance (no extra delay); 2 = two ticks ahead (less delay than DOS; can mispredict). */
void interp_set_runahead(int n);
int  interp_runahead(void);
void interp_input_changed(void);    /* a game key changed during the tick wait: run ahead again */
int  interp_speculating(void);      /* inside a silent run-ahead tick: sounds and hint boxes must do nothing */
void interp_set_check(int on);      /* tests: count real ticks that differ from what was run ahead for them */
void interp_check_counts(int *ticks, int *bad);
#endif
