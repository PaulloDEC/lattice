/*
 * map.h - Hocus Pocus map/tile renderer (original segment 124A).
 *
 * The tile graphics are packed in VRAM (see sprite_pack_tiles): tile t is 64 bytes at
 * src_off + t*64, as 16 rows of 4 bytes, copied VRAM->VRAM in latch mode (map mask 0xF - the
 * CALLER sets latch mode and the mask).  The map is 240 cells wide; two layers of bytes
 * (0xFF = empty): layer1 (DS:7062) wins over layer2 (DS:7066); a cell empty in both is left alone.
 * The screen shows 20 x 10 cells of 16x16 px (80 bytes x 160 rows).
 */
#ifndef HOCUS_MAP_H
#define HOCUS_MAP_H
#include <stdint.h>

#define MAP_WIDTH 240

/* 124A:0F15 - redraw the 20x10 cell window whose top-left cell is map_off, tile-aligned. */
void map_draw_even(uint16_t dest_off, uint16_t src_off, const uint8_t *layer1, const uint8_t *layer2, uint16_t map_off);
/* 124A:0FE5 - same, shifted half a tile left: right half of the first cell, 19 whole cells, left
 * half of the 21st (used when the camera X is odd). */
void map_draw_odd(uint16_t dest_off, uint16_t src_off, const uint8_t *layer1, const uint8_t *layer2, uint16_t map_off);

/* ---- parallax backdrop (124A:0006 even / 124A:02AA odd) -------------------------------------
 * Called AFTER the map draw with the backdrop page as source (the game passes DS:645C + 0x7D0):
 * every cell empty in both layers is filled from the backdrop, whose horizontal origin is
 * cam_x % 80 bytes (half the camera speed = parallax) with wrap at 80 bytes per row.  Same
 * map_off / layer arguments as map_draw_*.  Quirk kept: in the odd variant's last cell the wrap
 * test uses > 0x50 (not >=), exactly as the original.  These two set latch mode and map mask 0xF themselves. */
void backdrop_draw_even(uint16_t dest_off, uint16_t src_off, const uint8_t *layer1, const uint8_t *layer2, uint16_t map_off, int cam_x);
void backdrop_draw_odd(uint16_t dest_off, uint16_t src_off, const uint8_t *layer1, const uint8_t *layer2, uint16_t map_off, int cam_x);

/* ---- per-frame tile pass (original 0BA5:2C81) -------------------------------------------------
 * Walks the visible window (10 rows x 21 columns from the camera cell) and
 *   1. resolves the transient layer (DS:7056): 1 = remove the foreground tile, 2 = replace it
 *      with the alternate layer's tile (DS:705E); these are applied at once when `timer` (CE76,
 *      decremented first) is 0, otherwise one cell in three per frame (RNG), with a puff of dust;
 *   2. animates the background layer: tile table DS:6D6C, 3 bytes per tile {first, last, type}:
 *      type 1 = loop first..last every frame; type 2 = loop, but a resting tile (value == first)
 *      only starts with chance 1/20 per frame (RNG); type 0 / values >= 0xF0 are static. */
typedef struct MapState {
    uint8_t *dyn;          /* DS:7056  transient cells (0 / 1 / 2), 240 x 60  */
    uint8_t *alt;          /* DS:705E  replacement tiles                       */
    uint8_t *fg;           /* DS:7062  foreground layer (layer1)               */
    uint8_t *bg;           /* DS:7066  background layer (layer2)               */
    const uint8_t *anim;   /* DS:6D6C  240 x {first, last, type}               */
    int16_t timer;         /* DS:CE76                                           */
} MapState;
extern MapState g_map;
void map_animate(void);    /* uses g_cam (cam_row, cam_col, col_odd) */
#endif
