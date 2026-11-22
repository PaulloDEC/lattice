/* interp.c - Enhanced display smoothing (see interp.h) */
#include "interp.h"
#include "sprite.h"
#include "camera.h"
#include "map.h"
#include "vga.h"
#include "video.h"
#include "level.h"
#include "timer.h"
#include "loop.h"
#include "state.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define MAX_OPS 2048
enum { OP_SPR, OP_PIX, OP_RECT };
typedef struct {
    uint8_t kind, mode, clip;
    int16_t sheet, wrap, x, y, anim, frame;    /* PIX: x, y pixels, frame = colour.  RECT: x0, y0, wrap = x1, anim = y1 */
    int tag;
} Op;

/* visible map cells at draw time (the tile pass runs after the draw, so the live map is a tick ahead) */
#define SNAP_ML 7                                 /* cells left of / above the original 20 x 10 window ... */
#define SNAP_MT 5
#define SNAP_W (SNAP_ML + 20 + 7)                 /* ... and right / below: wide view (+53 px a side) + blending */
#define SNAP_H (SNAP_MT + 10 + 5)                 /*     HUD off (+20 px above and below) + blending */
typedef struct {
    int valid, nosmooth, flash;                   /* flash: the play area is one colour (level_flash) */
    int cam_x, cam_row, cam_col, col_odd;
    uint32_t tick;                                /* timer_last() of the tick that drew it */
    int16_t cells[SNAP_W * SNAP_H];
    /* the packed tile graphics (page 2 up to the sprite pixels), copied at draw time: the original's unclipped
     * player blit can run past the bottom of page 1 into them (falling off the screen), and on DOS the
     * damage only shows from the next frame on */
    uint16_t tile_bytes;
    uint8_t tiles[4][VIDEO_PAGE_STRIDE];
    int n;
    Op ops[MAX_OPS];
    uint8_t img[320 * 200];                       /* the finished page (status panel, boss bar) */
} Frame;

/* prev / cur: the last two real ticks; bld: the tick being drawn; ahead[i]: tick cur + 1 + i, run in advance */
static Frame s_fr[5];
static Frame *prev = &s_fr[0], *cur = &s_fr[1], *bld = &s_fr[2], *ahead[2] = { &s_fr[3], &s_fr[4] };
static Frame *s_rec = &s_fr[2];                   /* where recording goes (bld, or ahead[i] while running ahead) */
static int s_armed, s_ready, s_on;
static int s_runahead = 1, s_nahead, s_spec;      /* setting 0..2, ticks available, inside a run-ahead tick */
static void *s_state;
static int s_wide, s_hudoff;                      /* Enhanced views: 16:9 width, HUD off (full-height play area) */
static int s_level_active;                         /* between a level's setup and the end of level_run */
static int s_check, s_pred_n, s_pred_bad;         /* prediction check (tests) */
static uint8_t s_pred_img[320 * 200]; static int s_pred_have;

#define MAX_TAG 0x1100                            /* TAG_BURST + 8 * 16 < 0x1100 */
#define MAX_OCC 4
#define SNAP_DIST 48                              /* world pixels per tick; more = a jump (teleport, reused slot) */

/* ---- recording ---------------------------------------------------------------------------------------- */
static void rec_blit(int mode, int clip, int sheet, int wrap, int x, int y, int anim, int frame)
{
    if (!s_armed || s_rec->n >= MAX_OPS) return;
    Op *o = &s_rec->ops[s_rec->n++];
    o->kind = OP_SPR; o->mode = (uint8_t)mode; o->clip = (uint8_t)clip;
    o->sheet = (int16_t)sheet; o->wrap = (int16_t)wrap; o->x = (int16_t)x; o->y = (int16_t)y; o->anim = (int16_t)anim; o->frame = (int16_t)frame;
    o->tag = g_rec_tag;
}
static void rec_pixel(int x, int y, int color, int tag)
{
    if (!s_armed || s_rec->n >= MAX_OPS) return;
    Op *o = &s_rec->ops[s_rec->n++];
    memset(o, 0, sizeof *o);
    o->kind = OP_PIX; o->x = (int16_t)x; o->y = (int16_t)y; o->frame = (int16_t)color; o->tag = tag;
}
static void rec_rect(int x0, int y0, int x1, int y1)
{
    if (!s_armed || s_rec->n >= MAX_OPS) return;
    Op *o = &s_rec->ops[s_rec->n++];
    memset(o, 0, sizeof *o);
    o->kind = OP_RECT; o->x = (int16_t)x0; o->y = (int16_t)y0; o->wrap = (int16_t)x1; o->anim = (int16_t)y1;
}

