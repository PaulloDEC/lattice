#!/usr/bin/env python3
"""Differential test: 0BA5:0922 (spawn enemy projectile) vs projectile_spawn()."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
from diff_player_hits import pj, PJ
lib.projectile_spawn.argtypes = [ctypes.c_int] * 6; lib.projectile_spawn.restype = ctypes.c_int

def run(seed):
    r = random.Random(seed)
    defs = [r.randrange(-300, 300) for _ in range(6 * 12)]
    hdr = [[r.randrange(0, 65536) for _ in range(30)] for _ in range(10)]
    P = {n: [0] * 8 for n in PJ}
    for i in range(8):
        P['type'][i] = r.choice([-1, -1, 0, 5, -2])
        for n in PJ:
            if n != 'type': P[n][i] = r.randrange(-100, 100)
    if r.random() < .1: P['type'] = [0] * 8
    pxc = r.randrange(-20, 150)
    calls = [(r.choice([pxc, pxc - 1, pxc + 1, r.randrange(-30, 200)]), r.randrange(-30, 200), r.choice([0, 1, 5]),
              r.randrange(0, 30), r.randrange(0, 6), r.randrange(0, 10)) for _ in range(r.randrange(1, 5))]
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0xA552, struct.pack('<%dh' % len(defs), *defs)); w(0xCF2C, struct.pack('<h', pxc))
    for n, off in PJ.items(): w(off, struct.pack('<8h', *P[n]))
    for s in range(10): w(0xC1A0 + s * 220, struct.pack('<30H', *hdr[s]))
    lib.vga_reset(); lib.video_init()
    ctypes.memmove(ds.defs_buf, struct.pack('<%dh' % len(defs), *defs), len(defs) * 2)
    dc.cam.player_xcol = pxc
    for n in PJ:
        for i in range(8): getattr(pj, n)[i] = P[n][i]
    for s in range(10):
        for k in range(30): ds.spr.hdr[s * 30 + k] = hdr[s][k]
    csnd.clear(); bad = []
    for ci, a in enumerate(calls):
        ra = o.call(SEG_GAME, 0x922, a) & 0xFFFF; rb = lib.projectile_spawn(*a) & 0xFFFF
        if ra != rb: bad.append(f'ret{ci}')
        for n, off in PJ.items():
            if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(pj, n)): bad.append(f'{n}@{ci}')
    if o.snd != csnd: bad.append('sound')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:6]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    bad = sum(run(19000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
