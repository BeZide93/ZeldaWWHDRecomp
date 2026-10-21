#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Reproduce assets/island.glb and assets/arena.glb, the two own rooms of the sky island example; no external
assets. Metres, as in any .glb (the game counts 100 units per metre).

island (room 0): a floating island about 44 m across. Its grass top rises a little to the middle, where a round
clearing of trodden earth lies; an earth path leads north from the clearing up a ramp to the arena's gate. A
cliff with grass hanging over its edge runs round the rim, and a rocky underside narrows to a point 26 m below.
arena (room 1): an open-air ring 24 m across north of the island (centre z 36), its floor at y 2 paved in rings
of flagstones round a stone rosette, walled 5 m high, with a gateway (3 m wide) in its south wall facing the
island.

The game draws the models with the kit rooms' materials (see the manifest), many of which are texture atlases:
one picture holds several surfaces in bands (grass, a round clearing, a path strip and sand on Outset's ground
texture). Each surface's texture coordinates stay inside its band and repeat only along it; the ATLAS table
names the bands. Vertex colours shade the model the way the game's own rooms are shaded (darker under the rim
and towards the underside's point). The visible triangles are the collision."""
import json
import math
import struct
from pathlib import Path

R_TOP = 22.0       # the island's radius
GATE_Z = 22.0      # where the path meets the arena's gateway
ARENA_Z, ARENA_R, ARENA_Y, WALL_H = 36.0, 12.0, 2.0, 5.0
GATE_HALF, GATE_TOP = 1.5, 3.0
CLEARING_R = 5.0   # the clearing in the island's middle
PATH_HALF = 1.6    # half the path's width

# texture coordinate bands of the kit textures (u, v from the picture's top left), by the model's material names:
# the island's (kit: Outset's sea room) and the arena's (kit: the Tower of the Gods' outside room 0)
ATLAS = {
    'grass': {'v': (0.03, 0.21)},             # the ground picture's grass band
    'clearing': {'v': (0.255, 0.495)},        # the round clearing (its whole band is one picture)
    'path': {'v': (0.535, 0.715)},            # the earth path strip, repeating along u
    'cliff': {'v': (0.612, 1.0)},             # the rock wall with grass hanging over its top, repeating along u
    'paving': {'u': (0.218, 0.348)},          # a column of large flagstones, repeating along v
    'rosette': {'u': (0.357, 0.581), 'v': (0.527, 0.977)},  # the round stone rosette
}


def wobble(a, seed):
    """a fixed, hand-made irregularity of the rim (no randomness, the file is reproducible)"""
    return 1.0 + 0.08 * math.sin(3 * a + seed) + 0.05 * math.sin(7 * a + 2 * seed) + 0.03 * math.sin(13 * a)


def lerp(band, t):
    return band[0] + (band[1] - band[0]) * t


def tri(parts, material, a, b, c):
    """a triangle of vertices (position, uv, shade); its normal from the winding (counter-clockwise faces out)"""
    pos, nrm, uv, col, idx = parts.setdefault(material, ([], [], [], [], []))
    pa, pb, pc = a[0], b[0], c[0]
    u = [pb[k] - pa[k] for k in range(3)]
    v = [pc[k] - pa[k] for k in range(3)]
    n = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    ln = math.sqrt(sum(x * x for x in n)) or 1.0
    n = tuple(x / ln for x in n)
    k = len(pos)
    for p, t, s in (a, b, c):
        pos.append(tuple(p))
        nrm.append(n)
        uv.append(tuple(t))
        col.append((s, s, s, 1.0))
    idx.extend((k, k + 1, k + 2))


def quad(parts, material, a, b, c, d):
    tri(parts, material, a, b, c)
    tri(parts, material, a, c, d)


# ---- the island ---------------------------------------------------------------------------------------------

def height(x, z):
    """the grass top's height: 0.6 m in the middle, 0 at the rim"""
    f = min(1.0, math.hypot(x, z) / R_TOP)
    return 0.6 * (1 - f * f)


GRASS_U = 6.0      # metres the grass picture's width covers
GRASS_V = 4.3      # metres its band covers the other way (about as many texels a metre as along u)


