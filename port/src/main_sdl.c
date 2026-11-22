/*
 * main_sdl.c - SDL2 front end (Original mode): window, keyboard, timing, and a simple level driver until the
 * original menus (06B8 / 1A7C) are ported.  The game logic runs unchanged on the software VGA model (vga.c).
 *
 *   hocus [--dat HOCUS.DAT] [--episode 0..3] [--level 0..8] [--scale N] [--fullscreen]
 *         [--frames N --dump file.ppm]      (headless smoke test: run N frames, write the last frame, exit)
 *   Keys: arrows move, Ctrl jump, Alt fire (the game's own defaults), P pause, M / S music / sound, Esc menu.
 *   Game controller: see the controller section below (D-pad/stick move, A jump, X/B fire, Start menu).
 *   Smoothing (Enhanced display interpolation, interp.c): F11 or right stick click toggles; --smooth starts with it on.
 *   Run-ahead (default 1): F12 cycles 0 / 1 / 2; --runahead N.
 *   Views: F7 wide 16:9 on / off (--wide), F8 HUD on / off (--hud-off), left stick click cycles the four.
 *   Tests: --smooth-check (smooth picture at each tick == VGA page), --dump-alpha A (with --frames N --dump f.ppm).
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dat.h"
#include "rng.h"
#include "input.h"
#include "video.h"
#include "vga.h"
#include "timer.h"
#include "level.h"
#include "loader.h"
#include "loop.h"
#include "ui.h"
#include "frontend.h"
#include "stars.h"
#include "sound.h"
#include "music.h"
#include "enemy.h"
#include "frame.h"
#include "interp.h"

static SDL_Window *g_win; static SDL_Renderer *g_ren; static SDL_Texture *g_tex;
static int g_quit, g_dump_at = -1; static const char *g_dump_file; static double s_dump_alpha = -1;
static int s_menu_screen;                         /* a front-end screen is up (menus, help...): black sides in wide */
static uint32_t g_pix[432 * 200];               /* the presented picture, s_pix_w x 200 (320, or 426 wide) */
static int s_pix_w = 320;

/* SDL scancode -> DOS set-1 make code (0 = unmapped).  Extended keys use their plain code, as the game's key table does. */
static uint8_t dos_scancode(SDL_Scancode s)
{
    switch (s) {
    case SDL_SCANCODE_ESCAPE: return 0x01; case SDL_SCANCODE_RETURN: return 0x1C; case SDL_SCANCODE_SPACE: return 0x39;
    case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x1D; case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x38;
    case SDL_SCANCODE_LSHIFT: return 0x2A; case SDL_SCANCODE_RSHIFT: return 0x36; case SDL_SCANCODE_CAPSLOCK: return 0x3A;
    case SDL_SCANCODE_UP: return 0x48; case SDL_SCANCODE_DOWN: return 0x50; case SDL_SCANCODE_LEFT: return 0x4B; case SDL_SCANCODE_RIGHT: return 0x4D;
    case SDL_SCANCODE_HOME: return 0x47; case SDL_SCANCODE_END: return 0x4F; case SDL_SCANCODE_PAGEUP: return 0x49; case SDL_SCANCODE_PAGEDOWN: return 0x51;
    case SDL_SCANCODE_INSERT: return 0x52; case SDL_SCANCODE_DELETE: return 0x53; case SDL_SCANCODE_KP_5: return 0x4C;
    case SDL_SCANCODE_P: return 0x19; case SDL_SCANCODE_M: return 0x32; case SDL_SCANCODE_S: return 0x1F; case SDL_SCANCODE_C: return 0x2E; case SDL_SCANCODE_V: return 0x2F;
    case SDL_SCANCODE_F: return 0x21; case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_B: return 0x30;
    default: break;
    }
    if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F10) return (uint8_t)(0x3B + (s - SDL_SCANCODE_F1));
    return 0;
}

static void fatal(const char *msg)               /* stderr is invisible in a Windows GUI build, so also show a box */
{
    fprintf(stderr, "%s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Hocus Pocus", msg, NULL);
}

