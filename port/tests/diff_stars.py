#!/usr/bin/env python3
"""Differential test: star field update 06B8:0103 and init 06B8:039C vs stars_step() / stars_init(), and the start-up table build
(float table * 1e6 truncated) vs an x87-precision recomputation."""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS, BASE_SEG, E6
from diff_video import snapshot, SEG_VIDEO
import diff_run as dr
import numpy as np
SEG6 = 0x6B8 + BASE_SEG
lib.stars_set_table.argtypes = [ctypes.c_char_p]
dat = dr.dat; ent5 = dr.ent(5)
lib.stars_set_table(ent5)
TRIG = (ctypes.c_int32 * 720).in_dll(lib, 'g_trig')
# table check against numpy longdouble
fl = struct.unpack('<720f', ent5)
bad = [i for i in range(720) if int(np.longdouble(fl[i]) * np.longdouble(1000000.0)) != TRIG[i]]
print('trig table mismatches:', len(bad))
ANG = (ctypes.c_uint16 * 75).in_dll(lib, 'g_star_ang'); RAD = (ctypes.c_uint16 * 75).in_dll(lib, 'g_star_rad')
SPD = (ctypes.c_uint8 * 75).in_dll(lib, 'g_star_spd'); BR = (ctypes.c_uint8 * 75).in_dll(lib, 'g_star_br'); SP2 = (ctypes.c_uint8 * 75).in_dll(lib, 'g_star_spd2')
OFFS = dict(ang=(0x7B6C, 150), rad=(0x7AD6, 150), spd=(0x7A8A, 75), br=(0x7A3F, 75), sp2=(0x79F4, 75))

def noise(r):
    for p in range(4):
        buf = bytes(r.choice([0, 0, 0, r.randrange(0xF0, 256), r.randrange(256)]) for _ in range(0x10000))
        ctypes.memmove(lib.vga_plane(p), buf, 0x10000)

def run(seed, init):
    r = random.Random(seed); steps = r.randrange(1, 4)
    ang = []; rad = []
    while len(ang) < 75:                                  # stars always start inside the screen (init / respawn guarantee it)
        a = r.randrange(360); rr = r.randrange(0, 200)
        x = int(TRIG[a * 2] * rr / 1000000) + 0xA0; y = int(TRIG[a * 2 + 1] * rr / 1000000) + 0x70
        if 0 <= x <= 0x13F and 0 <= y <= 0xC7: ang.append(a); rad.append(rr)
    spd = [r.randrange(1, 6) for _ in range(75)]; br = [r.randrange(256) for _ in range(75)]; sp2 = [r.randrange(1, 6) for _ in range(75)]
    rngi = r.randrange(1000); page = 0
    lib.vga_reset(); o = de.O(); o.call(SEG_VIDEO, 0x185)
    W = lambda off, b: o.uc.mem_write(DS * 16 + off, b)
    W(0x4CA8, struct.pack('<720i', *TRIG)); W(0x711A, E6); W(0x7118, struct.pack('<h', rngi))
    W(0x7B6C, struct.pack('<75H', *ang)); W(0x7AD6, struct.pack('<75H', *rad)); W(0x7A8A, bytes(spd)); W(0x7A3F, bytes(br)); W(0x79F4, bytes(sp2))
    noise(random.Random(seed ^ 1)); o.call(SEG_VIDEO, 0xE4, (page,))
    # vsync wait: the C VGA model's retrace is driven by the shared model, so both sides see the same behaviour
    if init: o.uc.mem_write(SEG6 * 16 + 0x3A2, b'\xeb\x11')      # skip time() / srand()
    for _ in range(steps):
        if init: o.call(SEG6, 0x39C, ())
        else: o.call(SEG6, 0x103, ())
    sa = snapshot({}); da_ = {k: o.dsb(off, n) for k, (off, n) in OFFS.items()}; ra = o.dsb(0x7118, 2)
    lib.vga_reset(); lib.video_init(); assert lib.rng_load(E6, len(E6)) == 0; lib.rng_set_index(rngi)
    for i in range(75): ANG[i] = ang[i]; RAD[i] = rad[i]; SPD[i] = spd[i]; BR[i] = br[i]; SP2[i] = sp2[i]
    noise(random.Random(seed ^ 1)); lib.video_set_draw_page(page)
    for _ in range(steps): (lib.stars_init if init else lib.stars_step)()
    sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    cb = dict(ang=bytes(ANG), rad=bytes(RAD), spd=bytes(SPD), br=bytes(BR), sp2=bytes(SP2))
    bad += [k for k in cb if cb[k] != da_[k]]
    if ra != struct.pack('<h', lib.rng_index()): bad.append('rng')
    if bad: print(f'  seed {seed} init={init}: MISMATCH {bad[:5]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    bad = sum(run(51000 + s, s % 5 == 4) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
