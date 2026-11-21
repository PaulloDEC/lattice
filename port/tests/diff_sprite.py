#!/usr/bin/env python3
"""
Differential test: original sprite blitter (0BA5:249E) vs port/src/sprite.c.
Random opcode streams, frame tables and VRAM contents; the blitter runs as real machine code with
VGA/VRAM routed into the C VGA model.  Compared: every VRAM byte of every plane + VGA registers.
"""
import sys, struct, random, ctypes
import diff_effects as de          # builds libvideo.so (incl. sprite.c)
from diff_effects import lib, SEG_GAME, DS
import diff_video as dv
from diff_video import Orig, snapshot, SEG_VIDEO
from unicorn.x86_const import *

NS = 3
class Spr(ctypes.Structure):
    _fields_ = [('stream', ctypes.c_void_p * 40), ('src_off', ctypes.c_uint16), ('page_off', ctypes.c_uint16),
                ('base', ctypes.c_uint16 * 40), ('hdr', ctypes.c_uint16 * 300), ('tab', ctypes.c_uint16 * (40 * 110 + 64)), ('cursor', ctypes.c_uint16), ('marker0', ctypes.c_uint16), ('marker1', ctypes.c_uint16)]
spr = Spr.in_dll(lib, 'g_spr')
lib.sprite_draw.argtypes = [ctypes.c_int] * 6
STREAM_SEG = 0x4000

def make_stream(r):
    out = bytearray(); starts = []
    for f in range(4):
        starts.append(len(out))
        for _ in range(r.randrange(1, 4)):                       # plane groups
            out += bytes([0, r.choice([1, 2, 4, 8, r.randrange(1, 16)])])
            for _ in range(r.randrange(1, 14)):
                k = r.random()
                if k < .35: out += bytes([1]) + struct.pack('<h', r.choice([r.randrange(0, 12), r.randrange(0, 200), r.randrange(-3, 3)]))
                else: out += bytes([2]) * r.randrange(1, 40)
        out += bytes([3])
    return bytes(out), starts

def fill_vram(seed):
    r = random.Random(seed)
    for p in range(4):
        buf = bytes(r.randrange(256) for _ in range(0x10000))
        ctypes.memmove(lib.vga_plane(p), buf, 0x10000)

def run(seed, ncalls):
    r = random.Random(seed)
    streams, tab, base = [], {}, []
    for s in range(NS):
        b, starts = make_stream(r); streams.append(b)
        base.append(r.randrange(0, 0x1000))
        for a4 in (0, 1):
            for a5 in range(4):
                i = s * 110 + a4 * 20 + a5
                tab[i] = starts[(a4 * 2 + a5) % 4]; tab[i + 40] = r.randrange(0, 0x3000)
    src_off, page_off = r.randrange(0, 0x4000), r.randrange(0, 0x3000)
    page = r.randrange(4); latch_pre = r.randrange(2); mask = r.randrange(1, 16)
    calls = [(r.randrange(NS), r.choice([80, 80, 80, r.randrange(8, 120)]), r.randrange(-6, 90), r.randrange(-6, 170),
              r.randrange(2), r.randrange(4)) for _ in range(ncalls)]
    # --- original
    lib.vga_reset(); o = Orig(); o.call(SEG_VIDEO, 0x185); fill_vram(seed)
    o.call(SEG_VIDEO, 0xE4, (page,)); o.call(SEG_VIDEO, 0x158, (mask,))
    if latch_pre: o.call(SEG_VIDEO, 0x16A, (1,))
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for s in range(NS):
        o.uc.mem_write((STREAM_SEG + s * 0x100) * 16, streams[s]); w(0x7070 + s * 4, struct.pack('<HH', 0, STREAM_SEG + s * 0x100))
        w(0xC18C + s * 2, struct.pack('<H', base[s]))
    for i, v in tab.items(): w(0xC1DC + i * 2, struct.pack('<H', v))
    w(0x6458, struct.pack('<HH', src_off, 0xA000)); w(0xC188, struct.pack('<H', page_off))
    for a in calls: o.call(SEG_GAME, 0x249E, a)
    sa = snapshot({})
    # --- port
    lib.vga_reset(); lib.video_init(); fill_vram(seed)
    lib.video_set_draw_page(page); lib.video_set_map_mask(mask)
    if latch_pre: lib.video_set_latch_mode(1)
    bufs = [ctypes.create_string_buffer(b, len(b)) for b in streams]
    for s in range(NS): spr.stream[s] = ctypes.addressof(bufs[s]); spr.base[s] = base[s]
    for i, v in tab.items(): spr.tab[i] = v
    spr.src_off = src_off; spr.page_off = page_off
    for a in calls: lib.sprite_draw(*a)
    sb = snapshot({})
    diffs = [k for k in sa if sa[k] != sb[k]]
    if diffs: print(f"  seed {seed}: MISMATCH {diffs}  page={page} src={src_off:#x}+{page_off:#x}")
    return len(diffs) > 0

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    bad = sum(run(5000 + s, 6) for s in range(n))
    print(f"{n} scenarios x 6 blits, {bad} mismatching"); sys.exit(1 if bad else 0)
