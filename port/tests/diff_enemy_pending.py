#!/usr/bin/env python3
"""Differential test: original 0BA5:2E9D (spawn all pending map cells) vs enemy_spawn_pending()."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
INFO_SEG = 0x5000
lib.enemy_spawn_pending.argtypes = []
pend = en.pending
info_buf = (ctypes.c_int16 * 14400)(); ctypes.c_void_p.in_dll(lib, 'g_map_info').value = ctypes.addressof(info_buf)

def run(seed):
    r = random.Random(seed)
    defs = [r.choice([0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, -1]) if k == 11 else r.randrange(-5, 40) for _ in range(6) for k in range(12)]
    hdr = [[r.randrange(0, 65536) for _ in range(30)] for _ in range(10)]
    occ = [r.choice([-1, -1, -1, 0, 3, 5]) for _ in range(8)]
    clock = r.randrange(0, 1 << 32); dl = r.choice([clock, 0, r.randrange(0, 1 << 32)])
    dif = r.randrange(-1, 4); pxc = r.randrange(-20, 150); py = r.randrange(-50, 1000); st = rnd_state(r)
    cells = [r.choice([-1, r.randrange(0, 14400), r.randrange(0, 14400)]) for _ in range(32)]
    info = [0x7530] * 14400
    for c in cells:
        if c >= 0 and r.random() < .85: info[c] = r.choice([106 + r.randrange(0, 6), 0x7530 if r.random() < .3 else 106 + r.randrange(0, 6)])
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0xA552, struct.pack('<%dh' % len(defs), *defs)); w(0x706A, struct.pack('<h', dif))
    w(0xCF2C, struct.pack('<h', pxc)); w(0xCF28, struct.pack('<h', py))
    w(0x7908, struct.pack('<I', clock)); w(0xCFD4, struct.pack('<I', dl))
    for s in range(10): w(0xC1A0 + s * 220, struct.pack('<30H', *hdr[s]))
    w(0xC134, struct.pack('<32h', *cells)); w(0x705A, struct.pack('<HH', 0, INFO_SEG + BASE_SEG))
    o.uc.mem_write(INFO_SEG * 16 + 0x10000 * 0, b'')  # no-op
    o.uc.mem_write((INFO_SEG + BASE_SEG) * 16, struct.pack('<14400h', *[v if v < 0x8000 else v - 0x10000 for v in info]))
    for n, off in zip(NAMES, OFFS):
        for i in range(8): w(off + 2 * i, struct.pack('<h', occ[i] if n == 'type' else 0))
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    ctypes.memmove(ds.defs_buf, struct.pack('<%dh' % len(defs), *defs), len(defs) * 2)
    ds.diff.value = dif; ds.dc_cam.player_xcol = pxc; ds.dc_cam.player_y = py
    lib.timer_set_clock(clock); ds.ddl.value = dl
    for s in range(10):
        for k in range(30): spr.hdr[s * 30 + k] = hdr[s][k]
    for i in range(14400): info_buf[i] = info[i] if info[i] < 0x8000 else info[i] - 0x10000
    for i in range(32): pend[i] = cells[i]
    for n in NAMES:
        for i in range(8): getattr(en, n)[i] = occ[i] if n == 'type' else 0
    csnd.clear(); bad = []
    o.call(SEG_GAME, 0x2E9D); lib.enemy_spawn_pending()
    for n, off in zip(NAMES, OFFS):
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(en, n)): bad.append(n)
    if struct.unpack('<32h', o.dsb(0xC134, 64)) != tuple(pend): bad.append('pending')
    if struct.unpack('<I', o.dsb(0xCFD4, 4))[0] != ds.ddl.value: bad.append('deadline')
    if o.snd != csnd: bad.append('sound')
    if o.rw(0x7118) != (lib.rng_index() & 0xFFFF): bad.append('rng')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]}')
    return len(bad) > 0

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    bad = sum(run(9000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
