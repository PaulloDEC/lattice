#!/usr/bin/env python3
"""
Differential test of port/src/input.c against the ORIGINAL keyboard ISR.

Loads HOCUS.EXE's load module at segment 0x1000 (applying relocations), runs the
real ISR machine code (1392:026E) in a 16-bit x86 emulator with port 0x60 stubbed
to return the scancode, and compares every state byte it touches with input.c.
"""
import sys, struct, random, ctypes
sys.path.insert(0, '/home/claude/hocus')
from mz import MZ
from unicorn import *
from unicorn.x86_const import *

BASE_SEG = 0x1000
m = MZ('/home/claude/hocus/HOCUS.EXE')
mem = bytearray(m.mem)
for off, seg in m.relocs:                      # apply relocations
    lin = seg * 16 + off
    v = struct.unpack_from('<H', mem, lin)[0]
    struct.pack_into('<H', mem, lin, (v + BASE_SEG) & 0xFFFF)

DS = 0x1d51 + BASE_SEG
CS = 0x1392 + BASE_SEG
ISR = 0x026e
SENT_SEG, SENT_OFF = 0x0F00, 0x0000            # sentinel return address (hlt)
STACK_SEG = 0x0E00

uc = Uc(UC_ARCH_X86, UC_MODE_16)
uc.mem_map(0, 0x200000)
uc.mem_write(BASE_SEG * 16, bytes(mem))
uc.mem_write(SENT_SEG * 16, b'\xf4')

cur = {'sc': 0}
def hook_in(uc, port, size, ud):
    return cur['sc'] if port == 0x60 else 0
uc.hook_add(UC_HOOK_INSN, hook_in, None, 1, 0, UC_X86_INS_IN)
uc.hook_add(UC_HOOK_INSN, lambda *a: None, None, 1, 0, UC_X86_INS_OUT)

def ds(off, n=1):
    return DS * 16 + off
def rb(off): return uc.mem_read(ds(off), 1)[0]
def rw(off): return struct.unpack('<H', bytes(uc.mem_read(ds(off), 2)))[0]
def wb(off, v): uc.mem_write(ds(off), bytes([v & 0xFF]))
def ww(off, v): uc.mem_write(ds(off), struct.pack('<H', v & 0xFFFF))

def run_isr(sc):
    cur['sc'] = sc
    sp = 0x1000
    uc.reg_write(UC_X86_REG_SS, STACK_SEG); uc.reg_write(UC_X86_REG_SP, sp - 6)
    # iret frame: IP, CS, FLAGS
    uc.mem_write(STACK_SEG * 16 + sp - 6, struct.pack('<HHH', SENT_OFF, SENT_SEG, 0x0002))
    uc.reg_write(UC_X86_REG_CS, CS)
    uc.reg_write(UC_X86_REG_DS, DS); uc.reg_write(UC_X86_REG_ES, DS)
    uc.emu_start(ISR, SENT_OFF + 1 if False else 0xFFFFFFFF, count=2000) if False else None
    try:
        uc.emu_start(CS * 16 + ISR, SENT_SEG * 16, count=5000)
    except UcError as e:
        raise RuntimeError(f"emulation error for sc={sc:#x}: {e}")

# ---- C side ------------------------------------------------------------------
lib = ctypes.CDLL('/home/claude/hocus/port/tests/libinput.so')
class IS(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint8) for n in (
        'left right down scroll_up scroll_down jump fire up up2 latch_jump latch_fire latch_up '
        'esc pause hot_v hot_s hot_m hot_c f1 f10 f3 f2 last_scancode any_key suppress_bindings'
    ).split()] + [('sum', ctypes.c_uint16)]
g_in = IS.in_dll(lib, 'g_in')
g_binding = (ctypes.c_uint8 * 8).in_dll(lib, 'g_binding')
cheats = []
CB = ctypes.CFUNCTYPE(None, ctypes.c_uint16)
cb = CB(lambda s: cheats.append(s))
lib.input_set_cheat_callback(cb)

# (field, DS offset, width)
MAP = [('left',0x70ac),('right',0x70ab),('down',0x70ad),('scroll_up',0x70a4),('scroll_down',0x70a3),
       ('jump',0x70a9),('fire',0x70aa),('up',0x70ae),('up2',0x70af),
       ('latch_jump',0x70a6),('latch_fire',0x70a5),('latch_up',0x70a7),
       ('esc',0x70a8),('pause',0x7098),('hot_v',0x709b),('hot_s',0x709c),('hot_m',0x709d),
       ('hot_c',0x709e),('f1',0x709f),('f10',0x70a0),('f3',0x70a1),('f2',0x70a2),
       ('last_scancode',0x7099),('any_key',0x709a)]
