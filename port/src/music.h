/*
 * music.h - music: MIDI sequencer (original 144e, the Apogee Sound System "midi.c") driving the AdLib FM driver (161b, "al_midi.c") that
 * writes the OPL2 registers; opl.c turns those writes into sound.  Mono (one OPL2) and stereo FM (two chips: Sound Blaster Pro, or an OPL3).
 */
#ifndef HOCUS_MUSIC_H
#define HOCUS_MUSIC_H
#include <stdint.h>
#include <stddef.h>

extern const uint16_t g_al_slot_voice[18];
extern const uint8_t  g_al_offset_slot[18];
extern const uint16_t g_al_note_pitch0[12];
extern const uint16_t g_al_octave_pitch[8];
extern const uint8_t  g_al_drum_map[256];
extern const uint8_t  g_al_timbre[256 * 11];
extern const uint16_t g_al_fine[32][12];

/* OPL register write sink: (I/O port, register, value), as the original's 161b:0001 - the differential test replaces it with a logger. */
typedef void (*al_write_fn)(int port, int reg, int val);
enum { AL_MONO, AL_SBPRO, AL_OPL3 };                  /* one OPL2 at 0x388; two OPL2s at 0x220 / 0x222; an OPL3 at 0x388 / 0x38A */
extern al_write_fn g_al_write;

/* ---- AdLib driver (161b) ---- */
void al_init(void);                                   /* 161b:0838 fine-tune table + 095B reset (called once at start-up) */
void al_reset(void);                                  /* 161b:095B */
void al_set_card(int card);                           /* AL_* (161b:0ACF's port setup), then al_reset */
int  al_card(void);
void al_note_off(int ch, int key, int vel);           /* 161b:0B67 */
void al_note_on(int ch, int key, int vel);            /* 161b:0C48 */
void al_control(int ch, int ctrl, int val);           /* 161b:0D5F */
void al_program(int ch, int prog);                    /* 161b:0DCA */
void al_pitch_bend(int ch, int lsb, int msb);         /* 161b:0DE0 */
void al_set_channel_volume(int ch, int vol);          /* 161b:063D */
extern uint16_t g_al_note_pitch[32][12];              /* DS:3022 (row 0 = g_al_note_pitch0, rows 1..31 fine tune) */

/* ---- MPU-401 General MIDI driver (1711): the MIDI bytes go to this sink (one byte per call) ---- */
typedef void (*mpu_write_fn)(int byte);
extern mpu_write_fn g_mpu_write;

/* ---- sequencer (144e) ---- */
void midi_set_driver(int mpu);                                /* 0 = the AdLib FM driver (161b), 1 = MPU-401 General MIDI (1711) */
int  midi_play(const uint8_t *song, size_t size, int loop);   /* 144e:0822; 0 = ok */
void midi_stop(void);                                         /* 144e:07B0 */
void midi_service(void);                                      /* 144e:015D one MIDI tick */
void midi_set_volume(int vol);                                /* 144e:06DC  0..255 */
int  midi_playing(void);                                      /* 144e:0794 */
int midi_tick_rate(void);                                       /* current MIDI ticks per second (tempo dependent) */
void midi_reset(void);                                        /* clear all sequencer state (before the first song) */
void midi_pause(void);                                        /* 144e:077E */
void midi_resume(void);                                       /* 144e:076C */
void midi_set_rate_callback(void (*fn)(int rate));            /* called when a tempo event changes the rate (the timer task rate) */

/* ---- music player (mixer side) ---- */
void music_init(int out_rate);
void music_start(const uint8_t *song, size_t size, int loop); /* replaces the current song */
void music_stop(void);
void music_set_volume(int vol);                               /* 0..255 (settings: DS:4CA4) */
#define MUSIC_GM 3                                            /* General MIDI (gm.c), for music_set_card */
void music_set_card(int card);                                /* AL_* or MUSIC_GM: stops the song, resets the device; GM without a SoundFont = AL_MONO */
int  music_card(void);
int  music_active(void);                                         /* a song is playing */
void music_render(int16_t *out, int frames);                  /* adds stereo samples (interleaved L R) to out */
#endif
