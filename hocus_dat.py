#!/usr/bin/env python3
"""
hocus_dat.py - read Hocus Pocus (Apogee/Moonlite, Registered v1.1) resources.

HOCUS.DAT has no directory of its own. The (offset,size) file-allocation table
lives in HOCUS.EXE's data segment: DGROUP = 0x1D51 paragraphs, table at DS:0x0094
(file offset 0x1F1A4 in the v1.1 registered EXE), 8 bytes per entry, 652 entries.
Entries are contiguous and cover the whole DAT.

Formats follow the ModdingWiki pages (K1n9_Duk3, Malvineous, MainMemory, Hisymak)
and were cross-checked against this EXE's tables.
"""
import struct, os
import numpy as np

FAT_FILE_OFFSET = 0x1F1A4     # registered v1.1
FAT_COUNT = 652

# ---- resource map (entry indices, v1.1 registered) -------------------------
IDX_FONT       = 0
IDX_PAL_LO     = 7            # 128 colours, 6-bit VGA
IDX_PAL_HI     = 8            # next 128 colours
IDX_TEXT       = range(41, 72)      # 31 story/help pages, 1686 bytes each
IDX_BACKDROP_PAL = range(73, 89)    # 16 x 128-colour upper palettes (384 B)
IDX_BACKDROP   = range(89, 105)     # 16 PCX 320x200
IDX_TILESET    = range(105, 121)    # 16 PCX 320x200, 20x12 tiles of 16x16
IDX_SPRITES    = 130
IDX_MAP_BASE   = 131          # 13 file types x 36 levels, TYPE-MAJOR: entry = base + 36*type + level
IDX_MIDI       = range(599, 611)
IDX_VOC_PC     = range(611, 652)    # VOC / PC-speaker pairs

MAP_W, MAP_H = 240, 60


