#!/usr/bin/env python3
"""Differential test: 0BA5:39F3 (player / tile interaction) vs level_player_tiles().
Random info layer around the player (items of every kind, teleporters, levers, doors, spawn triggers), random item
table, lever / door / hint / spawn tables, layers, effect pools, VGA state.  38B6 (hint box) is recorded on both
sides; sound hook recorded.  Compared: every variable the function touches, the layers, tables, pending list,
effect pools, VRAM/VGA state, RNG, sound and hint sequences."""
import sys, struct, random, ctypes
import diff_enemy_spawn as ds
from diff_enemy_spawn import *
import diff_camera as dc
import diff_effects as de
from diff_effects import fx, o_bytes, c_bytes
from diff_player_hits import pl
import diff_animate as da
from diff_video import snapshot, BASE_SEG
lib.level_player_tiles.argtypes = []
N = 14400; INFO_SEG = 0x5000
LV = [('collected', 0xCE6A), ('goal', 0xCE68), ('complete', 0xCFD8), ('lives', 0x7048), ('flash', 0xCE78), ('mana', 0x7046),
      ('potion_timer', 0xCE88), ('potion_saved', 0xCE86), ('key1', 0xCE84), ('key2', 0xCE82), ('item4_flag', 0xCF10),
      ('item4_timer', 0xCF0E), ('score', 0x7052), ('ce94', 0xCE94), ('ce98', 0xCE98), ('ce96', 0xCE96),
      ('teleport', 0xCF06), ('tele_row_px', 0xCF04), ('tele_col2', 0xCF02), ('ce92', 0xCE92), ('level_icon', 0x706C), ('items_left', 0x704A), ('death_timer', 0xCE8C), ('finish_timer', 0xCE8A), ('facing', 0xCE90), ('hint_shown', 0xCF4E),
      ('check_x', 0xCF52), ('check_y', 0xCF50), ('tile_blank', 0x6D68), ('tile_lever_a', 0x6D69), ('tile_lever_b', 0x6D6A)]
T = {'score': ctypes.c_uint32, 'tile_blank': ctypes.c_uint8, 'tile_lever_a': ctypes.c_uint8, 'tile_lever_b': ctypes.c_uint8}
class Lvs(ctypes.Structure): _fields_ = [(n, T.get(n, ctypes.c_int16)) for n, _ in LV]
lv = Lvs.in_dll(lib, 'g_lv')
class Inp(ctypes.Structure): _fields_ = [(n, ctypes.c_uint8) for n in ('left','right','down','scroll_up','scroll_down','jump','fire','up','up2','latch_jump','latch_fire','latch_up')]
gin = Inp.in_dll(lib, 'g_in')
SZ = {'score': 4, 'tile_blank': 1, 'tile_lever_a': 1, 'tile_lever_b': 1}
TAB = {'g_tile_items': (0x2628, 23 * 42), 'g_teleporters': (0xAA94, 40), 'g_levers': (0xA89A, 23 * 22), 'g_doors1': (0xA76E, 300),
       'g_doors2': (0xA642, 300), 'g_hints': (0xAAC4, 10 * 504), 'g_spawn_groups': (0x8612, 250 * 32)}
info_buf = (ctypes.c_int16 * N)(); ctypes.c_void_p.in_dll(lib, 'g_map_info').value = ctypes.addressof(info_buf)
HINTS = []
HCB = ctypes.CFUNCTYPE(None, ctypes.c_int)
def _hint(i):
    HINTS.append((i, lib.vga_get_map_mask(), lib.vga_get_bit_mask())); lib.vga_set_map_mask(1 + i % 7); lib.vga_set_bit_mask(0x55)   # the real box draws: leaves other register values
hcb = HCB(_hint); lib.level_set_hint_callback(hcb)

def _hint_o(o, i):
    o.hints.append((i, lib.vga_get_map_mask(), lib.vga_get_bit_mask())); lib.vga_set_map_mask(1 + i % 7); lib.vga_set_bit_mask(0x55)

class OS3(OS):
    def __init__(self):
        super().__init__(); self.hints = []
        self.uc.mem_write((0xBA5 + BASE_SEG) * 16 + 0x38B6, b'\xe6\xa6\xcb')
    def wr(self, p, v):
        if p == 0xa6: _hint_o(self, self.arg(0))
        else: super().wr(p, v)

