#!/usr/bin/env python3
"""
Differential test: original LoadIMG (05D8:0A70) and LoadPalette (05D8:0AFF) vs video.c.
Inputs are the real IMG and palette entries of HOCUS.DAT.  File I/O is stubbed as in
diff_pcx.py, plus two more stubs: 1392:0800 (offset-only FAT lookup) and 068C:00F9
(read a whole entry into a far buffer).
"""
import sys, struct, random, ctypes
sys.path.insert(0, '/home/claude/hocus'); sys.path.insert(0, '/home/claude/hocus/port/tests')
import diff_pcx as dp
from diff_pcx import OrigIO, H, lib, snapshot, compare, orig_state, c_state, SEG_VIDEO, DS, BASE_SEG
from unicorn.x86_const import *

lib.video_load_img.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_int]
lib.video_load_palette.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_int]
lib.video_img_width4.restype = ctypes.c_int; lib.video_img_height.restype = ctypes.c_int

class OrigIO2(OrigIO):
    def __init__(self, entries):
        super().__init__(entries)
        mem = self.uc.mem_write
        mem((0x1392 + BASE_SEG) * 16 + 0x800, b'\xe6\x9e\xcb')   # GetEntryOffset(index, far &off)
        mem((0x68c + BASE_SEG) * 16 + 0xf9,  b'\xe6\x9f\xcb')    # ReadEntry(index, far dest)
    def wr(self, p, v):
        if p == 0x9e:
            idx = self.arg(0); self.cur = self.entries[idx]
            self.uc.mem_write(self.lin(self.arg(2), self.arg(1)), struct.pack('<I', 0))
        elif p == 0x9f:
            idx = self.arg(0); data = self.entries[idx]
            self.uc.mem_write(self.lin(self.arg(2), self.arg(1)), data)
        else: super().wr(p, v)

def pre(o_or_none, latch, use_c, page):
    pass

def run_img(entries, idx, x, y, page, latch, use_c):
    if use_c:
        lib.vga_reset(); lib.video_init(); lib.video_set_draw_page(page)
        if latch: lib.video_set_latch_mode(1); lib.video_set_map_mask(0x05)
        lib.video_load_img(entries[idx], len(entries[idx]), x, y)
        return snapshot({}), (lib.video_img_width4(), lib.video_img_height())
    o = OrigIO2(entries)
    lib.vga_reset(); o.call(SEG_VIDEO, 0x185); o.call(SEG_VIDEO, 0xE4, (page,))
    if latch: o.call(SEG_VIDEO, 0x16A, (1,)); o.call(SEG_VIDEO, 0x158, (0x05,))
    o.call(SEG_VIDEO, 0xA70, (idx, x, y))
    return snapshot({}), (o.rw(0x6328), o.rw(0x632a))

def run_pal(entries, idx, slot, apply, tgt0, use_c):
    if use_c:
        lib.vga_reset(); lib.video_init(); ctypes.memmove(lib.video_target_palette(), tgt0, 768)
        lib.video_load_palette(entries[idx], len(entries[idx]), slot, apply)
        return snapshot({}), c_state()[0]
    o = OrigIO2(entries)
    lib.vga_reset(); o.call(SEG_VIDEO, 0x185); o.uc.mem_write(DS * 16 + 0x6a64, tgt0)
    o.call(SEG_VIDEO, 0xAFF, (idx, slot, apply))
    return snapshot({}), orig_state(o)[0]

if __name__ == '__main__':
    bad = n = 0
    img_ids = [i for i in range(652) if H.is_img(i)]
    ents = {i: H.get(i) for i in img_ids}
    rng = random.Random(21)
    for rep in range(3):
        for i in img_ids:
            w4, h = struct.unpack('<HH', ents[i][:4])
            x = rng.randrange(0, max(1, 80 - w4 + 1)) if w4 <= 80 else 0
            y = rng.randrange(0, max(1, 200 - h + 1)) if h <= 200 else 0
            page = rng.randrange(4); latch = rng.randrange(2)
            a, ea = run_img(ents, i, x, y, page, latch, False); b, eb = run_img(ents, i, x, y, page, latch, True)
            bad += compare(f"IMG entry {i} ({w4*4}x{h}) at x={x} y={y} page={page} latch={latch}", a, b, ea, eb); n += 1
    print(f"IMG: {n} loads of {len(img_ids)} real entries, {bad} mismatches")
    # palette entries: the two game halves + the 16 backdrop palettes
    pal_ids = [7, 8] + list(range(73, 89)); pents = {i: H.get(i) for i in pal_ids}
    pb = 0; pn = 0
    for rep in range(2):
        for i in pal_ids:
            slot = rng.choice([0, 128]); apply = rng.randrange(2)
            tgt0 = bytes(rng.randrange(64) for _ in range(768))
            a, ta = run_pal(pents, i, slot, apply, tgt0, False); b, tb = run_pal(pents, i, slot, apply, tgt0, True)
            pb += compare(f"palette entry {i} slot={slot} apply={apply}", a, b, ta, tb); pn += 1
    print(f"palette: {pn} loads, {pb} mismatches")
    bad += pb; n += pn
    print(f"TOTAL: {n} scenarios, {bad} mismatches"); sys.exit(1 if bad else 0)
