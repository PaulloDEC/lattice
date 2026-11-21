#include "sound.h"
#include "dat.h"
#include "loop.h"
#include <stdlib.h>
#include <string.h>

const uint16_t g_sfx_table[SFX_COUNT][3] = {
    {620, 621, 14}, {624, 625, 13}, {626, 627, 12}, {628, 629, 12}, {634, 635, 18}, {630, 631, 17}, {636, 637, 14}, {638, 639, 16},
    {640, 641, 15}, {642, 643, 15}, {644, 645, 16}, {612, 613, 19}, {618, 619, 15}, {622, 623, 14}, {646, 647, 13}, {648, 649, 11},
};

uint32_t voc_decode(const uint8_t *v, uint32_t size, uint8_t **pcm, uint32_t *rate)
{
    static const char sig[] = "Creative Voice File\x1a";
    if (size < 26 || memcmp(v, sig, 20)) return 0;
    uint32_t p = v[20] | (v[21] << 8), total = 0, cap = size * 2 + 16, r = 0;
    uint8_t *out = malloc(cap);
    if (!out) return 0;
    while (p < size && v[p] != 0) {
        if (p + 4 > size) break;
        uint32_t t = v[p], len = v[p + 1] | (v[p + 2] << 8) | (v[p + 3] << 16), b = p + 4;
        if (b + len > size) len = size - b;
        if (t == 1 && len >= 2) {
            if (!r) r = 1000000u / (256 - v[b]);
            if (v[b + 1] == 0) { if (total + len - 2 > cap) break; memcpy(out + total, v + b + 2, len - 2); total += len - 2; }
        } else if (t == 2) { if (total + len > cap) break; memcpy(out + total, v + b, len); total += len; }
        else if (t == 3 && len >= 3 && r) {
            uint32_t n = (v[b] | (v[b + 1] << 8)) + 1u;
            if (total + n > cap) break;
            memset(out + total, 128, n); total += n;
        }
        p = b + len;
    }
    if (!total || !r) { free(out); return 0; }
    *pcm = out; *rate = r; return total;
}

typedef struct { uint8_t *pcm; uint32_t n, rate; } Sample;
typedef struct { int id; uint64_t pos; } Voice;       /* pos in 32.32 fixed point source samples */
static Sample s_smp[SFX_COUNT + 1];                  /* [SFX_COUNT] = the one-off sample of sound_play_entry */
static int prio(int id) { return id >= SFX_COUNT ? 20 : g_sfx_table[id][2]; }
static Voice s_voice[8];
static int s_out_rate = 44100;
static int s_volume = 255;                              /* DS:4CA2 */
void sound_set_volume(int v) { s_volume = v < 0 ? 0 : v > 255 ? 255 : v; }

int sound_init(int out_rate)
{
    int ok = 0;
    s_out_rate = out_rate > 0 ? out_rate : 44100;
    for (int i = 0; i < SFX_COUNT; i++) {
        uint32_t n; uint8_t *raw = dat_load(g_sfx_table[i][0], &n);
        if (!raw) continue;
        uint8_t *pcm; uint32_t rate, cnt = voc_decode(raw, n, &pcm, &rate);
        free(raw);
        if (cnt) { s_smp[i].pcm = pcm; s_smp[i].n = cnt; s_smp[i].rate = rate; ok++; }
    }
    for (int i = 0; i < 8; i++) s_voice[i].id = -1;
    return ok;
}

void sound_play(int id)
{
    if (!g_sound_on || id < 0 || id >= SFX_COUNT || !s_smp[id].n) return;
    int slot = -1;
    for (int i = 0; i < 8; i++) if (s_voice[i].id == id) slot = i;
    for (int i = 0; slot < 0 && i < 8; i++) if (s_voice[i].id < 0) slot = i;
    if (slot < 0) {                                   /* all busy: steal the voice with the lowest priority */
        slot = 0;
        for (int i = 1; i < 8; i++) if (prio(s_voice[i].id) < prio(s_voice[slot].id)) slot = i;
        if (prio(s_voice[slot].id) > prio(id)) return;
    }
    s_voice[slot].id = id; s_voice[slot].pos = 0;
}

/* 136B:01B8 plays one digitised sample (a whole DAT entry) outside the 16-sound table (e.g. the episode fanfare, entry 615). */
void sound_play_entry(int entry)
{
    if (!g_sound_on) return;
    uint32_t n; uint8_t *raw = dat_load(entry, &n); if (!raw) return;
    uint8_t *pcm; uint32_t rate, cnt = voc_decode(raw, n, &pcm, &rate); free(raw);
    if (!cnt) return;
    for (int i = 0; i < 8; i++) if (s_voice[i].id == SFX_COUNT) s_voice[i].id = -1;
    free(s_smp[SFX_COUNT].pcm); s_smp[SFX_COUNT].pcm = pcm; s_smp[SFX_COUNT].n = cnt; s_smp[SFX_COUNT].rate = rate;
    for (int i = 0; i < 8; i++) if (s_voice[i].id < 0) { s_voice[i].id = SFX_COUNT; s_voice[i].pos = 0; return; }
}

void sound_stop_all(void) { for (int i = 0; i < 8; i++) s_voice[i].id = -1; }

void sound_mix(int16_t *out, int frames)
{
    for (int v = 0; v < 8; v++) {
        if (s_voice[v].id < 0) continue;
        const Sample *s = &s_smp[s_voice[v].id];
        uint64_t step = ((uint64_t)s->rate << 32) / (uint32_t)s_out_rate, pos = s_voice[v].pos;
        for (int i = 0; i < frames; i++) {
            uint32_t k = (uint32_t)(pos >> 32);
            if (k >= s->n) { s_voice[v].id = -1; break; }
            int a = s->pcm[k] - 128, b = k + 1 < s->n ? s->pcm[k + 1] - 128 : a;
            int fr = (int)((pos >> 20) & 0xFFF);
            int x = (a * (4096 - fr) + b * fr) * 128 / 4096 * s_volume / 255;      /* 8-bit -> 14-bit-ish */
            int m = out[i] + x; out[i] = (int16_t)(m > 32767 ? 32767 : m < -32768 ? -32768 : m);
            pos += step;
        }
        s_voice[v].pos = pos;
    }
}
