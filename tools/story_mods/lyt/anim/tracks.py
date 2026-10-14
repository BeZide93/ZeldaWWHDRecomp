"""Animation track targets and key encoding.

A track block (tag) inside pai1 content: magic (e.g. "FLVC"), entry count u8, pad[3], u32 entry offsets
(relative to the tag start). An entry: index u8, target u8, curve u8 (1 step, 2 hermite), pad u8,
key count u16, pad u16, key offset u32 (0xC), then the keys:
  hermite: frame f32, value f32, slope f32      step: frame f32, value u16, pad u16

Tags and targets seen in the game's own .bflan files (content type 0 = pane, 1 = material):
  FLPA pane SRT     targets 0,1,5,6,7 used (translate x y, rotate z, scale x y); order = pan1 field order
  FLVC pane colour  targets 0-15 vertex colours (4 corners x rgba), 16 = pane alpha (verified in game)
  FLVI pane visibility (step)
  FLMC material colours targets 0-7      FLTS texture SRT (index = texture map) targets 0-4
  FLTP texture pattern (step; index = texture map, value = pai1 texture list index)
  FLFS (font shadow, text panes; lyt-text)
Targets marked "inferred" below follow the field order of the section they animate and still need a
game-run check before they are relied on.
"""
import struct

PANE, MATERIAL = 0, 1
HERMITE, STEP = 2, 1

_CORNERS = ("tl", "tr", "bl", "br")
_CH = ("r", "g", "b", "a")

# name -> (tag, content type, target, curve, verified)
TARGETS = {
    "tx": (b"FLPA", PANE, 0, HERMITE, True), "ty": (b"FLPA", PANE, 1, HERMITE, True),
    "tz": (b"FLPA", PANE, 2, HERMITE, False), "rx": (b"FLPA", PANE, 3, HERMITE, False),
    "ry": (b"FLPA", PANE, 4, HERMITE, False), "rz": (b"FLPA", PANE, 5, HERMITE, True),
    "sx": (b"FLPA", PANE, 6, HERMITE, True), "sy": (b"FLPA", PANE, 7, HERMITE, True),
    "w": (b"FLPA", PANE, 8, HERMITE, False), "h": (b"FLPA", PANE, 9, HERMITE, False),
    "alpha": (b"FLVC", PANE, 16, HERMITE, True),
    "visible": (b"FLVI", PANE, 0, STEP, True),
}
for i, c in enumerate(_CORNERS):
    for j, ch in enumerate(_CH):
        TARGETS["vc.%s.%s" % (c, ch)] = (b"FLVC", PANE, 4 * i + j, HERMITE, False)
for j, ch in enumerate(_CH):
    TARGETS["black.%s" % ch] = (b"FLMC", MATERIAL, j, HERMITE, False)
    TARGETS["white.%s" % ch] = (b"FLMC", MATERIAL, 4 + j, HERMITE, False)
for j, k in enumerate(("tx", "ty", "rotate", "sx", "sy")):
    TARGETS["tex.%s" % k] = (b"FLTS", MATERIAL, j, HERMITE, False)
TARGETS["tex.pattern"] = (b"FLTP", MATERIAL, 0, STEP, True)


def register(name, tag, content_type, target, curve, verified=False):
    """Add a target (e.g. the text pane work's FLFS targets)."""
    TARGETS[name] = (tag, content_type, target, curve, verified)


def resolve(track):
    """(tag, content type, index, target, curve) of a description track."""
    if "tag" in track:  # raw form: {"tag": "FLFS", "target": 3, "type": 0, "index": 0, "curve": 2}
        return (track["tag"].encode(), track.get("type", PANE if "pane" in track else MATERIAL),
                track.get("index", 0), track["target"], track.get("curve", HERMITE))
    tag, ctype, target, curve, _ = TARGETS[track["target"]]
    return tag, ctype, track.get("index", 0), target, curve


def hermite_keys(keys, interp="linear"):
    """keys: [[frame, value(, slope)]]. Missing slopes: "linear" = mean of the neighbouring segment slopes
    (2 keys: the straight line), "ease" = 0."""
    out = b""
    fs = [k[0] for k in keys]
    vs = [k[1] for k in keys]
    for i, k in enumerate(keys):
        if len(k) > 2:
            slope = k[2]
        elif interp == "ease" or len(keys) == 1:
            slope = 0.0
        else:
            segs = []
            if i > 0:
                segs.append((vs[i] - vs[i - 1]) / (fs[i] - fs[i - 1]))
            if i + 1 < len(keys):
                segs.append((vs[i + 1] - vs[i]) / (fs[i + 1] - fs[i]))
            slope = sum(segs) / len(segs)
        out += struct.pack(">fff", k[0], k[1], slope)
    return out


def step_keys(keys):
    return b"".join(struct.pack(">fHH", k[0], int(k[1]), 0) for k in keys)


def entry(index, target, curve, keys):
    data = hermite_keys(keys[0], keys[1]) if curve == HERMITE else step_keys(keys[0])
    return bytes([index, target, curve, 0]) + struct.pack(">HHI", len(keys[0]), 0, 0xC) + data


def tag_block(tag, entries):
    """entries: [encoded entry bytes]."""
    head = tag + bytes([len(entries), 0, 0, 0])
    offs, blob = [], b""
    first = len(head) + 4 * len(entries)
    for e in entries:
        offs.append(first + len(blob))
        blob += e
    return head + b"".join(struct.pack(">I", o) for o in offs) + blob
