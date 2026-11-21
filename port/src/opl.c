/*
 * opl.c - compact OPL2 (YM3812) emulator: 9 two-operator FM voices, 4 waveforms, ADSR envelope with key scaling, tremolo/vibrato,
 * feedback.  Melodic mode only (the game never enables rhythm mode: it writes 0xBD = 0 and plays drums on ordinary voices).
 * Written from the chip's documented behaviour (log-sine/exp tables, 1/256-octave attenuation units), not bit-exact to the real chip.
 */
#define _USE_MATH_DEFINES
#include "opl.h"
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <string.h>

enum { ATTACK, DECAY, SUSTAIN, RELEASE, OFF };
typedef struct {
    int am, vib, egt, ksr, mult, ksl, tl, ar, dr, sl, rr, ws;
    uint32_t phase; int env, state; int out, prev;
} Op;
typedef struct { int fnum, block, kon, fb, cnt; Op op[2]; } Ch;

static Ch s_ch[9]; static int s_bd, s_wse, s_nts;
static uint32_t s_timer; static uint32_t s_am_pos;
static int16_t s_logsin[256]; static uint16_t s_exp[256]; static int s_ready;
static const int s_mult[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};   /* x2 */
static const int s_ksl_rom[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};
static const int s_ksl_shift[4] = {8, 1, 2, 0};
static const uint8_t s_inc[4][8] = {{0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}};
/* operator register offset -> (channel, operator) */
static const int8_t s_slot_ch[32] = {0, 1, 2, 0, 1, 2, -1, -1, 3, 4, 5, 3, 4, 5, -1, -1, 6, 7, 8, 6, 7, 8, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static const int8_t s_slot_op[32] = {0, 0, 0, 1, 1, 1, -1, -1, 0, 0, 0, 1, 1, 1, -1, -1, 0, 0, 0, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

static void init_tables(void)
{
    for (int i = 0; i < 256; i++) {
        s_logsin[i] = (int16_t)lrint(-log2(sin((2 * i + 1) * M_PI / 1024.0)) * 256.0);
        s_exp[i] = (uint16_t)lrint(pow(2.0, -i / 256.0) * 4095.0);
    }
    s_ready = 1;
}
void opl_reset(void)
{
    if (!s_ready) init_tables();
    memset(s_ch, 0, sizeof s_ch); s_bd = s_wse = s_nts = 0; s_timer = 0; s_am_pos = 0;
    for (int c = 0; c < 9; c++) for (int o = 0; o < 2; o++) { s_ch[c].op[o].env = 511; s_ch[c].op[o].state = OFF; }
}
static void key(Ch *c, int on)
{
    if (on == c->kon) return;
    c->kon = on;
    for (int o = 0; o < 2; o++) {
        Op *p = &c->op[o];
        if (on) { p->state = ATTACK; p->phase = 0; } else if (p->state != OFF) p->state = RELEASE;
    }
}
void opl_write(int reg, int val)
{
    reg &= 0xFF; val &= 0xFF;
    if (reg == 1) { s_wse = (val >> 5) & 1; return; }
    if (reg == 8) { s_nts = (val >> 6) & 1; return; }
    if (reg == 0xBD) { s_bd = val; return; }
    if (reg >= 0xA0 && reg <= 0xA8) { Ch *c = &s_ch[reg - 0xA0]; c->fnum = (c->fnum & 0x300) | val; return; }
    if (reg >= 0xB0 && reg <= 0xB8) {
        Ch *c = &s_ch[reg - 0xB0]; c->fnum = (c->fnum & 0xFF) | ((val & 3) << 8); c->block = (val >> 2) & 7; key(c, (val >> 5) & 1); return;
    }
    if (reg >= 0xC0 && reg <= 0xC8) { Ch *c = &s_ch[reg - 0xC0]; c->fb = (val >> 1) & 7; c->cnt = val & 1; return; }
    if (reg < 0x20 || reg > 0xF5) return;
    int off = reg & 0x1F, grp = reg & 0xE0;
    if (off >= 32 || s_slot_ch[off] < 0) return;
    Op *p = &s_ch[s_slot_ch[off]].op[s_slot_op[off]];
    switch (grp) {
    case 0x20: p->am = val >> 7; p->vib = (val >> 6) & 1; p->egt = (val >> 5) & 1; p->ksr = (val >> 4) & 1; p->mult = val & 15; break;
    case 0x40: p->ksl = val >> 6; p->tl = val & 63; break;
    case 0x60: p->ar = val >> 4; p->dr = val & 15; break;
    case 0x80: p->sl = val >> 4; p->rr = val & 15; break;
    case 0xE0: p->ws = val & 3; break;
    }
}
static int op_rate(const Ch *c, const Op *p, int r)
{
    if (!r) return 0;
    int ksr = ((c->block << 1) | ((c->fnum >> (s_nts ? 8 : 9)) & 1)) >> (p->ksr ? 0 : 2);
    int rate = r * 4 + ksr; return rate > 63 ? 63 : rate;
}
static void env_step(const Ch *c, Op *p)
{
    int r;
    switch (p->state) {
    case ATTACK: r = op_rate(c, p, p->ar); break;
    case DECAY: r = op_rate(c, p, p->dr); break;
    case SUSTAIN: r = p->egt ? 0 : op_rate(c, p, p->rr); break;
    case RELEASE: r = op_rate(c, p, p->rr); break;
    default: return;
    }
    int hi = r >> 2, lo = r & 3, inc = 0;
    if (!r) return;
    if (p->state == ATTACK && hi >= 15) { p->env = 0; p->state = DECAY; return; }
    if (hi >= 12) inc = (1 << (hi - 12)) * (lo + 4) / 4; /* fast rates: several steps per sample */
    else { int sh = 12 - hi; if ((s_timer & ((1u << sh) - 1)) == 0) inc = s_inc[lo][(s_timer >> sh) & 7]; }
    if (!inc) return;
    if (p->state == ATTACK) {
        p->env += ((~p->env) * inc) >> 3; if (p->env <= 0) { p->env = 0; p->state = DECAY; }
    } else {
        p->env += inc; if (p->env > 511) p->env = 511;
        if (p->state == DECAY) { int sl = p->sl == 15 ? 0x1F0 : p->sl << 4; if (p->env >= sl) { p->env = sl; p->state = SUSTAIN; } }
        if (p->state == RELEASE && p->env >= 511) p->state = OFF;
    }
}
static int op_out(const Ch *c, Op *p, int mod, int am_level, int vib_off)
{
    int fn = c->fnum + vib_off;
    uint32_t inc = (((uint32_t)fn << c->block) * s_mult[p->mult]) >> 1;   /* 20-bit phase accumulator: 10 index bits at the top */
    p->phase = (p->phase + inc) & 0xFFFFF;
    int idx = (int)((p->phase >> 10) + mod) & 0x3FF, neg = 0, att;
    int w = s_wse ? p->ws : 0, half = idx & 0x200; neg = half != 0;
    int q = idx & 0x1FF; if (q & 0x100) q = 0x1FF - q; /* quarter-wave index 0..255 */
    switch (w) {
    case 1: if (neg) return 0; break;
    case 2: neg = 0; break;
    case 3: if (neg) return 0; if (idx & 0x100) return 0; break;
    }
    int ksl = 0;
    if (p->ksl) { ksl = (s_ksl_rom[c->fnum >> 6] << 2) - ((8 - c->block) << 5); if (ksl < 0) ksl = 0; ksl >>= s_ksl_shift[p->ksl]; }
    int env = p->env + (p->tl << 2) + ksl + (p->am ? am_level : 0); if (env > 511) env = 511;
    att = s_logsin[q] + (env << 3);
    int sh = att >> 8; if (sh >= 13) return 0;
    int v = s_exp[att & 255] >> sh;
    return neg ? -v : v;
}
int opl_sample(void)
{
    s_timer++; s_am_pos++;
    int am_p = (s_am_pos >> 8) % 210, am = am_p < 105 ? am_p : 210 - am_p;      /* triangle 0..105 -> tremolo ~3.7 Hz */
    am = (am >> 2) >> ((s_bd & 0x80) ? 0 : 2);                                  /* depth: 4.8 dB or 1 dB, in envelope units */
    int vp = (s_timer >> 10) & 7, sum = 0;
    for (int i = 0; i < 9; i++) {
        Ch *c = &s_ch[i]; Op *m = &c->op[0], *ca = &c->op[1];
        if (m->state == OFF && ca->state == OFF) continue;
        env_step(c, m); env_step(c, ca);
        int voff[2];
        for (int o = 0; o < 2; o++) {
            int range = (c->fnum >> 7) & 7;
            if (!(vp & 3)) range = 0; else if (vp & 1) range >>= 1;
            range >>= (s_bd & 0x40) ? 0 : 1; if (vp & 4) range = -range;
            voff[o] = c->op[o].vib ? range : 0;
        }
        int fbmod = c->fb ? (m->prev + m->out) >> (9 - c->fb) : 0;
        m->prev = m->out; m->out = op_out(c, m, fbmod, am, voff[0]);
        int o;
        if (c->cnt) { ca->out = op_out(c, ca, 0, am, voff[1]); o = m->out + ca->out; }
        else { ca->out = op_out(c, ca, m->out, am, voff[1]); o = ca->out; }
        sum += o;
    }
    sum *= 2; return sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum;
}