static uint32_t s_notice_until; static const char *s_notice;
static void notice(const char *t) { s_notice = t; s_notice_until = SDL_GetTicks() + 1200; }
static void draw_notice(uint32_t *pix, int w)    /* small white text with a shadow, top left; drawn over the output only */
{
    const uint8_t *f = ui_font();
    if (!s_notice || !f || SDL_TICKS_PASSED(SDL_GetTicks(), s_notice_until)) return;
    for (int pass = 0; pass < 2; pass++) {
        int x = 5 - pass, y = 5 - pass;
        uint32_t col = pass ? 0xFFFFFFFFu : 0xFF000000u;
        for (const char *s = s_notice; *s; s++) {
            int c = (unsigned char)*s;
            if (c == ' ') { x += 4; continue; }
            if (c < 0x21 || c > 0x7A) continue;
            int g = c - 0x21, m = 0;
            for (int r = 0; r < 8; r++) for (int i = 0; i < 8; i++) if (f[g * 8 + r] >> i & 1) { if (i > m) m = i; if (x + i >= 0 && x + i < w && y + r < 200) pix[(y + r) * w + x + i] = col; }
            x += m + 2;
        }
    }
}
static void hint_box(int i) { if (!interp_speculating()) ui_hint_box(i); }    /* never from a run-ahead tick */
static char s_notice_buf[48];
static void smooth_notice(void)
{
    if (!interp_enabled()) notice("SMOOTH: OFF");
    else { snprintf(s_notice_buf, sizeof s_notice_buf, "SMOOTH: DISPLAY  RUN-AHEAD %d", interp_runahead()); notice(s_notice_buf); }
}
static char s_view_buf[48];
static void view_notice(void)
{
    snprintf(s_view_buf, sizeof s_view_buf, "VIEW: %s  HUD: %s", interp_wide() ? "WIDE 16:9" : "CLASSIC", interp_hud_off() ? "OFF" : "CLASSIC");
    notice(s_view_buf);
}
static void toggle_wide(void) { interp_set_view(!interp_wide(), interp_hud_off()); view_notice(); }      /* F7 */
static void toggle_hud(void)  { interp_set_view(interp_wide(), !interp_hud_off()); view_notice(); }      /* F8 */
static void cycle_view(void)                      /* left stick click: classic -> wide -> wide, HUD off -> HUD off -> classic */
{
    int w = interp_wide(), h = interp_hud_off();
    if (!w && !h) w = 1; else if (w && !h) h = 1; else if (w && h) w = 0; else h = 0;
    interp_set_view(w, h); view_notice();
}
static void cycle_runahead(void)                  /* F12: 0 -> 1 -> 2 -> 0 (turns smoothing on) */
{
    if (!interp_enabled()) interp_set_enabled(1);
    else interp_set_runahead((interp_runahead() + 1) % 3);
    smooth_notice();
}
static void toggle_smooth(void)
{
    interp_set_enabled(!interp_enabled());
    smooth_notice();
}

static int compose(void)                          /* the picture to show into g_pix; returns its width */
{
    int w = 320;
    if (interp_render(g_pix, &w) || interp_compose_vga(g_pix, &w, !s_menu_screen)) return w;
    vga_render_rgba(g_pix);
    return 320;
}
static int s_tex_w;
static void set_tex_width(int w)                  /* texture w x 200 shown at w x 240 (200 rows stretched, as on a CRT) */
{
    if (w == s_tex_w && g_tex) return;
    if (g_tex) SDL_DestroyTexture(g_tex);
    g_tex = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, w, 200);
    SDL_RenderSetLogicalSize(g_ren, w, 240);
    if (s_tex_w && !(SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP)) {    /* windowed: keep the height */
        int ww, wh; SDL_GetWindowSize(g_win, &ww, &wh);
        SDL_SetWindowSize(g_win, wh * w / 240, wh);
    }
    s_tex_w = w;
}
static void present(void)
{
    if (!g_ren) return;
    int w = s_pix_w = compose();
    set_tex_width(w);
    draw_notice(g_pix, w);
    SDL_UpdateTexture(g_tex, NULL, g_pix, w * 4);
    SDL_RenderClear(g_ren);
    SDL_RenderCopy(g_ren, g_tex, NULL, NULL);
    SDL_RenderPresent(g_ren);
}

static int s_keys;                              /* pending key presses (the DOS keyboard buffer) */
static int s_kq[32], s_kqn;                     /* menu key queue (ASCII, FK_*) */
static void kq_push(int k) { if (s_kqn < 32) s_kq[s_kqn++] = k; }
static int menu_key(void) { if (!s_kqn) return 0; int k = s_kq[0]; memmove(s_kq, s_kq + 1, (--s_kqn) * sizeof(int)); return k; }
static int s_tq[64], s_tqn;                      /* typed characters (name entry) */
static void tq_push(int c) { if (s_tqn < 64) s_tq[s_tqn++] = c; }
static int text_key(void) { if (!s_tqn) return 0; int c = s_tq[0]; memmove(s_tq, s_tq + 1, (--s_tqn) * sizeof(int)); return c; }
static void menu_flush(void) { s_kqn = 0; s_tqn = 0; }
static void pad_open_first(void); static void pad_release_all(void); static void pad_button(int b, int down); static void pad_axis(int a, int v);
static SDL_GameController *s_pad;
static void pad_script_step(void);
static void toggle_smooth(void); static void cycle_runahead(void); static void toggle_wide(void); static void toggle_hud(void); static void cycle_view(void);

/* Tap latching (with smoothing on): a key pressed and released between two ticks would never be seen by the
 * game (the original reads key states once per tick).  Its release is held back until the next tick has run. */
