#!/usr/bin/env python3
"""End-to-end differential test: the ORIGINAL level function 0BA5:453A (setup + frame loop, run in Unicorn on the real HOCUS.DAT with
all graphics loaders, blitters and game code real; only file I/O, sound, music, keyboard hook and the hint / end screens are stubbed)
against level_run() in loop.c, frame by frame, with the same scripted keyboard input.  Per frame (at the loop head 0BA5:4D47) both
sides are digested: all four VGA planes, palette, display start, the layers, enemy / projectile pools, effect pools and ~60 variables."""
import sys, struct, random, ctypes, os, zlib
import diff_effects as de
from diff_effects import lib, DS, BASE_SEG, SEG_GAME, E6, o_bytes, c_bytes, rnd_state
from diff_video import Orig, snapshot, STACK_SEG, SENT_SEG
import diff_animate as da
import diff_camera as dc
import diff_move as dm
from diff_level import lv, LV, gin
from diff_player_hits import pl
import diff_enemy_spawn as ds
from unicorn import *
from unicorn.x86_const import *
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
exe = open(ROOT + '/HOCUS.EXE', 'rb').read(); dat = open(ROOT + '/HOCUS.DAT', 'rb').read()
fat = [struct.unpack_from('<II', exe, 0x1F1A4 + 8 * i) for i in range(652)]
ent = lambda i: dat[fat[i][0]:fat[i][0] + fat[i][1]]
STR_SEG = 0x6000; N = 14400

INPUTS = [('left', 0x70AC), ('right', 0x70AB), ('down', 0x70AD), ('scroll_up', 0x70A4), ('scroll_down', 0x70A3), ('jump', 0x70A9), ('fire', 0x70AA), ('up', 0x70AE), ('up2', 0x70AF)]
SCAL = [(lv, n, off) for n, off in LV if n not in ('tile_blank', 'tile_lever_a', 'tile_lever_b', 'score', 'level_icon')] + [(dc.cam, n, off) for n, off in dc.CAM] + \
       [(dm.mv, n, off) for n, off in dm.MVOFF.items()] + [(pl, 'health', 0x704C), (pl, 'invuln', 0xCE74), (pl, 'shield', 0xCE8E), (dm.fr, 'demo_pos', 0xCF3E)]
SPECIAL = {'level_icon', 'score'}

