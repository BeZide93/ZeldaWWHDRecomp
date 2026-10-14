#!/usr/bin/env python3
"""Unit tests for the story layout writers (tools/story_mods/lyt)."""
import hashlib
import json
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lyt  # noqa: E402
from lyt import bflan, bflim, bflyt, binio  # noqa: E402
from lyt.anim import tracks  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
EXAMPLE = os.path.join(HERE, "lyt", "examples", "plate.json")
sha = lambda b: hashlib.sha256(b).hexdigest()


def load_json(path):
    with open(path) as f:
        return json.load(f)


def example(**changes):
    d = load_json(EXAMPLE)
    d.update(changes)
    return lyt.Description(d, os.path.dirname(EXAMPLE))


class PrototypeEquivalence(unittest.TestCase):
    """The package reproduces the prototype's files byte for byte (the prototype ran in the game)."""

    def test_bflyt(self):
        data, ctx = bflyt.build(example())
        self.assertEqual(sha(data), "624c0c536cd8cec256c0b9ae70b6d6f7c9cb918947150659182648eeca37496d")
        self.assertEqual(ctx.textures, ["StoryPlate_00^l.bflim"])

    def test_bflan(self):
        d = example()
        data = bflan.build("InAll", d.animations["InAll"], d)
        self.assertEqual(sha(data), "c2e35a89a86be2edc124bab7883e882ee2657214ba61d5a5cef55aabe6a8f63c")

    def test_bflim(self):
        data = bflim.encode(bytes(range(256)) * 2, 64, 2)
        self.assertEqual(sha(data), "124fe89ae4fffd22650e84241b26bfffd98f658b984b6a346cb3ab11e0470804")
        self.assertEqual(bflim.parse_footer(data), (64, 2, 0x100, 0x14, 0x01, 512))


class Structure(unittest.TestCase):
    def test_section_order(self):
        data, _ = bflyt.build(example())
        order = [m for m, _ in binio.read_sections(data)]
        self.assertEqual(order, [b"lyt1", b"txl1", b"mat1", b"pan1", b"pas1", b"pan1", b"pas1", b"pic1",
                                 b"pae1", b"pae1", b"grp1", b"grs1", b"grp1", b"gre1"])

    def test_materials_shared_and_ordered(self):
        d = load_json(EXAMPLE)
        d["textures"]["Other_00"] = {"png": "x.png"}
        d["materials"]["Other_00"] = {"textures": [{"texture": "Other_00"}]}
        kids = d["panes"][0]["children"]
        kids.append({"type": "picture", "name": "P_B", "size": [4, 4], "material": "Other_00"})
        kids.append({"type": "picture", "name": "P_C", "size": [4, 4], "material": "StoryPlate_00"})
        data, ctx = bflyt.build(lyt.Description(d))
        self.assertEqual([n for n, _ in ctx.materials], ["StoryPlate_00", "Other_00"])
        self.assertEqual(ctx.textures, ["StoryPlate_00^l.bflim", "Other_00^l.bflim"])
        secs = dict((m, b) for m, b in binio.read_sections(data) if m in (b"txl1", b"mat1"))
        n, = struct.unpack(">H", secs[b"txl1"][:2])
        o0, o1 = struct.unpack(">II", secs[b"txl1"][4:12])
        self.assertEqual((n, o0, o1), (2, 8, 8 + len("StoryPlate_00^l.bflim") + 1))
        mat = secs[b"mat1"]
        offs = struct.unpack(">II", mat[4:12])
        self.assertEqual(offs[0], 0x14)  # relative to the section start (8-byte header + count + 2 offsets)
        second = mat[offs[1] - 8:]
        self.assertEqual(second[:8], b"Other_00")
        self.assertEqual(struct.unpack(">IHBB", second[36:44]), (0x15, 1, 4, 4))

    def test_hooks_not_written_yet(self):
        for kind in ("text",):
            d = load_json(EXAMPLE)
            d["panes"].append({"type": kind, "name": "X_00", "size": [1, 1]})
            with self.assertRaises(NotImplementedError):
                bflyt.build(lyt.Description(d))

    def test_validation(self):
        with self.assertRaises(ValueError):
            example(groups={"G": ["Missing"]})
        with self.assertRaises(ValueError):
            example(panes=[{"type": "sprite", "name": "A", "size": [1, 1]}])
        with self.assertRaises(ValueError):
            example(panes=[{"name": "A", "size": [1, 1]}, {"name": "A", "size": [1, 1]}], groups={},
                    animations={})
        with self.assertRaises(ValueError):
            binio.name("x" * 24, 24)

    def test_schema(self):
        try:
            import jsonschema
        except ImportError:
            self.skipTest("jsonschema not installed")
        schema = load_json(os.path.join(HERE, "lyt", "schema.json"))
        jsonschema.validate(load_json(EXAMPLE), schema)


