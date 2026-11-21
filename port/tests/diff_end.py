#!/usr/bin/env python3
"""Differential test: end-of-level screen 06B8:4889 (real text engine, string helpers, long division, itoa, x87 percentage, palette fades,
star field) vs ui_end_screen().  Stubs on both sides: key buffer (scripted), the IMG blitter (calls are logged)."""
import sys, struct, random, ctypes
import diff_effects as de
from diff_effects import lib, DS, BASE_SEG, E6
from diff_video import snapshot, SEG_VIDEO
import diff_run as dr
import diff_text as dt
from diff_level import lv
from unicorn.x86_const import UC_X86_REG_AX, UC_X86_REG_SP, UC_X86_REG_SS
SEG6 = 0x6B8 + BASE_SEG
lib.stars_set_table.argtypes = [ctypes.c_char_p]; lib.stars_set_table(dr.ent(5))
TRIG = (ctypes.c_int32 * 720).in_dll(lib, 'g_trig')
ANG = (ctypes.c_uint16 * 75).in_dll(lib, 'g_star_ang'); RAD = (ctypes.c_uint16 * 75).in_dll(lib, 'g_star_rad')
SPD = (ctypes.c_uint8 * 75).in_dll(lib, 'g_star_spd'); BR = (ctypes.c_uint8 * 75).in_dll(lib, 'g_star_br'); SP2 = (ctypes.c_uint8 * 75).in_dll(lib, 'g_star_spd2')
gdiff = ctypes.c_int16.in_dll(lib, 'g_difficulty'); gepi = ctypes.c_int16.in_dll(lib, 'g_episode'); gclock = ctypes.c_uint32.in_dll(lib, 'g_exit_clock')
lib.ui_end_screen.argtypes = [ctypes.c_int] * 3; lib.ui_end_screen.restype = ctypes.c_int
lib.video_target_palette.restype = ctypes.c_void_p
class UiHooks(ctypes.Structure): _fields_ = [('img', ctypes.c_void_p), ('kbhit', ctypes.c_void_p), ('getch', ctypes.c_void_p)]
uh = UiHooks.in_dll(lib, 'g_ui_hooks')
F0 = ctypes.CFUNCTYPE(None); FI = ctypes.CFUNCTYPE(ctypes.c_int); F3 = ctypes.CFUNCTYPE(None, ctypes.c_int, ctypes.c_int, ctypes.c_int)

class Keys:
    def __init__(self, pend, wait, after): self.pend, self.wait, self.after, self.calls, self.log, self.armed = pend, wait, after, 0, [], False
    def kbhit(self):
        if self.pend > 0: return 1
        if not self.armed: self.armed = True; return 0      # end of the initial flush
        self.calls += 1
        if self.calls > self.wait: self.pend = self.after; self.wait = 1 << 30; return 1
        return 0
    def getch(self): self.pend -= 1; self.log.append('g')

class EO(de.O):
    keys = None
    def __init__(self):
        super().__init__()
        w = lambda seg, off, b: self.uc.mem_write((seg + BASE_SEG) * 16 + off, b)
        w(0, 0x30BD, b'\xe4\xb0\x30\xe4\xcb'); w(0, 0x2E77, b'\xe6\xb1\xcb'); w(0x5D8, 0xA70, b'\xe6\xb2\xcb')
        self.imgs = []
    def wr(self, p, v):
        if p == 0xb1: self.keys.getch()
        elif p == 0xb2: self.imgs.append(tuple(self.arg(i) for i in range(3)))
        else: super().wr(p, v)
    def rd(self, p):
        if p == 0xb0: return self.keys.kbhit()
        return super().rd(p)

def patch_x87(o):
    for off in (0x4A20, 0x4A2A, 0x4A2E, 0x4A31, 0x4A36, 0x4A52, 0x4A89, 0x4A8D, 0x4A92):
        b = bytes(o.uc.mem_read(SEG6 * 16 + off, 2)); assert b[0] == 0xCD, hex(off)
        o.uc.mem_write(SEG6 * 16 + off, bytes([0x9B, 0xD8 + b[1] - 0x34]))
    o.uc.mem_write(SEG6 * 16 + 0x4A3A, b'\x9b\x90'); o.uc.mem_write(SEG6 * 16 + 0x4A96, b'\x9b\x90')
    p = lambda off, code: o.uc.mem_write(BASE_SEG * 16 + off, code)
    o.uc.mem_write((0x7000) * 16, b'\x9b\xdb\xe3\xcb'); o.call(0x7000 - BASE_SEG, 0, ()) if False else None
    p(0x113F, b'\x9b\xd9'); p(0x1143, b'\x90\x9b'); p(0x114C, b'\x9b\xd9'); p(0x1150, b'\x9b\xdf'); p(0x1157, b'\x9b\xd9')

