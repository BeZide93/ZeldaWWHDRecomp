#!/usr/bin/env python3
"""Feasibility prototype (local only): build a layout archive in the game's UI format from our own art.

Writes StoryLyt_00.szs (Yaz0 SARC) with
  blyt/StoryLyt_00.bflyt            one null pane N_All_00 with one picture pane P_Plate_00
  timg/StoryPlate_00^l.bflim        our picture, RGBA8 (FLIM format 0x14), GX2 linear-aligned tiling
  anim/StoryLyt_00_InAll.bflan      a fade of N_All_00's alpha (0 -> 255 over 10 frames)
The layout follows the structure of the game's own layout files (section order, field sizes and the
flag values seen there); nothing is copied from them and no game data is part of the output.

usage: story_lyt.py PNG OUT.szs [--width 512]
"""
import argparse, os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from notebook_model import sarc, yaz0_stored  # noqa: E402

LAYOUT = "StoryLyt_00"
TEXTURE = "StoryPlate_00^l.bflim"  # "^l": the RGBA8 textures of the game's layouts
F = lambda v: struct.pack(">f", v)


def name(s, n):
    b = s.encode() + b"\0"
    assert len(b) <= n, s
    return b + b"\0" * (n - len(b))


def section(magic, body):
    body += b"\0" * (-len(body) % 4)
    return magic + struct.pack(">I", 8 + len(body)) + body


def pane_body(nm, flags, w, h, tx=0.0, ty=0.0, alpha=0xFF, extra=0x02):
    # flags, origin, alpha, part flags | name[24] | user data[8] | translate xyz | rotate xyz | scale xy | size wh
    return (bytes([flags, 0x00, alpha, extra]) + name(nm, 24) + b"\0" * 8 + F(tx) + F(ty) + F(0) +
            F(0) + F(0) + F(0) + F(1) + F(1) + F(w) + F(h))


def bflyt(pw, ph, py):
    secs = []
    secs.append(section(b"lyt1", bytes([1, 0, 0, 0]) + F(1280) + F(720) + F(0) + F(0) + name(LAYOUT, 12)))
    tex = name(TEXTURE, len(TEXTURE) + 1 + (-(len(TEXTURE) + 1) % 4))
    secs.append(section(b"txl1", struct.pack(">HH", 1, 0) + struct.pack(">I", 4) + tex))
    # material: name[28] | black | white | flags (1 texmap, 1 tex SRT, 1 texgen) | texmap | tex SRT | texgen
    mat = (name("StoryPlate_00", 28) + bytes([0, 0, 0, 0]) + b"\xff" * 4 + struct.pack(">I", 0x15) +
           struct.pack(">HBB", 0, 4, 4) + F(0) + F(0) + F(0) + F(1) + F(1) + b"\0" * 8)
    secs.append(section(b"mat1", struct.pack(">HH", 1, 0) + struct.pack(">I", 0x10) + mat))  # offset from the section start
    secs.append(section(b"pan1", pane_body("RootPane", 0x01, 1280, 720, extra=0x00)))
    secs.append(section(b"pas1", b""))
    secs.append(section(b"pan1", pane_body("N_All_00", 0x03, 30, 40)))
    secs.append(section(b"pas1", b""))
    # picture: pane | vertex colours[4] | material index | tex coord count | pad | tex coords (TL TR BL BR)
    pic = (pane_body("P_Plate_00", 0x01, pw, ph, ty=py) + b"\xff" * 16 + struct.pack(">HBB", 0, 1, 0) +
           F(0) + F(0) + F(1) + F(0) + F(0) + F(1) + F(1) + F(1))
    secs.append(section(b"pic1", pic))
    secs.append(section(b"pae1", b""))
    secs.append(section(b"pae1", b""))
    secs.append(section(b"grp1", name("RootGroup", 24) + struct.pack(">HH", 0, 0)))
    secs.append(section(b"grs1", b""))
    secs.append(section(b"grp1", name("G_All_00", 24) + struct.pack(">HH", 1, 0) + name("N_All_00", 24)))
    secs.append(section(b"gre1", b""))
    body = b"".join(secs)
    return b"FLYT" + struct.pack(">HHIIHH", 0xFEFF, 0x14, 0x02020000, 0x14 + len(body), len(secs), 0) + body


def bflan_fade():
    # pat1: order, group count, name offset, group names offset, start/end frame, child binding | name | groups
    pat = (struct.pack(">HHIIhhBBH", 0, 1, 0x1C, 0x24, 0, 10, 0, 0, 0) + name("InAll", 8) + name("G_All_00", 28))
    # pai1: frame size, loop, pad, texture count, content count, content offsets offset | offsets | content
    key = struct.pack(">fff", 0, 0, 25.5) + struct.pack(">fff", 10, 255, 25.5)
    flvc = (b"FLVC" + bytes([1, 0, 0, 0]) + struct.pack(">I", 0xC) +
            bytes([0x00, 0x10, 0x02, 0x00]) + struct.pack(">HHI", 2, 0, 0xC) + key)
    content = name("N_All_00", 28) + bytes([1, 0, 0, 0]) + struct.pack(">I", 0x24) + flvc
    pai = struct.pack(">HBBHHI", 10, 0, 0, 0, 1, 0x14) + struct.pack(">I", 0x18) + content
    secs = [section(b"pat1", pat), section(b"pai1", pai)]
    body = b"".join(secs)
    return b"FLAN" + struct.pack(">HHIIHH", 0xFEFF, 0x14, 0x02020000, 0x14 + len(body), len(secs), 0) + body


def bflim_rgba8_linear(rgba, w, h):
    assert w % 64 == 0, "linear-aligned RGBA8 rows must be a multiple of 64 texels"
    image = bytes(rgba)
    footer = (b"FLIM" + struct.pack(">HHIIHH", 0xFEFF, 0x14, 0x02020000, len(image) + 0x28, 1, 0) +
              b"imag" + struct.pack(">IHHHBBI", 0x10, w, h, 0x100, 0x14, 0x01, len(image)))
    return image + footer


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("png")
    ap.add_argument("out")
    ap.add_argument("--width", type=int, default=512)
    a = ap.parse_args()
    from PIL import Image
    im = Image.open(a.png).convert("RGBA")
    w = a.width
    h = max(1, round(im.height * w / im.width))
    im = im.resize((w, h), Image.LANCZOS)
    files = {
        "blyt/%s.bflyt" % LAYOUT: bflyt(w, h, 160.0),
        "timg/%s" % TEXTURE: bflim_rgba8_linear(im.tobytes(), w, h),
        "anim/%s_InAll.bflan" % LAYOUT: bflan_fade(),
    }
    open(a.out, "wb").write(yaz0_stored(sarc(files)))
    print("%s: %dx%d texture, %d bytes" % (a.out, w, h, os.path.getsize(a.out)))


if __name__ == "__main__":
    main()
