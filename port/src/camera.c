#include "camera.h"

ViewExt g_view_ext;

Camera g_cam;

/* All arithmetic is 16-bit like the original (int16 stores, signed divisions truncate to zero). */
#define I16(v) ((int16_t)(v))

static void shift_x(int16_t di)
{
    for (int b = 0; b < 8; b++)                       /* A: active bursts only, pixels */
        if (g_fx.a_flags[b] > 0)
            for (int p = 0; p < 16; p++) g_fx.a_pos[b][p][0] = I16(g_fx.a_pos[b][p][0] + di);
    int16_t dc = I16(di / 4);                         /* the rest are in 4-px byte columns */
    for (int i = 0; i < 10; i++) g_fx.b[i][0] = I16(g_fx.b[i][0] + dc);
    for (int i = 0; i < 8;  i++) g_fx.c[i][0] = I16(g_fx.c[i][0] + dc);
    for (int i = 0; i < 20; i++) g_fx.d_x[i]  = I16(g_fx.d_x[i] + dc);
}

static void shift_y(int16_t dy)
{
    for (int i = 0; i < 10; i++) g_fx.e_y[i] = I16(g_fx.e_y[i] + dy);
    for (int b = 0; b < 8; b++)
        if (g_fx.a_flags[b] > 0)
            for (int p = 0; p < 16; p++) g_fx.a_pos[b][p][1] = I16(g_fx.a_pos[b][p][1] + dy);
    for (int i = 0; i < 10; i++) g_fx.b[i][1] = I16(g_fx.b[i][1] + dy);
    for (int i = 0; i < 8;  i++) g_fx.c[i][1] = I16(g_fx.c[i][1] + dy);
    for (int i = 0; i < 20; i++) g_fx.d_y[i]  = I16(g_fx.d_y[i] + dy);
}

void camera_update(void)
{
    Camera *c = &g_cam;
    int16_t old_row = c->cam_row;                     /* bp-2 */
    int16_t old_x   = c->cam_x;                       /* bp-6 */

    c->player_xcol = I16(c->player_x * 2);
    c->player_row  = I16(c->player_y / 16);
    c->col_odd = 0;

    /* ---- horizontal ------------------------------------------------------------------ */
    if (c->snap) {
        if (I16(c->player_x - 20) < 0)            c->cam_x = 0;
        else if (I16(c->player_x + 20) >= 0x1E0)  c->cam_x = 0x1B8;
        else {
            c->cam_x = I16(c->player_x - 20);
            if (c->player_x % 2 != 0) c->col_odd = 1;
        }
        /* no object shifting when snapping */
    } else if (I16(c->player_x - 20) < 0) {
        c->cam_x = 0;                                  /* clamp: objects are NOT shifted */
    } else if (I16(c->player_x + 20) >= 0x1E0) {
        c->cam_x = 0x1B8;                              /* clamp: objects are NOT shifted */
    } else {
        int16_t target = I16(c->player_x - 20);
        if (target > c->cam_x)       c->cam_x++;       /* ease 1 unit (8 px) per frame */
        else if (target < c->cam_x)  c->cam_x--;
        if (c->player_x % 2 != 0) c->col_odd = 1;
        shift_x(I16(I16(old_x - c->cam_x) << 3));
    }

    c->player_tile = I16(c->player_x / 2);
    c->cam_col     = I16(c->cam_x / 2);
    c->vscroll = 0;

    /* ---- vertical -------------------------------------------------------------------- */
    if (c->snap) {
        if (c->vfreeze == 0) {
            if (I16(c->player_row - c->vmargin) < 0)               c->cam_row = 0;
            else if (I16(c->player_row + c->vmargin) >= 0x3C)      c->cam_row = 0x32;
            else                                                   c->cam_row = I16(c->player_row - c->vmargin);
        }
    } else if (c->vfreeze == 0) {
        if (I16(c->player_row - c->vmargin) < 0)                   c->cam_row = 0;
        else if (I16(c->player_row + I16(10 - c->vmargin)) >= 0x3C) c->cam_row = 0x32;
        else if (I16(c->player_row - c->vmargin) > c->cam_row)   { c->cam_row++; c->vscroll = 1; }
        else if (I16(c->player_row - c->vmargin) < c->cam_row)   { c->cam_row--; c->vscroll = 1; }
        shift_y(I16(I16(old_row - c->cam_row) << 4));       /* applies after every branch, clamps included */
    }

    c->pscreen_y = I16(c->player_y - I16(c->cam_row << 4));
    c->pscreen_x = I16(I16(c->player_x - c->cam_x) * 2);
}