def run(seed):
    r = random.Random(seed)
    finished = r.randrange(2); goal = r.choice([r.randrange(1, 70), r.randrange(1, 12)])
    collected = r.choice([goal, goal, 0, r.randrange(0, goal + 1), max(0, goal - 1)])
    diff = r.choice([0, 1, 2, 3]); epi = r.randrange(4); lvl = r.randrange(9); score = r.randrange(0, 1 << 20)
    par = lambda: int(dr.exe[0 + 0] * 0)  # placeholder
    from diff_effects import DS as _DS
    tab = struct.unpack_from('<36H', dr.exe, (struct.unpack_from('<H', dr.exe, 8)[0] * 16) + 0x1D51 * 16 + 0x198A)
    p_ = tab[epi * 9 + lvl]
    clock = r.choice([(p_ + r.randrange(-3, 4)) * 140 + r.randrange(140), p_ * 140 + r.randrange(140), p_ * 140 + r.randrange(140), r.randrange(0, 600 * 140), r.randrange(0, 1 << 20)])
    font = bytes(r.choice([0, 0x7E, 0x18, 0x81, 0xFF, r.randrange(256)]) for _ in range(720))
    pend = r.randrange(0, 3); wait = r.randrange(0, 5); after = r.randrange(1, 3)
    rngi = r.randrange(1000); pal = bytes(r.randrange(64) for _ in range(768))
    ang = []; rad = []
    while len(ang) < 75:
        a = r.randrange(360); rr = r.randrange(0, 200)
        x = int(TRIG[a * 2] * rr / 1000000) + 0xA0; y = int(TRIG[a * 2 + 1] * rr / 1000000) + 0x70
        if 0 <= x <= 0x13F and 0 <= y <= 0xC7: ang.append(a); rad.append(rr)
    spd = [r.randrange(1, 6) for _ in range(75)]; br = [r.randrange(256) for _ in range(75)]; sp2 = [r.randrange(1, 6) for _ in range(75)]
    vis = r.randrange(2)
    lib.vga_reset(); o = EO(); o.keys = Keys(pend, wait, after); o.call(SEG_VIDEO, 0x185); patch_x87(o)
    W = lambda off, b: o.uc.mem_write(DS * 16 + off, b)
    W(0x4CA8, struct.pack('<720i', *TRIG)); W(0x711A, E6); W(0x7118, struct.pack('<h', rngi))
    W(0x7B6C, struct.pack('<75H', *ang)); W(0x7AD6, struct.pack('<75H', *rad)); W(0x7A8A, bytes(spd)); W(0x7A3F, bytes(br)); W(0x79F4, bytes(sp2))
    W(0x6440, struct.pack('<HH', 0, dt.FONT_SEG)); o.uc.mem_write(dt.FONT_SEG * 16, font)
    W(0x706A, struct.pack('<h', diff)); W(0x706C, struct.pack('<h', lvl)); W(0x706E, struct.pack('<h', epi)); W(0x7052, struct.pack('<I', score))
    W(0x7904, struct.pack('<I', clock)); W(0x48CE, struct.pack('<h', 0)); W(0x6A64, pal)
    o.call(SEG_VIDEO, 0xA7, (vis,)); o.call(SEG_VIDEO, 0xE4, (1 - vis,))
    ra = o.call(SEG6, 0x4889, (finished, goal, collected)) if False else None
    o.call(SEG6, 0x4889, (finished, goal, collected))
    ax = o.uc.reg_read(UC_X86_REG_AX) & 0xFFFF
    sa = snapshot({}); da_ = (o.dsb(0x7052, 4), o.dsb(0x7B6C, 150), o.dsb(0x7AD6, 150), o.dsb(0x7A8A, 75), o.dsb(0x7A3F, 75), o.dsb(0x79F4, 75), o.dsb(0x7118, 2))
    pa = o.rw(0x6446)
    # ---- port ----
    k2 = Keys(pend, wait, after); imgs = []
    kb = FI(k2.kbhit); gc = F0(k2.getch); im = F3(lambda e, x, y: imgs.append((e, x, y)))
    uh.img = ctypes.cast(im, ctypes.c_void_p).value; uh.kbhit = ctypes.cast(kb, ctypes.c_void_p).value; uh.getch = ctypes.cast(gc, ctypes.c_void_p).value
    lib.vga_reset(); lib.video_init(); assert lib.rng_load(E6, len(E6)) == 0; lib.rng_set_index(rngi); lib.ui_set_font(font)
    ctypes.memmove(lib.video_target_palette(), pal, 768)
    for i in range(75): ANG[i] = ang[i]; RAD[i] = rad[i]; SPD[i] = spd[i]; BR[i] = br[i]; SP2[i] = sp2[i]
    gdiff.value = diff; gepi.value = epi; lv.level_icon = lvl; lv.score = score; gclock.value = clock
    lib.video_show_page_now(vis); lib.video_set_draw_page(1 - vis)
    rb = lib.ui_end_screen(finished, goal, collected)
    sb = snapshot({}); db = (struct.pack('<I', lv.score), bytes(ANG), bytes(RAD), bytes(SPD), bytes(BR), bytes(SP2), struct.pack('<h', lib.rng_index()))
    bad = [k for k in sa if sa[k] != sb[k]]
    bad += [f'v{i}' for i in range(7) if da_[i] != db[i]]
    if o.imgs != imgs: bad.append('imgs')
    if o.keys.log != k2.log or o.keys.pend != k2.pend: bad.append('keys')
    if pa != lib.video_draw_page(): bad.append('draw page')
    if ax != rb: bad.append('ret')
    if bad: print(f'  seed {seed}: fin={finished} goal={goal} got={collected} diff={diff} MISMATCH {bad[:6]}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 60
    bad = sum(run(61000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
