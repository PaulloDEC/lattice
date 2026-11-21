#!/usr/bin/env python3
"""Differential test: modal boxes 0BA5:37FC (message) and 0BA5:38B6 (hint box) vs ui_message() / ui_hint_box().  The key wait is replaced on both sides
by the same scripted scancode flips."""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS, BASE_SEG, rnd_state, load_state
from diff_video import snapshot, SEG_VIDEO
from diff_level import TAB
import diff_text as dt
import diff_player_hits  # noqa
lib.ui_message.argtypes = [ctypes.c_char_p]; lib.ui_hint_box.argtypes = [ctypes.c_int]
POLL = ctypes.CFUNCTYPE(ctypes.c_int); lib.ui_set_poll.argtypes = [POLL]
gin = ctypes.c_uint8.in_dll(lib, 'g_in')   # placeholder; last_scancode is reached through the struct below
from diff_level import Inp
class In2(ctypes.Structure): _fields_ = Inp._fields_ + [('esc', ctypes.c_uint8), ('pause', ctypes.c_uint8), ('hot_v', ctypes.c_uint8), ('hot_s', ctypes.c_uint8), ('hot_m', ctypes.c_uint8), ('hot_c', ctypes.c_uint8), ('f1', ctypes.c_uint8), ('f10', ctypes.c_uint8), ('f3', ctypes.c_uint8), ('f2', ctypes.c_uint8), ('last_scancode', ctypes.c_uint8)]
gi = In2.in_dll(lib, 'g_in')
def _poll():
    gi.last_scancode = 0x90 if gi.last_scancode < 0x80 else 0x10
    return 0
pcb = POLL(_poll); lib.ui_set_poll(pcb)
HINTS = (ctypes.c_char * (10 * 504)).in_dll(lib, 'g_hints')
dp = ctypes.c_int16.in_dll(lib, 'g_demo_playback')
from unicorn import UC_HOOK_CODE
def run(seed):
    r = random.Random(seed)
    font = bytes(r.choice([0, 0x7E, 0x18, 0x81, 0xFF, r.randrange(256)]) for _ in range(720))
    kind = r.choice(['msg', 'hint', 'hint']); page_vis = r.randrange(2); last = r.choice([0x10, 0x90, 0x80, 0])
    hints = bytearray(r.randrange(256) for _ in range(10 * 504)) if False else bytearray(10 * 504)
    for k in range(10):
        nl = r.randrange(0, 11)
        for i in range(10):
            s = bytes(r.randrange(0x20, 0x7B) for _ in range(r.randrange(1, 30))) if i < nl else b''
            hints[k * 504 + 4 + i * 50: k * 504 + 4 + i * 50 + len(s)] = s
    idx = r.randrange(10); msg = bytes(r.randrange(0x21, 0x7B) if r.random() < .85 else 0x20 for _ in range(r.randrange(1, 40)))
    demo = r.choice([0, 0, 0, 1])
    st = rnd_state(r)
    lib.vga_reset(); o = de.O(); o.call(SEG_VIDEO, 0x185); load_state(st, o)
    o.uc.mem_write(dt.FONT_SEG * 16, font); o.uc.mem_write(dt.STR_SEG * 16, msg + b'\0')
    W = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    W(0x6440, struct.pack('<HH', 0, dt.FONT_SEG)); W(0xAAC4, bytes(hints)); W(0x7099, bytes([last])); W(0x790C, struct.pack('<h', demo)); W(0x48CE, struct.pack('<h', 0))
    o.call(SEG_VIDEO, 0x50, (page_vis,)) if False else None
    o.call(SEG_VIDEO, 0xA7, (page_vis,)); o.call(SEG_VIDEO, 0xE4, (1 - page_vis,))
    base = (0xBA5 + BASE_SEG) * 16
    def hp(uc, a, s, u): uc.mem_write(DS * 16 + 0x7099, bytes([0x90]))
    def hr(uc, a, s, u): uc.mem_write(DS * 16 + 0x7099, bytes([0x10]))
    for a in (0x3895, 0x39C5): o.uc.hook_add(UC_HOOK_CODE, hp, None, base + a, base + a)
    for a in (0x389E, 0x39CE): o.uc.hook_add(UC_HOOK_CODE, hr, None, base + a, base + a)
    # the demo branch calls 1392:AC (a delay): make it a no-op
    o.uc.mem_write((0x1392 + BASE_SEG) * 16 + 0xAC, b'\xcb')
    if kind == 'msg': o.call(0xBA5 + BASE_SEG, 0x37FC, (0, dt.STR_SEG))
    else: o.call(0xBA5 + BASE_SEG, 0x38B6, (idx,))
    sa = snapshot({}); la = o.dsb(0x7099, 1)[0]; pa = o.rw(0x6446)
    lib.vga_reset(); lib.video_init(); load_state(st, None); lib.ui_set_font(font)
    ctypes.memmove(ctypes.addressof(HINTS), bytes(hints), len(hints)); gi.last_scancode = last; dp.value = demo
    lib.video_show_page_now(page_vis); lib.video_set_draw_page(1 - page_vis)
    if kind == 'msg': lib.ui_message(msg)
    else: lib.ui_hint_box(idx)
    sb = snapshot({}); bad = [k for k in sa if sa[k] != sb[k]]
    if la != gi.last_scancode: bad.append('last')
    if pa != lib.video_draw_page(): bad.append('draw page')
    if bad: print(f'  seed {seed}: {kind} MISMATCH {bad[:5]}')
    return bool(bad)
if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    bad = sum(run(41000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
