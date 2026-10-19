# SPDX-License-Identifier: MPL-2.0
"""Game pieces converter: the port's recipes, the selection rule (whole components only, never a cut)
and the collision rules (listed groups inside the volume; exits and other native fields dropped; ground
types mapped to world-object surfaces), on synthetic data. With WWHD_GAME_DIR set to a game folder, it
also builds every recipe from the player's files into a temporary folder and checks the counts."""
import json
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import game_piece_model as gp  # noqa: E402
import collision_mesh  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
RECIPES = os.path.join(ROOT, 'runtime', 'assets', 'game_pieces')


def recipes():
    for name in sorted(os.listdir(RECIPES)):
        if name.endswith('.recipe.json'):
            with open(os.path.join(RECIPES, name)) as f:
                yield name, json.load(f)


def shape(node, pos, tris, material='m'):
    s = gp.Shape()
    s.node, s.pos, s.tris, s.material = node, pos, tris, material
    return s


def dzb(verts, tris, names, infos):
    """A minimal native collision file: header, vertices, triangles, groups, infos (no tree)."""
    head = 0x34
    ov = head
    ot = ov + 12 * len(verts)
    og = ot + 10 * len(tris)
    names_at = og + 0x34 * len(names)
    name_bytes, name_offsets = b'', []
    for n in names:
        name_offsets.append(names_at + len(name_bytes))
        name_bytes += n.encode() + b'\0'
    oi = names_at + len(name_bytes)
    oi += -oi % 4
    out = bytearray(oi + 16 * len(infos))
    struct.pack_into('>12I', out, 0, len(verts), ov, len(tris), ot, 0, 0, 0, 0, len(names), og, len(infos), oi)
    for i, v in enumerate(verts):
        struct.pack_into('>3f', out, ov + 12 * i, *v)
    for i, t in enumerate(tris):
        struct.pack_into('>5H', out, ot + 10 * i, *t)
    for i, at in enumerate(name_offsets):
        struct.pack_into('>I', out, og + 0x34 * i, at)
    out[names_at:names_at + len(name_bytes)] = name_bytes
    for i, info in enumerate(infos):
        struct.pack_into('>4I', out, oi + 16 * i, *info)
    return gp.read_dzb(bytes(out))


class Recipes(unittest.TestCase):
    def test_port_recipes_follow_the_rules(self):
        seen = set()
        for name, r in recipes():
            with self.subTest(recipe=name):
                gp.check_recipe(r)
                self.assertEqual(name, r['set'] + '.recipe.json')
                for p in r['pieces']:
                    self.assertNotIn(p['id'], seen)
                    seen.add(p['id'])
                    # references only: no game bytes, no file paths other than the room archive's name
                    text = json.dumps(p)
                    self.assertNotIn('/', text)
        self.assertIn('outset-house-small', seen)

    def test_every_part_pins_its_source_textures(self):
        # each part names the room textures its materials sample; the build refuses another look
        pinned = {}
        for name, r in recipes():
            for p in r['pieces']:
                t = p['expect'].get('textures')
                self.assertIsInstance(t, list, p['id'])
                self.assertEqual(len(t), len(p['parts']), p['id'])
                self.assertTrue(all(x for x in t), p['id'])
                pinned[p['id']] = t
        # the sea rocks are Dragon Roost's cliff stone (its streaked look is the original's), the torii its own wood
        for rock in ('sea-rock-spire', 'sea-rock-pillar', 'sea-rock-stack'):
            self.assertEqual(pinned[rock], [['Txe_Edragon_gake']])
        self.assertIn('Txe_Edragon_tesuri', pinned['dragon-roost-torii'][0])
        self.assertEqual(pinned['shipwreck-hull'], [['wood0313_22']])
        r = next(r for _, r in recipes())
        bad = json.loads(json.dumps(r))
        bad['pieces'][0]['expect']['textures'] = [['../x']]
        with self.assertRaises(gp.BuildError):
            gp.check_recipe(bad)

    def test_bad_recipes_refused(self):
        _, good = next(recipes())
        for change in (lambda r: r.update(game_pieces_format=2), lambda r: r['source'].update(archive='../x.szs'),
                       lambda r: r['pieces'][0].update(volume=[1, 2, 3]), lambda r: r['pieces'][0]['parts'][0].update(light='actor'),
                       lambda r: r['pieces'][0]['expect'].update(triangles=[1, 2, 3])):
            r = json.loads(json.dumps(good))
            change(r)
            with self.assertRaises(gp.BuildError):
                gp.check_recipe(r)