class Animation(unittest.TestCase):
    def test_hermite_slopes(self):
        keys = tracks.hermite_keys([[0, 0], [10, 100], [20, 100]])
        self.assertEqual(struct.unpack(">9f", keys), (0, 0, 10, 10, 100, 5, 20, 100, 0))
        self.assertEqual(struct.unpack(">6f", tracks.hermite_keys([[0, 0], [5, 1]], "ease"))[2::3], (0, 0))
        self.assertEqual(struct.unpack(">3f", tracks.hermite_keys([[0, 1, 7]])), (0, 1, 7))

    def test_pattern_and_tags(self):
        d = load_json(EXAMPLE)
        d["textures"]["Alt_00"] = {"png": "alt.png"}
        d["animations"]["Select"] = {"frames": 4, "loop": True, "groups": ["G_All_00"], "tracks": [
            {"material": "StoryPlate_00", "target": "tex.pattern", "keys": [[0, "StoryPlate_00"], [2, "Alt_00"]]},
            {"pane": "P_Plate_00", "target": "sx", "keys": [[0, 1], [4, 1.2]]},
            {"pane": "P_Plate_00", "target": "sy", "keys": [[0, 1], [4, 1.2]]},
            {"pane": "P_Plate_00", "target": "alpha", "keys": [[0, 255]]},
        ]}
        desc = lyt.Description(d)
        data = bflan.build("Select", desc.animations["Select"], desc)
        (_, pat), (_, pai) = binio.read_sections(data)
        frames, loop, _, ntex, ncont, table = struct.unpack(">HBBHHI", pai[:12])
        self.assertEqual((frames, loop, ntex, ncont), (4, 1, 2, 2))
        sec = b"pai1" + struct.pack(">I", 8 + len(pai)) + pai  # offsets are relative to the section start
        names = [sec[20 + o:].split(b"\0")[0] for o in struct.unpack(">II", sec[20:28])]
        self.assertEqual(names, [b"StoryPlate_00^l.bflim", b"Alt_00^l.bflim"])
        c0, c1 = struct.unpack(">II", sec[table:table + 8])
        self.assertEqual(sec[c0:c0 + 13], b"StoryPlate_00")
        self.assertEqual(sec[c0 + 28:c0 + 30], bytes([1, tracks.MATERIAL]))
        self.assertEqual(sec[c1:c1 + 10], b"P_Plate_00")
        ntags = sec[c1 + 28]
        tag_offs = struct.unpack(">%dI" % ntags, sec[c1 + 32:c1 + 32 + 4 * ntags])
        self.assertEqual([sec[c1 + o:c1 + o + 4] for o in tag_offs], [b"FLPA", b"FLVC"])
        flpa = c1 + tag_offs[0]
        self.assertEqual(sec[flpa + 4], 2)  # sx and sy share one FLPA block
        e0 = flpa + struct.unpack(">I", sec[flpa + 8:flpa + 12])[0]
        self.assertEqual(sec[e0:e0 + 4], bytes([0, 6, 2, 0]))
        # step keys of the pattern: frame f32, value u16, pad u16
        tp = c0 + struct.unpack(">I", sec[c0 + 32:c0 + 36])[0]
        e = tp + struct.unpack(">I", sec[tp + 8:tp + 12])[0]
        self.assertEqual(sec[e:e + 8], bytes([0, 0, 1, 0, 0, 2, 0, 0]))
        self.assertEqual(struct.unpack(">fHHfHH", sec[e + 12:e + 28]), (0, 0, 0, 2, 1, 0))
        self.assertEqual(pat[0x14:0x14 + 7], b"Select\0")


class Archive(unittest.TestCase):
    def test_files(self):
        loader = lambda spec, desc: (bytes(512 * 171 * 4), 512, 171)
        files = lyt.build_files(example(), loader)
        self.assertEqual(sorted(files), ["anim/StoryLyt_00_InAll.bflan", "blyt/StoryLyt_00.bflyt",
                                         "timg/StoryPlate_00^l.bflim"])

    def test_linear_width(self):
        with self.assertRaises(ValueError):
            bflim.encode(bytes(4 * 10), 10, 1)


if __name__ == "__main__":
    unittest.main()
