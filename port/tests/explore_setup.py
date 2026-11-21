#!/usr/bin/env python3
"""Explore 0BA5:4549..4D44 (level setup): which DS bytes it writes and which external routines it calls (stubbed)."""
import sys, struct, os
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_effects as de
import diff_animate as da
from diff_video import BASE_SEG
from range_harness import run_range
from unicorn import *
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
exe = open(ROOT + '/HOCUS.EXE', 'rb').read(); dat = open(ROOT + '/HOCUS.DAT', 'rb').read()
fat = [struct.unpack_from('<II', exe, 0x1F1A4 + 8 * i) for i in range(652)]
STUBS = {(0x5d8, 0x673): ('pcx', 2), (0x5d8, 0xaff): ('pal', 3), (0x5d8, 0xa70): ('img', 3), (0xba5, 0x2242): ('sprites', 0), (0x136b, 0x1ff): ('music', 2), (0x1392, 0x200): ('inp200', 0)}
PORTS = {}
class OP(OS):
    def __init__(self):
        super().__init__(); self.ext = []
        w = lambda seg, off, b: self.uc.mem_write((seg + BASE_SEG) * 16 + off, b)
        w(0x68c, 0xF9, b'\xe6\xad\xcb'); w(0x68c, 4, b'\xcb'); w(0x68c, 0x32, b'\xcb')
        for i, ((seg, off), (name, n)) in enumerate(STUBS.items()):
            PORTS[0xb0 + i] = (name, n); w(seg, off, bytes([0xe6, 0xb0 + i, 0xcb]))
    def wr(self, p, v):
        if p == 0xad:
            entry, off, seg = self.arg(0), self.arg(1), self.arg(2); o, n = fat[entry]; self.uc.mem_write(seg * 16 + off, dat[o:o + n]); self.ext.append(('read', entry))
        elif p in PORTS: nm, n = PORTS[p]; self.ext.append((nm,) + tuple(self.arg(i) for i in range(n)))
        else: super().wr(p, v)
def explore(epi, lvl, mode=0):
    import random
    st = rnd_state(random.Random(1))
    lib.vga_reset(); o = OP(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for k, seg in da.SEGS.items(): w(da.PTR[k], struct.pack('<HH', 0, seg))
    w(0x705A, struct.pack('<HH', 0, 0x5000)); w(0x7056, struct.pack('<HH', 0, 0x4000)); w(0x7070, bytes(160))
    w(0x706E, struct.pack('<h', epi)); w(0x706C, struct.pack('<h', lvl)); w(0x48C6, struct.pack('<HH', 0, 0x6000))
    w(0x0, bytes([0x5A]) * 0x100)
    # dirty marker: fill a region of DS with 0x5A pattern so unwritten bytes are recognisable
    snap = bytes(o.uc.mem_read(DS * 16, 0x10000))
    writes = set()
    lo, hi = DS * 16, DS * 16 + 0x10000
    def hook(uc, access, addr, size, value, ud):
        if lo <= addr < hi:
            for k in range(size): writes.add(addr - lo + k)
    o.uc.hook_add(UC_HOOK_MEM_WRITE, hook)
    run_range(o, 0x4549, 0x4D44, mode=mode, count=200_000_000)
    return o, writes, snap
def ranges(s):
    s = sorted(s); out = []
    for a in s:
        if out and a == out[-1][1] + 1: out[-1][1] = a
        else: out.append([a, a])
    return out
if __name__ == '__main__':
    o, writes, snap = explore(int(sys.argv[1]) if len(sys.argv) > 1 else 0, int(sys.argv[2]) if len(sys.argv) > 2 else 0, int(sys.argv[3]) if len(sys.argv) > 3 else 0)
    print('external calls:'); [print('  ', e) for e in o.ext]
    print('DS ranges written (%d bytes):' % len(writes)); print(' '.join(f'{a:04X}-{b:04X}' if a != b else f'{a:04X}' for a, b in ranges(writes)))
