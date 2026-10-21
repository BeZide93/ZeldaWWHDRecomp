#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Kit models: a package's own model drawn with the materials of a room of the game, as a world object.

A world object's model (docs/world-objects.md) normally brings its own pictures. A kit model brings none: like
a mod stage's ground model (docs/mod-stages.md, "Kit materials"), its materials are a kit room's, with that
room's textures, shader archives and texture animations, read from the player's own game files when the game
starts. It is drawn like a game piece (game_piece_model.py): each part with a background light set of the room
the object stands in, so it has that place's light through the day, and it has collision of its own.

The output is a game piece set of one piece (the same files and metadata as game_piece_model.py writes, read by
the same runtime code, story_game_pieces): <out> = Yaz0(SARC) with <identity>.bfres (the kit textures the model
uses, once, and one model per kit model its materials come from: part a from the kit room's model, part b from
its model1, ...), one <part>.sharcfb per part (the kit model's shader archive, which the game looks up by model
name), <out>.<id>.wcol (the collision, WCOL v1 as collision_mesh.encode writes it) and <out>.json (metadata).

The second part, when its kit model has a texture animation in the kit room (falling water, foam, spray), gets
the rows of that animation for its own materials, which the game plays on it, as on the kit room itself (a
model's materials may come from two of the kit room's models). Each part is
lit with the background light set of its kit model's number (model: BG0, model1: BG1, ...), as the kit room's
own models are.

Positions keep the GLB's own origin (metres x 100), as world objects do. Collision: the meshes and nodes named
*_col with col_* materials (docs/world-objects.md), else the visible triangles.

usage: kit_model.py --game GAME --recipe RECIPE.json --identity gk... --out OUT.szs     prints the metadata

The recipe (format 1): {"kit_model_format": 1, "id": "island", "glb": "/abs/island.glb",
                        "kit": {"stage": "sea", "room": 41}, "material": "<kit material>",
                        "materials": {"<glb material>": "<kit material>"}}
Nothing from the game is in this file, in a recipe or in a package: everything of the game comes from the
player's installation at build time and goes only to the player's model cache.
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notebook_model as base  # noqa: E402
import collision_mesh  # noqa: E402
import game_piece_model as gp  # noqa: E402
import stage_builder as sb  # noqa: E402
from skinned_actor_model import append_dictionary, append_string  # noqa: E402

VERSION = 1
FORMAT = 1
BuildError = base.BuildError
MAX_TRIANGLES = 20000          # visible, all parts together (a mod stage room's)
MAX_PART_VERTICES = 65535
MAX_PARTS = 2                  # kit models a model's materials may come from (a game piece draws two parts)
MAX_EXTENT = 20000             # game units from the model's origin (the collision limit of world objects)
MAX_ARCHIVE = gp.MAX_ARCHIVE
STAGE = re.compile(r'[A-Za-z0-9_]{1,7}')
ID = re.compile(r'[a-z0-9]+([-_][a-z0-9]+)*')


def require(ok, message):
    if not ok:
        raise BuildError(message)


def check_recipe(r):
    """the rules story_kit_models.h checks first (the runtime writes the recipe from the manifest)"""
    require(isinstance(r, dict) and r.get('kit_model_format') == FORMAT, 'unsupported kit_model_format')
    require(set(r) <= {'kit_model_format', 'id', 'glb', 'kit', 'material', 'materials'}, 'unknown recipe field')
    require(isinstance(r.get('id'), str) and len(r['id']) <= 32 and ID.fullmatch(r['id']), 'invalid model id')
    require(isinstance(r.get('glb'), str) and r['glb'].endswith('.glb') and os.path.isabs(r['glb']), 'the model must be an absolute .glb path')
    kit = r.get('kit')
    require(isinstance(kit, dict) and set(kit) == {'stage', 'room'} and isinstance(kit['stage'], str) and STAGE.fullmatch(kit['stage']) and
            isinstance(kit['room'], int) and not isinstance(kit['room'], bool) and 0 <= kit['room'] <= 63, 'kit is a stage of the game and a room number')
    require(r.get('material') is None or (isinstance(r['material'], str) and 0 < len(r['material']) <= 64), 'invalid material')
    mats = r.get('materials', {})
    require(isinstance(mats, dict) and len(mats) <= 16 and all(isinstance(k, str) and isinstance(v, str) and 0 < len(k) <= 64 and 0 < len(v) <= 64
                                                               for k, v in mats.items()), 'materials maps up to 16 model materials to kit materials')


