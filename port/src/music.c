/*
 * music.c - HOCUS.EXE music: the MIDI-file sequencer (segment 144e) and the AdLib FM voice driver (segment 161b) - the mono AdLib path.
 *
 * Sequencer: standard MIDI file, format 0/1, one chunk list; one call of midi_service() is one MIDI tick (the original runs it from a timer task whose
 * rate is division*120/60 Hz at the start and is changed by tempo events).  Events go to the driver through the table at DS:2ED4, which for the
 * AdLib holds note off/on, control change, program change and pitch bend (aftertouch is unused).  Channel volume (controller 7) is scaled by the
 * master volume (DS:2ED2) in the sequencer before it reaches the driver.
 *
 * Driver: 9 OPL voices (voice = two operators); voices are handed out from a FIFO free list; when none is free the oldest note of channel 9 is stolen.
 * The driver keeps the original's quirks (e.g. "all notes off" re-initialises the chip and every channel).
 */
#include "music.h"
#include "opl.h"
#include <string.h>
#include <stdlib.h>

al_write_fn g_al_write = opl_write;
uint16_t g_al_note_pitch[32][12];
static void W(int reg, int val) { g_al_write(reg & 0xFFFF, val & 0xFF); }

/* ------------------------------------------------------------------ driver (161b) ------------------------------------------------------- */
typedef struct { int next, prev, key, vel, chan, pitch, timbre, status; } Voice;      /* DS:D3DA + 0x18 * n */
typedef struct { int head, tail, program, bend, transpose, fine, volume, pan, unk, key_off, vel_off; } Chan;   /* DS:D092 + 0x1C * n */
static Voice s_v[9]; static Chan s_c[16];
static int s_free_head, s_free_tail;
static int s_maxlevel[18], s_scale[18];                    /* DS:D06E / D04A, per operator slot */
static int s_bend_scale;                                   /* DS:D048 */

static int note_hi[96], note_lo[96];
static void list_add(int node, int *head, int *tail)     /* 143f:2 - append at the tail (newest) */
{
    s_v[node].next = -1; s_v[node].prev = *tail;
    if (*tail >= 0) s_v[*tail].next = node; else *head = node;
    *tail = node;
}
static void list_remove(int node, int *head, int *tail)  /* 143f:6f */
{
    int n = s_v[node].next, p = s_v[node].prev;
    if (p < 0) *head = n; else s_v[p].next = n;
    if (n < 0) *tail = p; else s_v[n].prev = p;
    s_v[node].next = s_v[node].prev = -1;
}

