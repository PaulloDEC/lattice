#include "frontend.h"
#include "ui.h"
#include "video.h"
#include "vga.h"
#include "loop.h"
#include "loader.h"
#include "level.h"
#include "enemy.h"
#include "input.h"
#include "dat.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

FrontHooks g_front;
int g_skip_intro;
int g_sfx_volume = 255, g_music_volume = 192;
#define MENU_MUSIC 0x25A
extern int16_t g_joy_enabled;

/* ---------------------------------------------------------------- sparkles (06B8:0103 step, 039C init): 75 points flying out of (160, 112) */
#define NSPARK 75
static int32_t s_tx[360], s_ty[360];                   /* DS:4CA8 / 4CAC: DAT 5 floats * 1e6 (0548:02F9) */
static int16_t s_rad[NSPARK], s_ang[NSPARK];            /* DS:7AD6 / 7B6C */
static uint8_t s_spd[NSPARK], s_col[NSPARK], s_inc[NSPARK];   /* DS:7A8A / 7A3F / 79F4 */
static int s_spark_ready;
/* port, wide view: stars fly on to the edges of the wide picture; those beyond the 320 columns of the VGA page go
 * into g_front_side (wide coordinates, 0 = nothing), which the platform layer shows at the sides. */
int g_front_wide_extra;
uint8_t g_front_side[200][FRONT_SIDE_W];
unsigned g_front_side_steps;
static int spark_pos(int i, int *x, int *y)             /* returns non-zero when the point is outside the screen */
{
    *x = (int)((int32_t)s_rad[i] * s_tx[s_ang[i]] / 1000000) + 0xA0; *y = (int)((int32_t)s_rad[i] * s_ty[s_ang[i]] / 1000000) + 0x70;
    return *x < -g_front_wide_extra || *x > 0x13F + g_front_wide_extra || *y < 0 || *y > 0xC7;
}
static int side_x(int x) { return x + g_front_wide_extra; }
void frontend_side_clear(void) { memset(g_front_side, 0, sizeof g_front_side); }
static void spark_respawn(int i, int *x, int *y)
{
    do { s_ang[i] = (int16_t)(rand() % 360); s_rad[i] = (int16_t)(rand() % 0xA0); } while (spark_pos(i, x, y));
    s_spd[i] = (uint8_t)(rand() % 5 + 1); s_col[i] = 0; s_inc[i] = (uint8_t)(rand() % 5 + 1);
}
void sparkles_init(void)
{
    uint32_t n; uint8_t *d = dat_load(5, &n);
    if (!d || n < 2880) { free(d); return; }
    for (int i = 0; i < 360; i++) {
        float fx, fy; memcpy(&fx, d + i * 8, 4); memcpy(&fy, d + i * 8 + 4, 4);
        s_tx[i] = (int32_t)(fx * 1000000.0f); s_ty[i] = (int32_t)(fy * 1000000.0f);
    }
    free(d);
    for (int i = 0; i < NSPARK; i++) { int x, y; spark_respawn(i, &x, &y); }
    s_spark_ready = 1;
}
/* a sparkle screen ends: fade out (the side stars fade with it), then the side stars go */
static void menu_fade_out(void) { video_pal_fade_out(0x14); frontend_side_clear(); }

static void sparkles_step(void)
{
    if (!s_spark_ready) return;
    g_front_side_steps++;
    for (int i = 0; i < NSPARK; i++) {
        int x, y; spark_pos(i, &x, &y);
        if (x < 0 || x > 0x13F) { if (side_x(x) >= 0 && side_x(x) < FRONT_SIDE_W) g_front_side[y][side_x(x)] = 0; }
        else if (video_get_pixel(x, y) >= 0xF0) video_put_pixel(x, y, 0);
        s_rad[i] = (int16_t)(s_rad[i] + s_spd[i]);
        s_col[i] = (uint8_t)(s_col[i] + s_inc[i]); if (s_col[i] > 0x7F) s_col[i] = 0x7F;
        if (spark_pos(i, &x, &y)) spark_respawn(i, &x, &y);
        if (x < 0 || x > 0x13F) { if (side_x(x) >= 0 && side_x(x) < FRONT_SIDE_W) g_front_side[y][side_x(x)] = (uint8_t)(0xF0 + (s_col[i] >> 3)); }
        else if (video_get_pixel(x, y) == 0) video_put_pixel(x, y, 0xF0 + (s_col[i] >> 3));
    }
}

/* ---------------------------------------------------------------- menu data (DS:1A04..1AD4, strings from the EXE) */
#define MAXITEMS 10
static const char *k_main[]  = { "Begin a new game", "Restore an old game", "Ordering Information~", "Instructions", "Legends and hints~",
                                 "Change game options", "High scores", "Quit - return to DOS" };
static const char *k_ep[]    = { "Which game do you want to play?", "Time Tripping", "Shattered Worlds", "Warped and Weary", "Destination Home" };
static const char *k_level[] = { "Which level do you want to play?", "1) Level #1", "2) Level #2", "3) Level #3", "4) Level #4", "5) Level #5",
                                 "6) Level #6", "7) Level #7", "8) Level #8", "9) Boss Level!" };
static const char *k_skill[] = { "Choose a skill level", "Easy game - good for beginners", "Moderate - a resonable challenge", "Hard - the ultimate battle!" };
static const char *k_game[]  = { "How to play Hocus Pocus", "Abandon level & restart", "Save this game", "Restore an old game", "Change game options",
                                 "Back to the action!", "Quit to Main Menu" };
static const char *k_speed[] = { "Select Game Speed", "Slow", "Medium", "Fast" };
/* the port's option lists, styled like the speed list */
static const char *k_hud[]    = { "Select HUD Style", "Classic", "Off" };
static const char *k_aspect[] = { "Select Aspect Ratio", "Classic (4:3)", "Widescreen (16:9)" };
static const char *k_frate[]  = { "Select Framerate", "Original", "30 fps", "60 fps", "120 fps", "Display rate" };
static const char *k_padact[PAD_ACTIONS] = { "Jump", "Fire", "Look up", "Look down", "Pause" };
static const char k_hint[] = "Use UP/DOWN/LETTER to move - ENTER to select";

static struct {                       /* 0B21 state: DS:7C0C table, 7C0A count, 7C08 first-selectable flag, 7C06 title flag */
    const char *item[MAXITEMS]; int count, first, title;
} m;
static char s_opt[10][32];            /* the dynamic option strings (DS buffers; the port's options added) */
static int  s_sel[MENU_COUNT];        /* remembered selection per menu (DS:1A04, 1A3C, 1A26, 1A56, 1A80, 1A92, 1AD0; 8.. the port's) */
static int  s_cur_id, s_cur;          /* DS:82CA menu id, DS:82CE current selection */
static int  s_row[MAXITEMS], s_nsel;  /* DS:82D0 row table, DS:82CC selectable count */
static int  s_barx;                   /* DS:82F8 byte column of the marker */
static int  s_anim;                   /* DS:19D2 */

