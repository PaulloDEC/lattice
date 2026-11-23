# Hocus Pocus (Registered v1.1) - reverse-engineering notes

Status key: **[verified]** = checked against this EXE/DAT by code; **[doc]** = from ModdingWiki, not yet independently checked; **[todo]** = unknown.

## 1. The build
- HOCUS.EXE: 182,656 bytes, MZ, **real-mode, large model, Borland C++ 1991** (3.x), not packed. 1,552 relocations, 7,168-byte header. Uses 80186 `enter`/`leave`. Borland FP emulator interrupts (INT 34h-3Dh) present. [verified]
- String `HOCUS POCUS Version 1.1`; four episodes (Time Tripping / Shattered Worlds / Warped and Weary / Destination Home), 9 levels each = **36 levels**. Shareware nag text is still in the EXE. [verified]
- DGROUP segment = 0x1D51 (file offset 0x1D510 + 0x1C00 header = 0x1F110). Stack segment 0x2AC7. [verified]
- Hardware used: VGA Mode X (planar), PIT timer, keyboard IRQ, joystick port, AdLib/OPL, Sound Blaster / PAS / Sound Source via the Apogee Sound System (Multivoc). [verified from port/string census]

## 2. HOCUS.DAT container [verified]
No directory in the DAT. The FAT is `(u32 offset, u32 size)` x 652 at **EXE file offset 0x1F1A4** (DS:0x0094). Entries are contiguous and end exactly at the DAT's last byte. Loader module = code segment 068c (`fopen("HOCUS.DAT","rb")`, `fseek` by table entry).

| entries | content | status |
|---|---|---|
| 0 | 720 bytes, 90 x 8-byte glyphs (font) | loaded by the start-up code (0548:02A5, far pointer DS:6440); glyph layout [todo] |
| 1, 2, 4, 17, 72, 122, 23-40 | PCX 320x200 (screens, title, ordering, endings) | [verified] decode |
| 3, 9-16, 121, 123-129 | planar IMG (see 3.2) - 17 files | [verified] decode |
| 5 | 2880 bytes, loaded to DS:57E8 by 0548:02BB | purpose [todo] |
| 6 | **RNG table**: 1000 signed words (values 1..997) -> DS:711A | [verified] |
| 18-22 | unidentified (9380-12932 bytes) | [todo] |
| 7, 8 | palette lo/hi 128 colours each, 6-bit VGA | [verified] |
| 41-71 | 31 text pages, 1686 bytes | [verified] decode |
| 73-88 | 16 backdrop upper-halves (128 colours, 6-bit) | [verified] |
| 89-104 | 16 backdrop PCX | [verified] render |
| 105-120 | 16 tileset PCX (20x12 tiles of 16x16) | [verified] render |
| 130 | sprite bank, 40 sprites, 490 frames E+W | [verified] all decode |
| 131-598 | maps: **type-major**, entry = 131 + 36*type + level (type 0..12 = .000-.012) | [verified] all sizes match |
| 599-610 | 12 MIDI tracks | [verified] |
| 611-651 | VOC (Sound Blaster) + PC-speaker pairs | [verified] |

## 3. Formats
### 3.1 Palette [verified]
Game palette = entries 7+8 (768 bytes, 6-bit). A level replaces colours 128-255 with its backdrop's 128-colour palette (73+n). Index 0 = transparent in sprites.

### 3.2 Planar IMG [verified]
`u16 w4, u16 h`, then 4 blocks of `w4*h` bytes; block p holds pixels x%4==p. Size check `w4*h*4+4 == len`.

### 3.3 Sprites [verified]
220-byte info blocks (count = first iOffset / 220), 40 sprites. Frame = layout stream (ops 0=reset+type byte, 1=skip u16, 2=copy 4px, 3=end) over 4-pixel pixel groups, 320-px-wide buffer, pointers in 4-px cells. Frame count = number of op-3 terminators in `[lE[0], lW[0])`. Integrity rule `iOffset+iPixelsOff+iPixelsSize == next iOffset` holds for 40/40. 490/490 frames decode.

### 3.4 Maps [verified layout, [doc] semantics]
240x60 tiles of 16x16. Files per level:
`.000` start (null,x,y,shootDelay) | `.001` tile anim (4-byte hdr + 240x3) | `.002` 10 hint messages | `.003` 10 teleports | `.004` 23 switches | `.005/.006` 25 toggles each | `.007` 10 enemy defs | `.008` 250 spawn entries | `.009` bg | `.010` fg | `.011` insert-wall layer | `.012` event layer (u16, 30000 = empty). Event IDs and enemy behaviour codes are in the ModdingWiki Map Format page (not repeated here).
Level 1 verified: start (3,57), par 150 s, music 3, tileset 0, backdrop 0, shoot delay 50.

### 3.5 Text pages [verified]
1686 bytes: `u16 lines, u16, u16`, 20 x (u16 align, u16 colour), 20 x 80-byte strings.

## 4. EXE tables (v1.1 registered offsets) [verified unless noted]
| offset | table |
|---|---|
| 0x1F1A4 | FAT |
| 0x20680 | sound slots: 16 x (voc entry, pc-speaker entry, priority) |
| 0x20A22 | story pics (picEntry, x, y) x10; 0xFFFF = none |
| 0x20A9A | par/time limits, u16 x36 (9 per episode) |
| 0x21714 | item infos, 42 bytes: name[36], s16 score, u8 heal, u8 firepower, u8 type, pad |
| 0x21ADA / 0x21B2A / 0x21B7A | tileset / level / backdrop number per level, **10 per episode** |
| 0x21BDE | music per level, **9 per episode** |
| 0x21C26 | elevator tiles, s16 x80 (-1 = none) [doc] |
| 0x21CC6-0x21CE6 | boss 4 data [doc] |
| 0x21CEE / 0x21D20 | super-jump (25) / normal-jump (19) y increments [doc] |
| 0x21D46 | hurt health increments [doc] |

## 5. Code module map (segment = paragraph in load module)
| seg | size | role (from ports/strings/refs) |
|---|---|---|
| 0000 | 21.6 KB | Borland C0 startup + RTL |
| 05d8 | 2.9 KB | **video primitives** (pages, copy, pixel, palette) 0x0D-0x418; palette effects 0x419-0x52E; PCX loader 0x52F-0x733; 0x734+ blitters (not yet read). 1437 = far `(dest,src,n)` rep-movsb; 1439 = mode set |
| 068c | 0.7 KB | DAT loader |
| 06b8 | 20 KB, 67 fn | rendering + game core (planar map-mask writes, 264 RTL calls) |
| 0ba5 | 27 KB, 39 fn | big blitters (long, likely unrolled) |
| 124a | 4.6 KB | [todo] |
| 1392 | 2.6 KB | **FAT lookups 07CA (offset+size) and 0800 (offset only); JOYSTICK poll at 081D (reads gameport 0x201, called once per frame by the main loop when DS:48CE set)**; **timer tick ISR + PC-speaker note stepping + delay() (0x0A-0x108), keyboard INT 9 hook (0x195-0x5E8)**; later functions (0x617+) not yet read, joystick port 0x201 use unconfirmed |
| 1505 | 1.7 KB | **Apogee task manager (task_man.c)**: TS_SetClockSpeed (PIT ch0, mode 0x36), TS_SetTimer (period = 1192030/Hz), TS_ScheduleTask, TS_Terminate, dispatcher |
| 161b / 1731 / 1830 | 3.9 / 4.1 / 3.6 KB | Apogee Sound System: AdLib 0x388, PIC IRQ handlers |
| 1a7c | 10 KB, 12 fn | Borland math / x87 RTL (not game code; 1cf4 likewise). The real `main` is 0548:035A (init 0548:0014), the menu engine is 06B8 |
| others | small | [todo] |

## 6. Decompile / port plan
1. **Done**: data layer (this package). The port can load HOCUS.DAT/EXE tables directly.
2. Decompile in this order: 1392 (input) -> 1505 (timer) -> 05d8 (video; replace with a software indexed framebuffer + SDL2) -> 06b8/0ba5 (gameplay + draw; replace blitters with portable C) -> 1a7c (loop/menus) -> sound (replace ASS with a modern mixer; MIDI via an OPL/GM synth, PC-speaker and VOC data are in the DAT).
3. Tools: a Ghidra 12.2 headless run on this EXE needs JDK 25 and its auto-analysis stalled on this binary in the sandbox; full disassembly of all 28 segments (47k instructions) is done with Capstone and is usable as the ground truth.

## 7. Port progress
| module | status |
|---|---|
| data layer (`hocus_dat.py`, `hocus_extract.py`) | done, verified by rendering |
| input - keyboard ISR (`port/src/input.c`) | **done, differentially verified**: original ISR machine code run in Unicorn vs the C port, 18,000 invocations, 0 mismatches |
| input - joystick / remainder of 1392 | not started |
| video primitives (`port/src/vga.c`, `video.c`) | **done, differentially verified**: original 05D8 code run in Unicorn against the C VGA model vs `video.c`, 840 ops in 12 scenarios, 0 mismatches; harness mutation-tested (4 injected bugs all caught) |
| video: palette effects + PCX loader (05D8:0419-0734) | **done, differentially verified**: original LoadPCX run against all 56 real PCX entries of HOCUS.DAT (file I/O stubbed) + 20 synthetic RLE streams + 6 palette-effect sequences, 82 scenarios, 0 mismatches; mutation-tested (5 injected bugs caught) |
| video: IMG blit + palette resource (05D8:0A70, 0AFF) | **done, differentially verified**: all 17 real IMG entries x3 placements + 18 real palette entries x2, 87 scenarios, 0 mismatches; mutation-tested (5 injected bugs caught) |
| effect pools A/B/C/D (`port/src/effects.c`: spark bursts, score tags, puffs, sparkles; 8 routines) | **done, differentially verified**: original machine code vs C, 1,600 random operations incl. VRAM bytes, RNG index, pool memory and the recorded sprite-draw calls, 0 mismatches; mutation-tested (10 injected bugs caught). The sprite blitter 0BA5:249E is a callback, not yet ported |
| RNG (`port/src/rng.c`, 1392:0617) | **done, differentially verified**: 35,000 calls vs original machine code, 0 mismatches; mutation-tested (3 injected bugs caught) |
| camera / scroll update (0BA5:29C5-2C80) | **read and understood, not yet ported** (see section 8) |
| video: remaining (06B8, 0BA5 tile/sprite blitters; 05D8:088A PCX-from-file, 1 caller) | not started |
| timer / game clock / frame pacing (`port/src/timer.c`) | **done**; pacing logic read from the disassembly and unit-tested with a fake clock (not emulator-verified like input) |
| PC-speaker SFX stepper (1392:000A body), `delay()` (1392:00AC, uses FP emulation) | read, not yet decoded/ported |

### Keyboard ISR facts (1392:026E)
- 8 bindable actions (DS:1558-155F) index an 18-entry scancode table at DS:14FE. Defaults: Left/Right arrows, Ctrl=Jump, Alt=Fire, Up/Down arrows, PgUp/PgDn=scroll.
- Jump, Fire and Up are edge-triggered with an arming latch; key-repeat is ignored until release. The game clears the flags itself.
- Guarded block (skipped while any of DS:48CE/CE8C/CE8A/790C is non-zero): `any_key` + Up/Down/Left/Right/Jump/Fire. Scroll up/down and all fixed keys are NOT guarded.
- Fixed keys: ESC (level), sticky flags for P, F1, F2, F3, F10, C, M, S, V. Meanings of C/M/S/V/F-keys are inferred, not yet confirmed from the game code.
- Scancode-sum detector (DS:CFDE): reset by F/Q/B make; every byte incl. break codes is added; magic sums 0x5BA / 0x3D6 / 0x378 / 0x4D8 trigger effects on DS:704C, CE84/CE82, 7046/CE88/CE86, CE94/CE98/CE96. What those variables mean is not yet identified.

### Timing facts (verified from the disassembly)
- Clock: `TS_ScheduleTask(fn=1392:000A, rate=0x8C, priority=1, data=DS:7908)`. Rate is in Hz (`period = 1192030/Hz` PIT counts), so the **game clock is 140 Hz**, a 32-bit counter at DS:7908/790A. Task function also steps the PC-speaker SFX player (state at DS:636C-6372, lengths at DS:632C).
- Main loop (0BA5:4D2C-4DB8): `clock=0; last=0;` per frame: wait until `(int32)(clock-last) >= table[speed]`, `last=clock`, run frame step (0BA5:29C5). `table` = DS:14F8 = **{8, 7, 6}** ticks, `speed` = DS:790E (0/1/2, the "Select Game Speed" menu: Slow/Medium/Fast, assignment of names to 0/1/2 inferred).
- => **Logic runs at 17.50 / 20.00 / 23.33 fps**; no catch-up when a frame overruns. Per-frame physics (jump tables etc.) are tied to this rate, so higher framerates should come from render interpolation, not from stepping logic faster.
- Clock-related variables seen: DS:CFD4/CFD6 zeroed with the clock at loop start (purpose unknown); DS:7046 is not game speed (the 0x378 cheat sets it to 5 for 600 ticks; meaning unknown).

### Video facts (verified)
- **Mode Y**: BIOS mode 13h then unchained (SEQ 04 chain-4 off; GC 05/06 odd-even off; CRTC 14h dword off, 17h byte mode on; CRTC 09h = 01h; map mask F; 64 KB cleared) in 1439:0000. 320x200, 256 colours, 4 planes, **80 bytes/row**, pixel x in plane x&3, byte x>>2.
- **4 pages**, stride **0x3F00** bytes (page p starts at p*0x3F00; 0x3E80 used). Tables: DS:6448 CRTC start offsets, DS:6450 far draw pointers (A000:p*3F00), DS:6460 current draw pointer, DS:6446 draw page, DS:6444 visible page.
- Registers the game ever touches: SEQ 01 (bit 5 screen off), SEQ 02 map mask, GC 04 read plane, GC 08 bit mask (0 = latch copy, FF = normal), CRTC 0C/0D start address, 3DA retrace, DAC 3C7/3C8/3C9. Write modes / set-reset / rotate are never used.
- Page-to-page copies use latch mode + `rep movsb` (1437:0005, args dest, src, n). Rect copy/fill x-coordinates are in **bytes (4-pixel units)**.
- **Quirk (reproduced on purpose):** `05D8:022A` copy-rect sends every source row to the SAME destination row `dy` (dest row does not advance). It matters only if the game ever calls it with y0 != y1. The port keeps this behaviour; worth checking the callers before ever "fixing" it.
- Page flip (05D8:0050): wait end of retrace, write start address, wait start of retrace, then record the page - a platform vsync/present callback hooks in at both waits.

### Palette effects and PCX loader (verified)
- Two 768-byte 6-bit palette buffers: **target DS:6A64**, **working DS:6764**. Effects wait for vsync, then upload all 256 colours each step:
  `0419 fade_out(steps)` (steps..0, work = target*di/steps), `046E fade_in(steps)` (0..steps), `04B0 fade_white()` (70 steps, +1 per component up to 63, **modifies the target palette**), `04F3 set_black()`, `051A apply()`.
  Fade math is 16-bit signed (`imul`, `cwd`, `idiv`): only the low 16 bits of `value*di` are used.
- `0673 LoadPCX(entry, applyPalette)`: header (128 B) -> DS:7970, trailer palette (last 768 B) -> target palette `>>2`, optional immediate upload; width/height/bytes-per-line -> DS:796E/796C/796A; then `052F` decodes into the **current draw page from row 0**.
- Decoder: token `0xC0|n` + value emits exactly n pixels (n=0 emits none); other bytes are single pixels; a scanline ends once `x >= bytes_per_line`; pixel x goes to plane buffer `x&3`, byte `x>>2`; each row is written with map masks 1,2,4,8 after forcing bit mask 0xFF. Leaves map mask = 8.
- Whole-file ids: the loader asks FAT lookup `1392:07CA(index,&off,&size)`; reads go through `068C:0089(long off, long len, far dest)` and `068C:02B5` (getc).

