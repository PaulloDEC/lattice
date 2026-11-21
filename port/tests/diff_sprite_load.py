#!/usr/bin/env python3
"""
Differential test: original sprite-sheet loader (0BA5:2242) vs sprite_load_sheets() in sprite.c,
using the real DAT entry 130.  File I/O is stubbed: 68C:64 (select entry), seg0:3E39 (fseek) and
seg0:3D29 (fread) become callbacks into Python; everything else, including 05D8 put_pixel, runs as
original machine code.  Compared: VRAM+VGA state, frame/pixel tables, base table, cursor, streams.
"""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS, H
import diff_pcx as dp
from diff_pcx import OrigIO
from diff_video import snapshot, SEG_VIDEO, BASE_SEG
from diff_sprite import spr, Spr

SEG_GAME = de.SEG_GAME
E130 = H.get(0x82)
lib.sprite_load_sheets.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.POINTER(ctypes.c_int16)]
lib.sprite_stream_buf.restype = ctypes.POINTER(ctypes.c_uint8)
TMP_SEG, STR_SEG = 0x5000, 0x6000

class OrigLoad(OrigIO):
    def __init__(self):
        super().__init__({})
        m = self.uc.mem_write
        p = lambda seg, off, code: m((seg + BASE_SEG) * 16 + off, code)
        p(0x68c, 0x64, b'\xe6\xa0\xcb')     # select entry (arg0) -> pos = 0
        p(0, 0x3e39, b'\xe6\xa1\xcb')        # fseek(fp, long off, whence)
        p(0, 0x3d29, b'\xe6\xa2\xcb')        # fread(buf, size, n, fp)
        self.pos = 0; self.sel = None
    def wr(self, p, v):
        if p == 0xa0: self.sel = self.arg(0); self.pos = 0; assert self.sel == 0x82
        elif p == 0xa1:
            off = self.arg(2) | (self.arg(3) << 16); wh = self.arg(4); assert wh == 1
            self.pos += off
        elif p == 0xa2:
            size, n = self.arg(2), self.arg(3); buf = self.lin(self.arg(1), self.arg(0))
            self.uc.mem_write(buf, E130[self.pos:self.pos + size * n]); self.pos += size * n
        else: super().wr(p, v)

def run(seed):
    r = random.Random(seed)
    k = r.randrange(1, 11); ids = r.sample(range(40), k) + [-1] * (10 - k); r.shuffle(ids)
    page_off = r.choice([0, 0, r.randrange(0, 0x800)]); cursor = r.choice([0, 0, r.randrange(0, 0x3000) & ~3])
    ids = (ctypes.c_int16 * 10)(*ids)
    lib.vga_reset(); o = OrigLoad(); o.call(SEG_VIDEO, 0x185)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0xC174, struct.pack('<10h', *ids)); w(0xC188, struct.pack('<HH', page_off, cursor))
    w(0x48C6, struct.pack('<HH', 0, TMP_SEG))
    for s in range(10): w(0x7070 + s * 4, struct.pack('<HH', 0, STR_SEG + s * 0x400))
    o.call(SEG_GAME, 0x2242)
    sa = snapshot({})
    tab_o = o.dsb(0xC1DC, 0) if False else bytes(o.uc.mem_read(DS * 16 + 0xC1DC, 0))
    tabs_o = [bytes(o.uc.mem_read(DS * 16 + 0xC1DC + s * 220, 160)) for s in range(10)]
    draw_o = o.rw(0x6460); base_o = bytes(o.uc.mem_read(DS * 16 + 0xC18C, 20)); cur_o = o.rw(0xC18A)
    streams_o = [bytes(o.uc.mem_read((STR_SEG + s * 0x400) * 16, 16384)) for s in range(10)]
    # --- port
    lib.vga_reset(); lib.video_init(); spr.page_off = page_off; spr.cursor = cursor
    rc = lib.sprite_load_sheets(E130, len(E130), ids)
    sb = snapshot({})
    diffs = [x for x in sa if sa[x] != sb[x]]
    if rc: diffs.append('rc')
    lib.video_draw_offset.restype = ctypes.c_uint16
    if lib.video_draw_offset() != draw_o: diffs.append('draw_off')
    for s in range(10):
        if ids[s] == -1: continue
        if tabs_o[s] != struct.pack('<80H', *[spr.tab[s * 110 + j] for j in range(80)]): diffs.append(f'tab{s}')
        if struct.unpack_from('<H', base_o, s * 2)[0] != spr.base[s]: diffs.append(f'base{s}')
        ss = struct.unpack_from('<H', E130, ids[s] * 220 + 0x38)[0]
        if streams_o[s][:ss] != bytes(lib.sprite_stream_buf(s)[:ss]): diffs.append(f'stream{s}')
    if cur_o != spr.cursor: diffs.append("cursor"); print("   cursor orig", cur_o, "port", spr.cursor, "sum", sum(struct.unpack_from("<H", E130, i*220+0x3a)[0] for i in ids if i != -1) + cursor)
    if diffs: print(f"  seed {seed}: MISMATCH {diffs} ids={list(ids)}")
    return bool(diffs)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    bad = sum(run(8000 + s) for s in range(n)); print(f"{n} scenarios, {bad} mismatching"); sys.exit(1 if bad else 0)
