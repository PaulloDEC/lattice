#include "video.h"
#include "vga.h"
#include <string.h>

static uint16_t page_start[VIDEO_PAGES];  /* DS:6448 */
static uint16_t draw_off;                 /* DS:6460 */
static int draw_page;                     /* DS:6446 */
static int visible_page;                  /* DS:6444 */
static video_vsync_fn vsync_cb;

void video_set_vsync_callback(video_vsync_fn fn) { vsync_cb = fn; }
int  video_draw_page(void)       { return draw_page; }
int  video_visible_page(void)    { return visible_page; }
uint16_t video_draw_offset(void) { return draw_off; }
void video_adjust_draw_offset(int delta) { draw_off = (uint16_t)(draw_off + delta); }

void video_wait_vsync(void) { if (vsync_cb) vsync_cb(); }

void video_set_read_plane(int p) { vga_set_read_plane((uint8_t)p); }
void video_screen(int on)        { vga_set_screen_on(on); }
void video_set_map_mask(int m)   { vga_set_map_mask((uint8_t)m); }
void video_set_latch_mode(int on){ vga_set_bit_mask(on ? 0x00 : 0xFF); }

void video_set_draw_page(int page)
{
    draw_off  = (uint16_t)(page * VIDEO_PAGE_STRIDE);
    draw_page = page;
}
void video_show_page_now(int page)
{
    vga_set_start(page_start[page]);
    visible_page = page;
}
void video_show_page(int page)
{
    video_wait_vsync();                 /* wait for end of retrace */
    vga_set_start(page_start[page]);
    video_wait_vsync();                 /* wait for start of next retrace */
    visible_page = page;
}

void video_init(void)
{
    vga_reset();                        /* mode 13h, unchain, map mask F, VRAM cleared */
    for (int p = 0; p < VIDEO_PAGES; p++) {
        page_start[p] = (uint16_t)(p * VIDEO_PAGE_STRIDE);
        vga_memset(page_start[p], VIDEO_PAGE_BYTES, 0);
    }
    video_set_draw_page(0);
    video_show_page_now(0);
}

void video_copy_page(int dst, int src)
{
    video_set_latch_mode(1);
    video_set_map_mask(0x0F);
    vga_memcopy(page_start[dst], page_start[src], VIDEO_PAGE_BYTES);
}

void video_copy_rect(int src, int x0, int y0, int x1, int y1, int dst, int dx, int dy)
{
    int width = x1 - x0 + 1;
    video_set_latch_mode(1);
    video_set_map_mask(0x0F);
    for (int y = y0; y <= y1; y++)
        vga_memcopy((uint16_t)(page_start[dst] + dy * VIDEO_ROW_BYTES + dx),
                    (uint16_t)(page_start[src] + y * VIDEO_ROW_BYTES + x0),
                    (uint16_t)width);
}

void video_clear_draw_page(void)
{
    video_set_latch_mode(0);
    video_set_map_mask(0x0F);
    vga_memset(draw_off, VIDEO_PAGE_BYTES, 0);
}

void video_fill_rect(int x0, int y0, int x1, int y1)
{
    video_set_latch_mode(0);
    video_set_map_mask(0x0F);
    for (int y = y0; y <= y1; y++)
        vga_memset((uint16_t)(draw_off + y * VIDEO_ROW_BYTES + x0), (uint16_t)(x1 - x0 + 1), 0);
}

int video_get_pixel(int x, int y)
{
    video_set_read_plane(x % 4);
    return vga_read((uint16_t)(draw_off + y * VIDEO_ROW_BYTES + x / 4));
}

void video_put_pixel(int x, int y, int color)
{
    video_set_map_mask(1 << (x % 4));
    vga_write((uint16_t)(draw_off + y * VIDEO_ROW_BYTES + (x >> 2)), (uint8_t)color);
}

void video_clear_pixels(int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++)
            video_put_pixel(x, y, 0);
}

void video_set_palette(const uint8_t *rgb6, int first, int count)
{
    for (int i = 0; i < count; i++) vga_pal_set((first + i) & 255, rgb6 + 3 * i);
}
void video_get_palette(uint8_t *rgb6, int first, int count)
{
    for (int i = 0; i < count; i++) vga_pal_get((first + i) & 255, rgb6 + 3 * i);
}

/* ------------------------------------------------------------------------------------ */
static uint8_t pal_target[768];   /* DS:6A64 */
static uint8_t pal_work[768];     /* DS:6764 */
uint8_t *video_target_palette(void) { return pal_target; }
uint8_t *video_work_palette(void)   { return pal_work; }