def mirrored(t):
    """0 -> 1 -> 0 -> ...: a band that can't repeat is walked forwards and back (the vertices are closer than
    one band, so the turns stay sharp)"""
    t = t % 2.0
    return t if t <= 1.0 else 2.0 - t


def grass_vertex(p):
    x, y, z = p
    f = min(1.0, math.hypot(x, z) / R_TOP)
    shade = 1.0 - 0.18 * f ** 3 - 0.06 * max(0.0, math.sin(0.35 * x) * math.sin(0.3 * z))  # patches, a darker rim
    return (p, (x / GRASS_U, lerp(ATLAS['grass']['v'], mirrored((z + 100) / GRASS_V))), shade)


def path_height(z):
    """the path: on the grass, then a ramp up to the gateway's floor"""
    ramp = ARENA_Y - (GATE_Z - z) * 0.22
    return max(height(0.0, z) + 0.05, ramp)


def island():
    parts = {}
    seg, rings = 96, 16  # vertices about 1.4 m apart: closer than the grass band's turns

    def rim(a):
        return R_TOP * wobble(a, 1.3)

    def top(r_frac, a):
        r = rim(a) * r_frac
        x, z = r * math.cos(a), r * math.sin(a)
        return (x, height(x, z) if r_frac < 1 else 0.0, z)
    # the grass top, in rings round the middle (counter-clockwise seen from above: normals up)
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        tri(parts, 'grass', grass_vertex(top(0, 0)), grass_vertex(top(1 / rings, a1)), grass_vertex(top(1 / rings, a0)))
        for k in range(1, rings):
            f0, f1 = k / rings, (k + 1) / rings
            quad(parts, 'grass', grass_vertex(top(f0, a0)), grass_vertex(top(f0, a1)), grass_vertex(top(f1, a1)), grass_vertex(top(f1, a0)))

    # the clearing: a disc a little above the grass with the atlas's round clearing on it
    def clearing_vertex(x, z):
        u = 0.5 + 0.5 * x / CLEARING_R
        v = lerp(ATLAS['clearing']['v'], 0.5 + 0.5 * z / CLEARING_R)
        return ((x, height(x, z) + 0.03, z), (u, v), 0.97)
    cseg = 32
    for i in range(cseg):
        a0, a1 = 2 * math.pi * i / cseg, 2 * math.pi * (i + 1) / cseg
        for k in range(2):
            r0, r1 = CLEARING_R * k / 2, CLEARING_R * (k + 1) / 2
            p = [clearing_vertex(r * math.cos(a), r * math.sin(a)) for r, a in ((r0, a0), (r0, a1), (r1, a1), (r1, a0))]
            if k == 0:
                tri(parts, 'clearing', p[0], p[2], p[3])
            else:
                quad(parts, 'clearing', p[0], p[1], p[2], p[3])
    # the cliff round the rim (grass hangs over its top) and the rocky underside down to the point
    under = [(1.0, 0.0), (1.02, -2.5), (0.9, -6.0), (0.7, -11.0), (0.45, -17.0), (0.2, -22.0)]

    def ring(j, a):
        f, y = under[j]
        r = rim(a) * f * (wobble(a, 2.0 + 0.7 * j) / wobble(a, 1.3) if j else 1.0)
        return (r * math.cos(a), y, r * math.sin(a))
    circumference = 2 * math.pi * R_TOP
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        u0, u1 = circumference * i / seg / 7.0, circumference * (i + 1) / seg / 7.0  # the cliff picture every 7 m
        for j in range(len(under) - 1):
            y0, y1 = under[j][1], under[j + 1][1]
            if j < 2:  # the cliff: the picture's band from the hanging grass (top) down 6 m
                v0, v1 = lerp(ATLAS['cliff']['v'], -y0 / 6.0), lerp(ATLAS['cliff']['v'], -y1 / 6.0)
                s0, s1 = 0.95 + 0.25 * y0 / 6, 0.95 + 0.25 * y1 / 6
                quad(parts, 'cliff', (ring(j, a1), (u1, v0), s0), (ring(j + 1, a1), (u1, v1), s1),
                     (ring(j + 1, a0), (u0, v1), s1), (ring(j, a0), (u0, v0), s0))
            else:  # the underside: plain rock, repeating every 6 m, darker towards the point
                s0, s1 = 0.75 + 0.45 * y0 / 26, 0.75 + 0.45 * y1 / 26
                quad(parts, 'rock', (ring(j, a1), (u1 * 7 / 6, -y0 / 6), s0), (ring(j + 1, a1), (u1 * 7 / 6, -y1 / 6), s1),
                     (ring(j + 1, a0), (u0 * 7 / 6, -y1 / 6), s1), (ring(j, a0), (u0 * 7 / 6, -y0 / 6), s0))
        last = len(under) - 1
        tri(parts, 'rock', (ring(last, a1), (u1 * 7 / 6, -under[last][1] / 6), 0.37), ((0.0, -26.0, 0.0), ((u0 + u1) * 7 / 12, 26 / 6), 0.3),
            (ring(last, a0), (u0 * 7 / 6, -under[last][1] / 6), 0.37))
    # the path north from the clearing to the gateway, 3.2 m wide; where it runs above the grass (the ramp) its
    # sides are rock down into the ground (beyond the rim, a rock bridge)
    z = CLEARING_R - 0.6
    v_left, v_right = ATLAS['path']['v']
    while z < GATE_Z - 1e-6:
        z1 = min(z + 1.0, GATE_Z)
        h0, h1 = path_height(z), path_height(z1)
        quad(parts, 'path', ((-PATH_HALF, h0, z), (z / 4.0, v_left), 0.95), ((-PATH_HALF, h1, z1), (z1 / 4.0, v_left), 0.95),
             ((PATH_HALF, h1, z1), (z1 / 4.0, v_right), 0.95), ((PATH_HALF, h0, z), (z / 4.0, v_right), 0.95))
        g0, g1 = height(PATH_HALF, z) - 0.3, height(PATH_HALF, z1) - 0.3
        if z >= R_TOP * 0.85:
            g0, g1 = h0 - 1.6, h1 - 1.6
        if h0 > g0 + 0.4 or h1 > g1 + 0.4:
            for x, flip in ((-PATH_HALF, False), (PATH_HALF, True)):
                corners = ((x, g0, z), (x, h0, z), (x, h1, z1), (x, g1, z1))
                q = [(p, (p[2] / 5.0, (h1 - p[1]) / 5.0), 0.9 if p[1] >= min(h0, h1) - 0.01 else 0.65) for p in corners]
                quad(parts, 'rock', *(q[::-1] if flip else q))
        z = z1
    return parts


