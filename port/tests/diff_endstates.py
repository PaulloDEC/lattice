#!/usr/bin/env python3
"""Differential test: 0BA5:4E8B..50B1 (death and level-complete countdowns) vs level_end_states()."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import o_bytes, c_bytes
from diff_level import lv
from diff_player_hits import pl
from range_harness import run_range
from diff_video import snapshot, BASE_SEG
lib.level_end_states.argtypes = []
EV = ctypes.CFUNCTYPE(None, ctypes.c_int); lib.frame_set_event_hook.argtypes = [EV]
cev = []; evcb = EV(lambda e: cev.append(e)); lib.frame_set_event_hook(evcb)
class OP(OS):
    def __init__(self):
        super().__init__(); self.evq = []
        self.uc.mem_write((0x1392 + BASE_SEG) * 16 + 0x195, b'\xe6\xab\xcb'); self.uc.mem_write((0xBA5 + BASE_SEG) * 16 + 0x1B6, b'\xe6\xac\xcb')
    def wr(self, p, v):
        if p == 0xab: self.evq.append(1)
        elif p == 0xac: self.evq.append(2)
        else: super().wr(p, v)
LVN = [('death_timer', 0xCE8C), ('finish_timer', 0xCE8A), ('lives', 0x7048), ('items_left', 0x704A)]

def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    n = r.randrange(0, 8)
    L = dict(death_timer=r.choice([0, 0, 0x5A, 0x59, 0x5B, 0x1A, 0x19, 0x1B, 1, 2, r.randrange(0, 0x60)]), finish_timer=r.choice([0, 0, 0x5A, 0x59, 0x1A, 0x19, 0x1B, 1, 2, r.randrange(0, 0x60)]),
             lives=n, items_left=r.choice([n, n, n + 1, n - 1]))
    health = r.choice([0, 0, 1, 50, 100]); shield = r.choice([0, 400, 0x7530])
    cam = dict(pscreen_x=r.randrange(-3, 80), pscreen_y=r.randrange(-20, 190))
    lib.vga_reset(); o = OP(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for k, off in LVN: w(off, struct.pack('<h', L[k]))
    w(0x704C, struct.pack('<h', health)); w(0xCE8E, struct.pack('<h', shield)); w(0xCF2A, struct.pack('<h', cam['pscreen_x'])); w(0xCF26, struct.pack('<h', cam['pscreen_y']))
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    o.evq.clear(); run_range(o, 0x4E8B, 0x50B1); sa = snapshot({})
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for k, v in L.items(): setattr(lv, k, v)
    pl.health = health; pl.shield = shield
    for k, v in cam.items(): setattr(dc.cam, k, v)
    csnd.clear(); cev.clear(); lib.level_end_states(); sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    for k, off in LVN:
        if o.rw(off) != (getattr(lv, k) & 0xFFFF): bad.append(k)
    if o.rw(0xCE8E) != (pl.shield & 0xFFFF): bad.append('shield')
    if o.snd != csnd: bad.append('sound')
    if o.evq != cev: bad.append('events')
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k]]
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(91000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
