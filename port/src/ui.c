#include "ui.h"
#include "video.h"
#include "vga.h"
#include "input.h"
#include "level.h"
#include "frame.h"

static const uint8_t *s_font;
static const int16_t k_grad[10] = { 192, 200, 208, 216, 224, 232, 112, 104, 208, 0 };    /* DS:1AE6 */

void ui_set_font(const uint8_t font[720]) { s_font = font; }
const uint8_t *ui_font(void) { return s_font; }

static int glyph_maxbit(int g)
{
    int m = 0;
    for (int row = 0; row < 8; row++) { uint8_t b = s_font[g * 8 + row]; for (int i = 0; i < 8; i++) if ((b >> i & 1) && i > m) m = i; }
    return m;
}

/* draw one glyph at pixel column si; colour(row) gives the byte written */
static void draw_glyph(int g, int si, int y, int (*colour)(int, int, int), int a, int b)
{
    int base = (int16_t)(y * 80 + (si >> 2));
    int sh = si % 4;
    for (int row = 0; row < 8; row++, base = (int16_t)(base + 80)) {
        uint16_t v = (uint16_t)((uint16_t)s_font[g * 8 + row] << 8) >> (4 - sh);
        uint8_t col = (uint8_t)colour(row, a, b);
        uint16_t d = video_draw_offset();
        vga_set_map_mask((uint8_t)(v >> 4)); vga_write((uint16_t)(d + base), col);
        vga_set_map_mask((uint8_t)(v >> 8)); vga_write((uint16_t)(d + base + 1), col);
        vga_set_map_mask((uint8_t)(v >> 12)); vga_write((uint16_t)(d + base + 2), col);
    }
}
static int col_flat(int row, int c, int unused) { (void)row; (void)unused; return c; }
static int col_grad(int row, int flat, int base) { return flat ? 0x80 : base + row; }

static void run(int x, int y, const char *s, int (*colour)(int, int, int), int a, int b)
{
    int si = x;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0x21 && c <= 0x7A) { int g = c - 0x21; draw_glyph(g, si, y, colour, a, b); si += glyph_maxbit(g) + 2; }
        else if (c == 0x20) si += 4;
    }
}

void text_draw(int x, int y, int colour, const char *s)
{
    video_set_latch_mode(0);
    run(x, y, s, col_flat, colour, 0);
    video_set_latch_mode(1);
}

void text_draw_grad(int x, int y, int style, const char *s)
{
    video_set_latch_mode(0);
    int flat = style == 0, base = 0;
    if (!flat) base = k_grad[(style - 1) < 0 ? 0 : ((style - 1) > 9 ? 9 : style - 1)];
    run(x, y, s, col_grad, flat, base);
}

int text_width(const char *s)
{
    int si = 0;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c >= 0x21 && c <= 0x7A) si += glyph_maxbit(c - 0x21) + 2;
        else if (c == 0x20) si += 4;
    }
    return si;
}

/* ---- modal boxes (0BA5:37FC, 38B6) ---------------------------------------------------------------------------- */
static ui_poll_fn s_poll;
void ui_set_poll(ui_poll_fn fn) { s_poll = fn; }

static void wait_press(void)
{
    if (!s_poll) return;
    while (g_in.last_scancode < 0x80) if (s_poll()) return;      /* key still down: wait for its release */
    while (g_in.last_scancode >= 0x80) if (s_poll()) return;     /* then wait for the next key press */
}

void ui_message(const char *text)
{
    video_set_draw_page(video_visible_page());
    int x = (320 - text_width(text)) / 2;
    text_draw(x + 1, 0x51, 1, text);
    text_draw(x, 0x50, 0x68, text);
    wait_press();
    video_set_draw_page(video_visible_page() == 0);
}

void ui_hint_box(int index)
{
    int lines = 0, maxw = 0;
    const Hint *h = &g_hints[index];
    for (int i = 0; i < 10; i++) { int w = text_width(h->text[i]); if (!w) break; if (w > maxw) maxw = w; lines++; }
    video_set_draw_page(video_visible_page());
    int y0 = 0x50 - lines * 12, x0 = 0xA0 - maxw / 2;
    for (int i = 0; i < lines; i++) {
        text_draw(x0 + 1, y0 + i * 12 + 1, 1, h->text[i]);
        text_draw(x0, y0 + i * 12, 0x68, h->text[i]);
    }
    if (!g_demo_playback) wait_press();
    video_set_draw_page(video_visible_page() == 0);
}

