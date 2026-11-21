#!/usr/bin/env python3
"""Differential test: HUD drawing 0BA5:3125 / 3297 / 348D and the item counter 3643 vs hud_icon8 / hud_icon11 /
hud_draw / level_count_items.  VRAM (all planes, random initial content incl. the icon page), VGA registers, the
variables the HUD writes (CFD8, 704C, 704A) are compared."""
import sys, struct, random, ctypes
import diff_level as dl
from diff_level import *
import diff_effects as de
from diff_video import snapshot, BASE_SEG
lib.hud_icon8.argtypes = [ctypes.c_int] * 2; lib.hud_icon11.argtypes = [ctypes.c_int] * 2
lib.hud_draw.argtypes = []; lib.level_count_items.argtypes = []
EXTRA = [('level_icon', 0x706C), ('items_left', 0x704A)]

def noise(r):
    for p in range(4):
        lib.vga_set_map_mask(1 << p)
        for pg in range(4):
            for off in range(pg * 0x3F00 + 14000, pg * 0x3F00 + 16000): lib.vga_write(off, r.randrange(256))
    lib.vga_set_map_mask(0xF)

def run(seed):
    r = random.Random(seed); st = rnd_state(r); op = r.choice(['i8', 'i11', 'hud', 'hud', 'hud', 'count'])
    nz = r.randrange(1 << 30)
    items = bytearray()
    for v in range(23): items += struct.pack('<hBBB', 0, 0, 0, r.choice([0, 1, 1, 2, 5])) + bytes(37)
    info = [r.choice([0x7530, r.randrange(0, 23), r.randrange(0, 23), 0x16, 0x17, 0x18, 0x7530]) for _ in range(N)]
    V = dict(complete=r.choice([0, 0, 1, 9, 10, 15, 20, 21]), score=r.choice([0, 7, 99, 12345, 0xFFFFFFFF, r.randrange(0, 1 << 32), 987654321]),
             lives=r.choice([0, 3, 9, r.randrange(0, 20)]), key1=r.randrange(2), key2=r.randrange(2), level_icon=r.randrange(0, 12), items_left=r.randrange(0, 5))
    health = r.choice([0, 5, 100, 99, 150, -7, r.randrange(-20, 130)]); a0 = r.choice([r.randrange(0, 0x3E00), 0x392a, 0x389a, 0x3875]); a1 = r.randrange(-2, 14)
    lib.vga_reset(); o = OS3(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0x705A, struct.pack('<HH', 0, INFO_SEG + BASE_SEG)); o.uc.mem_write((INFO_SEG + BASE_SEG) * 16, struct.pack('<%dh' % N, *info))
    w(0x2628, bytes(items))
    for n, off in LV:
        if n in V: w(off, struct.pack('<I' if n == 'score' else '<h', V[n]))
    w(0x704C, struct.pack('<h', health))
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o); noise(random.Random(nz))
    args = {'i8': (a0, a1), 'i11': (a0, a1), 'hud': (), 'count': ()}[op]
    o.call(SEG_GAME, {'i8': 0x3125, 'i11': 0x3297, 'hud': 0x348D, 'count': 0x3643}[op], args); sa = snapshot({})
    lib.vga_reset(); lib.video_init(); load_state(st, None); noise(random.Random(nz))
    for i in range(N): info_buf[i] = info[i]
    ctypes.memmove(ctypes.addressof((ctypes.c_char * (23 * 42)).in_dll(lib, 'g_tile_items')), bytes(items), 23 * 42)
    for n in V: setattr(lv, n, V[n])
    pl.health = health
    {'i8': lambda: lib.hud_icon8(a0, a1), 'i11': lambda: lib.hud_icon11(a0, a1), 'hud': lib.hud_draw, 'count': lib.level_count_items}[op]()
    sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    for n, off in (('complete', 0xCFD8), ('level_icon', 0x706C), ('items_left', 0x704A)):
        if o.rw(off) != (getattr(lv, n) & 0xFFFF): bad.append(n)
    if o.rw(0x704C) != (pl.health & 0xFFFF): bad.append('health')
    if bad: print(f'  seed {seed} {op}: MISMATCH {sorted(set(bad))[:6]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    bad = sum(run(41000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