def weld(shape):
    """one record per distinct vertex (the GLB's triangles often repeat their corners)"""
    seen, records, pos, remap = {}, [], [], []
    for r, p in zip(shape.records, shape.pos):
        k = seen.get(r)
        if k is None:
            k = seen[r] = len(records)
            records.append(r)
            pos.append(p)
        remap.append(k)
    shape.records, shape.pos = records, pos
    shape.tris = [(remap[a], remap[b], remap[c]) for a, b, c in shape.tris]
    return shape


def kit_collision(prims):
    """WCOL bytes and metadata: the *_col meshes (or the visible triangles), welded, in the model's own units"""
    vertices, triangles, _ = sb.collision_from(prims, [])
    used, out_vertices, out_tris = {}, [], []
    for a, b, c, surface, _exit in triangles:
        row = []
        for k in (a, b, c):
            if k not in used:
                used[k] = len(out_vertices)
                out_vertices.append(vertices[k])
            row.append(used[k])
        out_tris.append((row[0], row[1], row[2], collision_mesh.SEMANTICS[sb.SURFACES[surface]]))
    require(out_tris, 'the model has no collision triangles')
    require(len(out_vertices) <= collision_mesh.MAX_VERTICES and len(out_tris) <= collision_mesh.MAX_TRIANGLES,
            'the collision has %d vertices and %d triangles; a world object\'s collision may have %d of each (give the model a simpler *_col mesh)' %
            (len(out_vertices), len(out_tris), collision_mesh.MAX_TRIANGLES))

    class Api:
        @staticmethod
        def require(ok, message):
            require(ok, message)
    return collision_mesh.encode(out_vertices, out_tris, Api)


