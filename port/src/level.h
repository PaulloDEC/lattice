/*
 * level.h - Hocus Pocus player / tile interaction (original 0BA5:39F3, called once per frame by the level loop).
 *
 * The player's 3 x 2 cell neighbourhood (columns -1, 0, +1) is looked up in the info layer (DS:705A, words; 0x7530 = nothing) and
 * the value picks an action by range:
 *     0 .. 22     item (table DS:2628, 42 bytes each): treasure / health / hazard / power-ups / keys / hints / checkpoint
 *     23 .. 32    teleporter (table DS:AA38 + 4*v: from cell, to cell)
 *     33 .. 55    lever (records DS:A89A, 22 bytes): toggles a background tile, and when all linked switches match
 *                 opens a rectangle of the map (transient layer)
 *     56 .. 80    door type 1 (records DS:A76E, 12 bytes): opens with a key (or freely) and clears/swaps a rectangle
 *     81 .. 105   door type 2 (records DS:A642, 12 bytes): same, but removes the foreground
 *     116 .. 365  enemy spawn trigger (table DS:8622 + 32*(v-116), 8 cells; the file record is 8 definition words followed by these 8 cells, loaded at DS:8612): queue those cells in the pending list
 * Names are inferred from the effects.
 */
#ifndef HOCUS_LEVEL_H
#define HOCUS_LEVEL_H
#include <stdint.h>

#pragma pack(push, 1)
typedef struct TileItem {      /* DS:2628, 42 bytes (the rest is a name string, unused here) */
    int16_t points;            /* +0  score; non-zero = treasure (counted towards the level goal)       */
    uint8_t health;            /* +2  health added                                                       */
    uint8_t mana;              /* +3  added to DS:7046 (cap 10)                                          */
    uint8_t kind;              /* +4  special action, see level.c                                        */
    uint8_t name[37];
} TileItem;
typedef struct Teleporter { int16_t from, to; } Teleporter;               /* DS:AA38 + 4*v           */
typedef struct Lever {         /* DS:A89A, 22 bytes */
    uint8_t mode;              /* 0: remove foreground tiles, else: swap in the alternate tile            */
    uint8_t pad;
    int16_t cell[4];           /* linked switch cells, -1 = none; cleared again when the lever fires      */
    uint8_t expect[4];         /* tile each linked cell must show                                         */
    int16_t col0, row0, col1, row1;   /* rectangle affected (inclusive)                                   */
} Lever;
typedef struct Door {          /* DS:A76E (type 1) and DS:A642 (type 2), 12 bytes */
    int16_t kind;              /* 0 free, 1 needs key 1 (DS:CE84), 2 needs key 2 (DS:CE82)               */
    uint8_t tile;              /* background tile left behind by a free door                              */
    uint8_t pad;
    int16_t col0, row0, col1, row1;
} Door;
typedef struct Hint { int16_t col, row; char text[10][50]; } Hint;        /* DS:AAC4, 504 bytes       */
#pragma pack(pop)

typedef struct LevelState {
    int16_t collected;     /* CE6A  treasures taken            */
    int16_t goal;          /* CE68  treasures in the level     */
    int16_t complete;      /* CFD8  set when collected == goal */
    int16_t lives;         /* DS:7048 (+1 per kind 1 item)     */
    int16_t flash;         /* CE78  set to 8 by kind 1 (a screen effect, handled by 220F) */
    int16_t mana;          /* DS:7046  0..10                   */
    int16_t potion_timer;  /* CE88                             */
    int16_t potion_saved;  /* CE86                             */
    int16_t key1, key2;    /* CE84, CE82                       */
    int16_t item4_flag;    /* CF10                             */
    int16_t item4_timer;   /* CF0E                             */
    uint32_t score;        /* DS:7052/7054                     */
    int16_t ce94, ce98, ce96;   /* kind 11 effect                                                    */
    int16_t teleport;      /* CF06  a teleport is pending                                             */
    int16_t tele_row_px;   /* CF04  destination row * 16                                              */
    int16_t tele_col2;     /* CF02  destination column * 2                                            */
    int16_t ce92;          /* CE92  copy of sprite slot 0 header word 15 (DS:C1BE) at teleport      */
    int16_t level_icon;    /* DS:706C  index of the level number icon (drawn as icon+1)               */
    int16_t items_left;    /* DS:704A  number of kind-1 items left on the map (counted by 3643)      */
    int16_t death_timer;   /* CE8C  counts down from 0x5A after health reaches 0                  */
    int16_t finish_timer;  /* CE8A  counts down from 0x5A after the last item is taken           */
    int16_t facing;        /* CE90  0 = facing right, 1 = left                                    */
    int16_t hint_shown;    /* CF4E                                                                    */
    int16_t check_x, check_y;   /* CF52 (= player X), CF50 (= player Y) checkpoint                   */
    uint8_t tile_blank;    /* DS:6D68  background tile of an empty cell                               */
    uint8_t tile_lever_a, tile_lever_b;   /* DS:6D69 / 6D6A  the two lever tiles                      */
} LevelState;
extern LevelState g_lv;

extern TileItem   g_tile_items[23];
extern Teleporter g_teleporters[10];
extern Lever      g_levers[23];
extern Door       g_doors1[25], g_doors2[25];
extern Hint       g_hints[10];
extern int16_t    g_spawn_groups[250][16];

typedef void (*hint_fn)(int index);
void level_set_hint_callback(hint_fn fn);      /* 0BA5:38B6 (modal hint box); not ported yet */
void level_player_tiles(void);                 /* 0BA5:39F3 */
/* HUD (0BA5:3125 / 3297 / 348D / 3643).  Icons are copied inside VRAM from page 3 (row 185) to the draw page with
 * latch writes, so the caller must have set bit mask 0 and map mask 0xF (the level loop does). */
void hud_icon8(int dest, int icon);            /* 3125: 2 bytes x 8 rows, source DS:645C + 0x39D0 + 2*icon */
void hud_icon11(int dest, int icon);           /* 3297: 2 bytes x 11 rows, source + 0x39E6 + 2*icon        */
void level_count_items(void);                  /* 3643: items_left = info cells whose item kind is 1       */
void hud_draw(void);                           /* 348D: level icon, score, health, lives, items left, keys */
#endif
