#include "../src/video.h"
#include "../src/vga.h"
#include <stdio.h>
#include <string.h>
static int fails, checks;
#define CHECK(c) do{checks++; if(!(c)){fails++; printf("FAIL line %d: %s\n",__LINE__,#c);}}while(0)
static int vs;
static void vcb(void){ vs++; }

int main(void){
    static uint8_t idx[320*200]; static uint32_t rgba[320*200];
    video_set_vsync_callback(vcb);
    video_init();
    CHECK(video_draw_page()==0 && video_visible_page()==0 && vga_get_start()==0);

    /* put/get round trip on all four plane phases, plus where it lands in VRAM */
    for (int x=0;x<8;x++){ video_put_pixel(x,5,10+x); }
    for (int x=0;x<8;x++){ CHECK(video_get_pixel(x,5)==10+x); }
    CHECK(vga_plane(0)[5*80+0]==10 && vga_plane(1)[5*80+0]==11 && vga_plane(3)[5*80+1]==17);

    /* render maps pixel x to plane x&3, byte x>>2 */
    vga_render_indexed(idx);
    for (int x=0;x<8;x++) CHECK(idx[5*320+x]==10+x);

    /* palette: 6-bit -> 8-bit, and RGBA packing */
    uint8_t p[3]={63,0,32}; video_set_palette(p,10,1);
    vga_render_rgba(rgba);
    CHECK(rgba[5*320+0]==(0xFF000000u|255u|(0u<<8)|(130u<<16)));   /* R=255 G=0 B=(32<<2|32>>4)=130 */

    /* latch copy moves all four planes */
    video_set_draw_page(1); for (int x=0;x<8;x++) video_put_pixel(x,0,100+x);
    video_copy_page(2,1);
    video_set_draw_page(2); for (int x=0;x<8;x++) CHECK(video_get_pixel(x,0)==100+x);

    /* page flip: start address and callback count */
    vs=0; video_show_page(2);
    CHECK(vga_get_start()==2*VIDEO_PAGE_STRIDE && video_visible_page()==2 && vs==2);
    vga_render_indexed(idx); CHECK(idx[0]==100 && idx[7]==107);

    /* screen blanking */
    video_screen(0); vga_render_indexed(idx); CHECK(idx[0]==0);
    video_screen(1); vga_render_indexed(idx); CHECK(idx[0]==100);

    /* copy_rect keeps the original's quirk: all rows land on the SAME destination row */
    video_set_draw_page(0); video_clear_draw_page();
    video_set_draw_page(1); video_clear_draw_page();
    for (int y=0;y<3;y++) for (int x=0;x<4;x++) video_put_pixel(x,y,50+y);
    video_copy_rect(1,0,0,0,2,3,0,10);          /* src rows 0..2 -> dst row 10 only */
    video_set_draw_page(3);
    CHECK(video_get_pixel(0,10)==52);           /* last row wins */
    CHECK(video_get_pixel(0,11)==0 && video_get_pixel(0,12)==0);

    /* palette effects end states */
    video_init();
    uint8_t *tg = video_target_palette(); uint8_t q[3], all[768];
    for (int i=0;i<768;i++) tg[i]=(uint8_t)((i*5)%64);
    video_pal_fade_out(4);  video_get_palette(all,0,256);
    int allzero=1; for (int i=0;i<768;i++) if(all[i]) allzero=0;
    CHECK(allzero);
    video_pal_fade_in(4);   video_get_palette(all,0,256);
    CHECK(memcmp(all,tg,768)==0);
    vs=0; video_pal_set_black(); video_get_palette(all,0,256); CHECK(all[300]==0 && vs==0);
    video_pal_apply();      video_get_palette(q,100,1); CHECK(q[0]==tg[300]);
    vs=0; video_pal_fade_white(); video_get_palette(all,0,256);
    int allwhite=1; for (int i=0;i<768;i++) if(all[i]!=63) allwhite=0;
    CHECK(allwhite && vs==70);                       /* 70 steps, one vsync wait each */
    CHECK(tg[0]==63);                                /* fade_white modifies the target palette */

    /* IMG: planes are separate blocks; pixel (col c, row r, plane p) lands at x=(x0+c)*4+p */
    {
        uint8_t img[4 + 4 * 2 * 3]; img[0] = 3; img[1] = 0; img[2] = 2; img[3] = 0;   /* w4=3, h=2 */
        for (int pl = 0; pl < 4; pl++) for (int r = 0; r < 2; r++) for (int c = 0; c < 3; c++)
            img[4 + pl * 6 + r * 3 + c] = (uint8_t)(10 * pl + 3 * r + c + 1);
        video_init(); video_set_draw_page(1);
        video_set_latch_mode(1);                                  /* loader must reset this */
        video_load_img(img, sizeof img, 5, 7);
        CHECK(video_img_width4() == 3 && video_img_height() == 2);
        CHECK(vga_get_bit_mask() == 0xFF && vga_get_map_mask() == 8);
        int ok = 1;
        for (int pl = 0; pl < 4; pl++) for (int r = 0; r < 2; r++) for (int c = 0; c < 3; c++)
            if (video_get_pixel((5 + c) * 4 + pl, 7 + r) != 10 * pl + 3 * r + c + 1) ok = 0;
        CHECK(ok);
        CHECK(video_get_pixel(4 * 4, 7) == 0 && video_get_pixel(8 * 4, 7) == 0);   /* neighbours untouched */
    }
    /* palette resource into slot 128 */
    {
        uint8_t pe[384]; for (int i = 0; i < 384; i++) pe[i] = (uint8_t)(i % 64);
        video_init(); memset(video_target_palette(), 0, 768);
        video_load_palette(pe, sizeof pe, 128, 0);
        CHECK(memcmp(video_target_palette() + 384, pe, 384) == 0 && video_target_palette()[0] == 0);
        uint8_t q2[3]; video_get_palette(q2, 128, 1); CHECK(q2[0] == 0 && q2[1] == 0);   /* not applied yet */
        video_load_palette(pe, sizeof pe, 0, 1);
        video_get_palette(q2, 5, 1); CHECK(q2[0] == 15 && q2[1] == 16 && q2[2] == 17);     /* applied */
    }
    printf("%d checks, %d failures\n",checks,fails);
    return fails!=0;
}
