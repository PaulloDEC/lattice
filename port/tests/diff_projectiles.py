#!/usr/bin/env python3
"""Differential test: 0BA5:0A81 (enemy projectile move/draw/wall-hit) vs projectiles_update().
The sprite blitter 249E is recorded on both sides; burst pools, RNG, tile layer 7062 compared."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import fx, o_bytes, c_bytes, c_calls
from diff_player_hits import pj, PJ
import diff_animate as da
lib.projectiles_update.argtypes = []
FG_SEG = 0x5800
fg_buf = (ctypes.c_uint8 * 14400)(); da.gm.fg = ctypes.addressof(fg_buf)

def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    camx = r.randrange(0, 441); camrow = r.randrange(0, 51); pxc = r.choice([r.randrange(0, 480), 2 * camx + r.randrange(0, 80)])
    P = {n: [0] * 8 for n in PJ}
    fg = [0xFF if r.random() < .9 else r.randrange(0, 255) for _ in range(14400)]
    for i in range(8):
        P['type'][i] = r.choice([-1, 0, 1, 2]); P['delay'][i] = r.choice([0, 0, 0, 1, 3])
        P['homing'][i] = r.choice([0, 1]); P['vy'][i] = r.randrange(-6, 7); P['speed'][i] = r.randrange(-2, 6)
        P['sheet'][i] = r.randrange(0, 10); P['dir'][i] = r.choice([0, 1]); P['last_frame'][i] = r.randrange(0, 6)
        P['frame'][i] = r.randrange(0, 7); P['jitter'][i] = r.choice([0, 0, 1])
        P['x'][i] = r.choice([2 * camx + r.randrange(-14, 90), 2 * camx + r.choice([-11, -10, 80, 81, 0x50, 0x51]), r.randrange(4, 950)])
        P['y'][i] = r.choice([camrow * 16 + r.randrange(-24, 170), camrow * 16 + r.choice([-0x15, -0x14, 0xA0, 0xA1]), r.randrange(0, 950)])
        if P['homing'][i] and r.random() < .5:      # land exactly on / next to the player's column after the move
            mv = P['speed'][i] if P['dir'][i] == 0 else -P['speed'][i]
            P['x'][i] = pxc - mv + r.choice([-1, 0, 0, 1])
        P['x'][i] = max(0, min(955, P['x'][i])); P['y'][i] = max(0, min(955, P['y'][i]))
    hdr13 = [r.randrange(0, 65536) for _ in range(10)]
    hdr13[1] = st['sb']; hdr13[3] = st['sc']        # DS:C296 / C44E are these very words
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for n, off in PJ.items(): w(off, struct.pack('<8h', *P[n]))
    w(0xCE80, struct.pack('<h', camx)); w(0xCE7A, struct.pack('<h', camrow)); w(0xCF2C, struct.pack('<h', pxc))
    w(0x7062, struct.pack('<HH', 0, FG_SEG + BASE_SEG)); o.uc.mem_write((FG_SEG + BASE_SEG) * 16, bytes(fg))
    for s in range(10): w(0xC1A0 + s * 220 + 26, struct.pack('<H', hdr13[s]))
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for n in PJ:
        for i in range(8): getattr(pj, n)[i] = P[n][i]
    dc.cam.cam_x = camx; dc.cam.cam_row = camrow; dc.cam.player_xcol = pxc
    for i in range(14400): fg_buf[i] = fg[i]
    for s in range(10): ds.spr.hdr[s * 30 + 13] = hdr13[s]
    c_calls.clear(); bad = []
    o.call(SEG_GAME, 0xA81); lib.projectiles_update()
    for n, off in PJ.items():
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(pj, n)): bad.append(n)
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k] and k != 'rng']
    if o.rw(0x7118) != (lib.rng_index() & 0xFFFF): bad.append('rng')
    if o.calls != list(c_calls): bad.append(f'draw-calls {o.calls[:2]} vs {list(c_calls)[:2]}')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:6]}')
    return len(bad) > 0

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(15000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