/* ---------------- end screen 06B8:4889 ---------------- */
#include "stars.h"
#include "dat.h"
#include "enemy.h"
#include "loader.h"
#include <stdio.h>
#include <string.h>

UiHooks g_ui_hooks;
uint32_t g_exit_clock;

static void put_centred(int y, int style, const char *s)
{
    text_draw_grad((320 - text_width(s)) / 2, y, style, s);
}
static void put_centred_shadow(int y, int style, const char *s)         /* 06B8:094B */
{
    int x = (320 - text_width(s)) / 2;
    text_draw_grad(x + 1, y + 1, 0, s);
    text_draw_grad(x, y, style, s);
}
static void cat_num(char *buf, int v) { char t[16]; snprintf(t, sizeof t, "%d", (int16_t)v); strcat(buf, t); }

int ui_end_screen(int finished, int goal, int collected)
{
    char buf[160];
    uint16_t elapsed = (uint16_t)((int32_t)g_exit_clock / 140);
    int y = 0x28 + (0x90 - (finished * 0x28 + 0x40)) / 2;
    int32_t bonus = 0;
    video_pal_set_black();
    video_set_draw_page(0);
    video_clear_draw_page();
    video_show_page_now(0);
    put_centred(y, 4, finished ? "Castle complete - congratulations!" : "Tough break - you'll get it next time!");
    y += 0x10;
    strcpy(buf, "Results for Level "); cat_num(buf, g_lv.level_icon + 1);
    put_centred(y, 2, buf); y += 0x18;
    strcpy(buf, "Treasures found: "); cat_num(buf, collected);
    strcat(buf, "  Treasures available: "); cat_num(buf, goal);
    put_centred(y, 3, buf); y += 0x10;
    float pct = 0.0f;
    if (collected != 0) pct = (float)((long double)(int16_t)collected / (long double)(int16_t)goal * 100.0f);
    strcpy(buf, "Accuracy: ");
    {
        long double v = pct;
        int64_t n = (v != v || v >= 9.3e18L || v <= -9.3e18L) ? INT64_MIN : (int64_t)v;
        cat_num(buf, (int)(uint16_t)n);
    }
    strcat(buf, "%  ");
    if (!finished) strcat(buf, "Castle not complete");
    else if (pct < 100.0f) strcat(buf, "Less than 100% - NO BONUS");
    else {
        if (g_difficulty == 0) { strcat(buf, "BONUS 25,000 points!"); bonus += 25000; }
        if (g_difficulty == 1) { strcat(buf, "BONUS 50,000 points!"); bonus += 50000; }
        if (g_difficulty >= 2) { strcat(buf, "BONUS 75,000 points!"); bonus += 75000; }
    }
    put_centred(y, 2, buf); y += 0x18;
    if (finished) {
        uint16_t par = g_par_time[g_episode * 9 + g_lv.level_icon];
        strcpy(buf, "Time to beat: "); cat_num(buf, par);
        strcat(buf, "  Your time: "); cat_num(buf, elapsed);
        put_centred(y, 3, buf); y += 0x10;
        if (par > elapsed) {
            if (g_difficulty == 0) { strcpy(buf, "BONUS 25,000 points!"); bonus += 25000; }
            if (g_difficulty == 1) { strcpy(buf, "BONUS 50,000 points!"); bonus += 50000; }
            if (g_difficulty >= 2) { strcpy(buf, "BONUS 75,000 points!"); bonus += 75000; }
        } else strcpy(buf, "Not fast enough - NO BONUS");
        put_centred(y, 2, buf); y += 0x18;
    }
    g_lv.score += (uint32_t)bonus;
    if (g_ui_hooks.img) g_ui_hooks.img(9, 0, 0xB8);
    put_centred_shadow(0xBC, 4, "Press any key");
    if (g_ui_hooks.img) g_ui_hooks.img(10, 0, 0);
    video_pal_fade_in(0x14);
    if (g_ui_hooks.kbhit) {
        while (g_ui_hooks.kbhit()) g_ui_hooks.getch();
        int got = 0;
        while (!got) { if (g_ui_hooks.kbhit()) got = 1; stars_step(); }
        while (g_ui_hooks.kbhit()) g_ui_hooks.getch();
    }
    video_pal_fade_out(0x14);
    return 0;
}
