"""Animation writer: one description animation -> anim/<layout>_<animation>.bflan.

Description:
  "InAll": {"frames": 10,            # frame count (pai1); pat1 range is [start, start + frames]
            "start": 0, "loop": false, "groups": ["G_All_00"], "order": 0,
            "tracks": [
              {"pane": "N_All_00", "target": "alpha", "keys": [[0, 0], [10, 255]], "interp": "linear"},
              {"material": "M_Btn", "target": "tex.pattern", "index": 0, "keys": [[0, "BtnA"], [5, "BtnB"]]},
              {"pane": "T_Text", "tag": "FLFS", "target": 3, "keys": [[0, 0]]}   # raw form
            ]}
Texture pattern keys name textures of the description; they are collected into pai1's texture list.
"""
from collections import OrderedDict

from . import binio
from .anim import pai1, pat1, tracks


def build(anim_name, spec, desc):
    frames = int(spec["frames"])
    start = int(spec.get("start", 0))
    textures = []
    by_target = OrderedDict()  # (name, type) -> OrderedDict(tag -> [entries])
    for t in spec.get("tracks", []):
        tag, ctype, index, target, curve = tracks.resolve(t)
        keys = t["keys"]
        if tag == b"FLTP":
            vals = []
            for f, tex in keys:
                fname = tex if str(tex).endswith(".bflim") else desc.texture_file(tex)
                if fname not in textures:
                    textures.append(fname)
                vals.append([f, textures.index(fname)])
            keys = vals
        owner = t.get("pane") or t.get("material")
        tags = by_target.setdefault((owner, ctype), OrderedDict())
        tags.setdefault(tag, []).append(tracks.entry(index, target, curve, (keys, t.get("interp", "linear"))))
    contents = [pai1.content(n, ct, list(tags.items())) for (n, ct), tags in by_target.items()]
    return binio.file_header(b"FLAN", [
        pat1.write(anim_name, spec.get("groups", []), start, start + frames, spec.get("order", 0),
                   spec.get("child_binding", 0)),
        pai1.write(frames, spec.get("loop", False), textures, contents),
    ])
