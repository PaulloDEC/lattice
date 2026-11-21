#!/usr/bin/env python3
"""Differential test: 0BA5:126C (per-frame update of all enemy slots) vs enemies_update().
Everything random: 8 slots of all types (incl. type 8 and the boss), fg tile layer, shots, projectile pool,
sheet headers, defs.  The original's stack is zeroed before the call so its uninitialised locals start at 0,
as the port's do.  Compared: the whole enemy table + pending list (flat), projectile pool, effect pools, VRAM
(boss bars), RNG, both draw hooks, sound, boss phase, sound deadline."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import fx, o_bytes, c_bytes, c_calls
from diff_player_hits import pj, PJ
import diff_enemy_bar as db
import diff_enemy_type8 as d8
from diff_enemy_type8 import OS2, c_solid
from diff_enemy_shots import shots
import diff_projectiles as dpr
from diff_video import snapshot, SEG_VIDEO, BASE_SEG, STACK_SEG
lib.enemies_update.argtypes = []
PH = ctypes.c_int16.in_dll(lib, 'g_boss_phase'); FC = ctypes.c_int16.in_dll(lib, 'g_enemy_fire_chance')
FG_SEG = 0x5800
TABLE = 29 * 8 + 32
NDEF = 12
big_defs = (ctypes.c_int16 * (12 * NDEF))()
ctypes.c_void_p.in_dll(lib, 'g_enemy_defs').value = ctypes.addressof(big_defs)
LAST = {}

def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    camx = r.randrange(0, 400); camrow = r.randrange(0, 45); pxc = r.choice([r.randrange(0, 480), 2 * camx + r.randrange(0, 80)])
    E = {n: [0] * 8 for n in NAMES}
    for s in range(8):
        E['type'][s] = r.choice([-1, 0, 0, 1, 1, 2, 3, 4, 5, 6, 7, 7, 7, 8, 9, 0x63 if s == 0 else 7])
        E['x'][s] = r.choice([2 * camx + r.randrange(-10, 90), r.randrange(8, 900)])
        E['y'][s] = r.choice([camrow * 16 + r.randrange(0, 160), r.randrange(32, 860)])
        E['box_w'][s] = r.randrange(2, 40); E['box_h'][s] = r.randrange(2, 40)
        E['vx'][s] = r.randrange(-3, 4); E['vy'][s] = r.randrange(-6, 7)
        E['bfa4'][s] = r.choice([-99, -99, -99, r.randrange(-6, 8)])
        E['bf64'][s] = r.choice([0, 0, 0, 0, 1, 2, 9]); E['bfb4'][s] = r.choice([0, 0, 0, 1, 3]); E['bfc4'][s] = r.randrange(0, 3)
        E['bf84'][s] = r.choice([0] * 6 + [1, 3]); E['bf94'][s] = r.choice([0, 0, 0, 1, 5]); E['c124'][s] = r.randrange(2)
        E['c104'][s] = r.randrange(2); E['c114'][s] = r.randrange(2); E['c0f4'][s] = r.choice([0, 0, 1, 2, 5])
        E['timer'][s] = r.choice([0, 0, r.randrange(-1, 10)]); E['facing'][s] = r.randrange(2); E['c034'][s] = r.randrange(-1, 9)
        E['flash'][s] = r.randrange(0, 6); E['sheet'][s] = r.randrange(0, 10); E['def'][s] = r.randrange(0, 6)
        E['hp'][s] = r.randrange(-3, 700); E['hp_max'][s] = r.choice([0, 5, 20, 21, r.randrange(1, 700)])
        E['c0d4'][s] = r.randrange(-3, 4); E['c0c4'][s] = r.choice([0, 0, 3]); E['bf74' if False else 'hp_max'][s] = E['hp_max'][s]
        for n in ('bf94', 'cell', 'c044' if False else 'cell', 'c0e4', 'c0f4'): pass
    LAST['E'] = {k: list(v) for k, v in E.items()}
    pend = [r.choice([-1, r.randrange(0, 14400)]) for _ in range(32)]
    phase = r.choice([0, 1, 2, 3]); dif = r.randrange(0, 4); clock = r.randrange(0, 1 << 32); dl = r.choice([0, clock])
    fire = r.randrange(1, 6)
    defs = [r.choice([0, 0, r.randrange(-3, 4)]) if k == 2 else r.randrange(-30, 30) for _ in range(NDEF) for k in range(12)]
    for d in range(NDEF): defs[d * 12 + 11] = r.choice([0x63, 2, 7])
    hdr = [[r.randrange(0, 65536) for _ in range(30)] for _ in range(10)]
    for k in range(10):
        hdr[k][17] = r.randrange(0, 4); hdr[k][18] = r.randrange(2, 9); hdr[k][19] = r.randrange(0, 9)
        hdr[k][21] = r.choice([0xFFFF, r.randrange(3, 7)]); hdr[k][22] = r.randrange(4, 10)
    hdr[1][13] = st['sb']; hdr[3][13] = st['sc']
    P = {n: [0] * 8 for n in PJ}
    for i in range(8):
        P['type'][i] = r.choice([-1, -1, -1, 0])
        for n in PJ:
            if n != 'type': P[n][i] = r.randrange(-50, 50)
    sx = [r.choice([-99, r.randrange(-20, 120), E['x'][r.randrange(8)] + r.randrange(-12, 12)]) for _ in range(10)]
    fg = bytes(0xFF if r.random() < .55 else r.randrange(0, 255) for _ in range(14400)); LAST['fg'] = fg
    # ---- original
    lib.vga_reset(); o = OS2(); o.call(de.SEG_VIDEO, 0x185); db.patch(o); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for n, off in zip(NAMES, OFFS): w(off, struct.pack('<8h', *E[n]))
    w(0xC134, struct.pack('<32h', *pend))
    for n, off in PJ.items(): w(off, struct.pack('<8h', *P[n]))
    w(0xA552, struct.pack('<%dh' % len(defs), *defs)); w(0x706A, struct.pack('<h', dif)); w(0x7042, struct.pack('<h', fire))
    w(0x7908, struct.pack('<I', clock)); w(0xCFD4, struct.pack('<I', dl)); w(0xCF46, struct.pack('<h', phase))
    w(0xCE80, struct.pack('<h', camx)); w(0xCE7A, struct.pack('<h', camrow)); w(0xCF2C, struct.pack('<h', pxc))
    w(0xCEEE, struct.pack('<10h', *sx))
    for k in range(10): w(0xC1A0 + k * 220, struct.pack('<30H', *hdr[k]))
    w(0x7062, struct.pack('<HH', 0, FG_SEG + BASE_SEG)); o.uc.mem_write((FG_SEG + BASE_SEG) * 16, fg + b'\xff' * (0x10000 - len(fg)))   # past the layer = empty, the port's convention
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185)
    o.uc.mem_write(STACK_SEG * 16, bytes(0x1000))          # stale locals start at 0, as in the port
    o.call(SEG_GAME, 0x126C, ()); sa = snapshot({})
    # ---- port
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for n in NAMES:
        for i in range(8): getattr(en, n)[i] = E[n][i]
    for i in range(32): en.pending[i] = pend[i]
    for n in PJ:
        for i in range(8): getattr(pj, n)[i] = P[n][i]
    ctypes.memmove(big_defs, struct.pack('<%dh' % len(defs), *defs), len(defs) * 2)
    ds.diff.value = dif; FC.value = fire; lib.timer_set_clock(clock); ds.ddl.value = dl; PH.value = phase
    dc.cam.cam_x = camx; dc.cam.cam_row = camrow; dc.cam.player_xcol = pxc
    for i in range(10): shots.x[i] = sx[i]
    for k in range(10):
        for j in range(30): ds.spr.hdr[k * 30 + j] = hdr[k][j]
    ctypes.memmove(dpr.fg_buf, fg, 14400)
    c_calls.clear(); c_solid.clear(); csnd.clear(); bad = []
    lib.enemies_update(); sb = snapshot({})
    bad += [k for k in sa if sa[k] != sb[k]]
    ot = struct.unpack('<%dh' % TABLE, o.dsb(0xBF64, TABLE * 2)); ct = tuple(ctypes.cast(ctypes.addressof(en), ctypes.POINTER(ctypes.c_int16 * TABLE)).contents)
    for i in range(TABLE):
        if ot[i] != ct[i]: bad.append(('pending' if i >= 232 else NAMES[i // 8]))
    for n, off in PJ.items():
        if struct.unpack('<8h', o.dsb(off, 16)) != tuple(getattr(pj, n)): bad.append('proj_' + n)
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k] and k != 'rng']
    if o.rw(0x7118) != (lib.rng_index() & 0xFFFF): bad.append('rng')
    if o.calls != list(c_calls): bad.append('draw')
    if o.solid != c_solid: bad.append('solid')
    if o.snd != csnd: bad.append('sound')
    if o.rw(0xCF46) != (PH.value & 0xFFFF): bad.append('phase')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]} types={E["type"]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    bad = sum(run(27000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
