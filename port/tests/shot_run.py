#!/usr/bin/env python3
"""Run level_run() of the port on the real HOCUS.DAT with scripted input and save the visible page as PNG at chosen frames.
usage: shot_run.py <episode> <level> <out_prefix> frame [frame ...]"""
import sys, struct, ctypes, zlib
sys.argv, args = sys.argv[:1], sys.argv[1:]
import diff_run as r
from diff_video import snapshot
def png(path, w, h, rgb):
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xFFFFFFFF)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))
def grab(path):
    s = snapshot({}); pal = s['pal']; start = s['start']; out = bytearray()
    for y in range(200):
        for x in range(320):
            c = s['plane%d' % (x & 3)][(start + y * 80 + (x >> 2)) & 0xFFFF]
            out += bytes(min(255, pal[c * 3 + k] * 4 + 3 * (pal[c * 3 + k] > 0)) for k in range(3))
    png(path, 320, 200, bytes(out))
epi, lvl, prefix = int(args[0]), int(args[1]), args[2]; frames = [int(a) for a in args[3:]]
scr = r.script(7, max(frames) + 2)
r.reset_port(); r.lib.vga_reset(); r.lib.video_init()
r.epi_c.value = epi; r.lv.level_icon = lvl; r.diff_c.value = 1; r.speed_c.value = 0
assert r.lib.rng_load(r.E6, len(r.E6)) == 0
def frame(n):
    if n in frames: grab('%s_%d.png' % (prefix, n))
    if n > max(frames): return 1
    for k, _ in r.INPUTS: setattr(r.gin, k, scr[n][k])
    return 0
fcb = r.FI(frame); r.lhk.frame = ctypes.cast(fcb, ctypes.c_void_p).value
r.lib.level_run(0, 0); print('saved', len(frames), 'frames')