static void set_items(const char **it, int n, int first, int title)
{ for (int i = 0; i < n; i++) m.item[i] = it[i]; m.count = n; m.first = first; m.title = title; }

static void menu_setup(int id)        /* 06B8:0B21 */
{
    s_cur_id = id;
    switch (id) {
    case MENU_MAIN:    set_items(k_main, 8, 0, 0); break;
    case MENU_OPTIONS:                /* the original's six, with the port's video options; joystick -> controller remap */
        snprintf(s_opt[0], sizeof s_opt[0], "Sound is now %s", g_sound_on ? "on" : "off");
        snprintf(s_opt[1], sizeof s_opt[1], "Music is now %s", g_music_on ? "on" : "off");
        strcpy(s_opt[2], "Volume control~");
        snprintf(s_opt[3], sizeof s_opt[3], "HUD style: %s", g_port.hud_off ? "Off" : "Classic");
        snprintf(s_opt[4], sizeof s_opt[4], "Aspect ratio: %s", g_port.wide ? "16:9" : "4:3");
        snprintf(s_opt[5], sizeof s_opt[5], "Framerate: %s~", config_framerate_name(g_port.framerate));
        strcpy(s_opt[6], "Game playing speed"); strcpy(s_opt[7], "Keyboard remap"); strcpy(s_opt[8], "Controller remap");
        { static const char *it[9]; for (int i = 0; i < 9; i++) it[i] = s_opt[i]; set_items(it, 9, 1, 0); } break;
    case MENU_HUD:       set_items(k_hud, 3, 1, 1); break;
    case MENU_ASPECT:    set_items(k_aspect, 3, 1, 1); break;
    case MENU_FRAMERATE: set_items(k_frate, 6, 1, 1); break;
    case MENU_PAD: {                  /* controller remap: one line per action with its button */
        static char l[PAD_ACTIONS][32]; static const char *it[PAD_ACTIONS + 2];
        it[0] = "Select an action to remap";
        for (int a = 0; a < PAD_ACTIONS; a++) { snprintf(l[a], sizeof l[a], "%s - %s", k_padact[a], config_pad_name(g_port.pad[a])); it[a + 1] = l[a]; }
        it[PAD_ACTIONS + 1] = "Reset to defaults";
        set_items(it, PAD_ACTIONS + 2, 1, 1); break;
    }
    case MENU_EPISODE: set_items(k_ep, 5, 1, 1); break;
    case MENU_LEVEL:   set_items(k_level, 10, 1, 1); break;
    case MENU_SKILL:   set_items(k_skill, 4, 1, 1); break;
    case MENU_GAME:    set_items(k_game, 7, 1, 0); break;
    case MENU_SPEED:   set_items(k_speed, 4, 1, 1); break;
    }
}

static int is_tilde(const char *s) { size_t n = strlen(s); return n && s[n - 1] == '~'; }

static void centred(int y, int style, const char *s) { text_draw_grad((320 - text_width(s)) / 2, y, style, s); }
static void shadowed(int x, int y, int style, const char *s) { text_draw_grad(x + 1, y + 1, 0, s); text_draw_grad(x, y, style, s); }   /* 06B8:094B */

static void menu_draw(void)           /* 06B8:19A4 */
{
    video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    int first = m.title ? 1 : 0, maxw = 0, extra = 0;
    for (int i = first; i < m.count; i++) { int w = text_width(m.item[i]); if (w > maxw) maxw = w; }
    int x = (320 - maxw) / 2;
    for (int i = 0; i < m.count; i++) if (is_tilde(m.item[i])) extra += 4;
    int y;
    if (m.title) {
        y = (0x90 - (m.count + 1) * 10 + extra) / 2 + 0x28;
        centred(y, 5, m.item[0]); y += 0x14;
    } else y = (0x90 - (m.count * 10 + extra)) / 2 + 0x28;
    if (g_front.img) g_front.img(9, 0, 0xB8);
    shadowed((320 - text_width(k_hint)) / 2, 0xBC, 4, k_hint);
    if (g_front.img) g_front.img(10, 0, 0);
    s_barx = (x - 24) / 4;
    s_nsel = 0;
    for (int i = first; i < m.count; i++) {                       /* 06B8:0901 item text: whole string style 2, first letter style 4 */
        char one[2] = { m.item[i][0], 0 };
        text_draw_grad(x, y, 2, m.item[i]); text_draw_grad(x, y, 4, one);
        s_row[s_nsel++] = y - 4; y += 10;
        if (is_tilde(m.item[i])) y += 4;
    }
    video_pal_fade_in(0x14);
}

static void bar_draw(int y)           /* 06B8:0080 */
{
    int frame = (s_anim / 5) * 4;
    video_set_latch_mode(1); video_set_map_mask(0xF);
    for (int r = 0; r < 15; r++)
        for (int c = 0; c < 4; c++) {
            vga_read((uint16_t)(3 * VIDEO_PAGE_STRIDE + (r + 0xB9) * 80 + 0x1C + frame + c));
            vga_write((uint16_t)(video_draw_offset() + (r + y) * 80 + s_barx + c), 0);
        }
    video_set_latch_mode(0);
    if (++s_anim >= 0x28) s_anim = 0;
}
static void bar_clear(int y) { video_clear_pixels(s_barx * 4, y, s_barx * 4 + 15, y + 14); }