static int live_cell(int cx, int cy)              /* layer1, else layer2, else -1 (map.c's pick) */
{
    if (!g_map.fg || !g_map.bg || cx < 0 || cx >= MAP_WIDTH || cy < 0 || cy >= 60) return -1;
    int i = cy * MAP_WIDTH + cx;
    if (g_map.fg[i] != 0xFF) return g_map.fg[i];
    if (g_map.bg[i] != 0xFF) return g_map.bg[i];
    return -1;
}

#define WIDE_W 426                                /* 16:9 at the original's 1.2:1 pixels: 200 * 1.2 * 16 / 9 */
#define MAP_PX_W (MAP_WIDTH * 16)
#define MAP_PX_H (60 * 16)
static int view_w(void) { return s_wide ? WIDE_W : 320; }
static int play_h(void) { return s_hudoff ? 200 : 160; }
/* how far the view's left (top) edge sits from the original screen's: centred, kept inside the map */
static int view_shift(int cam, int extra, int map, int size)
{
    int l = cam - extra;
    if (l > map - size) l = map - size;
    if (l < 0) l = 0;
    return l - cam;
}
static void set_view_ext(void)
{
    ViewExt e = { 0, 0, 0, 0 };
    if (s_wide || s_hudoff) {
        int W = view_w(), PH = play_h();
        int sx = view_shift(g_cam.cam_x * 8, (W - 320) / 2, MAP_PX_W, W);
        int sy = view_shift(g_cam.cam_row * 16, (PH - 160) / 2, MAP_PX_H, PH);
        e.left = (int16_t)(sx < 0 ? -((-sx + 3) / 4) : 0);
        e.right = (int16_t)((sx + W - 320 + 3) / 4);
        e.top = (int16_t)sy;
        e.bottom = (int16_t)(sy + PH - 160);
    }
    g_view_ext = e;
}

static void level_start(void)
{
    s_level_active = 1;
    prev->valid = cur->valid = bld->valid = ahead[0]->valid = ahead[1]->valid = 0;
    s_rec = bld; s_armed = 0; s_ready = 0; s_nahead = 0; s_pred_have = 0;
}
static void tick_begin(void)
{
    Frame *f = s_rec;
    f->valid = 1; f->nosmooth = 0; f->flash = g_lv.flash != 0 ? 0x77 - g_lv.flash : -1; f->n = 0;
    f->cam_x = g_cam.cam_x; f->cam_row = g_cam.cam_row; f->cam_col = g_cam.cam_col; f->col_odd = g_cam.col_odd;
    f->tick = timer_last();
    set_view_ext();
    for (int r = 0; r < SNAP_H; r++)
        for (int c = 0; c < SNAP_W; c++)
            f->cells[r * SNAP_W + c] = (int16_t)live_cell(f->cam_col - SNAP_ML + c, f->cam_row - SNAP_MT + r);
    f->tile_bytes = g_spr.page_off < VIDEO_PAGE_STRIDE ? g_spr.page_off : VIDEO_PAGE_STRIDE;
    for (int p = 0; p < 4; p++)
        for (uint16_t i = 0; i < f->tile_bytes; i++) f->tiles[p][i] = vga_plane(p)[(uint16_t)(g_spr.src_off + i)];
    s_armed = 1;
}
static void tick_nosmooth(void) { if (s_armed) s_rec->nosmooth = 1; }

static void render_pair(uint8_t *out, int W, int PH, const Frame *A, const Frame *B, double a);

/* Run-ahead: save everything, run the next tick(s) silently with the input held now, recording them into
 * ahead[], restore everything.  level_run's own next tick then repeats the first of them for real; it only
 * differs if the input read *before* the drawing changes (the look keys, via the camera), because the
 * original draws Hocus before reading the controls - the next picture is already decided. */
