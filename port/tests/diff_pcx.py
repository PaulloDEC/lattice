#!/usr/bin/env python3
"""
Differential test: original LoadPCX (05D8:0673 + decoder 052F) and palette effects
(05D8:0419..051A) vs video.c.

File I/O is stubbed: the DAT loader functions (068C:0004 open, 0032 close, 0047 seek,
0089 read, 02B5 getc) and the FAT lookup (1392:07CA) are overwritten in the emulator with
tiny stubs that call back into Python.  Everything else is the original machine code.
Inputs: every PCX entry in the real HOCUS.DAT, plus synthetic RLE streams.
"""
import sys, struct, random, ctypes
sys.path.insert(0, '/home/claude/hocus'); sys.path.insert(0, '/home/claude/hocus/port/tests')
import diff_video as dv
from diff_video import lib, Orig, snapshot, SEG_VIDEO, DS, STACK_SEG, BASE_SEG
from hocus_dat import HocusData
from unicorn.x86_const import *

H = HocusData('/home/claude/hocus/HOCUS.EXE', '/home/claude/hocus/HOCUS.DAT')
lib.video_load_pcx.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int]
lib.video_target_palette.restype = ctypes.POINTER(ctypes.c_uint8)
lib.video_work_palette.restype = ctypes.POINTER(ctypes.c_uint8)
for f in ('video_pcx_width', 'video_pcx_height', 'video_pcx_bytes_per_line'): getattr(lib, f).restype = ctypes.c_int
lib.video_pal_fade_out.argtypes = [ctypes.c_int]; lib.video_pal_fade_in.argtypes = [ctypes.c_int]

class OrigIO(Orig):
    def __init__(self, entries):
        super().__init__()
        self.entries = entries          # index -> bytes (virtual DAT entries)
        self.pos = 0; self.cur = b''; self.uploads = 0
        mem = self.uc.mem_write
        def patch(seg, off, code): mem((seg + BASE_SEG) * 16 + off, code)
        patch(0x68c, 0x04,  b'\xcb')                 # open  -> retf
        patch(0x68c, 0x32,  b'\xcb')                 # close -> retf
        patch(0x68c, 0x47,  b'\xe6\x9a\xcb')         # seek   (out 9Ah ; retf)
        patch(0x68c, 0x89,  b'\xe6\x9b\xcb')         # read   (out 9Bh ; retf)
        patch(0x68c, 0x2b5, b'\xe4\x99\x30\xe4\xcb') # getc   (in al,99h ; xor ah,ah ; retf)
        patch(0x1392, 0x7ca, b'\xe6\x9c\xcb')        # FAT lookup (out 9Ch ; retf)
    # stack helpers: at the stub, SS:SP -> [retIP][retCS][arg1]...
    def arg(self, n):
        sp = self.uc.reg_read(UC_X86_REG_SP); ss = self.uc.reg_read(UC_X86_REG_SS)
        return struct.unpack('<H', bytes(self.uc.mem_read(ss * 16 + sp + 4 + 2 * n, 2)))[0]
    def lin(self, seg, off): return ((seg << 4) + off) & 0xFFFFF
    def wr(self, p, v):
        if p == 0x3c8: self.uploads += 1
        if p == 0x9a:                                    # seek(long off)
            off = self.arg(0) | (self.arg(1) << 16); self.pos = off
        elif p == 0x9b:                                  # read(long off, long len, far dest)
            off = self.arg(0) | (self.arg(1) << 16); ln = self.arg(2) | (self.arg(3) << 16)
            d_off, d_seg = self.arg(4), self.arg(5)
            self.uc.mem_write(self.lin(d_seg, d_off), self.cur[off:off + ln]); self.pos = off + ln
        elif p == 0x9c:                                  # GetEntry(index, far &off, far &size)
            idx = self.arg(0); data = self.entries[idx]
            self.cur = data
            o_off, o_seg, s_off, s_seg = self.arg(1), self.arg(2), self.arg(3), self.arg(4)
            self.uc.mem_write(self.lin(o_seg, o_off), struct.pack('<I', 0))
            self.uc.mem_write(self.lin(s_seg, s_off), struct.pack('<I', len(data)))
        else: super().wr(p, v)
    def rd(self, p):
        if p == 0x99:
            b = self.cur[self.pos]; self.pos += 1; return b
        return super().rd(p)
    def ds_bytes(self, off, n): return bytes(self.uc.mem_read(DS * 16 + off, n))

def c_state():
    t = bytes(ctypes.string_at(lib.video_target_palette(), 768))
    w = bytes(ctypes.string_at(lib.video_work_palette(), 768))
    return t, w

def orig_state(o):
    return o.ds_bytes(0x6a64, 768), o.ds_bytes(0x6764, 768)

def compare(tag, a, b, extra_a=None, extra_b=None):
    diffs = [k for k in a if a[k] != b[k]]
    if extra_a != extra_b: diffs.append('extra')
    if diffs:
        print(f"MISMATCH {tag}: {diffs}")
        for k in diffs:
            if k.startswith('plane'): print(f"   {k}: {sum(1 for x,y in zip(a[k],b[k]) if x!=y)} bytes differ")
        return 1
    return 0

