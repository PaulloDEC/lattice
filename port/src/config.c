/* config.c - HOCUS.CFG (see config.h) */
#include "config.h"
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

PortSettings g_port;

const char *const g_bind_name[BIND_ACTIONS] = { "Left", "Right", "Up", "Down", "Jump", "Fire", "Look up", "Look down", "Pause" };
static const char *const k_bind_key[BIND_ACTIONS] = { "left", "right", "up", "down", "jump", "fire", "look_up", "look_down", "pause" };
/* SDL_GameControllerButton values (stable across SDL 2 releases), named as the Xbox / Steam Deck labels */
static const char *const k_btn[] = { "A", "B", "X", "Y", "View", "Guide", "Menu", "L-Stick", "R-Stick", "LB", "RB",
                                     "D-Up", "D-Down", "D-Left", "D-Right", "Share", "P1", "P2", "P3", "P4", "Touchpad" };
#define NBTN ((int)(sizeof k_btn / sizeof k_btn[0]))
static const char *const k_stick[8] = { "L-Stick Left", "L-Stick Right", "L-Stick Up", "L-Stick Down",
                                        "R-Stick Left", "R-Stick Right", "R-Stick Up", "R-Stick Down" };
static const char *const k_fr[FR_COUNT] = { "original", "30", "60", "120", "display" };

void config_default_keys(void)            /* the original's keys (DS:1558) + P for pause */
{
    static const int d[BIND_ACTIONS][BIND_SLOTS] = {
        { SDL_SCANCODE_LEFT, 0 }, { SDL_SCANCODE_RIGHT, 0 }, { SDL_SCANCODE_UP, 0 }, { SDL_SCANCODE_DOWN, 0 },
        { SDL_SCANCODE_LCTRL, 0 }, { SDL_SCANCODE_LALT, 0 }, { SDL_SCANCODE_PAGEUP, 0 }, { SDL_SCANCODE_PAGEDOWN, 0 },
        { SDL_SCANCODE_P, 0 } };
    memcpy(g_port.key, d, sizeof d);
}
void config_default_pad(void)
{
    static const int d[BIND_ACTIONS][BIND_SLOTS] = {
        { 13, PAD_LS_LEFT }, { 14, PAD_LS_LEFT + 1 }, { 11, PAD_LS_LEFT + 2 }, { 12, PAD_LS_LEFT + 3 },   /* D-pad + left stick */
        { 0, PAD_LT }, { 2, PAD_RT },                                                                      /* A + LT, X + RT */
        { 9, PAD_RS_LEFT + 2 }, { 10, PAD_RS_LEFT + 3 },                                                   /* LB + right stick */
        { 4, PAD_NONE } };                                                                                 /* View */
    memcpy(g_port.pad, d, sizeof d);
}
void config_defaults(void)
{
    memset(&g_port, 0, sizeof g_port);
    g_port.framerate = FR_DISPLAY;
    g_port.display = 3;
    config_default_keys(); config_default_pad();
}

const char *config_pad_name(int b)
{
    if (b == PAD_LT) return "LT";
    if (b == PAD_RT) return "RT";
    if (b >= PAD_LS_LEFT && b < PAD_CODES) return k_stick[b - PAD_LS_LEFT];
    return b >= 0 && b < NBTN ? k_btn[b] : "-";
}
int config_pad_assignable(int b)
{
    return (b >= 0 && b < NBTN && b != 5 && b != 6) || (b >= PAD_LT && b < PAD_CODES);
}
static int pad_code(const char *s)
{
    for (int i = 0; i < PAD_CODES; i++) if (config_pad_assignable(i) && !strcmp(s, config_pad_name(i))) return i;
    return PAD_NONE;
}
int config_key_assignable(int sc)
{
    if (sc <= 0 || sc >= SDL_NUM_SCANCODES || sc == SDL_SCANCODE_ESCAPE) return 0;
    if ((sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) || (sc >= SDL_SCANCODE_F13 && sc <= SDL_SCANCODE_F24)) return 0;
    return SDL_GetScancodeName((SDL_Scancode)sc)[0] != 0;
}
const char *config_key_name(int sc)
{
    static char b[4][24]; static int n;
    if (sc <= 0) return "-";
    char *o = b[n++ & 3]; const char *s = SDL_GetScancodeName((SDL_Scancode)sc);
    if (sc == SDL_SCANCODE_LEFT) s = "Left arrow"; else if (sc == SDL_SCANCODE_RIGHT) s = "Right arrow";        /* as the original named them */
    else if (sc == SDL_SCANCODE_UP) s = "Up arrow"; else if (sc == SDL_SCANCODE_DOWN) s = "Down arrow";
    int i = 0;
    for (; s[i] && i < 23; i++) o[i] = (s[i] >= 0x20 && s[i] <= 0x7A) ? s[i] : '?';   /* the game font: 0x20..0x7A */
    o[i] = 0;
    return i ? o : "?";
}
void config_bind_key(int a, int slot, int sc)
{
    if (sc > 0) for (int i = 0; i < BIND_ACTIONS; i++) for (int k = 0; k < BIND_SLOTS; k++) if (g_port.key[i][k] == sc) g_port.key[i][k] = 0;
    g_port.key[a][slot] = sc > 0 ? sc : 0;
}
void config_bind_pad(int a, int slot, int code)
{
    if (code >= 0) for (int i = 0; i < BIND_ACTIONS; i++) for (int k = 0; k < BIND_SLOTS; k++) if (g_port.pad[i][k] == code) g_port.pad[i][k] = PAD_NONE;
    g_port.pad[a][slot] = code >= 0 ? code : PAD_NONE;
}