# ---- the arena ----------------------------------------------------------------------------------------------

def arena():
    parts = {}
    seg = 48
    gate_a = -math.pi / 2  # the gateway faces the island (south, -z)

    def at(r, a, y):
        return (r * math.cos(a), y, ARENA_Z + r * math.sin(a))
    # the floor: a rosette in the middle, then rings of flagstones; each ring takes the flagstone column across its
    # width and repeats it along the ring
    rosette_r = 3.0
    ru, rv = ATLAS['rosette']['u'], ATLAS['rosette']['v']
    pu0, pu1 = ATLAS['paving']['u']
    bands = [rosette_r, 6.0, 9.0, ARENA_R]
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg

        def rose(r, a):
            x, z = r * math.cos(a), r * math.sin(a)
            return (at(r, a, ARENA_Y), (lerp(ru, 0.5 + 0.48 * x / rosette_r), lerp(rv, 0.5 - 0.48 * z / rosette_r)), 1.0)
        tri(parts, 'rosette', rose(0, 0), rose(rosette_r, a1), rose(rosette_r, a0))
        for k in range(len(bands) - 1):
            r0, r1 = bands[k], bands[k + 1]
            per_m = 0.087 * 3.0 / (r1 - r0)  # square flagstones whatever the ring's width
            v0, v1 = r1 * a0 * per_m, r1 * a1 * per_m
            shade = 0.97 - 0.05 * k
            quad(parts, 'paving', (at(r0, a1, ARENA_Y), (pu0, v1), shade), (at(r1, a1, ARENA_Y), (pu1, v1), shade),
                 (at(r1, a0, ARENA_Y), (pu1, v0), shade), (at(r0, a0, ARENA_Y), (pu0, v0), shade))
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        u0, u1 = ARENA_R * a0 / 5.0, ARENA_R * a1 / 5.0
        mid = (a0 + a1) / 2
        gap = abs(math.atan2(math.sin(mid - gate_a), math.cos(mid - gate_a))) * ARENA_R < GATE_HALF
        lo = ARENA_Y + (GATE_TOP if gap else 0.0)
        top = ARENA_Y + WALL_H
        vl = (top - lo) / WALL_H
        # the wall, facing inwards, open at the gateway
        quad(parts, 'wall', (at(ARENA_R, a1, lo), (u1, vl), 0.75), (at(ARENA_R, a1, top), (u1, 0.0), 1.0),
             (at(ARENA_R, a0, top), (u0, 0.0), 1.0), (at(ARENA_R, a0, lo), (u0, vl), 0.75))
        # the wall's outside and top (seen from the island)
        quad(parts, 'wall', (at(ARENA_R + 0.8, a0, lo), (u0, vl), 0.75), (at(ARENA_R + 0.8, a0, top), (u0, 0.0), 1.0),
             (at(ARENA_R + 0.8, a1, top), (u1, 0.0), 1.0), (at(ARENA_R + 0.8, a1, lo), (u1, vl), 0.75))
        quad(parts, 'paving', (at(ARENA_R, a1, top), (pu0, u1), 1.0), (at(ARENA_R + 0.8, a1, top), (pu1, u1), 1.0),
             (at(ARENA_R + 0.8, a0, top), (pu1, u0), 1.0), (at(ARENA_R, a0, top), (pu0, u0), 1.0))
        if gap:  # the gateway's lintel underside
            quad(parts, 'paving', (at(ARENA_R + 0.8, a1, lo), (pu1, u1), 0.7), (at(ARENA_R, a1, lo), (pu0, u1), 0.7),
                 (at(ARENA_R, a0, lo), (pu0, u0), 0.7), (at(ARENA_R + 0.8, a0, lo), (pu1, u0), 0.7))
        # the ring's rock base below the floor
        quad(parts, 'rock', (at(ARENA_R + 0.8, a0, ARENA_Y - 6), (u0, 1.2), 0.5), (at(ARENA_R + 0.8, a0, ARENA_Y), (u0, 0.0), 0.9),
             (at(ARENA_R + 0.8, a1, ARENA_Y), (u1, 0.0), 0.9), (at(ARENA_R + 0.8, a1, ARENA_Y - 6), (u1, 1.2), 0.5))
    # the gateway's floor between the island's path and the ring
    z0, z1 = GATE_Z, ARENA_Z - ARENA_R + 0.5
    quad(parts, 'paving', ((-GATE_HALF, ARENA_Y, z0), (pu0, z0 * 0.087), 0.9), ((-GATE_HALF, ARENA_Y, z1), (pu0, z1 * 0.087), 0.9),
         ((GATE_HALF, ARENA_Y, z1), (pu1, z1 * 0.087), 0.9), ((GATE_HALF, ARENA_Y, z0), (pu1, z0 * 0.087), 0.9))
    return parts


