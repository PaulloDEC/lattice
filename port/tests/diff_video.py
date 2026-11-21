#!/usr/bin/env python3
"""
Differential test of port/src/video.c against the ORIGINAL video module (05D8).

The original machine code runs in Unicorn.  Its VGA port I/O and A000:xxxx memory
accesses are routed into the C VGA model (vga.c) through ctypes, so both sides use the
same low-level VGA semantics; what is being compared is the game-side logic: page
layout, addressing, argument order, which registers get programmed, loop bounds.

For each random scenario: run it via the original code -> snapshot full VGA state +
original's DS variables + return values; reset; run the same ops through video.c ->
snapshot; compare everything.
"""
CALL_LIMIT = 60_000_000
import sys, struct, random, ctypes
sys.path.insert(0, '/home/claude/hocus')
from mz import MZ
from unicorn import *
from unicorn.x86_const import *

BASE_SEG = 0x1000
m = MZ('/home/claude/hocus/HOCUS.EXE')
mem = bytearray(m.mem)
for off, seg in m.relocs:
    lin = seg * 16 + off
    v = struct.unpack_from('<H', mem, lin)[0]
    struct.pack_into('<H', mem, lin, (v + BASE_SEG) & 0xFFFF)
DS = 0x1d51 + BASE_SEG
SEG_VIDEO = 0x5d8 + BASE_SEG
SENT_SEG, STACK_SEG = 0x0F00, 0x0E00
DATA_SEG = 0x3000                    # scratch for palette buffers

# ---- C library ------------------------------------------------------------------
lib = ctypes.CDLL('/home/claude/hocus/port/tests/libvideo.so')
u8, u16 = ctypes.c_uint8, ctypes.c_uint16
lib.vga_read.restype = u8; lib.vga_read.argtypes = [u16]
lib.vga_write.argtypes = [u16, u8]
lib.vga_plane.restype = ctypes.POINTER(u8)
lib.vga_get_start.restype = u16
for f in ('vga_get_map_mask', 'vga_get_read_plane', 'vga_get_bit_mask'): getattr(lib, f).restype = u8
lib.vga_get_screen_on.restype = ctypes.c_int
lib.vga_set_map_mask.argtypes = [u8]; lib.vga_set_bit_mask.argtypes = [u8]
lib.vga_set_screen_on.argtypes = [ctypes.c_int]; lib.vga_set_start.argtypes = [u16]
lib.vga_set_read_plane.argtypes = [u8]
lib.vga_pal_set.argtypes = [ctypes.c_int, ctypes.c_char_p]
lib.vga_pal_get.argtypes = [ctypes.c_int, ctypes.c_char_p]
lib.video_get_pixel.restype = ctypes.c_int
lib.video_draw_page.restype = ctypes.c_int; lib.video_visible_page.restype = ctypes.c_int
lib.video_draw_offset.restype = u16

