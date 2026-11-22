/*
 * config.h - the port's own settings (HOCUS.CFG), kept apart from the original HOCUS.SAV so that file keeps
 * the DOS layout.  Plain text, one "key=value" per line; unknown lines are ignored, missing ones keep defaults.
 */
#ifndef HOCUS_CONFIG_H
#define HOCUS_CONFIG_H

enum { FR_ORIGINAL, FR_30, FR_60, FR_120, FR_DISPLAY, FR_COUNT };
enum { PAD_JUMP, PAD_FIRE, PAD_LOOKUP, PAD_LOOKDOWN, PAD_PAUSE, PAD_ACTIONS };
#define PAD_LT 40                         /* the triggers, as button codes after SDL's buttons */
#define PAD_RT 41

typedef struct PortSettings {
    int hud_off;                          /* HUD style: 0 Classic, 1 Off */
    int wide;                             /* aspect ratio: 0 4:3, 1 16:9 */
    int framerate;                        /* FR_* */
    int pad[PAD_ACTIONS];                 /* controller button per action (SDL_GameControllerButton or PAD_LT / PAD_RT) */
} PortSettings;

extern PortSettings g_port;
void config_defaults(void);
void config_load(const char *path);       /* missing file = defaults */
void config_save(const char *path);
int  config_fps(int framerate);           /* the frame cap for FR_*: 0 = none (original / display) */
const char *config_framerate_name(int framerate);
const char *config_pad_name(int button);  /* "A", "LB", "View", "LT"... */
int  config_pad_assignable(int button);   /* D-pad (movement), Start (menu) and Guide are fixed */
#endif