def run_load(entries, idx, apply, draw_page, use_c=False):
    latch = (idx + draw_page) % 2          # half the runs start in latch mode with a partial map mask
    if use_c:
        lib.vga_reset(); lib.video_init(); lib.video_set_draw_page(draw_page)
        if latch: lib.video_set_latch_mode(1); lib.video_set_map_mask(0x05)
        lib.video_load_pcx(entries[idx], len(entries[idx]), apply)
        t, w = c_state()
        extra = (lib.video_pcx_width(), lib.video_pcx_height(), lib.video_pcx_bytes_per_line(), t)
        return snapshot({}), extra
    o = OrigIO(entries)
    lib.vga_reset(); o.call(SEG_VIDEO, 0x185); o.call(SEG_VIDEO, 0xE4, (draw_page,))
    if latch: o.call(SEG_VIDEO, 0x16A, (1,)); o.call(SEG_VIDEO, 0x158, (0x05,))
    o.call(SEG_VIDEO, 0x673, (idx, apply))
    t, w = orig_state(o)
    extra = (o.rw(0x796e), o.rw(0x796c), o.rw(0x796a), t)
    return snapshot({}), extra

def synth_pcx(rng, height, tokens_zero=True):
    """valid 320-wide PCX with random RLE, including zero-length runs and escaped literals"""
    body = bytearray()
    for _ in range(height):
        n = 0
        while n < 320:
            r = rng.random()
            if r < 0.5:
                run = rng.randrange(1, min(63, 320 - n) + 1)
                body += bytes([0xC0 | run, rng.randrange(256)]); n += run
            elif r < 0.55 and tokens_zero:
                body += bytes([0xC0, rng.randrange(256)])                 # zero-length run
            elif r < 0.7:
                v = rng.randrange(0xC0, 256); body += bytes([0xC1, v]); n += 1   # escaped literal
            else:
                body += bytes([rng.randrange(0xC0)]); n += 1
    hdr = bytearray(128); hdr[0] = 10; hdr[1] = 5; hdr[2] = 1; hdr[3] = 8
    struct.pack_into('<4H', hdr, 4, 0, 0, 319, height - 1); struct.pack_into('<H', hdr, 66, 320)
    pal = bytes([12]) + bytes(rng.randrange(256) for _ in range(768))
    return bytes(hdr) + bytes(body) + pal

if __name__ == '__main__':
    bad = n = 0
    # ---- 1. real PCX entries from HOCUS.DAT ----------------------------------------
    pcx_ids = [i for i in range(652) if H.get(i)[:4] == b'\x0a\x05\x01\x08']
    entries = {i: H.get(i) for i in pcx_ids}
    rng = random.Random(7)
    for k, i in enumerate(pcx_ids):
        apply = k % 2; page = rng.randrange(4)
        a, ea = run_load(entries, i, apply, page); b, eb = run_load(entries, i, apply, page, use_c=True)
        bad += compare(f"real PCX entry {i} apply={apply} page={page}", a, b, ea, eb); n += 1
    print(f"real PCX entries: {len(pcx_ids)} loaded, {bad} mismatches")
    # ---- 2. synthetic RLE ------------------------------------------------------------
    sbad = 0
    for s in range(20):
        rng = random.Random(500 + s); e = {999: synth_pcx(rng, rng.randrange(1, 201))}
        a, ea = run_load(e, 999, s % 2, s % 4); b, eb = run_load(e, 999, s % 2, s % 4, use_c=True)
        sbad += compare(f"synthetic #{s}", a, b, ea, eb); n += 1
    print(f"synthetic streams: 20 loaded, {sbad} mismatches"); bad += sbad
    # ---- 3. palette effects ------------------------------------------------------------
    pbad = 0
    ops = [('fade_out', 0x419, 'video_pal_fade_out', 1), ('fade_in', 0x46E, 'video_pal_fade_in', 1),
           ('white', 0x4B0, 'video_pal_fade_white', 0), ('black', 0x4F3, 'video_pal_set_black', 0),
           ('apply', 0x51A, 'video_pal_apply', 0)]
    for s in range(6):
        rng = random.Random(900 + s)
        tgt = bytes(rng.randrange(64) for _ in range(768))
        seq = [rng.choice(ops) for _ in range(4)]
        steps = [rng.choice([1, 2, 5, 16, 31, 64, 100]) for _ in seq]
        # original
        o = OrigIO({}); lib.vga_reset(); o.call(SEG_VIDEO, 0x185)
        o.uc.mem_write(DS * 16 + 0x6a64, tgt)
        for (name, off, cf, takes), st in zip(seq, steps): o.call(SEG_VIDEO, off, (st,) if takes else ())
        a = snapshot({}); ta, wa = orig_state(o); ups_o = o.uploads
        # port
        lib.vga_reset(); lib.video_init()
        ctypes.memmove(lib.video_target_palette(), tgt, 768)
        for (name, off, cf, takes), st in zip(seq, steps):
            f = getattr(lib, cf); f(st) if takes else f()
        b = snapshot({}); tb, wb = c_state()
        label = ','.join(f"{n_}({st})" if t else n_ for (n_, _, _, t), st in zip(seq, steps))
        pbad += compare(f"palette effects [{label}]", a, b, (ta, wa), (tb, wb)); n += 1
    print(f"palette effect sequences: 6 run, {pbad} mismatches"); bad += pbad
    print(f"TOTAL: {n} scenarios, {bad} mismatches")
    sys.exit(1 if bad else 0)
