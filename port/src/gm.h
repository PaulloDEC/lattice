/*
 * gm.h - General MIDI music: the MIDI bytes of the original's MPU-401 driver (music.c, g_mpu_write) played by a SoundFont
 * synthesizer (TinySoundFont, tsf.h, MIT licence) instead of a Roland Sound Canvas.
 */
#ifndef HOCUS_GM_H
#define HOCUS_GM_H
#include <stdint.h>

int  gm_load(const char *sf2_path, int out_rate);   /* 0 = ok; loading again replaces the SoundFont */
int  gm_ready(void);
void gm_byte(int b);                                 /* one MIDI byte, as sent to the MPU-401 */
void gm_reset(void);                                 /* silence + GS reset (power-on state of a Sound Canvas) */
void gm_render(int16_t *out, int frames);            /* adds interleaved stereo */
#endif
