#include "sprite.h"
#include "video.h"
#include "vga.h"

SpriteState g_spr;
int g_rec_tag;
void (*g_rec_blit)(int mode, int clip, int sheet, int wrap, int x, int y, int anim, int frame);
void (*g_rec_pixel)(int x, int y, int color, int tag);
void (*g_rec_rect)(int x0, int y0, int x1, int y1);

#define I16(v) ((int16_t)(v))

void sprite_init(void) { g_spr.src_off = 2 * VIDEO_PAGE_STRIDE; g_spr.cursor = 0; g_spr.page_off = 0; }

void sprite_pack_tiles(void)
{
    int16_t idx = 0;
    int done = 0;
    for (int16_t si = 0; si < 0x118 && !done; si++) {
        video_set_latch_mode(0);
        int16_t ty = (si / 20) << 4, tx = (si % 20) << 4;
        if (video_get_pixel(tx, ty) == 0xFF) {
            g_spr.cursor = (uint16_t)(idx << 2);
            done = 1; break;
        }
        video_set_latch_mode(1);
        video_set_map_mask(0xF);
        for (int16_t r = 0; r < 16; r++)
            for (int16_t k = 0; k < 4; k++) {
                uint16_t src = (uint16_t)(video_draw_offset() + (uint16_t)((r + ty) * 80) + (si % 20) * 4 + k);
                vga_write((uint16_t)(g_spr.src_off + idx), vga_read(src));
                idx++;
            }
    }
    vga_write((uint16_t)(g_spr.src_off + (g_spr.cursor >> 2)), 0x60);
    g_spr.marker0 = (uint16_t)(g_spr.cursor >> 2);
    g_spr.cursor = (uint16_t)(g_spr.cursor + 4);
    vga_write((uint16_t)(g_spr.src_off + (g_spr.cursor >> 2)), 0x70);
    g_spr.marker1 = (uint16_t)(g_spr.cursor >> 2);
    g_spr.cursor = (uint16_t)(g_spr.cursor + 4);
    g_spr.page_off = (uint16_t)(g_spr.cursor >> 2);
    g_spr.cursor = 0;
}

static uint8_t stream_buf[SPR_SLOTS][SPR_STREAM_MAX];
uint8_t *sprite_stream_buf(int slot) { return stream_buf[slot]; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

int sprite_load_sheets(const uint8_t *entry, uint32_t size, const int16_t ids[SPR_SLOTS])
{
    for (int si = 0; si < SPR_SLOTS; si++) {
        if (ids[si] == -1) continue;
        uint32_t rec_at = (uint32_t)(uint16_t)(ids[si] * 220);
        if (rec_at + 220 > size) return -1;
        const uint8_t *rec = entry + rec_at;
        uint32_t off = rd16(rec) | ((uint32_t)rd16(rec + 2) << 16);
        uint16_t ss = rd16(rec + 0x38), ps = rd16(rec + 0x3A);
        if (ss > SPR_STREAM_MAX || off + ss + ps > size) return -1;
        for (int k = 0; k < 30; k++) g_spr.hdr[si][k] = rd16(rec + 2 * k);
        for (int k = 0; k < 80; k++) g_spr.tab[si * 110 + k] = rd16(rec + 0x3C + 2 * k);
        for (uint32_t k = 0; k < ss; k++) stream_buf[si][k] = entry[off + k];
        g_spr.stream[si] = stream_buf[si];

        video_set_draw_page(2);
        video_adjust_draw_offset(I16(g_spr.page_off));
        video_set_latch_mode(0);
        g_spr.base[si] = (uint16_t)(g_spr.cursor >> 2);
        uint16_t end = (uint16_t)(g_spr.cursor + ps);
        const uint8_t *pix = entry + off + ss;
        for (uint16_t di = g_spr.cursor; di < end; di++)
            video_put_pixel(di % 320, di / 320, pix[di - g_spr.cursor]);
        video_adjust_draw_offset(-I16(g_spr.page_off));
        g_spr.cursor = (uint16_t)(g_spr.cursor + ps);
    }
    return 0;
}

/* One implementation for the six blitters of 0BA5.  `clip`: 249E / 26B3 / 2896 track row and column
 * (with the sheet width `wrap`) and skip pixels outside the 80x160 view; 23BA / 25FF / 27E2 just
 * advance the destination.  `mode` 0 copies the sprite's own pixels (source = base + table +
 * page offset, advancing); 1 / 2 paint a solid cell instead (marker0 = DS:CE72, marker1 = DS:CE70:
 * every pixel is read from that one cell, source never advances and the page offset is not added). */
static void blit(int mode, int clip, int sheet, int wrap, int x, int y, int anim, int frame)
{
    int16_t a0 = I16(sheet), a1 = I16(wrap), a2 = I16(x), a3 = I16(y), a4 = I16(anim), a5 = I16(frame);
    if (g_rec_blit) g_rec_blit(mode, clip, a0, a1, a2, a3, a4, a5);
    uint16_t idx = (uint16_t)(a0 * 110 + a4 * 20 + a5);      /* word index into the frame table */
    if (a0 < 0 || a0 >= SPR_SHEETS || idx + 40u >= SPR_TAB_WORDS) return;  /* original: no check */
    const uint8_t *s = g_spr.stream[a0];
    uint16_t pos = g_spr.tab[idx];
    uint16_t src = mode == 0 ? (uint16_t)(g_spr.base[a0] + g_spr.tab[idx + 40])
                             : (mode == 1 ? g_spr.marker0 : g_spr.marker1);
    uint16_t src_addr_base = mode == 0 ? (uint16_t)(g_spr.src_off + g_spr.page_off) : g_spr.src_off;
    uint16_t dst = (uint16_t)(a3 * 80 + a2);
    int16_t row = 0, col = 0;

    video_set_latch_mode(1);
    for (;;) {
        uint8_t op = s[pos++];
        if (op == 0) {
            video_set_map_mask(s[pos]);
            dst = (uint16_t)(a3 * 80 + a2);
            pos++;
            row = 0; col = 0;
        } else if (op == 1) {
            int16_t w = I16(s[pos] | (s[pos + 1] << 8));
            pos += 2;
            dst = (uint16_t)(dst + w);
            if (clip) {
                col = I16(col + w);
                if (col > a1) { row = I16(row + col / 80); col = I16(col % 80); }
            }
        } else if (op == 2) {
            int draw = 1;
            if (clip) {
                int32_t cx = (int32_t)col + a2, cy = (int32_t)row + a3;
                draw = cx >= 0 && I16(cx) < 0x50 && cy >= 0 && I16(cy) < 0xA0;
            }
            if (draw) {
                uint8_t v = vga_read((uint16_t)(src_addr_base + src));
                vga_write((uint16_t)(video_draw_offset() + dst), v);
            }
            if (mode == 0) src++;
            dst++;
            if (clip) {
                col++;
                if (col > a1) { row = I16(row + col / 80); col = I16(col % 80); }
            }
        } else {
            return;
        }
    }
}

void sprite_draw(int sheet, int wrap, int x, int y, int anim, int frame) { blit(0, 1, sheet, wrap, x, y, anim, frame); }
void sprite_draw_raw(int sheet, int x, int y, int anim, int frame) { blit(0, 0, sheet, 0, x, y, anim, frame); }
void sprite_solid(int marker, int sheet, int wrap, int x, int y, int anim, int frame) { blit(marker ? 2 : 1, 1, sheet, wrap, x, y, anim, frame); }
void sprite_solid_raw(int marker, int sheet, int x, int y, int anim, int frame) { blit(marker ? 2 : 1, 0, sheet, 0, x, y, anim, frame); }
