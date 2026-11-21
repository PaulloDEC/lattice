#!/usr/bin/env python3
"""Differential test: original RNG 1392:0617 (run in Unicorn) vs port/src/rng.c."""
import sys, struct, random, ctypes, subprocess, os
sys.path.insert(0, '/home/claude/hocus')
from mz import MZ
from hocus_dat import HocusData
from unicorn import *
from unicorn.x86_const import *
here = os.path.dirname(os.path.abspath(__file__))
subprocess.check_call(['gcc', '-shared', '-fPIC', '-O1', '-std=c11', '-o', here + '/librng.so', here + '/../src/rng.c'])
lib = ctypes.CDLL(here + '/librng.so')
lib.rng_next.argtypes = [ctypes.c_int]; lib.rng_load.argtypes = [ctypes.c_char_p, ctypes.c_uint32]
lib.rng_set_index.argtypes = [ctypes.c_int]

BASE = 0x1000
m = MZ('/home/claude/hocus/HOCUS.EXE'); mem = bytearray(m.mem)
for off, seg in m.relocs:
    lin = seg * 16 + off; v = struct.unpack_from('<H', mem, lin)[0]; struct.pack_into('<H', mem, lin, (v + BASE) & 0xFFFF)
DS, CS = 0x1d51 + BASE, 0x1392 + BASE
uc = Uc(UC_ARCH_X86, UC_MODE_16); uc.mem_map(0, 0x200000); uc.mem_write(BASE * 16, bytes(mem))
uc.mem_write(0x0F00 * 16, b'\xf4')
H = HocusData('/home/claude/hocus/HOCUS.EXE', '/home/claude/hocus/HOCUS.DAT'); e6 = H.get(6)
assert lib.rng_load(e6, len(e6)) == 0 and lib.rng_load(e6, 5) == -1
uc.mem_write(DS * 16 + 0x711A, e6)

def orig(n):
    sp = 0x1000 - 6
    uc.mem_write(0x0E00 * 16 + sp, struct.pack('<HHH', 0, 0x0F00, n & 0xFFFF))
    uc.reg_write(UC_X86_REG_SS, 0x0E00); uc.reg_write(UC_X86_REG_SP, sp)
    uc.reg_write(UC_X86_REG_DS, DS); uc.reg_write(UC_X86_REG_CS, CS)
    uc.emu_start(CS * 16 + 0x617, 0x0F00 * 16, count=1000)
    return struct.unpack('<h', struct.pack('<H', uc.reg_read(UC_X86_REG_AX)))[0]
def oidx(): return struct.unpack('<H', bytes(uc.mem_read(DS * 16 + 0x7118, 2)))[0]

rng = random.Random(5); calls = bad = 0
for start in (0, 1, 500, 997, 998, 999, 1000, 0xFFFF) + tuple(rng.randrange(1000) for _ in range(6)):
    uc.mem_write(DS * 16 + 0x7118, struct.pack('<H', start)); lib.rng_set_index(start)
    for _ in range(2500):                       # > 2 full wraps of the 1000-entry table
        n = rng.choice([2, 3, 4, 5, 6, 7, 8, 10, 16, 20, 50, 100, 997, 1000, 5000, -3, 1])
        a, b = orig(n), lib.rng_next(n); calls += 1
        if a != b or oidx() != lib.rng_index():
            bad += 1
            if bad < 6: print(f"MISMATCH start={start} n={n}: orig={a} idx={oidx()} port={b} idx={lib.rng_index()}")
print(f"{calls} calls, {bad} mismatches")
sys.exit(1 if bad else 0)