static void run_ahead(void)
{
    s_nahead = 0;
    if (!s_on || s_runahead <= 0 || !cur->valid || cur->nosmooth) return;
    if (!s_state && !(s_state = malloc(state_size()))) return;
    state_save(s_state);
    s_spec = 1;
    for (int i = 0; i < s_runahead; i++) {
        if (i > 0) { video_set_draw_page(video_draw_page() == 0); video_show_page_now(video_visible_page() == 0); }
        Frame *f = ahead[i];
        f->valid = 0; s_rec = f; s_armed = 0;
        level_tick_ahead();
        if (!s_armed) break;
        s_armed = 0;
        vga_render_indexed_at(video_draw_offset(), f->img);
        s_nahead = i + 1;
        if (g_lv.death_timer == 1 || g_lv.finish_timer == 1) break;    /* the level ends after this tick */
    }
    s_spec = 0;
    state_restore(s_state);
    s_rec = bld; s_armed = 0;
    s_pred_have = 0;
    if (s_check && s_nahead > 0 && !ahead[0]->nosmooth) { render_pair(s_pred_img, 320, 160, ahead[0], ahead[0], 1.0); s_pred_have = 1; }
}
/* the controls changed during the tick wait: predict again, so the picture reacts before the next tick */
void interp_input_changed(void) { if (s_ready && s_on && s_runahead > 0 && cur->valid) run_ahead(); }

static void tick_shown(void)
{
    if (s_armed && s_rec == bld) {
        Frame *t = prev; prev = cur; cur = bld; bld = t; s_armed = 0; s_rec = bld;
        vga_render_indexed(cur->img);
        if (s_check && s_pred_have) {             /* does the tick match what was run ahead for it? */
            static uint8_t real[320 * 200];
            render_pair(real, 320, 160, cur, cur, 1.0); s_pred_n++;
            if (memcmp(real, s_pred_img, 320 * 160)) s_pred_bad++;
        }
        s_pred_have = 0;
        run_ahead();
    }
    s_ready = cur->valid;
}
static void tick_waited(void) { s_ready = 0; }
static void level_end(void) { s_level_active = 0; s_ready = 0; }

void interp_install(void)
{
    g_rec_blit = rec_blit; g_rec_pixel = rec_pixel; g_rec_rect = rec_rect;
    g_loop_hooks.level_start = level_start; g_loop_hooks.tick_begin = tick_begin; g_loop_hooks.tick_nosmooth = tick_nosmooth;
    g_loop_hooks.tick_shown = tick_shown; g_loop_hooks.tick_waited = tick_waited; g_loop_hooks.level_end = level_end;
}
void interp_set_enabled(int on) { s_on = on != 0; timer_set_steady(s_on); }
int interp_enabled(void) { return s_on; }
int interp_ready(void) { return s_ready && cur->valid; }
int interp_speculating(void) { return s_spec; }
void interp_set_runahead(int n) { s_runahead = n < 0 ? 0 : n > 2 ? 2 : n; }
int interp_runahead(void) { return s_runahead; }
void interp_set_check(int on) { s_check = on; }
void interp_check_counts(int *ticks, int *bad) { *ticks = s_pred_n; *bad = s_pred_bad; }

/* ---- rendering ---------------------------------------------------------------------------------------- */
static int iround(double v) { return (int)floor(v + 0.5); }
static double lerp(double a, double b, double t) { return a + (b - a) * t; }

static int frame_cell(const Frame *f, int cx, int cy)
{
    int c = cx - (f->cam_col - SNAP_ML), r = cy - (f->cam_row - SNAP_MT);
    if (c >= 0 && c < SNAP_W && r >= 0 && r < SNAP_H) return f->cells[r * SNAP_W + c];
    return live_cell(cx, cy);
}

/* map + parallax backdrop into the play area (W x PH, row stride W).  mx / my = world pixel at the view's top
 * left; bdx = backdrop column at view x 0.  The backdrop is fixed to the screen (it never scrolls vertically),
 * so a taller view (HUD off) stretches its 160 rows; horizontally it repeats every 320 pixels, as it does in
 * the original. */
