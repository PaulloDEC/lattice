import struct
from capstone import *
class MZ:
    def __init__(s,path):
        s.raw=open(path,'rb').read()
        h=struct.unpack('<14H',s.raw[:28])
        s.hdr=h[4]*16
        s.mem=bytearray(s.raw[s.hdr:])
        s.ss,s.sp,s.ip,s.cs=h[7],h[8],h[10],h[11]
        n=h[3]; off=h[12]
        s.relocs=[struct.unpack('<HH',s.raw[off+4*i:off+4*i+4]) for i in range(n)]
        s.relseg={}  # linear addr of relocated word -> its (unrelocated) value = segment
        for o,sg in s.relocs:
            lin=sg*16+o
            s.relseg[lin]=struct.unpack('<H',s.mem[lin:lin+2])[0]
    def md(s):
        m=Cs(CS_ARCH_X86,CS_MODE_16); m.detail=True; return m
