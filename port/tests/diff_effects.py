#!/usr/bin/env python3
"""
Differential test: effect pools B/C/D (0BA5:3684,36F0,2174,1FAA,21C6,20B1) vs port/src/effects.c.
The original machine code runs in Unicorn: VGA/VRAM go to the C VGA model, the original RNG and
all 5D8 video calls run as real code, and only the sprite blitter 0BA5:249E is replaced by a
stub that records its arguments.  Compared after every operation: all pool memory byte-for-byte,
ring indices, RNG index, every VRAM byte, VGA registers, and the recorded sprite draw calls.
"""
import sys, struct, random, ctypes, subprocess, os
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, '/home/claude/hocus'); sys.path.insert(0, here)
src = here + '/../src/'
subprocess.check_call(['gcc', '-shared', '-fPIC', '-O1', '-std=c11', '-o', here + '/libvideo.so',
                       src + 'vga.c', src + 'video.c', src + 'rng.c', src + 'effects.c', src + 'camera.c', src + 'sprite.c', src + 'map.c', src + 'enemy.c', src + 'player.c', src + 'timer.c', src + 'level.c', src + 'frame.c', src + 'input.c', src + 'dat.c', src + 'dat_tables.c', src + 'loader.c', src + 'loop.c', src + 'ui.c', src + 'stars.c', src + 'sound.c'])
import diff_video as dv
from diff_video import lib, Orig, snapshot, SEG_VIDEO, DS, BASE_SEG
from hocus_dat import HocusData
from unicorn.x86_const import *
SEG_GAME = 0xba5 + BASE_SEG
H = HocusData('/home/claude/hocus/HOCUS.EXE', '/home/claude/hocus/HOCUS.DAT'); E6 = H.get(6)
SEG_RNG = 0x1392 + BASE_SEG
class FxFields(ctypes.Structure):
    _fields_ = [('b', ctypes.c_int16 * 50), ('c', ctypes.c_int16 * 32), ('d_t', ctypes.c_int16 * 20),
                ('d_y', ctypes.c_int16 * 20), ('d_x', ctypes.c_int16 * 20), ('c_ring', ctypes.c_int16),
                ('d_ring', ctypes.c_int16), ('sprite_b', ctypes.c_int16), ('sprite_c', ctypes.c_int16),
                ('a_flags', ctypes.c_int8 * 8), ('a_vel', ctypes.c_int8 * 384), ('a_pos', ctypes.c_int16 * 256), ('a_ring', ctypes.c_int16), ('e_y', ctypes.c_int16 * 10)]
fx = FxFields.in_dll(lib, 'g_fx')
lib.rng_load.argtypes = [ctypes.c_char_p, ctypes.c_uint32]; lib.rng_set_index.argtypes = [ctypes.c_int]
lib.rng_index.restype = ctypes.c_int
DRAW = ctypes.CFUNCTYPE(None, *([ctypes.c_int] * 6)); c_calls = []
cb = DRAW(lambda *a: c_calls.append(tuple(v & 0xFFFF for v in a))); lib.fx_set_draw_callback(cb)
for f in ('fx_spawn_score',): getattr(lib, f).argtypes = [ctypes.c_int] * 3
for f in ('fx_spawn_puff', 'fx_spawn_sparkle', 'fx_spawn_burst'): getattr(lib, f).argtypes = [ctypes.c_int] * 2

# DS layout of the pools (original offsets)
REG = [(0xCE04, 100), (0xCDC2, 64), (0xCF58, 120), (0xCE02, 2), (0x2ACC, 2), (0x7118, 2), (0xC296, 2), (0xC44E, 2)]
class O(Orig):
    def __init__(self):
        super().__init__(); self.calls = []
        self.uc.mem_write(SEG_GAME * 16 + 0x249E, b'\xe6\x9d\xcb')
    def wr(self, p, v):
        if p == 0x9d:
            self.calls.append(tuple(self.arg(i) for i in range(6)))
        else: super().wr(p, v)
    def arg(self, n):
        sp = self.uc.reg_read(UC_X86_REG_SP); ss = self.uc.reg_read(UC_X86_REG_SS)
        return struct.unpack('<H', bytes(self.uc.mem_read(ss * 16 + sp + 4 + 2 * n, 2)))[0]
    def dsb(self, off, n): return bytes(self.uc.mem_read(DS * 16 + off, n))
    def dsw(self, off, v): self.uc.mem_write(DS * 16 + off, struct.pack('<h', v))

