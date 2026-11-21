#!/usr/bin/env python3
"""Differential test: text routines of 06B8 (05E9 solid, 071F gradient, 087C width) vs ui.c: random fonts / strings / positions / pages."""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS, BASE_SEG, rnd_state, load_state
from diff_video import Orig, snapshot, SEG_VIDEO
lib.ui_set_font.argtypes = [ctypes.c_char_p]
lib.text_draw.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
lib.text_draw_grad.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_char_p]
lib.text_width.argtypes = [ctypes.c_char_p]; lib.text_width.restype = ctypes.c_int
lib.video_set_draw_page.argtypes = [ctypes.c_int]
SEG06B8 = 0x6B8 + BASE_SEG; FONT_SEG, STR_SEG = 0x5000, 0x5100
FN = {'draw': (0x5E9, lib.text_draw), 'grad': (0x71F, lib.text_draw_grad)}
def run(seed):
    r = random.Random(seed)
    font = bytes(r.choice([0, 0, 0x7E, 0x18, 0x81, 0xFF, r.randrange(256)]) if r.random() < .6 else r.randrange(256) for _ in range(720))
    n = r.randrange(0, 14)
    s = bytes(r.choice([0x20, 0x21, 0x7A, 0x7B, 0x1F, 0x41, 0x61, r.randrange(1, 256), r.randrange(0x21, 0x7B)]) for _ in range(n))
    s = bytes(c for c in s if c)
    x = r.choice([r.randrange(-20, 330), r.randrange(0, 300), 0, 1, 2, 3]); y = r.randrange(-3, 195)
    kind = r.choice(['draw', 'grad', 'width']); page = r.randrange(4); colour = r.randrange(256); style = r.randrange(0, 11)
    st = rnd_state(r)
    lib.vga_reset(); o = de.O(); o.call(SEG_VIDEO, 0x185); load_state(st, o)
    o.uc.mem_write(FONT_SEG * 16, font); o.uc.mem_write(STR_SEG * 16, s + b'\0')
    o.uc.mem_write(DS * 16 + 0x6440, struct.pack('<HH', 0, FONT_SEG))
    o.call(SEG_VIDEO, 0xE4, (page,))
    if kind == 'width':
        o.call(SEG06B8, 0x87C, (0, STR_SEG)); ra = o.uc.reg_read(__import__('unicorn.x86_const', fromlist=['x']).UC_X86_REG_AX) & 0xFFFF
    else:
        a2 = colour if kind == 'draw' else style
        o.call(SEG06B8, FN[kind][0], (x, y, a2, 0, STR_SEG)); ra = None
    sa = snapshot({})
    lib.vga_reset(); lib.video_init(); load_state(st, None); lib.ui_set_font(font); lib.video_set_draw_page(page)
    if kind == 'width': rb = lib.text_width(s) & 0xFFFF
    else: FN[kind][1](x, y, colour if kind == 'draw' else style, s); rb = None
    sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    if ra != rb: bad.append(f'ret {ra} {rb}')
    if bad: print(f'  seed {seed}: {kind} x={x} y={y} MISMATCH {bad[:5]}')
    return bool(bad)
if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    bad = sum(run(31000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