class HocusData:
    def __init__(self, exe_path, dat_path):
        exe = open(exe_path, 'rb').read()
        self.dat = open(dat_path, 'rb').read()
        self.fat = [struct.unpack_from('<II', exe, FAT_FILE_OFFSET + 8 * i)
                    for i in range(FAT_COUNT)]
        end = self.fat[-1][0] + self.fat[-1][1]
        if end != len(self.dat):
            raise ValueError("FAT does not cover HOCUS.DAT (wrong EXE version?) "
                             f"{end:#x} != {len(self.dat):#x}")
        self.exe = exe

    def get(self, i):
        o, n = self.fat[i]
        return self.dat[o:o + n]

    # ---- palettes ---------------------------------------------------------
    @staticmethod
    def vga6_to_rgb(b):
        a = np.frombuffer(b, np.uint8).reshape(-1, 3).astype(np.uint16) & 0x3F
        return ((a << 2) | (a >> 4)).astype(np.uint8)

    def game_palette(self, backdrop=None):
        """256x3 palette. Lower 128 = game, upper 128 = game default or the
        backdrop's own upper half."""
        pal = self.vga6_to_rgb(self.get(IDX_PAL_LO) + self.get(IDX_PAL_HI)).copy()
        if backdrop is not None:
            pal[128:] = self.vga6_to_rgb(self.get(IDX_BACKDROP_PAL[0] + backdrop))
        return pal

    # ---- 4-plane (Mode X style) image ------------------------------------
    def decode_img(self, i):
        b = self.get(i)
        w4, h = struct.unpack_from('<HH', b, 0)
        if w4 * h * 4 + 4 != len(b):
            raise ValueError(f"entry {i} is not an IMG ({w4}x{h})")
        planes = np.frombuffer(b, np.uint8, offset=4).reshape(4, h, w4)
        img = np.zeros((h, w4 * 4), np.uint8)
        for p in range(4):
            img[:, p::4] = planes[p]
        return img

    def is_img(self, i):
        b = self.get(i)
        if len(b) < 4:
            return False
        w4, h = struct.unpack_from('<HH', b, 0)
        return w4 > 0 and h > 0 and w4 * h * 4 + 4 == len(b)

    # ---- tileset (PCX 320x200 -> 240 tiles) -------------------------------
    def pcx_pixels(self, i):
        """Decode entry i as a 320x200 8bpp PCX; returns (pixels, embedded palette)."""
        b = self.get(i)
        assert b[0] == 0x0A and b[2] == 1 and b[3] == 8
        xmin, ymin, xmax, ymax = struct.unpack_from('<4H', b, 4)
        w, h = xmax - xmin + 1, ymax - ymin + 1
        bpl = struct.unpack_from('<H', b, 66)[0]
        out = bytearray(); p = 128
        need = bpl * h
        while len(out) < need:
            c = b[p]; p += 1
            if c >= 0xC0:
                n = c & 0x3F; out += bytes([b[p]]) * n; p += 1
            else:
                out.append(c)
        px = np.frombuffer(bytes(out[:need]), np.uint8).reshape(h, bpl)[:, :w]
        pal = None
        if b[-769] == 12:
            pal = np.frombuffer(b[-768:], np.uint8).reshape(256, 3)
        return px.copy(), pal

    def tileset(self, n):
        px, pal = self.pcx_pixels(IDX_TILESET[0] + n)
        tiles = []
        for ty in range(12):
            for tx in range(20):
                tiles.append(px[ty * 16:(ty + 1) * 16, tx * 16:(tx + 1) * 16])
        return tiles  # 240 tiles

    # ---- sprites ----------------------------------------------------------
    def sprites(self):
        b = self.get(IDX_SPRITES)
        first = struct.unpack_from('<I', b, 0)[0]
        n = first // 220
        out = []
        for s in range(n):
            o = 220 * s
            off, = struct.unpack_from('<I', b, o)
            name = b[o + 4:o + 26].split(b'\0')[0].decode('latin1')
            f = struct.unpack_from('<15H', b, o + 26)
            (w4, h, stand, stand2, walk1, walk2, jump, fall, sd1, sd2,
             pw4, ph, py, pframe, pf2) = f
            pix_off, pix_size = struct.unpack_from('<HH', b, o + 56)
            lE = struct.unpack_from('<20H', b, o + 60)
            lW = struct.unpack_from('<20H', b, o + 100)
            pE = struct.unpack_from('<20H', b, o + 140)
            pW = struct.unpack_from('<20H', b, o + 180)
            out.append(dict(index=s, name=name, offset=off, w=w4 * 4, h=h,
                            w4=w4, pix_off=pix_off, pix_size=pix_size,
                            frames_named=dict(stand=stand, walk=(walk1, walk2), jump=jump,
                                              fall=fall, shoot=(sd1, sd2)),
                            projectile=dict(w=pw4 * 4, h=ph, y=py, frame=pframe),
                            lE=lE, lW=lW, pE=pE, pW=pW))
        return out

    def sprite_frame_count(self, sp, _unused=0):
        """Frames = number of 0x03 terminators in the East layout block
        [lE[0], lW[0]) (the West block follows it)."""
        b = self.get(IDX_SPRITES)
        p = sp['offset'] + sp['lE'][0]
        end = sp['offset'] + sp['lW'][0]
        n = 0
        while p < end:
            op = b[p]
            if op == 0:   p += 2
            elif op == 1: p += 3
            elif op == 2: p += 1
            elif op == 3: p += 1; n += 1
            else: break
        return n

    def decode_sprite_frame(self, sp, frame, west=False):
        b = self.get(IDX_SPRITES)
        base = sp['offset']
        pix = b[base + sp['pix_off']: base + sp['pix_off'] + sp['pix_size']]
        lay = (sp['lW'] if west else sp['lE'])[frame]
        pstart = (sp['pW'] if west else sp['pE'])[frame] * 4
        h = sp['h']
        buf = np.zeros((h + 2, 320), np.uint8)       # 80 cells per row
        flat = buf.reshape(-1)
        p = base + lay
        ptr = 0
        while True:
            op = b[p]; p += 1
            if op == 0:
                p += 1          # transparency-type byte
                ptr = 0
            elif op == 1:
                ptr += struct.unpack_from('<H', b, p)[0]; p += 2
            elif op == 2:
                flat[ptr * 4:ptr * 4 + 4] = np.frombuffer(pix, np.uint8, 4, pstart)
                ptr += 1; pstart += 4
            elif op == 3:
                break
            else:
                raise ValueError(f"bad sprite op {op:#x} in {sp['name']} frame {frame}")
        return buf[:h, :sp['w']].copy()

    # ---- text pages -------------------------------------------------------
    def text_page(self, i):
        b = self.get(i)
        nlines, u1, u2 = struct.unpack_from('<3H', b, 0)
        lines = []
        for k in range(20):
            align, color = struct.unpack_from('<HH', b, 6 + 4 * k)
            txt = b[86 + 80 * k: 86 + 80 * (k + 1)].split(b'\0')[0].decode('latin1')
            lines.append(dict(align='center' if align == 0 else 'left',
                              color=color, text=txt))
        return dict(line_count=nlines, lines=lines[:nlines] if nlines <= 20 else lines)

    # ---- maps -------------------------------------------------------------
    def map_files(self, level):
        return [self.get(IDX_MAP_BASE + 36 * k + level) for k in range(13)]

    def decode_map(self, level):
        f = self.map_files(level)
        n = MAP_W * MAP_H
        info = np.frombuffer(f[12], '<u2').reshape(MAP_H, MAP_W)
        return dict(
            start=struct.unpack('<4H', f[0]),                    # null, x, y, shootDelay
            anim=f[1], messages=f[2], teleports=f[3], switches=f[4],
            toggle_a=f[5], toggle_b=f[6], enemy_defs=f[7], enemy_spawn=f[8],
            bg=np.frombuffer(f[9], np.uint8).reshape(MAP_H, MAP_W),
            fg=np.frombuffer(f[10], np.uint8).reshape(MAP_H, MAP_W),
            fg2=np.frombuffer(f[11], np.uint8).reshape(MAP_H, MAP_W),
            info=info)

    # ---- per-level EXE tables (v1.1 registered) -------------------------
    def level_tables(self):
        e = self.exe
        u16 = lambda o, n: list(struct.unpack_from('<%dH' % n, e, o))
        return dict(
            tileset=u16(0x21ADA, 40), level=u16(0x21B2A, 40), backdrop=u16(0x21B7A, 40),
            music=u16(0x21BDE, 36), par_time=u16(0x20A9A, 36),
            elevator=list(struct.unpack_from('<80h', e, 0x21C26)))