static void draw_world(uint8_t *out, int W, int PH, const Frame *B, int mx, int my, int bdx)
{
    const uint8_t *pl[4] = { vga_plane(0), vga_plane(1), vga_plane(2), vga_plane(3) };
    uint16_t tsrc = g_spr.src_off, bsrc = (uint16_t)(3 * VIDEO_PAGE_STRIDE + 0x7D0);
    bdx %= 320; if (bdx < 0) bdx += 320;
    for (int Y = 0; Y < PH; Y++) {
        int wy = my + Y, cy = wy >> 4, ty = wy & 15, by = Y * 160 / PH;
        uint8_t *o = out + Y * W;
        int X = 0;
        while (X < W) {
            int wx = mx + X, cx = wx >> 4, tx = wx & 15;
            int span = 16 - tx; if (span > W - X) span = W - X;
            int t = frame_cell(B, cx, cy);
            if (t >= 0) {
                uint16_t rel = (uint16_t)((t << 6) + ty * 4);
                if ((uint32_t)rel + 4 <= B->tile_bytes)
                    for (int k = 0; k < span; k++, tx++) o[X + k] = B->tiles[tx & 3][rel + (tx >> 2)];
                else
                    for (int k = 0; k < span; k++, tx++) o[X + k] = pl[tx & 3][(uint16_t)(tsrc + rel + (tx >> 2))];
            } else {
                int bx = (bdx + X) % 320;
                for (int k = 0; k < span; k++, bx = bx == 319 ? 0 : bx + 1)
                    o[X + k] = pl[bx & 3][(uint16_t)(bsrc + by * 80 + (bx >> 2))];
            }
            X += span;
        }
    }
}

/* sprite.c's blit() walk, drawing into the linear buffer shifted by (dxp, dyp) pixels, clipped to the play area */
static void draw_sprite(uint8_t *out, int W, int PH, const Op *op, int dxp, int dyp)
{
    int16_t a0 = op->sheet, a1 = op->wrap, a2 = op->x, a3 = op->y, a4 = op->anim, a5 = op->frame;
    uint16_t idx = (uint16_t)(a0 * 110 + a4 * 20 + a5);
    if (a0 < 0 || a0 >= SPR_SHEETS || idx + 40u >= SPR_TAB_WORDS || !g_spr.stream[a0]) return;
    const uint8_t *pl[4] = { vga_plane(0), vga_plane(1), vga_plane(2), vga_plane(3) };
    const uint8_t *s = g_spr.stream[a0];
    int mode = op->mode, clip = op->clip;
    uint16_t pos = g_spr.tab[idx];
    uint16_t src = mode == 0 ? (uint16_t)(g_spr.base[a0] + g_spr.tab[idx + 40]) : (mode == 1 ? g_spr.marker0 : g_spr.marker1);
    uint16_t sbase = mode == 0 ? (uint16_t)(g_spr.src_off + g_spr.page_off) : g_spr.src_off;
    uint16_t dst = (uint16_t)(a3 * 80 + a2);
    int16_t row = 0, col = 0;
    uint8_t mask = 0;
    for (int guard = 0; guard < 65536; guard++) {
        uint8_t c = s[pos++];
        if (c == 0) { mask = s[pos++]; dst = (uint16_t)(a3 * 80 + a2); row = 0; col = 0; }
        else if (c == 1) {
            int16_t w = (int16_t)(s[pos] | (s[pos + 1] << 8)); pos += 2;
            dst = (uint16_t)(dst + w);
            if (clip) { col = (int16_t)(col + w); if (col > a1) { row = (int16_t)(row + col / 80); col = (int16_t)(col % 80); } }
        } else if (c == 2) {
            int bx, by;
            if (clip) { bx = col + a2; by = row + a3; } else { bx = dst % 80; by = dst / 80; }
            uint16_t sa = (uint16_t)(sbase + src);
            for (int p = 0; p < 4; p++) {
                if (!(mask >> p & 1)) continue;
                int X = bx * 4 + p + dxp, Y = by + dyp;
                if (X >= 0 && X < W && Y >= 0 && Y < PH) out[Y * W + X] = pl[p][sa];
            }
            if (mode == 0) src++;
            dst++;
            if (clip) { col++; if (col > a1) { row = (int16_t)(row + col / 80); col = (int16_t)(col % 80); } }
        } else return;
    }
}

