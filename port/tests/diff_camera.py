#!/usr/bin/env python3
"""
Differential test: original camera routine (0BA5:29C5) vs port/src/camera.c.
Random camera/player state plus random contents of every world-anchored pool (A bursts, B scores,
C puffs, D sparkles, E shot Y).  Compared after each call: all pool memory, all camera variables.
Several calls per scenario so the easing (cam moves 1 unit per call) is exercised.
"""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, fx, O, SEG_GAME, DS, o_bytes, c_bytes, load_state, rnd_state

CAM = [('cam_row', 0xCE7A), ('col_odd', 0xCE7C), ('cam_col', 0xCE7E), ('cam_x', 0xCE80), ('vscroll', 0xCF0A),
       ('vfreeze', 0xCF0C), ('pscreen_y', 0xCF26), ('player_y', 0xCF28), ('pscreen_x', 0xCF2A),
       ('player_xcol', 0xCF2C), ('player_row', 0xCF2E), ('player_tile', 0xCF30), ('player_x', 0xCF32),
       ('vmargin', 0xCF36), ('snap', 0xCF38)]
class Cam(ctypes.Structure): _fields_ = [(n, ctypes.c_int16) for n, _ in CAM]
cam = Cam.in_dll(lib, 'g_cam')
EY = 0xCEDA

def rnd_cam(r):
    px = r.choice([r.randrange(-6, 490), r.randrange(0, 480), r.randrange(15, 40), r.randrange(440, 480)])
    row_target = r.randrange(-3, 70)
    vm = r.choice([0, 1, 2, 3, 4, 5, 6, 8, 10, r.randrange(0, 12)])
    py = (row_target * 16 + r.randrange(16)) if r.random() < .9 else r.randrange(-40, 1100)
    near = r.random() < .6
    return dict(cam_row=(max(0, min(50, row_target - vm + r.randrange(-2, 3))) if near else r.randrange(0, 51)),
                cam_x=(max(0, min(440, px - 20 + r.randrange(-3, 4))) if near else r.randrange(0, 441)),
                player_x=px, player_y=py, vmargin=vm, vfreeze=int(r.random() < .15), snap=int(r.random() < .25),
                col_odd=r.randrange(2), vscroll=r.randrange(2), cam_col=r.randrange(-5, 300), pscreen_y=r.randrange(-99, 99),
                pscreen_x=r.randrange(-99, 99), player_xcol=r.randrange(-99, 99), player_row=r.randrange(-9, 99),
                player_tile=r.randrange(-9, 300))

def run(seed, ncalls):
    r = random.Random(seed); st = rnd_state(r); st['ey'] = [r.randrange(-100, 400) for _ in range(10)]
    cs = rnd_cam(r)
    lib.vga_reset(); o = O(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    o.uc.mem_write(DS * 16 + EY, struct.pack('<10h', *st['ey']))
    for n, off in CAM: o.dsw(off, cs[n])
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for i, v in enumerate(st['ey']): fx.e_y[i] = v
    for n, _ in CAM: setattr(cam, n, cs[n])
    bad = 0
    for k in range(ncalls):
        if k and r.random() < .5:                      # player moves between calls, identically on both
            dx = r.randrange(-3, 4); dy = r.randrange(-20, 21)
            for n, d in (('player_x', dx), ('player_y', dy)):
                v = struct.unpack('<h', o.dsb(dict(CAM)[n], 2))[0] + d; o.dsw(dict(CAM)[n], v); setattr(cam, n, v)
        o.call(SEG_GAME, 0x29C5); lib.camera_update()
        ob, cb_ = o_bytes(o), c_bytes(); diffs = [f for f in ob if f != 'rng' and ob[f] != cb_[f]]
        if o.dsb(EY, 20) != bytes(ctypes.string_at(ctypes.addressof(fx) + ctypes.sizeof(fx) - 20, 20)): diffs.append('e_y')
        for n, off in CAM:
            if struct.unpack('<h', o.dsb(off, 2))[0] != getattr(cam, n): diffs.append(n)
        if diffs:
            bad += 1
            if bad <= 3: print(f"  seed {seed} call {k}: MISMATCH {diffs}  state={cs}")
    return bad

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(2000 + s, 12) for s in range(n))
    print(f"{n} scenarios x 12 calls, {bad} mismatching calls"); sys.exit(1 if bad else 0)
