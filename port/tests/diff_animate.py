#!/usr/bin/env python3
"""
Differential test: per-frame tile pass 0BA5:2C81 vs map_animate().
Random transient/alt/fg/bg layers, tile animation table, camera window, timer and RNG state.
Compared: all four layers byte for byte, RNG index, dust-puff pool (C), CE76.
"""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS, SEG_GAME, O, load_state, rnd_state, c_bytes, o_bytes, fx
from diff_camera import cam
from diff_video import SEG_VIDEO

class MapState(ctypes.Structure):
    _fields_ = [('dyn', ctypes.c_void_p), ('alt', ctypes.c_void_p), ('fg', ctypes.c_void_p), ('bg', ctypes.c_void_p),
                ('anim', ctypes.c_void_p), ('timer', ctypes.c_int16)]
gm = MapState.in_dll(lib, 'g_map')
SEGS = {'dyn': 0x4000, 'alt': 0x4400, 'fg': 0x4800, 'bg': 0x4C00}
PTR = {'dyn': 0x7056, 'alt': 0x705E, 'fg': 0x7062, 'bg': 0x7066}
N = 240 * 60

def run(seed):
    r = random.Random(seed)
    st = rnd_state(r)
    dens = r.choice([0.1, 0.5, 0.9])
    dyn = bytes((r.choice([1, 2, 1, 2, 3]) if r.random() < dens else 0) for _ in range(N))
    alt = bytes(r.randrange(256) for _ in range(N)); fg = bytes(r.randrange(256) for _ in range(N))
    bg = bytes(r.choice([r.randrange(0, 0xF0), r.randrange(0, 0x40), r.randrange(0xF0, 256)]) for _ in range(N))
    anim = bytearray()
    for t in range(240):
        a = max(0, t - r.randrange(0, 4)) if r.random() < .8 else r.randrange(0, 235); b = min(255, t + r.randrange(0, 4)) if r.random() < .8 else min(255, a + r.randrange(0, 6))
        anim += bytes([a, b, r.choice([0, 1, 2, 2, 3])])
        if r.random() < .3: anim[-3] = t                       # tile that rests at its first frame
    anim = bytes(anim)
    timer = r.choice([1, 1, 2, 5, 0, -1, r.randrange(-3, 40)])
    row, col, odd = r.randrange(0, 51), r.randrange(0, 220), r.randrange(2)
    lib.vga_reset(); o = O(); o.call(SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for k, seg in SEGS.items(): w(PTR[k], struct.pack('<HH', 0, seg))
    for k, data in (('dyn', dyn), ('alt', alt), ('fg', fg), ('bg', bg)): o.uc.mem_write(SEGS[k] * 16, data)
    w(0x6D6C, anim); w(0xCE76, struct.pack('<h', timer)); w(0xCE7A, struct.pack('<h', row)); w(0xCE7E, struct.pack('<h', col)); w(0xCE7C, struct.pack('<h', odd))
    o.call(SEG_GAME, 0x2C81)
    got = {k: bytes(o.uc.mem_read(SEGS[k] * 16, N)) for k in SEGS}; ob = o_bytes(o); otimer = o.rw(0xCE76)
    # ---- port
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    bufs = {k: ctypes.create_string_buffer(d, N) for k, d in (('dyn', dyn), ('alt', alt), ('fg', fg), ('bg', bg))}
    abuf = ctypes.create_string_buffer(anim, len(anim))
    for k in bufs: setattr(gm, k, ctypes.addressof(bufs[k]))
    gm.anim = ctypes.addressof(abuf); gm.timer = timer
    cam.cam_row, cam.cam_col, cam.col_odd = row, col, odd
    lib.map_animate()
    cb = c_bytes(); diffs = [k for k in ('c', 'c_ring', 'rng') if ob[k] != cb[k]]
    for k in SEGS:
        if got[k] != bytes(bufs[k].raw[:N]): diffs.append('layer_' + k)
    if (otimer & 0xFFFF) != (gm.timer & 0xFFFF): diffs.append('timer')
    if diffs: print(f"  seed {seed}: MISMATCH {diffs} timer={timer} row={row} col={col}")
    return bool(diffs)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    bad = sum(run(13000 + s) for s in range(n)); print(f"{n} scenarios, {bad} mismatching"); sys.exit(1 if bad else 0)
