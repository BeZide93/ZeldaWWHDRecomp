"""The layout description: a JSON document that the generator turns into a layout archive.

The format is documented in schema.json (JSON Schema) and README-style in this docstring:

{
  "name": "StoryLyt_00",                 # archive / layout name: Layout/<name>.szs, blyt/<name>.bflyt
  "size": [1280, 720],                   # layout size (RootPane size)
  "textures": {                          # our own art only, never game files
    "StoryPlate_00": {"png": "plate.png", "format": "rgba8", "width": 512}
  },
  "fonts": [],                           # font file names for text panes (fnl1), e.g. the game's own font
  "materials": {
    "StoryPlate_00": {"textures": [{"texture": "StoryPlate_00"}]}
  },
  "panes": [                             # children of the implicit RootPane
    {"type": "null", "name": "N_All_00", "size": [30, 40], "influence_alpha": true, "children": [
      {"type": "picture", "name": "P_Plate_00", "size": [512, 171], "translate": [0, 160],
       "material": "StoryPlate_00"}
    ]}
  ],
  "groups": {"G_All_00": ["N_All_00"]},  # children of the implicit RootGroup
  "animations": {                         # anim/<name>_<animation>.bflan
    "InAll": {"frames": 10, "groups": ["G_All_00"],
              "tracks": [{"pane": "N_All_00", "target": "alpha", "keys": [[0, 0], [10, 255]]}]}
  }
}

Pane types are registered in panes.py ("null", "picture", "window", "text"); each type's own keys are
documented in its section writer. Paths ("png") are relative to the description file.
"""
import json
import os

from . import bflim


class Description:
    def __init__(self, data, base_dir="."):
        self.data = data
        self.base_dir = base_dir
        self.name = data["name"]
        self.size = data.get("size", [1280, 720])
        self.textures = data.get("textures", {})
        self.fonts = list(data.get("fonts", []))
        self.materials = data.get("materials", {})
        self.panes = data.get("panes", [])
        self.groups = data.get("groups", {})
        self.animations = data.get("animations", {})
        self.validate()

    @classmethod
    def load(cls, path):
        with open(path) as f:
            return cls(json.load(f), os.path.dirname(os.path.abspath(path)))

    def path(self, rel):
        return rel if os.path.isabs(rel) else os.path.join(self.base_dir, rel)

    def texture_file(self, tex):
        """The archive file name of a texture: <name>^<format suffix>.bflim (the game's naming)."""
        if tex not in self.textures:
            raise KeyError("unknown texture %r" % tex)
        fmt = bflim.FORMATS[self.textures[tex].get("format", "rgba8")]
        return "%s^%s.bflim" % (tex, fmt.suffix)

    def iter_panes(self, panes=None):
        for p in self.panes if panes is None else panes:
            yield p
            yield from self.iter_panes(p.get("children", []))

    def validate(self):
        from .panes import PANE_TYPES
        names = set()
        for p in self.iter_panes():
            if p.get("type", "null") not in PANE_TYPES:
                raise ValueError("pane %r: unknown type %r" % (p.get("name"), p.get("type")))
            if p["name"] in names:
                raise ValueError("duplicate pane name %r" % p["name"])
            names.add(p["name"])
        for g, members in self.groups.items():
            for m in members:
                if m not in names:
                    raise ValueError("group %r: unknown pane %r" % (g, m))
        for a, spec in self.animations.items():
            for g in spec.get("groups", []):
                if g not in self.groups:
                    raise ValueError("animation %r: unknown group %r" % (a, g))
            for t in spec.get("tracks", []):
                if "pane" in t and t["pane"] not in names:
                    raise ValueError("animation %r: unknown pane %r" % (a, t["pane"]))
                if "material" in t and t["material"] not in self.materials:
                    raise ValueError("animation %r: unknown material %r" % (a, t["material"]))
        for t, spec in self.textures.items():
            if spec.get("format", "rgba8") not in bflim.FORMATS:
                raise ValueError("texture %r: unknown format %r" % (t, spec.get("format")))
