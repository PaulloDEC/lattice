#!/usr/bin/env python3
"""
Differential test: the MIDI sequencer (144e) + AdLib driver (161b) running as the ORIGINAL machine code in Unicorn vs port/src/music.c.
Both sides play the same random Standard MIDI File; compared tick by tick: every OPL register write (reg, value), every tempo/rate change,
the 'playing' flag, plus random volume / pause / resume / stop operations in between.
Stubs on the original: 161b:0001 (the single OPL write primitive) logs its arguments; the timer-task API (1505:501/5AA/5ED/601), malloc/free are replaced.
The fine-tune table (161b:0838, x87 pow) is not run: the port's table (trunc(pitch*2^(row/384)), decoded from the code) is written into the original's DS.
"""
import sys, struct, random, ctypes, subprocess, os
here = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, '/home/claude/hocus'); sys.path.insert(0, here)
src = here + '/../src/'
subprocess.check_call(['gcc', '-shared', '-fPIC', '-O1', '-std=c11', '-o', here + '/libmusic.so', src + 'music.c', src + 'music_tables.c', src + 'opl.c', '-lm'])
import diff_effects as de
from diff_video import Orig, DS, BASE_SEG, STACK_SEG
from unicorn.x86_const import *
LM = ctypes.CDLL(here + '/libmusic.so')
WR = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int); RATE = ctypes.CFUNCTYPE(None, ctypes.c_int)
LM.midi_play.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_int]
LM.midi_set_volume.argtypes = [ctypes.c_int]
S161B, S144E = 0x161B + BASE_SEG, 0x144E + BASE_SEG
TBL_SEG, HEAP_SEG, SONG_SEG, TASK_SEG = 0x7100, 0x7200, 0x7400, 0x7300
FINE = (ctypes.c_uint16 * 384).in_dll(LM, 'g_al_fine')