class FO(Orig):
    def __init__(self):
        super().__init__(); self.cur = b''; self.pos = 0; self.ext = []
        w = lambda seg, off, b: self.uc.mem_write((seg + BASE_SEG) * 16 + off, b)
        w(0x68c, 0x04, b'\xcb'); w(0x68c, 0x32, b'\xcb')
        w(0x68c, 0x47, b'\xe6\x9a\xcb'); w(0x68c, 0x89, b'\xe6\x9b\xcb'); w(0x68c, 0x2b5, b'\xe4\x99\x30\xe4\xcb'); w(0x1392, 0x7ca, b'\xe6\x9c\xcb')
        w(0x1392, 0x800, b'\xe6\x9e\xcb'); w(0x68c, 0xf9, b'\xe6\x9f\xcb')
        w(0x68c, 0x64, b'\xe6\xa0\xcb'); w(0, 0x3e39, b'\xe6\xa1\xcb'); w(0, 0x3d29, b'\xe6\xa2\xcb')
        for off, port in ((0x0B, 0xa3),): w(0x136b, off, bytes([0xe6, port, 0xcb]))        # sound effect
        w(0x136b, 0x1ff, b'\xe6\xa4\xcb'); w(0x136b, 0x261, b'\xe6\xa5\xcb')               # music, music off
        w(0x1392, 0x200, b'\xe6\xa6\xcb'); w(0x1392, 0x254, b'\xe6\xa7\xcb'); w(0x1392, 0x195, b'\xe6\xa8\xcb')   # keyboard on / off / music stop
        w(0xba5, 0x38b6, b'\xe6\xa9\xcb')                                                  # hint box
        w(0x6b8, 0x4889, b'\xe6\xaa\xcb'); w(0x6b8, 0x4600, b'\xe6\xab\xcb')               # end screen, quit menu
    def wr(self, p, v):
        a = self.arg
        if p == 0x9a: self.pos = a(0) | (a(1) << 16)
        elif p == 0x9b:
            off = a(0) | (a(1) << 16); ln = a(2) | (a(3) << 16)
            self.uc.mem_write(self.lin(a(5), a(4)), self.cur[off:off + ln]); self.pos = off + ln
        elif p == 0x9c:
            self.cur = ent(a(0)); self.uc.mem_write(self.lin(a(2), a(1)), struct.pack('<I', 0)); self.uc.mem_write(self.lin(a(4), a(3)), struct.pack('<I', len(self.cur)))
        elif p == 0x9e: self.cur = ent(a(0)); self.uc.mem_write(self.lin(a(2), a(1)), struct.pack('<I', 0))
        elif p == 0x9f: self.uc.mem_write(self.lin(a(2), a(1)), ent(a(0)))
        elif p == 0xa0: self.cur = ent(a(0)); self.pos = 0
        elif p == 0xa1: self.pos += a(2) | (a(3) << 16)
        elif p == 0xa2:
            size, n = a(2), a(3); self.uc.mem_write(self.lin(a(1), a(0)), self.cur[self.pos:self.pos + size * n]); self.pos += size * n
        elif p == 0xa3: self.ext.append(('snd', a(0)))
        elif p == 0xa4: self.ext.append(('music', a(0), a(1)))
        elif p == 0xa5: self.ext.append(('music_off',))
        elif p == 0xa6: self.ext.append(('input_on',))
        elif p == 0xa7: self.ext.append(('input_off',))
        elif p == 0xa8: self.ext.append(('ev1',))
        elif p == 0xa9: self.ext.append(('hint', a(0)))
        elif p == 0xaa: self.ext.append(('end', a(0), a(1), a(2))); self.uc.reg_write(UC_X86_REG_AX, 0)
        elif p == 0xab: self.ext.append(('quit',)); self.uc.reg_write(UC_X86_REG_AX, 0)
        else: super().wr(p, v)
    def rd(self, p):
        if p == 0x99: b = self.cur[self.pos]; self.pos += 1; return b
        return super().rd(p)
    def arg(self, n):
        sp = self.uc.reg_read(UC_X86_REG_SP); ss = self.uc.reg_read(UC_X86_REG_SS)
        return struct.unpack('<H', bytes(self.uc.mem_read(ss * 16 + sp + 4 + 2 * n, 2)))[0]
    def lin(self, seg, off): return ((seg << 4) + off) & 0xFFFFF
    def mem(self, off, n): return bytes(self.uc.mem_read(DS * 16 + off, n))
    dsb = mem

def script(seed, frames):
    r = random.Random(seed ^ 0x5EED); st = {n: 0 for n, _ in INPUTS}; out = []
    for f in range(frames):
        if r.random() < .12: st['left'], st['right'] = r.choice([(1, 0), (0, 1), (0, 0), (1, 0), (0, 1)])
        if r.random() < .08: st['up'] = st['up2'] = r.randrange(2)
        st['down'] = int(r.random() < .05); st['scroll_up'] = int(r.random() < .03); st['scroll_down'] = int(r.random() < .03)
        st['jump'] = int(r.random() < .07); st['fire'] = int(r.random() < .25)
        out.append(dict(st))
    return out

def patch_x87(o):
    def p(seg, off, code): o.uc.mem_write((seg + BASE_SEG) * 16 + off, code)
    for off, b in ((0xC7E, 0xDF), (0xC8D, 0xDF), (0xC91, 0xDE), (0xC94, 0xD8)): p(0xBA5, off, bytes([0x9B, b]))
    p(0x7000 - BASE_SEG, 0, b'\x9b\xdb\xe3\xcb'); o.call(0x7000, 0, ())      # FNINIT, as the C runtime startup does
    p(0, 0x113F, b'\x9b\xd9'); p(0, 0x1143, b'\x90\x9b'); p(0, 0x114C, b'\x9b\xd9'); p(0, 0x1150, b'\x9b\xdf'); p(0, 0x1157, b'\x9b\xd9')

def digest_vga():
    s = snapshot({}); h = 0
    for k in ('plane0', 'plane1', 'plane2', 'plane3'): h = zlib.crc32(s[k], h)
    return (h, s['start'], s['pal'], s['screen'])

