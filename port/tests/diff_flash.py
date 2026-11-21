#!/usr/bin/env python3
"""Differential test: 0BA5:220F (screen flash) vs level_flash()."""
import sys, struct, random, ctypes
from diff_enemy_spawn import *
import diff_effects as de
from diff_level import lv
from diff_video import snapshot
lib.level_flash.argtypes = []
def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    fl = r.randrange(1, 9); off = r.choice([0, 0x3F00, 0x7E00, 0xBD00, r.randrange(0, 0xC000)]); mm = r.choice([0xF, 0xF, 1, 5, 0xA])
    lib.vga_reset(); o = OS(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    o.call(de.SEG_VIDEO, 0x158, args=(mm,))
    o.uc.mem_write(DS * 16 + 0xCE78, struct.pack('<h', fl)); o.uc.mem_write(DS * 16 + 0x6460, struct.pack('<H', off))
    o.call(SEG_GAME, 0x220F); sa = snapshot({})
    lib.vga_reset(); lib.video_init(); load_state(st, None); lib.video_set_map_mask(mm); lv.flash = fl; lib.video_adjust_draw_offset(off)
    lib.level_flash(); sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    if o.rw(0xCE78) != (lv.flash & 0xFFFF): bad.append('flash')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]}')
    return bool(bad)
if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 100
    bad = sum(run(95000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
