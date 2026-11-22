/*
 * stars.h - the falling star field behind the end-of-level screen and the menus (original 06B8:0103 update, 06B8:039C init, tables built at
 * start-up by 0548:02F7).  75 stars; each has an angle (0..359 degrees), a radius, a speed (radius step per call), a brightness and a
 * brightness step.  Position = centre (160, 112) + trig[angle] * radius / 1,000,000, where trig = float table (DAT entry 5, cos / sin per
 * degree) * 1,000,000 truncated.  A star is drawn as a single pixel of colour 0xF0 + brightness / 8 on the draw page, only where the pixel
 * is 0 (black), and erased (set to 0) only if it is still one of the star colours (>= 0xF0).
 */
#ifndef HOCUS_STARS_H
#define HOCUS_STARS_H
#include <stdint.h>

#define STAR_COUNT 75
extern int32_t  g_trig[360][2];        /* DS:4CA8 */
extern uint16_t g_star_ang[STAR_COUNT];  /* DS:7B6C */
extern uint16_t g_star_rad[STAR_COUNT];  /* DS:7AD6 */
extern uint8_t  g_star_spd[STAR_COUNT];  /* DS:7A8A */
extern uint8_t  g_star_br[STAR_COUNT];   /* DS:7A3F */
extern uint8_t  g_star_spd2[STAR_COUNT]; /* DS:79F4 */

void stars_set_table(const uint8_t entry5[2880]);   /* 0548:02F7 (the 360 x 2 floats are DAT entry 5) */
void stars_init(void);                              /* 06B8:039C */
void stars_step(void);                              /* 06B8:0103  one frame: move all stars, then wait for the vertical retrace */
/* port, wide view: the starfields beyond the VGA page's 320 columns.  Both starfields (this one, used by the end
 * of level screen, and the front end's) put such points into g_star_side (wide x, 0 = nothing), which the platform
 * layer draws at the sides of full-screen displays.  This one uses the game's random numbers, so its stars stay
 * exactly the original's; a star leaving the picture sideways goes on as a cosmetic copy until the wide edge. */
#define STAR_SIDE_W 432
extern int g_star_wide_extra;                        /* extra columns each side (0 = 320 wide) */
extern uint8_t g_star_side[200][STAR_SIDE_W];
extern unsigned g_star_side_steps;                   /* counts starfield steps (the side stars are live while it moves) */
void stars_side_clear(void);
#endif