static void attract_pages(void);
static int menu_run(int main_flag)    /* 06B8:2259 */
{
    s_cur = s_sel[s_cur_id];
    if (g_front.flush_keys) g_front.flush_keys();
    uint32_t idle0 = g_front.ticks ? g_front.ticks() : 0; int idled = 0;
    for (;;) {
        if (g_front.quit && g_front.quit()) return 0xFF;
        int k = g_front.key ? g_front.key() : 0;
        if (k) idle0 = g_front.ticks ? g_front.ticks() : 0;
        if (main_flag && g_front.ticks && !g_skip_intro && g_front.ticks() - idle0 > 32143) {      /* DS:790A:7908 > 0x1194 ticks of 140 Hz */
            if (!idled) { attract_pages(); menu_setup(s_cur_id); menu_draw(); s_cur = s_sel[s_cur_id]; idle0 = g_front.ticks(); idled = 1; continue; }
            menu_fade_out(); return 0x9D;
        }
        if (k == FK_UP)   { bar_clear(s_row[s_cur]); if (--s_cur < 0) s_cur = s_nsel - 1; s_sel[s_cur_id] = s_cur; }
        else if (k == FK_DOWN) { bar_clear(s_row[s_cur]); if (++s_cur >= s_nsel) s_cur = 0; s_sel[s_cur_id] = s_cur; }
        else if (k == FK_ENTER) { menu_fade_out(); return s_cur; }
        else if (k == FK_ESC && m.first != 0) { menu_fade_out(); return 0xFF; }
        else if (k > 0 && k < 256 && k != FK_ESC) {
            int u = toupper(k), base = m.title ? 1 : 0;
            for (int i = base; i < m.count; i++)
                if (toupper((unsigned char)m.item[i][0]) == u) { bar_clear(s_row[s_cur]); s_cur = i - base; s_sel[s_cur_id] = s_cur; }
        }
        bar_draw(s_row[s_cur]);
        sparkles_step();
        if (g_front.pace) g_front.pace();
    }
}

int menu_show(int id, int main_flag) { menu_setup(id); menu_draw(); return menu_run(main_flag); }

/* ---------------------------------------------------------------- text pages and screens (06B8:3230, 3401, 3F7D, 0F3D, 44CC) */
static uint8_t s_text[1686];                       /* DS:7C34: u16 lines, u16 step (text), u16 step (blank), 20 x {u16 align, u16 style}, 20 x char[80] */
#define TXT_U16(o) ((int)(int16_t)(s_text[o] | s_text[(o) + 1] << 8))
#define TXT_LINE(i) ((const char *)s_text + 0x56 + (i) * 80)
static int load_text(int entry)
{
    uint32_t n; uint8_t *p = dat_load(entry, &n); if (!p) return 0;
    memset(s_text, 0, sizeof s_text); memcpy(s_text, p, n < sizeof s_text ? n : sizeof s_text); free(p);
    s_text[0x56 + 19 * 80 + 79] = 0; return 1;
}
static void load_menu_palette(void);
static int key_wait(void)                        /* blocking read of one key (0 = window closed) */
{
    for (;;) { if (g_front.quit && g_front.quit()) return 0; int k = g_front.key ? g_front.key() : 0; if (k) return k; sparkles_step(); if (g_front.pace) g_front.pace(); }
}
static void page_footer(int page, int total)     /* the line in the bar */
{
    char b[96];
    if (total == 1) snprintf(b, sizeof b, "Press any key to continue");
    else snprintf(b, sizeof b, "Page %d of %d%s", page + 1, total, page == 0 ? " - Press PGDN - ESC to cancel" : page == total - 1 ? " - Press PGUP - ESC to cancel" : " - Press PGUP/PGDN - ESC to cancel");
    shadowed((320 - text_width(b)) / 2, 0xBC, 4, b);
}
static void page_begin(void) { video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0); if (g_front.img) g_front.img(9, 0, 0xB8); }
static int page_height(void)
{
    int h = 0, n = TXT_U16(0);
    for (int i = 0; i < n && i < 20; i++) h += TXT_LINE(i)[0] ? TXT_U16(2) : TXT_U16(4);
    return h;
}
static void text_page(int page, int total)       /* 3230 */
{
    page_begin(); page_footer(page, total);
    if (g_front.img) g_front.img(10, 0, 0);
    int y = (0x90 - page_height()) / 2 + 0x28, n = TXT_U16(0);
    for (int i = 0; i < n && i < 20; i++) { centred(y, TXT_U16(8 + i * 4), TXT_LINE(i)); y += TXT_LINE(i)[0] ? TXT_U16(2) : TXT_U16(4); }
    video_pal_fade_in(0x14);
}
static const int16_t k_hint_img[10][3] = { {123,4,16},{121,50,25},{129,4,25},{124,4,21},{121,50,25},{-1,0,0},{-1,0,0},{-1,0,0},{-1,0,0},{121,50,25} };   /* DS:1912 */
static const int16_t k_end_img[10][3] = { {128,4,21},{121,50,25},{121,50,25},{121,50,25},{127,4,42},{121,50,25},{-1,0,0},{121,50,25},{126,4,19},{-1,0,0} };   /* DS:194E */
static const int16_t (*s_img_tab)[3] = k_hint_img;
static void hint_page(int page, int total)       /* 3401 */
{
    page_begin(); page_footer(page, total);
    if (s_img_tab[page][0] != -1 && g_front.img) g_front.img(s_img_tab[page][0], s_img_tab[page][1], s_img_tab[page][2]);
    int y = (0xB8 - page_height()) / 2, n = TXT_U16(0);
    for (int i = 0; i < n && i < 20; i++) {
        int align = TXT_U16(6 + i * 4), st = TXT_U16(8 + i * 4);
        if (align == 0) centred(y, st, TXT_LINE(i)); else text_draw_grad(align, y, st, TXT_LINE(i));
        y += TXT_LINE(i)[0] ? TXT_U16(2) : TXT_U16(4);
    }
    video_pal_fade_in(0x14);
}
static int page_nav(int page, int total)         /* 3F7D: new page, or -1 */
{
    if (g_front.flush_keys) g_front.flush_keys();
    for (;;) {
        int k = key_wait(), r = -2;
        if (!k || k == FK_ESC || total == 1) r = -1;
        else if ((k == FK_PGUP || k == FK_LEFT || k == FK_UP) && page > 0) r = page - 1;
        else if ((k == FK_PGDN || k == FK_RIGHT || k == FK_DOWN) && page < total - 1) r = page + 1;
        else if (k == FK_HOME && page != 0) r = 0;
        else if (k == FK_END && page < total - 1) r = total - 1;
        if (r != -2) { menu_fade_out(); return r; }
    }
}
static void paged(int first_entry, int total, void (*draw)(int, int))
{
    int p = 0;
    while (p != -1 && !(g_front.quit && g_front.quit())) { if (!load_text(first_entry + p)) return; draw(p, total); p = page_nav(p, total); }
}
void frontend_ordering(void) { paged(0x29, 9, text_page); }
void frontend_hints(void)    { s_img_tab = k_hint_img; paged(0x34, 10, hint_page); }

