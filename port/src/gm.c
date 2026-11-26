/* gm.c - see gm.h */
#include "gm.h"
#include <string.h>
#define TSF_IMPLEMENTATION
#include "tsf.h"

#define GM_GAIN_DB 0.8f                    /* overall level against the FM music (set by ear / measured RMS) */
static tsf *s_tsf;
static uint8_t s_msg[3]; static int s_len, s_need, s_status;     /* the message being received */
static uint8_t s_sysex[16]; static int s_sx_len, s_in_sysex;

int gm_ready(void) { return s_tsf != NULL; }
int gm_load(const char *path, int out_rate)
{
    tsf *t = tsf_load_filename(path);
    if (!t) return -1;
    if (s_tsf) tsf_close(s_tsf);
    s_tsf = t;
    tsf_set_output(s_tsf, TSF_STEREO_INTERLEAVED, out_rate > 0 ? out_rate : 44100, GM_GAIN_DB);
    tsf_set_max_voices(s_tsf, 128);        /* allocated now, not on the audio thread */
    gm_reset();
    return 0;
}
void gm_reset(void)                        /* GS reset: every channel to its power-on state, channel 10 = drums */
{
    if (!s_tsf) return;
    tsf_reset(s_tsf);
    for (int c = 0; c < 16; c++) {
        tsf_channel_midi_control(s_tsf, c, 121, 0);
        tsf_channel_midi_control(s_tsf, c, 7, 100);
        tsf_channel_set_presetnumber(s_tsf, c, 0, c == 9);
    }
    s_len = s_need = s_status = 0; s_in_sysex = 0;
}
static void message(void)
{
    int ch = s_status & 15, a = s_msg[0], b = s_msg[1];
    switch (s_status >> 4) {
    case 0x8: tsf_channel_note_off(s_tsf, ch, a); break;
    case 0x9: if (b) tsf_channel_note_on(s_tsf, ch, a, b / 127.0f); else tsf_channel_note_off(s_tsf, ch, a); break;
    case 0xB: tsf_channel_midi_control(s_tsf, ch, a, b); break;
    case 0xC: tsf_channel_set_presetnumber(s_tsf, ch, a, ch == 9); break;    /* GS: channel 10 is the drum part */
    case 0xE: tsf_channel_set_pitchwheel(s_tsf, ch, a | (b << 7)); break;
    default: break;                        /* aftertouch, channel pressure: not supported by the synthesizer */
    }
}
static void sysex_end(void)
{
    static const uint8_t gs_reset[] = { 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7F, 0x00, 0x41 };
    static const uint8_t gm_on[] = { 0x7E, 0x7F, 0x09, 0x01 };
    if ((s_sx_len == (int)sizeof gs_reset && !memcmp(s_sysex, gs_reset, sizeof gs_reset)) ||
        (s_sx_len == (int)sizeof gm_on && !memcmp(s_sysex, gm_on, sizeof gm_on))) gm_reset();
}
void gm_byte(int b)
{
    if (!s_tsf) return;
    b &= 0xFF;
    if (s_in_sysex) {
        if (b == 0xF7) { s_in_sysex = 0; sysex_end(); }
        else if (b & 0x80) s_in_sysex = 0;                 /* broken off: a status byte */
        else { if (s_sx_len < (int)sizeof s_sysex) s_sysex[s_sx_len] = (uint8_t)b; s_sx_len++; return; }
        if (!(b & 0x80) || b == 0xF7) return;
    }
    if (b == 0xF0) { s_in_sysex = 1; s_sx_len = 0; return; }
    if (b >= 0xF8) return;                                 /* real time */
    if (b & 0x80) {
        if (b >= 0xF0) { s_status = 0; return; }
        s_status = b; s_len = 0; s_need = (b >> 4) == 0xC || (b >> 4) == 0xD ? 1 : 2; return;
    }
    if (!s_status) return;
    s_msg[s_len++] = (uint8_t)b;
    if (s_len == s_need) { message(); s_len = 0; }         /* running status: keep the status */
}
void gm_render(int16_t *out, int frames)
{
    if (s_tsf) tsf_render_short(s_tsf, out, frames, 1);
}
