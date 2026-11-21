#include "../src/input.h"
#include <stdio.h>
#include <string.h>
static int fails, checks;
#define CHECK(c) do{checks++; if(!(c)){fails++; printf("FAIL line %d: %s\n",__LINE__,#c);}}while(0)
static int cheats[8], ncheat;
static void cb(uint16_t s){ if(ncheat<8) cheats[ncheat++]=s; }
static void fresh(void){ memset(&g_in,0,sizeof g_in); input_init_defaults(); input_reset(); ncheat=0; }

int main(void){
    input_set_cheat_callback(cb);

    /* level-triggered: Left arrow = 0x4B */
    fresh(); input_scancode(0x4B); CHECK(g_in.left==1 && g_in.right==0);
    input_scancode(0x4B); CHECK(g_in.left==1);
    input_scancode(0xCB); CHECK(g_in.left==0);

    /* edge-triggered Jump (Ctrl 0x1D): game consumes the flag, key repeat must NOT retrigger */
    fresh(); input_scancode(0x1D); CHECK(g_in.jump==1 && g_in.latch_jump==0);
    g_in.jump=0;                              /* game consumed it */
    input_scancode(0x1D);                     /* typematic repeat */
    CHECK(g_in.jump==0);
    input_scancode(0x9D);                     /* release re-arms */
    CHECK(g_in.jump==0 && g_in.latch_jump==1);
    input_scancode(0x1D); CHECK(g_in.jump==1);

    /* Fire (Alt 0x38) is independent of Jump */
    fresh(); input_scancode(0x38); CHECK(g_in.fire==1 && g_in.jump==0);

    /* Up sets BOTH flags with one latch */
    fresh(); input_scancode(0x48); CHECK(g_in.up==1 && g_in.up2==1 && g_in.latch_up==0);
    input_scancode(0xC8); CHECK(g_in.up==0 && g_in.up2==0 && g_in.latch_up==1);

    /* rebinding: put Jump on Space (index 5, 0x39) */
    fresh(); g_binding[ACT_JUMP]=5; input_scancode(0x1D); CHECK(g_in.jump==0);
    input_scancode(0x39); CHECK(g_in.jump==1);

    /* controller injection uses identical semantics */
    fresh(); input_action(ACT_JUMP,1); CHECK(g_in.jump==1);
    g_in.jump=0; input_action(ACT_JUMP,1); CHECK(g_in.jump==0);
    input_action(ACT_JUMP,0); CHECK(g_in.latch_jump==1);

    /* suppress_bindings (menus/text entry): action keys ignored, hotkeys still work */
    fresh(); g_in.suppress_bindings=1; input_scancode(0x4B); CHECK(g_in.left==0);
    input_scancode(0x19); CHECK(g_in.pause==1);
    CHECK(g_in.any_key==0);                       /* any_key is inside the guarded block */
    input_scancode(0x49); CHECK(g_in.scroll_up==1);  /* PgUp: scroll NOT suppressed */

    /* fixed keys */
    fresh(); input_scancode(0x01); CHECK(g_in.esc==1); input_scancode(0x81); CHECK(g_in.esc==0);
    input_scancode(0x3B); input_scancode(0x3C); input_scancode(0x3D); input_scancode(0x44);
    CHECK(g_in.f1&&g_in.f2&&g_in.f3&&g_in.f10);
    input_scancode(0x2E); input_scancode(0x32); input_scancode(0x1F); input_scancode(0x2F);
    CHECK(g_in.hot_c&&g_in.hot_m&&g_in.hot_s&&g_in.hot_v);
    input_reset(); CHECK(!g_in.f1 && !g_in.hot_c && g_in.latch_fire==1);

    /* any_key / last_scancode */
    fresh(); input_scancode(0xC8); CHECK(g_in.any_key==0 && g_in.last_scancode==0xC8);
    input_scancode(0x48); CHECK(g_in.any_key==1);

    /* scancode-sum detector: F(0x21) resets, sums include break codes */
    fresh(); input_scancode(0x21); CHECK(g_in.sum==0x21);
    input_scancode(0x10); CHECK(g_in.sum==0x10);  /* Q resets too */
    input_scancode(0x30); CHECK(g_in.sum==0x30);  /* B resets too */
    fresh(); g_in.sum = 0x5BA-0x10; input_scancode(0x10); /* reset happens first -> no match */
    CHECK(ncheat==0);
    fresh(); g_in.sum = 0x5BA-0x20; input_scancode(0x20); CHECK(ncheat==1 && cheats[0]==0x5BA && g_in.sum==0);
    fresh(); g_in.sum = 0x3D6-5; input_scancode(5); CHECK(ncheat==1 && cheats[0]==0x3D6);

    printf("%d checks, %d failures\n", checks, fails);
    return fails!=0;
}
