/*
 * ui.h - text output of the original UI module (code segment 06B8).  Proportional 8-row bitmap font (DAT entry 0, 90 glyphs of 8 bytes for
 * the characters 0x21..0x7A, bit 0 = leftmost pixel); the glyph width is its highest set bit + 1, plus 1 pixel of spacing; space = 4 pixels.
 * Characters outside 0x21..0x7A (other than space) are skipped.  Text goes to the current draw page, one plane group per byte, in latch-off mode.
 */
#ifndef HOCUS_UI_H
#define HOCUS_UI_H
#include <stdint.h>

void ui_set_font(const uint8_t font[720]);        /* DS:6440 (far pointer in the original) */
const uint8_t *ui_font(void);                      /* glyph g (char 0x21 + g) = 8 rows, bit i = pixel i */
/* 06B8:05E9 text_draw(x, y, colour, str): solid colour; leaves latch mode ON (bit mask 0) like the original. */
void text_draw(int x, int y, int colour, const char *s);
/* 06B8:071F text_draw_grad(x, y, style, str): style 0 = flat colour 0x80, else row r of the glyph uses colour table[style-1] + r (a vertical
 * gradient); leaves latch mode OFF. */
void text_draw_grad(int x, int y, int style, const char *s);
/* 06B8:087C width in pixels (sum of glyph width + 2, spaces 4) */
int  text_width(const char *s);

/* The two modal text boxes of the level code.  Both draw on the VISIBLE page (so they appear at once), wait for a key release and then a key
 * press (DS:7099 holds the last scancode, >= 0x80 = release), and set the draw page back to the hidden page.  `ui_set_poll` gives the platform
 * a chance to pump events / present frames while waiting; the callback returns non-zero to abort the wait (window closed). */
typedef int (*ui_poll_fn)(void);
void ui_set_poll(ui_poll_fn fn);
void ui_message(const char *text);        /* 0BA5:37FC  one centred line, shadowed */
void ui_hint_box(int index);              /* 0BA5:38B6  up to 10 lines of g_hints[index] */

/* 06B8:4889 the level complete / died screen (original joystick wait path not ported).  finished: 1 = level done, 0 = died.  Draws the results
 * on a black screen (page 0), adds the bonuses to g_lv.score, fades in, animates the star field until a key is pressed and fades out.  Uses
 * g_exit_clock (the game clock at the moment the level ended) for the elapsed time.  Always returns 0. */
typedef struct UiHooks {
    void (*img)(int entry, int x, int y);   /* 05D8:0A70  planar image from the DAT */
    int  (*kbhit)(void);                    /* 0:30BD  a key press is waiting */
    void (*getch)(void);                    /* 0:2E77  discard one key press */
} UiHooks;
extern UiHooks g_ui_hooks;
extern uint32_t g_exit_clock;               /* DS:7904 */
int  ui_end_screen(int finished, int goal, int collected);
#endif