class Selection(unittest.TestCase):
    def test_whole_components_only(self):
        # two separate quads: one inside the volume, one crossing it; plus a quad of another node
        inside = [(0, 0, 0), (10, 0, 0), (10, 0, 10), (0, 0, 10)]
        crossing = [(50, 0, 0), (150, 0, 0), (150, 0, 10), (50, 0, 10)]
        pos = inside + crossing
        a = shape('ie', pos, [(0, 1, 2), (0, 2, 3), (4, 5, 6), (4, 6, 7)])
        b = shape('jimen', inside, [(0, 1, 2)])
        taken, crossing_count = gp.select([a, b], ['ie'], [-1, -1, -1, 100, 100, 100])
        self.assertEqual(taken, {0: [(0, 1, 2), (0, 2, 3)]})
        self.assertEqual(crossing_count, 1)
        # a component joined through welded positions in another shape of the same node is one piece
        c = shape('ie', [(10, 0, 10), (200, 0, 10), (200, 0, 20)], [(0, 1, 2)])
        taken, crossing_count = gp.select([a, c], ['ie'], [-1, -1, -1, 100, 100, 100])
        self.assertNotIn(0, taken)
        self.assertEqual(crossing_count, 2)
        with self.assertRaises(gp.BuildError):
            gp.select([a], ['missing'], [-1, -1, -1, 100, 100, 100])


class Collision(unittest.TestCase):
    def test_groups_volume_and_fields(self):
        # room 44's origin; a floor (wood), a wall, a door polygon with an exit, a shadow floor, and a group not listed
        o = (-200000.0, 0.0, 300000.0)
        v = [(o[0] + x, y, o[2] + z) for x, y, z in [(0, 0, 0), (0, 0, 100), (100, 0, 0), (0, 100, 0), (100, 100, 0), (900, 0, 0), (900, 0, 100), (1000, 0, 0)]]
        floor_info = (0x07FFE0FF, (2 << 16) | 0xFF, 0xFFFFFFFF, 0)
        door_info = (0x07FFE0FF & ~(0x3F << 13) | (4 << 13), 0xFF, 0xFFFFFFFF, 0)
        roof_info = (0x07FFE0FF, (16 << 16) | (1 << 12) | 0xFF, 0xFFFFFFFF, 0)
        tris = [(0, 1, 2, 0, 0), (0, 2, 3, 1, 1), (2, 4, 3, 1, 2), (0, 1, 2, 2, 3), (5, 6, 7, 0, 4)]
        col = dzb(v, tris, ['house', 'house_KIRI', 'house_KAGE', 'house_yane', 'house'], [floor_info, door_info, roof_info])
        piece = {'volume': [-10, -10, -10, 500, 500, 500], 'collision': {'groups': ['house', 'house_KIRI', 'house_KAGE']}}
        body, info, count = gp.piece_collision(col, piece, o, (50, 0, 50))
        self.assertEqual(count, 3)  # the floor, the door polygon (a wall, no exit) and the shadow group's wall; not the far triangle, not the unlisted group
        n_vertices, n_tris = struct.unpack_from('>II', body, 8)
        self.assertEqual(n_tris, 3)
        records = [struct.unpack_from('>4H', body, 40 + 12 * n_vertices + 8 * i) for i in range(n_tris)]
        surfaces = sorted(r[3] for r in records)
        self.assertEqual(surfaces, sorted([collision_mesh.SEMANTICS['col_wood'], collision_mesh.SEMANTICS['col_wall'], collision_mesh.SEMANTICS['col_wall']]))
        # positions are relative to the piece's origin
        first = struct.unpack_from('>3f', body, 40)
        self.assertEqual(first, (-50.0, 0.0, -50.0))
        self.assertEqual(info['collision_triangles'], 3)
        self.assertEqual(gp.piece_collision(col, {'volume': piece['volume'], 'collision': {'groups': []}}, o, (0, 0, 0))[2], 0)


@unittest.skipUnless(os.environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to build the recipes from a game folder')
class FromGame(unittest.TestCase):
    def test_build_every_recipe(self):
        game = os.environ['WWHD_GAME_DIR']
        with tempfile.TemporaryDirectory() as d:
            for name, r in recipes():
                path = os.path.join(RECIPES, name)
                meta = gp.build(game, path, 'gp' + '0' * 29, os.path.join(d, r['set'] + '.szs'))
                self.assertEqual(len(meta['pieces']), len(r['pieces']))
                self.assertLessEqual(meta['archive_bytes'], r['memory']['archive_bytes'])
                again = gp.build(game, path, 'gp' + '0' * 29, os.path.join(d, r['set'] + '.szs'))
                self.assertEqual(again['archive_sha256'], meta['archive_sha256'])


if __name__ == '__main__':
    unittest.main()