/* One picture: B (blended from A by a) into out, W x 200 (stride W): the play area is W x PH, below it the
 * status panel of B's page, centred (the sides are left 0 and blacked out by the caller). */
static void render_pair(uint8_t *out, int W, int PH, const Frame *A, const Frame *B, double a)
{
    const uint8_t *img = B->img;
    int ox = (W - 320) / 2;
    memset(out, 0, (size_t)W * 200);
    for (int y = PH; y < 200; y++) memcpy(out + y * W + ox, img + (y - PH + 160) * 320, 320);

    const Frame *p = A;
    int use_prev = p->valid && !p->nosmooth
                && abs(B->cam_x - p->cam_x) * 8 <= 32 && abs(B->cam_row - p->cam_row) * 16 <= 32;
    if (a < 0) a = 0;
    if (a > 1) a = 1;
    if (!use_prev) { a = 1; p = B; }

    /* cameras: sprites are placed relative to cam_x (8-px units), the map by its cell column + half-cell flag */
    int csx = iround(lerp(p->cam_x * 8.0, B->cam_x * 8.0, a));
    int csy = iround(lerp(p->cam_row * 16.0, B->cam_row * 16.0, a));
    int cmx = iround(lerp(p->cam_col * 16.0 + p->col_odd * 8, B->cam_col * 16.0 + B->col_odd * 8, a));
    int bdx = iround(lerp(p->cam_x * 4.0, B->cam_x * 4.0, a));
    /* the view around the original screen: centred, kept inside the map */
    int sxo = view_shift(cmx, ox, MAP_PX_W, W), syo = view_shift(csy, (PH - 160) / 2, MAP_PX_H, PH);
    if (B->flash >= 0) memset(out, B->flash, (size_t)W * PH);       /* level_flash: one colour, sprites on top */
    else draw_world(out, W, PH, B, cmx + sxo, csy + syo, bdx + sxo);

    /* previous tick's tagged objects, by (tag, occurrence) */
    static int pidx[MAX_TAG][MAX_OCC], cnt[MAX_TAG];
    memset(pidx, 0xFF, sizeof pidx); memset(cnt, 0, sizeof cnt);
    if (use_prev)
        for (int i = 0; i < p->n; i++) {
            int t = p->ops[i].tag;
            if (t > 0 && t < MAX_TAG && cnt[t] < MAX_OCC) pidx[t][cnt[t]++] = i;
        }
    memset(cnt, 0, sizeof cnt);

    for (int i = 0; i < B->n; i++) {
        const Op *o = &B->ops[i];
        if (o->kind == OP_RECT) {                 /* screen-fixed (boss bar): as drawn, kept at the play area's foot */
            for (int y = o->y; y <= o->anim; y++)
                for (int x = o->x; x <= o->wrap; x++) {
                    int X = x + ox, Y = y + PH - 160;
                    if (x >= 0 && x < 320 && y >= 0 && y < 160 && X < W && Y < PH) out[Y * W + X] = img[y * 320 + x];
                }
            continue;
        }
        int px = o->kind == OP_SPR ? o->x * 4 : o->x;            /* screen pixels as drawn */
        double cwx = px + B->cam_x * 8.0, cwy = o->y + B->cam_row * 16.0;    /* world position */
        double wx = cwx, wy = cwy;
        int t = o->tag;
        if (t > 0 && t < MAX_TAG && cnt[t] < MAX_OCC) {
            int k = pidx[t][cnt[t]++];
            if (k >= 0) {
                const Op *q = &p->ops[k];
                if (q->kind == o->kind && (o->kind != OP_SPR || q->sheet == o->sheet)) {
                    double pwx = (q->kind == OP_SPR ? q->x * 4 : q->x) + p->cam_x * 8.0, pwy = q->y + p->cam_row * 16.0;
                    if (fabs(cwx - pwx) <= SNAP_DIST && fabs(cwy - pwy) <= SNAP_DIST) { wx = lerp(pwx, cwx, a); wy = lerp(pwy, cwy, a); }
                }
            }
        }
        int sx = iround(wx) - csx - sxo, sy = iround(wy) - csy - syo;
        if (o->kind == OP_SPR) draw_sprite(out, W, PH, o, sx - px, sy - o->y);
        else if (sx >= 0 && sx < W && sy >= 0 && sy < PH) out[sy * W + sx] = (uint8_t)o->frame;
    }
}

