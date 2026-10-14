"""Texture writer: .bflim (GX2 surface data followed by the FLIM footer).

Footer, as in the game's own .bflim files: "FLIM", BOM, header size 0x14, version, file size, 1 section,
then "imag" (size 0x10), width u16, height u16, alignment u16, format u8, tiling byte (low nibble: GX2 tile
mode, high nibble: swizzle), image size u32. The image data comes before the footer.

Formats are registered in FORMATS (archive file suffix + FLIM format code + encoder). The format codes were
inferred from the game's own files (bytes per texel of every layout texture):
  0x14 "^l" RGBA8 (verified in game)   others: see textures.py (compressed formats) as they are added.
Encoders take (rgba bytes, width, height) and return the untiled surface; tiling is applied by tile modes.
"""
import struct
from collections import namedtuple

from . import binio

Format = namedtuple("Format", "code suffix encode bytes_per_block block")  # block: texels per block side

FORMATS = {
    "rgba8": Format(0x14, "l", lambda rgba, w, h: bytes(rgba), 4, 1),
}

# tile modes: name -> (tiling byte, tiler(surface bytes, width, height, fmt) -> tiled bytes, alignment)
TILE_MODES = {
    "linear": (0x01, lambda data, w, h, fmt: data, 0x100),  # GX2 linear-aligned
}


def register_format(key, code, suffix, encode, bytes_per_block, block):
    FORMATS[key] = Format(code, suffix, encode, bytes_per_block, block)


def register_tile_mode(key, tile_byte, tiler, alignment):
    TILE_MODES[key] = (tile_byte, tiler, alignment)


def check_size(w, h, fmt="rgba8", tile="linear"):
    if tile == "linear" and fmt == "rgba8" and w % 64:
        raise ValueError("linear-aligned RGBA8 rows must be a multiple of 64 texels (width %d)" % w)


def encode(rgba, w, h, fmt="rgba8", tile="linear"):
    if len(rgba) != w * h * 4:
        raise ValueError("expected %d RGBA bytes, got %d" % (w * h * 4, len(rgba)))
    check_size(w, h, fmt, tile)
    f = FORMATS[fmt]
    tile_byte, tiler, align = TILE_MODES[tile]
    image = tiler(f.encode(rgba, w, h), w, h, f)
    footer = (b"FLIM" + struct.pack(">HHIIHH", 0xFEFF, 0x14, binio.VERSION, len(image) + 0x28, 1, 0) +
              b"imag" + struct.pack(">IHHHBBI", 0x10, w, h, align, f.code, tile_byte, len(image)))
    return image + footer


def parse_footer(data):
    """(width, height, alignment, format code, tiling byte, image size) of a .bflim."""
    f = data[-0x28:]
    assert f[:4] == b"FLIM" and f[0x14:0x18] == b"imag", "not a bflim"
    return struct.unpack(">HHHBBI", f[0x1C:0x28])
