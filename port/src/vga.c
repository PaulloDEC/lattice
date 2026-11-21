#include "vga.h"
#include <string.h>

static struct {
    uint8_t  plane[4][VGA_PLANE_SIZE];
    uint8_t  latch[4];
    uint8_t  map_mask, read_plane, bit_mask;
    int      screen_on;
    uint16_t start;
    uint8_t  pal[256][3];
} v;

void vga_reset(void)
{
    memset(&v, 0, sizeof v);
    v.map_mask = 0x0F; v.bit_mask = 0xFF; v.screen_on = 1;
}
void vga_set_map_mask(uint8_t m)   { v.map_mask = m & 0x0F; }
void vga_set_read_plane(uint8_t p) { v.read_plane = p & 3; }
void vga_set_bit_mask(uint8_t m)   { v.bit_mask = m; }
void vga_set_screen_on(int on)     { v.screen_on = on != 0; }
void vga_set_start(uint16_t a)     { v.start = a; }
uint16_t vga_get_start(void)       { return v.start; }

void vga_write(uint16_t off, uint8_t val)
{
    for (int p = 0; p < 4; p++)
        if (v.map_mask & (1u << p))
            v.plane[p][off] = (uint8_t)((val & v.bit_mask) | (v.latch[p] & ~v.bit_mask));
}
uint8_t vga_read(uint16_t off)
{
    for (int p = 0; p < 4; p++) v.latch[p] = v.plane[p][off];
    return v.latch[v.read_plane];
}
void vga_memcopy(uint16_t dst, uint16_t src, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        uint8_t b = vga_read((uint16_t)(src + i));
        vga_write((uint16_t)(dst + i), b);
    }
}
void vga_memset(uint16_t dst, uint16_t n, uint8_t val)
{
    for (uint16_t i = 0; i < n; i++) vga_write((uint16_t)(dst + i), val);
}

void vga_pal_set(int i, const uint8_t c[3]) { for (int k = 0; k < 3; k++) v.pal[i & 255][k] = c[k] & 0x3F; }
void vga_pal_get(int i, uint8_t c[3])       { for (int k = 0; k < 3; k++) c[k] = v.pal[i & 255][k]; }

void vga_render_indexed(uint8_t out[320 * 200])
{
    for (int y = 0; y < 200; y++)
        for (int x = 0; x < 320; x++)
            out[y * 320 + x] = v.screen_on
                ? v.plane[x & 3][(uint16_t)(v.start + y * 80 + (x >> 2))] : 0;
}
void vga_render_rgba(uint32_t out[320 * 200])
{
    static uint8_t idx[320 * 200];
    vga_render_indexed(idx);
    for (int i = 0; i < 320 * 200; i++) {
        const uint8_t *c = v.screen_on ? v.pal[idx[i]] : v.pal[0];
        uint32_t r = (uint32_t)((c[0] << 2) | (c[0] >> 4));
        uint32_t g = (uint32_t)((c[1] << 2) | (c[1] >> 4));
        uint32_t b = (uint32_t)((c[2] << 2) | (c[2] >> 4));
        out[i] = r | (g << 8) | (b << 16) | 0xFF000000u;
    }
}

const uint8_t *vga_plane(int p) { return v.plane[p & 3]; }
uint8_t vga_get_map_mask(void)  { return v.map_mask; }
uint8_t vga_get_read_plane(void){ return v.read_plane; }
uint8_t vga_get_bit_mask(void)  { return v.bit_mask; }
int     vga_get_screen_on(void) { return v.screen_on; }
