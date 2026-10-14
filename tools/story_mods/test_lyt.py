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
        for kind in ("window", "text"):
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
        with self.assertRaises(ValueError):  # a track for a pane outside the animation's groups
            d = load_json(EXAMPLE)
            d["animations"]["InAll"]["tracks"].append({"pane": "P_Plate_00", "target": "sx", "keys": [[0, 1]]})
            lyt.Description(d)

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
        d["groups"]["G_All_00"].append("P_Plate_00")
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

    def test_rgba_size_checked(self):
        with self.assertRaises(ValueError):
            bflim.encode(bytes(4 * 10), 10, 2)


def gradient(w, h, alpha=True):
    """A synthetic test picture: colour gradients, a hard edge and (optionally) an alpha ramp."""
    import numpy as np
    y, x = np.mgrid[0:h, 0:w]
    a = np.zeros((h, w, 4), np.uint8)
    a[..., 0] = x * 255 // max(w - 1, 1)
    a[..., 1] = y * 255 // max(h - 1, 1)
    a[..., 2] = np.where(x > w // 2, 200, 40)
    a[..., 3] = (x * 255 // max(w - 1, 1)) if alpha else 255
    return a.tobytes()


def rmse(a, b, channels=(0, 1, 2, 3)):
    import numpy as np
    x = np.frombuffer(a, np.uint8).reshape(-1, 4)[:, channels].astype(float)
    y = np.frombuffer(b, np.uint8).reshape(-1, 4)[:, channels].astype(float)
    return float(np.sqrt(((x - y) ** 2).mean()))


class Channels(unittest.TestCase):
    def test_game_drawing(self):
        """bc4a is drawn white with alpha, bc4l grey and opaque, bc5la grey with alpha (game run)."""
        img = bytes([200, 100, 50, 77] * 16)
        lum = round(200 * 0.299 + 100 * 0.587 + 50 * 0.114)
        for fmt, texel in (("bc4a", (255, 255, 255, 77)), ("bc4l", (lum, lum, lum, 255)),
                           ("bc5la", (lum, lum, lum, 77))):
            back, _, _ = bflim.decode(bflim.encode(img, 4, 4, fmt, "tiled"))
            self.assertEqual(tuple(back[:4]), texel, fmt)


class Tiling(unittest.TestCase):
    # (texels w, h, FLIM format code, image size, alignment) of game layout textures (tile mode 4); all
    # 308 2D-tiled game textures match, these rows cover every format and alignment case
    GAME = [(300, 300, 0x14, 389120, 2048), (72, 72, 0x14, 30720, 2048), (300, 300, 0x15, 61440, 4096),
            (160, 90, 0x15, 16384, 4096), (160, 8, 0x10, 8192, 4096), (300, 300, 0x0F, 61440, 4096),
            (1010, 150, 0x11, 196608, 8192), (505, 75, 0x11, 65536, 8192), (80, 80, 0x17, 16384, 8192),
            (2300, 500, 0x15, 589824, 4096)]

    def test_game_sizes(self):
        from lyt import gx2tile
        bpp = {0x14: 32, 0x15: 64, 0x10: 64, 0x0F: 64, 0x11: 128, 0x17: 128}
        for w, h, code, size, align in self.GAME:
            ew, eh = (w, h) if code == 0x14 else (-(-w // 4), -(-h // 4))
            self.assertEqual(gx2tile.surface_size(ew, eh, bpp[code], 4)[2:], (size, align), (w, h, code))
        self.assertEqual(gx2tile.surface_size(2, 2, 64, 2)[2:], (512, 256))  # 8 x 8 texels, 1D tiled

    def test_round_trip(self):
        from lyt import gx2tile
        for bpp in (32, 64, 128):
            for mode in (1, 2, 4):
                for swz in ((0, 3, 7) if mode == 4 else (0,)):
                    w, h = 37, 21
                    data = bytes((i * 7 + 3) & 0xFF for i in range(w * h * bpp // 8))
                    t = gx2tile.tile(data, w, h, bpp, mode, swz << 8)
                    self.assertEqual(gx2tile.untile(t, w, h, bpp, mode, swz << 8), data, (bpp, mode, swz))
                    addr = gx2tile.addresses(w, h, bpp, mode, swz << 8).reshape(-1)
                    self.assertEqual(len(set(addr.tolist())), w * h)  # no two elements share an address


class Textures(unittest.TestCase):
    def test_bc_quality(self):
        from lyt import bc
        w, h = 64, 32
        img = gradient(w, h)
        limits = {"bc1": (12, (0, 1, 2)), "bc3": (12, (0, 1, 2, 3)), "bc4": (4, (0,)), "bc5": (4, (0, 1))}
        for fmt, (limit, ch) in limits.items():
            src = gradient(w, h, alpha=False) if fmt == "bc1" else img
            back = bc.decode(fmt, bc.encode(fmt, src, w, h), w, h)
            img_ = src
            self.assertLess(rmse(img_, back, ch), limit, fmt)

    def test_bc1_punch_through(self):
        import numpy as np
        from lyt import bc
        img = np.frombuffer(gradient(8, 8, alpha=False), np.uint8).reshape(8, 8, 4).copy()
        img[:4, :4, 3] = 0
        back = np.frombuffer(bc.decode("bc1", bc.encode("bc1", img.tobytes(), 8, 8), 8, 8), np.uint8)
        back = back.reshape(8, 8, 4)
        self.assertTrue((back[:4, :4, 3] == 0).all() and (back[4:, :, 3] == 255).all())

    def test_bflim_formats(self):
        w, h = 100, 50
        img = gradient(w, h)
        for fmt in bflim.FORMATS:
            for tile, swz in (("tiled", 0), ("tiled", 5), ("1d", 0), ("linear", 0)):
                data = bflim.encode(img, w, h, fmt, tile, swz)
                fw, fh, align, code, tile_byte, size = bflim.parse_footer(data)
                self.assertEqual((fw, fh, code, tile_byte), (w, h, bflim.FORMATS[fmt].code,
                                                            bflim.TILE_MODES[tile] | swz << 5))
                self.assertEqual(len(data), size + 0x28)
                back, _, _ = bflim.decode(data)
                if fmt == "rgba8":
                    self.assertEqual(back, img)
                else:
                    from lyt import bc
                    f = bflim.FORMATS[fmt]
                    lin = bflim.elements(img, w, h, fmt)[0]
                    self.assertEqual(back, bflim._from_channels(bc.decode(f.bc, lin, w, h), f.channels))
                    if fmt in ("bc3", "bc4a", "bc5la"):  # smooth alpha survives
                        self.assertLess(rmse(img, back, (3,)), 10, fmt)


def cemu_latte_lib():
    """The port's libcemu_latte.a (address library) from WWHD_CEMU_LATTE_LIB or a build next to the repo."""
    root = os.path.dirname(os.path.dirname(HERE))
    for p in (os.environ.get("WWHD_CEMU_LATTE_LIB", ""), os.path.join(root, "build", "cmake", "libcemu_latte.a"),
              os.path.join(root, "build", "libcemu_latte.a")):
        if p and os.path.exists(p):
            return p, root
    return None, root


class PortAddrLib(unittest.TestCase):
    """Our tiled textures read back through the port's own address library (LatteAddrLib)."""

    @classmethod
    def setUpClass(cls):
        import shutil
        import subprocess
        import tempfile
        lib, root = cemu_latte_lib()
        cxx = shutil.which("clang++") or shutil.which("c++")
        if not lib or not cxx:
            raise unittest.SkipTest("no libcemu_latte.a (set WWHD_CEMU_LATTE_LIB) or no C++ compiler")
        cls.tmp = tempfile.mkdtemp()
        cemu = os.path.join(root, "runtime", "third_party", "cemu")
        cls.exe = os.path.join(cls.tmp, "gx2_detile")
        subprocess.run([cxx, "-std=c++20", "-w", "-O1", "-include", os.path.join(cemu, "cemu_shim.h"),
                        "-I" + cemu, "-I" + os.path.join(cemu, "Cafe"),
                        "-I" + os.path.join(root, "runtime", "third_party", "fmt", "include"),
                        os.path.join(HERE, "lyt", "tests", "gx2_detile.cpp"), lib, "-o", cls.exe], check=True)

    @classmethod
    def tearDownClass(cls):
        import shutil
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def test_detile(self):
        import subprocess
        for (w, h) in ((100, 50), (300, 300), (512, 171), (8, 8)):
            img = gradient(w, h)
            for fmt, f in bflim.FORMATS.items():
                for tile, swz in (("tiled", 0), ("tiled", 3), ("tiled", 6), ("1d", 0), ("linear", 0)):
                    data = bflim.encode(img, w, h, fmt, tile, swz)
                    _, _, align, _, _, size = bflim.parse_footer(data)
                    src, dst = os.path.join(self.tmp, "in"), os.path.join(self.tmp, "out")
                    with open(src, "wb") as fh:
                        fh.write(data[:size])
                    out = subprocess.run([self.exe, hex(f.gx2), str(w), str(h), str(bflim.TILE_MODES[tile]),
                                          hex(swz << 8), src, dst], check=True, capture_output=True, text=True)
                    pitch, height, psize, palign = map(int, out.stdout.split())
                    self.assertEqual((psize, palign), (size, align), (w, h, fmt, tile))
                    with open(dst, "rb") as fh:
                        self.assertEqual(fh.read(), bflim.elements(img, w, h, fmt)[0], (w, h, fmt, tile, swz))


if __name__ == "__main__":
    unittest.main()
