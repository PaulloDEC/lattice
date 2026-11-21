/*
 * camera.h - Hocus Pocus camera / scroll update (original 0BA5:29C5-2C80, called once per frame).
 *
 * Field names follow what the values do; DS offsets are the original variables.
 * Units: X in 8-pixel units (map = 480), rows in tiles, player_y in pixels.
 */
#ifndef HOCUS_CAMERA_H
#define HOCUS_CAMERA_H
#include <stdint.h>
#include "effects.h"

typedef struct Camera {
    int16_t cam_row;       /* CE7A  camera top row, tiles, 0..50                                  */
    int16_t col_odd;       /* CE7C  1 when player_x is odd (set by this routine, read elsewhere)  */
    int16_t cam_col;       /* CE7E  camera X in 16-px tile columns (= cam_x / 2)                  */
    int16_t cam_x;         /* CE80  camera X, 8-px units, 0..440                                  */
    int16_t vscroll;       /* CF0A  1 on frames where the camera row moved                        */
    int16_t vfreeze;       /* CF0C  non-zero: camera row frozen                                   */
    int16_t pscreen_y;     /* CF26  player Y on screen, pixels                                    */
    int16_t player_y;      /* CF28  player Y in the world, pixels                                 */
    int16_t pscreen_x;     /* CF2A  player X on screen, 4-px byte columns                         */
    int16_t player_xcol;   /* CF2C  player X in 4-px byte columns                                 */
    int16_t player_row;    /* CF2E  player row in tiles (= player_y / 16)                         */
    int16_t player_tile;   /* CF30  player tile column (= player_x / 2)                           */
    int16_t player_x;      /* CF32  player X, 8-px units, 0..480                                  */
    int16_t vmargin;       /* CF36  vertical margin in tiles                                      */
    int16_t snap;          /* CF38  non-zero: camera X snaps to the player instead of easing      */
} Camera;

extern Camera g_cam;
/* One frame of camera update.  Also shifts the world-anchored effect positions in g_fx so they
 * stay put while the camera moves (burst/score/puff/sparkle positions, shot Y). */
void camera_update(void);
#endif
