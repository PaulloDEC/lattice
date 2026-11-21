#!/usr/bin/env python3
"""
Differential test: the tile-packing step of the level setup (0BA5:49D8..4AD8, inside 453A) vs
sprite_pack_tiles().  The fragment is run in place (no function boundary): BP/SS are set up so its
locals work, execution stops at 0BA5:4AD8.  Page 3 is filled with random pixels (never 0xFF) and a
0xFF marker pixel at the top-left of a random tile.  Compared: all VRAM + VGA state, markers, C188, C18A.
"""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS, SEG_GAME
from diff_video import Orig, snapshot, SEG_VIDEO, STACK_SEG
from diff_sprite import spr
from unicorn.x86_const import *

lib.video_get_pixel.restype = ctypes.c_int

def fill_page3(r, stop_tile):
    for p in range(4):
        buf = bytearray(r.randrange(255) for _ in range(0x10000)) if True else None
        ctypes.memmove(lib.vga_plane(p), bytes(buf), 0x10000)
    if stop_tile < 0x118:
        tx, ty = (stop_tile % 20) * 16, (stop_tile // 20) * 16
        off = 3 * 0x3F00 + ty * 80 + tx // 4
        lib.vga_plane(tx % 4)[off] = 0xFF

def run(seed):
    r = random.Random(seed)
    stop = r.choice([0, 1, r.randrange(2, 60), r.randrange(60, 0x118), 0x118, 0x200])
    cur0 = r.randrange(0, 0x1000) & ~3; mask = r.randrange(1, 16); latch = r.randrange(2)
    lib.vga_reset(); o = Orig(); o.call(SEG_VIDEO, 0x185); fill_page3(r, stop)
    o.call(SEG_VIDEO, 0xE4, (3,)); o.call(SEG_VIDEO, 0x158, (mask,))
    if latch: o.call(SEG_VIDEO, 0x16A, (1,))
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0x6458, struct.pack('<HH', 2 * 0x3F00, 0xA000)); w(0xC18A, struct.pack('<H', cur0))
    uc = o.uc; sp = 0x1000
    uc.reg_write(UC_X86_REG_SS, STACK_SEG); uc.reg_write(UC_X86_REG_SP, sp - 0x40); uc.reg_write(UC_X86_REG_BP, sp - 0x20)
    uc.reg_write(UC_X86_REG_DS, DS); uc.reg_write(UC_X86_REG_ES, DS); uc.reg_write(UC_X86_REG_CS, SEG_GAME)
    uc.emu_start(SEG_GAME * 16 + 0x49D8, SEG_GAME * 16 + 0x4AD8, count=60_000_000)
    sa = snapshot({}); oo = (o.rw(0xCE72), o.rw(0xCE70), o.rw(0xC188), o.rw(0xC18A))
    # --- port
    lib.vga_reset(); lib.video_init(); fill_page3(random.Random(seed), 0) if False else None
    r2 = random.Random(seed); r2.choice([0]); 
    lib.vga_reset(); lib.video_init()
    r3 = random.Random(seed)
    stop3 = r3.choice([0, 1, r3.randrange(2, 60), r3.randrange(60, 0x118), 0x118, 0x200]); r3.randrange(0, 0x1000); r3.randrange(1, 16); r3.randrange(2)
    fill_page3(r3, stop3)
    lib.video_set_draw_page(3); lib.video_set_map_mask(mask)
    if latch: lib.video_set_latch_mode(1)
    lib.sprite_init(); spr.cursor = cur0; lib.sprite_pack_tiles()
    sb = snapshot({}); pp = (spr.marker0, spr.marker1, spr.page_off, spr.cursor)
    diffs = [k for k in sa if sa[k] != sb[k]]
    if oo != pp: diffs.append(f'vars orig={oo} port={pp}')
    if diffs: print(f"  seed {seed}: MISMATCH {diffs} stop={stop} cur0={cur0:#x}")
    return bool(diffs)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    bad = sum(run(9000 + s) for s in range(n)); print(f"{n} scenarios, {bad} mismatching"); sys.exit(1 if bad else 0)
