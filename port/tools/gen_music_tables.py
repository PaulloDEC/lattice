#!/usr/bin/env python3
"""Dump the AdLib driver tables of HOCUS.EXE (data segment 1D51) into src/music_tables.c.   usage: gen_music_tables.py HOCUS.EXE > src/music_tables.c"""
import struct, sys
exe = open(sys.argv[1], 'rb').read(); hdr = int.from_bytes(exe[8:10], 'little') * 16
b = hdr + 0x1D51 * 16; d = exe[b:b + 0x10000]
def arr(name, typ, vals, per=12):
    s = f"{typ} {name}[{len(vals)}] = {{\n"
    for i in range(0, len(vals), per): s += "    " + ", ".join(str(v) for v in vals[i:i + per]) + ",\n"
    return s + "};\n"
out = '/* Generated from HOCUS.EXE (data segment 1D51): AdLib driver tables (tools/gen_music_tables.py). */\n#include "music.h"\n\n'
out += "/* DS:2FEC  operator slots of each OPL voice: {modulator, carrier} */\n" + arr('g_al_slot_voice', 'const uint16_t', list(struct.unpack_from('<18H', d, 0x2fec)), 2)
out += "/* DS:3010  slot -> register offset of the operator */\n" + arr('g_al_offset_slot', 'const uint8_t', list(d[0x3010:0x3010 + 18]), 18)
out += "/* DS:3022  note pitch (F-number) of the 12 semitones; the fine-tune rows are computed at start-up */\n" + arr('g_al_note_pitch0', 'const uint16_t', list(struct.unpack_from('<12H', d, 0x3022)), 12)
out += "/* DS:3322  octave bits of the pitch word */\n" + arr('g_al_octave_pitch', 'const uint16_t', list(struct.unpack_from('<8H', d, 0x3322)), 8)
out += "/* DS:396E  percussion map: per MIDI key {timbre, note} */\n" + arr('g_al_drum_map', 'const uint8_t', list(d[0x396e:0x396e + 256]), 16)
out += "/* DS:3A6E  timbre bank, 256 x 11 bytes (128 melodic + 128 percussion) */\n" + arr('g_al_timbre', 'const uint8_t', list(d[0x3a6e:0x3a6e + 256 * 11]), 11)
import math
fine = [[int(g * math.pow(2.0, di / 384.0)) for g in struct.unpack_from('<12H', d, 0x3022)] for di in range(32)]
out += "/* rows 0..31 of the fine-tune table (DS:3022 + 0x18 * row): trunc(pitch * 2^(row/384)); row 0 is the base table.  Computed by 161b:0838 at start-up. */\n" + 'const uint16_t g_al_fine[32][12] = {\n' + ''.join('    {' + ', '.join(map(str, r)) + '},\n' for r in fine) + '};\n'
sys.stdout.write(out)