/* the two frames to blend for the run-ahead setting (falls back to holding the latest tick) */
static void pick_pair(const Frame **A, const Frame **B)
{
    int n = s_on ? s_runahead : 0;
    if (n >= 2 && s_nahead >= 2) { *A = ahead[0]; *B = ahead[1]; }
    else if (n >= 1 && s_nahead >= 1) { *A = cur; *B = ahead[0]; }
    else if (n >= 1) { *A = cur; *B = cur; }
    else { *A = prev; *B = cur; }
}

#define OUT_MAXW WIDE_W
static uint8_t s_idx[OUT_MAXW * 200];
static uint8_t s_last[OUT_MAXW * 200]; static int s_last_w, s_last_ph;    /* last level picture (for the sides) */

static void to_rgba(const uint8_t *idx, int W, int PH, int black_sides_from, uint32_t *rgba)
{
    vga_indexed_to_rgba_n(idx, W * 200, rgba);
    int ox = (W - 320) / 2;
    if (ox > 0)
        for (int y = black_sides_from; y < 200; y++)
            for (int x = 0; x < W; x++)
                if (x < ox || x >= ox + 320) rgba[y * W + x] = 0xFF000000u;
    (void)PH;
}

int interp_render_at(uint32_t *rgba, int *w, double a)
{
    if (!s_ready || !cur->valid) return 0;
    if (!s_on && !s_wide && !s_hudoff) return 0;                  /* Original: the VGA page itself */
    const Frame *A, *B; pick_pair(&A, &B);
    if (!s_on) { A = cur; B = cur; a = 1; }
    if (!B->valid || B->nosmooth) return 0;
    int W = view_w(), PH = play_h();
    render_pair(s_idx, W, PH, A, B, a);
    memcpy(s_last, s_idx, (size_t)W * 200); s_last_w = W; s_last_ph = PH;
    to_rgba(s_idx, W, PH, PH, rgba);
    *w = W;
    return 1;
}
int interp_render_latest(uint8_t out[320 * 200])
{
    if (!s_ready || !cur->valid || cur->nosmooth) return 0;
    render_pair(out, 320, 160, cur, cur, 1.0);
    return 1;
}

int interp_render(uint32_t *rgba, int *w)
{
    if (!s_ready) return 0;
    double a = 1;
    if (s_on) {
        int interval = g_speed_interval[(g_game_speed >= 0 && g_game_speed <= 2) ? g_game_speed : 0];
        a = (timer_clock_frac() - (double)cur->tick) / interval;
    }
    return interp_render_at(rgba, w, a);
}

/* The VGA page in an Enhanced view (menus, hint boxes, messages, fades, screens between levels): centred; while a
 * level is running, the sides keep the last level picture (with the current palette, so fades apply), else black. */
int interp_compose_vga(uint32_t *rgba, int *w, int keep_sides)
{
    int W = view_w();
    if (W == 320) return 0;
    int ox = (W - 320) / 2;
    static uint8_t vga[320 * 200];
    vga_render_indexed(vga);
    int sides = keep_sides && s_level_active && s_last_w == W;
    if (sides) memcpy(s_idx, s_last, (size_t)W * 200); else memset(s_idx, 0, (size_t)W * 200);
    for (int y = 0; y < 200; y++) memcpy(s_idx + y * W + ox, vga + y * 320, 320);
    to_rgba(s_idx, W, 200, sides ? (s_last_ph < 200 ? s_last_ph : 200) : 0, rgba);
    *w = W;
    return 1;
}

void interp_set_view(int wide, int hud_off) { s_wide = wide != 0; s_hudoff = hud_off != 0; s_last_w = 0; set_view_ext(); }
int interp_wide(void) { return s_wide; }
int interp_hud_off(void) { return s_hudoff; }
int interp_view_width(void) { return view_w(); }