# ---- emulator --------------------------------------------------------------------
class Orig:
    def __init__(self):
        uc = self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x9F000)                       # conventional memory incl. load module
        uc.mem_write(BASE_SEG * 16, bytes(mem))
        uc.mem_map(0xB0000, 0x50000)
        uc.mmio_map(0xA0000, 0x10000, self.mmio_r, None, self.mmio_w, None)
        uc.mem_write(SENT_SEG * 16, b'\xf4')
        self.seq = {1: 0x01, 2: 0x0F, 4: 0x0E}; self.gc = {4: 0, 5: 0x10, 6: 0x05, 8: 0xFF}
        self.crtc = {}; self.si = self.gi = self.ci = 0
        self.dac_w = [0, 0]; self.dac_r = [0, 0]; self.n3da = 0
        uc.hook_add(UC_HOOK_INSN, self.h_in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self.h_out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, lambda *a: None)
    # -- MMIO into the C VGA model
    def mmio_r(self, uc, off, size, ud):
        v = 0
        for i in range(size): v |= lib.vga_read((off + i) & 0xFFFF) << (8 * i)
        return v
    def mmio_w(self, uc, off, size, val, ud):
        for i in range(size): lib.vga_write((off + i) & 0xFFFF, (val >> (8 * i)) & 0xFF)
    # -- ports
    def h_out(self, uc, port, size, val, ud):
        if size == 2: self.wr(port, val & 0xFF); self.wr(port + 1, val >> 8)
        else: self.wr(port, val & 0xFF)
    def h_in(self, uc, port, size, ud): return self.rd(port)
    def wr(self, p, v):
        if p == 0x3c4: self.si = v
        elif p == 0x3c5:
            self.seq[self.si] = v
            if self.si == 2: lib.vga_set_map_mask(v)
            elif self.si == 1: lib.vga_set_screen_on(0 if v & 0x20 else 1)
        elif p == 0x3ce: self.gi = v
        elif p == 0x3cf:
            self.gc[self.gi] = v
            if self.gi == 4: lib.vga_set_read_plane(v)
            elif self.gi == 8: lib.vga_set_bit_mask(v)
        elif p == 0x3d4: self.ci = v
        elif p == 0x3d5:
            self.crtc[self.ci] = v
            if self.ci in (0xC, 0xD):
                lib.vga_set_start(((self.crtc.get(0xC, 0) << 8) | self.crtc.get(0xD, 0)) & 0xFFFF)
        elif p == 0x3c8: self.dac_w = [v, 0]
        elif p == 0x3c7: self.dac_r = [v, 0]
        elif p == 0x3c9:
            idx, k = self.dac_w
            cur = ctypes.create_string_buffer(3); lib.vga_pal_get(idx, cur)
            b = bytearray(cur.raw); b[k] = v & 0x3F; lib.vga_pal_set(idx, bytes(b))
            k += 1
            self.dac_w = [(idx + 1) & 255, 0] if k == 3 else [idx, k]
    def rd(self, p):
        if p == 0x3c5: return self.seq.get(self.si, 0)
        if p == 0x3cf: return self.gc.get(self.gi, 0)
        if p == 0x3d5: return self.crtc.get(self.ci, 0)
        if p == 0x3da:
            self.n3da += 1
            return 8 if (self.n3da // 3) % 2 else 0
        if p == 0x3c9:
            idx, k = self.dac_r
            cur = ctypes.create_string_buffer(3); lib.vga_pal_get(idx, cur)
            val = cur.raw[k]; k += 1
            self.dac_r = [(idx + 1) & 255, 0] if k == 3 else [idx, k]
            return val
        return 0
    # -- calling convention: far cdecl, 16-bit args pushed right-to-left
    def call(self, seg, off, args=(), far_args=()):
        uc = self.uc
        words = []
        for a in args: words.append(a & 0xFFFF)
        sp = 0x1000
        frame = b''.join(struct.pack('<H', w) for w in words)
        sp -= len(frame) + 4
        uc.mem_write(STACK_SEG * 16 + sp, struct.pack('<HH', 0, SENT_SEG) + frame)
        uc.reg_write(UC_X86_REG_SS, STACK_SEG); uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_DS, DS); uc.reg_write(UC_X86_REG_ES, DS)
        uc.reg_write(UC_X86_REG_CS, seg)
        try: uc.emu_start(seg * 16 + off, SENT_SEG * 16, count=CALL_LIMIT)
        except UcError as e: raise RuntimeError(f"emu error in {seg:#x}:{off:#x} args={args}: {e}")
        return uc.reg_read(UC_X86_REG_AX)
    def rw(self, off): return struct.unpack('<H', bytes(self.uc.mem_read(DS * 16 + off, 2)))[0]

