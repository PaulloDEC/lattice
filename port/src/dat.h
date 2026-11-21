/*
 * dat.h - HOCUS.DAT access.  The file has no directory of its own: the table of (offset, size) pairs is generated from
 * the EXE (tools/gen_tables.py -> dat_tables.c).  Entry numbers are the ones of notes section 2.
 */
#ifndef HOCUS_DAT_H
#define HOCUS_DAT_H
#include <stdint.h>
#include <stddef.h>

#define DAT_ENTRIES 652
extern const uint32_t g_dat_fat[DAT_ENTRIES][2];     /* offset, size */
extern const int16_t g_lvl_map_base[40];             /* DS:2A1A  per episode * 10 + level */
extern const int16_t g_lvl_tileset[40];              /* DS:29CA */
extern const int16_t g_lvl_backdrop[40];             /* DS:2A6A */
extern const uint16_t g_par_time[36];                /* DS:198A  par time (s) per episode * 9 + level */
extern const int16_t g_lvl_music[36];                /* DS:2ACE  per episode * 9 + level */
extern const int16_t g_lvl_plat_tiles[80];           /* DS:2B16  {DS:CF4C, DS:CF4A} per episode * 10 + level */
extern const int16_t g_boss_init[4];                /* DS:2BB6 / 2BBE / 2BC6 / 2BCE: column, row, cell, facing of the final boss */
extern const uint16_t g_tick_table[4];               /* DS:14F8  {8, 7, 6, ...} ticks per frame */

enum { DAT_FONT = 0, DAT_PAL_LO = 7, DAT_PAL_HI = 8, DAT_BACKDROP_PAL = 73, DAT_BACKDROP = 89, DAT_TILESET = 105,
       DAT_SPRITES = 130, DAT_MAP = 131 /* + 36 * type + level */ };

int       dat_open(const char *path);                /* 0 on success */
void      dat_close(void);
uint32_t  dat_size(int entry);                       /* 0 for a bad entry */
/* read entry into dest (at most cap bytes); returns the entry size, or 0 on an error / if it does not fit */
uint32_t  dat_read(int entry, void *dest, uint32_t cap);
/* read entry into a fresh malloc() buffer (free() it); NULL on error */
uint8_t  *dat_load(int entry, uint32_t *size);
#endif
