#!/usr/bin/env python3
"""
Differential test: map renderer 124A:0F15 (even) and 124A:0FE5 (odd) vs map.c.
Random two-layer maps (0xFF = empty), random VRAM (tile page + destination), random latch/mask
preconditions as set by the caller.  Compared: every VRAM byte and VGA register.
"""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS
from diff_video import Orig, snapshot, SEG_VIDEO, BASE_SEG
from diff_sprite import fill_vram

SEG_MAP = 0x124a + BASE_SEG
L1_SEG, L2_SEG = 0x5000, 0x6000
MAPSZ = 240 * 64
lib.map_draw_even.argtypes = lib.map_draw_odd.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint16]

def run(seed, odd):
    r = random.Random(seed)
    dens = r.choice([0.2, 0.5, 0.9])
    mk = lambda: bytes((0xFF if r.random() < dens else r.randrange(0, 0xFE)) for _ in range(MAPSZ))
    l1, l2 = mk(), mk()
    page = r.randrange(4); mask = r.choice([0xF, 0xF, r.randrange(1, 16)]); latch = r.choice([1, 1, 0])
    map_off = r.randrange(0, 240 * 50 + 220)
    dest = page * 0x3F00; src = 2 * 0x3F00
    lib.vga_reset(); o = Orig(); o.call(SEG_VIDEO, 0x185); fill_vram(seed)
    o.call(SEG_VIDEO, 0xE4, (page,)); o.call(SEG_VIDEO, 0x158, (mask,)); o.call(SEG_VIDEO, 0x16A, (latch,))
    o.uc.mem_write(L1_SEG * 16, l1); o.uc.mem_write(L2_SEG * 16, l2)
    o.call(SEG_MAP, 0xFE5 if odd else 0xF15, (dest, 0xA000, src, 0xA000, 0, L1_SEG, 0, L2_SEG, map_off))
    sa = snapshot({})
    lib.vga_reset(); lib.video_init(); fill_vram(seed)
    lib.video_set_draw_page(page); lib.video_set_map_mask(mask); lib.video_set_latch_mode(latch)
    (lib.map_draw_odd if odd else lib.map_draw_even)(dest, src, l1, l2, map_off)
    sb = snapshot({})
    diffs = [k for k in sa if sa[k] != sb[k]]
    if diffs: print(f"  seed {seed} odd={odd}: MISMATCH {diffs} page={page} off={map_off}")
    return bool(diffs)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    bad = sum(run(11000 + s, s % 2) for s in range(n)); print(f"{n} scenarios, {bad} mismatching"); sys.exit(1 if bad else 0)