def run_orig(epi, lvl, mode, scr, seed, difficulty):
    st = rnd_state(random.Random(seed))
    lib.vga_reset(); o = FO(); o.call(de.SEG_VIDEO, 0x185); patch_x87(o)
    W = lambda off, fmt, *v: o.uc.mem_write(DS * 16 + off, struct.pack(fmt, *v))
    for k, seg in da.SEGS.items(): W(da.PTR[k], '<HH', 0, seg)
    W(0x705A, '<HH', 0, 0x5000); W(0x7056, '<HH', 0, 0x4000)
    for s in range(10): W(0x7070 + s * 4, '<HH', 0, STR_SEG + s * 0x400)
    W(0x48C6, '<HH', 0, 0x9000)
    o.uc.mem_write(DS * 16 + 0x711A, E6)
    W(0x706E, '<h', epi); W(0x706C, '<h', lvl); W(0x706A, '<h', difficulty); W(0x790E, '<h', 0); W(0x48CE, '<h', 0); W(0x643C, '<h', 0); W(0x643E, '<h', 0)
    W(0x7052, '<I', 0); W(0x704E, '<I', 0); W(0x7118, '<h', 0)
    digs = []; ctr = [0]
    base = (0xBA5 + BASE_SEG) * 16
    def hook_head(uc, addr, size, ud):
        f = ctr[0]
        if f > 0:
            d = [digest_vga(), o.mem(0xBF64, 528), o.mem(0xBE74, 240), o.mem(0x7118, 2), o.mem(0x7908, 4), o.mem(0x7052, 4)]
            d.append(bytes(uc.mem_read(0x4000 * 16 + BASE_SEG * 16 * 0, 0)))
            for seg in (0x4000, 0x4400, 0x4800, 0x4C00): d.append(zlib.crc32(bytes(uc.mem_read(seg * 16, N))))
            ob = o_bytes(o); d.append(tuple(ob[k] for k in sorted(ob)))
            d.append(tuple(o.rw(off) for _, _, off in SCAL)); d.append(o.mem(0xCEEE, 20)); d.append(o.mem(0xCE9A, 20)); d.append(o.mem(0xCF46, 2))
            digs.append((d, list(o.ext)))
        if f >= len(scr): uc.emu_stop(); return
        for n, off in INPUTS: uc.mem_write(DS * 16 + off, bytes([scr[f][n]]))
        ctr[0] += 1
    def hook_wait(uc, addr, size, ud):
        v = struct.unpack('<I', bytes(uc.mem_read(DS * 16 + 0x7908, 4)))[0]; uc.mem_write(DS * 16 + 0x7908, struct.pack('<I', (v + 1) & 0xFFFFFFFF))
    o.uc.hook_add(UC_HOOK_CODE, hook_head, None, base + 0x4D47, base + 0x4D47)
    o.uc.hook_add(UC_HOOK_CODE, hook_wait, None, base + 0x4D67, base + 0x4D67)
    uc = o.uc; sp = 0x1000 - 8
    uc.mem_write(STACK_SEG * 16 + sp, struct.pack('<HHhh', 0, SENT_SEG, mode, 0))
    uc.reg_write(UC_X86_REG_SS, STACK_SEG); uc.reg_write(UC_X86_REG_SP, sp); uc.reg_write(UC_X86_REG_DS, DS); uc.reg_write(UC_X86_REG_ES, DS); uc.reg_write(UC_X86_REG_CS, SEG_GAME)
    uc.emu_start(SEG_GAME * 16 + 0x453A, SENT_SEG * 16, count=3_000_000_000)
    return digs, o

