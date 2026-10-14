"""Pane type registry: description "type" -> (section magic, writer(pane, ctx) -> section body).

The window (wnd1) and text (txt1) writers plug in here; further pane types (parts, prt1; bounding, bnd1)
can be added with register()."""
from .sections import bnd1, pan1, pic1, prt1, txt1, wnd1

PANE_TYPES = {
    "null": (b"pan1", pan1.write),
    "picture": (b"pic1", pic1.write),
    "window": (b"wnd1", wnd1.write),
    "text": (b"txt1", txt1.write),
    "bounding": (b"bnd1", bnd1.write),
    "parts": (b"prt1", prt1.write),
}


def register(type_name, magic, writer):
    PANE_TYPES[type_name] = (magic, writer)
