#!/usr/bin/env python3
"""Render the first screen of a level using the C port only (video model, tile packing, camera, map draw).
usage: render_level.py <level 0..35> <out.png>"""
import sys, ctypes, numpy as np
import diff_effects as de
from diff_effects import lib, H
from diff_camera import cam
from PIL import Image
sys.path.insert(0, '/home/claude/hocus')
from hocus_dat import IDX_BACKDROP, IDX_TILESET

lib.video_load_pcx.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int]
lib.map_draw_even.argtypes = lib.map_draw_odd.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint16]
lib.vga_plane.restype = ctypes.POINTER(ctypes.c_uint8)
lib.backdrop_draw_even.argtypes = lib.backdrop_draw_odd.argtypes = [ctypes.c_uint16, ctypes.c_uint16, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint16, ctypes.c_int]
lib.video_load_img.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_int, ctypes.c_int]

def render(level):
    t = H.level_tables(); m = H.decode_map(level)
    bd, ts = t['backdrop'][level], t['tileset'][level]
    lib.vga_reset(); lib.video_init()
    lib.sprite_init()
    e = H.get(IDX_TILESET[0] + ts);  lib.video_set_draw_page(3); lib.video_load_pcx(e, len(e), 0)
    lib.sprite_pack_tiles()                       # tiles -> page 2
    e = H.get(IDX_BACKDROP[0] + bd)               # backdrop -> page 3, 25 rows down (original: 6460 += 0x7D0)
    lib.video_set_draw_page(3); lib.video_adjust_draw_offset(0x7D0); lib.video_load_pcx(e, len(e), 0); lib.video_adjust_draw_offset(-0x7D0)
    for page in (0, 1):                           # status panel strip (IMG 12 at y=160) on both display pages
        lib.video_set_draw_page(page); e = H.get(12); lib.video_load_img(e, len(e), 0, 160)
    # camera as at level start: player at the start tile, snap mode, vertical margin 5
    _, sx, sy, _ = m['start']
    cam.player_x = sx * 2; cam.player_y = sy * 16; cam.snap = 1; cam.vmargin = 5; cam.vfreeze = 0
    lib.camera_update()
    lib.video_set_draw_page(0); lib.video_set_latch_mode(1); lib.video_set_map_mask(0xF)
    fg, bg = m['fg'].tobytes(), m['bg'].tobytes()
    off = cam.cam_row * 240 + cam.cam_col
    (lib.map_draw_odd if cam.col_odd else lib.map_draw_even)(0, 2 * 0x3F00, fg, bg, off)
    (lib.backdrop_draw_odd if cam.col_odd else lib.backdrop_draw_even)(0, 3 * 0x3F00 + 0x7D0, fg, bg, off, cam.cam_x)
    lib.video_set_draw_page(0)
    for ent, x in ((11, 0), (14, 0x1C), (15, 0x3C)):   # HUD frames along the bottom edge (y=185)
        e = H.get(ent); lib.video_load_img(e, len(e), x, 185)
    planes = [np.ctypeslib.as_array(lib.vga_plane(p), shape=(0x10000,)) for p in range(4)]
    img = np.zeros((200, 320), np.uint8)
    for x in range(4): img[:, x::4] = planes[x][:200 * 80].reshape(200, 80)
    pal = H.game_palette(bd).astype(np.uint8)
    return Image.fromarray(pal[img]), (level, bd, ts, sx, sy, cam.cam_x, cam.cam_row, cam.col_odd)

if __name__ == '__main__':
    im, info = render(int(sys.argv[1])); im = im.resize((640, 400), Image.NEAREST); im.save(sys.argv[2]); print(info)
