#!/usr/bin/env python3
"""Differential test: 0BA5:4549..4D44 (level setup) vs level_setup().  External loaders (pcx / pal / img / sprites / music /
input) are stubbed on both sides and their call sequences compared; everything else (every DS variable and pool the setup
touches, VGA state, palette, RNG index, clock) is compared after the run.  All 36 levels x 3 modes, random prior state."""
import sys, struct, random, ctypes, os
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import o_bytes, c_bytes
from diff_level import lv, LV
from diff_player_hits import pl
import diff_move as dm
import explore_setup as ex
from range_harness import run_range, local
from diff_video import snapshot
ROOT = ex.ROOT
lib.dat_open.argtypes = [ctypes.c_char_p]; assert lib.dat_open((ROOT + '/HOCUS.DAT').encode()) == 0
lib.level_setup.argtypes = [ctypes.c_int]; lib.level_setup.restype = ctypes.c_int
class Hooks(ctypes.Structure):
    _fields_ = [('music', ctypes.c_void_p), ('pcx', ctypes.c_void_p), ('pal', ctypes.c_void_p), ('img', ctypes.c_void_p), ('sprites', ctypes.c_void_p), ('input', ctypes.c_void_p), ('demo', ctypes.c_void_p)]
hk = Hooks.in_dll(lib, 'g_level_hooks'); calls = []
F2 = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int); F3 = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int, ctypes.c_int); F1 = ctypes.CFUNCTYPE(None, ctypes.c_void_p); F0 = ctypes.CFUNCTYPE(None)
_keep = [F2(lambda e, l: calls.append(('music', e, l))), F2(lambda e, a: calls.append(('pcx', e, a))), F3(lambda e, s, a: calls.append(('pal', e, s, a))),
         F3(lambda e, x, y: calls.append(('img', e, x, y))), F1(lambda p: calls.append(('sprites',))), F0(lambda: calls.append(('inp200',)))]
hk.music, hk.pcx, hk.pal, hk.img, hk.sprites, hk.input = [ctypes.cast(k, ctypes.c_void_p).value for k in _keep]
ids_p = (ctypes.c_int16 * 10).in_dll(lib, 'g_sprite_ids'); demo_len = ctypes.c_int16.in_dll(lib, 'g_demo_len')
epi = ctypes.c_int16.in_dll(lib, 'g_episode')
class Sh(ctypes.Structure): _fields_ = [('x', ctypes.c_int16 * 10), ('power', ctypes.c_int16 * 10), ('count', ctypes.c_int16), ('up', ctypes.c_int16 * 10), ('dir', ctypes.c_int16 * 10), ('cursor', ctypes.c_int16)]
shots = Sh.in_dll(lib, 'g_shots'); boss = ctypes.c_int16.in_dll(lib, 'g_boss_phase')
lib.timer_clock.restype = ctypes.c_uint32
W = lambda o, off, fmt, *v: o.uc.mem_write(DS * 16 + off, struct.pack(fmt, *v))

