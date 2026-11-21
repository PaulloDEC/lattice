#!/usr/bin/env python3
"""
hocus_extract.py - dump all Hocus Pocus (Registered v1.1) assets.

usage: python hocus_extract.py HOCUS.EXE HOCUS.DAT outdir
"""
import sys, os, json
import numpy as np
from PIL import Image
from hocus_dat import *


def save_idx(path, arr, pal, transparent0=False):
    p = pal.copy().astype(np.uint8)
    im = Image.fromarray(arr.astype(np.uint8), 'P')
    im.putpalette(p.reshape(-1).tolist())
    if transparent0:
        im.info['transparency'] = 0
    im.save(path)


def main(exe, dat, out):
    H = HocusData(exe, dat)
    T = H.level_tables()
    gp = H.game_palette()
    for d in ('raw', 'pcx', 'img', 'tilesets', 'backdrops', 'sprites', 'maps', 'text', 'audio'):
        os.makedirs(os.path.join(out, d), exist_ok=True)

    # ---- raw dump of every FAT entry (always) --------------------------------
    manifest = []
    for i, (o, n) in enumerate(H.fat):
        b = H.get(i)
        kind = ('pcx' if b[:4] == b'\x0a\x05\x01\x08' else 'voc' if b[:4] == b'Crea' else
                'midi' if b[:4] == b'MThd' else 'img' if H.is_img(i) else
                'sprites' if i == IDX_SPRITES else 'text' if i in IDX_TEXT else
                'map' if IDX_MAP_BASE <= i < IDX_MAP_BASE + 468 else 'other')
        manifest.append(dict(index=i, offset=o, size=n, kind=kind))
        open(os.path.join(out, 'raw', f'{i:03d}.bin'), 'wb').write(b)
    json.dump(manifest, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)

    # ---- PCX screens, backdrops, tilesets ------------------------------------
    for i, m in enumerate(manifest):
        if m['kind'] == 'pcx':
            px, pal = H.pcx_pixels(i)
            save_idx(os.path.join(out, 'pcx', f'{i:03d}.png'), px, pal if pal is not None else gp)
    for n in range(16):
        px, _ = H.pcx_pixels(IDX_BACKDROP[0] + n)
        save_idx(os.path.join(out, 'backdrops', f'backdrop_{n:02d}.png'), px, H.game_palette(n))
        px, _ = H.pcx_pixels(IDX_TILESET[0] + n)
        save_idx(os.path.join(out, 'tilesets', f'tileset_{n:02d}.png'), px, H.game_palette(n))

    # ---- planar IMG files ----------------------------------------------------
    for i, m in enumerate(manifest):
        if m['kind'] == 'img':
            save_idx(os.path.join(out, 'img', f'{i:03d}.png'), H.decode_img(i), gp, True)

    # ---- sprites ---------------------------------------------------------------
    meta = []
    for sp in H.sprites():
        d = os.path.join(out, 'sprites', f"{sp['index']:02d}_{sp['name'].replace(' ', '_')}")
        os.makedirs(d, exist_ok=True)
        n = H.sprite_frame_count(sp)
        for west in (False, True):
            for k in range(n):
                save_idx(os.path.join(d, f"{'W' if west else 'E'}{k:02d}.png"),
                         H.decode_sprite_frame(sp, k, west), gp, True)
        info = {k: v for k, v in sp.items() if k not in ('lE', 'lW', 'pE', 'pW')}
        info['frames'] = n
        meta.append(info)
    json.dump(meta, open(os.path.join(out, 'sprites', 'sprites.json'), 'w'), indent=1)

    # ---- text pages ---------------------------------------------------------------
    for i in IDX_TEXT:
        pg = H.text_page(i)
        json.dump(pg, open(os.path.join(out, 'text', f'page_{i:03d}.json'), 'w'), indent=1)

    # ---- maps ---------------------------------------------------------------------
    for lv in range(36):
        m = H.decode_map(lv)
        d = os.path.join(out, 'maps', f'level_{lv:02d}')
        os.makedirs(d, exist_ok=True)
        for k, name in enumerate(('000', '001', '002', '003', '004', '005', '006',
                                  '007', '008', '009', '010', '011', '012')):
            open(os.path.join(d, f'{lv:02d}.{name}'), 'wb').write(H.map_files(lv)[k])
        json.dump(dict(level=lv, episode=lv // 9, level_in_episode=lv % 9,
                       start=dict(x=m['start'][1], y=m['start'][2]), shoot_delay=m['start'][3],
                       par_time=T['par_time'][lv], music=T['music'][lv],
                       tileset=T['tileset'][(lv // 9) * 10 + lv % 9],
                       backdrop=T['backdrop'][(lv // 9) * 10 + lv % 9]),
                  open(os.path.join(d, 'level.json'), 'w'), indent=1)

    # ---- audio --------------------------------------------------------------------
    for k, i in enumerate(IDX_MIDI):
        open(os.path.join(out, 'audio', f'music_{k:02d}.mid'), 'wb').write(H.get(i))
    for i in IDX_VOC_PC:
        b = H.get(i)
        ext = 'voc' if b[:4] == b'Crea' else 'pcspk'
        open(os.path.join(out, 'audio', f'sfx_{i:03d}.{ext}'), 'wb').write(b)

    json.dump(T, open(os.path.join(out, 'exe_tables.json'), 'w'))
    print(f"extracted {len(manifest)} entries to {out}")


if __name__ == '__main__':
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
