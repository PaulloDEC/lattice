/*
 * video.h - 1:1 port of the original video module (code segment 05D8 + 1439).
 *
 * Geometry (verified from the disassembly): 4 pages of 0x3F00 bytes in VRAM,
 * each 320x200 = 0x3E80 bytes per plane at 80 bytes/row.  Horizontal coordinates in
 * the *rect* functions are in BYTES (4-pixel units); put/get pixel take real pixels.
 * Original function addresses are in the comments.
 */
#ifndef HOCUS_VIDEO_H
#define HOCUS_VIDEO_H
#include <stdint.h>

#define VIDEO_PAGES       4
#define VIDEO_PAGE_STRIDE 0x3F00
#define VIDEO_PAGE_BYTES  0x3E80
#define VIDEO_ROW_BYTES   80

/* called at each vertical-retrace point so the platform layer can present a frame */
typedef void (*video_vsync_fn)(void);
void video_set_vsync_callback(video_vsync_fn fn);

void video_init(void);                          /* 05D8:0185 (+1439:0000 mode set) */
void video_set_draw_page(int page);             /* 05D8:00E4 */
void video_show_page(int page);                 /* 05D8:0050 (waits for vsync) */
void video_show_page_now(int page);             /* 05D8:00A7 */
void video_wait_vsync(void);                    /* 05D8:0104 */
void video_set_read_plane(int p);               /* 05D8:0123 */
void video_screen(int on);                      /* 05D8:0135  0 = blank, else on */
void video_set_map_mask(int m);                 /* 05D8:0158 */
void video_set_latch_mode(int on);              /* 05D8:016A  on -> bit mask 0 */
void video_copy_page(int dst, int src);         /* 05D8:01F0 */
/* NOTE: the original copies every row to the SAME destination row (dy); see notes. */
void video_copy_rect(int src, int x0, int y0, int x1, int y1,
                     int dst, int dx, int dy);  /* 05D8:022A */
void video_clear_draw_page(void);               /* 05D8:0296 */
void video_fill_rect(int x0, int y0, int x1, int y1); /* 05D8:02BE  bytes, fills 0 */
int  video_get_pixel(int x, int y);             /* 05D8:0306 */
void video_put_pixel(int x, int y, int color);  /* 05D8:033B */
void video_clear_pixels(int x0, int y0, int x1, int y1); /* 05D8:0372 */
void video_set_palette(const uint8_t *rgb6, int first, int count); /* 05D8:039C */
void video_get_palette(uint8_t *rgb6, int first, int count);       /* 05D8:03DA */

/* ---- palette effects (05D8:0419-051A).  Target palette DS:6A64, working copy DS:6764,
 * both 768 bytes of 6-bit RGB.  Each step waits for vsync then uploads all 256 colours. */
void video_pal_fade_out(int steps);     /* 05D8:0419  target -> black, steps..0   */
void video_pal_fade_in(int steps);      /* 05D8:046E  black -> target, 0..steps   */
void video_pal_fade_white(void);        /* 05D8:04B0  70 steps, every component +1 up to 63; MODIFIES target */
void video_pal_set_black(void);         /* 05D8:04F3 */
void video_pal_apply(void);             /* 05D8:051A  upload the target palette */
uint8_t *video_target_palette(void);    /* DS:6A64 */
uint8_t *video_work_palette(void);      /* DS:6764 */

/* ---- PCX loading (05D8:0673 + 052F).  `entry` is the complete PCX file as stored in
 * HOCUS.DAT.  Loads the trailer palette (>>2) into the target palette, optionally uploads
 * it, then RLE-decodes the image into the CURRENT DRAW PAGE starting at row 0.
 * Leaves map mask = 8, bit mask = 0xFF (as the original does). */
void video_load_pcx(const uint8_t *entry, uint32_t size, int apply_palette);
int  video_pcx_width(void);             /* DS:796E */
int  video_pcx_height(void);            /* DS:796C */
int  video_pcx_bytes_per_line(void);    /* DS:796A */

/* ---- planar IMG blit (05D8:0A70).  `entry` = a whole IMG resource from HOCUS.DAT:
 * u16 width_in_4px_units, u16 height, then 4 planes of w4*h bytes.  Written straight into
 * the CURRENT DRAW PAGE at byte-column x, row y (no clipping, no transparency; offsets wrap
 * at 16 bits like the original).  Forces bit mask 0xFF; leaves map mask = 8. */
void video_load_img(const uint8_t *entry, uint32_t size, int x, int y);
int  video_img_width4(void);            /* DS:6328 */
int  video_img_height(void);            /* DS:632A */

/* ---- palette resource (05D8:0AFF).  Copies the whole entry (a 6-bit palette, normally
 * 384 bytes = 128 colours) into the target palette at colour `slot` (0 or 128), then
 * optionally uploads all 256 colours.  (The original does not bounds-check; this port
 * clamps to the 768-byte buffer.) */
void video_load_palette(const uint8_t *entry, uint32_t size, int slot, int apply);

int  video_draw_page(void);                     /* DS:6446 */
int  video_visible_page(void);                  /* DS:6444 */
uint16_t video_draw_offset(void);               /* DS:6460 (offset part) */
void video_adjust_draw_offset(int delta);       /* add to DS:6460 (the sprite loader does this) */
#endif