### Caller census of the 05D8 video API (from the disassembly)
Heavily used: SetDrawPage 51, LoadIMG 46, FadeOut 41, FadeIn 34, ShowPageNow 34, SetBlack 33, LoadPalette 30, ClearDrawPage 26, ClearPixels 17, LoadPCX 10, CopyPage 7, WaitVsync 7, GetPixel 3, PutPixel 3, GetPalette 3, ShowPage 2, FadeWhite 1, ApplyPalette 1, VideoInit 1, PCX-from-file 1.
**Dead code (0 callers): CopyRect 05D8:022A, FillRect 02BE, ScreenDump 095E, raw ScreenBin loader 09DF.** (CopyRect's "same destination row" quirk is therefore irrelevant; the port keeps it anyway.) No DAT entry has the 64,768-byte size ScreenBin would need.

### IMG and palette resources (verified)
- `0A70 LoadIMG(entry, xByte, y)`: header `u16 w4, u16 h` -> DS:6328/632A; bit mask reset to FF; for plane 0..3 (map mask 1<<plane) copies h rows of w4 bytes to `drawptr + (y+row)*80 + x + col` (16-bit wrap, no clipping/transparency). Leaves map mask 8. Straight opaque copy: this is how UI/story images are drawn.
- `0AFF LoadPalette(entry, slot, apply)`: reads the whole entry (6-bit, no shift) into `target + slot*3` (slot 0 or 128 for 128-colour entries 7/8/73-88), optional upload of all 256.
- File-based helpers `088A` (PCX from a path via C stdio `FILE*` at DS:79F0, decoder at 0734 is the same RLE decoder as 052F) are used once; not ported yet.

## 8. Camera / scroll update (0BA5:29C5-2C80, 252 insns, no calls)
Read from the disassembly; variable meanings are **inferred from how the numbers fit the map geometry** (not yet confirmed by watching the game run). Units fit consistently: X is in 8-pixel units (map = 240 tiles x 16 px = 480 units), Y in pixels / tiles (map = 60 tiles).

| DS offset | meaning (inferred) |
|---|---|
| CF32 | player X, 8-px units (0..480) |
| CF28 | player Y, pixels; CF2E = CF28/16 = player row (tiles) |
| CE80 | camera X, 8-px units; target = CF32-20 (screen is 40 units wide, player centred), clamped 0..0x1B8 (440 = 480-40) |
| CE7A | camera Y, tile rows, clamped 0..50 (0x32); target = playerRow - CF36 |
| CF36 | vertical margin in tiles (set elsewhere) |
| CF38 | non-zero: camera X snaps straight to target; zero: eases 1 unit (8 px) per frame toward it |
| CF0C | non-zero: vertical camera frozen |
| CE7C | set to 1 when CF32 is odd (probably "a new 16-px tile column is needed", unconfirmed) |
| CF0A | set to 1 on frames where camera Y moved (vertical redraw flag, unconfirmed) |
| outputs | CF2C = CF32*2 (player X in 4-px byte columns); CF30 = CF32/2 (player tile column); CE7E = CE80/2 (camera tile column); CF2A = (CF32-CE80)*2 (player screen X, byte columns); CF26 = CF28 - CE7A*16 (player screen Y, pixels) |

It also shifts several object-position arrays so on-screen objects stay put relative to the world when the camera moves: the X camera delta (in pixels, and /4 for byte-column arrays) is added to 4 tables, and the Y delta (tiles*16) to the matching 4. Table bases: DS:CA38 (8 active flags), CBC0 (8 objects x 16 entries, stride 64 bytes / 4), CE04/CE02 (stride 10), CDC2/CDC4 (stride 8), CFA8/CF80 (20 words). Their meaning (enemies, shots, effects...) is not identified yet.
**Porting note:** the scalar part is simple; the table shifts need the real object structures first, so the clean order is to identify those tables (their writers and readers in 0BA5/06B8) before porting this routine.

### Tooling notes
- The sandbox was reset between sessions: the working directory was rebuilt from the delivered bundle, the game re-extracted and the disassembly regenerated. Windows (MinGW-w64) compile checks are not available until `apt-get update` is run before installing it.
- A quick function-start regex that only matched `enter 0x...` operands missed small-frame functions such as `enter 6, 0`; use `funcmap.py` style matching (`enter N, 0` for any N).

## 9. Random numbers and the effect pools (verified unless noted)
- **RNG**: `1392:0617(n)`: `index++; if (index > 999) index = 0; return table[index] % n` (signed), table = DAT entry 6 loaded to DS:711A, index DS:7118. 112 callers + inlined copies at 0BA5:2D02 and 2E47. Index reset to 0 at game start (0548:02F1) **and at the start of every level loop (0BA5:4D0D)**: the sequence is identical each time a level starts (matters for any future replay/demo or "same seed" behaviour; the port keeps it).
- **Effect pools** (all are world-anchored, which is why the camera routine shifts them; X is in 4-px byte columns unless stated):
  | pool | base | slots | layout | spawn | update/draw | reading |
  |---|---|---|---|---|---|---|
  | B | DS:CE04 | 10 x 5 words | x, y, frame(0-4), life (0=free, set 17), variant 0/1 | 0BA5:3684(x,y,type) first free slot; type>4 -> variant 1, frame=type-5 | 0BA5:36F0 each frame: draw via 249E, y -= 2, life-- | rising 17-frame popup, very likely the **score tags** (sprite 1, 5 frames) |
  | C | DS:CDC2 | 8 x 4 words, ring index DS:CE02 | x, y, counter=9, random 0/1 (from RNG n=2) | 0BA5:2174(x,y) | **0BA5:1FAA** (verified): counter 9..1 plays frames 0,1,2,3,4,3,2,1,0 via 249E(3, DS:C44E, x, y, variant, frame), counter-- | 9-frame animated puff (kind of effect [todo]) |
  | D | DS:CFA8 (x) / CF80 (y) / CF58 (age) | 20 words each, ring index DS:2ACC | x (-99 = free), y, 0 | 0BA5:21C6(x,y): fills the slot only if free, but the ring ALWAYS advances | **0BA5:20B1** (verified): every slot ages each frame (y+1, age+1, freed at 16); on-screen slots (x 0..79 byte columns, y 0..159) draw ONE pixel: plane = RNG(4), then 1-in-2 chance (RNG(2)==0) of writing colour 0x78 + age/4 | falling fading sparkles (palette entries 0x78-0x7B) |
  | A | DS:CBC0 pos, CA40 vel, CA38 flags, ring CDC0 | 8 bursts x 16 particles. pos = (x,y) words in PIXELS (stride 64 B per burst); vel = (vx, vy, colour) signed bytes (stride 3, 48 B per burst); flags = frames left | 0BA5:2F1C(x,y) (pixels): flags=16; per particle vx=RNG(10)-RNG(10), vy=-(RNG(7)+5), colour 0x70/0x60 by RNG(2) (4 RNG calls each, in that order) | **0BA5:2FF3** (verified): each live burst, each particle: y+=vy, vy++ (gravity), x+=vx, plot one pixel (plane x&3, colour byte) if 0<=x<=319 and 0<=y<=159; flags-- | **spark/debris bursts with gravity** (what triggers them [todo]) |
  | E | DS:CEDA | 10 words | Y only | 0267, 453A | | [todo] |
- Still to do before the camera routine can be ported cleanly: pool E (DS:CEDA, 10 words, Y only; touched by 0BA5:0267 and 453A) and the callers of the spawners in 453A (3,143 insns, probably the per-frame game logic). The other four pools are done.

### Verified details from the effect pools
- Draw callback argument order (249E): `(mode, sprite, x, y, variant, frame)`; score tags use mode 1 with the sprite word at DS:C296, puffs mode 3 with DS:C44E.
- Sparkle RNG order is part of the behaviour: plane (n=4) first, then flicker (n=2); swapping them changes every later random number, so the port keeps the original order.
- Every pool slot ages even when off screen (sparkles) and the camera routine shifts the same slots, so the pool structs in `effects.h` use the original 16-bit field layout.

### Pool A quirk (kept out on purpose)
`0BA5:3071-308D` contains `if (!flags % 4) colour++` (compiled as `(flags==0) % 4`): an operator-precedence slip in the original source. It sits inside a branch that requires `flags > 0`, so it never fires. The port omits it; the differential test confirms identical behaviour.

## 10. Camera routine and pool E (shots)
- `0BA5:0267` = player-shot vs enemy collision (inferred): 8 enemies x 10 shots; builds each enemy's screen box from CE80/CE7A, hits spawn spark bursts (`2F1C`), free the shot (x=-99, `dec [CEAE]`), handle enemy HP/hit flash/death (3 bursts, event 0x7530 via far ptr DS:705A, `inc [CE6C]`). Pool E: shot X DS:CEEE, Y DS:CEDA (hit test adds 2), counter CE9A, count CEAE.
- `0BA5:29C5` camera update ported as `port/src/camera.[ch]`, verified by `tests/diff_camera.py` (300 scenarios x 12 calls, 0 mismatches; mutation checks catch 4 of 5 injected bugs, the 5th was an equivalent mutant since shifts are multiples of 8).
- Behaviours: horizontal ease is 1 unit (8 px)/frame; clamps and snap mode do NOT shift world-anchored objects horizontally; vertical shift applies after every non-frozen, non-snap branch (clamps included). Only pool E's Y array is shifted (X is not).

## 11. Sprite blitter 0BA5:249E (ported: `port/src/sprite.[ch]`, test `tests/diff_sprite.py`)
- Args (call order): sheet, wrap, x (byte cols), y, anim (0/1), frame. Opcode stream (far ptr DS:7070[sheet]): op0 m = set map mask + restart at (x,y); op1 w16 = skip; op2 = copy one byte VRAM->VRAM in latch mode (clipped to 80x160); other = end.
- Tables: frame table DS:C1DC[sheet*110+anim*20+frame] = stream start; same table +40 words = source offset (anim must be 0/1, anim 2/3 slots hold the source offsets); per-sheet base DS:C18C; source = DS:6458 + C188 + base + offset; dest = draw page + y*80+x.
- `wrap` (a1) is the column limit that triggers row advance (normally 80). Verified 60 scenarios, 0 mismatches; 6 injected bugs all caught.
- Variants of this blitter exist at 0BA5:25FF-29C4 (several similar routines, not yet read); the sprite loader that fills the tables is at 0BA5:22F6-2479.

## 12. Sprite sheet loader 0BA5:2242 (ported: `sprite_load_sheets` in `port/src/sprite.c`, test `tests/diff_sprite_load.py`)
- DAT entry 130 (0x82) = 40 sheet records of 220 bytes, then the data. Record: +0 u32 data offset (relative to entry start), +4..0x37 ASCII name, +0x38 u16 stream size, +0x3A u16 pixel size, +0x3C 80 words (40 frame-start offsets, 40 pixel-source offsets). Data = opcode stream, then pixel bytes; offsets chain (off[i+1] = off[i]+stream+pix).
- Per-level setup (caller 0BA5:4B35) loads 10 slots (DS:C174 = DAT sheet ids, -1 = unused): ids 0,1,3,2 fixed + up to 6 enemy sheet ids from the table at DS:A552 (stride 0x18). The blitter's `sheet` argument is the SLOT (0..9), not the DAT record.
- Pixels are put linearly (320 per row, put_pixel) into draw page 2 + DS:C188; slot base = cursor/4 (DS:C18A running cursor); blitter source = DS:6458 (page-2 pointer) + C188 + base + pixel offset.
- Before this, the level setup (0BA5:49E2-4AD2) copies the tile graphics into the same page-2 area and sets C188/C18A (not read in detail yet).
- Test: 12 scenarios identical to the original; found that the emulator's 3M-instruction cap silently truncated long loops (raised to 60M in `diff_video.py`). Mutations caught: base shift, x/y swap, unrestored draw offset; "skip latch-off" is equivalent because put_pixel resets it.

## 13. Level-setup tile packing (0BA5:49D8-4AD8) and where the source pointer comes from
- DS:6458 = DS:6450[2] = far pointer to VRAM page 2 (A000:7E00), set by the video code; DS:6460 = current draw pointer. The sprite blitter therefore reads page 2.
- Level load order inside 453A: DAT entries (base + 0x83.. etc, via 68C:F9) -> tile PCX to page 3 (+palette) -> tiles (16x16, 20 per row, until a 0xFF top-left pixel) packed to the start of page 2 (64 bytes each) -> markers 0x60 / 0x70 (DS:CE72 / CE70) -> C188 = first free VRAM offset -> sprite sheets (2242) packed after it. Ported as `sprite_pack_tiles()`; test `tests/diff_tiles.py` (fragment run in place, 12 scenarios, 5 mutations all caught).

## 14. Progress estimate (instruction counts from the Capstone dumps)
| Area | Insns | Status |
|---|---|---|
| Segment 0000 (Borland C runtime) | 9,442 | not ported; replaced by libc/SDL |
| 05D8 video, 1392 input/timer/RNG, 1505 | ~2,600 | ported + verified (some dead code) |
| 0BA5 game: effects, camera, blitter, sheet loader, tile pack | ~1,100 of 9,668 | ported + verified |
| 0BA5 rest (453A main logic 3,143; 126C; 0267 collision; 4 blitter variants; ...) | ~8,500 | not ported |
| 06B8 UI / menus / screens (not the renderer) | 7,757 | not read |
| 1A7C Borland x87 RTL | 4,452 | not game code |
| 124A map renderer (0F15/0FE5 ported; 4 scroll variants not yet) | 1,766 | ~400 ported |
| Sound (161B/1731/1830) | 4,800 | not read |
| Small segments | ~3,500 | partly |
Data layer (DAT container, all asset formats): done. Estimate: ~10% of game-specific code ported (~3,700 of ~38,000 insns), but the foundation (video model, test harness, level data) is in place.

## 15. Segment map corrections and the map renderer (124A) - ported: `port/src/map.[ch]`, test `tests/diff_map.py`
- **06B8 is NOT the renderer**: it is the UI/screens module (menus, story/help pages, shop/intermission, high scores, text drawing `05E9`, fades; 67 functions, many IMG loads + palette fades). **0000 = Borland RTL.** **124A = the map/tile renderer** (6 functions: `0F15` even full redraw, `0FE5` odd (half-tile) full redraw, `0006/02AA/0859/0A92` scroll-update variants using page copies).
- Map layers (loaded in the level setup 4716-48F9): DAT entry base+0x83+0x24*k -> k=0 DS:703C start, 1 6D68, 2 AAC4, 3 AA94, 4 A89A, 5 A76E, 6 A642, 7 A552 (enemy defs), 8 8612 (spawn), 9 -> DS:7066 (bg layer), 10 -> DS:7062 (fg layer), 11 -> 705E, 12 -> 705A (info words); DS:7056 = zeroed dynamic layer (14400 bytes). Map is 240 wide x 60.
- Renderer: 20x10 cells; per cell tile = layer 7062 (fg) if != 0xFF else layer 7066 (bg); both 0xFF = cell untouched (backdrop shows). Tile = 64 bytes at page2 + tile*64 (4 planes via latch copy, 16 rows x 4 bytes). Odd camera: right half of first cell, 19 whole cells, left half of the 21st. Caller sets latch mode + mask 0xF. Args of 0F15/0FE5: dest far, src far, layer1 far, layer2 far, map offset (= cam_row*240 + cam_col).
- `0BA5:2C81` = per-frame pass over the visible window handling animated/transient cells (layers 7056/705E/7062, spawns puffs) - not read in detail. `0BA5:39F3` = player-vs-tile interaction (pickups; tile table DS:2628 stride 0x2A). `0BA5:3125/3297` = 8x8 HUD digit/icon blits from DS:645C.
- First end-to-end render: `tests/render_level.py <level> <png>` (C port only: video model, PCX loads, tile packing, camera, map draw, palette from the data layer). Level 0 shows backdrop + tiles; level 9 is a treasure room.

## 16. Per-frame draw order, backdrop routines (124A:0006 / 02AA ported), level-setup facts
- Level frame (453A, 4D67-4E87): wait for timer tick (DS:790A:7908 vs interval table DS:14F8[790E]) -> optional joystick poll -> camera `29C5` -> if CF06==0 player/tile interaction `39F3` -> if CE78: `220F` (special) else: **if CE7C (odd camera): map `0FE5` then backdrop `02AA` else map `0F15` then backdrop `0006`** (backdrop source = DS:645C + 0x7D0) -> `348D` (HUD numbers, uses `3125/3297`) -> ... (rest not read).
- DS:6450 is a table of the four page far pointers (6450,6454,6458=page 2,645C=page 3); 6460 = draw pointer.
- Level setup order (453A 4716-4D00): level data entries -> tile PCX -> page 3 -> pack tiles into page 2 -> sprite sheets (page 2 after tiles) -> backdrop PCX loaded to page 3 at +0x7D0 (25 rows down) -> palette entry 7 at slot 0 and backdrop palette (entry 73+n) at slot 128 -> HUD IMGs 11, 14, 15 at y=185 (x = 0, 0x1C, 0x3C) -> IMG 12 at y=160 on pages 0 and 1 -> first camera call with CF38 (snap) = 1, vmargin CF36 = 5.
- Backdrop: parallax, origin = (cam_x % 80) bytes, i.e. half the foreground speed (camera unit = 8 px fg = 1 byte = 4 px backdrop); only cells empty in both layers are filled. The functions program latch mode 1 + mask 0xF themselves (the map functions do not). Test `tests/diff_backdrop.py`: 16 scenarios 0 mismatches; mutations caught except one equivalent (x==0x4C fast vs slow path give identical output). `0859` and `0A92` have no callers (dead code).
- `tests/render_level.py <level> <png>` now produces a full 320x200 frame (map + parallax backdrop + status panel) through the C port only: `render_level0.png`, `render_level4.png` in outputs.

## 17. Per-frame tile pass 0BA5:2C81 (ported: `map_animate()` in `port/src/map.c`, test `tests/diff_animate.py`)
- Map file k=1 (DS:6D68, 724 bytes) = 4-byte header + 240 tile records {first, last, type} (DS:6D6C). Type 1 = animated loop first..last every frame (e.g. tiles 40-45, 46-53); type 2 = rests at `first`, starts with chance 1/20 per frame, then loops; type 0 static (202 of 240 tiles in level 0).
- Transient layer DS:7056 (0 / 1 = remove fg tile / 2 = swap in the tile from layer DS:705E): applied immediately when DS:CE76 (decremented each call) is 0, otherwise 1 cell in 3 per frame via the RNG, with a dust puff (pool C) at the cell (puffs only for columns < 20).
- RNG consumption order matters for determinism: one draw per non-zero transient cell when the timer is not 0, one draw per resting type-2 tile. All covered by the differential test (7 mutations caught).
- `0BA5:2E9D` (55 insns): walks DS:C134[32] (map cell indices, -1 = free) -> for each active entry calls `04D3` (which is the enemy SPAWN, see section 18) and frees the slot if it returns non-zero; `04D3` (401 insns, 14 RNG calls) not read yet.
- Regression run after this step: effects, camera, sprite, tile pack, map, backdrop, animate all 0 mismatches.

## 18. Enemy spawn 0BA5:04D3 (ported: `port/src/enemy.[ch]`, test `tests/diff_enemy_spawn.py`)
- `04D3(def, x, y, cell)` is the enemy **spawner**, not an update: finds the first of 8 slots whose type (DS:BFD4) is -1, fills 29 per-slot int16 arrays (DS:BF64..C124, stride 16 bytes = 8 slots) from the enemy definition (DS:A552, 12 words each) and returns 1, or 0 when all slots are used. Field names in `enemy.h` are inferred (marked `?` where unsure).
- Definition words: w0 sheet id, w1 hit points (+2 x difficulty DS:706A when >= 0), w4..w8, w10 copied, w11 = behaviour type 0..8. Slot sprite sheet = def + 4; hit-box size comes from sheet header words 13/14, so `sprite_load_sheets` now keeps all 30 header words (`g_spr.hdr`).
- Facing = enemy is right of the player column (CF2C); type 2 forces 0. If the enemy is within 0x32 columns and 0x64 pixels of the player and the 32-bit clock is greater (unsigned) than the deadline (DS:CFD4), sound 0xE plays and the deadline becomes clock+5.
- Per type: 0/1/7 walk toward the player (vx = +-1); 2/3/6 random velocity (vx = r2-r2, vy = (r2-r2)<<2) plus timer r(0x14); **type 6 retries only while vx == 0 (original quirk), types 2/3 while both are 0**; 4 stationary + sound 0xB; 5 stationary; 8 vx = x, c0c4 = r(10)+10.
- Verified: 1000 random scenarios, 0 mismatches; 22 mutants all killed (boundary cases at exactly 0x32/0x64 distance and a -2 slot value were added after the first run let three survive).

## 19. Pending-spawn pass 0BA5:2E9D (ported: `enemy_spawn_pending()`, test `tests/diff_enemy_pending.py`)
- DS:C134[32] = map cells waiting to spawn (-1 = free). For each: info word = layer DS:705A[cell] (map file 12); 0x7530 = nothing there (entry stays pending); else `enemy_spawn(info-106, col*4, row*16, cell)` with col = cell % 240, row = cell / 240 (signed idiv). A successful spawn clears the entry; a failed one (all 8 slots busy) stays pending and is retried next frame.
- Verified: 150 scenarios, 0 mismatches; 7 of 8 mutants killed (the survivor is equivalent for valid data).
- Still open: who fills DS:C134 (probably the map scroll/visibility code in 453A/126C), and the collision routine 0267.

## 20. Shots vs enemies 0267, kill-all 01B6, off-screen despawn 0439 (ported in `port/src/enemy.c`, test `tests/diff_enemy_shots.py`)
- Shot pool E: x DS:CEEE[10] (-99 = free), y DS:CEDA[10] (`g_fx.e_y`, hit test adds 2), power CE9A[10] (non-zero = piercing), live count CEAE. New `g_shots`; kills counter DS:CE6C = `g_kills`.
- Enemy box in screen units: x0 = x - 2*cam_x, y0 = y - 16*cam_row, x1 = x0+w-1, y1 = y0+h-1 (w,h = sheet header words 13/14). Centre for sparks = ((x0+w/2)<<2, y0+h/2). Only enemies with bf84 == 0 can be hit.
- Hit: spark burst (2F1C); non-piercing shot is freed. Then by enemy hp: -2 = boss trigger (hp := -1 and `01B6` kills every enemy); -1 = invulnerable; >0 and normal shot = hp-1, flash = 10; otherwise death: sound 0xA, 3 more bursts (4 in total), slot freed, its map info cell set to 0x7530 (never respawns), kills++. The shot loop keeps running with the same box after a death (a second shot can "kill" it again, as in the original).
- `01B6`: for every live enemy: burst at its centre, sound 0xA, free it, info cell = 0x7530, kills++ (no on-screen check).
- `0439`: free (type=-1, cell=-1; map info untouched so it can respawn) every enemy outside x in [2*cam_x-0x32, 2*cam_x+0x78] / y in [16*cam_row-0x78, 16*cam_row+0x1FE], unless hp_max >= 500.
- Verified: 300 random scenarios (all three routines), 0 mismatches; 24 mutants, all killed except one comment edit. Writing it also found my first port used 3 bursts instead of 4 on death.

## 21. Per-frame update order and player damage checks 0006 / 00D3 (ported: `port/src/player.[ch]`, test `tests/diff_player_hits.py`)
- The per-frame game update (the function around 0BA5:5340-54xx, after 453A) calls in this order: `0267` shots vs enemies -> `0006` player vs hazard pool -> `00D3` player vs enemies -> `2E9D` pending spawns -> `0A81` (not read) -> `126C` (player/enemy update, not read) -> `0439` despawn -> `2FF3` bursts -> `36F0` scores -> `1FAA` puffs -> `20B1` sparkles -> `2C81` tile animation (only on alternate frames, flag at bp-0xd). Its other half (5340-53FF) draws the HUD/ blinking indicators; 5405-5468 is the pause screen.
- Player: DS:704C = health (0..100; cheat sets 100), DS:CE74 = invulnerability frames (20 after a hit; counted down in 39F3), DS:CE8E = shield timer (400 = power-up, 0x7530 = permanent), blocks all damage. Damage per hit by difficulty (DS:2C36): -4, -12, -16, 0.
- Player hit box in the same units as enemies (4-px columns / pixels): x in [xcol+2, xcol+3], y in [y+6, y+0x19].
- `0006`: second object pool DS:BED4..BF54 (8 slots, type -1 free; probably enemy projectiles; start delay BF54 > 0 = harmless and invisible): overlap -> health += damage, invuln = 20, sound 8, slot freed (only when not invulnerable and no shield).
- `00D3`: enemy contact (enemy bf84 must be 0): overlap with hp_max > 20 and difficulty != 3 = lethal (health = 0, ignores invulnerability and does not free the enemy); otherwise normal damage/invulnerability/sound 8.
- Verified: 600 random scenarios, 0 mismatches; 22 mutants all killed.

## 22. Enemy projectiles 0BA5:0A81 (ported: `projectiles_update()` in `port/src/player.c`, test `tests/diff_projectiles.py`)
- The pool read by `0006` is the enemy projectile pool, 14 arrays x 8 words at DS:BE84..BF54 (stride 16): homing BE84, vy BE94, speed BEA4, sheet slot BEB4, dir BEC4 (0 = right, 1 = left, also the sprite variant), h BED4, w BEE4, y BEF4, x BF04 (4-px columns), type BF14 (-1 free), last_frame BF24, frame BF34, jitter BF44, delay BF54. Who spawns them is still unknown (probably inside `126C`, enemy shooting).
- Per frame (`0A81`): delay > 0 -> count down only. Otherwise screen pos = (x - 2*cam_x, y - 16*cam_row); "off" = outside x -10..0x50 / y -0x14..0xA0 (1); a non-empty fg tile (!= 0xFF) at (y/16)*240 + x/4 gives 2 and wins (even when off-screen). Off: freed; wall hit (2) also makes a spark burst at (sx*4+8, sy+8). Else: draw (sheet, hdr13[sheet], sx, sy, dir, frame); frame++ while < last_frame; x += or -= speed by dir; homing = 1 column toward the player; y += vy; jitter = y += rng(2) - rng(2).
- DS:C296 and DS:C44E, the effects' "sprite handles" (`g_fx.sprite_b/c`), are the header word 13 of sheet slots 1 and 3 (C1A0 + slot*220 + 26): the same words the blitter receives as its `wrap` argument. Harmless in the port (separate fields), but the loader must keep them equal when the real level setup is ported.
- Out-of-range tile lookups read garbage in the original; the port treats them as empty.
- Verified: 400 random scenarios (draw calls, bursts, RNG, layer, pool), 0 mismatches; 22 mutants, all killed (two homing comparisons needed an exact-column case first).

## 23. 126C structure and the boss health bar 0BA5:0C5B (ported: `enemy_draw_bar()`, test `tests/diff_enemy_bar.py`)
- `126C` (ends at 1F99) is the per-enemy update loop over the 8 slots: free slots skipped; **type 8 -> `0CE8(slot)`**; **type 0x63 -> `1019()`**; hp_max > 20 -> `0C5B(slot)` (boss bar) and then the normal update; bf84 != 0 -> the branch at 1EE6; otherwise the movement code (type 0/1/7 walkers, 2/3/6 flyers, 4/5 stationary) which also spawns puffs (`2174`), calls the blitter variants (`2896`, `249E`) and spawns projectiles through `0922`. Not yet read: 0CE8, 1019, and the movement/shooting body.
- `0922(x, y, aim, delay, def, sheet)` = spawn one enemy projectile (see section 24, ported).
- **The code uses Borland x87 emulation**: `INT 34h..3Bh` + operand bytes = FPU opcodes D8..DF (`INT 3Dh` = FWAIT), disassemblers show garbage after them. The float-to-int helper is `0000:1139` (truncating FISTP). Differential tests patch these to real opcodes (`9B Dn`) and run `FNINIT` first, because Unicorn starts with control word 0.
- `0C5B(slot)`: boss health bar. Plane mask 0xF, latch off; length = trunc(hp / hp_max * 78.0) pixels (x87 extended precision, low 16 bits kept; negative = nothing); three scanlines at draw offset + 0x2F81 / 0x2FD1 / 0x3021 (+ i), colour 0x6F - i/10. The port uses `long double` (80-bit on x86 gcc/MinGW; a `double` version fails the exact-multiple cases, e.g. hp=1, hp_max=3).
- Verified: 300+ random scenarios incl. exact multiples, random starting latch/mask state: 0 mismatches; mutants killed (two survived until the test started from random VGA state; two more are equivalent).

## 24. Projectile spawner 0BA5:0922 (ported: `projectile_spawn()` in `port/src/player.c`, test `tests/diff_projectile_spawn.py`)
- `0922(x, y, aim, delay, def, sheet)`: first free slot (type -1), else returns 0. delay -> BF54. If `aim`: x <= player column -> dir 0 and x offset = sheet header word 13 (sprite width), else dir 1 and no offset; without `aim` dir is 0 and no offset. From the sheet header: w = word 23, h = word 24 + 3, first frame = word 26, last frame = word 27, y offset = word 25. From the enemy definition: speed = word 2, vy = word 3, homing = word 5, jitter = word 9. Type = 0x7530, sound 0xD, aux (DS:BE74) = 0.
- The pool gained its first array, `aux` at DS:BE74 (cleared at spawn, no reader found yet).
- Verified: 400 random scenarios (4 calls each, full pools, sound, return value), 0 mismatches; all 21 mutants killed (the `<= -1` free-slot mutant after adding a type -2 case).

## 25. The other five blitters (ported: `sprite_draw_raw`, `sprite_solid`, `sprite_solid_raw` in `port/src/sprite.c`, test `tests/diff_sprite_variants.py`)
- All six sprite blitters of 0BA5 share the same opcode streams and frame tables; they differ in clipping, argument order and the source pixel:
  | original | C | args | clipped | source |
  |---|---|---|---|---|
  | 249E | `sprite_draw` | sheet, wrap, x, y, anim, frame | yes | own pixels |
  | 23BA | `sprite_draw_raw` | sheet, x, y, anim, frame | no | own pixels |
  | 26B3 | `sprite_solid(0,...)` | sheet, wrap, x, y, anim, frame | yes | solid cell marker0 (DS:CE72, colour 0x60) |
  | 2896 | `sprite_solid(1,...)` | same | yes | solid cell marker1 (DS:CE70, colour 0x70) |
  | 25FF | `sprite_solid_raw(0,...)` | sheet, x, y, anim, frame | no | marker0 |
  | 27E2 | `sprite_solid_raw(1,...)` | same | no | marker1 |
- "Solid" variants read every pixel from the one marker cell (source never advances, `C188` page offset not added) = a one-colour silhouette of the sprite (hit flash / shadow). Unclipped ones do not track row/column at all.
- All six now go through one `blit()` in sprite.c. Verified: 100 scenarios x 6 blits over the five new variants, 0 mismatches; 14 mutants killed (the 15th, in the `249E` wrapper, is covered by `diff_sprite.py`).

## 26. Type 8 enemy 0BA5:0CE8 (ported: `enemy_update_type8()` in `port/src/enemy.c`, test `tests/diff_enemy_type8.py`)
- Type 8 = hovering shooter. Per frame: boss bar (`0C5B`, always, even for hp_max <= 20); screen pos = (x - 2*cam_x, y - 16*cam_row).
  - **Spawning** (bf84 > 0): 1 in 5 frames a dust puff at the centre +- (r12-r12, r3-r3) (RNG order: rng(5), y: r12, r12, x: r3, r3); bf84--; nothing else happens (no drawing).
  - **Drawing**: frame c034 0..3 -> facing = frame >= 2, sprite frame = frame & 1 style (frame < 2 ? frame : frame - 2); odd `flash` = silhouette blitter (`2896`, marker1) else the normal one (`249E`); flash counts down.
  - **Shooting**: only on frame 1, 1 in 10, and only when the enemy is right of the player (x > player column): `0922(x, y, aim=1, delay=0, def, sheet)` then sound 0xC.
  - **Wiggle** (when timer c0c4 == 0, then timer = r10 + 10): moves +-1 column around its anchor (vx = the spawn x set by `04D3`): 5 or more away -> always back toward the anchor, else a coin flip; frame (c034) goes -1 / +1 with the move and wraps 0..3. Timer decrements every frame.
- Two draw hooks exist now for tests and the platform layer: `fx_set_draw_callback` (blitter 249E) and `fx_set_solid_callback` (2896). In the game both will be wired to `sprite_draw` / `sprite_solid(1, ...)`.
- Verified: 300 random scenarios (pools, VRAM from the boss bar, RNG, sound, both draw hooks, projectile pool), 0 mismatches; 24 mutants, all killed after biasing a share of scenarios toward the shooting roll (one early "survivor" was a mutation of the spawn function, not this one).

## 27. Final boss 0BA5:1019 (ported: `enemy_update_boss()` in `port/src/enemy.c`, test `tests/diff_enemy_boss.py`)
- Type 0x63 (99) = the final boss. The routine is hard-wired to enemy slot 0 (absolute DS addresses BFF4/BF84/C0B4...). Boss phase = DS:CF46 (`g_boss_phase`, 0..4).
- Frame: boss bar (always), then the same spawn-dust as type 8 while bf84 > 0 (shared helper `spawn_dust`), otherwise: facing = x > player column (recomputed every frame), draw (odd flash = silhouette `2896`, else `249E`) with frame c034, flash--.
- Attack cycle (timer c0f4, state c0d4): timer 0 -> state 0: frame 1, state 1, timer 4; state 1: state 2, timer 1 and **fire a projectile** (`0922(x, y, aim=1, delay=0, def, sheet)`, no sound); state 2: frame 0, state 0, timer = rng(0x2D) + 5. Timer > 0: timer--.
- Phases: if threshold[phase] > hp (signed) and phase < 4: state < 3 -> start dying (state 3, timer 20, frame 2, sound 5); state >= 3 and timer 0 -> phase++, slot 0 freed and re-spawned with `04D3(def 0, x = t[ph]<<2, y = t[4+ph]<<4, cell = t[8+ph])`, then hp = t[15+ph], facing = t[12+ph], vx = 0, timer = rng(0x32) + 5, state 0.
- Tables DS:2BB6.. are read as ONE flat block (x 128,128,113,128 | y 54,48,42,36 | cell 13088,11648,10193,8768 | facing 1,1,0,1 | hp/threshold 600,400,200,-1,-16). The arrays are 4 entries but phase 4 indexes the 5th entry, reading into the next table (garbage); hp[ph] and threshold[ph] are one shared array offset by one. Phase 4 would need hp < -1, which the shot code cannot produce (hp -1 = invulnerable, hp 0 = dies), so it never happens in play, but the port reproduces it anyway.
- Verified: 300 random scenarios (all enemy arrays, effect pools, VRAM from the bar, RNG, sound, both draw hooks, projectile pool, phase, sound deadline), 0 mismatches; 31 mutants including every table value all killed.

## 28. Per-enemy update loop 0BA5:126C (ported: `enemies_update()` in `port/src/enemy.c`, test `tests/diff_enemies_update.py`)
Per slot (0..7), skipping free slots (type -1):
- type 8 -> `0CE8`, type 0x63 -> `1019` (sections 26, 27). Otherwise: boss bar if hp_max > 20; while spawning (bf84 > 0) only the spawn dust.
- bf64 > 0 = firing pose countdown, bfb4 > 0 = movement delay (refilled from bfc4 every frame by the tail): count down and skip movement/animation.
- Movement: bfa4 != -99 = mid-jump (y += bfa4, bfa4++ until 7, then frame 0). Otherwise x += vx, y += vy and a box scan of the fg layer over ceil(w/4) x ceil(h/16) cells sets `blocked`; `cell` = top-left cell (+1 when facing right).
- Per type (jump table CS:1F9A): **2/3/6 flyers**: timer (C074) <= 0 also blocks; blocked = undo the move, timer = r(30)+5, new random velocity (type 2 with def w5 drifts toward the player; type 6 doubles vx and only retries on vx == 0; type 3 turns to the player). **0/1/7 walkers**: floor check one row below the box at the leading edge; empty (0xFF) = undo the move and turn round. Type 0 facing the player with def w7 (c104) fires with chance 1/DS:7042 (firing pose: frame hdr[21], bf64 = 9), else 1/20 turns to the player. Type 1: 1/20 turns to the player and starts a shooting anim (bf94 = r(15)+10, frame hdr[21]). Type 7 mid-jump: floor check + 1/20 turn. **Type 7 on the ground: jump away from shots (see bugs).** Type 5: turns to the player. Type 4: nothing.
- Tail: frame++ and wrap: in the bf94 anim range hdr[21]..hdr[22] (or 17..18 if hdr[21] = -1); while c124 (shot in flight) the fire range ends at hdr[22] and then c124 = 0, frame = hdr[17]; else hdr[17]..hdr[18]. bfb4 = bfc4 unless bf94 counts down.
- Fire check (also reached from the bf64/bfb4 paths): (c104 && 1/DS:7042 && type != 0) or bf64 == 1 -> c124 = 1 and `0922`: def w2 (speed) != 0: aimed shot (x, y, aim 1, delay 1 (type 5: hdr22 - hdr21)), only when facing the player if c114 (def w6) is set; speed 0: dropped from (x + c0d4, y + h), aim 0. Failed spawn clears c124. Frame = hdr[21] if set.
- Jumping frame = hdr[19]; draw (odd flash = silhouette 2896, else 249E) with sheet = low byte of the sheet slot, wrap = hdr[13]; flash--; then the spawn dust if bf84 > 0.
- Sheet header words used: 13 width, 17/18 walk frames, 19 jump frame, 21/22 fire frames (21 = -1: none).
**Original bugs/quirks reproduced exactly**
1. **Type 7 shot check reuses the slot loop variable** (`for (i = 0; i < 10; i++)` with the same `i` as the outer slot loop): shot i is compared with *enemy i*, the jump (bfa4 = -6) is given to *slot i* (shots 8, 9 write into the next array, BFB4[0..1]), and the rest of that frame runs with slot index 10, which in DS memory is slot 2 of the *next* array (frame[10] = facing[2], c124[10] = pending-spawn cell[2], ...); the outer loop then ends, so later slots are not updated that frame. The port indexes the enemy table flat (`EF()` macro) with the pending list inside the struct to land on the same words.
2. The test is `abs(shot_x - x < 10)` (precedence slip): any shot to the left, or less than 10 columns to the right, counts.
3. `blocked`, `cell` and the box size in cells are only computed on the non-jumping path; mid-jump enemies (and the fire paths) use the previous slot's values. The port keeps them across slots; before the first slot the original has stack garbage (port: 0; the test zeroes the stack).
4. The fire delay is hdr22 - hdr22 + 1 = always 1 (except type 5).
5. Out-of-layer tile reads (only possible with the garbage locals): the original reads past the layer; the port treats them as empty (0xFF), and the test fills the memory past the layer with 0xFF to match.
- Verified: 200 random scenarios (8 random slots of every type incl. 8 and the boss, random tile layer, shots, projectile pool, headers, defs): the whole enemy table + pending list (flat), projectile pool, effect pools, VRAM, RNG, both draw hooks, sound and boss phase match, 0 mismatches. 33 mutants all killed, including "fixing" either bug, resetting the stale locals per slot, and every per-type constant (two needed more timer-0 / more scenarios first).
- With this, all enemy code in 0BA5 is ported: spawn (04D3), pending spawns (2E9D), shots (0267), kill-all (01B6), despawn (0439), player contact (0006/00D3), projectiles (0922/0A81), boss bar (0C5B), type 8 (0CE8), boss (1019) and the main loop (126C).

## 29. Player / tile interaction 0BA5:39F3 (ported: `level_player_tiles()` in `port/src/level.c`, test `tests/diff_level.py`)

Called once per frame by the level loop (when no teleport is pending, CF06 == 0).  Decrements the invulnerability counter (CE74), then looks at the 3 x 2 cells around the player (outer loop = columns -1, 0, +1, inner = 2 rows; cell = row*240 + (player_x+1)/2 + col + 240*row_k; "centre column" below = col 0) in the info layer (DS:705A, words, 0x7530 = nothing).  The value picks the action by range:

| value | action |
|---|---|
| 0..22 | **item**, table DS:2628, 42 bytes each: `+0` points (word), `+2` health (byte), `+3` mana (byte, DS:7046, cap 10), `+4` kind (byte), then a name string (the table names are an editor list, e.g. "Diamond", "Goblet", "Crown"; they do not line up exactly with the effects, so the port names kinds by effect). |
| 23..32 | **teleporter**, table DS:AA38 + 4*v = {from cell, to cell}: only fires from the exact cell, centre column only. Sets CF06, CF04 = (to/240)*16, CF02 = (to%240)*2, CE92 = DS:C1BE (sprite slot 0 header word 15), unfreezes the camera (CF0C = 0), snaps player Y to a tile row, clears the cell, sound 5, tag 6. |
| 33..55 | **lever**, records DS:A89A (22 bytes: mode, 4 linked cells, 4 expected tiles, rectangle). Needs the "use" flag (DS:70AE) and the centre column: toggles the background tile between DS:6D69/6D6A; then, if at least one linked cell is set (not -1) and every linked cell shows its expected tile -> sound 7, CE76 = 0x19, rectangle opened through the transient layer DS:7056 (mode 0: 1 = remove foreground where DS:7062 is not 0xFF; else 2 = swap in DS:705E tile), linked cells cleared to -1 (one shot); otherwise sound 6. |
| 56..80 | **door type 1**, records DS:A76E (12 bytes: kind, tile, rectangle): kind 0 = free, 1 = needs DS:CE84 key, 2 = needs DS:CE82 key. Opens the rectangle with the "swap" rule, CE76 = 10, sound 7, clears the cell (background = door tile for a free door, else DS:6D68); a used key is consumed and a puff is spawned at the cell. |
| 81..105 | **door type 2**, records DS:A642: same but uses the "remove foreground" rule. |
| 106..115 | nothing |
| 116..365 | **spawn trigger**, table DS:8622 + 32*(v-116) (first 8 words used): each listed cell whose info is not 0x7530 and that is neither a live enemy's cell (C004) nor already queued is added to the pending list DS:C134 (first free entry). Left column only. |

Item kinds (table byte +4), after the common checks (health item with health < 100 -> sound 1 and taken; health item at full health -> left in place; points != 0 = treasure: counts towards the goal CE6A/CE68 (sound 9 and CFD8 = 1 when they meet), score tag by value 100/250/500/1000/5000 = tag 0..4, sound = 2 + rng(2)):
1 extra life (DS:7048++, CE78 = 8, sound 4) - 2 hazard (damage by difficulty, invulnerability 20, centre column only, item stays) - 3 shield (CE8E = 400, 4 puffs around the player, invulnerability cleared, tag 8) - 4 CF10 = 1, CF0E = 25, tag 7 (centre column, once) - 5 potion (CE88 = 600, DS:7046 = 10, old value saved in CE86, tag 9) - 7 / 8 keys (CE84 / CE82 = 1, tag 5) - 9 hazard with invulnerability 10 - 10 hint sign (needs the use flag: runs 38B6(index) for every hint record DS:AAC4 (504 bytes: col, row, 10 text lines of 50) whose row equals the player's row and |col - player tile| < 3, wrapped in latch-mode 0 / map-mask 0xF ... latch-mode 1 / map-mask 0xF, sets CF4E) - 11 CE94 += 3, CE98 = 1, CE96 = 5 - 12 checkpoint (CF52 = player X, CF50 = player Y, clears this and the next cell).
Every item then: sound 9 if mana > 0; mana += value (cap 10); while the potion timer runs the saved mana is raised too; score (DS:7052, 32 bit) += points (sign-extended); health += health byte (not clamped here - a "full health" item that is left in place keeps adding every frame, so the clamp must be elsewhere); cell cleared (info = 0x7530, background = DS:6D68) unless the action said to leave it.

Original quirks reproduced:
- the screen-X local (bp-0x12) doubles as the hint loop counter: after a hint with the use flag it is 10, so every later score tag / puff spawned from that column uses x = 10;
- CF4E is cleared at the start of every column iteration, so only the last column's value survives;
- negative info values (never in real maps) would index before the item table; the port ignores them, and cells past the end of the map read as "nothing";
- the modal hint box 38B6 is a hook (`level_set_hint_callback`); it blocks on a key press in the original.

Test: `tests/diff_level.py` - random info layer around the player (with per-scenario focus on one category / item kind), random item / teleporter / lever / door / hint / spawn tables, layers, pools and VGA state; compares every touched variable, layers, tables, pending list, effect pools, VGA, RNG, sound and hint-box calls (with the VGA masks at call time).  1,500 scenarios 0 mismatches; 71 mutants of `level.c`, all killed except one equivalent (the `break` after queueing a spawn cell: the duplicate check stops a second insert anyway).  Note found by the test: the loop over columns runs -1, 0, +1 (three columns), not two.
Still to do in this area: `38B6` (the modal hint box, uses the font/text blitters in segment 06B8), and whoever fills the tables above from the level file (453A).

## 30. HUD 0BA5:348D with 3125 / 3297 / 3643 (ported: `hud_draw()`, `hud_icon8()`, `hud_icon11()`, `level_count_items()` in `port/src/level.c`, test `tests/diff_hud.py`)

- `3125(dest, icon)`: copies an 8-row x 2-byte (8 x 8 px) icon inside VRAM from page 3 (DS:645C, offset 0x39D0 + 2*icon, i.e. row 185) to the draw page (DS:6460 + dest) with latch writes; `3297` is the same for 11 rows from 0x39E6 (keys / small icons).  The caller must have set bit mask 0 / map mask 0xF.  Rows are 80 bytes apart.
- `3643`: counts info cells (all 0x3840 = 14400) whose value is < 0x17 and whose item kind (table DS:2628, +4) is 1 -> DS:704A.  It is the number drawn next to the lives (the "items left" counter).
- `348D` (HUD, every frame): level icon at 0x392A (icon = DS:706C + 1; once the level goal CFD8 is set it alternates with icon 10 for 20 frames), score (ltoa of DS:7052/7054) right-aligned ending at 0x389A, health (clamped to 0..100, three blanks then digits ending at 0x38A9), lives (first digit of DS:7048) at 0x38B7, items-left first digit at 0x38BB, key icons: slots 0x3875 / 0x3877 are blanked (icon 2), both keys -> icons 0 and 1, one key -> its icon at 0x3876 (icon 0 for DS:CE84, else 1).
- Quirks kept: only the first digit of lives / items-left is drawn; the digit position formula 0x389A - len + 2*i is not a right-alignment for longer numbers; negative numbers draw the '-' as icon -3 (reads the bytes before the icon strip).
- Test: 6 operation kinds, random VRAM everywhere (incl. the icon page), random item table / info layer / HUD variables, VGA mask / latch state random; 150 scenarios 0 mismatches; 23 mutants, all killed except `v > 0x17` (it only differs for info value 0x17, which reads past the item table into unrelated data).

## 31. The level function 0BA5:453A - map, and the first piece (ported: `frame_controls()` in `port/src/frame.c`, test `tests/diff_frame_controls.py`)

`453A(mode, pause)` is the WHOLE level: setup, frame loop, end-of-level / death sequences (3,143 instructions, 4540..6A4D, returns bp-8).  `mode` (bp+6): 0 normal, 1 recording a demo, 2 playing one back (DS:790C = 1 then); `pause` (bp+8) is the pause request.  Map found so far:

| address | what |
|---|---|
| 4540-4D44 | **setup**: clears the player / effect / enemy tables, loads the level from HOCUS.DAT through `68C:F9(entry, far dest)`: entries base+0x83 -> DS:703C (start x/y), +0xA7 -> 6D68 (tile constants, anim table), +0xCB hints AAC4, +0xEF teleporters AA94, +0x113 levers A89A, +0x137 doors1 A76E, +0x15B doors2 A642, +0x17F enemy defs A552, +0x1A3 spawn groups 8612, +0x1C7 layer 7066, +0x1EB layer 7062, +0x20F layer 705E, +0x233 info layer 705A (base = DS:2A1A[episode*10 + level], episode DS:706E, level DS:706C); zeroes layer 7056; counts goal items (CE68 = info cells 0..22 with points != 0) and CE6E; tile PCX (+0x29CA table) -> page 3 and the 16x16 tile pack, sprite sheets via 2242, backdrop PCX, palette, HUD frame; demo data for mode 2 from entry 0x12 + level/2; final-level boss set-up (episode 3, level 8) |
| 4D47-4D67 | loop head: swap draw / show pages |
| 4D67-4E87 | wait for the timer tick (DS:790A:7908 vs DS:14F8[790E]), joystick poll, camera 29C5, tile interaction 39F3, special 220F (CE78) or map + backdrop draw (124A:FE5/2AA or F15/6) |
| 4E87-4FE3 | HUD 348D; death: health 0 starts CE8C = 0x5A and CE8E = 0x7530; spark effects while it counts down |
| 4FE3-50B1 | level complete: DS:704A == DS:7048 starts CE8A = 0x5A, kill-all 01B6, sparks |
| 50B1-533C | **player sprite**: picks the frame word (CE92 from sprite slot 0 header words C1C6/C1C8/C1CA...), draws it with 249E / 2896 (blink when invulnerable, odd CE74) / 26B3 (potion timer odd) variants; teleport animation by CF06 |
| 533C-53FF | power-up sparkles (CF0E), shot-counter indicator (CE94 / CE98 / CE96), shield timer CE8E countdown |
| 53FF-5469 | pause screen (mode arg) |
| 5469-5495 | per-frame updates: 0267 0006 00D3 2E9D 0A81 126C 0439 2FF3 36F0 1FAA 20B1, 2C81 on alternate frames (bp-0xD) |
| **54AC-5824** | **ported (section below)**: demo record / playback, joystick, fire, potion timer |
| 5824-5F62 | player shot pool update (10 slots: move, hit tiles with tile id DS:6D6B -> removed + puff, draw) - next |
| 5F62-694D | player movement, jump, fall, collisions, camera follow - not read yet |
| 694D- | loop condition / exit handling, then the end sequences up to 6A4D |

`frame_controls(mode)` = 0BA5:54AC..5824: counts down the byte local bp-0xC; mode 1 clears the demo record CF40..CF44 (up, left, right, fire, jump); mode 2 copies 6 bytes from DS:48C6 (far pointer) + CF3E*6 + 2 into CF40.. and drives the input flags from them (latches derived: fire latch = fire ? 0 : 1, up latch likewise), CF3E++; CF49 = fire held, CF48 = up held (from the latches).  When joystick support is on (DS:48CE) and no teleport / death / finish is running and no demo plays: the keyboard left / right / down / up flags are cleared and rebuilt from the joystick words (78F2 left, 78F0 right, 78EE up, 78EC down, 78EA = swap buttons, 78FA / 78F8 = buttons) with the same edge latches as the keyboard ISR.  Fire (70AA, not during a teleport): single-shot unless the potion timer runs; finds the first free slot of the shot pool (x == -99), allowed while live shots (CEAE) < DS:7046; power shot flag when CE94 > 0 (decrements); shot goes up when up is held (x = screen x + 1, y = screen y) else sideways (x = screen x +- 2, y = screen y + sprite header word 25); direction = CE90; sound 0 (0xD with the potion).  Finally the potion timer CE88 counts down; at 1 it ends and the saved mana CE86 is restored to DS:7046.
New fields: `g_shots.up` (CEC6), `.dir` (CEB2), `.cursor` (CEB0); `g_lv.death_timer` (CE8C), `finish_timer` (CE8A), `facing` (CE90).  Test technique: `tests/range_harness.py` runs the original between two addresses inside a fake frame (bp+6 = mode), so pieces of 453A can be tested without running the whole level.  600 scenarios 0 mismatches; 33 mutants all killed (the shot-cursor reset needed 600 scenarios).

## 32. Player shot pool update 0BA5:5824..5F6A (ported: `shots_update()` in `port/src/frame.c`, test `tests/diff_shots.py`)

Loop over the 10 slots of `g_shots` (x at CEEE, y at CEDA = `g_fx.e_y`, power CE9A, up CEC6, dir CEB2, count CEAE).  A slot with x == -99 is free.  Three variants, chosen by `up` then `dir`:

| variant | cell index `di` | cells tested against the breakable tile DS:6D6B (`g_shot_block_tile`) | burst when (and spawned at) | move / cull | frame (C1D4 + ...) | sparkle (RNG(10) > 2) |
|---|---|---|---|---|---|---|
| right (up = 0, dir = 0) | (y/16 + cam_row)*240 + x/4 + cam_col + 1 | di, di+0xF0, di+1, di+0xF1; puff at (x+2-2*col_odd, y-8), (x+2.., y+8), (x+6.., y-8), (x+6.., y+8) | fg[di] or fg[di+1] != 0xFF: `2F1C(x*4+0x18, y+0xA)` | x += 4; free when x > 0x4F or y < 0 | +2 if power | `21C6(x, y+5)` |
| left (up = 0, dir = 1) | same | di, di+0xF0, di-1, di+0xEF; puff at (x-2c, y-8), (x-2c, y+8), (x-2-2c, y-8), (x-2-2c, y+8) | fg[di-1] or fg[di] != 0xFF: `2F1C(x*4+1, y+2)` | x -= 4; free when x < -3 or y < 0 | +2 if power | `21C6(x, y+5)` |
| up (up = 1) | as above, +1 only if dir = 1, then -0xF0 (+0xF0 back if that went negative) | **di - facing** (CE90!) and +1; puff at (x-1-2c, y-15), (x+3-2c, y-15) | fg[di] (without the facing offset) != 0xFF: `2F1C(x*4+8, y+2)` | y -= 0x10; free when x > 0x4F or y < 0 | +3 if power, +1 otherwise | `21C6(x + 2*facing, y+5)` |

(c = col_odd.)  On a tile hit the cell becomes 0xFF, the slot is freed (x = -99, count--) and no draw / sparkle happens.  Draw = `249E(0, C1CE, x, y, dir, frame)`.  The in-branch "if up" tests of the right / left variants in the original are dead code (up was already excluded) and are not ported.  Quirk kept: the up variant's facing-offset index, and the burst test ignoring it.

Test: 10 random shots per scenario (all three variants, free slots, edge x / y values), random fg layer with blocking tiles planted around the live shots, random camera / facing / header words, random effect pools + RNG state.  Compared: shot arrays, count, fg layer, effect pools, RNG index, VGA state and the list of 249E draw calls.  Cells are kept inside the 240x60 layer (the original would read whatever lies beyond it; the port treats that as empty).

Verification: 1500 scenarios 0 mismatches; 50 mutants (every second operator / constant mutation in `shots_update`), all killed after adding the boundary starting positions x = 0x4C and 1 to the generator.  New helper `tests/mutate.py <src.c> <marker> <test.py> <nscen> [stride]` runs this kind of mutation test (it restores the source afterwards; if it is killed mid-run, restore the file from a backup).

## 33. Player movement 0BA5:5F6A..6700 (ported: `player_move()` in `port/src/frame.c`, state `g_mv`, test `tests/diff_move.py`)

`player_move(mode)` runs once per frame after the shot pool.  Everything below is in 16-bit signed arithmetic; map cells are `row*240 + column` into the fg layer (`g_map.fg`, 0xFF = empty).  `x` = `g_cam.player_x` (CF32, units of 8 px, 0..480), `y` = `player_y` (CF28, pixels), `row` = `player_row` (CF2E, only recomputed during a jump: `y/16`).

1. **Teleport slide** (when `g_lv.teleport` != 0 the rest of the block is skipped): if the player is already on the target (`tele_col2`, `tele_row_px`) the counter runs 0x19 -> 0x28 -> 0 (sound 5 at 0x19), otherwise it counts up to 0x19.  At exactly 0x19 the player moves one step per frame towards the target: y by 16 px, x by 1.
2. **Jump start**: jump pressed, not already jumping (`vfreeze` == CF0C, shared with the camera: it freezes the camera row), no vertical scroll (CF0A) and not falling (CF08).  Sound 0xF; picks the table DS:2C10 (19 entries, peak 9) or, if the item-4 power-up flag is set (consumed, sound 5), DS:2BDE (25 entries, peak 0xC).  `g_jump_tab_normal` / `g_jump_tab_power`.
3. **Jump arc** (while `vfreeze` and not dying / finishing): `y += tab[idx]`, `row = y/16`; if the cell at `row*240 + (x+1)/2` is not empty the arc is mirrored (`idx = len-1-idx`, `y += tab[idx]` again); `idx++`; at `len` the jump ends (`vfreeze = 0`, `g_frame.jump_end` = bp-0xE).
4. **Floor** (cell `row*240 + (x+1)/2 + 0x1E0`, two rows below the head): solid -> `fall = 0`; a jump past its peak index ends and `y = row*16` (sound 0xF); landing after a fall plays sound 0xF once (`g_frame.landed` = bp-0xF).  Empty and not jumping -> `y += 16` and, if the cell 3 rows down is also empty, `fall = 1`.  A jump that ended without starting a fall plays sound 0xF.
5. **Side flags**: column `x/2` (left) and `(x+2)/2` (right): `free_*` = rows row, row+1 (and row+2 when y is not a multiple of 16) all empty; `step_*_up` = row-1 and row empty but row+1 solid (a one-tile step).  Both step flags are cleared while falling or jumping.
6. **Walking**: left (needs x != 0) / right (needs x < 0x1DD).  If the facing is the other way the player only turns (no step).  Otherwise `free_*` -> x +-1, else `step_*_up` -> x +-1 and y -= 16.  Each step advances the walk frame `ce92` (wraps from hdr[18] to hdr[17]) and sets `moved`.  The demo recorder bytes CF40 (up) / CF41 (left) / CF42 (right) / CF44 (jump) are set in mode 1.
7. **Lift platforms** (tile ids `plat_tile_a` CF4A = right half, `plat_tile_b` CF4C = left half, -1 = none; not while a hint is shown): standing on one, up (70AF or 70AE) moves the two-tile platform and the player up one tile when the cells above are empty (the up key is consumed: 70AE = 0, latch 70A7 = 1); down moves them down one tile.
8. **Idle frame**: not walked and fire not held -> walk frame = hdr[15].
9. **Look margin**: `plat_up` / `plat_down` (CF56 / CF54) count frames up (70AF) / down (70AD) is held, capped at 10; look-down request (joystick 78F6, key flag 70A3, or `plat_down`) lowers `vmargin` (CF36) to 0, look-up (78F4 / 70A4 / `plat_up`) raises it to 8; with neither requested and neither counter at 10 it resets to 5.  (`look_off` CF34 is never non-zero in this block.)

After this block the original continues at 6700 with the system keys (pause 7098, save/load/quit 70A8/70A2/70A1/70A0, sound / music toggles, demo record flush) - not ported yet.

**Integration**: the 70AE byte used to exist twice in the port (`g_lv.use_flag` in level.c and `g_in.up`); merged - level.c now reads `g_in.up` and `LevelState` has no `use_flag` field.

Verification of section 33: 1500 scenarios, 0 mismatches (categories: lift, step, jump, fall + random).  75 mutants (every second mutation of `player_move`): 74 killed, 1 equivalent (`py < tele_row_px` vs `<=`: on equality the +16 is undone by the following `py > tele_row_px` step).  The first mutation run killed only 47 of 75, which showed that random scenarios rarely reach lifts, one-tile steps and the mirrored arc; the test now has focus categories that plant those situations.

## 34. Player sprite, sparkles, shot icons, shield countdown 0BA5:50B1..53FF (ported: `player_draw()` in `port/src/frame.c`, test `tests/diff_pdraw.py`)

Blitter calls are routed through one hook (`frame_set_blit_hook(which, a0..a5)`, `which` = original address 0x249E / 0x2896 / 0x27E2 / 0x26B3 / 0x25FF / 0x23BA, arguments in call order; default hook = the sprite.c blitters).  The test patches all six entry points of the original to stub ports and compares the call sequences.

1. Shield timer (`g_player.shield`, CE8E, 0x7530 = permanent) != 0: nothing is drawn, no sparkles, no icons; it counts down by one (sound 5 when it reaches 0).  (Nothing else in this block runs.)
2. Teleport counter (CF06) == 0: choose the player's frame word `ce92`: fire held (CF49) and not walking: `hdr[21]` (+1 when up is held, CF48); jumping: `hdr[19]`, past the peak index: `hdr[20]`; falling: `hdr[20]`.  Draw: if the player is above the screen (`pscreen_y < 0`) first a clipped `249E`; then by state: invulnerable (`g_player.invuln` odd) silhouette marker 1 (`2896` clipped / `27E2` unclipped), potion active (`potion_timer` odd) silhouette marker 0 (`26B3` / `25FF`), else plain (`249E` clipped / `23BA` unclipped).  So a player above the top edge gets the clipped plain sprite twice (kept).
3. Teleport counter != 0: `23BA(2, x, y, facing, k)` with k = 0 (t < 5), 1 (< 0xA), 2 (< 0xF), 3 (< 0x14), 4 (<= 0x19, y jittered by `RNG(2) - RNG(2)`), 3 (< 0x1E), 2 (< 0x23), 1 (< 0x28), nothing afterwards.
4. Item-4 power-up sparkles (CF0E != 0): `2174(x + RNG(7) - 2, y + RNG(10) + 0x14)` (RNG(10) is drawn first); the timer decrements only when the item-4 flag (CF10) is clear.
5. Shot-counter icons (CE94 != 0, blinking by CE98): if CE98 is set, min(CE94, 3) copies of sprite C1CE (frame C1D4 + 2) stacked above the player (x + 1, y - 14 - 14*i, facing); then CE96 counts down from 5 and toggles CE98 each time it hits 0.

Names to be aware of: DS:7048 (`g_lv.lives`) is the number of kind-1 items (crystals) collected, DS:704A (`g_lv.items_left`) the number on the map; the level is complete when they are equal (4FE3).  The `lives` name is a leftover from the HUD port.

Verification of section 34: 1500 scenarios 0 mismatches; 33 mutants, 29 killed on the first pass; the survivors were boundary values (teleport counter thresholds, y == 0, jump index == peak) plus one equivalent (`ce94 > 3 ? 3 : ce94` vs `>= 3`).  The generator now plants exact thresholds and keeps the teleport counter at 0 in 60% of the scenarios; the remaining real mutants are killed.

## 35. Death and level-complete countdowns 0BA5:4E8B..50B1 (ported: `level_end_states()` in `port/src/frame.c`, test `tests/diff_endstates.py`)

Runs right after the HUD (348D) each frame.  Two external routines go through `frame_set_event_hook(ev)`: 1 = `1392:0195` (stops the level music), 2 = `0BA5:01B6` (`enemy_kill_all`, default hook).

* **Death**: `health` (704C) == 0 and the death timer (CE8C) idle: timer = 0x5A, shield (CE8E) = 0x7530 (permanent, so nothing else can hurt), event 1.  While the timer is running: decrement, `RNG(4)` is drawn every frame; a blast happens when (`RNG(4) == 0` and timer > 0x19) or timer == 0x59 (so always on the first frame).  Blast = twice [ burst `2F1C(4*x + RNG(10) + 12 - RNG(10), y + RNG(10) + 16 - RNG(10))` (the draws happen in the order a,b,c,d = y+, y-, x+, x-), puff `2174(x + RNG(3) - RNG(3), y + RNG(12) - RNG(12))` ], then sound 0xA.  (x, y = player's screen position CF2A / CF26.)
* **Level complete**: `items_left` (704A) == `lives` (7048, the crystals collected counter) and the finish timer (CE8A) idle: timer = 0x5A, shield = 0x7530, events 1 and 2 (all enemies die).  While running: decrement; `RNG(7)` is drawn every frame; when it is 0 and timer > 0x19: two puffs (same offsets as above) and sound 9.

Verification of section 35: 600 scenarios 0 mismatches; 26 of 26 mutants killed.

## 36. DAT access and level data loading (`port/src/dat.c`, `dat_tables.c`, `loader.c`; generator `port/tools/gen_tables.py`; test `tests/diff_loader.py`)

* `tools/gen_tables.py HOCUS.EXE > src/dat_tables.c` dumps the 652-entry file table (EXE offset 0x1F1A4) and the per-level tables of the data segment: map base per level (DS:2A1A, index `episode*10 + level`), tileset (29CA), backdrop (2A6A), music (2ACE, `episode*9 + level`), lift-platform tile ids (2B16, `{CF4C, CF4A}` per level), tick table (14F8 = {8, 7, 6}).  `dat_open/dat_read/dat_load` read entries from HOCUS.DAT by those offsets (the file is not compressed).
* `level_load_data()` = the data half of the level setup (0BA5:470D..499F): loads the 13 map file types of entry `DAT_MAP + 36*type + base` into their tables: 0 start (8 bytes = {-, col, row, enemy fire chance}), 1 tile table (724 bytes: 4 constants `blank, lever a, lever b, shot-blocking tile`, then 240 x {first, last, type}), 2 hints (10 x 504), 3 teleporters (40), 4 levers (506), 5 / 6 doors (300 each), 7 enemy definitions (240), 8 spawn groups (8000), 9 background / 10 foreground / 11 alternate layers (14400 each), 12 info layer (28800).  Then: player start = (col*2, row*16), checkpoint = same, transient layer zeroed, `goal` (CE68) = info cells 0..22 whose item has points != 0, `g_enemies_total` (CE6E) = info cells 106..115 whose enemy definition word 1 != -1, collected / complete / kills cleared.
* Test: the original's `68C:F9` (read entry to far pointer) is replaced by a stub that serves entries from the real HOCUS.DAT, `68C:4` / `68C:32` (open / close) by `retf`; all 36 levels plus 72 perturbed copies of the DAT (random boundary values in the info layer, unused enemy definitions) compared byte for byte (all tables, the four layers, the info layer, the anim table, counters).  Mutation test: 20 of 24 killed, the other four are equivalent (index bound check, `<= 0x16` because item 22 has 0 points, loop bound reading one cell past the array).
* Not part of it yet: the graphics half of the setup (tile PCX, backdrop, HUD frame, palettes, the sprite loader 0BA5:2242), the global state resets at 4549..470D, the boss set-up for episode 3 level 8, and the demo data for mode 2.

## 37. Screen flash 0BA5:220F and level setup 0BA5:4549..4D44 (ported: `level_flash()` in `frame.c`, `level_setup(mode)` in `loader.c`; tests `tests/diff_flash.py`, `tests/diff_setup.py`)

* **220F** (runs instead of the map draw on frames where `g_lv.flash` != 0, then the HUD is still drawn): latch mode off, fill the draw page (0x3200 bytes per plane) with colour `0x77 - flash`, `flash--`, latch mode on.  Set to 8 by the extra-life item, so the flash fades over 8 frames.
* **Setup order** (everything before the frame loop): mana = 1; lift-platform tile ids from `DS:2B16` (`episode*20 + level*2`: first word -> CF4C, second -> CF4A); fire / up held = 0, demo position = 0, demo-playback = (mode == 2); clear both draw pages, show page 1; start the level music (DAT entry `0x258 + DS:2ACE[episode*9 + level]`, loop 1); then the **selective** resets: flash, teleport, invulnerability, shot count, shield, item-4 flag / timer, potion timer, death timer, landing flag, both keys, shot-icon counter (CE94), walk flag, vertical margin = 5, look offset = 0, shot cursor = 0, shot x = -99.  Of the effect pools only these fields are cleared: burst flags = -1, burst ring, puff ring, the puff counters (word 2), the score-tag lives (word 3), the sparkle x = -99 (`fx_level_reset()`; the other fields keep stale values the update routines never reach).  Enemies: every slot's type and map cell = -1, the 32-entry pending list = -1, the enemy-projectile types (BF14) = -1.  Note that `finish_timer` (CE8A) is *not* reset here.
* Then the data load (`level_load_data`), tile PCX (entry `105 + tileset`, palette applied) into page 3, the palette read back into the target palette, tile packing, the sprite load (ids at DS:C174 = {0, 1, 3, 2} + sheet word 0 of the first six enemy definitions, -1 where unused), the backdrop PCX (entry `89 + n`) into page 3 at +0x7D0 without palette, palette entries 7 (slot 0) and `73 + n` (slot 128), HUD icon images 11 / 14 / 15 at row 185, a one-step fade-out, the HUD frame image 12 at row 160 on pages 0, 1, 0, 1; items counted, crystals collected (7048) = 0, health = 100, player start, sprite header word 15 of slot 0 -> CE92, facing = 0, camera snapped once.
* Mode 2 loads the demo (`DAT 0x12 + level/2`) and takes the frame count from its first word (CF3C).  Episode 3 level 8 spawns the final boss (definition 0 at `DS:2BB6/2BBE/2BC6`, facing `DS:2BCE`, RNG(20)+10 attack timer).
* Last: keyboard hook (1392:0200), RNG index = 0, show page 0, apply the palette, clock = 0, enemy-sound deadline = 0.
* External loaders (music, pcx, palette, img, sprite loader, input hook, demo read) go through `g_level_hooks`; NULL members use the DAT-backed defaults.
* Verification: all 36 levels x 3 modes, random prior state; recorded external call sequences, every touched variable, enemy / projectile pools, effect pools, VGA state, RNG index and clock all compared: 108 setups, 0 mismatches.

## 38. The level function assembled: `level_run(mode, pause)` (`port/src/loop.c`; end-to-end test `tests/diff_run.py`)

Frame order of 0BA5:453A (after the setup of section 37), exactly as the original:
1. loop head 4D47: draw page = the other page, show page = the other page (no vsync wait), `timer_frame_wait(speed)`; joystick poll when a joystick is enabled and no demo plays.
2. `camera_update` (29C5); latch mode on, map mask 0xF; `level_player_tiles` (39F3) unless a teleport is running.
3. Flash (`level_flash`, 220F) instead of the world draw when `flash` != 0, else map draw (odd / even by the camera column parity) + backdrop draw (source = page 3 + 0x7D0); then `hud_draw`, `level_end_states`, `player_draw`.
4. If the pause argument is set: "press a key" screen (IMG entry 16 at byte column 12, row 46), wait for a key, restore.
5. Entity updates: shots vs enemies (0267), enemy projectiles hit player (0006), enemies hit player (00D3), pending spawns (2E9D), projectile update (0A81), enemies (126C), off-screen despawn (0439), bursts (2FF3), score tags (36F0), puffs (1FAA), sparkles (20B1); tile animation (2C81) on every second frame (`anim_toggle`).
6. `frame_controls`, `shots_update`, `player_move`, then the system keys (6700..694D: P pause, ESC menu / demo exit, F1 / F2 / F3 / F10 / C / V menus, M music toggle, S sound toggle, death / finish countdown reaching 1 ends the level, demo recording appends the 6 record bytes) and the loop test.
7. Exit (694D..6A4D): keyboard unhook, demo save (mode 1), 20-step palette fade, level music reset, end screens (finish: collected / goal; death), result: 1 finished, 0 died, -1 quit; exit codes 2 / 3 restart the setup (score restored from DS:704E, mana = 1).
All UI / file / sound pieces are `LoopHooks` (NULL = nothing): joystick poll, quit menu, pause, F-key menus, status messages, settings save, demo save, end screens, music, `frame(n)` (per-frame callback, also the abort path).

Facts found while assembling:
* Sprite header word 13 of slots 1 and 3 are what the original keeps in DS:C296 / C44E (`g_fx.sprite_b / sprite_c`): they must be refreshed after every sprite load.
* The effect modules draw through callbacks; `loop_install_defaults()` wires them to the blitters and initialises the sprite source page (DS:6458 = page 2).
* **Spawn table**: the 32-byte record of map file type 8 (loaded at DS:8612) is 8 enemy-definition words followed by the 8 map cells; the trigger code (39F3) reads the *cells* from DS:8622 + 32 * (info - 0x74).  The port had indexed the first half (found by the end-to-end run, fixed in `level.c`, `diff_level.py` now fills the table at 0x8612).

Verification: the original level function runs in the emulator on the real HOCUS.DAT with every graphics loader, blitter and game routine real (only file I/O, sound, music, keyboard hook, hint box and end screens are stubbed); the same scripted keyboard input (walk, jump, fire, look) drives it and `level_run`; at every frame head the four VGA planes, display start, palette, the four map layers, enemy / projectile / effect pools, 60 variables, shots, RNG index, clock, score and the recorded sound / music / event sequence are compared.

Verification of section 38: 72 level runs (all 36 levels, 2 seeds, 200-300 frames each, difficulty varied) match the original frame by frame.  Harness lessons: the original's Borland x87 stubs must be patched to real FPU opcodes (as in `diff_enemy_bar.py`) or the boss-bar routine corrupts SI in the emulator; port state (struct globals, timer, RNG index) is reset between runs by `reset_port()`.  `tests/shot_run.py` renders frames of the port as PNG.

## 39. SDL2 front end, Original mode (`port/src/main_sdl.c`, `port/Makefile`)

* Builds with `make` (Linux, libsdl2-dev) into one `hocus` binary: all game sources + `main_sdl.c`; the non-SDL sources also compile clean with MinGW-w64 (Windows build is left to the CI step).
* Window 320x240 logical (the 200 rows are stretched to 4:3 as on a CRT), integer-friendly scale `--scale N`, Alt+Enter fullscreen.  The software VGA frame is converted to RGBA and presented from the frame-timer idle callback (once per page flip) and from the palette-fade vsync callback.
* Keyboard: SDL scancodes are mapped to DOS set-1 make / break codes and fed to `input_scancode` (the game's own key tables and defaults then apply: arrows, Ctrl fire, Alt jump, PgUp / PgDn look, P pause, M / S music / sound toggles).  Esc quits for now (the original ESC menu is not ported).
* Level driver: starts at `--episode / --level`, finished -> next level (after level 9 the next episode), died -> replay.  The pre-level "press a key" screen works.  Hint boxes, F-key menus, end screens, sound and music are not wired yet.
* `--frames N --dump out.ppm` runs headless (`SDL_VIDEODRIVER=dummy`) and writes frame N: used as the smoke test for CI.

## 40. Text engine, modal boxes, end screen, star field (ui.c, stars.c)

**Text engine (06B8)** – font = DAT entry 0 (720 bytes, 90 glyphs of 8 rows for chars 0x21..0x7A, bit 0 = leftmost pixel); glyph advance = highest set bit + 2, space = 4, other chars skipped.  `05E9 text_draw(x,y,colour,str)` solid colour, leaves latch mode ON; `071F text_draw_grad(x,y,style,str)` style 0 = flat 0x80, else colour k_grad[style-1] + row with k_grad = {192,200,208,216,224,232,112,104,208,0} (DS:1AE6), leaves latch OFF; `087C text_width`; `094B` = shadow draw (style 0 at +1,+1, then style at x,y).  Port: `text_draw`, `text_draw_grad`, `text_width` (diff_text.py, 400 scenarios).

**Modal boxes (0BA5)** – `37FC ui_message(text)` and `38B6 ui_hint_box(index)`: drawn on the VISIBLE page, shadow colour 1 at +1,+1 and text 0x68; wait for a key RELEASE (DS:7099 >= 0x80) then a PRESS; restore draw page = hidden page.  The hint box (10 lines max, 12 px spacing, centred on y 0x50 upwards) does not wait while a demo plays.  diff_msg.py, 200 scenarios.

**End screen 06B8:4889 `ui_end_screen(finished, goal, collected)`** (params bp+6 / +8 / +0xA).  Black palette, draw page 0, clear, show page 0.  Lines, y starts at 0x28 + (0x90 - (finished*0x28 + 0x40)) / 2, centred with style: title (style 4, "Castle complete - congratulations!" / "Tough break - you'll get it next time!") +0x10; "Results for Level N" (N = level_icon + 1, style 2) +0x18; "Treasures found: a  Treasures available: b" (style 3) +0x10; "Accuracy: P%  " + (not finished: "Castle not complete" | P < 100: "Less than 100% - NO BONUS" | else "BONUS 25,000/50,000/75,000 points!" by difficulty 0 / 1 / >= 2) (style 2) +0x18, P = ftol(float(found/goal * 100)) computed in x87 extended precision, stored as float32, 0 when found == 0.  If finished: "Time to beat: T  Your time: S" (style 3, T = DS:198A[episode*9+level] par seconds, S = (int32)clock / 140 where clock = DS:7904 = game clock at the level exit) +0x10 and a time bonus line (same 25k / 50k / 75k when T > S (unsigned), else "Not fast enough - NO BONUS") +0x18.  Bonuses are added to the score (DS:7052).  Then IMG entry 9 at (0, 0xB8), "Press any key" (shadowed, style 4, y 0xBC), IMG entry 10 at (0, 0), palette fade in (0x14 steps), flush the key buffer, star-field animation until a key is hit, flush, fade out.  The joystick wait path (DS:48CE) is not ported.  `g_exit_clock` is stored by level_run right after input_off.  diff_end.py (250 scenarios: real text, itoa, ldiv, x87, fades, stars).

**Star field 06B8:0103 / 039C (stars.c)** – 75 stars {angle word DS:7B6C, radius word 7AD6, speed byte 7A8A, brightness byte 7A3F, brightness step 79F4}.  Table DS:4CA8 = 360 x {x, y} longs = (float from DAT entry 5, 2880 bytes of 720 floats) * 1e6f, truncated (built at start-up by 0548:02F7).  Position = (0xA0, 0x70) + trig * radius / 1,000,000 (signed long maths).  Step: erase the old pixel if it is >= 0xF0; radius += speed; brightness += step, clamped to 0x7F; if the new position is outside 0..0x13F / 0..0xC7 respawn (angle = rng(360), radius = rng(160) until inside; speed = rng(5)+1, brightness = 0, step = rng(5)+1); put pixel 0xF0 + brightness/8 where the pixel is 0; then wait for the vertical retrace.  Init 039C does the respawn for all stars (plus a time() / srand that is irrelevant).  diff_stars.py (150 scenarios, init + step).

**SDL front end wiring (main_sdl.c)** – font = DAT entry 0, star table = DAT entry 5, `ui_set_poll` pumps events / presents / delays, `level_set_hint_callback(ui_hint_box)`, loop hook `message = ui_message`, `palette_entry` (palette resources reloaded after the exit fade, needed by the end screen), `end_screen = ui_end_screen`, `g_ui_hooks` {img from DAT, kbhit / getch over a 16-entry key-press counter fed by SDL key-downs}.  `stars_init()` runs at start-up with the RNG index 0 (the original does it in 0548 start-up).  Debug option `--end 0|1 [--frames N --dump f.ppm]` shows the end screen once.

## 41. Sound effects (sound.c)
Sound table EXE 0x20680 (DS:1574) = 16 x {VOC entry, PC-speaker entry, priority}: ids 0..15 -> VOC DAT entries 620,624,626,628,634,630,636,638,640,642,644,612,618,622,646,648 (entries 611..651 are VOC / speaker pairs; 611 is a larger VOC not in the table).  The original `136B:000B(id)` does nothing when DS:48CA (sound on) is 0; with a Sound Blaster (DS:643C) it plays the VOC through the Apogee Sound System at volume 0xFF; otherwise the speaker path (ids < 14, priority gate DS:6432).  The port plays the VOC samples through a small 8-voice software mixer (linear resampling, voice stealing by table priority, retrigger restarts the same id) fed by the SDL audio callback; the PC-speaker entries are ignored.  VOC decode: header offset at +20, blocks 1 (rate = 1e6 / (256 - divisor), codec 0 only), 2, 3 (silence), 0.  Checked by tests/test_sound.py (all 16 decode, lengths match the block sizes, bounded mix).  No differential test (hardware).  Music (DAT 599..610, 12 MIDI tracks, AdLib in the original) is NOT ported yet.

## 42. Music: MIDI sequencer (144e) + AdLib driver (161b) + OPL2 emulation (`port/src/music.c`, `opl.c`, `music_tables.c`; tests `tests/diff_music.py`, `diff_music_real.py`)
The music is Standard MIDI Files (DAT entries 599..610, `MThd` format 0/1) played through the Apogee Sound System's AdLib path. Only the mono AdLib path (DS:3348 = 0, one chip at 0x388) is ported.

**Sequencer (144e).** `0822 play(song far, loop)`: checks `MThd`, reads header length / format (>1 = error) / track count / division (-> DS:CFE6), walks the `MTrk` chunks into a linked track list (0x18-byte nodes: next, delta dword +8, active +0C, running status +0E, start ptr +10, cursor +14), `00F0` rewinds every track (reads first delta), `06BF` = all-notes-off + reset-controllers on 16 channels, installs timer task `015D` at rate `(int16)(division*120)/60` Hz (16-bit imul/idiv: division 480 overflows to 65404; the songs that have this set the right tempo in their first event), playing=1.
`015D midi_service` = one MIDI tick: per track `while (active && delta==0) event(); delta--;`; when the active-track count hits 0 (checked after every track): rewind; if looping, channels whose stored volume != 0x5A get volume 0x5A again and the walk restarts at track 0 inside the same tick, else playing=0.
Events: running status via +0E; status 0xFF = meta (type, 1-byte length - not a varlen!): 2F ends the track, 51 tempo -> `rate = (60000000/tempo)*division/60` pushed to the timer task, others skipped by length; 8/9 note off/on, B control (0x7E reads one extra byte; controller 7 goes through `05E5`: stored per channel at DS:CFE8, scaled `vol*master(DS:2ED2)/255`, then sent as controller 7), C program, E pitch bend (lsb, msb), A and D are skipped; 0xF0-0xFE are not skipped (the byte is treated as a status with cmd 0xF = ignored). Driver table at DS:2ED4: note off, on, aftertouch (null), control, program, pressure (null), bend; no volume/patch-load entries.
`06DC` master volume 0..255 (default from settings: music volume 0xC0), `077E/076C` pause/resume, `07B0` stop (frees tracks).

**AdLib driver (161b).** 9 voices x {modulator, carrier} slots (DS:2FEC/3010 tables), 16 channel records (0x1C bytes at DS:D092: voice list head/tail, program +8, bend +A, transpose +C, fine row +E, volume +10 (reset 0x5A), pan +14, +16, key/vel offsets +18/+1A (never set)), voice records (0x18 bytes at DS:D3DA: list links, key +A, vel +C, channel +E, pitch +10, timbre +14, status +16 = 0x2000 while keyed).
Voices come from a FIFO free list; when empty the oldest voice of channel 9 is released and reused, otherwise the note is dropped. Note on with velocity 0 = note off. Note on: timbre (`006E`: bank DS:3A6E, 11 bytes per timbre = op1 and op2 `20/40/60/80/E0` bytes + `C0`; channel 9 uses `drum_map[key*2]` as timbre index and `drum_map[key*2+1]` as pitch note), then volume (`030C`), then pitch (`0469`).
*Quirk:* the velocity clamp in `030C` compiles as `max(vel, 127)`, so every note is at full velocity; the carrier level is `(maxlevel * 0xFF * channel_volume) >> 15 ^ 0x3F | scale bits`.
Pitch: `note = key + transpose - 12` clamped 0..95; `fnum word = octave_pitch[note/12] | pitch_table[fine_row][note%12]`, written A0+v (low) and B0+v (high | 0x20 key-on). Pitch bend: `raw = lsb + (msb<<8)`, `t = ((raw*2)&0xFFFF)/512`, `transpose = (t>>5) - 2`, `fine = t&31` (centre = 0x4000). Fine-tune rows 1..31 = `trunc(pitch * 2^(row/384))` (computed by `161b:0838` with x87 pow at start-up; the port has them as a table).
*Quirk:* controller 0x7B ("all notes off") releases the channel's notes and then runs the full chip reset `095B` (regs 1/8/BD, silence all voices, re-initialise ALL voice and channel records); the sequencer sends it for all 16 channels at play/stop.

**OPL2 emulator (`opl.c`).** Compact melodic-mode YM3812 (9 voices, 4 waveforms, ADSR with KSR/KSL, tremolo/vibrato, feedback) at 49716 Hz, linearly resampled to the audio rate; the sequencer is clocked from the audio callback (`tick_rate` accumulator). Not bit-exact to the chip; frequency checked (437.7 Hz expected / 438 measured), envelope times in the documented range.

**Tests.** `diff_music.py`: random Standard MIDI Files (running status, all controllers, tempo/text meta events, multi-byte deltas, up to 16 channels) played by the original machine code in Unicorn (OPL write primitive `161b:0001`, malloc/free and the timer-task API stubbed) vs the port; every register write (reg, value), every rate change and the playing flag compared per tick, with random volume / pause / resume / stop calls in between: 300 songs, 151,135 writes identical. `diff_music_real.py`: all 12 real songs (DAT 599..610) for 2500 ticks each, identical.
Mutation test of `music.c`: 95/138 killed; the survivors are equivalent or untestable by the register stream (boundary compares that cannot differ, the never-set key/velocity offsets, the modulator's unused max level, header-validation edge cases, and the audio player which has no original counterpart). Real songs also match over a full loop (DAT 599: 30,000 ticks, 20,270 writes). A song with a delta but no event makes the original loop forever too (the emulator just stops at its instruction limit) - the generator avoids that.
**Wiring (`main_sdl.c`).** `music_init(rate)` with the audio device; `g_loop_hooks.music`/`g_level_hooks.music` = load DAT entry + `music_start` (only while `g_music_on`), `music_off` = `music_stop`; `audio_cb` adds `music_render` after the sound effects (gain x3 on the OPL output). Audible quality is untested (no audio device here); WAV renders of songs 599/601/607 are non-silent with the expected pitches.


## 43. Front end (menus), Original mode: `src/frontend.c/.h`

* **Real main** = `0548:035A` (init `0548:0014`: loads the whole asset set, fonts, settings from HOCUS.SAV).  Loop at `0548:0521`: draw page 3, IMG 14 at (byte col 0x1C, row 0xB9) and IMG 15 at (0x3C, 0xB9) (the 8+8 frames of the selection wand, 4 bytes x 15 rows each), `06B8:24C3` (main menu) -> jump table at cs:0x8E9 (keys 0x9D idle, 0..7).  Palettes: DAT 7 -> slot 0, DAT 8 -> slot 128.  Menu music = DAT entry 0x25A, looped.
* **Menu engine** (06B8): `0B21(id)` fills the table (DS:7C0C far pointers, 7C0A count incl. title, 7C08 first-selectable flag (also the hint index), 7C06 title flag), `19A4` draws, `2259(flag)` runs.  Ids: 0 main, 1 episode, 2 options, 3 level, 4 skill, 5 in-game, 7 speed.  Items are strings in DS (1A04..1AD4); a trailing `~` adds 4 pixels of spacing (and is not drawn: the font skips it).
* **Layout** (19A4): black palette, draw/show page 0, clear; widest item centred (x = (320 - w) / 2); no title: y0 = (0x90 - (n*10 + extra)) / 2 + 0x28; with title: y0 = (0x90 - (n+1)*10 + extra) / 2 + 0x28 (sic: `+ extra`), title centred in style 5 then +0x14; each item: whole string style 2 then its first letter overdrawn in style 4, row table entry y - 4, step 10 (+4 for `~`).  Footer: IMG 9 at (0, 0xB8), hint "Use UP/DOWN/LETTER to move - ENTER to select" shadowed (94B: style 0 at +1,+1 then style 4) at y 0xBC, IMG 10 at (0, 0); fade in 0x14 steps.
* **Run loop** (2259): Up/Down wrap (clear the 16x15 marker rectangle first), letter keys jump to the item with that first letter (toupper), Enter fades out (0x14) and returns the index, Esc fades out and returns 0xFF (ignored in the main menu), the marker (0080) is a latch copy from page 3: source column 0x1C + (anim/5)*4, anim 0..0x27, drawn at byte column (x-24)/4, then `0103` (sparkle animation, not ported) every iteration.  Idle for 0x1194 ticks: attract screen (42CB) then idle code 0x9D (demo handler 0570) - not ported.  Selection is remembered per menu (DS:1A04 etc.).
* **Main menu items** -> `0548`: 0 new game (episode menu 24DC -> skill menu 2527 -> `level_run(0, pause)`, pause = 1 only for episode 0 level 0; score reset, mana 1; completed -> next level, died -> replay with the saved score, quit -> main menu; after level 8 the score / ending screens 4D76, 40FD..41BD, 1BAE, 1C9B), 1 restore (2B8B), 2 ordering info (407D), 3 instructions (44CC(0)), 4 legends and hints (40BD), 5 options (loop 24F5: sound toggle, music toggle (off -> 136B:261, on -> music(0x25A, 1)), volume 137D, joystick 2F16, speed (menu 7 -> DS:790E and 48D0), define keys 17DA; leaving saves the 0x3DE byte settings block to HOCUS.SAV), 6 high scores (4D76), 7 quit.
* **In-game menu** (06B8:4600, menu 5): input off, fade out, saves the target palette (DS:82FA), black, reload the menu palettes, loop on `2540`: 0 how to play (44CC(0)), 1 -> returns 1 (abandon & restart), 2 save (25CC), 3 restore (2B8B; returns 3 when loaded), 4 options (same loop), 5 / Esc back, 6 -> returns 2 (quit to main menu).  Back: palette restored, `copy_page(0, 1)`, show / draw page restored.
* **HOCUS.SAV** (990 = 0x3DE bytes = DS:48CA..4CA8): words sound, music, joystick, game speed at +0/+2/+4/+6; the 8 key scancodes at +0x16 (default 0F 10 02 03 0D 0E 0B 0C); high-score names / scores later.  The port reads / writes the first words in the working directory and keeps the rest of an existing file.
* Port: `frontend.c` (menus verified by rendering - see `--keys` / `--frames N --dump` / `--esc-at N` test options of `main_sdl.c`; not differential-tested against the Unicorn harness).  Everything else of section 44 is ported too; only the joystick (calibration 2F16 / 2FA8, poll 1392:081D) and the PC-speaker paths remain.


## 44. Front end, remaining screens (Original mode) - all in `src/frontend.c`

* **Text pages** (DAT 41..71, format of 3.5): header `u16 lines, u16 step_text, u16 step_blank`, then 20 x `{u16 align, u16 style}` at +6 (style = colour at +8), 20 x char[80] at +0x56.  `3230` ordering pages (DAT 0x29.., 9 pages; centred, IMG 10 on top, y0 = (0x90 - height) / 2 + 0x28), `3401` hint pages (DAT 0x34.., 10 pages, y0 = (0xB8 - height) / 2, align 0 = centred else pixel x, one picture per page from DS:1912 = {entry, x, y} x 10), `364D / 3899 / 3AE5 / 3D31` episode ending pages (DAT 0x3E / 0x40 / 0x42 / 0x46 with 2 / 2 / 4 / 2 pages; pictures from DS:194E, 10 entries in the same order).  Footer "Page N of M - Press PGUP/PGDN - ESC to cancel" (first page PGDN only, last PGUP only) or "Press any key to continue" for a single page.  Navigation `3F7D`: PgUp / Left / Up = previous, PgDn / Right / Down = next, Home / End, Esc = leave; each change fades out (0x14).  Order sheet `429A` (DAT 51) only exists for the shareware flag DS:14F6 = 0, never reached by the registered version.
* **How to play** `44CC`: PCX DAT 25, 27, 28, 29 (26 is the joystick page), each fades in / waits for a key / fades out; Esc ends.  The in-game F1 variant saves the palette and pages like 4600.
* **High scores**: HOCUS.SAV +0x16C = char[4][5][26] names, +0x374 = u32[4][5] scores (defaults Hocus Pocus 1,000,000 / Andre Foucault 800,000 / Jamie Cook 600,000 / Karim Sultan 400,000 / Chris tenDen 200,000 in every episode).  `1BAE(ep, score)`: first entry whose score is below the new one; shift the entries below down, new name = "", returns position + 1 (0 = none).  `1C9B(ep, pos)`: "High Scores For Game N" (style 5, y 0x48), rows from y 0x5C step 10 (+10 after the first), name at x 0x1B, score right-aligned to x 0x122, style 4 for the top row else 3, footer "Enter name then press ENTER"; typing: chars 0x20..0x7A while length < 25, Backspace; Enter saves the 0x3DE block.  `2018(0)` shows the four tables, one page per episode ("Press any key to continue", Esc ends).
* **Episode end** `4D76`: pages 0 and 1 cleared, PCX DAT 4 on page 0, digitised fanfare DAT 615 (`136B:01B8` = play one whole VOC entry), fade in 0x3C, fade to white (05D8:04B0), fade out; then the ending pages of the episode; episode 4 additionally shows PCX DAT 72 with music entry 0x262 (not looped) until a key.  After the pages: `1BAE` and, when it qualifies, `1C9B`.  Quitting a level with ESC also runs the high-score check.
* **Saved games** (`2B8B` restore, `25CC` save): 9 slots in HOCUS.SAV: +0x1E episode word (-1 = empty), +0x30 level, +0x42 difficulty, +0x54 name char[26] ("<empty>"), +0x148 score u32 (the score at the start of the level, DS:704E).  Slot list: title "Select RESTORE slot" / "Select SAVE slot" at y 0x3B, rows from 0x4F step 10: number (style 4) at x 0x23, name (style 3) at x 0x2D, wand marker at byte column 4, Up/Down/1..9 move, Enter selects (restore ignores empty slots), Esc leaves.  Restore loads episode / level / difficulty / score and starts that level.  F2 = save overlay (3048), F3 = restore overlay (30DE, loaded game restarts the level), F10 = quit confirmation `2EE1` ("Reminder: Save your game before quitting" / "Quit game?", Y / N / Esc), V = volume overlay (1744), F1 = help (44CC(1)).  Overlays fade out, save the palette, show the menu palettes, run, restore palette, `copy_page(0, 1)`, show the saved page.
* **Volume** `137D`: two sliders of 16 boxes (box = 16 x 13 latch copy from page 3, byte column 0x3C + 4 * state, row 0xB9; state 1 = the cursor, blinking), "Sound FX Volume" at y 0x3E, "Music Volume" 0x28 lower; value = step * 16 + 15 stored in HOCUS.SAV +0x3D8 (effects, `156D:02A3`) / +0x3DA (music master, `15D4:015E`, defaults 255 / 192); a random effect plays every ~50 iterations; Enter / Esc saves.
* **Define keys** `17DA` / `10AC`: eight actions A..H (Left, Right, Jump, Fire, Up, Down, Scroll up, Scroll down), current key name right-aligned at x 0xFA; choosing one lists the 18 keys A..R in three columns of six (x 0x1B / 0x7D / 0xC8) and stores the letter index as the binding (HOCUS.SAV +0x16..0x1D; default 0F 10 02 03 0D 0E 0B 0C).
* **Start-up** `0B97:0001`: PCX DAT 17 (piracy notice) for 15 s, PCX DAT 1 with music 0x257 for 9 s, then music 0x25A and PCX DAT 2 + IMG 3 at (0, 0xBC) (registered) with the digitised sound 0x263 for 4 s; any key skips a screen (`1392:0108(ms)`).  **Attract** `42CB`: after 4500 ticks (140 Hz) idle in the main menu show PCX DAT 0x17 and 0x18 (2.5 s each), redraw the menu; a second idle period returns 0x9D = demo `0548:0570`: episode 0, level (rand % 5) * 2, difficulty 1, `level_run(2, 0)` (demo data DAT 0x12 + level / 2).
* **Sparkles** `06B8:0103` (init `039C`), called once per menu iteration: 75 points; table of 360 {x, y} = DAT 5 floats * 1e6 (truncated to long); position = (radius * tab / 1e6) + (160, 112) (signed 32-bit multiply / truncating divide); each step erases the old pixel when its colour is >= 0xF0, radius += speed (1..5), colour += step (1..5) capped at 0x7F, respawns (random angle 0..359, radius 0..159, until on screen; speed / step 1..5, colour 0) when outside the screen, draws colour 0xF0 + (colour >> 3) when the pixel is black; then waits for the retrace.
* Test options of `main_sdl.c`: `--keys "..."` (U D E X = up, down, enter, esc; lower-case = typed text; `<` backspace, `+` `-` right / left; other upper-case letters = menu keys, one every 30 iterations), `--frames N --dump f.ppm`, `--esc-at N [--fkey 1|2|10|22]`, `--skip-intro`, `--test-screen endN|hs|demo|save|restore`.

## 45. Startup DAT check and game controller support

**DAT check** (`dat_check()` in dat.c, called right after `dat_open`): the FAT is compiled in from the Registered
v1.1 EXE, so the file must be at least as long as the furthest entry end (the registered HOCUS.DAT is 6,101,525
bytes), and entry 0x11 (the first start-up picture) must start with a PCX header (0x0A, version <= 5).  A shareware HOCUS.DAT
fails the size test; before this check it was read at wrong offsets and crashed after the Apogee logo.
Startup errors go to stderr *and* an SDL message box (`fatal()` in main_sdl.c), since a `-mwindows` build has
no console.

**Controller** (main_sdl.c, SDL GameController API, `SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS=0` so A is
always the bottom face button).  The first connected pad is used; hot-plug is handled (removal releases everything).
Pad inputs drive *virtual buttons*; a virtual button acts like a key: on press it bumps the key-press count
(`s_keys`, for "press any key" screens), queues a menu key, and sends the DOS make code of the key currently
**bound** to its action (so Define Keys still applies); on release the break code.  Several pad inputs can hold
one virtual button (bitmask per button), so e.g. D-pad + stick don't fight.

| Pad | Game | Menus |
|---|---|---|
| D-pad / left stick | move; up = doors/switches/wizards; down | navigate; left/right = volume sliders |
| A, LT | jump (bound Jump key, default Ctrl) | select (Enter) |
| X, RT | fire (bound Fire key, default Alt) | – |
| B | fire | back (Esc) |
| LB / RB, right stick up/down | look up / down (scroll keys) | page up / down |
| Start | Esc: opens the in-game menu; again closes it | Esc |
| Back / View | pause (P) | – |

Stick dead zone: engage at 50% deflection, release under 35% (hysteresis); triggers engage at 50%, release at 25%.
Menu keys queued during play are discarded every gameplay frame (`menu_flush()` in `frame_hook`), so A/B
pressed in play can't auto-select in the next menu (this also applies to the keyboard).
Not covered by the pad: typing a high-score name (use a keyboard, or the Steam Deck on-screen keyboard:
Steam + X), the M/S/V/F-key hotkeys (all reachable from the Start menu), and Define Keys.

Test option `--pad STR`: attaches an SDL virtual controller and presses one input per 30 ticks: `U D L R` D-pad,
`A B X Y`, `S` Start, `V` Back, `l r` shoulders (each released on the next step); `>`/`<` toggle holding the left
stick right/left, `j` toggles the right trigger, `.` waits.  E.g. `--episode 0 --level 0 --pad ">..A" --frames 133
--dump f.ppm` (walk right and jump), `--skip-intro --pad ".DDDA"` (Instructions).

## 46. Enhanced display smoothing ("Smooth: display") - `src/interp.[ch]`

The logic keeps its tick (17.5 / 20 / 23.3 Hz); the picture between ticks is redrawn at the display rate with
objects and camera blended between the previous and the latest tick (the Techbot approach).  Toggle: **F11** or
the **right stick click (R3)**; `--smooth` starts with it on; a "SMOOTH: DISPLAY / OFF" notice shows for 1.2 s.
Off = Original: nothing shown changes (the hooks only record).

**Recording** (every tick, whether smoothing is on or not, so toggling works at once):
* `LoopHooks` gained `level_start` (after each setup), `tick_begin` (after `camera_update` / `level_player_tiles`,
  before the world draw), `tick_nosmooth` (the "press a key" picture is drawn into the frame), `tick_shown` (loop
  head, the finished frame was just shown, before the tick wait) and `tick_waited`.
* `tick_begin` stores the camera (`cam_x`, `cam_row`, `cam_col`, `col_odd`), the tick's scheduled clock, and a
  25 x 15 snapshot of the visible map cells (2 cells of margin): the tile pass (`map_animate`) runs *after* the draw
  in the same tick, so the live map is already a tick ahead when the picture is shown.
* `sprite.c`: `g_rec_blit` is called by `blit()` (all six blitters) with its arguments; `g_rec_pixel` by the spark
  bursts (tag per particle) and the falling sparkles (untagged: their plane is random each tick); `g_rec_rect` by the
  boss bar (screen-fixed, copied from the VGA page).  `g_rec_tag` (TAG_* in sprite.h) is set around each draw:
  player (0x10, two blits when y < 0 matched by occurrence), teleport (0x11), shot icons over the player (0x20+i),
  enemies 0x100+slot, player shots 0x200+slot, enemy projectiles 0x300+i, score tags 0x400+i, puffs 0x500+i.
* A frame with the screen flash (`g_lv.flash`) or the pause picture is marked "not smoothable".

**Rendering** (`interp_render`, called by `present()`; only during a level's tick wait - menus, hint boxes, status
messages, fades and end screens all show the VGA page):
* alpha = (clock - scheduled clock of the latest tick) / interval, clamped 0..1; the previous tick is used only if it
  was smoothable and the camera moved <= 32 px (otherwise alpha = 1: snaps on teleports / level start).
* Cameras are interpolated separately: sprites are positioned relative to `cam_x * 8` px, the map by
  `cam_col * 16 + col_odd * 8` (the original picks the half-cell routine by the *player's* X parity), both by
  `cam_row * 16` vertically; the backdrop origin is `cam_x * 4` px (half speed), rows fixed.  Each camera is rounded
  to whole pixels before objects are placed, so a world-fixed object stays locked to the map.
* Map + backdrop: every play-area pixel is the cell's tile pixel (layer1, else layer2) or, for a cell empty in both,
  the backdrop pixel ((origin + X) mod 320, same row) - exactly what `map_draw_*` + `backdrop_draw_*` produce.
* Sprites: the recorded blit is replayed by a copy of `blit()`'s opcode walk that writes into a linear buffer at a
  pixel offset with its own clipping to the play area (so a sprite clipped at the screen edge on its tick is
  complete when blended inwards).  Position = world position (screen + camera of its own tick) blended with the
  same tag / occurrence in the previous tick (same kind and sheet, <= 48 px apart, else no blend), minus the
  rounded camera.  Draw order and appearance (frame, flash) are the latest tick's.  Untagged draws are world-fixed.
* Rows 160..199 (status panel) come from the VGA page.

**Timing:** with smoothing on, `timer_frame_wait` advances its schedule by exactly one interval (resync if more than
one behind), so a present blocking on vsync cannot slow the game; `idle()` presents every call (vsync paces it; at
most ~250 per second without vsync).  Measured: 173 ticks in 10 s at Slow (expected 17.5 / s).  The picture is up to
one tick behind (57 / 50 / 43 ms).

**Tile snapshot:** each frame also keeps a copy of the packed tile graphics (page 2 up to `page_off`).  When the
player falls below the screen, the original's unclipped player blit runs past the end of page 1 into page 2 and
damages the tile (and later sprite) graphics - an original glitch, faithfully reproduced; on DOS the damage shows from
the next frame, so the renderer must not read the live tiles.

**Verification:** `--smooth-check` renders the smooth picture at alpha 1 after every tick and compares the play area
with the VGA page (the first difference is saved as `smooth_bad.ppm` / `vga_bad.ppm`): 0 differences over 1,782
ticks on E1 levels 1, 2, 5 and on E1L9, E2L9, E3L6, E3L9, E4L3, E4L9 (bosses included).  E2L4 (falling into the lava
off the bottom of the screen) differs on 4 of 1,498 ticks: by then the glitch above has also damaged sprite graphics,
which in the original a sprite drawn later in the same frame already picks up; harmless (the screen is full of the
glitch's garbage at that point).
`--dump-alpha A --frames N --dump f.ppm` writes the smooth picture at alpha A after the Nth tick (used to inspect
the in-between frames: camera, backdrop, shots, jumps).  The differential tests (`tests/diff_run.py all`) still pass.

## 47. Run-ahead, whole-state save / restore, tap latching

**Why:** the original draws Hocus *before* reading the controls in each tick (camera, world, player draw, enemies,
effects, `frame_controls`, shots, `player_move`), so a key press shows up in the picture after next: 1-2 ticks
on DOS.  Blending the previous tick into the latest (section 46) added a full tick on top.

**Run-ahead** (`interp.c`, setting 0 / 1 / 2, default 1; **F12** or **left stick click (L3)** cycles it - pressing
it with smoothing off turns smoothing on; `--runahead N`; the notice shows "SMOOTH: DISPLAY  RUN-AHEAD n"):
* At `tick_shown` (loop head, page flipped, before the wait) the whole game state is saved (`state_save`), the
  next tick (or two) is run by `level_tick_ahead()` in loop.c - the frame body of `level_run` without the joystick
  poll, pause picture, menus, hot keys and exit checks - recorded into `ahead[0]` / `ahead[1]` with its finished
  page image, and everything is restored.  Sounds (`play_sfx`) and hint boxes do nothing during it
  (`interp_speculating()`).  A second tick isn't run if the first ends the level (death / finish timer at 1); for it
  the pages are flipped as the real loop does.
* Blend pairs: 0 = previous -> latest (a tick behind); 1 = latest -> `ahead[0]`; 2 = `ahead[0]` -> `ahead[1]`.
  Without the needed ahead frames it holds the latest tick.
* Run-ahead 1 is exact: the next tick's picture is already decided when the tick ends (the controls it reads come
  after its drawing; only the look keys, read by the camera before drawing, could differ).  Measured: 0 real ticks
  differing from their prediction in 1,785 (E1L1, E2L4; the 4 off-screen glitch ticks of section 46 excepted).
* Run-ahead 2 depends on the controls held during the next tick.  When a game key changes during the tick wait,
  the run-ahead is redone at once (`interp_input_changed()` from `game_key`), so the picture starts reacting in the
  same wait as the press.  It can mispredict if the controls change again before the tick; the blend then bends.
* Timeline (trace, jump pressed during the wait after tick 61): tick 62 reads it and moves Hocus (drawn still at the
  old place); tick 63's picture shows the jump.  DOS / run-ahead 0: on screen from tick 63 (run-ahead 0 reaches it
  only at 64); run-ahead 1: blends towards it from tick 62; run-ahead 2: from the press itself.

**State** (`state.[ch]`): every writable variable of the game modules, listed from `nm` on the objects (globals by
name; private ones through `vga_state`, `video_state`, `rng_state`, `loader_state`): about 420 KB including the
256 KB of VGA memory, copied twice per tick.  Not saved: hook pointers, the DAT handle, the timer, load-time
constants, the platform layer.  **Add any new game global / static to `state.c`.**  It is also the base for
quicksave.

**Tap latching** (`game_key()` in main_sdl.c, with smoothing on): a key pressed and released within one tick wait
was never seen by the original (it reads key states once per tick).  Its release is now held back until the next tick
has run (`tap_flush()` at `tick_shown`, and whenever no level is waiting).  Keyboard and controller both.

**Tests:**
* `--frame-hash FILE`: a hash of every shown page + palette.  Same scripted input with run-ahead 0, 1, 2 (and with
  re-prediction on input changes and taps): identical over ~1,900 ticks on E1L1, E2L4, E3L9, E4L3 - nothing the
  run-ahead touches leaks into the real game.
* `--smooth-check` now also prints how many real ticks differed from what was run ahead for them.
* `--pad` gained `t`: tap A (down and up before the next tick).  Without smoothing the tap is lost (as on DOS); with
  it Hocus jumps.
* `--dump-alpha` now captures in the tick wait after the input has been read (closer to real play).
* `tests/diff_run.py all` (Original mode vs the DOS game): still 72 runs, 0 mismatches.

## 48. Enhanced views: 16:9 wide and HUD off (groundwork)

**Keys:** **F7** wide on / off, **F8** HUD on / off, **left stick click (L3)** cycles classic -> wide -> wide + HUD
off -> HUD off -> classic (run-ahead cycling moved to F12 only).  `--wide`, `--hud-off`.  F7 / F8 / F11 / F12
never reach the game (so they can't disturb the cheat-code key sums).  Notice: "VIEW: WIDE 16:9  HUD: OFF".

**Geometry:** wide = **426 x 200** (16:9 with the original's 1.2:1 pixels: 200 x 1.2 x 16 / 9), shown at
426 x 240 like the 320 x 200 picture is shown at 320 x 240.  The texture is recreated on a width change and a
windowed window keeps its height.  HUD off = the play area fills all 200 rows.  The view is centred on the
original screen (wide: +53 px a side; HUD off: +20 px above and below) and kept inside the 3840 x 960 map, so at
a map edge it extends to one side only.  Classic + HUD + smoothing off = the VGA page, exactly as before.

**Rendering** (interp.c `render_pair(out, W, PH, ...)`): the redraw of section 46 at any width / play height.
Map cells come from a larger per-tick snapshot (7 cells left / right, 5 above / below).  The backdrop repeats every
320 px horizontally (as the original's wrap does); it is fixed to the screen, so with HUD off its 160 rows are
stretched to 200 (nearest).  Wide without smoothing shows the latest tick redrawn (no blending).  The status panel
(rows 160..199 of the page) is centred with black sides; the boss bar keeps its place at the play area's foot,
centred.  `level_flash` frames are now redrawn as one colour with the sprites on top.
**When the VGA page must be shown** in a wide view (`interp_compose_vga`): centred; for overlays on the play area
(hint boxes, status messages, pause - anything not through the front end's pace hook) the sides keep the last level
picture, converted with the current palette so fades apply; for menus and front-end screens (`s_menu_screen`, set by
`front_pace`, cleared at the next level tick) and outside a level (new `level_end` hook: `level_run` is now a
wrapper around `level_run_body`) they are black.

**Gameplay bounds** (`g_view_ext` in camera.[ch]; set each tick in `tick_begin` from the camera and the view;
all zero in the classic view, so the original is unchanged):
* player shots are freed at the view's edge instead of the original screen's (`frame.c`: `x > 0x4F + right`,
  `x < -3 + left`, `y < top`);
* enemy projectiles likewise (`player.c`: `-10 + left`, `0x50 + right`, `-0x14 + top`, `0xA0 + bottom`).
  So shots reach further and enemies in the extra view can hit you with projectiles - a deliberate Enhanced change;
  without it shots and projectiles would visibly vanish 53 px short of the edge.
* spark bursts are recorded across the view (VGA writes stay inside the original screen); falling sparkles outside
  the original screen are drawn with their own pattern, never the game's random numbers, so randomness is unchanged.
* Enemies needed nothing: they stay alive up to ~200 px (left) / 160 px (right) beyond the original screen, are drawn
  while alive, and appear through trigger tiles, not at screen edges.

**Checks:** classic `--smooth-check`: 598 ticks, 0 differences, 0 mispredictions.  Wide + HUD off: run-ahead 1
identical to run-ahead 0 by `--frame-hash` (E1L2, E2L4).  Shots fly on past the original edge to the wide edge
(E1L2).  `tests/diff_run.py all`: 72 runs, 0 mismatches.  `--dump` and `--dump-alpha` write the picture as shown
(426 wide in the wide view).

**Fixes after the first test (2026-11-22):**
* *Tile animations and switched tiles froze beyond the 4:3 area.*  The original's tile pass (`map_animate`) only
  covers the 21 x 10 cells of its own screen: animated background tiles advance and pending switch changes
  (`g_map.dyn`, set for a whole rectangle by `open_rect`) are applied only there - elsewhere they wait until scrolled
  in.  With a non-zero `g_view_ext` it now covers the whole view plus one cell (for the smoothing), in row order;
  cells inside the original screen still take the game's random numbers in the original order, the others use
  `g_map_view_rng` (saved with the state, so run-ahead stays exact).  Applied switch tiles change collision, so the
  extra area behaves sooner than on DOS - only where you can already see it.  Checked with a C test that sets up
  every level and runs the pass with a wide + HUD-off extent: 22 cells beyond the original screen change on 7
  levels' opening views (0 before).
* *Demos:* `g_view_ext` stays zero while a demo plays (`g_demo_playback`): demos replay recorded input and must follow
  the original rules exactly; only their picture is wide.
* *Talking to a wizard switched back to the 4:3 HUD view.*  Overlays drawn on the shown page (hint boxes, status
  messages) are now found by comparing the page with that tick's picture (`cur->img`); only the changed pixels are
  drawn onto the last level picture, placed as on the original screen (centred like the view: +ox, and + (PH-160)/2
  for play-area rows).  More than 60 % changed counts as a full-screen display (centred, black sides).  This also
  covers HUD off in the 320-wide view.
* Test option `--dump-poll K` (with `--dump f.ppm`): dump on the K-th poll of a modal wait (hint box, message).

**Fixes, round 2 (2026-11-22):**
* *Lift platforms snapped while the player glided.*  A lift is two foreground tiles (`g_mv.plat_tile_b` left,
  `plat_tile_a` right) that `player_move` moves a whole cell (16 px) per tick together with the player.  The snapshot
  now lists lift tiles (`Frame.lift[]`) and stores what lies behind them in their cells (layer 2 or backdrop); the
  renderer draws each lift tile after the world, matched with the previous tick's (same tile and column, same or
  neighbouring row) and blended like the sprites, so platform and player move together.  Checked on E3L3 riding a
  lift up: `--smooth-check` 0 differences / 0 mispredictions; in-between frames keep Hocus standing on it.
* *Menu starfield in the wide view.*  `frontend.c`'s stars (C `rand()`, not the game's random numbers) now fly to the
  wide edges (`g_front_wide_extra`, set from the view); points beyond the VGA page's 320 columns go into
  `g_front_side[200][432]`, which `compose()` draws at the sides of front-end screens with the current palette (so
  fades apply).  Every front-end fade-out (`menu_fade_out`) clears it afterwards; it is also ignored when the starfield
  hasn't moved for a second.  A view change clears it.

**Open (for later):** wider HUD art; HUD Off needs some status display (health / score) eventually; a wider view can
show parts of levels the designers never expected to be seen together (no problem spotted on the levels looked at).

## 49. Options menu for the port's features; HOCUS.CFG; Controller Remap

**Change game options** (menu 2, also reached from the in-game menu) now has nine entries (tildes add the group gaps):
Sound is now on/off, Music is now on/off, Volume control~ / HUD style: Classic|Off, Aspect ratio: 4:3|16:9,
Framerate: <value>~ / Game playing speed, Keyboard remap (the old "Define key controls" screen), Controller remap
(replaces "Joystick is now on/off"; the DOS joystick code was never ported and HOCUS.SAV keeps writing it as off).
First letters stay unique for the letter jump.  The three new entries open lists styled like Game Speed (menu ids 8
`MENU_HUD`, 9 `MENU_ASPECT`, 10 `MENU_FRAMERATE`; `choose()` opens each on the current value - Game Speed now does
too).  A choice applies at once (`g_front.port_apply`) and saves HOCUS.CFG.

**Framerate:** Original = no smoothing (the picture changes at the tick rate, as on DOS; wide / HUD off still work);
30 / 60 / 120 fps = smoothing with a frame cap in `idle()`; Display = smoothing at the refresh rate (vsync).  A cap
above the refresh rate behaves like Display.  Run-ahead is 1 whenever smoothing is on (no option; `--runahead N` for
tests); tap latching is on with smoothing.

**HOCUS.CFG** (`config.[ch]`, next to HOCUS.SAV, which keeps its 990-byte DOS layout): plain text, `key=value`:
`hud=classic|off`, `aspect=4:3|16:9`, `framerate=original|30|60|120|display`, `pad_jump`, `pad_fire`, `pad_look_up`,
`pad_look_down`, `pad_pause` = A, B, X, Y, View, L-Stick, R-Stick, LB, RB, Share, P1..P4 (Steam Deck back buttons),
Touchpad, LT, RT.  Unknown lines are ignored, bad values keep the default (D-pad, Menu and Guide are refused).
Defaults: Classic HUD, 4:3, Display, Jump A, Fire X, Look up LB, Look down RB, Pause View.  `--wide`, `--hud-off`,
`--smooth` override it for one run (not saved).  Test runs (`--dump`, `--keys`, `--pad`, `--smooth-check`,
`--frame-hash`) never read or write it and start with framerate Original.

**Shortcuts** (kept until the menus are proven): F7 aspect, F8 HUD, F11 Original <-> the last smooth framerate; all three
change the same settings and save them; the notice shows "ASPECT: .. HUD: .. FRAMERATE: ..".  F12 (run-ahead) and the
stick clicks (L3 view, R3 smoothing) are gone - the stick clicks can now be assigned in Controller Remap.

**Controller Remap** (frontend.c `pad_remap` / `pad_prompt`): the Define Keys screens' look, but driven by the menu
marker so a controller can work it: a list "Jump - A" .. "Pause - View", "Reset to defaults" (menu 11 `MENU_PAD`);
choosing an action shows "Press a button for: Jump" / "Currently A" and takes the next controller press
(`g_front.pad_button`, raw presses queued by main_sdl.c).  Menu (Start) or Esc cancels; D-pad / Guide are ignored.
A button does one thing: taking one from another action gives that action the old button (swap).
**Controller layer** (main_sdl.c): game actions jump / fire / look up / look down / pause follow `g_port.pad`; B (fire),
LT (jump), RT (fire) work as extras while not assigned to anything; fixed: D-pad / left stick move, right stick looks,
Menu = Esc.  Menu keys are fixed per physical button (D-pad / stick navigate, A Enter, B and Menu Esc, LB / RB page
up / down), so menus stay usable whatever the remap.  Bindings changed: everything held is released.

**Checks:** menus rendered (options, framerate list, remap list); 16:9 chosen through the menus switches the menu to
426 wide at once; Jump remapped to Y with the virtual pad shows "Jump - Y"; in play (`--pad-map jump=Y`, test option):
A jumps by default, Y doesn't; after the remap Y jumps and A doesn't; B fires as an extra and jumps when assigned to Jump.
HOCUS.CFG round trip identical; bad values refused.  `--smooth-check` 598 ticks / 0 differences / 0 mispredictions;
run-ahead 0 vs 1 identical (E2L4, wide).

## 50. Playtest fixes (2026-11-22, evening)

* **Starfields in the wide view, everywhere.**  Two implementations exist: the front end's (`frontend.c` sparkles:
  menus, key waits, save / restore lists; C `rand()`) and `stars.c` (the end-of-level screen via `ui_end_screen`; uses
  the game's random numbers).  The side layer moved to `stars.c` (`g_star_side[200][432]`, `g_star_wide_extra`,
  `g_star_side_steps`, `stars_side_clear()`), shared by both.  `stars.c`'s own stars are left exactly as the original
  (same random numbers); a star leaving the 320 columns sideways is copied into a cosmetic "ghost" (up to 64) that
  keeps flying through the side area to the wide edge.  The platform layer draws the side layer on every full-screen
  display (`interp_compose_vga` returns 2) while a starfield has stepped within the last second.
* **Backdrop at level edges.**  In the wide view the view stops at a map edge before the original camera does
  (+53 px), but the backdrop origin followed the original camera.  It now follows the view: origin =
  `cam_x * 4 + floor((sxo - ox) / 2)` - identical to before away from edges and in the classic view (ox = sxo = 0).
  Measured at E1L1's start: the classic camera starts at ~tick 42, the wide view at tick 50; the backdrop now starts
  with the view.
  *Follow-up (2026-11-23):* with smoothing on, the backdrop flickered by a pixel in that stretch, faster at higher
  framerates: the camera term and the edge correction were each rounded to whole pixels, and between ticks their
  roundings disagreed.  The origin is now computed from the view's left edge with one rounding:
  `round((clamp(cam_x*8 blended - ox, 0, mapW - W) - ox) / 2)` (classic: = round(cam_x*4 blended), as before).
  Checked: 35 captures over ticks 43..49 at 5 points between ticks - backdrop identical in all; afterwards it moves
  monotonically; `--smooth-check` 598 ticks, 0 differences.
* **Overlays at the start of a level (and generally).**  `interp_compose_vga` picks the frame drawn on the *shown*
  page (each frame records `page`): the latest finished tick, or the tick being drawn when its page is already up -
  E1L1's start-of-level picture (IMG 16, drawn into the frame being built, before any tick has finished) and anything
  hot keys draw after a tick.  For the frame being drawn, the reference (what the page shows without the overlay) is
  its classic redraw.  The front-end flag that forced black sides for everything reached through the front end is
  gone: during a level the 60 % rule alone decides (the F-key screens - help, save, quit, volume - are full screens in
  Hocus, so they stay centred with black sides).  Every level was started in wide + HUD off without input: none opens a
  box by itself besides E1L1's picture.
* **"Quit - return to DOS"** is now **"Exit the game"** (the letter jump stays unique: E).
* `--dump-poll` also works in the start-of-level key wait.
