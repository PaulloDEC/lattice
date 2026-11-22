/* config.c - HOCUS.CFG (see config.h) */
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

PortSettings g_port;

/* SDL_GameControllerButton values (stable across SDL 2 releases), named as the Xbox / Steam Deck labels */
static const char *const k_btn[] = { "A", "B", "X", "Y", "View", "Guide", "Menu", "L-Stick", "R-Stick", "LB", "RB",
                                     "D-Up", "D-Down", "D-Left", "D-Right", "Share", "P1", "P2", "P3", "P4", "Touchpad" };
#define NBTN ((int)(sizeof k_btn / sizeof k_btn[0]))
static const char *const k_act[PAD_ACTIONS] = { "jump", "fire", "look_up", "look_down", "pause" };
static const char *const k_fr[FR_COUNT] = { "original", "30", "60", "120", "display" };

void config_defaults(void)
{
    memset(&g_port, 0, sizeof g_port);
    g_port.framerate = FR_DISPLAY;
    g_port.pad[PAD_JUMP] = 0;            /* A */
    g_port.pad[PAD_FIRE] = 2;            /* X */
    g_port.pad[PAD_LOOKUP] = 9;          /* LB */
    g_port.pad[PAD_LOOKDOWN] = 10;       /* RB */
    g_port.pad[PAD_PAUSE] = 4;           /* View (Back) */
}

const char *config_pad_name(int b)
{
    if (b == PAD_LT) return "LT";
    if (b == PAD_RT) return "RT";
    return b >= 0 && b < NBTN ? k_btn[b] : "-";
}
int config_pad_assignable(int b)
{
    if (b == PAD_LT || b == PAD_RT) return 1;
    return b >= 0 && b < NBTN && b != 5 && b != 6 && (b < 11 || b > 14);
}
static int pad_code(const char *s)
{
    if (!strcmp(s, "LT")) return PAD_LT;
    if (!strcmp(s, "RT")) return PAD_RT;
    for (int i = 0; i < NBTN; i++) if (!strcmp(s, k_btn[i])) return i;
    return -1;
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
        else if (!strcmp(k, "framerate")) { for (int i = 0; i < FR_COUNT; i++) if (!strcmp(v, k_fr[i])) g_port.framerate = i; }
        else for (int a = 0; a < PAD_ACTIONS; a++) {
            char key[32]; snprintf(key, sizeof key, "pad_%s", k_act[a]);
            int b = pad_code(v);
            if (!strcmp(k, key) && config_pad_assignable(b)) g_port.pad[a] = b;
        }
    }
    fclose(f);
}

void config_save(const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "# Hocus Pocus port settings (the original options stay in HOCUS.SAV)\n");
    fprintf(f, "hud=%s\naspect=%s\nframerate=%s\n", g_port.hud_off ? "off" : "classic", g_port.wide ? "16:9" : "4:3", k_fr[g_port.framerate]);
    for (int a = 0; a < PAD_ACTIONS; a++) fprintf(f, "pad_%s=%s\n", k_act[a], config_pad_name(g_port.pad[a]));
    fclose(f);
}