def run(epi_n, lvl_n, mode, seed):
    r = random.Random(seed); st = rnd_state(r)
    lib.vga_reset(); o = ex.OP(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    for k, seg in ex.da.SEGS.items(): W(o, ex.da.PTR[k], '<HH', 0, seg)
    W(o, 0x705A, '<HH', 0, 0x5000); W(o, 0x7056, '<HH', 0, 0x4000); W(o, 0x7070, '<%dx' % 160)
    W(o, 0x706E, '<h', epi_n); W(o, 0x706C, '<h', lvl_n); W(o, 0x48C6, '<HH', 0, 0x6000)
    # random prior values of everything the setup is expected to set, identical on both sides
    rv = lambda: r.choice([0, 1, 5, r.randrange(1, 300), -1, 99])
    vals = []
    for n, off in LV:
        if n in ('level_icon', 'score', 'tile_blank', 'tile_lever_a', 'tile_lever_b'): continue
        vals.append((lv, n, off, rv()))
    for n, off in dc.CAM: vals.append((dc.cam, n, off, rv() if n not in ('cam_row', 'cam_x') else r.randrange(0, 40)))
    for n, off in dm.MVOFF.items(): vals.append((dm.mv, n, off, rv()))
    vals += [(pl, 'health', 0x704C, rv()), (pl, 'invuln', 0xCE74, rv()), (pl, 'shield', 0xCE8E, rv()), (dm.fr, 'demo_pos', 0xCF3E, rv())]
    for obj, n, off, v in vals:
        setattr(obj, n, v); W(o, off, '<h', v)
    lv.level_icon = lvl_n; epi.value = epi_n
    for off, p in ((0xCF49, 'fire_held'), (0xCF48, 'up_held')): v = r.randrange(2); setattr(dm.fr, p, v); W(o, off, '<B', v)
    am, la = r.randrange(2), r.randrange(2); dm.fr.anim_toggle = am; dm.fr.landed = la
    dp = ctypes.c_int16.in_dll(lib, 'g_demo_playback'); dp.value = r.randrange(2); W(o, 0x790C, '<h', dp.value)
    sx = [r.choice([-99, r.randrange(0, 80)]) for _ in range(10)]; W(o, 0xCEEE, '<10h', *sx); W(o, 0xCEAE, '<h', 7); W(o, 0xCEB0, '<h', 3)
    for i in range(10): shots.x[i] = sx[i]
    pw_ = [r.randrange(0, 3) for _ in range(10)]; W(o, 0xCE9A, '<10h', *pw_)
    for i in range(10): shots.power[i] = pw_[i]
    shots.count = 7; shots.cursor = 3
    W(o, 0xCF46, '<h', 2); boss.value = 2; W(o, 0xCF3C, '<h', 77); demo_len.value = 77
    h15 = r.randrange(0, 65536); W(o, 0xC1BE, '<H', h15)
    spr_hdr = (ctypes.c_uint16 * 30).from_address(ctypes.addressof(ctypes.c_uint16.in_dll(lib, 'g_spr')) + ctypes.sizeof(ctypes.c_void_p) * 40 + 2 + 2 + 80)
    spr_hdr[15] = h15
    base_clock = r.randrange(0, 1000)
    W(o, 0xCFD4, '<I', 12345); ctypes.c_uint32.in_dll(lib, 'g_enemy_sound_deadline').value = 12345
    W(o, 0x7908, '<I', base_clock); lib.timer_set_clock(base_clock)
    # pools: identical random enemy / projectile contents
    blob = bytes(r.randrange(256) for _ in range(528)); W(o, 0xBF64, '<528s', blob); ctypes.memmove(ctypes.addressof(ds.en), blob, 528)
    pblob = bytes(r.randrange(256) for _ in range(240)); W(o, 0xBE74, '<240s', pblob); ctypes.memmove(ctypes.addressof(ctypes.c_char.in_dll(lib, 'g_proj')), pblob, 240)
    load_state(st, None)
    # (load_state restores the effect pools on the port side; the original got them above)
    o.ext.clear()
    run_range(o, 0x4549, 0x4D44, mode=mode, locals_={-0xD: bytes([am]), -0xF: bytes([la])}, count=300_000_000)
    sa = snapshot({})
    # port
    calls.clear()
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    rc = lib.level_setup(mode); sb = snapshot({}); bad = []
    if rc: bad.append('rc')
    ext = [e for e in o.ext if not (e[0] == 'read' and e[1] >= 131)]
    demo = [e[1] for e in ext if e[0] == 'read']; ext = [e for e in ext if e[0] != 'read']
    if ext != calls: bad.append('calls')
    if demo != ([0x12 + lvl_n // 2] if mode == 2 else []): bad.append('demo-entry')
    bad += [k for k in sa if sa[k] != sb[k]]
    mem = lambda off, n: bytes(o.uc.mem_read(DS * 16 + off, n))
    if mem(0xC174, 20) != bytes(ids_p): bad.append('sprite ids %s %s' % (struct.unpack('<10h', mem(0xC174, 20)), list(ids_p)))
    for obj, n, off, _ in vals:
        pv = getattr(obj, n)
        if (pv & 0xFFFF) != o.rw(off): bad.append(n)
    pw = lambda off: o.rw(off)
    pe = bytes((ctypes.c_char * 528).from_address(ctypes.addressof(ds.en)))
    if mem(0xBF64, 528) != pe:
        bad.append('enemies')
        if os.environ.get('V'): print([hex(0xBF64 + i) for i in range(528) if mem(0xBF64, 528)[i] != pe[i]][:20])
    if mem(0xBE74, 240) != bytes((ctypes.c_char * 240).from_address(ctypes.addressof(ctypes.c_char.in_dll(lib, 'g_proj')))): bad.append('projectiles')
    if struct.unpack('<10h', mem(0xCEEE, 20)) != tuple(shots.x): bad.append('shots.x')
    if struct.unpack('<10h', mem(0xCE9A, 20)) != tuple(shots.power): bad.append('shots.power')
    if pw(0xCEAE) != shots.count & 0xFFFF: bad.append('shots.count')
    if pw(0xCEB0) != shots.cursor & 0xFFFF: bad.append('shots.cursor')
    if pw(0xCF46) != boss.value & 0xFFFF: bad.append('boss')
    if pw(0xCF3C) != demo_len.value & 0xFFFF: bad.append('demo_len')
    if pw(0x790C) != dp.value & 0xFFFF: bad.append('playback')
    if mem(0xCF49, 1)[0] != dm.fr.fire_held or mem(0xCF48, 1)[0] != dm.fr.up_held: bad.append('held')
    if local(o, -0xD, 1)[0] != dm.fr.anim_toggle: bad.append('anim_toggle')
    if local(o, -0xF, 1)[0] != dm.fr.landed: bad.append('landed')
    if local(o, -0xB, 2) != b'\0\0': bad.append('loop-exit local')
    if pw(0x7118) != lib.rng_index() & 0xFFFF: bad.append('rng')
    if struct.unpack('<I', mem(0x7908, 4))[0] != lib.timer_clock(): bad.append('clock')
    if struct.unpack('<I', mem(0xCFD4, 4))[0] != ctypes.c_uint32.in_dll(lib, 'g_enemy_sound_deadline').value: bad.append('deadline')
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k]]
    if os.environ.get('V'):
        for k in ob:
            if ob[k] != cb_[k]: print(k, ob[k][:40].hex(), cb_[k][:40].hex())
    if bad: print(f'  e{epi_n} l{lvl_n} mode {mode}: MISMATCH {sorted(set(bad))[:10]}')
    return bool(bad)

if __name__ == '__main__':
    allc = [(e, l, m) for e in range(4) for l in range(9) for m in range(3)]
    if len(sys.argv) > 1:
        k = int(sys.argv[1]); allc = [(3, 8, 0), (3, 8, 2), (1, 4, 2)] + allc[::max(1, len(allc) // k)]
    bad = 0
    for e, l, m in allc: bad += run(e, l, m, 500 + e * 100 + l * 3 + m)
    print(f'{len(allc)} setups, {bad} mismatches'); sys.exit(1 if bad else 0)
