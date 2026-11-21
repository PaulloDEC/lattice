#!/usr/bin/env python3
"""Differential test: 0BA5:0C5B (boss health bar) vs enemy_draw_bar().
The original uses Borland x87 emulation stubs (INT 34h..3Bh/3Dh) which are patched into the real x87
opcodes (as Borland's startup code does when an FPU is present) so Unicorn runs them in 80-bit."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
from diff_video import snapshot, SEG_VIDEO, BASE_SEG
from math import gcd
lib.enemy_draw_bar.argtypes = [ctypes.c_int]; lib.video_set_draw_page.argtypes = [ctypes.c_int]

def patch(o):
    def p(seg, off, code): o.uc.mem_write((seg + BASE_SEG) * 16 + off, code)
    for off, b in ((0xC7E, 0xDF), (0xC8D, 0xDF), (0xC91, 0xDE), (0xC94, 0xD8)): p(0xBA5, off, bytes([0x9B, b]))
    p(0x7000 - BASE_SEG, 0, b'\x9b\xdb\xe3\xcb'); o.call(0x7000, 0, ())      # FNINIT, as the C runtime startup does
    p(0, 0x113F, b'\x9b\xd9'); p(0, 0x1143, b'\x90\x9b'); p(0, 0x114C, b'\x9b\xd9'); p(0, 0x1150, b'\x9b\xdf'); p(0, 0x1157, b'\x9b\xd9')

def run(seed):
    r = random.Random(seed)
    hpmax = r.choice([r.randrange(21, 600), r.randrange(21, 100), 0, 30000, 78, 39, 26, 21, 300])
    mode = r.randrange(5)
    if mode == 0 and hpmax: hp = r.randrange(0, hpmax + 1)
    elif mode == 1 and hpmax:                       # exact multiples: 78*hp divisible by hpmax
        g = hpmax // gcd(hpmax, 78); hp = g * r.randrange(0, max(1, hpmax // g) + 1)
    elif mode == 2: hp = r.randrange(-20, 5)
    elif mode == 3 and hpmax: hp = r.randrange(0, hpmax * 3)
    else: hp = r.randrange(-32768, 32768)
    hp = max(-32768, min(32767, hp)); page = r.randrange(4); slot = r.randrange(8)
    latch = r.randrange(2); mask = r.randrange(1, 16)
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); patch(o); o.call(SEG_VIDEO, 0xE4, (page,))
    if latch: o.call(SEG_VIDEO, 0x16A, (1,)); o.call(SEG_VIDEO, 0x158, (mask,))
    o.uc.mem_write(DS * 16 + 0xC024 + 2 * slot, struct.pack('<h', hp)); o.uc.mem_write(DS * 16 + 0xBF74 + 2 * slot, struct.pack('<h', hpmax))
    o.call(SEG_GAME, 0xC5B, (slot,)); a = snapshot({})
    lib.vga_reset(); lib.video_init(); lib.video_set_draw_page(page)
    if latch: lib.video_set_latch_mode(1); lib.video_set_map_mask(mask)
    en.hp[slot] = hp; en.hp_max[slot] = hpmax
    lib.enemy_draw_bar(slot); b = snapshot({})
    bad = [k for k in a if a[k] != b[k]]
    if bad: print(f'  seed {seed}: hp={hp} hpmax={hpmax} MISMATCH {bad[:4]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    bad = sum(run(17000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
