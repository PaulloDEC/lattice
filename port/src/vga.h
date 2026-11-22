/*
 * vga.h - software model of the VGA subset Hocus Pocus uses.
 *
 * The game runs in "Mode Y" (mode 13h unchained): 320x200, 256 colours, 4 planes,
 * 80 bytes per row, pixel x lives in plane (x & 3) at byte (x >> 2).  The original
 * code writes A000:xxxx directly and programs these registers (found in segments
 * 05D8 / 1439):
 *     SEQ 02 map mask        GC 04 read-plane select      GC 08 bit mask (0 = latch copy)
 *     SEQ 01 bit 5 screen off  CRTC 0C/0D display start   DAC 3C7/3C8/3C9 palette
 * Anything else (write modes, set/reset, rotate/logic ops) is never touched.
 */
#ifndef HOCUS_VGA_H
#define HOCUS_VGA_H
#include <stdint.h>

#define VGA_PLANE_SIZE 0x10000

void    vga_reset(void);                         /* state right after the mode-Y set */
void    vga_set_map_mask(uint8_t m);             /* SEQ 02 */
void    vga_set_read_plane(uint8_t p);           /* GC 04  */
void    vga_set_bit_mask(uint8_t m);             /* GC 08  */
void    vga_set_screen_on(int on);               /* SEQ 01 bit 5 (inverted) */
void    vga_set_start(uint16_t addr);            /* CRTC 0C/0D */
uint16_t vga_get_start(void);

/* CPU access to A000:off (write mode 0, read mode 0; reads load the latches) */
void    vga_write(uint16_t off, uint8_t v);
uint8_t vga_read(uint16_t off);
/* rep movsb / rep stosb semantics, forward, 16-bit offset wrap */
void    vga_memcopy(uint16_t dst, uint16_t src, uint16_t n);
void    vga_memset(uint16_t dst, uint16_t n, uint8_t v);

/* DAC (6-bit components, as the hardware holds them) */
void    vga_pal_set(int index, const uint8_t rgb6[3]);
void    vga_pal_get(int index, uint8_t rgb6[3]);

/* Render the visible 320x200 frame: indexed pixels, then RGBA with the palette. */
void    vga_render_indexed(uint8_t out[320 * 200]);
void    vga_render_rgba(uint32_t out[320 * 200]);    /* 0xAABBGGRR little-endian bytes R,G,B,A */
void    vga_render_indexed_at(uint16_t start, uint8_t out[320 * 200]);   /* any page, ignoring screen-off */
uint8_t vga_palette(int index, int component);       /* DAC entry, 6-bit r / g / b */
void    vga_indexed_to_rgba(const uint8_t idx[320 * 200], uint32_t out[320 * 200]);
void    vga_indexed_to_rgba_n(const uint8_t *idx, int n, uint32_t *out);   /* with the DAC palette (and screen-off) */

/* test/debug access */
const uint8_t *vga_plane(int p);
uint8_t vga_get_map_mask(void);
uint8_t vga_get_read_plane(void);
uint8_t vga_get_bit_mask(void);
int     vga_get_screen_on(void);
#endif