def c_bytes():
    raw = bytes(fx)
    # struct order: b(100) c(64) d_t d_y d_x(120) c_ring d_ring sprite_b sprite_c
    return dict(b=raw[0:100], c=raw[100:164], d=raw[164:284], c_ring=raw[284:286], d_ring=raw[286:288],
                sb=raw[288:290], sc=raw[290:292], rng=struct.pack('<H', lib.rng_index() & 0xFFFF),
                af=raw[292:300], av=raw[300:684], ap=raw[684:1196], ar=raw[1196:1198])
def o_bytes(o):
    return dict(b=o.dsb(0xCE04, 100), c=o.dsb(0xCDC2, 64), d=o.dsb(0xCF58, 120), c_ring=o.dsb(0xCE02, 2),
                d_ring=o.dsb(0x2ACC, 2), sb=o.dsb(0xC296, 2), sc=o.dsb(0xC44E, 2), rng=o.dsb(0x7118, 2),
                af=o.dsb(0xCA38, 8), av=o.dsb(0xCA40, 384), ap=o.dsb(0xCBC0, 512), ar=o.dsb(0xCDC0, 2))

def rnd_state(r):
    b = [0] * 50; c = [0] * 32; dx = []; dy = []; dt = []
    for i in range(10):
        live = r.random() < .6
        b[5*i:5*i+5] = [r.randrange(-5, 85), r.randrange(-10, 200), r.randrange(0, 5), r.randrange(1, 18) if live else 0, r.randrange(2)]
    for i in range(8):
        live = r.random() < .6
        c[4*i:4*i+4] = [r.randrange(-5, 85), r.randrange(-10, 200), r.choice([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 9, 10, -1]) if live else 0, r.randrange(2)]
    for i in range(20):
        if r.random() < .35: dx.append(-99)
        else: dx.append(r.choice([r.randrange(-3, 84), r.randrange(0, 80)]))
        dy.append(r.choice([r.randrange(-5, 175), r.randrange(0, 160)])); dt.append(r.randrange(0, 20))
    return dict(b=b, c=c, d=dt + dy + dx, c_ring=r.randrange(8), d_ring=r.randrange(20),
                sb=r.randrange(0x10000) & 0x7FFF, sc=r.randrange(0x10000) & 0x7FFF, rng=r.randrange(1000),
                af=[r.choice([0, 0, 16, 15, 9, 1, -1, -7, 127]) for _ in range(8)],
                av=[r.randrange(-12, 13) for _ in range(384)], ar=r.randrange(8),
                ap=[v for _ in range(128) for v in (r.choice([r.randrange(-10, 330), r.randrange(0, 320)]), r.choice([r.randrange(-10, 170), r.randrange(0, 160)]))],
                page=r.randrange(4), mask=r.randrange(1, 16), latch=r.randrange(2))

def load_state(st, to_orig):
    pk = lambda vals: struct.pack('<%dh' % len(vals), *vals)
    if to_orig is not None:
        o = to_orig
        o.uc.mem_write(DS * 16 + 0xCE04, pk(st['b'])); o.uc.mem_write(DS * 16 + 0xCDC2, pk(st['c']))
        o.uc.mem_write(DS * 16 + 0xCF58, pk(st['d'])); o.dsw(0xCE02, st['c_ring']); o.dsw(0x2ACC, st['d_ring'])
        o.uc.mem_write(DS * 16 + 0xCA38, struct.pack('<8b', *st['af'])); o.uc.mem_write(DS * 16 + 0xCA40, struct.pack('<384b', *st['av']))
        o.uc.mem_write(DS * 16 + 0xCBC0, pk(st['ap'])); o.dsw(0xCDC0, st['ar'])
        o.dsw(0xC296, st['sb']); o.dsw(0xC44E, st['sc']); o.uc.mem_write(DS * 16 + 0x711A, E6); o.dsw(0x7118, st['rng'])
    else:
        for i, v in enumerate(st['b']): fx.b[i] = v
        for i, v in enumerate(st['c']): fx.c[i] = v
        for i in range(20): fx.d_t[i] = st['d'][i]; fx.d_y[i] = st['d'][20 + i]; fx.d_x[i] = st['d'][40 + i]
        for i, v in enumerate(st['af']): fx.a_flags[i] = v
        for i, v in enumerate(st['av']): fx.a_vel[i] = v
        for i, v in enumerate(st['ap']): fx.a_pos[i] = v
        fx.a_ring = st['ar']
        fx.c_ring = st['c_ring']; fx.d_ring = st['d_ring']; fx.sprite_b = st['sb']; fx.sprite_c = st['sc']
        assert lib.rng_load(E6, len(E6)) == 0; lib.rng_set_index(st['rng'])