int config_fps(int fr) { return fr == FR_30 ? 30 : fr == FR_60 ? 60 : fr == FR_120 ? 120 : 0; }
const char *config_framerate_name(int fr)
{
    static const char *const n[FR_COUNT] = { "Original", "30 fps", "60 fps", "120 fps", "Display" };
    return fr >= 0 && fr < FR_COUNT ? n[fr] : "?";
}

void config_load(const char *path)
{
    config_defaults();
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        v[strcspn(v, "\r\n")] = 0;
        while (*k == ' ') k++;
        while (*v == ' ') v++;
        if (!strcmp(k, "hud")) g_port.hud_off = !strcmp(v, "off");
        else if (!strcmp(k, "aspect")) g_port.wide = !strcmp(v, "16:9");
        else if (!strcmp(k, "music")) g_port.music = !strcmp(v, "sbpro") ? MUS_SBPRO : !strcmp(v, "gm") ? MUS_GM : MUS_ADLIB;
        else if (!strcmp(k, "display")) { if (!strcmp(v, "fullscreen")) g_port.display = 0; else { int n = atoi(v); if (n >= 2 && n <= 8) g_port.display = n; } }
        else if (!strcmp(k, "framerate")) { for (int i = 0; i < FR_COUNT; i++) if (!strcmp(v, k_fr[i])) g_port.framerate = i; }
        else for (int a = 0; a < BIND_ACTIONS; a++) for (int s = 0; s < BIND_SLOTS; s++) {
            char key[32];
            snprintf(key, sizeof key, "key_%s%s", k_bind_key[a], s ? "2" : "");     /* key_jump=Left Ctrl, key_jump2=Space ("-" = none) */
            if (!strcmp(k, key)) {
                int sc = strcmp(v, "-") ? (int)SDL_GetScancodeFromName(v) : 0;
                if (!strcmp(v, "-") || config_key_assignable(sc)) { g_port.key[a][s] = sc; g_port.keys_from_cfg = 1; }
            }
            snprintf(key, sizeof key, "pad_%s%s", k_bind_key[a], s ? "2" : "");     /* pad_jump=A, pad_jump2=LT */
            if (!strcmp(k, key)) {
                int c = strcmp(v, "-") ? pad_code(v) : PAD_NONE;
                if (!strcmp(v, "-") || c != PAD_NONE) g_port.pad[a][s] = c;
            }
        }
    }
    fclose(f);
}

void config_save(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Hocus Pocus port settings (the original options stay in HOCUS.SAV)\n");
    fprintf(f, "music=%s\n", g_port.music == MUS_SBPRO ? "sbpro" : g_port.music == MUS_GM ? "gm" : "adlib");
    if (g_port.display) fprintf(f, "display=%dx\n", g_port.display); else fprintf(f, "display=fullscreen\n");
    fprintf(f, "hud=%s\naspect=%s\nframerate=%s\n", g_port.hud_off ? "off" : "classic", g_port.wide ? "16:9" : "4:3", k_fr[g_port.framerate]);
    for (int a = 0; a < BIND_ACTIONS; a++) for (int s = 0; s < BIND_SLOTS; s++)
        fprintf(f, "key_%s%s=%s\n", k_bind_key[a], s ? "2" : "", g_port.key[a][s] ? SDL_GetScancodeName((SDL_Scancode)g_port.key[a][s]) : "-");
    for (int a = 0; a < BIND_ACTIONS; a++) for (int s = 0; s < BIND_SLOTS; s++)
        fprintf(f, "pad_%s%s=%s\n", k_bind_key[a], s ? "2" : "", g_port.pad[a][s] >= 0 ? config_pad_name(g_port.pad[a][s]) : "-");
    fclose(f);
}
