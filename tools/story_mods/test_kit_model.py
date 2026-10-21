# SPDX-License-Identifier: MPL-2.0
"""Kit models (kit_model.py): the recipe rules, welding, the collision (its *_col meshes, surfaces, the body limit)
on synthetic data. With WWHD_GAME_DIR set to a game folder, it also builds a small model with materials of two of
a kit room's models (Forest Haven's ground and its falling water) and checks the set: two parts, each lit with
its kit model's background light set, the second with the kit room's texture animation, and the collision."""
import json
import os
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kit_model as km  # noqa: E402
import collision_mesh  # noqa: E402
import game_piece_model as gp  # noqa: E402
import notebook_model as base  # noqa: E402


def glb(meshes):
    """a GLB (metres) from [(node name, [(material, positions, triangles)])]: positions, normals, uvs, indices"""
    binary, views, accessors, out_meshes, nodes, materials = bytearray(), [], [], [], [], []

    def add(data, kind, count, component, target, bounds=None):
        while len(binary) % 4:
            binary.append(0)
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data), 'target': target})
        binary.extend(data)
        a = {'bufferView': len(views) - 1, 'componentType': component, 'count': count, 'type': kind}
        if bounds:
            a['min'], a['max'] = bounds
        accessors.append(a)
        return len(accessors) - 1
    for name, prims in meshes:
        ps = []
        for material, pos, tris in prims:
            if material not in materials:
                materials.append(material)
            lo = [min(p[k] for p in pos) for k in range(3)]
            hi = [max(p[k] for p in pos) for k in range(3)]
            at = {'POSITION': add(b''.join(struct.pack('<3f', *p) for p in pos), 'VEC3', len(pos), 5126, 34962, (lo, hi)),
                  'NORMAL': add(struct.pack('<3f', 0, 1, 0) * len(pos), 'VEC3', len(pos), 5126, 34962),
                  'TEXCOORD_0': add(b''.join(struct.pack('<2f', p[0], p[2]) for p in pos), 'VEC2', len(pos), 5126, 34962)}
            idx = add(b''.join(struct.pack('<3I', *t) for t in tris), 'SCALAR', 3 * len(tris), 5125, 34963)
            ps.append({'attributes': at, 'indices': idx, 'material': materials.index(material)})
        out_meshes.append({'name': name, 'primitives': ps})
        nodes.append({'name': name, 'mesh': len(out_meshes) - 1})
    doc = {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': list(range(len(nodes)))}], 'nodes': nodes, 'meshes': out_meshes,
           'materials': [{'name': m} for m in materials], 'accessors': accessors, 'bufferViews': views, 'buffers': [{'byteLength': len(binary)}]}
    text = json.dumps(doc).encode()
    text += b' ' * (-len(text) % 4)
    while len(binary) % 4:
        binary.append(0)
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)
    return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body


def quad(x0, z0, x1, z1, y=0.0):
    return [(x0, y, z0), (x1, y, z0), (x1, y, z1), (x0, y, z1)], [(0, 2, 1), (0, 3, 2)]


def grid(n, size=1.0):
    pos = [(i * size, 0.0, j * size) for j in range(n + 1) for i in range(n + 1)]
    tris = []
    for j in range(n):
        for i in range(n):
            a = j * (n + 1) + i
            tris += [(a, a + n + 1, a + 1), (a + 1, a + n + 1, a + n + 2)]
    return pos, tris


def recipe(path, **extra):
    r = {'kit_model_format': 1, 'id': 'island', 'glb': path, 'kit': {'stage': 'sea', 'room': 41}}
    r.update(extra)
    return r


class KitModelTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)

    def write(self, name, data):
        path = os.path.join(self.dir.name, name)
        with open(path, 'wb') as f:
            f.write(data)
        return path

    def test_recipe_rules(self):
        path = self.write('m.glb', b'')
        km.check_recipe(recipe(path, materials={'grass': 'm_kusa_v'}, material='m_iwa_v'))
        bad = [recipe(path, kit_model_format=2), recipe('relative.glb'), recipe(path, id='Bad Id'), recipe(path, kit={'stage': 'sea'}),
               recipe(path, kit={'stage': 'toolongname', 'room': 1}), recipe(path, kit={'stage': 'sea', 'room': 64}),
               recipe(path, kit={'stage': 'sea', 'room': True}), recipe(path, materials={'a': 1}), recipe(path, extra=1),
               recipe(path, materials={str(i): 'm' for i in range(17)})]
        for r in bad:
            with self.assertRaises(km.BuildError, msg=str(r)):
                km.check_recipe(r)

    def test_weld_keeps_triangles(self):
        s = gp.Shape()
        s.records = [b'a', b'b', b'c', b'a', b'c', b'd']
        s.pos = [(0, 0, 0), (1, 0, 0), (0, 0, 1), (0, 0, 0), (0, 0, 1), (1, 0, 1)]
        s.tris = [(0, 1, 2), (3, 4, 5)]
        km.weld(s)
        self.assertEqual(s.records, [b'a', b'b', b'c', b'd'])
        self.assertEqual(s.tris, [(0, 1, 2), (0, 2, 3)])

    def test_collision_from_col_meshes(self):
        ground, ground_tris = quad(-2, -2, 2, 2)
        wall, wall_tris = [(-2, 0, 2), (2, 0, 2), (2, 2, 2), (-2, 2, 2)], [(0, 1, 2), (0, 2, 3)]
        path = self.write('m.glb', glb([('visible', [('grass', ground, ground_tris)]),
                                        ('island_col', [('col_grass', ground, ground_tris), ('col_stone', wall, wall_tris)])]))
        import stage_builder as sb
        body, info = km.kit_collision(sb.read_glb(path))
        mesh = struct.unpack_from('>4sIII', body, 0)
        self.assertEqual(mesh[:2], (b'WCOL', 1))
        self.assertEqual((mesh[2], mesh[3]), (6, 4))  # the shared edge's two corners welded
        self.assertEqual(info['collision_triangles'], 4)
        semantics = [struct.unpack_from('>4H', body, 40 + 12 * mesh[2] + 8 * i)[3] for i in range(4)]
        self.assertEqual(sorted(semantics), sorted([collision_mesh.SEMANTICS['col_grass']] * 2 + [collision_mesh.SEMANTICS['col_stone']] * 2))
        # an unknown collision material is refused
        bad = self.write('bad.glb', glb([('visible', [('grass', ground, ground_tris)]), ('island_col', [('col_lava', ground, ground_tris)])]))
        with self.assertRaises(km.BuildError):
            km.kit_collision(sb.read_glb(bad))

    def test_collision_body_limit(self):
        import stage_builder as sb
        pos, tris = grid(64)  # 8192 triangles: the limit
        path = self.write('ok.glb', glb([('visible', [('grass', *quad(0, 0, 1, 1))]), ('g_col', [('col_grass', pos, tris)])]))
        _, info = km.kit_collision(sb.read_glb(path))
        self.assertEqual(info['collision_triangles'], collision_mesh.MAX_TRIANGLES)
        pos, tris = grid(65)
        path = self.write('big.glb', glb([('visible', [('grass', *quad(0, 0, 1, 1))]), ('g_col', [('col_grass', pos, tris)])]))
        with self.assertRaisesRegex(km.BuildError, 'simpler'):
            km.kit_collision(sb.read_glb(path))

    @unittest.skipUnless(os.environ.get('WWHD_GAME_DIR'), 'set WWHD_GAME_DIR to a game folder to build from its files')
    def test_build_from_the_game(self):
        game = os.environ['WWHD_GAME_DIR']
        ground, ground_tris = grid(4, 2.0)
        fall, fall_tris = [(0, 0, 0), (2, 0, 0), (2, -6, 0), (0, -6, 0)], [(0, 1, 2), (0, 2, 3)]
        path = self.write('island.glb', glb([('island', [('grass', ground, ground_tris), ('waterfall', fall, fall_tris)]),
                                             ('island_col', [('col_grass', ground, ground_tris)])]))
        r = recipe(path, materials={'grass': 'm_kusa_v', 'waterfall': 'SC_00_m_v_x'})
        rp = self.write('r.json', json.dumps(r).encode())
        out = os.path.join(self.dir.name, 'cache', 'gk' + '0' * 29 + '.szs')
        meta = km.build(game, rp, 'gk' + '0' * 29, out)
        piece = meta['pieces'][0]
        self.assertEqual(meta['set'], 'kit:island')
        self.assertEqual([p['light'] for p in piece['parts']], [1, 2])          # model: BG0, model1: BG1
        self.assertEqual(piece['parts'][1].get('animation'), piece['parts'][1]['model'])  # the falling water moves
        self.assertEqual([p['triangles'] for p in piece['parts']], [32, 2])
        self.assertEqual(piece['collision']['collision_triangles'], 32)
        self.assertTrue(os.path.isfile(os.path.join(os.path.dirname(out), piece['collision_file'])))
        with open(out, 'rb') as f:
            members = dict(base.shaderprep.sarc(base.shaderprep.yaz0(f.read())))
        self.assertEqual(sorted(members), sorted([meta['identity'] + '.bfres'] + [m + '.sharcfb' for m in meta['models']]))
        # a second build of the same input is the kept one
        self.assertEqual(km.build(game, rp, 'gk' + '0' * 29, out)['archive_sha256'], meta['archive_sha256'])
        # a material the kit room doesn't have is refused with the kit room's names
        r['materials']['grass'] = 'no_such_material'
        rp = self.write('r2.json', json.dumps(r).encode())
        with self.assertRaisesRegex(km.BuildError, 'it has:'):
            km.build(game, rp, 'gk' + '1' * 29, out + '2')


if __name__ == '__main__':
    unittest.main()