static uint8_t s_tap_down[128], s_tap_held[128];
static void tap_flush(void)                       /* a tick has read the keys (or no level is waiting) */
{
    for (int k = 0; k < 128; k++) {
        s_tap_down[k] = 0;
        if (s_tap_held[k]) { s_tap_held[k] = 0; input_scancode((uint8_t)(k | 0x80)); }
    }
}
static void game_key(uint8_t sc)
{
    uint8_t k = sc & 0x7F;
    if (interp_enabled() && interp_ready()) {
        if (!(sc & 0x80)) { s_tap_down[k] = 1; s_tap_held[k] = 0; }
        else if (s_tap_down[k]) { s_tap_held[k] = 1; return; }
    }
    input_scancode(sc);
    interp_input_changed();
}
static void pump(void)
{
    if (!interp_ready()) tap_flush();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) g_quit = 1;
        else if (e.type == SDL_CONTROLLERDEVICEADDED) pad_open_first();
        else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
            if (s_pad && e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad))) { pad_release_all(); SDL_GameControllerClose(s_pad); s_pad = NULL; pad_open_first(); }
        }
        else if ((e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) && s_pad && e.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad)))
            pad_button(e.cbutton.button, e.type == SDL_CONTROLLERBUTTONDOWN);
        else if (e.type == SDL_CONTROLLERAXISMOTION && s_pad && e.caxis.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(s_pad)))
            pad_axis(e.caxis.axis, e.caxis.value);
        else if (e.type == SDL_TEXTINPUT) { for (const char *t = e.text.text; *t; t++) if ((unsigned char)*t >= 0x20 && (unsigned char)*t < 0x7F) tq_push((unsigned char)*t); }
        else if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && !e.key.repeat) {
            uint8_t sc = dos_scancode(e.key.keysym.scancode);
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_RETURN && (e.key.keysym.mod & KMOD_ALT)) {
                Uint32 f = SDL_GetWindowFlags(g_win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(g_win, f ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP); continue;
            }
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F11) { toggle_smooth(); continue; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F12) { cycle_runahead(); continue; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F7) { toggle_wide(); continue; }
            if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_F8) { toggle_hud(); continue; }
            if ((e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) && (e.key.keysym.scancode == SDL_SCANCODE_F7 || e.key.keysym.scancode == SDL_SCANCODE_F8 ||
                e.key.keysym.scancode == SDL_SCANCODE_F11 || e.key.keysym.scancode == SDL_SCANCODE_F12)) continue;    /* port keys: never seen by the game */
            if (e.type == SDL_KEYDOWN && sc && s_keys < 16) s_keys++;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_UP) kq_push(FK_UP); else if (k == SDLK_DOWN) kq_push(FK_DOWN);
                else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) kq_push(FK_ENTER); else if (k == SDLK_ESCAPE) kq_push(FK_ESC);
                else if (k == SDLK_HOME) kq_push(FK_HOME); else if (k == SDLK_END) kq_push(FK_END); else if (k == SDLK_PAGEUP) kq_push(FK_PGUP);
                else if (k == SDLK_PAGEDOWN) kq_push(FK_PGDN); else if (k == SDLK_LEFT) kq_push(FK_LEFT); else if (k == SDLK_RIGHT) kq_push(FK_RIGHT);
                else if (k == SDLK_SPACE) kq_push(' '); else if (k == SDLK_BACKSPACE) kq_push(FK_BKSP);
                else if (k >= 'a' && k <= 'z') kq_push(k - 32); else if (k >= '0' && k <= '9') kq_push(k);
            }
            if (sc) game_key(e.type == SDL_KEYDOWN ? sc : (uint8_t)(sc | 0x80));
        }
    }
}

/* ---- game controller (SDL GameController: Xbox-style layout, so the Steam Deck and most pads work) ----
 * Each pad input drives a virtual button; a virtual button behaves like pressing a key: it sends the DOS
 * scancode of the key currently bound to its action (so "Define keys" still applies), counts as a key press
 * for "press any key" screens, and queues a menu key.  Several pad inputs can drive one virtual button
 * (D-pad and stick, A and LT): the button is down while any of them is held.
 *   D-pad / left stick   move, up = doors/up, down = down       menus: navigate, left/right = sliders
 *   A, LT                jump                                   menus: select
 *   X, B, RT             fire                                   menus: B = back
 *   LB / RB, right stick look up / down (the scroll keys)       text pages: page up / down
 *   Start                Esc: open / close the in-game menu     Back/View  pause */
enum { VB_LEFT, VB_RIGHT, VB_UP, VB_DOWN, VB_JUMP, VB_FIRE, VB_LOOKUP, VB_LOOKDN, VB_BACK, VB_START, VB_PAUSE, VB_COUNT };
static uint32_t s_vb_src[VB_COUNT];               /* bit per pad input holding the virtual button */
static int s_axis[SDL_CONTROLLER_AXIS_MAX];       /* -1 / 0 / +1 per axis after the dead zone */