void al_init(void)                                         /* 161b:0838 + 095B */
{
    for (int i = 0; i < 96; i++) { note_lo[i] = i % 12; note_hi[i] = i / 12; }
    for (int r = 0; r < 32; r++) for (int n = 0; n < 12; n++)
        g_al_note_pitch[r][n] = r == 0 ? g_al_note_pitch0[n] : g_al_fine[r][n];
    al_reset();
}
void al_reset(void)                                        /* 161b:095B */
{
    s_bend_scale = 2;
    W(1, 0x20); W(8, 0); W(0xBD, 0);
    for (int v = 0; v < 9; v++) {
        W(0xA0 + v, 0); W(0xB0 + v, 0);
        W(0xE0 + g_al_offset_slot[g_al_slot_voice[2 * v]], 0); W(0xE0 + g_al_offset_slot[g_al_slot_voice[2 * v + 1]], 0);
    }
    s_free_head = s_free_tail = -1;                        /* 06D8 */
    for (int v = 0; v < 9; v++) {
        s_v[v].key = 0; s_v[v].vel = 0; s_v[v].chan = -1; s_v[v].timbre = -1; s_v[v].status = 0;
        list_add(v, &s_free_head, &s_free_tail);
    }
    for (int c = 0; c < 16; c++) {
        Chan *ch = &s_c[c];
        ch->head = ch->tail = -1; ch->program = 0; ch->bend = 0; ch->transpose = 0; ch->fine = 0; ch->volume = 0x5A; ch->pan = 0;
        ch->key_off = 0; ch->vel_off = 0;
    }
}
static void set_voice_timbre(int v)                        /* 161b:006E */
{
    int chn = s_v[v].chan, t;
    if (chn == 9) t = g_al_drum_map[(s_v[v].key * 2) & 0xFF]; else t = s_c[chn].program;
    if (s_v[v].timbre == t) return;
    s_v[v].timbre = t;
    const uint8_t *p = &g_al_timbre[t * 11];
    int sm = g_al_slot_voice[2 * v], di = g_al_offset_slot[sm];
    s_maxlevel[sm] = 0x3F - (p[2] & 0x3F); s_scale[sm] = p[2] & 0xC0;
    W(0xA0 + v, 0); W(0xB0 + v, 0);
    W(0x80 + di, 0xFF); W(0x60 + di, p[4]); W(0x80 + di, p[6]); W(0x20 + di, p[0]); W(0xE0 + di, p[8]); W(0x40 + di, p[2]);
    int sc = g_al_slot_voice[2 * v + 1];
    W(0xC0 + v, p[10]);
    di = g_al_offset_slot[sc];
    s_maxlevel[sc] = 0x3F - (p[3] & 0x3F); s_scale[sc] = p[3] & 0xC0;
    W(0x40 + di, 0x3F); W(0x80 + di, 0xFF); W(0x60 + di, p[5]); W(0x80 + di, p[7]); W(0x20 + di, p[1]); W(0xE0 + di, p[9]);
}
static void set_voice_volume(int v)                        /* 161b:030C */
{
    int chn = s_v[v].chan, vol = s_v[v].vel + s_c[chn].vel_off;
    if (vol <= 0x7F) vol = 0x7F;                           /* sic: the original compiles this clamp as max(), so velocity has no effect */
    int sc = g_al_slot_voice[2 * v + 1];
    uint32_t x = (uint32_t)s_maxlevel[sc] * (uint32_t)(vol | 0x80);
    x = x * (uint32_t)s_c[chn].volume; x >>= 15;
    x = (x ^ 0x3F) | (uint32_t)s_scale[sc];
    W(0x40 + g_al_offset_slot[sc], (int)x);
}
static void set_voice_pitch(int v)                         /* 161b:0469 */
{
    int chn = s_v[v].chan, n;
    n = chn == 9 ? g_al_drum_map[(s_v[v].key * 2 + 1) & 0xFF] : s_v[v].key;
    n += s_c[chn].transpose + s_c[chn].key_off - 12;
    if (n > 0x5F) n = 0x5F;
    if (n < 0) n = 0;
    int p = g_al_octave_pitch[note_hi[n]] | g_al_note_pitch[s_c[chn].fine & 31][note_lo[n]];
    s_v[v].pitch = p; p |= s_v[v].status;
    W(0xA0 + v, p); W(0xB0 + v, p >> 8);
}
static int find_voice(int ch, int key)                     /* 161b:041E */
{
    for (int v = s_c[ch].head; v >= 0; v = s_v[v].next) if (s_v[v].key == key) return v;
    return -1;
}
void al_note_off(int ch, int key, int vel)                 /* 161b:0B67 */
{
    (void)vel;
    int v = find_voice(ch, key);
    if (v < 0) return;
    s_v[v].status = 0;
    W(0xB0 + v, s_v[v].pitch >> 8);
    list_remove(v, &s_c[ch].head, &s_c[ch].tail);
    list_add(v, &s_free_head, &s_free_tail);
}
void al_note_on(int ch, int key, int vel)                  /* 161b:0C48 */
{
    if (vel == 0) { al_note_off(ch, key, vel); return; }
    int v = s_free_head;
    if (v >= 0) list_remove(v, &s_free_head, &s_free_tail);
    if (v < 0) {
        if (s_c[9].head >= 0) {
            al_note_off(9, s_v[s_c[9].head].key, 0);
            v = s_free_head; if (v >= 0) list_remove(v, &s_free_head, &s_free_tail);
        }
        if (v < 0) return;
    }
    s_v[v].key = key; s_v[v].chan = ch; s_v[v].vel = vel; s_v[v].status = 0x2000;
    list_add(v, &s_c[ch].head, &s_c[ch].tail);
    set_voice_timbre(v); set_voice_volume(v); set_voice_pitch(v);
}
void al_set_channel_volume(int ch, int vol)                /* 161b:063D */
{
    if (vol < 0) vol = 0;
    if (vol > 0x7F) vol = 0x7F;
    s_c[ch].volume = vol;
    for (int v = s_c[ch].head; v >= 0; v = s_v[v].next) set_voice_volume(v);
}
static void all_notes_off(int ch)                          /* 161b:0D21 */
{
    while (s_c[ch].head >= 0) al_note_off(ch, s_v[s_c[ch].head].key, 0);
    al_reset();
}
void al_control(int ch, int ctrl, int val)                 /* 161b:0D5F */
{
    switch (ctrl) {
    case 7: al_set_channel_volume(ch, val); break;
    case 10: s_c[ch].pan = val; break;
    case 0x5E: s_c[ch].unk = val; break;
    case 0x7B: all_notes_off(ch); break;
    case 0x79: al_set_channel_volume(ch, 0x5A); s_c[ch].pan = 0; s_c[ch].unk = 0; break;
    default: break;
    }
}
void al_program(int ch, int prog) { s_c[ch].program = prog; }                         /* 161b:0DCA */
void al_pitch_bend(int ch, int lsb, int msb)               /* 161b:0DE0 */
{
    int raw = (lsb + (msb << 8)) & 0xFFFF;
    s_c[ch].bend = raw;
    uint32_t t = ((uint32_t)(raw * s_bend_scale) & 0xFFFF) / 512u;
    s_c[ch].transpose = (int)(t >> 5) - s_bend_scale;
    s_c[ch].fine = (int)(t & 0x1F);
    for (int v = s_c[ch].head; v >= 0; v = s_v[v].next) set_voice_pitch(v);
}

