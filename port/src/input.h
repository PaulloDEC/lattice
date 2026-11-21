/*
 * input.h - Hocus Pocus keyboard/controller state.
 *
 * Replaces the original INT 9 handler (code segment 1392, ISR at 1392:026E).
 * Behaviour is a 1:1 port of the ISR logic; the hardware parts (ports 0x60/0x61,
 * PIC EOI, vector hooking, BIOS shift-flag clearing) are gone.
 *
 * The platform layer feeds DOS set-1 scancodes (make = 0x01..0x58, break = make+0x80)
 * through input_scancode(), and may inject controller input with input_action().
 * Game code reads the same flags the DOS game read (original DS offsets in comments).
 */
#ifndef HOCUS_INPUT_H
#define HOCUS_INPUT_H
#include <stdint.h>

/* Bindable actions, in the order of the in-game "Define key controls" menu.
 * Binding slots live at DS:1558..155F. */
enum Action {
    ACT_LEFT, ACT_RIGHT, ACT_JUMP, ACT_FIRE, ACT_UP, ACT_DOWN,
    ACT_SCROLL_UP, ACT_SCROLL_DOWN, ACT_COUNT
};

#define KEY_TABLE_SIZE 18

typedef struct InputState {
    /* --- bound actions: set on make, cleared on break ---------------------- */
    uint8_t left;          /* 70AC */
    uint8_t right;         /* 70AB */
    uint8_t down;          /* 70AD */
    uint8_t scroll_up;     /* 70A4 */
    uint8_t scroll_down;   /* 70A3 */
    /* --- edge-triggered: set on make only while the latch is armed; the latch
     *     re-arms on break.  The GAME clears these flags itself once consumed. */
    uint8_t jump;          /* 70A9, latch 70A6 */
    uint8_t fire;          /* 70AA, latch 70A5 */
    uint8_t up, up2;       /* 70AE, 70AF, latch 70A7 (two flags, same trigger) */
    uint8_t latch_jump, latch_fire, latch_up;

    /* --- fixed keys ---------------------------------------------------------- */
    uint8_t esc;           /* 70A8  level: 1 while ESC held */
    /* sticky, set on make, cleared by the game: */
    uint8_t pause;         /* 7098  P   */
    uint8_t hot_v;         /* 709B  V   */
    uint8_t hot_s;         /* 709C  S   */
    uint8_t hot_m;         /* 709D  M   */
    uint8_t hot_c;         /* 709E  C   */
    uint8_t f1;            /* 709F  */
    uint8_t f10;           /* 70A0  */
    uint8_t f3;            /* 70A1  */
    uint8_t f2;            /* 70A2  */

    uint8_t last_scancode; /* 7099 */
    uint8_t any_key;       /* 709A  set on any make code; game clears */

    /* The original ISR skipped the six-action section while any of the words
     * 48CE, CE8C, CE8A, 790C were non-zero (menus / text entry).  The game sets this. */
    uint8_t suppress_bindings;

    /* Scancode-sum detector (CFDE).  Reset by F/Q/B make; every scancode
     * (including break codes) is added.  See input_cheat_cb. */
    uint16_t sum;
} InputState;

extern InputState g_in;
extern uint8_t g_binding[ACT_COUNT];            /* indices into g_key_scancode[] */
extern const uint8_t g_key_scancode[KEY_TABLE_SIZE]; /* DS:14FE */
extern const char *const g_key_name[KEY_TABLE_SIZE];

/* Called when the scancode sum hits a magic value (0x5BA, 0x3D6, 0x378, 0x4D8). */
typedef void (*input_cheat_cb)(uint16_t sum);
void input_set_cheat_callback(input_cheat_cb cb);

void input_init_defaults(void);   /* default bindings: arrows, Ctrl, Alt, PgUp/PgDn */
void input_reset(void);           /* original 1392:0195 */
void input_scancode(uint8_t sc);  /* one byte from the keyboard (ISR body) */
void input_action(int action, int down); /* controller/mouse injection */
#endif