def snapshot(extra):
    s = {}
    for p in range(4): s[f'plane{p}'] = ctypes.string_at(lib.vga_plane(p), 0x10000)
    s['start'] = lib.vga_get_start(); s['mask'] = lib.vga_get_map_mask()
    s['rplane'] = lib.vga_get_read_plane(); s['bitmask'] = lib.vga_get_bit_mask()
    s['screen'] = lib.vga_get_screen_on()
    pal = bytearray()
    for i in range(256):
        b = ctypes.create_string_buffer(3); lib.vga_pal_get(i, b); pal += b.raw
    s['pal'] = bytes(pal); s.update(extra)
    return s

def preload_noise(rng):
    lib.vga_set_bit_mask(0xFF)
    for p in range(4):
        lib.vga_set_map_mask(1 << p)
        data = bytes(rng.randrange(256) for _ in range(0x10000))
        for off in range(0, 0xFC00): lib.vga_write(off, data[off])
    lib.vga_set_map_mask(0x0F)

def make_ops(rng, n):
    ops = []
    for _ in range(n):
        k = rng.choice(['draw', 'shownow', 'show', 'copypage', 'copyrect', 'clear', 'fill', 'put',
                        'put', 'put', 'get', 'clrpix', 'setpal', 'getpal', 'screen', 'mask', 'rplane', 'latch'])
        if k in ('draw', 'shownow', 'show'): ops.append((k, rng.randrange(4)))
        elif k == 'copypage': ops.append((k, rng.randrange(4), rng.randrange(4)))
        elif k == 'copyrect':
            x0 = rng.randrange(80); x1 = rng.randrange(x0, 80); y0 = rng.randrange(200); y1 = min(199, y0 + rng.randrange(8))
            w = x1 - x0 + 1; dx = rng.randrange(0, 80 - w + 1); dy = rng.randrange(200)
            ops.append((k, rng.randrange(4), x0, y0, x1, y1, rng.randrange(4), dx, dy))
        elif k == 'clear': ops.append((k,))
        elif k == 'fill':
            x0 = rng.randrange(80); x1 = rng.randrange(x0, 80); y0 = rng.randrange(200); y1 = min(199, y0 + rng.randrange(30))
            ops.append((k, x0, y0, x1, y1))
        elif k == 'put': ops.append((k, rng.randrange(320), rng.randrange(200), rng.randrange(256)))
        elif k == 'get': ops.append((k, rng.randrange(320), rng.randrange(200)))
        elif k == 'clrpix':
            x0 = rng.randrange(320); x1 = min(319, x0 + rng.randrange(6)); y0 = rng.randrange(200); y1 = min(199, y0 + rng.randrange(4))
            ops.append((k, x0, y0, x1, y1))
        elif k in ('setpal', 'getpal'):
            first = rng.randrange(256); cnt = rng.randrange(1, 256 - first + 1) if rng.random() < .5 else rng.randrange(1, 8)
            cnt = min(cnt, 256 - first)
            data = bytes(rng.randrange(64) for _ in range(3 * cnt))
            ops.append((k, first, cnt, data))
        elif k == 'screen': ops.append((k, rng.randrange(2)))
        elif k == 'mask': ops.append((k, rng.randrange(16)))
        elif k == 'rplane': ops.append((k, rng.randrange(4)))
        elif k == 'latch': ops.append((k, rng.randrange(2)))
    return ops

ORIG_OFF = dict(draw=0xE4, shownow=0xA7, show=0x50, copypage=0x1F0, copyrect=0x22A, clear=0x296,
                fill=0x2BE, put=0x33B, get=0x306, clrpix=0x372, setpal=0x39C, getpal=0x3DA,
                screen=0x135, mask=0x158, rplane=0x123, latch=0x16A)