def run(seed):
    r = random.Random(seed); st = rnd_state(r)
    row = r.randrange(0, 58); px = r.randrange(0, 479); cell0 = (row * 240 + (px + 1) // 2)
    camrow = r.randrange(0, 50); camcol = r.randrange(0, 220); pscx = r.randrange(-5, 80); pscy = r.randrange(0, 200); py = r.randrange(0, 960)
    ptile = px // 2 if r.random() < .8 else r.randrange(0, 240)
    near = [cell0 + a + 240 * b for a in (-1, 0, 1) for b in (0, 1)]
    items = bytearray(); ikind = []
    for v in range(23):
        real = [(100, 0, 0, 0), (250, 0, 0, 0), (500, 0, 0, 0), (1000, 0, 0, 0), (0, 10, 0, 0)] + [(0, 0, 0, k) for k in range(1, 13)]
        if r.random() < .5 and v < 17: p, h, m, k = (list(real[v]) + [0])[:4] if v < 5 else (0, 0, 0, real[v][3])
        else: p, h, m, k = r.choice([0, 0, 100, 250, 500, 1000, 5000, 77, -5]), r.choice([0, 0, 0, 10, 255, 7]), r.choice([0, 0, 1, 3, 255]), r.randrange(0, 14)
        if v < 5 and r.random() < .5: p, h, m, k = real[v][0], real[v][1], real[v][2], real[v][3]
        items += struct.pack('<hBBB', p, h, m, k) + bytes(37); ikind.append((p, h, k))
    focus = r.choice([None, None, 'tele', 'lever', 'door', 'spawn', 'hint', 'items'] + [('k', k) for k in range(1, 13)])
    def val():
        if focus is not None and r.random() < .7:
            if focus == 'tele': return r.randrange(23, 33)
            if focus == 'lever': return r.randrange(33, 56)
            if focus == 'door': return r.randrange(56, 106)
            if focus == 'spawn': return r.choice([r.randrange(116, 366), 0x74, 0x16D, 0x16D])
            if focus == 'hint': return r.choice([k for k in range(23) if ikind[k][2] == 10] or [r.randrange(23)])
            if focus == 'items': return r.randrange(0, 23)
            ks = [v for v in range(23) if ikind[v][2] == focus[1] and ikind[v][0] == 0 and ikind[v][1] == 0]
            if ks: return r.choice(ks)
        c = r.random()
        if c < .25: return 0x7530
        if c < .5: return r.randrange(0, 23)
        if c < .6: return r.randrange(23, 33)
        if c < .7: return r.randrange(33, 56)
        if c < .8: return r.randrange(56, 81)
        if c < .85: return r.randrange(81, 106)
        if c < .93: return r.randrange(116, 366)
        return r.choice([106, 115, 366, 0x16D, 0x16D, 0x16C, 0x74, 0x6A, 0x20, 0x21, 0x37, 0x38, 0x50, 0x51, 0x69, 0x17, 0x16])
    info = [0x7530] * N
    for c in near: info[c] = val()
    for _ in range(40): info[r.randrange(0, N)] = val()
    def lay(p0): return bytes(0xFF if r.random() < p0 else r.randrange(0, 255) for _ in range(N))
    fg, alt, bg = lay(.5), lay(.5), bytearray(lay(.2)); dyn = bytes(r.choice([0, 0, 0, 1, 2]) for _ in range(N))
    blank = r.choice([0xFF, 0, r.randrange(0, 255)]); la, lb = r.randrange(0, 255), r.randrange(0, 255)
    if r.random() < .05: lb = la
    for c in range(0, N, 7):
        if r.random() < .5: bg[c] = r.choice([la, lb, blank, r.randrange(256)])
    for c in near: bg[c] = r.choice([la, lb, blank, r.randrange(256)])
    bg = bytes(bg)
    def anycell(): return r.choice([r.choice(near), r.randrange(0, N), r.randrange(0, N)])
    # tables
    tl = [[anycell() if r.random() < .7 else r.randrange(0, N), r.randrange(0, N)] for _ in range(10)]
    for c in near:
        if 23 <= info[c] <= 32 and r.random() < .85: tl[info[c] - 23][0] = c
    tele = b''.join(struct.pack('<hh', *t) for t in tl)
    levers = bytearray()
    for _ in range(23):
        cells = [r.choice([-1, -1, anycell()]) for _ in range(4)]; exp = [r.choice([la, lb, la, lb, 0, r.randrange(256)]) for _ in range(4)]
        if r.random() < .15: cells = [-1] * 4
        c0 = r.randrange(0, 235); r0 = r.randrange(0, 55)
        levers += struct.pack('<BB4h4B4h', r.randrange(0, 3), 0, *cells, *exp, c0, r0, c0 + r.randrange(-1, 5), r0 + r.randrange(-1, 4))
    def doors():
        d = bytearray()
        for _ in range(25):
            c0 = r.randrange(0, 235); r0 = r.randrange(0, 55)
            d += struct.pack('<hBBhhhh', r.choice([0, 0, 1, 2, 1, 2]), r.randrange(256), 0, c0, r0, c0 + r.randrange(-1, 5), r0 + r.randrange(-1, 4))
        return bytes(d)
    d1, d2 = doors(), doors()
    hints = bytearray()
    for _ in range(10):
        hints += struct.pack('<hh', ptile + r.randrange(-4, 5) if r.random() < .7 else r.randrange(0, 240), row if r.random() < .7 else r.randrange(0, 60)) + bytes(500)
    groups = bytearray()
    for v in range(250):
        cs = [anycell() if r.random() < .9 else r.randrange(0, N) for _ in range(8)]
        if r.random() < .3: cs = [r.choice(near + [near[0]]) for _ in range(8)]
        groups += struct.pack('<16h', *(cs + [0] * 8))
    tabs = {'g_tile_items': bytes(items), 'g_teleporters': tele, 'g_levers': bytes(levers), 'g_doors1': d1, 'g_doors2': d2,
            'g_hints': bytes(hints), 'g_spawn_groups': bytes(groups)}
    coll = r.randrange(0, 50)
    V = dict(collected=coll, goal=r.choice([coll + 1, coll + 1, coll + 2, 0, r.randrange(0, 60)]), complete=r.randrange(2), lives=r.randrange(0, 5), flash=r.randrange(0, 9),
             mana=r.choice([0, 5, 9, 10, 11, r.randrange(0, 15)]), potion_timer=r.choice([0, 0, 5]), potion_saved=r.choice([0, 8, 9, 10, r.randrange(0, 12)]),
             key1=r.randrange(2), key2=r.randrange(2), item4_flag=r.randrange(2), item4_timer=r.randrange(0, 30),
             score=r.choice([0, 12345, 0xFFFFFF00, r.randrange(0, 1 << 32)]), use_flag=r.choice([0, 1, 1, 0xFF]), ce94=r.randrange(0, 30), ce98=r.randrange(2), ce96=r.randrange(0, 9),
             teleport=r.randrange(2), tele_row_px=r.randrange(0, 900), tele_col2=r.randrange(0, 480), ce92=r.randrange(-5, 5), level_icon=r.randrange(0, 12), items_left=r.randrange(0, 5), death_timer=r.randrange(0, 3), finish_timer=r.randrange(0, 3), facing=r.randrange(2), hint_shown=r.randrange(2),
             check_x=r.randrange(0, 480), check_y=r.randrange(0, 900), tile_blank=blank, tile_lever_a=la, tile_lever_b=lb)
    health = r.choice([100, 99, 101, r.randrange(-20, 130)]); invuln = r.choice([0, 0, 0, 1, 20]); shield = r.choice([0, 0, 0, 1, 400])
    dif = r.randrange(0, 4); tmr = r.choice([0, 5, -1]); vfreeze = r.randrange(0, 3)
    pend = [r.choice([-1, -1, -1, r.randrange(0, N), r.choice(near)]) for _ in range(32)]
    ecell = [r.choice([-1, r.randrange(0, N), r.choice(near)]) for _ in range(8)]
    c1be = r.randrange(0, 65536); cam_vals = dict(player_row=row, player_x=px, player_tile=ptile, pscreen_x=pscx, pscreen_y=pscy, player_y=py,
                                                  cam_col=camcol, cam_row=camrow, vfreeze=vfreeze)
    # ---- original
    lib.vga_reset(); o = OS3(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    w = lambda off, data: o.uc.mem_write(DS * 16 + off, data)
    w(0x705A, struct.pack('<HH', 0, INFO_SEG + BASE_SEG)); o.uc.mem_write((INFO_SEG + BASE_SEG) * 16, struct.pack('<%dh' % N, *info))
    for k, seg in da.SEGS.items(): w(da.PTR[k], struct.pack('<HH', 0, seg + 0))
    for k, data in (('dyn', dyn), ('alt', alt), ('fg', fg), ('bg', bg)): o.uc.mem_write(da.SEGS[k] * 16, data)
    for n, (off, sz) in TAB.items(): w(off, tabs[n])
    for n, off in LV: w(off, struct.pack('<I' if n == 'score' else '<B' if n in SZ else '<h', V[n]))
    w(0x70AE, bytes([V['use_flag']]))
    w(0x704C, struct.pack('<h', health)); w(0xCE74, struct.pack('<h', invuln)); w(0xCE8E, struct.pack('<h', shield)); w(0x706A, struct.pack('<h', dif))
    w(0xCE76, struct.pack('<h', tmr)); w(0xC134, struct.pack('<32h', *pend)); w(0xC004, struct.pack('<8h', *ecell)); w(0xC1BE, struct.pack('<H', c1be))
    for n, off in (('player_row', 0xCF2E), ('player_x', 0xCF32), ('player_tile', 0xCF30), ('pscreen_x', 0xCF2A), ('pscreen_y', 0xCF26), ('player_y', 0xCF28),
                   ('cam_col', 0xCE7E), ('cam_row', 0xCE7A), ('vfreeze', 0xCF0C)): w(off, struct.pack('<h', cam_vals[n]))
    lib.vga_reset(); o.call(de.SEG_VIDEO, 0x185); load_state(st, o)
    o.call(SEG_GAME, 0x39F3, ()); sa = snapshot({})
    # ---- port
    lib.vga_reset(); lib.video_init(); load_state(st, None)
    for i in range(N): info_buf[i] = info[i]
    bufs = {k: ctypes.create_string_buffer(d, N) for k, d in (('dyn', dyn), ('alt', alt), ('fg', fg), ('bg', bg))}
    for k in bufs: setattr(da.gm, k, ctypes.addressof(bufs[k]))
    for n, (off, sz) in TAB.items(): ctypes.memmove(ctypes.addressof((ctypes.c_char * sz).in_dll(lib, n)), tabs[n], sz)
    for n, _ in LV: setattr(lv, n, V[n])
    gin.up = V['use_flag']
    pl.health = health; pl.invuln = invuln; pl.shield = shield; ds.diff.value = dif; da.gm.timer = tmr
    for i in range(32): ds.en.pending[i] = pend[i]
    for i in range(8): ds.en.cell[i] = ecell[i]
    ds.spr.hdr[15] = c1be
    for n, v in cam_vals.items(): setattr(dc.cam, n, v)
    csnd.clear(); HINTS.clear(); bad = []
    lib.level_player_tiles(); sb = snapshot({})
    bad += [k for k in sa if sa[k] != sb[k]]
    for n, off in LV:
        a = struct.unpack('<I' if n == 'score' else '<B' if n in SZ else '<H', o.dsb(off, SZ.get(n, 2)))[0]
        b = getattr(lv, n) & (0xFFFFFFFF if n == 'score' else 0xFF if n in SZ else 0xFFFF)
        if a != b: bad.append(n)
    for n, off in (('health', 0x704C), ('invuln', 0xCE74), ('shield', 0xCE8E)):
        if o.rw(off) != (getattr(pl, n) & 0xFFFF): bad.append(n)
    for n, off in (('player_y', 0xCF28), ('vfreeze', 0xCF0C)):
        if o.rw(off) != (getattr(dc.cam, n) & 0xFFFF): bad.append(n)
    if o.rw(0xCE76) != (da.gm.timer & 0xFFFF): bad.append('timer')
    if bytes(o.uc.mem_read((INFO_SEG + BASE_SEG) * 16, 2 * N)) != struct.pack('<%dh' % N, *info_buf): bad.append('info')
    for k in da.SEGS:
        if bytes(o.uc.mem_read(da.SEGS[k] * 16, N)) != bufs[k].raw[:N]: bad.append('layer_' + k)
    for n, (off, sz) in TAB.items():
        if o.dsb(off, sz) != bytes((ctypes.c_char * sz).in_dll(lib, n)): bad.append(n)
    if struct.unpack('<32h', o.dsb(0xC134, 64)) != tuple(ds.en.pending): bad.append('pending')
    ob, cb_ = o_bytes(o), c_bytes(); bad += [k for k in ob if ob[k] != cb_[k]]
    if o.dsb(0x70AE, 1)[0] != gin.up: bad.append('use_flag')
    if o.snd != csnd: bad.append('sound')
    if o.hints != HINTS: bad.append('hints')
    if bad: print(f'  seed {seed}: MISMATCH {sorted(set(bad))[:8]} snd={o.snd} vs {csnd}')
    return bool(bad)

if __name__ == '__main__':
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    bad = sum(run(31000 + s) for s in range(n)); print(f'{n} scenarios, {bad} mismatches'); sys.exit(1 if bad else 0)