static void vb_emit(int vb, int down)
{
    static const int act[VB_COUNT] = { ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN, ACT_JUMP, ACT_FIRE, ACT_SCROLL_UP, ACT_SCROLL_DOWN, ACT_FIRE, -1, -1 };
    static const int mkey[VB_COUNT] = { FK_LEFT, FK_RIGHT, FK_UP, FK_DOWN, FK_ENTER, 0, FK_PGUP, FK_PGDN, FK_ESC, FK_ESC, 0 };
    uint8_t sc = act[vb] >= 0 ? g_key_scancode[g_binding[act[vb]]] : vb == VB_START ? 0x01 : 0x19;
    if (down) { if (s_keys < 16) s_keys++; if (mkey[vb]) kq_push(mkey[vb]); }
    game_key(down ? sc : (uint8_t)(sc | 0x80));
}
static void vb_set(int vb, int src, int down)
{
    uint32_t was = s_vb_src[vb];
    if (down) s_vb_src[vb] |= 1u << src; else s_vb_src[vb] &= ~(1u << src);
    if (!was != !s_vb_src[vb]) vb_emit(vb, s_vb_src[vb] != 0);
}
static void pad_release_all(void)
{
    for (int vb = 0; vb < VB_COUNT; vb++) if (s_vb_src[vb]) { s_vb_src[vb] = 0; vb_emit(vb, 0); }
    memset(s_axis, 0, sizeof s_axis);
}
static void pad_open_first(void)
{
    if (s_pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (s_pad = SDL_GameControllerOpen(i)) != NULL) return;
}
static void pad_button(int b, int down)
{
    switch (b) {                                  /* source ids: buttons 0..20, axes 24.. */
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:  vb_set(VB_LEFT, b, down); break;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: vb_set(VB_RIGHT, b, down); break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:    vb_set(VB_UP, b, down); break;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:  vb_set(VB_DOWN, b, down); break;
    case SDL_CONTROLLER_BUTTON_A:          vb_set(VB_JUMP, b, down); break;
    case SDL_CONTROLLER_BUTTON_X:          vb_set(VB_FIRE, b, down); break;
    case SDL_CONTROLLER_BUTTON_B:          vb_set(VB_BACK, b, down); break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:  vb_set(VB_LOOKUP, b, down); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: vb_set(VB_LOOKDN, b, down); break;
    case SDL_CONTROLLER_BUTTON_START:      vb_set(VB_START, b, down); break;
    case SDL_CONTROLLER_BUTTON_BACK:       vb_set(VB_PAUSE, b, down); break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: if (down) toggle_smooth(); break;    /* R3: smoothing on / off */
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:  if (down) cycle_view(); break;       /* L3: classic / wide / wide + HUD off / HUD off */
    default: break;
    }
}
static void pad_axis(int a, int v)
{
    int st;                                       /* dead zone with hysteresis: engage past 50%, release under 35% */
    int prev = s_axis[a];
    if (a == SDL_CONTROLLER_AXIS_TRIGGERLEFT || a == SDL_CONTROLLER_AXIS_TRIGGERRIGHT)
        st = v > (prev ? 8000 : 16000);
    else
        st = v < (prev < 0 ? -11500 : -16400) ? -1 : v > (prev > 0 ? 11500 : 16400) ? 1 : 0;
    if (st == prev) return;
    s_axis[a] = st;
    int src = 24 + a;
    switch (a) {
    case SDL_CONTROLLER_AXIS_LEFTX:  vb_set(VB_LEFT, src, st < 0); vb_set(VB_RIGHT, src, st > 0); break;
    case SDL_CONTROLLER_AXIS_LEFTY:  vb_set(VB_UP, src, st < 0);   vb_set(VB_DOWN, src, st > 0); break;
    case SDL_CONTROLLER_AXIS_RIGHTY: vb_set(VB_LOOKUP, src, st < 0); vb_set(VB_LOOKDN, src, st > 0); break;
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:  vb_set(VB_JUMP, src, st); break;
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: vb_set(VB_FIRE, src, st); break;
    default: break;
    }
}

/* --pad STR (test): drive an SDL virtual game controller, one step per 30 menu/frame ticks.  U D L R = D-pad,
 * A B X Y, S = Start, V = Back, l r = shoulders; each press is released on the next step; t = tap A (down and up
 * before the next tick).  '>' / '<' toggle
 * holding the left stick right / left, 'j' toggles holding the right trigger; '.' waits one step. */
