/*
 * frontend.h - the menu system of the original (06B8 menu engine, 0548 main menu loop), Original mode.
 *
 * A menu is a title (optional) plus items.  Layout (06B8:19A4): the widest item is centred, the block is centred vertically
 * around row 0x28 + 0x48, a title uses gradient style 5, items style 2 with their first letter overdrawn in style 4, a footer
 * bar (IMG 9 at row 0xB8, IMG 10 at the top) carries the hint text.  The selection marker is a 16x15 animated wand taken
 * from video page 3 (IMG 14 / 15 at byte columns 0x1C / 0x3C, row 0xB9) and latch-copied next to the selected item.
 * The platform supplies keys and pacing; everything else is drawn through video.c / ui.c.
 */
#ifndef HOCUS_FRONTEND_H
#define HOCUS_FRONTEND_H
#include <stdint.h>

#define FK_UP    0x148
#define FK_DOWN  0x150
#define FK_ENTER 13
#define FK_ESC   27
#define FK_BKSP  8
#define FK_HOME  0x147
#define FK_PGUP  0x149
#define FK_LEFT  0x14B
#define FK_RIGHT 0x14D
#define FK_END   0x14F
#define FK_PGDN  0x151

typedef struct FrontHooks {
    int  (*key)(void);                  /* next key press (ASCII, FK_*), 0 = none */
    void (*pace)(void);                 /* once per menu iteration: pump events, present, delay ~16 ms */
    int  (*quit)(void);                 /* window closed */
    void (*music)(int entry, int loop); /* 136B:01FF */
    void (*music_off)(void);            /* 136B:0261 */
    void (*pal)(int entry, int slot);   /* 05D8:0AFF without applying */
    void (*img)(int entry, int x, int y);   /* 05D8:0A70 */
    void (*flush_keys)(void);
    int  (*text)(void);                 /* next typed character (cased ASCII), 0 = none; NULL = use key() */
    void (*apply_volume)(int sfx, int music);   /* 156D:02A3 / 15D4:015E */
    void (*sfx)(int id);                /* 136B:000B */
    uint32_t (*ticks)(void);            /* milliseconds */
    void (*voc)(int entry);             /* 136B:01B8 play one digitised sound (DAT entry) */
    /* port: */
    void (*port_apply)(void);           /* g_port (config.h) changed: apply it and save HOCUS.CFG */
    int  (*pad_button)(void);           /* next controller button press (config.h codes), -1 = none */
    int  (*display_max)(void);
    int  sound_hw;                      /* an audio device is open (music needs one) */
    int  (*gm_available)(void);         /* a SoundFont for General MIDI is next to the program */          /* the largest window multiple (2..8) that fits the current display */
} FrontHooks;
extern FrontHooks g_front;

/* Menu ids of 06B8:0B21 */
enum { MENU_MAIN, MENU_EPISODE, MENU_OPTIONS, MENU_LEVEL, MENU_SKILL, MENU_GAME, MENU_SPEED = 7,
       MENU_HUD, MENU_ASPECT, MENU_FRAMERATE, MENU_PAD, MENU_DISPLAY, MENU_MUSICDEV, MENU_COUNT };   /* 8.. the port's */

/* 0B21 + 19A4 + 2259: returns the selected item (0-based, title excluded), 0xFF for Esc, 0x9D for the idle time-out. */
int  menu_show(int id, int main_flag);

/* 0548:035A  the main menu loop; returns when the player quits (or the window is closed). */
void frontend_run(void);
/* 0B97:0001 the start-up screens (three pictures, any key skips one), 06B8:42CB attract pages, 0548:0570 demo. */
void frontend_intro(void);
void frontend_demo(void);
extern int g_skip_intro;
/* 06B8:4600  the in-game Esc menu: 0 = back to the action, 1 = abandon level & restart, 2 = quit to the main menu, 3 = loaded a game. */
int  frontend_game_menu(void);

/* 06B8:44CC how to play (PCX pages 25, 27, 28, 29), in_game = the F1 variant that saves / restores the screen. */
void frontend_how_to_play(int in_game);
/* 06B8:407D ordering information (9 text pages), 06B8:40BD legends and hints (10 pages with pictures). */
void frontend_ordering(void);
void frontend_hints(void);

/* 06B8:2B8B restore (1 = a game was loaded into g_episode / level / difficulty / score) and 06B8:25CC save. */
int  frontend_restore(void);
void frontend_save(void);

/* In-game overlays (each saves the pages / palette, runs a screen and restores them): F1 help, F2 save, F3 restore (returns 1 when a game was
 * loaded), F10 quit confirmation (1 = quit), V volume. */
void frontend_overlay_help(void);
void frontend_overlay_save(void);
int  frontend_overlay_restore(void);
int  frontend_overlay_quit(void);
void frontend_overlay_volume(void);

/* HOCUS.SAV: settings block (sound, music, joystick, speed words at its start); missing file = defaults. */
void settings_load(const char *path);
void settings_save(void);

extern int g_sfx_volume, g_music_volume;   /* DS:4CA2 / 4CA4 */

/* High scores (HOCUS.SAV: names char[4][5][26] at +0x16C, scores u32[4][5] at +0x374). */
int  hs_check(int episode, uint32_t score);    /* 06B8:1BAE  inserts when it qualifies; returns position + 1, 0 = not a high score */
void hs_enter(int episode, int pos);           /* 06B8:1C9B  name entry screen */
void hs_view(void);                            /* 06B8:2018(0) the four tables */
void frontend_episode_end(int episode);        /* 06B8:4D76 + 40FD..41BD: fanfare screen and the ending pages of an episode */
#endif