def run_orig(ops, rng_noise_seed):
    o = Orig()
    lib.vga_reset(); o.call(SEG_VIDEO, 0x185)                    # original VideoInit
    preload_noise(random.Random(rng_noise_seed))
    rets = []
    for op in ops:
        k, a = op[0], op[1:]
        if k == 'draw': o.call(SEG_VIDEO, ORIG_OFF[k], a)
        elif k in ('setpal', 'getpal'):
            first, cnt, data = a
            o.uc.mem_write(DATA_SEG * 16, data)
            far = (DATA_SEG, 0)
            r = o.call(SEG_VIDEO, ORIG_OFF[k], (0, DATA_SEG, first, cnt))   # far ptr: off then seg (off pushed last -> first arg)
            if k == 'getpal': rets.append(bytes(o.uc.mem_read(DATA_SEG * 16, 3 * cnt)))
        elif k == 'get':
            rets.append(o.call(SEG_VIDEO, ORIG_OFF[k], a) & 0xFFFF)
        elif k == 'copyrect':
            o.call(SEG_VIDEO, ORIG_OFF[k], a)
        else: o.call(SEG_VIDEO, ORIG_OFF[k], a)
    extra = dict(visible=o.rw(0x6444), drawpage=o.rw(0x6446), drawoff=o.rw(0x6460))
    return snapshot(extra), rets

def run_port(ops, rng_noise_seed):
    lib.vga_reset(); lib.video_init()
    preload_noise(random.Random(rng_noise_seed))
    rets = []
    for op in ops:
        k, a = op[0], op[1:]
        if k == 'draw': lib.video_set_draw_page(a[0])
        elif k == 'shownow': lib.video_show_page_now(a[0])
        elif k == 'show': lib.video_show_page(a[0])
        elif k == 'copypage': lib.video_copy_page(*a)
        elif k == 'copyrect': lib.video_copy_rect(*a)
        elif k == 'clear': lib.video_clear_draw_page()
        elif k == 'fill': lib.video_fill_rect(*a)
        elif k == 'put': lib.video_put_pixel(*a)
        elif k == 'get': rets.append(lib.video_get_pixel(*a) & 0xFFFF)
        elif k == 'clrpix': lib.video_clear_pixels(*a)
        elif k == 'setpal':
            first, cnt, data = a; lib.video_set_palette(data, first, cnt)
        elif k == 'getpal':
            first, cnt, data = a; buf = ctypes.create_string_buffer(3 * cnt)
            lib.video_get_palette(buf, first, cnt); rets.append(buf.raw)
        elif k == 'screen': lib.video_screen(a[0])
        elif k == 'mask': lib.video_set_map_mask(a[0])
        elif k == 'rplane': lib.video_set_read_plane(a[0])
        elif k == 'latch': lib.video_set_latch_mode(a[0])
    extra = dict(visible=lib.video_visible_page(), drawpage=lib.video_draw_page(), drawoff=lib.video_draw_offset())
    return snapshot(extra), rets

if __name__ == '__main__':
    only = sys.argv[1] if len(sys.argv) > 1 else None
    nscen = int(sys.argv[2]) if len(sys.argv) > 2 else 6
    nops = int(sys.argv[3]) if len(sys.argv) > 3 else 60
    total_ops = bad = 0
    for sc in range(nscen):
        rng = random.Random(1000 + sc)
        ops = make_ops(rng, nops)
        if only and only != 'all': ops = [o for o in ops if o[0] == only]
        a, ra = run_orig(ops, sc); b, rb = run_port(ops, sc)
        total_ops += len(ops)
        diffs = [k for k in a if a[k] != b[k]]
        if ra != rb: diffs.append('return-values')
        if diffs:
            bad += 1
            print(f"scenario {sc}: MISMATCH in {diffs}")
            for k in diffs:
                if k.startswith('plane'):
                    n = sum(1 for x, y in zip(a[k], b[k]) if x != y); print(f"   {k}: {n} bytes differ")
                elif k != 'return-values': print(f"   {k}: orig={a[k] if not isinstance(a[k],bytes) else '..'} port={b[k] if not isinstance(b[k],bytes) else '..'}")
    print(f"{nscen} scenarios, {total_ops} ops, {bad} mismatching scenarios")
    sys.exit(1 if bad else 0)
