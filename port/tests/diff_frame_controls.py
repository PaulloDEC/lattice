#!/usr/bin/env python3
"""Differential test: 0BA5:54AC..5824 (demo input, joystick, fire, potion timer) vs frame_controls()."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_level import lv, LV, SZ, Lvs
from diff_player_hits import pl
from range_harness import run_range, local
from diff_video import snapshot, BASE_SEG
lib.frame_controls.argtypes = [ctypes.c_int]
class Inp(ctypes.Structure): _fields_ = [(n, ctypes.c_uint8) for n in ('left','right','down','scroll_up','scroll_down','jump','fire','up','up2','latch_jump','latch_fire','latch_up')]
gin = Inp.in_dll(lib, 'g_in')
class Sh(ctypes.Structure): _fields_ = [('x', ctypes.c_int16 * 10), ('power', ctypes.c_int16 * 10), ('count', ctypes.c_int16), ('up', ctypes.c_int16 * 10), ('dir', ctypes.c_int16 * 10), ('cursor', ctypes.c_int16)]
sh = Sh.in_dll(lib, 'g_shots')
class Joy(ctypes.Structure): _fields_ = [(n, ctypes.c_int16) for n in ('left','right','up','down','swap','btn1','btn2')]
joy = Joy.in_dll(lib, 'g_joy')
class Fr(ctypes.Structure): _fields_ = [('rec', ctypes.c_uint8 * 6), ('demo_pos', ctypes.c_int16), ('fire_held', ctypes.c_uint8), ('up_held', ctypes.c_uint8), ('local_c', ctypes.c_uint8), ('anim_toggle', ctypes.c_uint8)]
fr = Fr.in_dll(lib, 'g_frame')
JE = ctypes.c_int16.in_dll(lib, 'g_joy_enabled'); DP = ctypes.c_int16.in_dll(lib, 'g_demo_playback')
DEMO_SEG = 0x5200
INB = [('left', 0x70AC), ('right', 0x70AB), ('down', 0x70AD), ('jump', 0x70A9), ('fire', 0x70AA), ('up', 0x70AE), ('up2', 0x70AF), ('latch_jump', 0x70A6), ('latch_fire', 0x70A5), ('latch_up', 0x70A7)]
JO = [('left', 0x78F2), ('right', 0x78F0), ('up', 0x78EE), ('down', 0x78EC), ('swap', 0x78EA), ('btn1', 0x78FA), ('btn2', 0x78F8)]
demo_buf = (ctypes.c_uint8 * 4096)(); ctypes.c_void_p.in_dll(lib, 'g_demo_data').value = ctypes.addressof(demo_buf)

def run(seed):
    r = random.Random(seed); st = rnd_state(r); mode = r.choice([0, 0, 1, 2, 2])
    I = {n: r.choice([0, 1]) for n, _ in INB}; J = {n: r.choice([0, 0, 1]) for n, _ in JO}
    if r.random() < .3: I['fire'] = 1
    if r.random() < .3: J['btn1'] = 1; J['btn2'] = r.randrange(2)
    rec = bytes(r.choice([0, 1]) for _ in range(6)); demo = bytes(r.randrange(2) for _ in range(4096)); pos = r.randrange(0, 600)
    g = dict(teleport=r.choice([0, 0, 0, 1]), death_timer=r.choice([0, 0, 0, 5]), finish_timer=r.choice([0, 0, 0, 5]), potion_timer=r.choice([0, 0, 1, 2, 5, 600]),
             potion_saved=r.randrange(0, 11), mana=r.choice([0, 1, 3, 5, 10]), ce94=r.choice([0, 0, 1, 3]), facing=r.randrange(2))
    je = r.choice([0, 1, 1]); dp = r.choice([0, 0, 1])
    sx = [r.choice([-99, -99, r.randrange(0, 80)]) for _ in range(10)]; cnt = r.randrange(0, 8)
    pscx = r.randrange(0, 80); pscy = r.randrange(0, 200); h25 = r.randrange(-5, 30); local_c = r.choice([0, 0, 1, 5])
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for n, off in INB: w(off, bytes([I[n]]))
    for n, off in JO: w(off, struct.pack('<h', J[n]))
    w(0xCF40, rec); w(0xCF3E, struct.pack('<h', pos)); w(0x48C6, struct.pack('<HH', 0, DEMO_SEG + BASE_SEG)); o.uc.mem_write((DEMO_SEG + BASE_SEG) * 16, demo)
    for n, off in (('teleport', 0xCF06), ('death_timer', 0xCE8C), ('finish_timer', 0xCE8A), ('potion_timer', 0xCE88), ('potion_saved', 0xCE86), ('mana', 0x7046), ('ce94', 0xCE94), ('facing', 0xCE90)):
        w(off, struct.pack('<h', g[n]))
    w(0x48CE, struct.pack('<h', je)); w(0x790C, struct.pack('<h', dp)); w(0xCEEE, struct.pack('<10h', *sx)); w(0xCEAE, struct.pack('<h', cnt))
    w(0xCF2A, struct.pack('<h', pscx)); w(0xCF26, struct.pack('<h', pscy)); w(0xC1D2, struct.pack('<h', h25))
    P = {n: [r.choice([0, 1]) for _ in range(10)] for n in ('power', 'up', 'dir')}; EY = [r.randrange(0, 200) for _ in range(10)]; cur = r.randrange(0, 12)
    for n, off in (('power', 0xCE9A), ('up', 0xCEC6), ('dir', 0xCEB2)): w(off, struct.pack('<10h', *P[n]))
    w(0xCEDA, struct.pack('<10h', *EY)); w(0xCEB0, struct.pack('<h', cur))
    f48, f49 = r.randrange(2), r.randrange(2); w(0xCF48, bytes([f48])); w(0xCF49, bytes([f49]))
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    run_range(o, 0x54AC, 0x5824, mode=mode, locals_={-0xc: bytes([local_c])}); sa = snapshot({})
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for n, _ in INB: setattr(gin, n, I[n])
    for n, _ in JO: setattr(joy, n, J[n])
    for i in range(6): fr.rec[i] = rec[i]
    fr.demo_pos = pos; fr.fire_held = f49; fr.up_held = f48; fr.local_c = local_c
    for k, v in g.items(): setattr(lv, k, v)
    JE.value = je; DP.value = dp
    for i in range(10): sh.x[i] = sx[i]; sh.power[i] = P['power'][i]; sh.up[i] = P['up'][i]; sh.dir[i] = P['dir'][i]; de.fx.e_y[i] = EY[i]
    sh.cursor = cur
    sh.count = cnt; dc.cam.pscreen_x = pscx; dc.cam.pscreen_y = pscy; ds.spr.hdr[25] = h25
    ctypes.memmove(demo_buf, demo, 4096)
    csnd.clear(); lib.frame_controls(mode); sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    for n, off in INB:
        if o.dsb(off, 1)[0] != getattr(gin, n): bad.append(n)
    if o.dsb(0xCF40, 6) != bytes(fr.rec): bad.append('rec')
    for n, off, sz in (('demo_pos', 0xCF3E, 2),):
        if o.rw(off) != (fr.demo_pos & 0xFFFF): bad.append(n)
    if o.dsb(0xCF49, 1)[0] != fr.fire_held: bad.append('fire_held')
    if o.dsb(0xCF48, 1)[0] != fr.up_held: bad.append('up_held')
    if local(o, -0xc, 1)[0] != fr.local_c: bad.append('local_c')
    for n, off in (('teleport', 0xCF06), ('death_timer', 0xCE8C), ('finish_timer', 0xCE8A), ('potion_timer', 0xCE88), ('potion_saved', 0xCE86), ('mana', 0x7046), ('ce94', 0xCE94), ('facing', 0xCE90)):
        if o.rw(off) != (getattr(lv, n) & 0xFFFF): bad.append(n)
    for n, off in (('x', 0xCEEE), ('power', 0xCE9A), ('up', 0xCEC6), ('dir', 0xCEB2)):
        if struct.unpack('<10h', o.dsb(off, 20)) != tuple(getattr(sh, n)): bad.append('shot_' + n)
    if struct.unpack('<10h', o.dsb(0xCEDA, 20)) != tuple(de.fx.e_y) : bad.append('shot_y')
    if o.rw(0xCEAE) != (sh.count & 0xFFFF): bad.append('count')
    if o.rw(0xCEB0) != (sh.cursor & 0xFFFF): bad.append('cursor')
    if o.snd != csnd: bad.append('sound')
    if bad: print(f'  seed {seed} mode={mode}: MISMATCH {sorted(set(bad))[:8]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(51000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