def build_set(game, recipe, identity):
    check_recipe(recipe)
    require(re.fullmatch(r'gk[0-9a-f]{29}', identity) is not None, 'invalid kit model identity')
    prims = sb.read_glb(recipe['glb'])
    for p in prims:
        require(all(abs(x) <= MAX_EXTENT for v in p['positions'] for x in v), 'the model reaches farther than %d units from its origin' % MAX_EXTENT)
    kit = recipe['kit']
    files = gp.room_files(game, '%s_Room%d.szs' % (kit['stage'], kit['room']))
    bname = 'Room%d.bfres' % kit['room']
    require(bname in files, 'the kit room archive has no room model')
    data = files[bname]
    src = base.Bfres(data)
    mats = sb.kit_materials(src)
    require(mats, 'the kit room has no background model')
    default = recipe.get('material') or sb.main_material(src)
    require(default in mats, 'the kit room has no material "%s" (it has: %s)' % (default, ', '.join(sorted(mats))))
    mapping = recipe.get('materials', {})
    for kit_name in mapping.values():
        require(kit_name in mats, 'the kit room has no material "%s" (it has: %s)' % (kit_name, ', '.join(sorted(mats))))
    # the visible primitives, merged per kit material, grouped by the kit model the material belongs to
    merged, tris = {}, 0
    for p in prims:
        if p['collision']:
            continue
        kit_name = mapping.get(p['material']) or (p['material'] if p['material'] in mats else default)
        m = merged.setdefault(kit_name, {'positions': [], 'normals': [], 'uvs': [], 'colors': [], 'triangles': [], 'has_colors': False})
        at = len(m['positions'])
        m['positions'] += p['positions']
        m['normals'] += p['normals']
        m['uvs'] += p['uvs']
        m['colors'] += p['colors'] if p['colors'] else [(1.0, 1.0, 1.0, 1.0)] * len(p['positions'])
        m['has_colors'] |= bool(p['colors'])
        m['triangles'] += [(a + at, b + at, c + at) for a, b, c in p['triangles']]
        tris += len(p['triangles'])
    require(0 < tris <= MAX_TRIANGLES, 'the model has %d visible triangles; a kit model may have 1 to %d' % (tris, MAX_TRIANGLES))
    by_model = {}
    for kit_name, m in sorted(merged.items()):
        if not m['has_colors']:
            m['colors'] = None
        kit_model, template = mats[kit_name]
        by_model.setdefault(kit_model, []).append(weld(sb.mesh_shape(src, template, m)))
    require(len(by_model) <= MAX_PARTS, 'the model uses materials of more than %d kit models' % MAX_PARTS)
    kit_models = [n for n, _ in src.dict(src.rel(0x20))]
    order = sorted(by_model, key=kit_models.index)
    # the output: the textures of the used materials once, then one model per part
    out = base.Bfres(data[:0x70])
    out.b[0x18:0x68] = bytes(0x50)
    w = gp.Writer(data, out)
    loaded = {n: gp.read_model(src, n) for n in order}
    texture_names = []
    for n in order:
        mat_at = dict(loaded[n][2])
        for s in by_model[n]:
            m = mat_at[s.material]
            refs = src.rel(m + 0x28)
            for i in range(src.b[m + 0x10]):
                t = src.cstr(src.rel(refs + 8 * i))
                if t not in texture_names:
                    texture_names.append(t)
    all_textures = dict(src.dict(src.rel(0x24)))
    textures, texture_bytes = {}, 0
    for t in texture_names:
        require(t in all_textures, 'a kit material names a missing texture')
        textures[t], size = gp.copy_texture(w, all_textures[t])
        texture_bytes += size
    out.set_rel(0x24, append_dictionary(out, [(t, textures[t]) for t in texture_names]))
    out.put('H', 0x52, len(texture_names))
    animated = dict(src.dict(src.rel(0x34))) if src.rel(0x34) else {}
    models, shaders, parts, animations = [], {}, [], []
    lo, hi = [1e30] * 3, [-1e30] * 3
    for k, kit_model in enumerate(order):
        shapes = by_model[kit_model]
        for s in shapes:
            require(len(s.records) <= MAX_PART_VERTICES, 'a part of the model has more than %d vertices' % MAX_PART_VERTICES)
            for p in s.pos:
                for j in range(3):
                    lo[j], hi[j] = min(lo[j], p[j]), max(hi[j], p[j])
        model_at, _, materials = loaded[kit_model]
        name = identity[:2] + identity[-4:] + 'abcd'[k]
        dst, count, used = gp.write_model(w, model_at, name, shapes, {i: s.tris for i, s in enumerate(shapes)}, materials, textures, (0.0, 0.0, 0.0))
        models.append((name, dst))
        archive = kit_model + '.sharcfb'
        require(archive in files, 'the kit room has no shader archive ' + archive)
        shaders[name + '.sharcfb'] = files[archive]
        light = kit_models.index(kit_model)
        require(light <= 3, 'a kit model past the fourth has no background light set')
        part = {'model': name, 'light': light + 1, 'triangles': count, 'materials': used, 'kit_model': kit_model}
        # the second part's texture animation plays (a game piece animates its second part, story_game_pieces.h)
        if k == 1 and kit_model in animated:
            animation = gp.copy_animation(w, kit_model, used, name, dst)
            if animation is not None:
                animations.append((name, animation))
                part['animation'] = name
        parts.append(part)
    out.set_rel(0x20, append_dictionary(out, models))
    out.put('H', 0x50, len(models))
    if animations:
        out.set_rel(0x34, append_dictionary(out, animations))
        out.put('H', 0x5A, len(animations))
    out.set_rel(0x14, append_string(out, identity))
    out.append(b'', 0x1000)
    out.put('I', 0xC, len(out.b))
    bfres = bytes(out.b)
    members = {identity + '.bfres': bfres}
    members.update(shaders)
    packed = base.sarc(members)
    require(len(packed) <= MAX_ARCHIVE, 'the model\'s archive exceeds 12 MiB (its kit textures and models)')
    collision, collision_info = kit_collision(prims)
    piece = {'id': recipe['id'], 'parts': parts, 'origin': [0.0, 0.0, 0.0], 'minimum': lo, 'maximum': hi, 'collision': collision_info}
    meta = {'set': 'kit:' + recipe['id'], 'identity': identity, 'pieces': [piece], 'models': [m for m, _ in models],
            'animations': [a for a, _ in animations], 'texture_bytes': texture_bytes, 'shader_bytes': sum(len(v) for v in shaders.values()),
            'bfres_bytes': len(bfres), 'archive_bytes': len(packed), 'triangles': tris}
    return packed, collision, meta