/* ------------------------------------------------------------------ sequencer (144e) ---------------------------------------------------- */
typedef struct { int next; uint32_t delta; int active, running; size_t start, cur; } Track;     /* 0x18 bytes at the original */
static const uint8_t *s_data; static size_t s_size;
static Track s_tr[64]; static int s_ntracks;
static int s_active_tracks, s_playing, s_loaded, s_loop;
static int s_master = 0xFF;                                  /* DS:2ED2 */
static int s_chanvol[16];                                    /* DS:CFE8 */
static int s_division;                                       /* DS:CFE6 */
static void (*s_rate_cb)(int) = NULL;
static int s_rate;

static int byte_at(size_t i) { return i < s_size ? s_data[i] : 0; }
static uint32_t read_be(size_t pos, int n)                   /* 144e:000D */
{
    uint32_t v = 0;
    if (n > 4) n = 4;
    for (int i = 0; i < n; i++) v = (v << 8) + (uint32_t)byte_at(pos + i);
    return v;
}
static uint32_t read_var(Track *t)                           /* 144e:006C */
{
    int b = byte_at(t->cur++); uint32_t v = (uint32_t)b;
    if (b & 0x80) {
        v = (uint32_t)(b & 0x7F);
        do { b = byte_at(t->cur++); v = (v << 7) + (uint32_t)(b & 0x7F); } while (b & 0x80);
    }
    return v;
}
static void rewind_tracks(void)                              /* 144e:00F0 */
{
    s_active_tracks = 0;
    for (int i = 0; i < s_ntracks; i++) {
        Track *t = &s_tr[i];
        t->cur = t->start; t->delta = read_var(t); t->running = 0; t->active = 1; s_active_tracks++;
    }
}
static void seq_channel_volume(int ch, int vol)              /* 144e:05E5 */
{
    s_chanvol[ch] = vol;
    vol = (int16_t)(vol * s_master) / 255;                   /* no driver volume entry for the AdLib */
    al_control(ch, 7, vol);
}
static void all_notes_off_16(void) { for (int c = 0; c < 16; c++) al_control(c, 0x7B, 0); }          /* 144e:05B4 */
static void reset_controllers_16(void)                       /* 144e:0654 */
{
    for (int c = 0; c < 16; c++) {
        al_control(c, 0x79, 0); al_control(c, 0x64, 0); al_control(c, 0x65, 0); al_control(c, 6, 2); al_control(c, 0x26, 0);
        seq_channel_volume(c, 0x5A);
    }
}
static void set_rate(int r) { s_rate = r; if (s_rate_cb) s_rate_cb(r); }
static void event(Track *t)                                  /* 144e:017F..04FB */
{
    int st = byte_at(t->cur++);
    if (st == 0xFF) {
        int type = byte_at(t->cur++), len = byte_at(t->cur++);
        if (type == 0x2F) { t->active = 0; s_active_tracks--; }
        else if (type == 0x51) {
            uint32_t tempo = read_be(t->cur, 3);
            if (tempo) { uint32_t bpm = 60000000u / tempo; set_rate((int)((bpm * (uint32_t)(int16_t)s_division / 60u) & 0xFFFF)); }
        }
        t->cur += (size_t)len;
    } else {
        if (st & 0x80) t->running = st; else { st = t->running; t->cur--; }
        int ch = st & 15, cmd = st >> 4, a, b;
        switch (cmd) {
        case 8: a = byte_at(t->cur++); b = byte_at(t->cur++); al_note_off(ch, a, b); break;
        case 9: a = byte_at(t->cur++); b = byte_at(t->cur++); al_note_on(ch, a, b); break;
        case 0xA: t->cur += 2; break;
        case 0xB:
            a = byte_at(t->cur++); b = byte_at(t->cur++);
            if (a == 0x7E) t->cur++;
            if (a == 7) seq_channel_volume(ch, b); else al_control(ch, a, b);
            break;
        case 0xC: a = byte_at(t->cur++); al_program(ch, a); break;
        case 0xD: t->cur++; break;
        case 0xE: a = byte_at(t->cur++); b = byte_at(t->cur++); al_pitch_bend(ch, a, b); break;
        default: break;
        }
    }
    t->delta = read_var(t);
}
void midi_service(void)                                      /* 144e:015D */
{
    if (!s_playing) return;
    int ti = s_ntracks ? 0 : -1;
    while (ti >= 0) {
        Track *t = &s_tr[ti];
        while (t->active && t->delta == 0) event(t);
        t->delta--;
        ti = t->next;
        if (s_active_tracks == 0) {
            rewind_tracks();
            if (s_loop) {
                for (int c = 0; c < 16; c++) if (s_chanvol[c] != 0x5A) seq_channel_volume(c, 0x5A);
                ti = s_ntracks ? 0 : -1;
            } else { s_playing = 0; ti = -1; }
        }
    }
}
int midi_play(const uint8_t *song, size_t size, int loop)    /* 144e:0822 */
{
    if (s_loaded) midi_stop();
    s_data = song; s_size = size;
    if (size < 14 || memcmp(song, "MThd", 4)) return 0xFF03;
    uint32_t hlen = read_be(4, 4); int fmt = (int)read_be(8, 2), n = (int)read_be(10, 2); s_division = (int)read_be(12, 2);
    if (fmt > 1) return 0xFF04;
    if (!n) return 0xFF05;
    if (n > 64) n = 64;
    size_t p = 8 + hlen; s_ntracks = 0;
    for (int i = 0; i < n; i++) {
        if (p + 8 > size || memcmp(song + p, "MTrk", 4)) { s_ntracks = 0; return 0xFF06; }
        uint32_t len = read_be(p + 4, 4);
        Track *t = &s_tr[s_ntracks]; memset(t, 0, sizeof *t);
        t->start = p + 8; t->next = i + 1 < n ? s_ntracks + 1 : -1; s_ntracks++;
        p += 8 + len;
    }
    rewind_tracks();
    all_notes_off_16(); reset_controllers_16();
    s_loop = loop;
    set_rate((uint16_t)((int16_t)(s_division * 120) / 60));       /* 16-bit imul/idiv like the original */
    s_playing = 1; s_loaded = 1;
    return 0;
}
void midi_stop(void)                                         /* 144e:07B0 */
{
    if (!s_loaded) return;
    s_playing = 0; s_loaded = 0;
    all_notes_off_16(); reset_controllers_16();
    s_ntracks = 0;
}
void midi_set_volume(int vol)                                /* 144e:06DC */
{
    if (vol > 0xFF) vol = 0xFF;
    if (vol < 0) vol = 0;
    s_master = vol;
    for (int c = 0; c < 16; c++) seq_channel_volume(c, s_chanvol[c]);
}
void midi_reset(void)                                        /* power-on state of the sequencer (the original's zeroed DS) */
{
    s_playing = s_loaded = s_loop = 0; s_ntracks = 0; s_active_tracks = 0; s_master = 0; s_division = 0; s_rate = 0;
    memset(s_chanvol, 0, sizeof s_chanvol);
}
int midi_playing(void) { return s_playing; }
void midi_pause(void) { if (s_loaded) { s_playing = 0; all_notes_off_16(); } }
void midi_resume(void) { if (s_loaded) s_playing = 1; }
void midi_set_rate_callback(void (*fn)(int rate)) { s_rate_cb = fn; }
int midi_tick_rate(void) { return s_rate; }