void frontend_how_to_play(int in_game)           /* 44CC */
{
    int vis = video_visible_page(), drw = video_draw_page(); uint8_t pal[768];
    if (in_game) { menu_fade_out(); memcpy(pal, video_target_palette(), 768); }
    video_pal_set_black(); load_menu_palette(); video_pal_set_black();
    video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    for (int p = 0; p < 5; p++) {
        if (p == 1) continue;                    /* page 26 is the joystick page (only shown with a joystick) */
        uint32_t n; uint8_t *d = dat_load(25 + p, &n); if (!d) break;
        video_load_pcx(d, n, 0); free(d);
        video_pal_fade_in(0x14);
        if (g_front.flush_keys) g_front.flush_keys();
        int k = key_wait(); menu_fade_out();
        if (!k || k == FK_ESC) break;
    }
    if (in_game) { memcpy(video_target_palette(), pal, 768); video_copy_page(0, 1); video_show_page_now(vis); video_set_draw_page(drw); video_pal_fade_in(1); }
    else { video_pal_set_black(); load_menu_palette(); }
}

/* ---------------------------------------------------------------- settings and high scores (HOCUS.SAV = DS:48CA..4CA7) */
static const char *s_sav = "HOCUS.SAV";
static uint8_t s_blk[0x3DE];
#define NAME_AT(ep, i) ((char *)s_blk + 0x16C + (ep) * 0x82 + (i) * 26)
#define SCORE_AT(ep, i) (s_blk + 0x374 + (ep) * 20 + (i) * 4)
static uint32_t get_score(int ep, int i) { const uint8_t *p = SCORE_AT(ep, i); return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static void set_score(int ep, int i, uint32_t v) { uint8_t *p = SCORE_AT(ep, i); p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void settings_defaults(void)
{
    static const char *nm[5] = { "Hocus Pocus", "Andre Foucault", "Jamie Cook", "Karim Sultan", "Chris tenDen" };
    memset(s_blk, 0, sizeof s_blk);
    s_blk[0] = 1; s_blk[2] = 1; s_blk[6] = 1; s_blk[0x3D8] = 255; s_blk[0x3DA] = 192;
    static const uint8_t keys[8] = { 0x0F, 0x10, 0x02, 0x03, 0x0D, 0x0E, 0x0B, 0x0C }; memcpy(s_blk + 0x16, keys, 8);
    memset(s_blk + 0x1E, 0xFF, 0x12);
    for (int i = 0; i < 9; i++) strcpy((char *)s_blk + 0x54 + i * 26, "<empty>");
    for (int ep = 0; ep < 4; ep++) for (int i = 0; i < 5; i++) { strcpy(NAME_AT(ep, i), nm[i]); set_score(ep, i, 1000000 - 200000 * i); }
}
void settings_load(const char *path)
{
    s_sav = path ? path : s_sav;
    settings_defaults();
    FILE *f = fopen(s_sav, "rb");
    if (f) { uint8_t t[sizeof s_blk]; if (fread(t, 1, sizeof t, f) == sizeof t) memcpy(s_blk, t, sizeof t); fclose(f); }
    g_sfx_volume = s_blk[0x3D8] | s_blk[0x3D9] << 8; g_music_volume = s_blk[0x3DA] | s_blk[0x3DB] << 8;
    if (g_sfx_volume > 255) g_sfx_volume = 255;
    if (g_music_volume > 255) g_music_volume = 255;
    if (g_front.apply_volume) g_front.apply_volume(g_sfx_volume, g_music_volume);
    g_sound_on = (s_blk[0] | s_blk[1]) != 0; g_music_on = (s_blk[2] | s_blk[3]) != 0; g_joy_enabled = 0;
    int sp = s_blk[6] | s_blk[7] << 8; g_game_speed = (int16_t)(sp >= 0 && sp <= 2 ? sp : 1);
    for (int ep = 0; ep < 4; ep++) for (int i = 0; i < 5; i++) NAME_AT(ep, i)[25] = 0;
    for (int i = 0; i < 8; i++) g_binding[i] = s_blk[0x16 + i] < 18 ? s_blk[0x16 + i] : g_binding[i];
}
void settings_save(void)
{
    s_blk[0] = (uint8_t)g_sound_on; s_blk[1] = 0; s_blk[2] = (uint8_t)g_music_on; s_blk[3] = 0; s_blk[4] = (uint8_t)g_joy_enabled; s_blk[5] = 0; s_blk[6] = (uint8_t)g_game_speed; s_blk[7] = 0;
    for (int i = 0; i < 8; i++) s_blk[0x16 + i] = g_binding[i];
    s_blk[0x3D8] = (uint8_t)g_sfx_volume; s_blk[0x3D9] = 0; s_blk[0x3DA] = (uint8_t)g_music_volume; s_blk[0x3DB] = 0;
    FILE *f = fopen(s_sav, "wb"); if (f) { fwrite(s_blk, 1, sizeof s_blk, f); fclose(f); }
}

int hs_check(int ep, uint32_t score)             /* 1BAE */
{
    for (int i = 0; i < 5; i++) {
        if (get_score(ep, i) >= score) continue;
        for (int j = 3; j >= i; j--) { strcpy(NAME_AT(ep, j + 1), NAME_AT(ep, j)); set_score(ep, j + 1, get_score(ep, j)); }
        NAME_AT(ep, i)[0] = 0; set_score(ep, i, score);
        return i + 1;
    }
    return 0;
}

static void hs_screen_begin(int ep)              /* title, footer, images, rows: shared by 2018 and 1C9B; returns via s_hs_y the row y of each entry */
{
    video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    char b[48]; snprintf(b, sizeof b, "High Scores For Game %d", ep + 1);
    centred(0x48, 5, b);
}
static int s_hs_y[5];
static void hs_rows(int ep)
{
    int y = 0x48 + 0x14;
    for (int i = 0; i < 5; i++) {
        int st = i == 0 ? 4 : 3; char b[16];
        text_draw_grad(0x1B, y, st, NAME_AT(ep, i));
        snprintf(b, sizeof b, "%u", (unsigned)get_score(ep, i));
        text_draw_grad(0x122 - text_width(b), y, st, b);
        s_hs_y[i] = y; y += 10; if (i == 0) y += 10;
    }
}
static void hs_frame(const char *footer)
{
    if (g_front.img) g_front.img(9, 0, 0xB8);
    shadowed((320 - text_width(footer)) / 2, 0xBC, 4, footer);
    if (g_front.img) g_front.img(10, 0, 0);
}
void hs_view(void)                               /* 2018(0) */
{
    for (int ep = 0; ep < 4; ep++) {
        hs_screen_begin(ep); hs_rows(ep); hs_frame("Press any key to continue");
        video_pal_fade_in(0x14);
        if (g_front.flush_keys) g_front.flush_keys();
        int k = key_wait(); menu_fade_out();
        if (!k || k == FK_ESC) break;
    }
    video_pal_set_black(); load_menu_palette();
}
void hs_enter(int ep, int pos)                   /* 1C9B */
{
    char *name = NAME_AT(ep, pos);
    hs_screen_begin(ep); hs_rows(ep); hs_frame("Enter name then press ENTER");
    int y = s_hs_y[pos], st = pos == 0 ? 4 : 3;
    video_pal_fade_in(0x14);
    if (g_front.flush_keys) g_front.flush_keys();
    for (;;) {
        video_clear_pixels(0x1B, y, 0x122, y + 9);
        { char sc[16]; snprintf(sc, sizeof sc, "%u", (unsigned)get_score(ep, pos)); text_draw_grad(0x122 - text_width(sc), y, st, sc); }
        text_draw_grad(0x1B, y, st, name);
        char cur[2] = { '_', 0 }; text_draw_grad(text_width(name) + 0x1B, y, 2, cur);
        for (;;) {
            if (g_front.quit && g_front.quit()) { menu_fade_out(); return; }
            int c = g_front.text ? g_front.text() : 0, k = g_front.key ? g_front.key() : 0, len = (int)strlen(name);
            if (!c && !g_front.text && k >= 0x20 && k <= 0x7A) c = k;
            if (k == FK_ENTER) { video_clear_pixels(0x1B, y, 0x122, y + 9); { char sc[16]; snprintf(sc, sizeof sc, "%u", (unsigned)get_score(ep, pos)); text_draw_grad(0x122 - text_width(sc), y, st, sc); } text_draw_grad(0x1B, y, st, name); settings_save(); menu_fade_out(); return; }
            if (k == FK_BKSP && len > 0) { name[len - 1] = 0; break; }
            if (c >= 0x20 && c <= 0x7A && len < 25) { name[len] = (char)c; name[len + 1] = 0; break; }
            if (g_front.pace) g_front.pace();
        }
    }
}

/* Episode ending: the fanfare picture, then the story pages of the episode (DAT 62.. with the pictures of DS:194E), episode 4 also the final picture. */
void frontend_episode_end(int ep)
{
    static const int first[4] = { 0x3E, 0x40, 0x42, 0x46 }, count[4] = { 2, 2, 4, 2 }, img0[4] = { 0, 2, 4, 8 };
    video_set_draw_page(0); video_clear_draw_page(); video_set_draw_page(1); video_clear_draw_page(); video_set_draw_page(0);
    { uint32_t n; uint8_t *d = dat_load(4, &n); if (d) { video_load_pcx(d, n, 0); free(d); } }
    if (g_front.voc) g_front.voc(0x267);
    video_show_page_now(0); video_pal_fade_in(0x3C); video_pal_fade_white(); menu_fade_out();
    video_pal_set_black(); load_menu_palette();
    s_img_tab = &k_end_img[img0[ep]];
    paged(first[ep], count[ep], hint_page);
    if (ep == 3) {
        video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
        { uint32_t n; uint8_t *d = dat_load(0x48, &n); if (d) { video_load_pcx(d, n, 0); free(d); } }
        if (g_front.music) g_front.music(0x262, 0);
        video_pal_fade_in(0x14);
        if (g_front.flush_keys) g_front.flush_keys();
        key_wait(); menu_fade_out();
        video_pal_set_black(); load_menu_palette();
        if (g_front.music && g_music_on) g_front.music(MENU_MUSIC, 1);
    }
}

/* ---------------------------------------------------------------- volume control (06B8:137D, box 1316) */
static void vol_box(int pos, int y, int state)    /* 16 x 13 latch copy from page 3 (byte column 0x3C + 4 * state, row 0xB9) */
{
    video_set_latch_mode(1); video_set_map_mask(0xF);
    for (int r = 0; r < 13; r++) for (int c = 0; c < 4; c++) {
        vga_read((uint16_t)(3 * VIDEO_PAGE_STRIDE + (r + 0xB9) * 80 + 0x3C + state * 4 + c));
        vga_write((uint16_t)(video_draw_offset() + (r + y) * 80 + (pos * 4 + 8) + c), 0);
    }
    video_set_latch_mode(0);
}
static void volume_screen(void)
{
    int y = 0x3E, ypos[2], pos[2] = { g_sfx_volume / 16, g_music_volume / 16 };
    video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    centred(y, 5, "Sound FX Volume"); y += 0x14;
    for (int i = 0; i < 16; i++) vol_box(i, y, 0);
    vol_box(pos[0], y, 1); ypos[0] = y; y += 0x28;
    centred(y, 5, "Music Volume"); y += 0x14;
    for (int i = 0; i < 16; i++) vol_box(i, y, 0);
    vol_box(pos[1], y, 1); ypos[1] = y;
    { static const char h[] = "UP/DOWN/LEFT/RIGHT to move - ENTER when done"; if (g_front.img) g_front.img(9, 0, 0xB8); shadowed((320 - text_width(h)) / 2, 0xBC, 4, h); if (g_front.img) g_front.img(10, 0, 0); }
    video_pal_fade_in(0x14);
    if (g_front.flush_keys) g_front.flush_keys();
    int sel = 0, blink = 0, tick = 0;
    for (;;) {
        if (g_front.quit && g_front.quit()) break;
        int k = g_front.key ? g_front.key() : 0;
        if (k == FK_ENTER || k == FK_ESC) break;
        if (k) {
            if (k == FK_RIGHT && pos[sel] < 15) { vol_box(pos[sel], ypos[sel], 0); pos[sel]++; }
            else if (k == FK_LEFT && pos[sel] > 0) { vol_box(pos[sel], ypos[sel], 0); pos[sel]--; }
            else if (k == FK_UP) { vol_box(pos[sel], ypos[sel], 1); sel = 0; }
            else if (k == FK_DOWN) { vol_box(pos[sel], ypos[sel], 1); sel = 1; }
            g_sfx_volume = pos[0] * 16 + 15; g_music_volume = pos[1] * 16 + 15;
            if (g_front.apply_volume) g_front.apply_volume(g_sfx_volume, g_music_volume);
        }
        vol_box(pos[sel], ypos[sel], blink < 10);        /* the cursor blinks */
        if (++blink > 20) blink = 0;
        if (tick == 0 && g_front.sfx) g_front.sfx(rand() & 15);      /* a random effect now and then so the level can be judged */
        if (++tick > 50) tick = 0;
        if (g_front.pace) g_front.pace();
    }
    settings_save(); menu_fade_out();
}

/* ---------------------------------------------------------------- saved games (06B8:2B8B restore, 25CC save): 9 slots in HOCUS.SAV */
#define SLOT_EP(i)    ((int16_t)(s_blk[0x1E + 2 * (i)] | s_blk[0x1F + 2 * (i)] << 8))
#define SLOT_NAME(i)  ((char *)s_blk + 0x54 + (i) * 26)
static uint32_t get_slot_score(int i) { const uint8_t *p = s_blk + 0x148 + 4 * i; return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int s_slot_sel;                                   /* DS:19DA */
static void slot_ui_begin(const char *title)
{
    video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    centred(0x3B, 5, title);
    if (g_front.img) g_front.img(9, 0, 0xB8);
    { static const char h[] = "Use UP/DOWN/NUMBER to move - ENTER to select"; shadowed((320 - text_width(h)) / 2, 0xBC, 4, h); }
    if (g_front.img) g_front.img(10, 0, 0);
    s_barx = 4; s_nsel = 9;
    int y = 0x3B + 0x14;
    for (int i = 0; i < 9; i++) {
        char n[4]; snprintf(n, sizeof n, "%d", i + 1);
        text_draw_grad(0x23, y, 4, n); text_draw_grad(0x2D, y, 3, SLOT_NAME(i));
        s_row[i] = y - 4; y += 10;
    }
    video_pal_fade_in(0x14);
}
static int slot_pick(int save)                           /* returns the slot, or -1 for Esc */
{
    s_cur = s_slot_sel; if (g_front.flush_keys) g_front.flush_keys();
    for (;;) {
        if (g_front.quit && g_front.quit()) return -1;
        int k = g_front.key ? g_front.key() : 0;
        if (k == FK_UP)   { bar_clear(s_row[s_cur]); if (--s_cur < 0) s_cur = 8; s_slot_sel = s_cur; }
        else if (k == FK_DOWN) { bar_clear(s_row[s_cur]); if (++s_cur > 8) s_cur = 0; s_slot_sel = s_cur; }
        else if (k >= '1' && k <= '9') { bar_clear(s_row[s_cur]); s_cur = k - '1'; s_slot_sel = s_cur; }
        else if (k == FK_ESC) { menu_fade_out(); return -1; }
        else if (k == FK_ENTER && (save || SLOT_EP(s_cur) != -1)) { menu_fade_out(); return s_cur; }
        bar_draw(s_row[s_cur]);
        sparkles_step();
        if (g_front.pace) g_front.pace();
    }
}
int frontend_restore(void)
{
    slot_ui_begin("Select RESTORE slot");
    int i = slot_pick(0);
    if (i < 0) return 0;
    g_episode = SLOT_EP(i);
    g_lv.level_icon = (int16_t)(s_blk[0x30 + 2 * i] | s_blk[0x31 + 2 * i] << 8);
    g_difficulty = (int16_t)(s_blk[0x42 + 2 * i] | s_blk[0x43 + 2 * i] << 8);
    g_lv.score = g_score_saved = get_slot_score(i);
    return 1;
}
void frontend_save(void)
{
    slot_ui_begin("Select SAVE slot");
    int i = slot_pick(1);
    if (i < 0) return;
    /* name entry on the slot row (an empty slot starts blank) */
    char *name = SLOT_NAME(i); int y = s_row[i] + 4;
    if (SLOT_EP(i) == -1) name[0] = 0;
    slot_ui_begin("Select SAVE slot");                    /* the pick faded the screen out: draw it again for the edit */
    for (;;) {
        video_clear_pixels(0x2D, y, 0x13F, y + 9);
        text_draw_grad(0x2D, y, 3, name);
        char cur[2] = { '_', 0 }; text_draw_grad(text_width(name) + 0x2D, y, 2, cur);
        int done = 0;
        for (;;) {
            if (g_front.quit && g_front.quit()) return;
            int c = g_front.text ? g_front.text() : 0, k = g_front.key ? g_front.key() : 0, len = (int)strlen(name);
            if (!c && !g_front.text && k >= 0x20 && k <= 0x7A) c = k;
            if (k == FK_ESC) { menu_fade_out(); return; }
            if (k == FK_ENTER) { done = 1; break; }
            if (k == FK_BKSP && len > 0) { name[len - 1] = 0; break; }
            if (c >= 0x20 && c <= 0x7A && len < 24) { name[len] = (char)c; name[len + 1] = 0; break; }
            if (g_front.pace) g_front.pace();
        }
        if (done) break;
    }
    s_blk[0x1E + 2 * i] = (uint8_t)g_episode; s_blk[0x1F + 2 * i] = 0;
    s_blk[0x30 + 2 * i] = (uint8_t)g_lv.level_icon; s_blk[0x31 + 2 * i] = 0;
    s_blk[0x42 + 2 * i] = (uint8_t)g_difficulty; s_blk[0x43 + 2 * i] = 0;
    { uint32_t v = g_score_saved; uint8_t *p = s_blk + 0x148 + 4 * i; p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
    settings_save(); menu_fade_out();
}

/* ---------------------------------------------------------------- define keys (06B8:17DA, 10AC) */
static void key_hint(void)
{
    static const char h[] = "Press LETTER to select - ESC to exit";
    if (g_front.img) g_front.img(9, 0, 0xB8);
    shadowed((320 - text_width(h)) / 2, 0xBC, 4, h);
    if (g_front.img) g_front.img(10, 0, 0);
}
static void pick_key(int action)                  /* 10AC: choose a key for one action from the 18 listed keys */
{
    static const char *an[8] = { "Left", "Right", "Jump", "Fire", "Up", "Down", "Scroll up", "Scroll down" };
    char b[48]; video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    snprintf(b, sizeof b, "Select new key for: %s", an[action]); centred(0x48, 5, b);
    key_hint();
    for (int i = 0; i < 18; i++) {
        int col = i / 6, x = col == 0 ? 0x1B : col == 1 ? 0x7D : 0xC8, y = 0x5C + (i % 6) * 10;
        char l[2] = { (char)('A' + i), 0 };
        text_draw_grad(x, y, 4, l); text_draw_grad(x + 10, y, 2, "-"); text_draw_grad(x + 0x14, y, 2, g_key_name[i]);
    }
    video_pal_fade_in(0x14);
    if (g_front.flush_keys) g_front.flush_keys();
    for (;;) {
        int k = key_wait();
        if (!k || k == FK_ESC) break;
        if (k >= 'A' && k <= 'R') { g_binding[action] = (uint8_t)(k - 'A'); break; }
    }
    menu_fade_out();
}
static void define_keys(void)
{
    static const char *an[8] = { "Left", "Right", "Jump", "Fire", "Up", "Down", "Scroll up", "Scroll down" };
    for (;;) {
        video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
        centred(0x3E, 5, "Select a key to define");
        key_hint();
        for (int i = 0; i < 8; i++) {
            int y = 0x5C + i * 10; char l[2] = { (char)('A' + i), 0 };
            text_draw_grad(0x46, y, 4, l); text_draw_grad(0x50, y, 2, "-"); text_draw_grad(0x5A, y, 2, an[i]);
            const char *kn = g_key_name[g_binding[i] % 18]; text_draw_grad(0xFA - text_width(kn), y, 3, kn);
        }
        video_pal_fade_in(0x14);
        if (g_front.flush_keys) g_front.flush_keys();
        int k = key_wait();
        if (!k || k == FK_ESC) { menu_fade_out(); return; }
        menu_fade_out();
        if (k >= 'A' && k <= 'H') pick_key(k - 'A');
    }
}

/* ---------------------------------------------------------------- quit confirmation (06B8:2EE1, 0E52, 0FBC) */
static int confirm_quit(void)
{
    video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    centred(0x61, 5, "Reminder: Save your game before quitting");
    centred(0x61 + 0x14, 3, "Quit game?");
    if (g_front.img) g_front.img(9, 0, 0xB8);
    { static const char h[] = "Press Y for yes - N for no - ESC to exit"; shadowed((320 - text_width(h)) / 2, 0xBC, 4, h); }
    if (g_front.img) g_front.img(10, 0, 0);
    video_pal_fade_in(0x14);
    if (g_front.flush_keys) g_front.flush_keys();
    int yes = 0;
    for (;;) { int k = key_wait(); if (!k || k == FK_ESC || k == 'N') break; if (k == 'Y') { yes = 1; break; } }
    menu_fade_out();
    return yes;
}

/* ---------------------------------------------------------------- in-game overlays (06B8:3048 / 30DE / 3187 / 1744 / 44CC(1)) */
static int overlay(int (*fn)(void), int keep_black_on_result)
{
    int vis = video_visible_page(), drw = video_draw_page(); uint8_t pal[768];
    menu_fade_out(); memcpy(pal, video_target_palette(), 768);
    video_pal_set_black(); load_menu_palette();
    int r = fn();
    memcpy(video_target_palette(), pal, 768);
    if (r && keep_black_on_result) { video_clear_draw_page(); return r; }
    video_copy_page(0, 1); video_show_page_now(vis); video_set_draw_page(drw); video_pal_fade_in(1);
    return r;
}
static int ov_save(void) { frontend_save(); return 0; }
static int ov_volume(void) { volume_screen(); return 0; }
void frontend_overlay_save(void) { overlay(ov_save, 0); }
int  frontend_overlay_restore(void) { return overlay(frontend_restore, 1); }
int  frontend_overlay_quit(void) { return overlay(confirm_quit, 1); }
void frontend_overlay_volume(void) { overlay(ov_volume, 0); }
void frontend_overlay_help(void) { frontend_how_to_play(1); }

/* ---------------------------------------------------------------- options (0548:0745) */
static void toggle_music(void)
{
    g_music_on = !g_music_on;
    if (!g_music_on) { if (g_front.music_off) g_front.music_off(); }
    else if (g_front.music) g_front.music(MENU_MUSIC, 1);
}
/* port: a choice list (like the speed list), opened on the current value; returns the choice or -1 */
static int choose(int id, int current)
{
    s_sel[id] = current;
    int r = menu_show(id, 0);
    return r == 0xFF ? -1 : r;
}
static void port_apply(void) { if (g_front.port_apply) g_front.port_apply(); }

/* port: controller remap - the Define Keys screens, but picked with the menu marker so a controller can work it */
static void pad_prompt(int action)
{
    char b[48];
    video_pal_set_black(); video_set_draw_page(0); video_clear_draw_page(); video_show_page_now(0);
    snprintf(b, sizeof b, "Press a button for: %s", k_padact[action]); centred(0x48, 5, b);
    snprintf(b, sizeof b, "Currently %s", config_pad_name(g_port.pad[action])); centred(0x5C, 3, b);
    centred(0x70, 2, "D-pad, stick and Menu stay fixed");
    {
        static const char h[] = "Press MENU or ESC to cancel";
        if (g_front.img) g_front.img(9, 0, 0xB8);
        shadowed((320 - text_width(h)) / 2, 0xBC, 4, h);
        if (g_front.img) g_front.img(10, 0, 0);
    }
    video_pal_fade_in(0x14);
    if (g_front.flush_keys) g_front.flush_keys();
    for (;;) {
        if (g_front.quit && g_front.quit()) break;
        int k = g_front.key ? g_front.key() : 0;
        if (k == FK_ESC) break;
        int btn = g_front.pad_button ? g_front.pad_button() : -1;
        if (btn == 6) break;                                  /* Menu (Start) */
        if (btn >= 0 && config_pad_assignable(btn)) {
            for (int a = 0; a < PAD_ACTIONS; a++)             /* a button does one thing: swap with its old action */
                if (a != action && g_port.pad[a] == btn) g_port.pad[a] = g_port.pad[action];
            g_port.pad[action] = btn;
            port_apply();
            break;
        }
        if (g_front.pace) g_front.pace();
    }
    menu_fade_out();
    if (g_front.flush_keys) g_front.flush_keys();
}
static void pad_remap(void)
{
    for (;;) {
        int r = menu_show(MENU_PAD, 0);
        if (r == 0xFF || (g_front.quit && g_front.quit())) return;
        if (r < PAD_ACTIONS) pad_prompt(r);
        else {                                                /* reset to defaults (the other settings stay) */
            PortSettings keep = g_port; config_defaults();
            g_port.hud_off = keep.hud_off; g_port.wide = keep.wide; g_port.framerate = keep.framerate;
            port_apply();
        }
    }
}

static void options_loop(void)
{
    for (;;) {
        int r = menu_show(MENU_OPTIONS, 0), c;
        switch (r) {
        case 0: g_sound_on = !g_sound_on; break;
        case 1: toggle_music(); break;
        case 2: volume_screen(); break;
        case 3: if ((c = choose(MENU_HUD, g_port.hud_off)) >= 0) { g_port.hud_off = c; port_apply(); } break;
        case 4: if ((c = choose(MENU_ASPECT, g_port.wide)) >= 0) { g_port.wide = c; port_apply(); } break;
        case 5: if ((c = choose(MENU_FRAMERATE, g_port.framerate)) >= 0) { g_port.framerate = c; port_apply(); } break;
        case 6: if ((c = choose(MENU_SPEED, g_game_speed)) >= 0) g_game_speed = (int16_t)c; break;
        case 7: define_keys(); break;
        case 8: pad_remap(); break;
        default: settings_save(); return;
        }
        if (g_front.quit && g_front.quit()) return;
    }
}

/* ---------------------------------------------------------------- main menu (0548) */
static void load_menu_palette(void)
{
    if (g_front.pal) { g_front.pal(DAT_PAL_LO, 0); g_front.pal(DAT_PAL_HI, 128); }
}
static void draw_marker_sheet(void)
{
    video_set_draw_page(3);
    if (g_front.img) { g_front.img(14, 0x1C, 0xB9); g_front.img(15, 0x3C, 0xB9); }
}

static void scores_for(int episode)
{
    int pos = hs_check(episode, g_lv.score);
    if (pos) hs_enter(episode, pos - 1);
}

static void play_levels(int episode, int skill, int resume)
{
    if (!resume) { g_episode = (int16_t)episode; g_difficulty = (int16_t)skill; g_lv.level_icon = 0; g_lv.score = 0; }
    g_lv.mana = 1;
    int pause = !resume && episode == 0;
    for (;;) {
        g_lv.mana = 1; g_score_saved = g_lv.score;
        int r = level_run(0, pause); pause = 0;
        if (r == 1) {
            if (g_lv.level_icon < 8) { g_lv.level_icon++; continue; }
            frontend_episode_end(g_episode); scores_for(g_episode); return;           /* after the boss level: ending pages, then the high score check */
        }
        else if (r == 0) g_lv.score = g_score_saved;                              /* died: replay with the score of the level start */
        else { if (r == -1) scores_for(g_episode); return; }                        /* -1 quit to the main menu, -2 window closed */
    }
}

static int wait_ms(uint32_t ms)                   /* 1392:0108: wait ms milliseconds or a key; returns 1 when a key ended it */
{
    uint32_t t0 = g_front.ticks ? g_front.ticks() : 0;
    if (g_front.flush_keys) g_front.flush_keys();
    for (;;) {
        if (g_front.quit && g_front.quit()) return 1;
        if (g_front.key && g_front.key()) return 1;
        if (g_front.ticks && g_front.ticks() - t0 >= ms) return 0;
        if (g_front.pace) g_front.pace();
    }
}
static void show_pcx(int entry)
{
    uint32_t n; uint8_t *d = dat_load(entry, &n); if (d) { video_load_pcx(d, n, 0); free(d); }
}
static void black_page0(void) { video_set_draw_page(0); video_show_page_now(0); }

void frontend_intro(void)                         /* 0B97:0001 */
{
    if (g_skip_intro) return;
    video_pal_fade_out(0x28); black_page0();
    show_pcx(0x11); video_pal_fade_in(0x14); wait_ms(15000); menu_fade_out();
    black_page0(); show_pcx(1);
    if (g_front.music) g_front.music(0x257, 0);
    video_pal_fade_in(0x14); wait_ms(9000); video_pal_fade_out(0x1E);
    if (g_front.music) g_front.music(MENU_MUSIC, 1);
    black_page0(); show_pcx(2);
    { uint32_t n; uint8_t *d = dat_load(3, &n); if (d) { video_load_img(d, n, 0, 0xBC); free(d); } }     /* registered: the "registered version" strip */
    video_pal_fade_in(0x28);
    if (g_front.voc) g_front.voc(0x263);
    wait_ms(4000); video_pal_fade_out(0x1E);
    video_pal_set_black(); load_menu_palette();
}

static void attract_pages(void)                   /* 06B8:42CB: two story pictures */
{
    menu_fade_out();
    for (int i = 0; i < 2; i++) {
        show_pcx(0x17 + i); video_pal_fade_in(0x14);
        int key = wait_ms(2500); video_pal_fade_out(0xF);
        if (key) break;
    }
    video_pal_set_black(); load_menu_palette();
}

void frontend_demo(void)                        /* 0548:0570 */
{
    g_episode = 0; g_lv.level_icon = (int16_t)((rand() % 5) * 2); g_difficulty = 1; g_lv.score = 0; g_score_saved = 0; g_lv.mana = 1;
    if (g_front.music_off) g_front.music_off();
    level_run(2, 0);
    if (g_front.music) g_front.music(MENU_MUSIC, 1);
}

void frontend_run(void)
{
    int quit = 0;
    sparkles_init();
    if (g_skip_intro) { if (g_front.music) g_front.music(MENU_MUSIC, 1); } else frontend_intro();
    while (!quit && !(g_front.quit && g_front.quit())) {
        load_menu_palette(); draw_marker_sheet();
        int k = menu_show(MENU_MAIN, 1);
        switch (k) {
        case 0x9D: frontend_demo(); break;
        case 0: { int ep = menu_show(MENU_EPISODE, 0); if (ep == 0xFF) break;
                  int sk = menu_show(MENU_SKILL, 0);   if (sk == 0xFF) break;
                  if (g_front.music_off) g_front.music_off();
                  play_levels(ep, sk, 0);
                  if (g_front.music) g_front.music(MENU_MUSIC, 1);
                  break; }
        case 5: options_loop(); break;
        case 7: quit = 1; break;
        case 1: if (frontend_restore()) { if (g_front.music_off) g_front.music_off(); play_levels(g_episode, g_difficulty, 1); if (g_front.music) g_front.music(MENU_MUSIC, 1); } break;
        case 2: frontend_ordering(); break;
        case 3: frontend_how_to_play(0); break;
        case 4: frontend_hints(); break;
        case 6: hs_view(); break;
        default: break;      /* restore: not ported yet */
        }
    }
}

int frontend_game_menu(void)          /* 06B8:4600 */
{
    int vis = video_visible_page(), drw = video_draw_page();
    uint8_t pal[768]; memcpy(pal, video_target_palette(), 768);
    menu_fade_out();
    video_pal_set_black();
    load_menu_palette();
    int result = 0;
    for (;;) {
        int k = menu_show(MENU_GAME, 0);
        if (k == 1) { result = 1; break; }
        if (k == 6) { result = 2; break; }
        if (k == 4) { options_loop(); continue; }
        if (k == 0xFF || k == 5) break;
        if (k == 0) { frontend_how_to_play(0); continue; }
        if (k == 2) { frontend_save(); result = 0; break; }
        if (k == 3) { if (frontend_restore()) { result = 3; break; } continue; }
    }
    memcpy(video_target_palette(), pal, 768);
    if (result == 0) { video_copy_page(0, 1); video_show_page_now(vis); video_set_draw_page(drw); video_pal_fade_in(1); }
    else video_clear_draw_page();
    return result;
}
