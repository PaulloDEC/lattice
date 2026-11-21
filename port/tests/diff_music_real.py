#!/usr/bin/env python3
"""The game's real songs (HOCUS.DAT entries 598..612 that are Standard MIDI Files): original code vs port, register stream compared for N ticks."""
import sys, ctypes, random
import diff_music as m
from hocus_dat import HocusData
H = HocusData('/home/claude/hocus/HOCUS.EXE', '/home/claude/hocus/HOCUS.DAT')
songs = [(i, H.get(i)) for i in range(590, 620) if H.get(i)[:4] == b'MThd']
print('songs:', [(i, len(s)) for i, s in songs])
nticks = int(sys.argv[1]) if len(sys.argv) > 1 else 3000
import diff_music
def run_one(song, loop, vol, nt):
    o = m.MO(); o.uc.mem_write(m.SONG_SEG * 16, song)
    o.call(m.S161B, 0x95B); o.call(m.S144E, 0x6DC, (vol,)); o.call(m.S144E, 0x822, (0, m.SONG_SEG, loop))
    for t in range(nt): o.call(m.S144E, 0x15D, (0, m.TASK_SEG))
    log = []; rates = []
    cbw = m.WR(lambda reg, val: log.append((reg & 0xFF, val & 0xFF))); cbr = m.RATE(lambda v: rates.append(v & 0xFFFF))
    ctypes.c_void_p.in_dll(m.LM, 'g_al_write').value = ctypes.cast(cbw, ctypes.c_void_p).value
    m.LM.midi_set_rate_callback(cbr); m.LM.midi_reset(); m.LM.al_init(); m.LM.midi_set_volume(vol); m.LM.midi_play(song, len(song), loop)
    for t in range(nt): m.LM.midi_service()
    return o.log == log, o.rates == rates, len(log), o.rates[:4]
for i, s in songs:
    print(i, run_one(s, 1, 0xC0, nticks))
