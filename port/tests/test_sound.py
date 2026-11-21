#!/usr/bin/env python3
"""Sanity test of the VOC decoder / mixer: every sound slot loads, the decoded length matches the VOC block lengths, the mix output is bounded,
and a voice ends by itself.  (No differential test: the original drives sound hardware.)"""
import ctypes, struct, sys, os
import diff_effects as de
from diff_effects import lib
import diff_run as dr
lib.sound_init.argtypes = [ctypes.c_int]; lib.sound_play.argtypes = [ctypes.c_int]; lib.sound_mix.argtypes = [ctypes.c_void_p, ctypes.c_int]
gson = ctypes.c_int16.in_dll(lib, 'g_sound_on')
n = lib.sound_init(44100); print('loaded', n); assert n == 16
tab = [struct.unpack_from('<HHH', dr.exe, 0x20680 + 6 * i) for i in range(16)]
SM = (ctypes.c_uint8 * 1).in_dll  # unused
for i in range(16):
    raw = dr.ent(tab[i][0]); p = struct.unpack_from('<H', raw, 20)[0]; total = 0; rate = None
    while raw[p] != 0:
        t = raw[p]; ln = raw[p + 1] | raw[p + 2] << 8 | raw[p + 3] << 16
        if t == 1: rate = 1000000 // (256 - raw[p + 4]); total += ln - 2
        elif t == 2: total += ln
        p += 4 + ln
    dur = total / rate
    gson.value = 1; lib.sound_play(i)
    buf = (ctypes.c_int16 * 4410)(); frames = 0; peak = 0
    while True:
        ctypes.memset(buf, 0, ctypes.sizeof(buf)); lib.sound_mix(buf, 4410)
        pk = max(abs(x) for x in buf); peak = max(peak, pk)
        if pk == 0 and frames > 0: break
        frames += 4410
        if frames > 44100 * 20: break
    print(f'sfx {i:2d}: {total:6d} samples @ {rate} Hz = {dur:5.2f}s, mixed {frames/44100:5.2f}s peak {peak}')
    assert abs(frames / 44100 - dur) < 0.25 and 0 < peak <= 32767
gson.value = 0; lib.sound_play(0); buf = (ctypes.c_int16 * 100)(); lib.sound_mix(buf, 100); assert not any(buf)
print('OK')