# ---- the .glb -----------------------------------------------------------------------------------------------

def glb(parts):
    binary = bytearray()
    views, accessors, primitives, materials = [], [], [], []

    def view(body):
        binary.extend(b'\0' * (-len(binary) % 4))
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)})
        binary.extend(body)
        return len(views) - 1

    def floats(rows, width):
        accessors.append({'bufferView': view(struct.pack('<%df' % (len(rows) * width), *(x for r in rows for x in r))), 'componentType': 5126,
                          'count': len(rows), 'type': 'VEC%d' % width, 'min': [min(r[k] for r in rows) for k in range(width)],
                          'max': [max(r[k] for r in rows) for k in range(width)]})
        return len(accessors) - 1
    for name, (pos, nrm, uv, col, idx) in sorted(parts.items()):
        p, n, t, c = floats(pos, 3), floats(nrm, 3), floats(uv, 2), floats(col, 4)
        accessors.append({'bufferView': view(struct.pack('<%dH' % len(idx), *idx)), 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'})
        materials.append({'name': name})
        primitives.append({'attributes': {'POSITION': p, 'NORMAL': n, 'TEXCOORD_0': t, 'COLOR_0': c}, 'indices': len(accessors) - 1,
                           'material': len(materials) - 1})
    doc = {'asset': {'version': '2.0', 'generator': 'Original sky island example, MPL-2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
           'nodes': [{'mesh': 0, 'name': 'room'}], 'meshes': [{'name': 'room', 'primitives': primitives}], 'materials': materials,
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    return b'glTF' + struct.pack('<II', 2, 12 + 8 + len(text) + 8 + len(binary)) + struct.pack('<I4s', len(text), b'JSON') + text + \
        struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)


# ---- the island far off (models/island-distant.glb) ----------------------------------------------------------------
# The island and its arena as the bird scene shows them in Outset's sky: a low-detail model of both with small
# pictures of its own (the kit textures belong to the stage build, not to a world object), placed as a world
# object of the package. Same metres and origin as island.glb, the arena north of it as in room 1.

def png(w, h, pixel):
    """a PNG (8-bit RGB) of pixel(x, y) -> (r, g, b)"""
    import zlib
    rows = b''.join(b'\0' + bytes(c for x in range(w) for c in pixel(x, y)) for y in range(h))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')


def noise(x, y, seed):
    """a fixed texture wobble in -1..1 that tiles every 64 texels"""
    t = 2 * math.pi / 64
    return 0.5 * math.sin(t * (3 * x + 2 * y) + seed) + 0.3 * math.sin(t * (5 * y - 4 * x) + 2 * seed) + 0.2 * math.sin(t * (11 * x + 7 * y))


def shade(base, f):
    return tuple(max(0, min(255, int(c * f))) for c in base)


def cliff_texel(x, y):
    if y < 6 + 2 * math.sin(x * 2 * math.pi / 16):  # grass hanging over the top rows
        return shade((96, 178, 64), 0.9 + 0.1 * noise(x, y, 2.0))
    return shade((150, 128, 104), (0.95 - 0.15 * ((y // 10) % 2)) * (1.0 + 0.07 * noise(x, y, 0.7)))


PICTURES = {
    'grass': lambda x, y: shade((96, 178, 64), 1.0 + 0.10 * noise(x, y, 0.4)),
    'earth': lambda x, y: shade((182, 146, 98), 1.0 + 0.08 * noise(x, y, 1.1)),
    'cliff': cliff_texel,
    'rock': lambda x, y: shade((104, 92, 84), 1.0 + 0.12 * noise(x, y, 3.3)),
    'stone': lambda x, y: shade((196, 188, 170), (0.85 if x % 16 == 0 or y % 16 == 0 else 1.0) * (1.0 + 0.05 * noise(x, y, 4.1))),
}


def distant():
    parts = {}
    seg = 40

    def rim(a):
        return R_TOP * wobble(a, 1.3)
    under = [(1.0, 0.0), (1.02, -3.0), (0.85, -8.0), (0.55, -15.0), (0.25, -21.0)]

    def side(j, a):
        f, y = under[j]
        r = rim(a) * f
        return (r * math.cos(a), y, r * math.sin(a))
    for i in range(seg):
        a0, a1 = 2 * math.pi * i / seg, 2 * math.pi * (i + 1) / seg
        ring = [0.0, 0.3, 0.65, 1.0]
        for k in range(len(ring) - 1):
            pts = []
            for f, a in ((ring[k], a0), (ring[k], a1), (ring[k + 1], a1), (ring[k + 1], a0)):
                r = rim(a) * f
                x, z = r * math.cos(a), r * math.sin(a)
                pts.append(((x, height(x, z), z), (x / 4.0, z / 4.0), 1.0))
            if k == 0:
                tri(parts, 'grass', pts[0], pts[2], pts[3])
            else:
                quad(parts, 'grass', *pts)
        u0, u1 = 2 * math.pi * R_TOP * i / seg / 7.0, 2 * math.pi * R_TOP * (i + 1) / seg / 7.0
        for j in range(len(under) - 1):
            y0, y1 = under[j][1], under[j + 1][1]
            quad(parts, 'cliff' if j == 0 else 'rock', (side(j, a1), (u1, -y0 / 6), 1.0), (side(j + 1, a1), (u1, -y1 / 6), 1.0),
                 (side(j + 1, a0), (u0, -y1 / 6), 1.0), (side(j, a0), (u0, -y0 / 6), 1.0))
        last = len(under) - 1
        tri(parts, 'rock', (side(last, a1), (u1, 3.5), 1.0), ((0.0, -26.0, 0.0), ((u0 + u1) / 2, 4.3), 1.0), (side(last, a0), (u0, 3.5), 1.0))
    # the clearing and the path, a little above the grass
    top = height(0, 0) + 0.05
    for i in range(16):
        a0, a1 = 2 * math.pi * i / 16, 2 * math.pi * (i + 1) / 16
        tri(parts, 'earth', ((0.0, top, 0.0), (0.0, 0.0), 1.0), ((CLEARING_R * math.cos(a1), top, CLEARING_R * math.sin(a1)), (math.cos(a1), math.sin(a1)), 1.0),
            ((CLEARING_R * math.cos(a0), top, CLEARING_R * math.sin(a0)), (math.cos(a0), math.sin(a0)), 1.0))
    z = CLEARING_R - 0.5
    while z < GATE_Z - 1e-6:
        z1 = min(z + 3.0, GATE_Z)
        h0, h1 = path_height(z) + 0.05, path_height(z1) + 0.05
        quad(parts, 'earth', ((-PATH_HALF, h0, z), (0, z / 3), 1.0), ((-PATH_HALF, h1, z1), (0, z1 / 3), 1.0),
             ((PATH_HALF, h1, z1), (1, z1 / 3), 1.0), ((PATH_HALF, h0, z), (1, z / 3), 1.0))
        z = z1
    # the arena: its floor, its wall (both faces, open at the gateway) and the rock below it

    def at(r, a, y):
        return (r * math.cos(a), y, ARENA_Z + r * math.sin(a))
    aseg = 32
    for i in range(aseg):
        a0, a1 = 2 * math.pi * i / aseg, 2 * math.pi * (i + 1) / aseg
        tri(parts, 'stone', (at(0, 0, ARENA_Y), (0, 0), 1.0), (at(ARENA_R, a1, ARENA_Y), (math.cos(a1) * 3, math.sin(a1) * 3), 1.0),
            (at(ARENA_R, a0, ARENA_Y), (math.cos(a0) * 3, math.sin(a0) * 3), 1.0))
        mid = (a0 + a1) / 2
        gap = abs(math.atan2(math.sin(mid + math.pi / 2), math.cos(mid + math.pi / 2))) * ARENA_R < GATE_HALF
        lo, high = ARENA_Y + (GATE_TOP if gap else 0.0), ARENA_Y + WALL_H
        u0, u1 = ARENA_R * a0 / 4, ARENA_R * a1 / 4
        quad(parts, 'stone', (at(ARENA_R, a1, lo), (u1, 1), 1.0), (at(ARENA_R, a1, high), (u1, 0), 1.0), (at(ARENA_R, a0, high), (u0, 0), 1.0), (at(ARENA_R, a0, lo), (u0, 1), 1.0))
        quad(parts, 'stone', (at(ARENA_R + 0.8, a0, lo), (u0, 1), 1.0), (at(ARENA_R + 0.8, a0, high), (u0, 0), 1.0),
             (at(ARENA_R + 0.8, a1, high), (u1, 0), 1.0), (at(ARENA_R + 0.8, a1, lo), (u1, 1), 1.0))
        quad(parts, 'rock', (at(ARENA_R + 0.8, a0, ARENA_Y - 8), (u0, 1.3), 1.0), (at(ARENA_R + 0.8, a0, ARENA_Y), (u0, 0), 1.0),
             (at(ARENA_R + 0.8, a1, ARENA_Y), (u1, 0), 1.0), (at(ARENA_R + 0.8, a1, ARENA_Y - 8), (u1, 1.3), 1.0))
        tri(parts, 'rock', (at(ARENA_R + 0.8, a1, ARENA_Y - 8), (u1, 1.3), 1.0), ((0.0, ARENA_Y - 14, ARENA_Z), ((u0 + u1) / 2, 2.0), 1.0),
            (at(ARENA_R + 0.8, a0, ARENA_Y - 8), (u0, 1.3), 1.0))
    return parts


def textured_glb(parts):
    """a world object's .glb: one mesh, a material per part with its own embedded 64 x 64 PNG picture"""
    binary = bytearray()
    views, accessors, primitives, materials, images, textures = [], [], [], [], [], []

    def view(body):
        binary.extend(b'\0' * (-len(binary) % 4))
        views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(body)})
        binary.extend(body)
        return len(views) - 1

    def floats(rows, width):
        accessors.append({'bufferView': view(struct.pack('<%df' % (len(rows) * width), *(x for r in rows for x in r))), 'componentType': 5126,
                          'count': len(rows), 'type': 'VEC%d' % width, 'min': [min(r[k] for r in rows) for k in range(width)],
                          'max': [max(r[k] for r in rows) for k in range(width)]})
        return len(accessors) - 1
    for name, (pos, nrm, uv, col, idx) in sorted(parts.items()):
        images.append({'bufferView': view(png(64, 64, PICTURES[name])), 'mimeType': 'image/png'})
        textures.append({'source': len(images) - 1, 'sampler': 0})
        materials.append({'name': name, 'pbrMetallicRoughness': {'baseColorTexture': {'index': len(textures) - 1}, 'metallicFactor': 0.0}})
        p, n, t = floats(pos, 3), floats(nrm, 3), floats(uv, 2)
        accessors.append({'bufferView': view(struct.pack('<%dH' % len(idx), *idx)), 'componentType': 5123, 'count': len(idx), 'type': 'SCALAR'})
        primitives.append({'attributes': {'POSITION': p, 'NORMAL': n, 'TEXCOORD_0': t}, 'indices': len(accessors) - 1, 'material': len(materials) - 1})
    doc = {'asset': {'version': '2.0', 'generator': 'Original sky island example, MPL-2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
           'nodes': [{'mesh': 0, 'name': 'island-distant'}], 'meshes': [{'name': 'island-distant', 'primitives': primitives}],
           'materials': materials, 'images': images, 'textures': textures, 'samplers': [{'wrapS': 10497, 'wrapT': 10497}],
           'buffers': [{'byteLength': len(binary)}], 'bufferViews': views, 'accessors': accessors}
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    binary.extend(b'\0' * (-len(binary) % 4))
    return b'glTF' + struct.pack('<II', 2, 12 + 8 + len(text) + 8 + len(binary)) + struct.pack('<I4s', len(text), b'JSON') + text + \
        struct.pack('<I4s', len(binary), b'BIN\0') + bytes(binary)


if __name__ == '__main__':
    here = Path(__file__).resolve().parent
    for name, make in (('island', island), ('arena', arena)):
        parts = make()  # in metres, as a .glb is (the game's units are centimetres)
        (here / (name + '.glb')).write_bytes(glb(parts))
        print(name, sum(len(p[4]) // 3 for p in parts.values()), 'triangles', sorted(parts))
    parts = distant()  # a world object's model: the package's models folder, where the editor's import_model puts models
    (here.parent / 'models').mkdir(exist_ok=True)
    (here.parent / 'models' / 'island-distant.glb').write_bytes(textured_glb(parts))
    print('island-distant', sum(len(p[4]) // 3 for p in parts.values()), 'triangles,', sum(len(p[0]) for p in parts.values()), 'vertices', sorted(parts))