static const char *s_pad_script; static int s_pad_vj = -1, s_pad_tick, s_pad_release = -1;
static void pad_script_step(void)
{
#if SDL_VERSION_ATLEAST(2, 0, 14)
    static int hold_x, hold_t;
    if (!s_pad_script) return;
    if (s_pad_vj < 0) { s_pad_vj = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, SDL_CONTROLLER_AXIS_MAX, SDL_CONTROLLER_BUTTON_MAX, 0); SDL_JoystickOpen(s_pad_vj); return; }
    if (++s_pad_tick % 30) return;
    SDL_Joystick *j = SDL_JoystickFromPlayerIndex(0); if (!j) j = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(s_pad_vj));
    if (!j) return;
    if (s_pad_release >= 0) { SDL_JoystickSetVirtualButton(j, s_pad_release, 0); s_pad_release = -1; return; }
    if (!*s_pad_script) return;
    char c = *s_pad_script++; int b = -1;
    switch (c) {
    case 'U': b = SDL_CONTROLLER_BUTTON_DPAD_UP; break; case 'D': b = SDL_CONTROLLER_BUTTON_DPAD_DOWN; break;
    case 'L': b = SDL_CONTROLLER_BUTTON_DPAD_LEFT; break; case 'R': b = SDL_CONTROLLER_BUTTON_DPAD_RIGHT; break;
    case 'A': b = SDL_CONTROLLER_BUTTON_A; break; case 'B': b = SDL_CONTROLLER_BUTTON_B; break;
    case 'X': b = SDL_CONTROLLER_BUTTON_X; break; case 'Y': b = SDL_CONTROLLER_BUTTON_Y; break;
    case 'S': b = SDL_CONTROLLER_BUTTON_START; break; case 'V': b = SDL_CONTROLLER_BUTTON_BACK; break;
    case 'l': b = SDL_CONTROLLER_BUTTON_LEFTSHOULDER; break; case 'r': b = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER; break;
    case '>': hold_x = hold_x > 0 ? 0 : 1; SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, hold_x * 30000); break;
    case '<': hold_x = hold_x < 0 ? 0 : -1; SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_LEFTX, hold_x * 30000); break;
    case 'j': hold_t = !hold_t; SDL_JoystickSetVirtualAxis(j, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, hold_t ? 32767 : 0); break;
    case 't': SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 1); SDL_JoystickUpdate(); SDL_JoystickSetVirtualButton(j, SDL_CONTROLLER_BUTTON_A, 0); break;   /* tap A within one tick */
    default: break;
    }
    if (b >= 0) { SDL_JoystickSetVirtualButton(j, b, 1); s_pad_release = b; }
#endif
}

static uint16_t s_last_start = 0xFFFF;
static void dump_pending(void);
static void idle(void)                        /* called while the frame timer waits */
{
    pump();
    dump_pending();
    if (interp_enabled() && interp_ready()) {     /* smoothing: a new picture every display refresh (vsync paces it) */
        static Uint64 last; Uint64 now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
        if (last && (now - last) * 1000 < f * 4) { SDL_Delay(1); return; }    /* no vsync: at most ~250 per second */
        last = now; present(); s_last_start = vga_get_start();
        return;
    }
    uint16_t st = vga_get_start();
    if (st != s_last_start) { s_last_start = st; present(); }
    SDL_Delay(1);
}
static void vsync(void) { pump(); present(); SDL_Delay(14); }   /* palette fades and "wait for retrace" screens */