/* ------------------------------------------------------------------ player: sequencer clock + OPL -> audio samples ------------------------- */
static int s_out_rate = 44100, s_tick_rate; static int64_t s_tick_acc; static double s_opl_frac; static int s_s0, s_s1;
static uint8_t *s_song_copy;
static void player_rate(int r) { s_tick_rate = r; }
void music_init(int out_rate)
{
    s_out_rate = out_rate > 0 ? out_rate : 44100; s_tick_rate = 0; s_tick_acc = 0; s_opl_frac = 0; s_s0 = s_s1 = 0;
    g_al_write = opl_write; opl_reset(); midi_reset(); al_init(); midi_set_rate_callback(player_rate); midi_set_volume(0xC0);
}
void music_stop(void)
{
    midi_stop(); free(s_song_copy); s_song_copy = NULL; s_tick_rate = 0;
}
void music_start(const uint8_t *song, size_t size, int loop)
{
    music_stop();
    s_song_copy = malloc(size ? size : 1);
    if (!s_song_copy) return;
    memcpy(s_song_copy, song, size);
    if (midi_play(s_song_copy, size, loop)) { free(s_song_copy); s_song_copy = NULL; }
    s_tick_acc = 0;
}
void music_set_volume(int vol) { midi_set_volume(vol); }
int music_active(void) { return midi_playing(); }
void music_render(int16_t *out, int frames)
{
    for (int i = 0; i < frames; i++) {
        if (midi_playing() && s_tick_rate > 0) {
            s_tick_acc += s_tick_rate;
            while (s_tick_acc >= s_out_rate) { s_tick_acc -= s_out_rate; midi_service(); }
        }
        s_opl_frac += (double)OPL_RATE / s_out_rate;
        while (s_opl_frac >= 1.0) { s_opl_frac -= 1.0; s_s0 = s_s1; s_s1 = opl_sample(); }
        int s = (int)(s_s0 + (s_s1 - s_s0) * (1.0 - s_opl_frac)) * 3 + out[i];
        out[i] = (int16_t)(s > 32767 ? 32767 : s < -32768 ? -32768 : s);
    }
}