OPS = {'spawn_b': (0x3684, 3), 'upd_b': (0x36F0, 0), 'spawn_c': (0x2174, 2), 'upd_c': (0x1FAA, 0),
       'spawn_d': (0x21C6, 2), 'upd_d': (0x20B1, 0),
       'spawn_a': (0x2F1C, 2), 'upd_a': (0x2FF3, 0)}
def args_for(name, r):
    if name == 'spawn_b': return (r.randrange(-3, 85), r.randrange(-5, 200), r.choice([0, 1, 4, 5, 9, 2, 7, r.randrange(-2, 14)]))
    if name in ('spawn_c', 'spawn_d', 'spawn_a'): return (r.randrange(-5, 90), r.randrange(-5, 190))
    return ()
def run(seed, nops):
    r = random.Random(seed); st = rnd_state(r)
    ops = [(n, args_for(n, r)) for n in (r.choice(list(OPS)) for _ in range(nops))]
    # --- original
    lib.vga_reset(); o = O(); o.call(SEG_VIDEO, 0x185); o.call(SEG_VIDEO, 0xE4, (st['page'],))
    if st['latch']: o.call(SEG_VIDEO, 0x16A, (1,))
    o.call(SEG_VIDEO, 0x158, (st['mask'],)); load_state(st, o)
    trace_o = []
    for n, a in ops:
        off, _ = OPS[n]; o.calls.clear(); o.call(SEG_GAME, off, a)
        trace_o.append((o_bytes(o), list(o.calls), dv.snapshot({})))
    # --- port
    lib.vga_reset(); lib.video_init(); lib.video_set_draw_page(st['page'])
    if st['latch']: lib.video_set_latch_mode(1)
    lib.video_set_map_mask(st['mask']); load_state(st, None)
    bad = 0
    for k, (n, a) in enumerate(ops):
        c_calls.clear()
        {'spawn_b': lib.fx_spawn_score, 'upd_b': lib.fx_update_scores, 'spawn_c': lib.fx_spawn_puff, 'upd_c': lib.fx_update_puffs,
         'spawn_d': lib.fx_spawn_sparkle, 'upd_d': lib.fx_update_sparkles,
         'spawn_a': lib.fx_spawn_burst, 'upd_a': lib.fx_update_bursts}[n](*a)
        ob, oc, osn = trace_o[k]; cb_ = c_bytes(); sn = dv.snapshot({})
        diffs = [f for f in ob if ob[f] != cb_[f]]
        if oc != list(c_calls): diffs.append('draw-calls')
        if [x for x in osn if osn[x] != sn[x]]: diffs.append('vga:' + ','.join(x for x in osn if osn[x] != sn[x]))
        if diffs:
            bad += 1
            if bad <= 4: print(f"  seed {seed} op {k} {n}{a}: MISMATCH {diffs}")
    return bad
if __name__ == '__main__':
    nseeds = int(sys.argv[1]) if len(sys.argv) > 1 else 40; nops = int(sys.argv[2]) if len(sys.argv) > 2 else 40
    tot = bad = 0
    for s in range(nseeds):
        b = run(100 + s, nops); tot += nops; bad += b
    print(f"{nseeds} scenarios, {tot} operations, {bad} mismatching operations"); sys.exit(1 if bad else 0)
