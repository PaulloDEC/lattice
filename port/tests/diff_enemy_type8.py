#!/usr/bin/env python3
"""Differential test: 0BA5:0CE8 (type 8 enemy update) vs enemy_update_type8().
Blitters 249E / 2896 are recorded on both sides; the boss bar (0C5B, x87) is patched as in diff_enemy_bar."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import fx, o_bytes, c_bytes, c_calls
from diff_player_hits import pj, PJ
import diff_enemy_bar as db
from diff_video import snapshot, SEG_VIDEO, BASE_SEG
lib.enemy_update_type8.argtypes = [ctypes.c_int]
DRAWF = ctypes.CFUNCTYPE(None, *([ctypes.c_int] * 6)); c_solid = []
solid_cb = DRAWF(lambda *a: c_solid.append(tuple(v & 0xFFFF for v in a))); lib.fx_set_solid_callback(solid_cb)

class OS2(OS):
    def __init__(self):
        super().__init__(); self.solid = []
        self.uc.mem_write((0xBA5 + BASE_SEG) * 16 + 0x2896, b'\xe6\xa5\xcb')
    def wr(self, p, v):
        if p == 0xa5: self.solid.append(tuple(self.arg(i) for i in range(6)))
        else: super().wr(p, v)

def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    s = r.randrange(8); camx = r.randrange(0, 441); camrow = r.randrange(0, 51)
    pxc = r.choice([r.randrange(0, 480), 2 * camx + r.randrange(0, 80)])
    E = {n: [r.randrange(-5, 30) for _ in range(8)] for n in NAMES}
    E['sheet'] = [r.randrange(0, 10) for _ in range(8)]; E['def'] = [r.randrange(0, 6) for _ in range(8)]
    E['type'] = [8] * 8
    E['bf84'][s] = r.choice([0, 0, 0, 0, 1, 5, 20]); E['c034'][s] = r.choice([1, 1, r.randrange(-1, 6)]); E['flash'][s] = r.randrange(0, 12)
    E['c0c4'][s] = r.choice([0, 0, 0, 1, 5]); E['box_w'][s] = r.randrange(2, 40); E['box_h'][s] = r.randrange(2, 40)
    E['hp'][s] = r.randrange(-5, 600); E['hp_max'][s] = r.choice([0, r.randrange(1, 600)])
    E['x'][s] = r.choice([2 * camx + r.randrange(-10, 90), r.randrange(0, 900)])
    E['y'][s] = r.choice([camrow * 16 + r.randrange(-10, 170), r.randrange(0, 900)])
    E['vx'][s] = E['x'][s] + r.choice([-9, -6, -5, -4, 0, 4, 5, 6, 9, r.randrange(-12, 13)])    # anchor
    defs = [r.randrange(-300, 300) for _ in range(6 * 12)]
    if r.random() < .6:      # make the shooting roll rng(10)==0 succeed (next table value divisible by 10)
        tab = struct.unpack('<1000h', de.E6[:2000])
        ok = [i for i in range(1000) if tab[(i + 1) % 1000 if i + 1 <= 999 else 0] % 10 == 0]
        st['rng'] = r.choice(ok)
        E['x'][s] = max(E['x'][s], pxc + r.randrange(-1, 5))
    hdr = [[r.randrange(0, 65536) for _ in range(30)] for _ in range(10)]
    hdr[1][13] = st['sb']; hdr[3][13] = st['sc']
    P = {n: [0] * 8 for n in PJ}
    for i in range(8):
        P['type'][i] = r.choice([-1, -1, 0, 5])
        for n in PJ:
            if n != 'type': P[n][i] = r.randrange(-50, 50)
    lib.vga_reset(); o = OS2(); o.call(de.SEG_VIDEO, 0x185); db.patch(o); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for n, off in zip(NAMES, OFFS): w(off, struct.pack('<8h', *E[n]))
    for n, off in PJ.items(): w(off, struct.pack('<8h', *P[n]))
    w(0xA552, struct.pack('<%dh' % len(defs), *defs))
    w(0xCE80, struct.pack('<h', camx)); w(0xCE7A, struct.pack('<h', camrow)); w(0xCF2C, struct.pack('<h', pxc))
    for k in range(10): w(0xC1A0 + k * 220, struct.pack('<30H', *hdr[k]))
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for n in NAMES:
        for i in range(8): getattr(en, n)[i] = E[n][i]
    for n in PJ:
        for i in range(8): getattr(pj, n)[i] = P[n][i]
    ctypes.memmove(ds.defs_buf, struct.pack('<%dh' % len(defs), *defs), len(defs) * 2)
    dc.cam.cam_x = camx; dc.cam.cam_row = camrow; dc.cam.player_xcol = pxc
    for k in range(10):
        for j in range(30): ds.spr.hdr[k * 30 + j] = hdr[k][j]
    c_calls.clear(); c_solid.clear(); csnd.clear(); bad = []
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); o.call(SEG_GAME, 0xCE8, (s,)); sa = snapshot({})
    lib.vga_reset(); lib.video_init(); lib.enemy_update_type8(s); sb = snapshot({})
    bad += [k for k in sa if sa[k] != sb[k]]
    for n, off in zip(NAMES, OFFS):
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(en, n)): bad.append(n)
    for n, off in PJ.items():
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(pj, n)): bad.append('proj_' + n)
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k] and k != 'rng']
    if o.rw(0x7118) != (lib.rng_index() & 0xFFFF): bad.append('rng')
    if o.calls != list(c_calls): bad.append('draw')
    if o.solid != c_solid: bad.append('solid')
    if o.snd != csnd: bad.append('sound')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:6]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(23000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