/* ---- hooks ---- */
static int s_frames_left = -1;
static void dump_frame(void)
{
    FILE *f = fopen(g_dump_file, "wb");
    int w = compose();                            /* what would be shown (the VGA page in the Original setup) */
    if (f) { fprintf(f, "P6\n%d 200\n255\n", w); for (int i = 0; i < w * 200; i++) { fputc(g_pix[i] & 255, f); fputc((g_pix[i] >> 8) & 255, f); fputc((g_pix[i] >> 16) & 255, f); } fclose(f); }
}
static int g_esc_at = -1, g_fkey;
static int frame_hook(int n)
{
    pad_script_step();
    menu_flush();                                 /* menu keys pressed during play (A = Enter, B = Esc...) must not leak into the next menu */
    if (g_dump_at >= 0 && n == g_dump_at && s_dump_alpha < 0) { dump_frame(); g_quit = 1; }
    if (g_esc_at >= 0 && n == g_esc_at) { if (g_fkey == 10) g_in.f10 = 1; else if (g_fkey == 1) g_in.f1 = 1; else if (g_fkey == 22) g_in.hot_v = 1; else if (g_fkey == 2) g_in.f2 = 1; else g_in.esc = 1; }          /* --esc-at N (test): press Esc at frame N */
    return g_quit;
}
/* ---- UI hooks (modal boxes, end screen) ---- */
static int ui_poll(void) { pump(); present(); SDL_Delay(15); return g_quit; }
static int s_end_polls;
static int ui_kbhit(void)
{
    pump();
    if (g_dump_at >= 0 && ++s_end_polls == g_dump_at + 1) { dump_frame(); g_quit = 1; s_keys++; }   /* --frames N --dump: snapshot of the end screen */
    return s_keys > 0;
}
static void pal_entry(int entry, int slot) { uint32_t n; uint8_t *p = dat_load(entry, &n); if (p) { video_load_palette(p, n, slot, 0); free(p); } }
static SDL_AudioDeviceID g_audio;
static void play_sfx(int id) { if (interp_speculating()) return; SDL_LockAudioDevice(g_audio); sound_play(id); SDL_UnlockAudioDevice(g_audio); }   /* the mixer runs on the audio thread */
static void audio_cb(void *ud, Uint8 *stream, int len) { (void)ud; memset(stream, 0, len); sound_mix((int16_t *)stream, len / 2); music_render((int16_t *)stream, len / 2); }
static void play_music(int entry, int loop)       /* 136b:01FF: stop the current song, load the DAT entry, play it (only while music is on) */
{
    uint32_t n; uint8_t *p = g_music_on ? dat_load(entry, &n) : NULL;
    SDL_LockAudioDevice(g_audio); if (p) music_start(p, n, loop); SDL_UnlockAudioDevice(g_audio); free(p);
}
static void play_voc(int entry) { SDL_LockAudioDevice(g_audio); sound_play_entry(entry); SDL_UnlockAudioDevice(g_audio); }
static void apply_volume(int sfx, int music) { SDL_LockAudioDevice(g_audio); sound_set_volume(sfx); music_set_volume(music); SDL_UnlockAudioDevice(g_audio); }
static void stop_music(void) { SDL_LockAudioDevice(g_audio); music_stop(); SDL_UnlockAudioDevice(g_audio); }
static void ui_getch(void) { if (s_keys > 0) s_keys--; }
static void ui_img(int entry, int x, int y) { uint32_t n; uint8_t *p = dat_load(entry, &n); if (p) { video_load_img(p, n, x, y); free(p); } }
static int quit_menu(void) { int r = frontend_game_menu(); menu_flush(); input_reset(); return r; }
static const char *s_script; static int s_pace;
static void f_help(int a) { (void)a; frontend_overlay_help(); menu_flush(); input_reset(); }
static void f_save(void) { frontend_overlay_save(); menu_flush(); input_reset(); }
static int  f_load(void) { int r = frontend_overlay_restore(); menu_flush(); input_reset(); return r; }
static int  f_quit(void) { int r = frontend_overlay_quit(); menu_flush(); input_reset(); return r; }
static void f_vol(void) { frontend_overlay_volume(); menu_flush(); input_reset(); }
static void front_pace(void)        /* --keys "DDE.." scripted menu keys (U D E=enter X=esc, letters), one every 30 iterations; --frames N --dump: dump after N */
{
    s_menu_screen = 1;                                /* until the next level tick */
    pump(); present(); SDL_Delay(g_dump_at >= 0 || s_script ? 1 : 16);
    ++s_pace; pad_script_step();
    if (s_script && s_pace % 30 == 0 && *s_script) { char c = *s_script++; if (c >= 'a' && c <= 'z') tq_push(c); else if (c == '<') kq_push(FK_BKSP); else if (c == '+') kq_push(FK_RIGHT); else if (c == '-') kq_push(FK_LEFT); else kq_push(c == 'U' ? FK_UP : c == 'D' ? FK_DOWN : c == 'E' ? FK_ENTER : c == 'X' ? FK_ESC : c); }
    if (g_dump_at >= 0 && s_pace == g_dump_at) { dump_frame(); g_quit = 1; }
}
static int front_quit(void) { pump(); return g_quit; }
static void pause_game(void)                 /* P: wait for the next P */
{
    g_in.pause = 0;
    while (!g_quit) { pump(); present(); SDL_Delay(30); if (g_in.pause) { g_in.pause = 0; break; } }
}
static void wait_key(void)                   /* the "press a key" screen before a level */
{
    if (g_dump_at >= 0) return;
    g_in.any_key = 0;
    while (!g_quit && !g_in.any_key) { pump(); present(); SDL_Delay(15); }
    g_in.any_key = 0; input_reset();
}
static int end_screen(int finished, int goal, int collected) { return ui_end_screen(finished, goal, collected); }

/* --smooth-check: after every shown tick, the smooth picture at alpha 1 must equal the VGA page (play area).
 * --dump-alpha A with --frames N --dump f.ppm: write the smooth picture at alpha A after the Nth shown tick. */
static int s_check, s_checked, s_check_bad, s_shown, s_dump_pending; static FILE *s_hash_f;   /* --frame-hash FILE: one line per tick */
static void (*s_tick_shown_inner)(void);
static void dump_indexed(const uint8_t *idx)
{
    FILE *f = fopen(g_dump_file, "wb"); if (!f) return;
    vga_indexed_to_rgba(idx, g_pix);
    fprintf(f, "P6\n320 200\n255\n");
    for (int i = 0; i < 320 * 200; i++) { fputc(g_pix[i] & 255, f); fputc((g_pix[i] >> 8) & 255, f); fputc((g_pix[i] >> 16) & 255, f); }
    fclose(f);
}
static void dump_pending(void)                   /* --dump-alpha: the smooth picture once this wait's input is in */
{
    if (!s_dump_pending) return;
    s_dump_pending = 0;
    int w = 320;
    if (interp_render_at(g_pix, &w, s_dump_alpha)) {
        FILE *f = fopen(g_dump_file, "wb");
        if (f) { fprintf(f, "P6\n%d 200\n255\n", w); for (int i = 0; i < w * 200; i++) { fputc(g_pix[i] & 255, f); fputc((g_pix[i] >> 8) & 255, f); fputc((g_pix[i] >> 16) & 255, f); } fclose(f); }
    } else dump_frame();
    g_quit = 1;
}
static void tick_shown_main(void)
{
    tap_flush();                                     /* the tick that just ran has seen every tap; also before run-ahead */
    s_menu_screen = 0;
    s_tick_shown_inner();
    ++s_shown;
    static uint8_t a[320 * 200], b[320 * 200];
    if (s_hash_f) {                                  /* FNV-1a of the shown page and the palette */
        vga_render_indexed(b);
        uint32_t h = 2166136261u;
        for (int i = 0; i < 320 * 200; i++) h = (h ^ b[i]) * 16777619u;
        for (int i = 0; i < 256; i++) for (int c = 0; c < 3; c++) h = (h ^ vga_palette(i, c)) * 16777619u;
        fprintf(s_hash_f, "%d %08x\n", s_shown, h);
    }
    if (s_check && interp_render_latest(a)) {
        vga_render_indexed(b); s_checked++;
        int bad = 0; for (int i = 0; i < 320 * 160; i++) if (a[i] != b[i]) bad++;
        if (bad) {
            if (s_check_bad++ < 5) fprintf(stderr, "smooth-check: tick %d differs in %d pixels\n", s_shown, bad);
            if (s_check_bad == 1) {                      /* first difference: smooth_bad.ppm / vga_bad.ppm */
                const char *keep = g_dump_file;
                g_dump_file = "smooth_bad.ppm"; dump_indexed(a);
                g_dump_file = "vga_bad.ppm"; dump_indexed(b);
                g_dump_file = keep;
            }
        }
    }
    if (s_dump_alpha >= 0 && g_dump_file && s_shown == g_dump_at) s_dump_pending = 1;    /* in idle(), after input */
}

