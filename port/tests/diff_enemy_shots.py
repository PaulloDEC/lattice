#!/usr/bin/env python3
"""Differential test: 0BA5:0267 (shots vs enemies), 01B6 (kill all), 0439 (despawn off-screen)."""
import sys, struct, random, ctypes
import diff_enemy_pending as dp
import diff_enemy_spawn as ds
from diff_enemy_pending import *
from diff_enemy_spawn import *
import diff_camera as dc
from diff_effects import fx, o_bytes, c_bytes
for f in ('enemy_shots_collide', 'enemy_kill_all', 'enemy_despawn_offscreen'): getattr(lib, f).argtypes = []
class Shots(ctypes.Structure): _fields_ = [('x', ctypes.c_int16 * 10), ('power', ctypes.c_int16 * 10), ('count', ctypes.c_int16)]
shots = Shots.in_dll(lib, 'g_shots'); kills = ctypes.c_int16.in_dll(lib, 'g_kills')
OPS = {'collide': 0x267, 'killall': 0x1B6, 'despawn': 0x439}

def run(seed):
    r = random.Random(seed); op = r.choice(list(OPS)) if seed % 3 else 'collide'
    st = rnd_state(r); camx = r.randrange(0, 441); camrow = r.randrange(0, 51)
    E = {n: [0] * 8 for n in NAMES}
    for s in range(8):
        E['type'][s] = r.choice([-1, 0, 1, 2, 3, 4, 5, 6, 7, 8]) if r.random() < .8 else -1
        E['bf84'][s] = 0 if r.random() < .8 else r.randrange(1, 20)
        E['box_w'][s] = r.choice([r.randrange(2, 40), r.randrange(-3, 41)]); E['box_h'][s] = r.choice([r.randrange(2, 40), r.randrange(-3, 41)])
        E['x'][s] = r.choice([2 * camx + r.randrange(-70, 150), 2 * camx + r.choice([-0x33, -0x32, -0x31, 0x77, 0x78, 0x79]), r.randrange(-50, 1000)])
        E['y'][s] = r.choice([camrow * 16 + r.randrange(-140, 560), camrow * 16 + r.choice([-0x79, -0x78, -0x77, 0x1FD, 0x1FE, 0x1FF]), r.randrange(-50, 1000)])
        E['hp'][s] = r.choice([-2, -1, 0, 1, 2, 3, 5]); E['hp_max'][s] = r.choice([0, 3, 499, 500, 501, 900])
        E['cell'][s] = r.randrange(0, 14400); E['flash'][s] = r.randrange(0, 12)
    sx = []; sy = []; pw = []
    for d in range(10):
        pw.append(r.choice([0, 0, 0, 1, 3]))
        if r.random() < .25: sx.append(-99); sy.append(r.randrange(-20, 220)); continue
        e = r.randrange(8); x0 = E['x'][e] - 2 * camx; y0 = E['y'][e] - camrow * 16
        sx.append(x0 + r.randrange(-2, max(E['box_w'][e], 1) + 2)); sy.append(y0 + r.randrange(-4, max(E['box_h'][e], 1) + 1))
    cnt = sum(1 for v in sx if v != -99)
    kl = r.randrange(0, 50); info = [r.choice([0x7530, 106 + r.randrange(6), 0]) for _ in range(14400)]
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for n, off in zip(NAMES, OFFS): w(off, struct.pack('<8h', *E[n]))
    w(0xCEEE, struct.pack('<10h', *sx)); w(0xCEDA, struct.pack('<10h', *sy)); w(0xCE9A, struct.pack('<10h', *pw))
    w(0xCEAE, struct.pack('<h', cnt)); w(0xCE6C, struct.pack('<h', kl)); w(0xCE80, struct.pack('<h', camx)); w(0xCE7A, struct.pack('<h', camrow))
    w(0x705A, struct.pack('<HH', 0, INFO_SEG + BASE_SEG))
    o.uc.mem_write((INFO_SEG + BASE_SEG) * 16, struct.pack('<14400h', *info))
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for n in NAMES:
        for s in range(8): getattr(en, n)[s] = E[n][s]
    for d in range(10): shots.x[d] = sx[d]; shots.power[d] = pw[d]; fx.e_y[d] = sy[d]
    shots.count = cnt; kills.value = kl; dc.cam.cam_x = camx; dc.cam.cam_row = camrow
    for i in range(14400): info_buf[i] = info[i]
    csnd.clear(); bad = []
    o.call(SEG_GAME, OPS[op]); getattr(lib, {'collide': 'enemy_shots_collide', 'killall': 'enemy_kill_all', 'despawn': 'enemy_despawn_offscreen'}[op])()
    for n, off in zip(NAMES, OFFS):
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(en, n)): bad.append(n)
    if struct.unpack('<10h', o.dsb(0xCEEE, 20)) != tuple(shots.x): bad.append('shot_x')
    if struct.unpack('<10h', o.dsb(0xCEDA, 20)) != tuple(fx.e_y): bad.append('shot_y')
    if struct.unpack('<h', o.dsb(0xCEAE, 2))[0] != shots.count: bad.append('count')
    if struct.unpack('<h', o.dsb(0xCE6C, 2))[0] != kills.value: bad.append('kills')
    if bytes(o.uc.mem_read((INFO_SEG + BASE_SEG) * 16, 28800)) != struct.pack('<14400h', *info_buf): bad.append('info')
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k] and k != 'rng']
    if o.rw(0x7118) != (lib.rng_index() & 0xFFFF): bad.append('rng')
    if o.snd != csnd: bad.append('sound')
    if bad and '--dbg' in sys.argv: print('E',{n:E[n] for n in ('type','bf84','box_w','box_h','x','y','hp','hp_max')},'cam',camx,camrow,'shots',sx,sy,pw,'snd',o.snd,csnd,'hp_after',list(en.hp),struct.unpack('<8h',o.dsb(0xC024,16)))
    if bad: print(f'  seed {seed} {op}: MISMATCH {sorted(set(bad))[:8]}')
    return len(bad) > 0

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    bad = sum(run(11000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