class MO(Orig):
    def __init__(self):
        super().__init__()
        w = lambda seg, off, b: self.uc.mem_write((seg + BASE_SEG) * 16 + off, b)
        w(0x161B, 0x0001, b'\xe6\xb4\xcb')                                   # OPL write primitive: log (port, reg, val)
        w(0, 0x2B4F, bytes([0xb8, 0, 0, 0xba, HEAP_SEG & 255, HEAP_SEG >> 8, 0xcb]))   # malloc -> HEAP_SEG:0
        w(0, 0x2A45, b'\xcb')                                                # free
        w(0x1505, 0x501, b'\xe6\xb5' + bytes([0xb8, 0, 0, 0xba, TASK_SEG & 255, TASK_SEG >> 8, 0xcb]))   # task create: log rate
        w(0x1505, 0x5ED, b'\xcb'); w(0x1505, 0x5AA, b'\xcb')
        w(0x1505, 0x601, b'\xe6\xb6\xcb')                                    # task set rate: log
        self.log = []; self.rates = []
        uc = self.uc; d = DS * 16
        uc.mem_write(d + 0x2ED4, struct.pack('<HH', 0, TBL_SEG))
        tbl = [(0xB67, 1), (0xC48, 1), (0, 0), (0xD5F, 1), (0xDCA, 1), (0, 0), (0xDE0, 1), (0, 0), (0, 0), (0, 0), (0, 0)]
        uc.mem_write(TBL_SEG * 16, b''.join(struct.pack('<HH', o, (S161B if k else 0)) for o, k in tbl))
        for off in (0x3348, 0x334A, 0x2EC4, 0x2EC6, 0x2EC8, 0x2ED2): uc.mem_write(d + off, b'\0\0')
        uc.mem_write(d + 0x3344, struct.pack('<HH', 0x388, 0x388))
        uc.mem_write(d + 0x3332, bytes(18))
        for i in range(96):
            uc.mem_write(d + 0xD25A + 2 * i, struct.pack('<H', i % 12)); uc.mem_write(d + 0xD31A + 2 * i, struct.pack('<H', i // 12))     # filled by 161b:0838
        for r in range(32):
            uc.mem_write(d + 0x3022 + r * 0x18, struct.pack('<12H', *FINE[r * 12:r * 12 + 12]))
    def wr(self, p, v):
        if p == 0xb4: self.log.append((self.arg(1) & 0xFF, self.arg(2) & 0xFF))
        elif p == 0xb5: self.rates.append(self.arg(2))
        elif p == 0xb6: self.rates.append(self.arg(2))
        else: super().wr(p, v)
    def arg(self, n):
        sp = self.uc.reg_read(UC_X86_REG_SP); ss = self.uc.reg_read(UC_X86_REG_SS)
        return struct.unpack('<H', bytes(self.uc.mem_read(ss * 16 + sp + 4 + 2 * n, 2)))[0]
    def playing(self): return self.rw(0x2EC4)

def vlq(v):
    out = [v & 0x7F]; v >>= 7
    while v: out.append((v & 0x7F) | 0x80); v >>= 7
    return bytes(reversed(out))
def make_song(r):
    ntr = r.choice([1, 1, 2, 3]); div = r.choice([48, 96, 120, 192, 240, 480]); tracks = []
    nch = r.choice([2, 5, 16]); chans = r.sample(range(16), nch)
    if r.random() < .5 and 9 not in chans: chans[0] = 9
    dense = r.random() < .4                                   # chords: many note-ons, few note-offs, few channels -> voice stealing, lists with several voices
    if dense: chans = r.sample(range(16), 3) + ([9] if r.random() < .6 else [])
    for _ in range(ntr):
        ev = bytearray(); last = None; t = r.randrange(20, 160); held = []
        for i in range(t):
            dl = r.choice([0, 0, 0, 1, 2, 5, 10, 40, 130, 300]) if r.random() < .5 else r.randrange(0, 8)
            ev += vlq(dl); c = r.choice(chans); k = r.random()
            if dense and k < .75: k = r.choice([.1, .1, .1, .45, .65])
            if held and r.random() < .3:                          # release a note that is really sounding (random keys almost never match)
                hc, hk = held.pop(r.randrange(len(held)))
                ev += bytes([0x90 | hc, hk, 0]) if r.random() < .5 else bytes([0x80 | hc, hk, 64]); last = None
            elif k < .38:
                kk = r.randrange(0, 128) if r.random() < .3 else r.randrange(30, 90); held.append((c, kk))
                ev += bytes([0x90 | c, kk, r.choice([100, r.randrange(1, 128)])]); last = 0x90 | c
            elif k < .5:
                st = 0x80 | c; ev += (b'' if st == last and r.random() < .7 else bytes([st])) + bytes([r.randrange(128), r.randrange(128)]); last = st
            elif k < .6 and last is not None and (last & 0xF0) == 0x90 and r.random() < .5:
                ev += bytes([r.randrange(30, 90), r.randrange(0, 128)])        # running status note on
            elif k < .72:
                cc = r.choice([7, 7, 10, 0x5E, 0x7B, 0x79, 0x7E, 0x40, 0x64, 6, 1]); ev += bytes([0xB0 | c, cc, r.randrange(128)]) + (b'\x00' if cc == 0x7E else b''); last = 0xB0 | c
            elif k < .8: ev += bytes([0xC0 | c, r.randrange(128)]); last = 0xC0 | c
            elif k < .87: ev += bytes([0xE0 | c, r.randrange(128), r.randrange(128)]); last = 0xE0 | c
            elif k < .9: ev += bytes([0xA0 | c, r.randrange(128), r.randrange(128)]); last = 0xA0 | c
            elif k < .92: ev += bytes([0xD0 | c, r.randrange(128)]); last = 0xD0 | c
            elif k < .96: ev += bytes([0xFF, 0x51, 3]) + r.randrange(150000, 1500000).to_bytes(3, 'big')
            elif k < .98: n = r.randrange(0, 5); ev += bytes([0xFF, 0x01, n]) + bytes(r.randrange(32, 100) for _ in range(n))
            else: ev += bytes([0xFF, 0x01, 0])                    # (a delta with no event would corrupt the stream: the original then loops forever too)
        ev += vlq(r.choice([0, 3, 50])) + bytes([0xFF, 0x2F, 0])
        tracks.append(bytes(ev))
    return b'MThd' + struct.pack('>IHHH', 6, 1 if ntr > 1 else 0, ntr, div) + b''.join(b'MTrk' + struct.pack('>I', len(t)) + t for t in tracks)

def run(seed):
    r = random.Random(seed); song = make_song(r); loop = r.randrange(2); vol0 = r.choice([0xC0, 0xFF, 0x40, r.randrange(256)])
    nticks = r.choice([150, 400, 900]); ops = {}
    for _ in range(r.randrange(0, 5)):
        ops[r.randrange(nticks)] = r.choice([('vol', r.randrange(-5, 270)), ('pause',), ('resume',), ('stop',), ('vol', r.randrange(256))])
    # ---- original
    o = MO(); o.uc.mem_write(SONG_SEG * 16, song)
    o.call(S161B, 0x95B)
    o.call(S144E, 0x6DC, (vol0,)); o.call(S144E, 0x822, (0, SONG_SEG, loop))
    ticks_o = []; pre_o_n = len(o.log)
    for t in range(nticks):
        n0 = len(o.log)
        if t in ops:
            op = ops[t]
            if op[0] == 'vol': o.call(S144E, 0x6DC, (op[1],))
            elif op[0] == 'pause': o.call(S144E, 0x77E)
            elif op[0] == 'resume': o.call(S144E, 0x76C)
            else: o.call(S144E, 0x7B0)
        o.call(S144E, 0x15D, (0, TASK_SEG)); ticks_o.append((tuple(o.log[n0:]), o.playing()))
    # ---- port
    log = []; rates = []
    cbw = WR(lambda reg, val: log.append((reg & 0xFF, val & 0xFF))); cbr = RATE(lambda v: rates.append(v & 0xFFFF))
    ctypes.c_void_p.in_dll(LM, 'g_al_write').value = ctypes.cast(cbw, ctypes.c_void_p).value
    LM.midi_set_rate_callback(cbr)
    LM.midi_reset(); LM.al_init(); n_init = len(log)
    ticks_p = []
    LM.midi_set_volume(vol0); LM.midi_play(song, len(song), loop)
    pre_p_n = len(log)
    for t in range(nticks):
        n0 = len(log)
        if t in ops:
            op = ops[t]
            if op[0] == 'vol': LM.midi_set_volume(op[1])
            elif op[0] == 'pause': LM.midi_pause()
            elif op[0] == 'resume': LM.midi_resume()
            else: LM.midi_stop()
        LM.midi_service(); ticks_p.append((tuple(log[n0:]), LM.midi_playing()))
    # the original's first log entries (095B, volume, play) happened before tick 0: compare everything
    return o.log[:pre_o_n], log[:pre_p_n], ticks_o, ticks_p, o.rates, rates, song, ops

if __name__ == '__main__':
    a, b = (0, int(sys.argv[1])) if len(sys.argv) == 2 else (int(sys.argv[1]), int(sys.argv[2])); bad = 0; nw = 0
    for seed in range(a, b):
        pre_o, pre_p, to, tp, ro, rp, song, ops = run(seed)
        ok = pre_o == pre_p and to == tp and ro == rp
        nw += sum(len(x) for x, _ in to)
        if not ok:
            bad += 1; print('MISMATCH seed', seed, 'ops', ops)
            if pre_o != pre_p: print(' prelude', len(pre_o), len(pre_p), next((i for i, (x, y) in enumerate(zip(pre_o, pre_p)) if x != y), None))
            for i, (x, y) in enumerate(zip(to, tp)):
                if x != y: print(' tick', i, '\n  orig', x, '\n  port', y); break
            if ro != rp: print(' rates', ro[:10], rp[:10])
            if bad > 3: break
    print('OK' if not bad else 'FAILED', f'{b - a} songs, {nw} register writes compared,', bad, 'mismatches')
