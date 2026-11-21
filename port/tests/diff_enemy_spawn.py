#!/usr/bin/env python3
"""Differential test: original enemy spawn (0BA5:04D3) vs enemy_spawn() in enemy.c.
Sound hook 136B:000B is stubbed as a recorder; sprite sheet headers (words 13/14) are random;
clock, deadline, difficulty, RNG index, player position, slot occupancy and defs are random."""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, O, SEG_GAME, DS, load_state, rnd_state
from diff_video import BASE_SEG
from diff_sprite import spr

NAMES = ['bf64','hp_max','bf84','bf94','bfa4','bfb4','bfc4','type','def','sheet','cell','flash','hp','c034','facing',
         'box_h','box_w','timer','vy','vx','y','x','c0c4','c0d4','c0e4','c0f4','c104','c114','c124']
OFFS = [0xBF64,0xBF74,0xBF84,0xBF94,0xBFA4,0xBFB4,0xBFC4,0xBFD4,0xBFE4,0xBFF4,0xC004,0xC014,0xC024,0xC034,0xC044,
        0xC054,0xC064,0xC074,0xC084,0xC094,0xC0A4,0xC0B4,0xC0C4,0xC0D4,0xC0E4,0xC0F4,0xC104,0xC114,0xC124]
class En(ctypes.Structure): _fields_ = [(n, ctypes.c_int16 * 8) for n in NAMES] + [('pending', ctypes.c_int16 * 32)]
en = En.in_dll(lib, 'g_en')
lib.enemy_spawn.argtypes = [ctypes.c_int] * 4; lib.enemy_spawn.restype = ctypes.c_int
lib.timer_init.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; lib.timer_set_clock.argtypes = [ctypes.c_uint32]
NOW = ctypes.CFUNCTYPE(ctypes.c_uint64)(lambda: 0); lib.timer_init(NOW, None)
SND = ctypes.CFUNCTYPE(None, ctypes.c_int); csnd = []
snd_cb = SND(lambda i: csnd.append(i)); lib.game_set_sound_callback(snd_cb)
ddl = ctypes.c_uint32.in_dll(lib, 'g_enemy_sound_deadline'); diff = ctypes.c_int16.in_dll(lib, 'g_difficulty')
defs_buf = (ctypes.c_int16 * (12 * 8))(); ctypes.c_void_p.in_dll(lib, 'g_enemy_defs').value = ctypes.addressof(defs_buf)
cam = de.lib  # camera struct through diff_camera layout
import diff_camera as dc
dc_cam = dc.cam

class OS(O):
    def __init__(self):
        super().__init__(); self.snd = []
        self.uc.mem_write((0x136B + BASE_SEG) * 16 + 0x0B, b'\xe6\xa3\xcb')
    def wr(self, p, v):
        if p == 0xa3: self.snd.append(self.arg(0))
        else: super().wr(p, v)

def run(seed):
    r = random.Random(seed)
    defs = [r.choice([0, 1, 1, 2, 3, 4, 5, 6, 7, 8, 8, 9, -1]) if k == 11 else r.randrange(-5, 40) for _ in range(6) for k in range(12)]
    for i in range(6):                                      # hit points sometimes negative
        if r.random() < .2: defs[i*12+1] = r.randrange(-3, 0)
    hdr = [[r.randrange(0, 65536) for _ in range(30)] for _ in range(10)]
    occ = [r.choice([-1, -1, 0, 3, 5, -2]) for _ in range(8)]
    if r.random() < .1: occ = [0] * 8
    clock = r.choice([r.randrange(0, 100000), 0xFFFFFFF0, 0x7FFFFFF0, 0xFFFF0, r.randrange(0, 1 << 32)])
    dl = r.choice([clock, (clock - r.randrange(0, 9)) & 0xFFFFFFFF, (clock + r.randrange(0, 9)) & 0xFFFFFFFF, r.randrange(0, 1 << 32), 0])
    dif = r.randrange(-1, 4); pxc = r.randrange(-20, 150); py = r.randrange(-50, 1000)
    st = rnd_state(r)
    calls = [(r.randrange(0, 6), r.choice([pxc + r.randrange(-60, 60), r.randrange(-10, 150), pxc + r.choice([-0x33, -0x32, -0x31, 0x31, 0x32, 0x33])]),
              r.choice([py + r.randrange(-120, 120), r.randrange(0, 960), py + r.choice([-0x65, -0x64, -0x63, 0x63, 0x64, 0x65, 0])]), r.randrange(0, 240)) for _ in range(r.randrange(1, 6))]
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0xA552, struct.pack('<%dh' % len(defs), *defs)); w(0x706A, struct.pack('<h', dif))
    w(0xCF2C, struct.pack('<h', pxc)); w(0xCF28, struct.pack('<h', py))
    w(0x7908, struct.pack('<I', clock)); w(0xCFD4, struct.pack('<I', dl))
    for i, t in enumerate(occ): w(0xBFD4 + 2 * i, struct.pack('<h', t))
    for s in range(10):                                     # sprite slot headers at C1A0 + slot*220
        w(0xC1A0 + s * 220, struct.pack('<30H', *hdr[s]))
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    ctypes.memmove(defs_buf, struct.pack('<%dh' % len(defs), *defs), len(defs) * 2)
    diff.value = dif; dc_cam.player_xcol = pxc; dc_cam.player_y = py
    lib.timer_set_clock(clock); ddl.value = dl
    for s in range(10):
        for k in range(30): spr.hdr[s * 30 + k] = hdr[s][k]
    for i, t in enumerate(occ):
        for n in NAMES: getattr(en, n)[i] = 0
        en.type[i] = t
    for i, t in enumerate(occ): pass
    # make the original's untouched fields match the port's zero-fill
    for n, off in zip(NAMES, OFFS):
        for i, t in enumerate(occ):
            if n != 'type': w(off + 2 * i, struct.pack('<h', 0))
    csnd.clear(); bad = []
    for ci, (d, x, y, cell) in enumerate(calls):
        ra = o.call(SEG_GAME, 0x4D3, (d, x, y, cell)) & 0xFFFF; rb = lib.enemy_spawn(d, x, y, cell) & 0xFFFF
        if ra != rb: bad.append(f'ret{ci}')
        for n, off in zip(NAMES, OFFS):
            a = struct.unpack('<8h', o.dsb(off, 16)); b = tuple(getattr(en, n))
            if a != b: bad.append(f'{n}@{ci}')
    if struct.unpack('<I', o.dsb(0xCFD4, 4))[0] != ddl.value: bad.append('deadline')
    if o.snd != csnd: bad.append(f'sound {o.snd} vs {csnd}')
    if o.rw(0x7118) != (lib.rng_index() & 0xFFFF): bad.append('rng')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]} calls={calls}')
    return len(bad) > 0

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    bad = sum(run(7000 + s) for s in range(n))
    print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
