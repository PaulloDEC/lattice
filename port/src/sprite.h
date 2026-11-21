/*
 * sprite.h - Hocus Pocus sprite blitter (original 0BA5:249E).
 *
 * Sprites are stored as opcode streams (see the sprite format PDF) and drawn VRAM->VRAM using
 * VGA write mode 1 (latch copy), so a pixel byte is a whole 4-pixel group in the plane(s)
 * selected by the current map mask.
 *
 *   op 0 m      : set map mask = m, restart at (x, y)         (starts each plane group)
 *   op 1 w16    : skip w bytes (dest += w, col += w, wraps into the next row at `wrap`)
 *   op 2        : copy one byte from sprite memory to the screen (clipped to 80 x 160)
 *   anything else ends the frame
 *
 * Tables (original DS offsets): stream far pointers DS:7070[sheet], source page DS:6458 + DS:C188,
 * per-sheet base DS:C18C[sheet], frame table DS:C1DC[sheet*110 + anim*20 + frame] (stream start)
 * and the same table + 40 words (source offset of the frame's pixels).
 */
#ifndef HOCUS_SPRITE_H
#define HOCUS_SPRITE_H
#include <stdint.h>

#define SPR_SLOTS 10
#define SPR_SHEETS 40
#define SPR_TAB_WORDS (SPR_SHEETS * 110 + 64)

typedef struct SpriteState {
    const uint8_t *stream[SPR_SHEETS]; /* DS:7070 far pointers (RAM copy of each opcode stream) */
    uint16_t src_off;                  /* DS:6458 offset part (segment is VRAM)                 */
    uint16_t page_off;                 /* DS:C188                                               */
    uint16_t base[SPR_SHEETS];         /* DS:C18C                                               */
    uint16_t hdr[SPR_SLOTS][30];       /* DS:C1A0 + slot*220: record header words (loader fills) */
    uint16_t tab[SPR_TAB_WORDS];       /* DS:C1DC                                               */
    uint16_t cursor;                   /* DS:C18A  running pixel-memory cursor (pixels)         */
    uint16_t marker0, marker1;         /* DS:CE72, CE70  VRAM offsets of the two solid-colour cells */
} SpriteState;

extern SpriteState g_spr;

/* ---- loader (original 0BA5:2242).  Only slots 0..SPR_SLOTS-1 are ever loaded; each slot holds
 * one of the 40 sheets stored in DAT entry 130 (0x82) as 220-byte records:
 *   +0 u32 offset of this sheet's data in the entry, +4..0x37 name (ASCII), +0x38 u16 stream size,
 *   +0x3A u16 pixel size, +0x3C 80 words: frame-start table (40) and pixel-offset table (40).
 * data = opcode stream, then pixel bytes.  Pixels are plotted linearly (320 per row) into draw
 * page 2 at DS:C188 (page_off); base[slot] = cursor/4.  ids[i] = -1 skips a slot.
 * Returns 0 on success, -1 if a stream is larger than SPR_STREAM_MAX or the entry is too short. */
#define SPR_STREAM_MAX 16384
int sprite_load_sheets(const uint8_t *entry, uint32_t size, const int16_t ids[SPR_SLOTS]);
uint8_t *sprite_stream_buf(int slot);

/* ---- level setup step (inside 0BA5:453A, 49D8-4AD8): pack the tile graphics.
 * Precondition: draw page 3 holds the level's tile PCX (20 tiles of 16x16 per row, in reading
 * order).  Each tile is copied (VRAM->VRAM, latch mode, 16 rows x 4 bytes x 4 planes = 64 bytes
 * per plane) into the start of page 2 until a tile whose top-left pixel is 0xFF.  Then writes
 * marker cells 0x60 and 0x70, sets marker0/1, page_off (= next free VRAM offset) and cursor = 0. */
void sprite_init(void);                /* source pointer = page 2, cursor/page_off = 0 */
void sprite_pack_tiles(void);

/* sheet=a0 wrap=a1 x=a2 (byte columns) y=a3 anim=a4 (0 or 1) frame=a5 : arguments in call order */
void sprite_draw(int sheet, int wrap, int x, int y, int anim, int frame);

/* ---- draw recording (Enhanced display smoothing; NULL = off, the default).  Every blit calls g_rec_blit
 * with its arguments before drawing; the pixel effects and the boss bar call g_rec_pixel / g_rec_rect.
 * g_rec_tag names the object being drawn (set by the caller, 0 = unknown) so a renderer can match it
 * between ticks.  Recording never changes what is drawn. */
extern int g_rec_tag;
extern void (*g_rec_blit)(int mode, int clip, int sheet, int wrap, int x, int y, int anim, int frame);
extern void (*g_rec_pixel)(int x, int y, int color, int tag);       /* x, y in pixels */
extern void (*g_rec_rect)(int x0, int y0, int x1, int y1);           /* screen-fixed area, pixels, inclusive */
enum { TAG_PLAYER = 0x10, TAG_TELEPORT = 0x11, TAG_PICON = 0x20, TAG_ENEMY = 0x100, TAG_SHOT = 0x200,
       TAG_PROJ = 0x300, TAG_SCORE = 0x400, TAG_PUFF = 0x500, TAG_BURST = 0x1000 };
/* The other blitters of 0BA5, same frame tables and opcode streams:
 *   23BA sprite_draw_raw(sheet, x, y, anim, frame)            own pixels, no clipping
 *   26B3 sprite_solid(0, sheet, wrap, x, y, anim, frame)       solid cell marker0 (colour 0x60), clipped
 *   2896 sprite_solid(1, ...)                                  solid cell marker1 (colour 0x70), clipped
 *   25FF sprite_solid_raw(0, sheet, x, y, anim, frame)         marker0, no clipping
 *   27E2 sprite_solid_raw(1, ...)                              marker1, no clipping
 * The solid ones draw a one-colour silhouette of the sprite (hit flash / shadow). */
void sprite_draw_raw(int sheet, int x, int y, int anim, int frame);
void sprite_solid(int marker, int sheet, int wrap, int x, int y, int anim, int frame);
void sprite_solid_raw(int marker, int sheet, int x, int y, int anim, int frame);
#endif
