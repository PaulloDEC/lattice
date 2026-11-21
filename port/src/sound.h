/*
 * sound.h - sound effects (original 136B:000B sound(id) -> Apogee Sound System).  The original table DS:1574 = 16 x {VOC entry, PC-speaker entry,
 * priority} (EXE 0x20680); this port plays the Sound Blaster VOC samples (DAT entries 612..648) through a small software mixer and ignores the PC
 * speaker entries.  Ids 0..15; the original ignores ids >= 16 (and the speaker path ignores ids >= 14).
 */
#ifndef HOCUS_SOUND_H
#define HOCUS_SOUND_H
#include <stdint.h>

#define SFX_COUNT 16
extern const uint16_t g_sfx_table[SFX_COUNT][3];      /* {voc entry, pc-speaker entry, priority} */

/* Decodes the Creative Voice File `voc` (blocks 1 sound data, 2 continuation, 3 silence, 0 end) into unsigned 8-bit PCM; returns the sample
 * count (malloc'ed *pcm, caller frees) and the sample rate in *rate, or 0 on a bad file. */
uint32_t voc_decode(const uint8_t *voc, uint32_t size, uint8_t **pcm, uint32_t *rate);

int  sound_init(int out_rate);                         /* loads all 16 samples from the open DAT; returns the number loaded */
void sound_play(int id);                               /* starts sound `id` (honours g_sound_on); an already playing copy restarts */
void sound_play_entry(int entry);                      /* 136B:01B8: play one whole VOC entry of the DAT */
void sound_set_volume(int v);                          /* 0..255 (settings: DS:4CA2) */
void sound_stop_all(void);
void sound_mix(int16_t *out, int frames);              /* mixes mono 16-bit samples into `out` (adds, does not clear) */
#endif
