#!/usr/bin/env python3
"""Differential test: 0BA5:50B1..53FF (player sprite, sparkles, shot icons, shield countdown) vs player_draw()."""
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
lib.player_draw.argtypes = []
class Inp(ctypes.Structure): _fields_ = [(n, ctypes.c_uint8) for n in ('left','right','down','scroll_up','scroll_down','jump','fire','up','up2','latch_jump','latch_fire','latch_up')]
class Fr(ctypes.Structure): _fields_ = [('rec', ctypes.c_uint8 * 6), ('demo_pos', ctypes.c_int16), ('fire_held', ctypes.c_uint8), ('up_held', ctypes.c_uint8), ('local_c', ctypes.c_uint8), ('anim_toggle', ctypes.c_uint8), ('jump_end', ctypes.c_uint8), ('landed', ctypes.c_uint8)]
fr = Fr.in_dll(lib, 'g_frame')
MV = ['fall', 'jump_idx', 'jump_len', 'jump_peak', 'jump_tab', 'step_r_up', 'step_l_up', 'free_r', 'free_l', 'moved', 'look_off', 'plat_down', 'plat_up', 'plat_tile_a', 'plat_tile_b']
class Mv(ctypes.Structure): _fields_ = [(n, ctypes.c_void_p if n == 'jump_tab' else ctypes.c_int16) for n in MV]
mv = Mv.in_dll(lib, 'g_mv')
BL = ctypes.CFUNCTYPE(None, *([ctypes.c_int] * 7)); lib.frame_set_blit_hook.argtypes = [BL]
NARGS = {0x249E: 6, 0x2896: 6, 0x26B3: 6, 0x27E2: 5, 0x25FF: 5, 0x23BA: 5}
cblits = []
def _bl(which, *a): cblits.append((which,) + tuple(v & 0xFFFF for v in a[:NARGS[which]]))
blcb = BL(_bl); lib.frame_set_blit_hook(blcb)
STUB = {0x249E: 0x9d, 0x2896: 0xa5, 0x27E2: 0xa7, 0x26B3: 0xa8, 0x25FF: 0xa9, 0x23BA: 0xaa}
PORT = {v: k for k, v in STUB.items()}
class OP(OS):
    def __init__(self):
        super().__init__(); self.blq = []
        for addr, p in STUB.items(): self.uc.mem_write((0xBA5 + BASE_SEG) * 16 + addr, bytes([0xe6, p, 0xcb]))
    def wr(self, p, v):
        if p in PORT: self.blq.append((PORT[p],) + tuple(self.arg(i) for i in range(NARGS[PORT[p]])))
        else: super().wr(p, v)

LVN = [('teleport', 0xCF06), ('facing', 0xCE90), ('ce92', 0xCE92), ('potion_timer', 0xCE88), ('item4_timer', 0xCF0E), ('item4_flag', 0xCF10), ('ce94', 0xCE94), ('ce98', 0xCE98), ('ce96', 0xCE96)]
MVN = [('fall', 0xCF08), ('jump_idx', 0xCF16), ('jump_peak', 0xCF14), ('moved', 0xCF1C)]
HD = [(13, 0xC1BA), (19, 0xC1C6), (20, 0xC1C8), (21, 0xC1CA), (23, 0xC1CE), (26, 0xC1D4)]

def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    L = dict(teleport=0 if r.random() < .6 else r.choice([r.randrange(0, 45), 4, 5, 9, 0xA, 0xE, 0xF, 0x13, 0x14, 0x19, 0x1A, 0x1D, 0x1E, 0x22, 0x23, 0x27, 0x28, 0x29]), facing=r.randrange(2), ce92=r.randrange(0, 20), potion_timer=r.choice([0, 1, 2, 7, 600]),
             item4_timer=r.choice([0, 0, 1, 20]), item4_flag=r.randrange(2), ce94=r.choice([0, 1, 2, 3, 3, 4, 9]), ce98=r.choice([0, 1, 1]), ce96=r.choice([1, 1, 2, 5, 0]))
    pk = r.choice([9, 0xC]); M = dict(fall=r.choice([0, 0, 0, 1]), jump_idx=r.choice([r.randrange(0, 22), pk - 1, pk, pk + 1]), jump_peak=pk, moved=r.choice([0, 0, 1]))
    cam = dict(pscreen_x=r.randrange(-3, 80), pscreen_y=r.choice([r.randrange(-20, 190), r.randrange(0, 190), -1, 0, 1]), vfreeze=r.choice([0, 1]))
    inv = r.choice([0, 1, 2, 3, 20, -1]); shield = r.choice([0, 0, 0, 0, 0, 1, 2, 400, 0x7530]); fh = r.choice([0, 1, 1]); uh = r.randrange(2)
    hd = {k: r.randrange(0, 65536) if k in (13, 23) else r.randrange(0, 30) for k, _ in HD}
    lib.vga_reset(); o = OP(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    for n, off in LVN: w(off, struct.pack('<h', L[n]))
    for n, off in MVN: w(off, struct.pack('<h', M[n]))
    w(0xCF2A, struct.pack('<h', cam['pscreen_x'])); w(0xCF26, struct.pack('<h', cam['pscreen_y'])); w(0xCF0C, struct.pack('<h', cam['vfreeze']))
    w(0xCE74, struct.pack('<h', inv)); w(0xCE8E, struct.pack('<h', shield)); w(0xCF49, bytes([fh])); w(0xCF48, bytes([uh]))
    for k, off in HD: w(off, struct.pack('<H', hd[k]))
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    o.blq.clear(); run_range(o, 0x50B1, 0x53FF); sa = snapshot({}); oseq = list(o.blq)
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for n, v in L.items(): setattr(lv, n, v)
    for n, v in M.items(): setattr(mv, n, v)
    for n, v in cam.items(): setattr(dc.cam, n, v)
    pl.invuln = inv; pl.shield = shield; fr.fire_held = fh; fr.up_held = uh
    for k, _ in HD: ds.spr.hdr[k] = hd[k]
    csnd.clear(); cblits.clear(); lib.player_draw(); sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    for n, off in LVN:
        if o.rw(off) != (getattr(lv, n) & 0xFFFF): bad.append(n)
    if o.rw(0xCE8E) != (pl.shield & 0xFFFF): bad.append('shield')
    if o.snd != csnd: bad.append('sound')
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k]]
    if oseq != cblits: bad.append('blits')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(81000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