# ---------------- port side ----------------
lib.dat_open.argtypes = [ctypes.c_char_p]; assert lib.dat_open((ROOT + '/HOCUS.DAT').encode()) == 0
lib.level_run.argtypes = [ctypes.c_int, ctypes.c_int]; lib.level_run.restype = ctypes.c_int
lib.rng_load.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
lib.timer_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; lib.timer_clock.restype = ctypes.c_uint32
class Hooks(ctypes.Structure): _fields_ = [(n, ctypes.c_void_p) for n in ('music', 'pcx', 'pal', 'img', 'sprites', 'input', 'demo')]
class LHooks(ctypes.Structure): _fields_ = [(n, ctypes.c_void_p) for n in ('poll_joystick', 'input_off', 'wait_key', 'pause_game', 'quit_menu', 'menu_f2', 'menu_f3', 'menu_f10', 'menu_f1', 'menu_c', 'menu_v', 'message', 'music_off', 'save_settings', 'save_demo', 'end_screen', 'music', 'palette_entry', 'release', 'frame')]
hk = Hooks.in_dll(lib, 'g_level_hooks'); lhk = LHooks.in_dll(lib, 'g_loop_hooks')
F2 = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int); F0 = ctypes.CFUNCTYPE(None); FI = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int); F1 = ctypes.CFUNCTYPE(None, ctypes.c_int)
F3i = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int)
ext = []; now = [0]; shared = {}
NOW = ctypes.CFUNCTYPE(ctypes.c_uint64)(lambda: now[0]); IDLE = F0(lambda: now.__setitem__(0, now[0] + 7142858))
lib.timer_init(ctypes.cast(NOW, ctypes.c_void_p), ctypes.cast(IDLE, ctypes.c_void_p))
SND = ctypes.CFUNCTYPE(None, ctypes.c_int); snd_cb = SND(lambda i: ext.append(('snd', i))); lib.game_set_sound_callback(snd_cb)
HC = ctypes.CFUNCTYPE(None, ctypes.c_int); hint_cb = HC(lambda i: ext.append(('hint', i))); lib.level_set_hint_callback(hint_cb)
EV = ctypes.CFUNCTYPE(None, ctypes.c_int)
def _ev(e):
    if e == 1: ext.append(('ev1',))
    else: lib.enemy_kill_all()
ev_cb = EV(_ev); lib.frame_set_event_hook(ev_cb)
_k = [F2(lambda e, l: ext.append(('music', e, l))), F0(lambda: ext.append(('input_on',))), F0(lambda: ext.append(('input_off',))), F0(lambda: ext.append(('music_off',))),
      F1(lambda e: None)]
hk.music = ctypes.cast(_k[0], ctypes.c_void_p).value; hk.input = ctypes.cast(_k[1], ctypes.c_void_p).value
lhk.input_off = ctypes.cast(_k[2], ctypes.c_void_p).value; lhk.music_off = ctypes.cast(_k[3], ctypes.c_void_p).value
lhk.music = ctypes.cast(_k[0], ctypes.c_void_p).value
ENDF = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int)
_end = ENDF(lambda a, b, c: (ext.append(('end', a, b, c)), 0)[1]); lhk.end_screen = ctypes.cast(_end, ctypes.c_void_p).value
_quit = ctypes.CFUNCTYPE(ctypes.c_int)(lambda: (ext.append(('quit',)), 0)[1]); lhk.quit_menu = ctypes.cast(_quit, ctypes.c_void_p).value
lib.loop_install_defaults()
epi_c = ctypes.c_int16.in_dll(lib, 'g_episode'); diff_c = ctypes.c_int16.in_dll(lib, 'g_difficulty'); speed_c = ctypes.c_int16.in_dll(lib, 'g_game_speed')
shots = ctypes.c_int16 * 10
import subprocess
_SYMS = {}
for line in subprocess.check_output(['nm', '-S', '--defined-only', os.path.dirname(os.path.abspath(__file__)) + '/libvideo.so'], text=True).splitlines():
    p = line.split()
    if len(p) == 4 and p[2] in 'bBdD': _SYMS[p[3]] = int(p[1], 16)
RESET = ['g_lv', 'g_cam', 'g_mv', 'g_frame', 'g_player', 'g_en', 'g_proj', 'g_shots', 'g_fx', 'g_in', 'g_joy', 'g_boss_phase', 'g_kills', 'g_enemies_total', 'g_spr', 'g_demo_playback',
         'g_joy_enabled', 'g_enemy_sound_deadline', 'g_demo_len', 'g_level_start', 'g_shot_block_tile', 'g_score_saved', 'g_pending_dummy']
def reset_port():
    for n in RESET:
        if n in _SYMS: ctypes.memset(ctypes.addressof((ctypes.c_char * _SYMS[n]).in_dll(lib, n)), 0, _SYMS[n])
    lib.loop_install_defaults(); lib.timer_reset(); lib.rng_reset()
