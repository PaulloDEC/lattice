#!/usr/bin/env python3
"""Differential test: 0BA5:470D..499F (level data loading + counters) vs level_load_data(), all 36 levels, real HOCUS.DAT."""
import sys, struct, ctypes, os
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
import diff_animate as da
from diff_level import lv, N
from diff_video import BASE_SEG
from range_harness import run_range
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
exe = open(ROOT + '/HOCUS.EXE', 'rb').read(); dat = open(ROOT + '/HOCUS.DAT', 'rb').read()
fat = [struct.unpack_from('<II', exe, 0x1F1A4 + 8 * i) for i in range(652)]
assert lib.dat_open(os.path.join(ROOT, 'HOCUS.DAT').encode()) == 0 if False else True
lib.dat_open.argtypes = [ctypes.c_char_p]; assert lib.dat_open((ROOT + '/HOCUS.DAT').encode()) == 0
lib.level_load_data.restype = ctypes.c_int
epi = ctypes.c_int16.in_dll(lib, 'g_episode'); total = ctypes.c_int16.in_dll(lib, 'g_enemies_total'); kills = ctypes.c_int16.in_dll(lib, 'g_kills')
class OP(OS):
    def __init__(self):
        super().__init__()
        w = lambda seg, off, b: self.uc.mem_write((seg + BASE_SEG) * 16 + off, b)
        w(0x68c, 0xF9, b'\xe6\xad\xcb'); w(0x68c, 4, b'\xcb'); w(0x68c, 0x32, b'\xcb')
    def wr(self, p, v):
        if p == 0xad:
            entry, off, seg = self.arg(0), self.arg(1), self.arg(2)
            o, n = fat[entry]; self.uc.mem_write(seg * 16 + off, bytes(dat[o:o + n]))
        else: super().wr(p, v)
DSR = [(0x703C, 8), (0x6D68, 724), (0xAAC4, 5040), (0xAA94, 40), (0xA89A, 506), (0xA76E, 300), (0xA642, 300), (0xA552, 240), (0x8612, 8000)]
def run(epi_n, lvl_n, perturb=0):
    global dat
    import random, tempfile
    real = dat
    if perturb:
        r = random.Random(perturb * 1000 + epi_n * 10 + lvl_n); dat = bytearray(real)
        base = struct.unpack('<h', exe[0x1F110 + 0x2A1A + 2 * (epi_n * 10 + lvl_n):][:2])[0]
        o_i, n_i = fat[131 + 36 * 12 + base]
        for _ in range(300): dat[o_i + 2 * r.randrange(14400): ][:0]; p = o_i + 2 * r.randrange(14400); dat[p:p + 2] = struct.pack('<h', r.choice([0, 1, 0x15, 0x16, 0x17, 0x18, 0x69, 0x6A, 0x6B, 0x72, 0x73, 0x74, -1, 0x7530, 5]))
        o_d, n_d = fat[131 + 36 * 7 + base]
        for k in range(10):
            if r.random() < .5: dat[o_d + 24 * k + 2: o_d + 24 * k + 4] = struct.pack('<h', -1)
        tf = tempfile.NamedTemporaryFile(delete=False); tf.write(bytes(dat)); tf.close(); assert lib.dat_open(tf.name.encode()) == 0
    st = rnd_state(__import__('random').Random(epi_n * 10 + lvl_n))
    lib.vga_reset(); o = OP(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for k, seg in da.SEGS.items(): w(da.PTR[k], struct.pack('<HH', 0, seg))
    w(0x705A, struct.pack('<HH', 0, 0x5000)); w(0x7056, struct.pack('<HH', 0, 0x4000))
    w(0x706E, struct.pack('<h', epi_n)); w(0x706C, struct.pack('<h', lvl_n))
    run_range(o, 0x470D, 0x499F)
    # port
    epi.value = epi_n; lv.level_icon = lvl_n
    buf = {}
    assert lib.level_load_data() == 0
    bad = []
    mem = lambda addr, n: bytes(o.uc.mem_read(addr, n))
    # compare through the port's globals
    def sym(name, n): return bytes((ctypes.c_char * n).in_dll(lib, name))
    for (off, n), name in zip(DSR, ('g_level_start', None, 'g_hints', 'g_teleporters', 'g_levers', 'g_doors1', 'g_doors2', None, 'g_spawn_groups')):
        if name and mem(DS * 16 + off, n) != sym(name, n): bad.append(name)
    if mem(DS * 16 + 0x6D68, 4) != bytes([lv.tile_blank, lv.tile_lever_a, lv.tile_lever_b, ctypes.c_uint8.in_dll(lib, 'g_shot_block_tile').value]): bad.append('tiles')
    for k, pn in (('dyn', 0x7056), ('alt', 0x705E), ('fg', 0x7062), ('bg', 0x7066)):
        seg = struct.unpack('<HH', mem(DS * 16 + pn, 4))[1]
        p = ctypes.c_void_p.in_dll(lib, 'g_map').value  # not used
    gm = ds.__dict__.get('gm') or da.gm
    for k, pn in (('dyn', 0x7056), ('alt', 0x705E), ('fg', 0x7062), ('bg', 0x7066)):
        seg = struct.unpack('<HH', mem(DS * 16 + pn, 4))[1]
        if mem(seg * 16, N) != bytes((ctypes.c_char * N).from_address(getattr(gm, k))): bad.append(k)
    iseg = struct.unpack('<HH', mem(DS * 16 + 0x705A, 4))[1]
    if mem(iseg * 16, 2 * N) != bytes((ctypes.c_char * (2 * N)).from_address(ctypes.c_void_p.in_dll(lib, 'g_map_info').value)): bad.append('info')
    if mem(DS * 16 + 0x6D6C, 720) != bytes((ctypes.c_char * 720).from_address(gm.anim)): bad.append('anim')
    if mem(DS * 16 + 0xA552, 240) != bytes((ctypes.c_char * 240).from_address(ctypes.c_void_p.in_dll(lib, 'g_enemy_defs').value)): bad.append('defs')
    for nm, off in (('player_x', 0xCF32), ('player_y', 0xCF28)):
        if o.rw(off) != (getattr(dc.cam, nm) & 0xFFFF): bad.append(nm)
    for nm, off, val in (('check_x', 0xCF52, lv.check_x), ('check_y', 0xCF50, lv.check_y), ('goal', 0xCE68, lv.goal), ('collected', 0xCE6A, lv.collected), ('complete', 0xCFD8, lv.complete), ('kills', 0xCE6C, kills.value), ('total', 0xCE6E, total.value), ('fire', 0x7042, ctypes.c_int16.in_dll(lib, 'g_enemy_fire_chance').value)):
        if o.rw(off) != (val & 0xFFFF): bad.append(nm)
    if perturb: dat = real; lib.dat_open((ROOT + '/HOCUS.DAT').encode())
    if bad: print(f'  episode {epi_n} level {lvl_n}: MISMATCH {bad}')
    return bool(bad)
if __name__ == '__main__':
    bad = sum(run(e, l, p) for p in range(3) for e in range(4) for l in range(9)); print(f'108 level loads, {bad} mismatches'); sys.exit(1 if bad else 0)
