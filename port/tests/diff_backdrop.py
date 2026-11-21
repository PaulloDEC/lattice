#!/usr/bin/env python3
"""
Differential test: parallax backdrop 124A:0006 (even) and 124A:02AA (odd) vs map.c backdrop_draw_*.
The original reads cam_x/row/col from DS:CE80/CE7A/CE7E and its slow path uses the globals
DS:645C/6460 (equal to the arguments, as in the game).
"""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS
from diff_video import Orig, snapshot, SEG_VIDEO, BASE_SEG
from diff_sprite import fill_vram
from diff_map import SEG_MAP, L1_SEG, L2_SEG, MAPSZ

for f in (lib.backdrop_draw_even, lib.backdrop_draw_odd):
    f.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint16, ctypes.c_int]

def run(seed, odd):
    r = random.Random(seed)
    dens = r.choice([0.2, 0.6, 0.95])
    mk = lambda: bytes((0xFF if r.random() < dens else r.randrange(0, 0xFE)) for _ in range(MAPSZ))
    l1, l2 = mk(), mk()
    page = r.randrange(4); mask = r.choice([0xF, 0xF, r.randrange(1, 16)]); latch = r.choice([1, 1, 0])
    row, col = r.randrange(0, 51), r.randrange(0, 221)
    cam_x = r.choice([r.randrange(0, 441), 0x4C + 80 * r.randrange(0, 5) + r.randrange(0, 4), 80 * r.randrange(0, 5), r.randrange(0, 441)])
    map_off = row * 240 + col
    dest = page * 0x3F00; src = 3 * 0x3F00 + 0x7D0 if r.random() < .5 else r.randrange(0, 0x4000)
    lib.vga_reset(); o = Orig(); o.call(SEG_VIDEO, 0x185); fill_vram(seed)
    o.call(SEG_VIDEO, 0xE4, (page,)); o.call(SEG_VIDEO, 0x158, (mask,)); o.call(SEG_VIDEO, 0x16A, (latch,))
    o.uc.mem_write(L1_SEG * 16, l1); o.uc.mem_write(L2_SEG * 16, l2)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0xCE7A, struct.pack('<h', row)); w(0xCE7E, struct.pack('<h', col)); w(0xCE80, struct.pack('<h', cam_x))
    w(0x645C, struct.pack('<HH', src, 0xA000)); w(0x7062, struct.pack('<HH', 0, L1_SEG)); w(0x7066, struct.pack('<HH', 0, L2_SEG))
    o.call(SEG_MAP, 0x2AA if odd else 0x006, (src, 0xA000, dest, 0xA000))
    sa = snapshot({})
    lib.vga_reset(); lib.video_init(); fill_vram(seed)
    lib.video_set_draw_page(page); lib.video_set_map_mask(mask); lib.video_set_latch_mode(latch)
    (lib.backdrop_draw_odd if odd else lib.backdrop_draw_even)(dest, src, l1, l2, map_off, cam_x)
    sb = snapshot({})
    diffs = [k for k in sa if sa[k] != sb[k]]
    if diffs: print(f"  seed {seed} odd={odd}: MISMATCH {diffs} cam_x={cam_x} x0={cam_x % 80}")
    return bool(diffs)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 16
    bad = sum(run(12000 + s, s % 2) for s in range(n)); print(f"{n} scenarios, {bad} mismatching"); sys.exit(1 if bad else 0)