# cheat side-effect variables in the original (to compare with callback events)
CHEAT_VARS = {0x5BA:[(0x704c,2)], 0x3D6:[(0xce84,2),(0xce82,2)],
              0x378:[(0x7046,2),(0xce88,2),(0xce86,2)], 0x4D8:[(0xce94,2),(0xce98,2),(0xce96,2)]}
ALL_CHEAT = [(o,2) for v in CHEAT_VARS.values() for (o,_) in v]

def sync_to_c(state):
    """copy initial DS state into the C module"""
    for name, off in MAP:
        setattr(g_in, name, state['b'][off])
    g_in.sum = state['sum']; g_in.suppress_bindings = state['supp']

random.seed(1234)
ACT_OFF = [0x1558,0x1559,0x155a,0x155b,0x155c,0x155d,0x155e,0x155f]
KEYTAB = list(mem[0x1d51*16+0x14fe: 0x1d51*16+0x14fe+18])
interesting = sorted(set(KEYTAB + [0x01,0x19,0x3b,0x3c,0x3d,0x44,0x2e,0x32,0x1f,0x2f,0x21,0x10,0x30]))
checks = mism = 0
cheat_events_seen = set()

for trial in range(300):
    # random bindings, suppress flag, starting state
    binds = [random.randrange(18) for _ in range(8)]
    supp = random.random() < 0.2
    for off, b in zip(ACT_OFF, binds): wb(off, b)
    for w in (0x48ce, 0xce8c, 0xce8a, 0x790c): ww(w, 0)
    if supp: ww(0x790c, 1)
    for _, off in MAP: wb(off, 0)
    for off in (0x70a5, 0x70a6, 0x70a7): wb(off, 1)        # latches armed
    for o, _ in ALL_CHEAT: ww(o, 0)
    ww(0x7046, 7)                                            # nonzero so we can see 0x378 effect
    ww(0xcfde, 0)
    state = {'b': {off: rb(off) for _, off in MAP}, 'sum': 0, 'supp': int(supp)}
    for i in range(8): g_binding[i] = binds[i]
    sync_to_c(state)
    cheats.clear()
    for step in range(60):
        base = random.choice(interesting)
        sc = base | (0x80 if random.random() < 0.45 else 0)
        if random.random() < 0.1: sc = random.randrange(256)
        # occasionally let the "game" consume edge flags / sticky flags (identically on both sides)
        if random.random() < 0.15:
            for name in ('jump','fire','up','up2','pause','f1','f2','f3','f10','hot_c','hot_m','hot_s','hot_v','any_key'):
                off = dict(MAP)[name]; wb(off, 0); setattr(g_in, name, 0)
        before = {o: rw(o) for o, _ in ALL_CHEAT}
        run_isr(sc)
        cheats.clear()
        lib.input_scancode(sc)
        checks += 1
        # compare flags
        for name, off in MAP:
            a, b = rb(off), getattr(g_in, name)
            if a != b:
                mism += 1; print(f"MISMATCH trial {trial} step {step} sc={sc:#04x} {name}: orig={a} port={b}")
        if rw(0xcfde) != g_in.sum:
            mism += 1; print(f"MISMATCH sum: orig={rw(0xcfde):#x} port={g_in.sum:#x} sc={sc:#x}")
        # cheat side effects vs callbacks
        changed = {o for o, _ in ALL_CHEAT if rw(o) != before[o]}
        orig_events = {k for k, vs in CHEAT_VARS.items() if any(o in changed for o, _ in vs)}
        # 0x378 sets 0x7046=5 even if already 5; account by also checking sum value
        orig_events |= {k for k in (0x5BA,0x3D6,0x378,0x4D8) if False}
        port_events = set(cheats)
        # Re-derive original events from the sum the ISR left (3D6/378/4D8 do not reset the sum)
        s = rw(0xcfde)
        if s in (0x3D6, 0x378, 0x4D8): orig_events.add(s)
        if (rw(0x704c) == 0x64 and before.get(0x704c, 0) != 0x64): orig_events.add(0x5BA)
        cheat_events_seen |= port_events
        if port_events != orig_events:
            mism += 1; print(f"MISMATCH cheat events sc={sc:#x}: orig={sorted(map(hex,orig_events))} port={sorted(map(hex,port_events))}")
print(f"{checks} ISR invocations compared, {mism} mismatches; cheat events seen: {sorted(map(hex,cheat_events_seen))}")
sys.exit(1 if mism else 0)
