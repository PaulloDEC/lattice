#!/usr/bin/env python3
"""Differential test: 0BA5:5824..5F6A (player shot pool: move, tile hits, bursts, draw, sparkle trail) vs shots_update()."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import fx, o_bytes, c_bytes
from diff_level import lv, N
import diff_animate as da
from range_harness import run_range
from diff_video import snapshot
lib.shots_update.argtypes = []
class Sh(ctypes.Structure): _fields_ = [('x', ctypes.c_int16 * 10), ('power', ctypes.c_int16 * 10), ('count', ctypes.c_int16), ('up', ctypes.c_int16 * 10), ('dir', ctypes.c_int16 * 10), ('cursor', ctypes.c_int16)]
sh = Sh.in_dll(lib, 'g_shots')
import os; DEBUG = os.environ.get('DBG')
BLK = ctypes.c_uint8.in_dll(lib, 'g_shot_block_tile')

def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    blk = r.choice([0, 3, 7, 0x40, 0xFE]); cam_row = r.randrange(0, 45); cam_col = r.randrange(0, 200); odd = r.randrange(2); facing = r.randrange(2)
    c1ce = r.randrange(0, 65536); c1d4 = r.choice([r.randrange(0, 65536), r.randrange(0, 40)])
    tiles = [0xFF, 0xFF, blk, blk, r.randrange(256)]
    fgd = bytearray(r.choice(tiles) if r.random() < .5 else 0xFF for _ in range(N))
    xs, ys, pw, up, dr = [], [], [], [], []
    for _ in range(10):
        free = r.random() < .3
        xs.append(-99 if free else r.choice([r.randrange(-4, 90), r.randrange(0, 80), 0x4C, 0x4D, 0x4E, 0x4F, -3, -2, 0, 1, 2, 3]))
        ys.append(r.choice([r.randrange(-5, 200), r.randrange(0, 192), 0, 15, 16, 17]))
        pw.append(r.randrange(2)); up.append(r.choice([0, 0, 1])); dr.append(r.randrange(2))
    cnt = sum(1 for v in xs if v != -99) + r.choice([0, 0, 0, 2])
    # make the cells around live shots sometimes blocking so every branch is hit
    for i in range(10):
        if xs[i] == -99 or r.random() < .35: continue
        d = ((ys[i] // 16 if ys[i] >= 0 else -((-ys[i]) // 16)) + cam_row) * 240 + (xs[i] // 4 if xs[i] >= 0 else -((-xs[i]) // 4)) + cam_col
        for a in (-0xF1, -0xF0, -0xEF, -2, -1, 0, 1, 2, 0xEE, 0xEF, 0xF0, 0xF1, 0xF2, 0x1E0):
            for f in (0, -1, 1):
                if r.random() < .25 and 0 <= d + a + f < N: fgd[d + a + f] = r.choice([blk, 0xFF, 0xFF, 5])
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for k, seg in da.SEGS.items(): w(da.PTR[k], struct.pack('<HH', 0, seg))
    o.uc.mem_write(da.SEGS['fg'] * 16, bytes(fgd))
    w(0x6D6B, bytes([blk])); w(0xCE7A, struct.pack('<h', cam_row)); w(0xCE7E, struct.pack('<h', cam_col)); w(0xCE7C, struct.pack('<h', odd)); w(0xCE90, struct.pack('<h', facing))
    w(0xC1CE, struct.pack('<H', c1ce)); w(0xC1D4, struct.pack('<H', c1d4))
    w(0xCEEE, struct.pack('<10h', *xs)); w(0xCEDA, struct.pack('<10h', *ys)); w(0xCE9A, struct.pack('<10h', *pw)); w(0xCEC6, struct.pack('<10h', *up))
    w(0xCEB2, struct.pack('<10h', *dr)); w(0xCEAE, struct.pack('<h', cnt))
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    o.calls.clear(); run_range(o, 0x5824, 0x5F6A, mode=0, pause=0); sa = snapshot({}); ocalls = list(o.calls)
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    buf = ctypes.create_string_buffer(bytes(fgd), N)
    da.gm.fg = ctypes.addressof(buf)
    BLK.value = blk; dc.cam.cam_row = cam_row; dc.cam.cam_col = cam_col; dc.cam.col_odd = odd; lv.facing = facing
    ds.spr.hdr[23] = c1ce; ds.spr.hdr[26] = c1d4
    for i in range(10): sh.x[i] = xs[i]; de.fx.e_y[i] = ys[i]; sh.power[i] = pw[i]; sh.up[i] = up[i]; sh.dir[i] = dr[i]
    sh.count = cnt
    de.c_calls.clear(); lib.shots_update(); sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    if struct.unpack('<10h', o.dsb(0xCEEE, 20)) != tuple(sh.x): bad.append('x')
    if struct.unpack('<10h', o.dsb(0xCEDA, 20)) != tuple(de.fx.e_y): bad.append('y')
    if o.rw(0xCEAE) != (sh.count & 0xFFFF): bad.append('count')
    if bytes(o.uc.mem_read(da.SEGS['fg'] * 16, N)) != buf.raw[:N]: bad.append('fg')
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k]]
    cc = [tuple(v & 0xFFFF for v in c) for c in de.c_calls]; oc = [tuple(v & 0xFFFF for v in c) for c in ocalls]
    if oc != cc: bad.append('draws')
    if bad:
        print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]}')
        if DEBUG: print('   in x', xs, 'y', ys, 'up', up, 'dir', dr, 'pw', pw, 'cnt', cnt, 'cam', cam_row, cam_col, odd, facing); print('   orig x', struct.unpack('<10h', o.dsb(0xCEEE, 20)), 'port x', tuple(sh.x), 'orig cnt', o.rw(0xCEAE), 'port', sh.count)
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(61000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
