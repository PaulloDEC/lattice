#!/usr/bin/env python3
"""Differential test: 0BA5:0006 (player vs hazard pool) and 0BA5:00D3 (player vs enemies)."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
class Pl(ctypes.Structure): _fields_ = [('health', ctypes.c_int16), ('invuln', ctypes.c_int16), ('shield', ctypes.c_int16)]
class Pj(ctypes.Structure): _fields_ = [(n, ctypes.c_int16 * 8) for n in ('aux', 'homing', 'vy', 'speed', 'sheet', 'dir', 'h', 'w', 'y', 'x', 'type', 'last_frame', 'frame', 'jitter', 'delay')]
pl = Pl.in_dll(lib, 'g_player'); pj = Pj.in_dll(lib, 'g_proj')
PJ = dict(aux=0xBE74, homing=0xBE84, vy=0xBE94, speed=0xBEA4, sheet=0xBEB4, dir=0xBEC4, h=0xBED4, w=0xBEE4, y=0xBEF4, x=0xBF04,
          type=0xBF14, last_frame=0xBF24, frame=0xBF34, jitter=0xBF44, delay=0xBF54)
lib.player_hit_by_projectiles.argtypes = []; lib.player_hit_by_enemies.argtypes = []

def run(seed):
    r = random.Random(seed); op = 'proj' if seed % 2 else 'enemy'
    pxc = r.randrange(-20, 150); py = r.randrange(-50, 1000); dif = r.randrange(0, 4)
    P = {n: [0] * 8 for n in PJ}; E = {n: [0] * 8 for n in NAMES}
    def near(): return (pxc + r.choice([-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, r.randrange(-9, 10)]),
                        py + r.choice([-30, -20, -10, -2, 0, 5, 6, 7, 0x18, 0x19, 0x1A, 0x20, r.randrange(-40, 50)]))
    for i in range(8):
        P['type'][i] = r.choice([-1, 0, 1, 2]); P['delay'][i] = r.choice([0, 0, 0, 1])
        P['w'][i] = r.randrange(-1, 12); P['h'][i] = r.randrange(-1, 40); P['x'][i], P['y'][i] = near()
        E['type'][i] = r.choice([-1, 0, 1, 2, 8]); E['bf84'][i] = r.choice([0, 0, 0, 3])
        E['box_w'][i] = r.randrange(-1, 12); E['box_h'][i] = r.randrange(-1, 40); E['x'][i], E['y'][i] = near()
        E['hp_max'][i] = r.choice([0, 5, 0x14, 0x15, 30, 500])
    health = r.randrange(-20, 120); invuln = r.choice([0, 0, 1, 20]); shield = r.choice([0, 0, 0, 1, 400])
    st = rnd_state(r)
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for n, off in PJ.items(): w(off, struct.pack('<8h', *P[n]))
    for n, off in zip(NAMES, OFFS): w(off, struct.pack('<8h', *E[n]))
    w(0x706A, struct.pack('<h', dif)); w(0xCF2C, struct.pack('<h', pxc)); w(0xCF28, struct.pack('<h', py))
    w(0x704C, struct.pack('<h', health)); w(0xCE74, struct.pack('<h', invuln)); w(0xCE8E, struct.pack('<h', shield))
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for n in PJ:
        for i in range(8): getattr(pj, n)[i] = P[n][i]
    for n in NAMES:
        for i in range(8): getattr(en, n)[i] = E[n][i]
    ds.diff.value = dif; dc.cam.player_xcol = pxc; dc.cam.player_y = py
    pl.health = health; pl.invuln = invuln; pl.shield = shield
    csnd.clear(); bad = []
    o.call(SEG_GAME, 0x06 if op == 'proj' else 0xD3)
    (lib.player_hit_by_projectiles if op == 'proj' else lib.player_hit_by_enemies)()
    for n, off in PJ.items():
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(pj, n)): bad.append(n)
    for n, off in zip(NAMES, OFFS):
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(en, n)): bad.append(n)
    for n, off in (('health', 0x704C), ('invuln', 0xCE74), ('shield', 0xCE8E)):
        if struct.unpack('<h', o.dsb(off, 2))[0] != getattr(pl, n): bad.append(n)
    if o.snd != csnd: bad.append('sound')
    if bad: print(f'  seed {seed} {op}: MISMATCH {sorted(set(bad))[:8]}')
    return len(bad) > 0

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    bad = sum(run(13000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
