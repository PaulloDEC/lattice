"""Run the original code between two addresses of segment 0BA5 inside a fake frame of 453A."""
import struct
from unicorn import *
from unicorn.x86_const import *
from diff_video import STACK_SEG, DS, BASE_SEG
BP = 0x0F00
def run_range(o, start, end, mode=0, pause=0, locals_=None, count=20_000_000):
    uc = o.uc; seg = 0xBA5 + BASE_SEG
    uc.mem_write(STACK_SEG * 16, bytes(0x1000))
    uc.mem_write(STACK_SEG * 16 + BP + 6, struct.pack('<hh', mode, pause))
    for off, data in (locals_ or {}).items(): uc.mem_write(STACK_SEG * 16 + BP + off, data)
    uc.reg_write(UC_X86_REG_SS, STACK_SEG); uc.reg_write(UC_X86_REG_BP, BP); uc.reg_write(UC_X86_REG_SP, BP - 0x40)
    uc.reg_write(UC_X86_REG_DS, DS); uc.reg_write(UC_X86_REG_ES, DS); uc.reg_write(UC_X86_REG_CS, seg)
    uc.emu_start(seg * 16 + start, seg * 16 + end, count=count)
def local(o, off, n=2): return bytes(o.uc.mem_read(STACK_SEG * 16 + BP + off, n))
