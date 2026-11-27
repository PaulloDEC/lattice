/*
 * config.h - the port's own settings (HOCUS.CFG), kept apart from the original HOCUS.SAV so that file keeps
 * the DOS layout.  Plain text, one "key=value" per line; unknown lines are ignored, missing ones keep defaults.
 */
#ifndef HOCUS_CONFIG_H
#define HOCUS_CONFIG_H

enum { FR_ORIGINAL, FR_30, FR_60, FR_120, FR_DISPLAY, FR_COUNT };
enum { MUS_ADLIB, MUS_SBPRO, MUS_GM, MUS_COUNT };   /* mono FM, stereo FM (the original's Sound Blaster Pro path), General MIDI */
/* Remappable actions, the same for the keyboard and the controller; two bindings each */
enum { BIND_LEFT, BIND_RIGHT, BIND_UP, BIND_DOWN, BIND_JUMP, BIND_FIRE, BIND_LOOKUP, BIND_LOOKDOWN, BIND_PAUSE, BIND_ACTIONS };
#define BIND_SLOTS 2
/* Controller input codes: SDL_GameControllerButton 0..20, then the triggers and the stick directions */
#define PAD_LT 40
#define PAD_RT 41
#define PAD_LS_LEFT 42                    /* 42..45 left stick left / right / up / down, 46..49 right stick */
#define PAD_RS_LEFT 46
#define PAD_CODES 50
#define PAD_NONE (-1)

typedef struct PortSettings {
    int hud_off;                          /* HUD style: 0 Classic, 1 Off */
    int wide;                             /* aspect ratio: 0 4:3, 1 16:9 */
    int framerate;                        /* FR_* */
    int music;                            /* music device while music is on: MUS_* */
    int display;                          /* display size: 0 = fullscreen, 2..8 = window of that many times 320 x 240 */
    int key[BIND_ACTIONS][BIND_SLOTS];    /* keyboard: SDL_Scancode, 0 = none */
    int pad[BIND_ACTIONS][BIND_SLOTS];    /* controller: PAD codes above, PAD_NONE = none */
    int keys_from_cfg;                    /* the keyboard bindings were in HOCUS.CFG (else taken once from HOCUS.SAV) */
} PortSettings;

extern PortSettings g_port;
void config_defaults(void);
void config_load(const char *path);       /* missing file = defaults */
void config_save(const char *path);
int  config_fps(int framerate);           /* the frame cap for FR_*: 0 = none (original / display) */
const char *config_framerate_name(int framerate);
extern const char *const g_bind_name[BIND_ACTIONS];   /* "Left", "Right", ... "Pause" */
const char *config_pad_name(int code);    /* "A", "LB", "View", "LT", "L-Stick Left"... */
int  config_pad_assignable(int code);     /* everything but Menu (opens the menu) and Guide (the system's) */
int  config_key_assignable(int scancode); /* everything but Esc and F1..F12 */
const char *config_key_name(int scancode);  /* SDL's name, limited to the game font's characters */
void config_default_keys(void);
void config_default_pad(void);
/* Bind a key / pad code to (action, slot); the code leaves any other slot it was in ("move"). code 0 / PAD_NONE clears. */
void config_bind_key(int action, int slot, int scancode);
void config_bind_pad(int action, int slot, int code);
#endif
