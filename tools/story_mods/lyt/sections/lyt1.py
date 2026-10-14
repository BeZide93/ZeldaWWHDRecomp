"""lyt1: layout header. drawFromCenter u8, pad[3], width f32, height f32, max parts width/height f32,
layout name (NUL-terminated, padded to 4)."""
from .. import binio


def write(desc):
    w, h = desc.size
    body = (bytes([1, 0, 0, 0]) + binio.f32(w) + binio.f32(h) + binio.f32(0) + binio.f32(0) +
            binio.cstr4(desc.name))
    return binio.section(b"lyt1", body)