def build(game, recipe_path, identity, out):
    with open(recipe_path, 'rb') as f:
        recipe_bytes = f.read(64 * 1024 + 1)
    require(len(recipe_bytes) <= 64 * 1024, 'recipe too large')
    try:
        recipe = json.loads(recipe_bytes)
    except ValueError as exc:
        raise BuildError('the recipe is not JSON') from exc
    check_recipe(recipe)
    digest = hashlib.sha256()
    for path in (__file__, gp.__file__, sb.__file__, collision_mesh.__file__):
        with open(path, 'rb') as f:
            digest.update(f.read())
    digest.update(identity.encode() + b'\0' + recipe_bytes)
    with open(recipe['glb'], 'rb') as f:
        digest.update(hashlib.sha256(f.read()).digest())
    kit = recipe['kit']
    room = gp.room_files(game, '%s_Room%d.szs' % (kit['stage'], kit['room']))
    for name in sorted(room):
        digest.update(name.encode() + b'\0' + hashlib.sha256(room[name]).digest())
    cached = gp.cached_meta(out, digest.hexdigest())
    if cached and cached.get('converter_version') == gp.VERSION and cached.get('kit_converter_version') == VERSION:
        return cached
    packed, collision, meta = build_set(game, recipe, identity)
    archive = base.yaz0_stored(packed)
    meta.update(converter_version=gp.VERSION, kit_converter_version=VERSION, cache_key=digest.hexdigest(),
                archive_sha256=hashlib.sha256(archive).hexdigest())
    directory = os.path.dirname(os.path.abspath(out))
    os.makedirs(directory, exist_ok=True)

    def publish(path, body):
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=directory, prefix='.kit-model-', delete=False) as target:
                temporary = target.name
                target.write(body)
            os.replace(temporary, path)
            temporary = None
        finally:
            if temporary is not None:
                os.unlink(temporary)
    publish(out, archive)
    piece = meta['pieces'][0]
    path = '%s.%s.wcol' % (out, piece['id'])
    publish(path, collision)
    piece['collision_file'] = os.path.basename(path)
    publish(out + '.json', (json.dumps(meta, sort_keys=True) + '\n').encode())
    return meta


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', required=True)
    parser.add_argument('--recipe', required=True)
    parser.add_argument('--identity', required=True)
    parser.add_argument('--out', required=True)
    args = parser.parse_args()
    try:
        meta = build(args.game, args.recipe, args.identity, args.out)
    except (BuildError, OSError, KeyError, IndexError, struct.error, ValueError) as exc:
        parser.exit(1, 'kit_model: %s\n' % exc)
    print(json.dumps(meta, sort_keys=True))


if __name__ == '__main__':
    main()
