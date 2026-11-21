/*
 * loader.h - level data loading (original 0BA5:4549..49A0 of the level function 453A).
 */
#ifndef HOCUS_LOADER_H
#define HOCUS_LOADER_H
#include <stdint.h>

extern int16_t g_episode;          /* DS:706E  0..3 */
extern int16_t g_enemies_total;    /* DS:CE6E  spawn cells holding an enemy definition that is in use */
extern int16_t g_level_start[4];   /* DS:703C  {-, start column, start row, enemy fire chance} */

/* Loads the map files of (g_episode, g_lv.level_icon) from the open DAT and resets the per-level counters:
 * start position / checkpoint, goal count, enemy total, tile constants.  Returns 0 on success. */
int level_load_data(void);

/* ---- level setup (original 0BA5:4549..4D44): everything the level function does before its frame loop starts.
 * The external loaders are reached through hooks so a test can record the call sequence; the defaults read HOCUS.DAT
 * (dat_open must have been called) and use the video / sprite modules. */
typedef struct LevelHooks {
    void (*music)(int entry, int loop);        /* 136B:01FF  start the level music (DAT entry 0x258 + table; the call passes the raw value) */
    void (*pcx)(int entry, int apply);         /* 05D8:0673  PCX from DAT entry into the draw page */
    void (*pal)(int entry, int slot, int apply);   /* 05D8:0AFF */
    void (*img)(int entry, int x, int y);      /* 05D8:0A70 */
    void (*sprites)(const int16_t ids[10]);    /* 0BA5:2242 (ids are in DS:C174) */
    void (*input)(void);                       /* 1392:0200  hook the keyboard */
    void (*demo)(int entry);                   /* 068C:00F9 read the demo data (mode 2) */
} LevelHooks;
extern LevelHooks g_level_hooks;               /* NULL members -> defaults */
extern int16_t g_sprite_ids[10];               /* DS:C174 */
extern int16_t g_demo_len;                     /* DS:CF3C */
extern uint8_t g_demo_buf[0x4000];             /* demo data (original: far buffer DS:48C6) */
/* mode 0 normal, 1 recording, 2 playing a demo.  Returns 0 on success. */
int level_setup(int mode);
#endif