int main(int argc, char **argv)
{
    int smooth = 0, runahead = 1, wide = 0, hudoff = 0;
    const char *dat = "HOCUS.DAT"; int episode = 0, level = 0, scale = 3, full = 0, frames = -1, end_test = -1, direct = 0; const char *test_screen = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--dat") && i + 1 < argc) dat = argv[++i];
        else if (!strcmp(argv[i], "--episode") && i + 1 < argc) { episode = atoi(argv[++i]); direct = 1; }
        else if (!strcmp(argv[i], "--test-screen") && i + 1 < argc) test_screen = argv[++i];
        else if (!strcmp(argv[i], "--fkey") && i + 1 < argc) g_fkey = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--esc-at") && i + 1 < argc) g_esc_at = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--skip-intro")) g_skip_intro = 1;
        else if (!strcmp(argv[i], "--keys") && i + 1 < argc) s_script = argv[++i];
        else if (!strcmp(argv[i], "--pad") && i + 1 < argc) s_pad_script = argv[++i];
        else if (!strcmp(argv[i], "--smooth")) smooth = 1;
        else if (!strcmp(argv[i], "--wide")) wide = 1;
        else if (!strcmp(argv[i], "--hud-off")) hudoff = 1;
        else if (!strcmp(argv[i], "--runahead") && i + 1 < argc) runahead = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--smooth-check")) s_check = 1;
        else if (!strcmp(argv[i], "--frame-hash") && i + 1 < argc) s_hash_f = fopen(argv[++i], "w");
        else if (!strcmp(argv[i], "--dump-alpha") && i + 1 < argc) s_dump_alpha = atof(argv[++i]);
        else if (!strcmp(argv[i], "--level") && i + 1 < argc) { level = atoi(argv[++i]); direct = 1; }
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fullscreen")) full = 1;
        else if (!strcmp(argv[i], "--end") && i + 1 < argc) end_test = atoi(argv[++i]);   /* show the end screen once (0 = died, 1 = finished) */
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) g_dump_file = argv[++i];
        else { fprintf(stderr, "usage: %s [--dat HOCUS.DAT] [--episode 0..3] [--level 0..8] [--scale N] [--fullscreen] [--smooth] [--runahead 0..2] [--wide] [--hud-off] [--frames N --dump out.ppm]\n", argv[0]); return 2; }
    }
    if (frames >= 0 && g_dump_file) g_dump_at = frames;
    if (dat_open(dat)) { char m[512]; snprintf(m, sizeof m, "Cannot open %s.\nCopy HOCUS.DAT from the registered Hocus Pocus v1.1 next to the program, or use --dat.", dat); fatal(m); return 1; }
    { const char *err = dat_check(); if (err) { fatal(err); return 1; } }
    uint32_t n6; uint8_t *r6 = dat_load(6, &n6);
    if (!r6 || rng_load(r6, n6)) { fatal("Bad HOCUS.DAT (random table)."); return 1; }
    free(r6);
    { uint32_t n; uint8_t *f = dat_load(0, &n); if (!f || n < 720) { fatal("Bad HOCUS.DAT (font)."); return 1; } ui_set_font(f);   /* kept for the whole run */
      uint8_t *t = dat_load(5, &n); if (!t || n < 2880) { fatal("Bad HOCUS.DAT (star table)."); return 1; } stars_set_table(t); free(t); }

    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");   /* A = bottom face button on every pad (Nintendo layouts too) */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) < 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    g_win = SDL_CreateWindow("Hocus Pocus", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, (wide ? 426 : 320) * scale, 240 * scale,
                             SDL_WINDOW_RESIZABLE | (full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    g_ren = g_win ? SDL_CreateRenderer(g_win, -1, SDL_RENDERER_PRESENTVSYNC) : NULL;
    if (!g_ren && g_win) g_ren = SDL_CreateRenderer(g_win, -1, 0);
    if (!g_ren) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    s_tex_w = 0; set_tex_width(wide ? 426 : 320);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");

    { SDL_AudioSpec want, have; SDL_zero(want); want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audio_cb;
      SDL_AudioDeviceID dev = g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);     /* no audio device (CI, headless): the game runs silent */
      sound_init(dev ? have.freq : 44100); music_init(dev ? have.freq : 44100);
      if (dev) { SDL_PauseAudioDevice(dev, 0); g_sound_on = 1; g_sound_hw = 1; game_set_sound_callback(play_sfx); g_loop_hooks.music = play_music; g_loop_hooks.music_off = stop_music; g_level_hooks.music = play_music; g_music_on = 1; } }
    pad_open_first();                             /* later pads are picked up by SDL_CONTROLLERDEVICEADDED */
    input_init_defaults();
    { int hw = g_sound_hw; g_front.apply_volume = apply_volume; settings_load("HOCUS.SAV"); if (!hw) { g_sound_on = 0; g_music_on = 0; } }
    input_init_defaults(); input_reset();
    vga_reset(); video_init(); video_set_vsync_callback(vsync);
    timer_init(NULL, idle);
    loop_install_defaults();
    ui_set_poll(ui_poll); level_set_hint_callback(hint_box);
    g_ui_hooks.img = ui_img; g_ui_hooks.kbhit = ui_kbhit; g_ui_hooks.getch = ui_getch;
    g_loop_hooks.message = ui_message; g_loop_hooks.palette_entry = pal_entry;
    rng_set_index(0); stars_init();
    g_loop_hooks.frame = frame_hook; g_loop_hooks.quit_menu = quit_menu; g_loop_hooks.menu_f1 = f_help; g_loop_hooks.menu_f2 = f_save; g_loop_hooks.menu_f3 = f_load; g_loop_hooks.menu_f10 = f_quit; g_loop_hooks.menu_v = f_vol; g_loop_hooks.pause_game = pause_game; g_loop_hooks.end_screen = end_screen; g_loop_hooks.wait_key = wait_key;
    interp_install(); interp_set_view(wide, hudoff); interp_set_enabled(smooth); interp_set_runahead(runahead); interp_set_check(s_check);
    s_tick_shown_inner = g_loop_hooks.tick_shown; g_loop_hooks.tick_shown = tick_shown_main;
    g_difficulty = 1; g_game_speed = 0; g_joy_enabled = 0; g_lv.mana = 1;
    g_episode = (int16_t)episode; g_lv.level_icon = (int16_t)level;
    (void)s_frames_left;

    if (end_test >= 0) { pal_entry(DAT_PAL_LO, 0); pal_entry(DAT_PAL_HI, 128); g_exit_clock = 140 * 100; ui_end_screen(end_test, 12, 9); SDL_Quit(); return 0; }
    g_front.key = menu_key; g_front.pace = front_pace; g_front.quit = front_quit; g_front.music = play_music; g_front.music_off = stop_music;
    g_front.pal = pal_entry; g_front.img = ui_img; g_front.flush_keys = menu_flush; g_front.apply_volume = apply_volume; g_front.sfx = play_sfx; g_front.ticks = SDL_GetTicks; g_front.text = text_key; g_front.voc = play_voc;
    if (test_screen) {                                /* --test-screen endN | hs: show one front-end screen (with --keys / --frames / --dump) */
        pal_entry(DAT_PAL_LO, 0); pal_entry(DAT_PAL_HI, 128);
        if (!strcmp(test_screen, "demo")) frontend_demo();
        else if (!strncmp(test_screen, "end", 3)) frontend_episode_end(atoi(test_screen + 3));
        else if (!strcmp(test_screen, "save")) { g_episode = 2; g_lv.level_icon = 4; g_difficulty = 1; g_score_saved = 12345; frontend_save(); }
        else if (!strcmp(test_screen, "restore")) { int r = frontend_restore(); printf("restore=%d ep=%d lvl=%d skill=%d score=%u\n", r, g_episode, g_lv.level_icon, g_difficulty, (unsigned)g_lv.score); }
        else { int p = hs_check(0, 500000); hs_enter(0, p - 1); }
        SDL_Quit(); return 0;
    }
    if (!direct) {
        frontend_run();
        if (s_check) { int pn, pb; interp_check_counts(&pn, &pb); printf("smooth-check: %d ticks compared, %d differ; run-ahead: %d ticks predicted, %d mispredicted\n", s_checked, s_check_bad, pn, pb); }
        SDL_Quit(); return 0;
    }
    int pause = 1;
    while (!g_quit) {
        g_score_saved = g_lv.score;
        int r = level_run(0, pause); pause = 1;
        if (r == -1 || r == -2) break;
        if (r == 1) { if (++g_lv.level_icon >= 9) { g_lv.level_icon = 0; if (++g_episode >= 4) g_episode = 0; } }
    }
    if (s_check) { int pn, pb; interp_check_counts(&pn, &pb); printf("smooth-check: %d ticks compared, %d differ; run-ahead: %d ticks predicted, %d mispredicted\n", s_checked, s_check_bad, pn, pb); }
    SDL_Quit();
    return 0;
}