def run_port(epi, lvl, mode, scr, seed, difficulty):
    st = rnd_state(random.Random(seed))
    reset_port()
    lib.vga_reset(); lib.video_init()
    # the original starts from its initial data segment; the port from the zero state
    epi_c.value = epi; lv.level_icon = lvl; diff_c.value = difficulty; speed_c.value = 0
    assert lib.rng_load(E6, len(E6)) == 0
    ext.clear(); digs = []; ctr = [0]
    gm = da.gm
    def frame(n):
        if n > 0:
            d = [digest_vga(), bytes((ctypes.c_char * 528).from_address(ctypes.addressof(ds.en))), bytes((ctypes.c_char * 240).from_address(ctypes.addressof(ctypes.c_char.in_dll(lib, 'g_proj')))),
                 struct.pack('<H', lib.rng_index() & 0xFFFF), struct.pack('<I', lib.timer_clock()), struct.pack('<I', lv.score)]
            d.append(b'')
            for k in ('dyn', 'alt', 'fg', 'bg'): d.append(zlib.crc32(bytes((ctypes.c_char * N).from_address(getattr(gm, k)))))
            cb_ = c_bytes(); d.append(tuple(cb_[k] for k in sorted(cb_)))
            d.append(tuple(getattr(o, n_) & 0xFFFF for o, n_, _ in SCAL)); d.append(bytes(dm.shots_x()) if hasattr(dm, 'shots_x') else bytes(PSH.x)); d.append(bytes(PSH.power)); d.append(struct.pack('<H', BOSS.value & 0xFFFF))
            digs.append((d, list(ext)))
        if n >= len(scr): return 1
        for k, _ in INPUTS: setattr(gin, k, scr[n][k])
        return 0
    fcb = FI(frame); lhk.frame = ctypes.cast(fcb, ctypes.c_void_p).value
    rc = lib.level_run(mode, 0)
    return digs, rc
class Sh(ctypes.Structure): _fields_ = [('x', ctypes.c_int16 * 10), ('power', ctypes.c_int16 * 10), ('count', ctypes.c_int16), ('up', ctypes.c_int16 * 10), ('dir', ctypes.c_int16 * 10), ('cursor', ctypes.c_int16)]
PSH = Sh.in_dll(lib, 'g_shots'); BOSS = ctypes.c_int16.in_dll(lib, 'g_boss_phase')

def compare(a, b):
    names = ['vga', 'enemies', 'projectiles', 'rng', 'clock', 'score', '-', 'dyn', 'alt', 'fg', 'bg', 'fxpools', 'scalars', 'shots.x', 'shots.power', 'boss']
    for f, ((da_, ea), (db_, eb)) in enumerate(zip(a, b)):
        bad = [names[i] for i in range(len(names)) if names[i] != '-' and da_[i] != db_[i]]
        if ea != eb: bad.append('ext %s vs %s' % (ea[-3:], eb[-3:]))
        if bad:
            if 'scalars' in bad:
                sc = [(n, ta, tb) for (_, n, _), ta, tb in zip(SCAL, da_[names.index('scalars')], db_[names.index('scalars')]) if ta != tb]; bad.append(sc[:6])
            if 'vga' in bad:
                x, y = da_[0], db_[0]; bad.append(['plane crc' if x[0] != y[0] else '', 'start' if x[1] != y[1] else '', 'pal' if x[2] != y[2] else ''])
            return f, bad
    return None, None

def run(epi, lvl, mode, frames, seed, difficulty=1):
    scr = script(seed, frames)
    A, o = run_orig(epi, lvl, mode, scr, seed, difficulty)
    B, rc = run_port(epi, lvl, mode, scr, seed, difficulty)
    f, bad = compare(A, B)
    if len(A) != len(B): bad = (bad or []) + ['frames %d vs %d' % (len(A), len(B))]; f = f or min(len(A), len(B))
    print(f'  e{epi} l{lvl} mode {mode} seed {seed}: {len(A)} frames', 'OK' if not bad else f'MISMATCH at frame {f}: {bad}', flush=True)
    return bool(bad)

if __name__ == '__main__':
    if sys.argv[1:2] == ['all']:
        frames = int(sys.argv[2]) if len(sys.argv) > 2 else 300; nseeds = int(sys.argv[3]) if len(sys.argv) > 3 else 1; bad = 0; n = 0
        for sd in range(nseeds):
            for e in range(4):
                for l in range(9): bad += run(e, l, 0, frames, 100 + sd * 1000 + e * 10 + l, difficulty=(e + l + sd) % 4); n += 1
        print(f'{n} level runs, {bad} mismatches'); sys.exit(1 if bad else 0)
    args = [int(x) for x in sys.argv[1:]]
    epi, lvl, frames = (args + [0, 0, 60][len(args):])[:3] if args else (0, 0, 60)
    sys.exit(1 if run(epi, lvl, 0, frames, 1) else 0)
