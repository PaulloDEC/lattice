#!/usr/bin/env python3
"""Differential test: 0BA5:5F6A..6700 (walk, jump arc, fall, lift platforms, teleport slide, look margin) vs player_move()."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import o_bytes, c_bytes
from diff_level import lv, LV, N
import diff_animate as da
from range_harness import run_range, local
from diff_video import snapshot
lib.player_move.argtypes = [ctypes.c_int]
class Inp(ctypes.Structure): _fields_ = [(n, ctypes.c_uint8) for n in ('left','right','down','scroll_up','scroll_down','jump','fire','up','up2','latch_jump','latch_fire','latch_up')]
gin = Inp.in_dll(lib, 'g_in')
class Joy(ctypes.Structure): _fields_ = [(n, ctypes.c_int16) for n in ('left','right','up','down','swap','btn1','btn2','scroll_down','scroll_up')]
joy = Joy.in_dll(lib, 'g_joy')
class Fr(ctypes.Structure): _fields_ = [('rec', ctypes.c_uint8 * 6), ('demo_pos', ctypes.c_int16), ('fire_held', ctypes.c_uint8), ('up_held', ctypes.c_uint8), ('local_c', ctypes.c_uint8), ('anim_toggle', ctypes.c_uint8), ('jump_end', ctypes.c_uint8), ('landed', ctypes.c_uint8)]
fr = Fr.in_dll(lib, 'g_frame')
MV = ['fall', 'jump_idx', 'jump_len', 'jump_peak', 'jump_tab', 'step_r_up', 'step_l_up', 'free_r', 'free_l', 'moved', 'look_off', 'plat_down', 'plat_up', 'plat_tile_a', 'plat_tile_b']
class Mv(ctypes.Structure): _fields_ = [(n, ctypes.c_void_p if n == 'jump_tab' else ctypes.c_int16) for n in MV]
mv = Mv.in_dll(lib, 'g_mv')
MVOFF = dict(fall=0xCF08, jump_idx=0xCF16, jump_len=0xCF12, jump_peak=0xCF14, step_r_up=0xCF1E, step_l_up=0xCF20, free_r=0xCF22, free_l=0xCF24, moved=0xCF1C,
             look_off=0xCF34, plat_down=0xCF54, plat_up=0xCF56, plat_tile_a=0xCF4A, plat_tile_b=0xCF4C)
TABS = {0x2C10: ctypes.addressof(ctypes.c_int16.in_dll(lib, 'g_jump_tab_normal')), 0x2BDE: ctypes.addressof(ctypes.c_int16.in_dll(lib, 'g_jump_tab_power'))}
INB = [('left', 0x70AC), ('right', 0x70AB), ('down', 0x70AD), ('jump', 0x70A9), ('up', 0x70AE), ('up2', 0x70AF), ('latch_up', 0x70A7), ('scroll_down', 0x70A3), ('scroll_up', 0x70A4)]
JO = [('scroll_down', 0x78F6), ('scroll_up', 0x78F4)]
CAM = [('player_x', 0xCF32), ('player_y', 0xCF28), ('player_row', 0xCF2E), ('vfreeze', 0xCF0C), ('vscroll', 0xCF0A), ('vmargin', 0xCF36)]
LVN = [('teleport', 0xCF06), ('tele_col2', 0xCF02), ('tele_row_px', 0xCF04), ('facing', 0xCE90), ('ce92', 0xCE92), ('item4_flag', 0xCF10), ('death_timer', 0xCE8C),
       ('finish_timer', 0xCE8A), ('hint_shown', 0xCF4E)]

def run(seed):
    r = random.Random(seed); st = rnd_state(r); mode = r.choice([0, 0, 1, 2])
    px = r.choice([0, 1, 0x1DC, 0x1DD, 0x1DE, r.randrange(0, 0x1E0), r.randrange(2, 100)])
    row = r.randrange(1, 50); py = row * 16 + r.choice([0, 0, 0, r.randrange(16), -16, 16, 1, 15]) if r.random() < .9 else r.randrange(0, 900)
    prow = row if r.random() < .9 else r.randrange(1, 50)
    tabs = r.choice([0x2C10, 0x2BDE]); ln = 0x13 if tabs == 0x2C10 else 0x19
    cam = dict(player_x=px, player_y=py, player_row=prow, vfreeze=r.choice([0, 0, 1, 1]), vscroll=r.choice([0, 0, 0, 1]), vmargin=r.randrange(0, 10))
    I = {n: r.choice([0, 0, 1]) for n, _ in INB}; I['latch_up'] = r.randrange(2)
    if r.random() < .5: I['scroll_up'] = I['scroll_down'] = 0
    J = {n: r.choice([0, 0, 0, 1]) for n, _ in JO}
    ta, tb = r.choice([7, 8, 100]), r.choice([8, 9, 100, -1, -1, 200])
    M = dict(fall=r.choice([0, 0, 0, 1]), jump_idx=r.randrange(0, ln + 2), jump_len=ln, jump_peak=9 if tabs == 0x2C10 else 0xC, step_r_up=r.randrange(2), step_l_up=r.randrange(2),
             free_r=r.randrange(2), free_l=r.randrange(2), moved=r.randrange(2), look_off=r.choice([0, 0, 0, 1]), plat_down=r.randrange(0, 12), plat_up=r.randrange(0, 12),
             plat_tile_a=ta, plat_tile_b=tb)
    L = dict(teleport=r.choice([0, 0, 0, 1, 0x18, 0x19, 0x1A, 0x27, 0x28]), tele_col2=px + r.choice([0, 0, 3, -3, 1, -1]), tele_row_px=py + r.choice([0, 0, 16, -16, 32, -32]),
             facing=r.randrange(2), ce92=r.randrange(0, 12), item4_flag=r.randrange(2), death_timer=r.choice([0, 0, 0, 4]), finish_timer=r.choice([0, 0, 0, 4]), hint_shown=r.choice([0, 0, 0, 1]))
    h15, h17, h18 = r.randrange(0, 65536), r.randrange(0, 12), r.randrange(0, 14)
    fire_held = r.randrange(2); recb = bytes(r.randrange(2) for _ in range(6)); loc_e = r.randrange(2); loc_f = r.randrange(2)
    focus = r.choice([None, None, 'lift', 'lift', 'step', 'step', 'jump', 'jump', 'fall'])
    if focus:
        L['teleport'] = 0; L['death_timer'] = L['finish_timer'] = 0
        if focus in ('lift', 'step', 'fall'):
            cam['vfreeze'] = 0; cam['vscroll'] = 0; M['fall'] = 0; I['jump'] = 0
            py = prow * 16; cam['player_y'] = py
        if focus == 'lift':
            L['hint_shown'] = 0
            if tb == -1: M['plat_tile_b'] = tb = 9
            I['up'] = r.randrange(2); I['up2'] = r.randrange(2); I['down'] = r.randrange(2)
            if r.random() < .5: I['up'] = I['up2'] = 0
        if focus == 'step':
            I['left'], I['right'] = r.choice([(1, 0), (0, 1), (1, 1)]); M['moved'] = 0
            L['facing'] = 1 if I['left'] and not I['right'] else 0 if I['right'] and not I['left'] else r.randrange(2)
            px = r.randrange(2, 0x1DC); cam['player_x'] = px
            L['tele_col2'] = px; L['tele_row_px'] = py
        if focus == 'jump':
            cam['vfreeze'] = 1; M['jump_idx'] = r.randrange(0, ln); I['jump'] = 0
    # fg layer window around the player
    fgd = bytearray([0xFF]) * N
    pc = max(0, (px + 1) // 2); base = prow * 240 + pc
    pa = r.choice([0.15, 0.3, 0.5])
    for rr in range(-3, 6):
        for cc in range(-3, 5):
            i = base + rr * 240 + cc
            if 0 <= i < N and r.random() < pa: fgd[i] = r.choice([0, 1, 5, ta, tb & 0xFF, 0x30])
    if r.random() < .5:      # plant lift platforms under the player
        for off in (0x1E0, 0x1DF, 0x1E1):
            i = base + off
            if 0 <= i < N - 2: fgd[i] = r.choice([ta, tb & 0xFF, 3]); fgd[i + 1] = r.choice([ta, tb & 0xFF, 0xFF])
        for off in (0x1E0 - 0x2D0, 0x1E0 - 0x2D0 - 1, 0x1E0 - 0x2D0 - 2):
            i = base + off
            if 0 <= i < N - 2 and r.random() < .7: fgd[i] = fgd[i + 1] = 0xFF
        for off in (0x1E0 + 0xF0, 0x1E0 + 0xEF, 0x1E0 + 0xF1):
            i = base + off
            if 0 <= i < N - 2 and r.random() < .7: fgd[i] = fgd[i + 1] = 0xFF
    pc = max(0, (px + 1) // 2); base = prow * 240 + pc
    def setc(i, v):
        if 0 <= i < N: fgd[i] = v
    if focus == 'lift':
        t0 = r.choice([ta, tb & 0xFF]); pos = base + 0x1E0
        setc(pos, t0); setc(pos - 1 if t0 == ta else pos + 1, tb & 0xFF if t0 == ta else ta)
        for off in ((-0x2D0, -0x2CF), (-0x2D0 - 1, -0x2D0), (0xF0, 0xF1), (0x2D0 - 1, 0x2D0)):
            if r.random() < .8:
                for k in off: setc(pos + k - (1 if t0 == ta else 0) if False else base + 0x1E0 + k - (1 if t0 == ta else 0), 0xFF)
        for off in (0xF0, 0xF1, 0xEF):
            if r.random() < .7: setc(base + 0x1E0 + off, 0xFF)
        for off in (0xEE, 0xF2, 0xF3, 0x2D2, 0x2D1, -0x2CE, -0x2CD):
            setc(base + 0x1E0 + off, r.choice([0xFF, 0, 0xFF]))
    if focus == 'step':
        col = base - (pc - (px // 2 if I['left'] and not I['right'] else (px + 2) // 2)) if True else base
        col = prow * 240 + (px // 2 if (I['left'] and not (I['right'] and L['facing'] == 0)) and L['facing'] == 1 else (px + 2) // 2)
        for rr, v in ((-1, 0xFF), (0, 0xFF), (1, r.choice([0, 0xFF, 5])), (2, r.choice([0, 0xFF])), (3, 0)):
            setc(col + rr * 240, v)
        for c in (base, base + 240): setc(c + 0x1E0 - 240 * 0 + 0, 0)
        setc(base + 0x1E0, r.choice([0, 0, 0xFF])); setc(base + 0x1E0 + 0xF0, 0)
    if focus == 'jump':
        setc(prow * 240 + pc, r.choice([0, 0, 0xFF])); setc(max(0, ((py + 8) // 16)) * 240 + pc, r.choice([0, 0xFF]))
        setc(base + 0x1E0, r.choice([0, 0xFF]))
    if focus == 'fall':
        setc(base + 0x1E0, 0xFF); setc(base + 0x1E0 + 0xF0, r.choice([0xFF, 0xFF, 0]))
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for k, seg in da.SEGS.items(): w(da.PTR[k], struct.pack('<HH', 0, seg))
    o.uc.mem_write(da.SEGS['fg'] * 16, bytes(fgd))
    for n, off in INB: w(off, bytes([I[n]]))
    for n, off in JO: w(off, struct.pack('<h', J[n]))
    for n, off in CAM: w(off, struct.pack('<h', cam[n]))
    for n, off in LVN: w(off, struct.pack('<h', L[n]))
    for n, off in MVOFF.items(): w(off, struct.pack('<h', M[n]))
    w(0xCF18, struct.pack('<HH', tabs, DS)); w(0xC1BE, struct.pack('<H', h15)); w(0xC1C2, struct.pack('<H', h17)); w(0xC1C4, struct.pack('<H', h18))
    w(0xCF49, bytes([fire_held])); w(0xCF40, recb)
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    run_range(o, 0x5F6A, 0x6700, mode=mode, locals_={-0xe: bytes([loc_e]), -0xf: bytes([loc_f])}); sa = snapshot({})
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    buf = ctypes.create_string_buffer(bytes(fgd), N); da.gm.fg = ctypes.addressof(buf)
    for n, _ in INB: setattr(gin, n, I[n])
    for n, _ in JO: setattr(joy, n, J[n])
    for n, v in cam.items(): setattr(dc.cam, n, v)
    for n, v in L.items(): setattr(lv, n, v)
    for n, v in M.items(): setattr(mv, n, v)
    mv.jump_tab = TABS[tabs]
    ds.spr.hdr[15] = h15; ds.spr.hdr[17] = h17; ds.spr.hdr[18] = h18
    fr.fire_held = fire_held
    for i in range(6): fr.rec[i] = recb[i]
    fr.jump_end = loc_e; fr.landed = loc_f
    csnd.clear(); lib.player_move(mode); sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    for n, off in INB:
        if o.dsb(off, 1)[0] != getattr(gin, n): bad.append(n)
    for n, off in CAM:
        if o.rw(off) != (getattr(dc.cam, n) & 0xFFFF): bad.append(n)
    for n, off in LVN:
        if o.rw(off) != (getattr(lv, n) & 0xFFFF): bad.append(n)
    for n, off in MVOFF.items():
        if o.rw(off) != (getattr(mv, n) & 0xFFFF): bad.append(n)
    to, ts = struct.unpack('<HH', o.dsb(0xCF18, 4))
    if ts != DS or TABS.get(to) != mv.jump_tab: bad.append('tab')
    if o.dsb(0xCF40, 6) != bytes(fr.rec): bad.append('rec')
    if local(o, -0xe, 1)[0] != fr.jump_end: bad.append('jump_end')
    if local(o, -0xf, 1)[0] != fr.landed: bad.append('landed')
    if bytes(o.uc.mem_read(da.SEGS['fg'] * 16, N)) != buf.raw[:N]: bad.append('fg')
    if o.snd != csnd: bad.append('sound')
    if bad: print(f'  seed {seed} mode={mode}: MISMATCH {sorted(set(bad))[:8]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(71000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