/* the original does imul (16x16->32) then cwd + idiv: only the low 16 bits of the product
 * are used, sign-extended.  Identical for every realistic step count; kept for fidelity. */
static uint8_t scale(uint8_t v, int num, int den)
{
    int16_t prod = (int16_t)(v * num);
    return (uint8_t)(prod / (int16_t)den);
}

void video_pal_fade_out(int steps)
{
    memcpy(pal_work, pal_target, 768);
    for (int di = steps; di >= 0; di--) {
        for (int i = 0; i < 768; i++) pal_work[i] = scale(pal_target[i], di, steps);
        video_wait_vsync();
        video_set_palette(pal_work, 0, 256);
    }
}
void video_pal_fade_in(int steps)
{
    for (int di = 0; di <= steps; di++) {
        for (int i = 0; i < 768; i++) pal_work[i] = scale(pal_target[i], di, steps);
        video_wait_vsync();
        video_set_palette(pal_work, 0, 256);
    }
}
void video_pal_fade_white(void)
{
    for (int di = 0; di < 0x46; di++) {
        for (int i = 0; i < 768; i++) if (pal_target[i] < 0x3F) pal_target[i]++;
        video_wait_vsync();
        video_set_palette(pal_target, 0, 256);
    }
}
void video_pal_set_black(void)
{
    memset(pal_work, 0, 768);
    video_set_palette(pal_work, 0, 256);
}
void video_pal_apply(void) { video_set_palette(pal_target, 0, 256); }

/* ------------------------------------------------------------------------------------ */
static int pcx_w, pcx_h, pcx_bpl;
int video_pcx_width(void)          { return pcx_w; }
int video_pcx_height(void)         { return pcx_h; }
int video_pcx_bytes_per_line(void) { return pcx_bpl; }

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void video_load_pcx(const uint8_t *e, uint32_t size, int apply_palette)
{
    int xmin = le16(e + 4), ymin = le16(e + 6), xmax = le16(e + 8), ymax = le16(e + 10);
    for (int i = 0; i < 768; i++) pal_target[i] = (uint8_t)(e[size - 768 + i] >> 2);
    if (apply_palette) video_set_palette(pal_target, 0, 256);
    pcx_w   = (xmax - xmin + 1) & 0xFFFF;
    pcx_h   = (ymax - ymin + 1) & 0xFFFF;
    pcx_bpl = le16(e + 66);

    /* 05D8:052F - decode one scanline at a time into four 80-byte plane buffers.
     * (Original buffer is 326 bytes on the stack; valid data never overruns it.) */
    uint8_t buf[4 * 80 + 64];
    const uint8_t *p = e + 128;
    for (int row = 0; row < pcx_h; row++) {
        int di = 0, si = 0;
        do {
            int b = *p++;
            if ((b & 0xC0) == 0xC0) {
                int cnt = b & 0x3F;
                int val = *p++;
                while (cnt-- > 0) {
                    buf[si * 80 + (di >> 2)] = (uint8_t)val;
                    di++; if (++si > 3) si = 0;
                }
            } else {
                buf[si * 80 + (di >> 2)] = (uint8_t)b;
                di++; if (++si > 3) si = 0;
            }
        } while (di < pcx_bpl);
        video_set_latch_mode(0);
        for (int pl = 0; pl < 4; pl++) {
            video_set_map_mask(1 << pl);
            for (int k = 0; k < 80; k++)
                vga_write((uint16_t)(video_draw_offset() + row * 80 + k), buf[pl * 80 + k]);
        }
    }
}

/* ------------------------------------------------------------------------------------ */
static int img_w4, img_h;
int video_img_width4(void) { return img_w4; }
int video_img_height(void) { return img_h; }

void video_load_img(const uint8_t *e, uint32_t size, int x, int y)
{
    (void)size;
    img_w4 = le16(e);
    img_h  = le16(e + 2);
    const uint8_t *p = e + 4;
    video_set_latch_mode(0);
    for (int pl = 0; pl < 4; pl++) {
        video_set_map_mask(1 << pl);
        for (int di = 0; di < img_h; di++)
            for (int si = 0; si < img_w4; si++)
                vga_write((uint16_t)(video_draw_offset() + (uint16_t)((y + di) * 80) + x + si), *p++);
    }
}

void video_load_palette(const uint8_t *e, uint32_t size, int slot, int apply)
{
    uint32_t dst = (uint32_t)((slot * 3) & 0xFFFF);
    if (dst < 768) {
        uint32_t n = size;
        if (dst + n > 768) n = 768 - dst;
        memcpy(pal_target + dst, e, n);
    }
    if (apply) video_set_palette(pal_target, 0, 256);
}
