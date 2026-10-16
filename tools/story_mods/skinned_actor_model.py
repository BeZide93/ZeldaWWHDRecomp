#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Bounded original two-influence GLB -> player-local HD skinned BFRES."""
import argparse
import copy
import hashlib
import json
import math
import os
import re
import struct
import tempfile
import actor_model as static

base = static.base
BuildError = static.BuildError
require = static.require
VERSION = 2
ARCHIVE = 'content/Common/Object/Fobj00.szs'
MAX_JOINTS = 32


def document(path):
    require(os.path.getsize(path) <= static.MAX_SOURCE, 'GLB exceeds 8 MiB')
    with open(path, 'rb') as source: data = source.read(static.MAX_SOURCE + 1)
    require(len(data) >= 28 and data[:4] == b'glTF', 'expected GLB')
    require(struct.unpack_from('<II', data, 4) == (2, len(data)), 'invalid GLB header')
    chunks, at = [], 12
    while at < len(data):
        require(at + 8 <= len(data), 'truncated chunk')
        count, kind = struct.unpack_from('<I4s', data, at)
        require(count % 4 == 0 and count <= len(data)-at-8, 'invalid chunk')
        chunks.append((kind, data[at+8:at+8+count])); at += 8+count
    require(len(chunks) == 2 and [x[0] for x in chunks] == [b'JSON', b'BIN\0'], 'expected JSON and BIN')
    return json.loads(chunks[0][1]), chunks[1][1]


def pack_glb(doc, binary):
    text = json.dumps(doc, separators=(',', ':')).encode(); text += b' ' * (-len(text) % 4)
    binary += b'\0' * (-len(binary) % 4)
    body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(binary), b'BIN\0') + binary
    return b'glTF' + struct.pack('<II', 2, 12+len(body)) + body


def accessor(doc, binary, index, shape, types):
    index = static.integer(index); require(index < len(doc['accessors']), 'invalid accessor index')
    a = doc['accessors'][index]
    require(a.get('type') == shape and a.get('componentType') in types and not a.get('normalized') and 'sparse' not in a, 'unsupported skin accessor')
    count = static.integer(a['count'], 1); require(count <= static.MAX_VERTICES, 'accessor limit')
    vi = static.integer(a['bufferView']); require(vi < len(doc['bufferViews']), 'invalid buffer view')
    v = doc['bufferViews'][vi]; require(v.get('buffer', 0) == 0, 'invalid buffer')
    start, length = static.integer(v.get('byteOffset', 0)), static.integer(v['byteLength'])
    declared = static.integer(doc['buffers'][0]['byteLength'])
    require(start <= declared and length <= declared-start and declared <= len(binary), 'view outside buffer')
    code, size = {5126: ('f', 4), 5123: ('H', 2), 5121: ('B', 1)}[a['componentType']]
    width = {'VEC4': 4, 'MAT4': 16}[shape]; packed = size*width
    offset = static.integer(a.get('byteOffset', 0)); stride = static.integer(v.get('byteStride', packed), packed)
    require(stride <= 252 and stride % size == 0 and offset % size == 0 and offset+(count-1)*stride+packed <= length, 'invalid skin accessor span')
    rows = [struct.unpack_from('<'+str(width)+code, binary, start+offset+i*stride) for i in range(count)]
    require(all(math.isfinite(x) for row in rows for x in row), 'non-finite skin attribute')
    return rows


def inverse_rigid(m):
    return [[m[c][r] for c in range(3)] + [-sum(m[k][r]*m[k][3] for k in range(3))] for r in range(3)] + [[0., 0., 0., 1.]]


def read_glb(path):
    doc, binary = document(path)
    require(len(doc.get('skins', [])) == 1, 'one skin required')
    require(not doc.get('extensionsUsed') and not doc.get('extensionsRequired'), 'extensions unsupported')
    def no_extensions(value):
        if isinstance(value, dict):
            require(not value.get('extensions'), 'extensions unsupported')
            for item in value.values(): no_extensions(item)
        elif isinstance(value, list):
            for item in value: no_extensions(item)
    no_extensions(doc)
    require(len(doc.get('nodes', [])) <= 64, 'node limit')
    skin = doc['skins'][0]; joints = skin.get('joints', [])
    require(1 <= len(joints) <= MAX_JOINTS and len(set(joints)) == len(joints), 'joint limit or duplicate joint')
    nodes = doc['nodes']; parents = {}
    for i, node in enumerate(nodes):
        for child in node.get('children', []):
            child = static.integer(child); require(child < len(nodes) and child not in parents, 'shared/invalid child')
            parents[child] = i
    require(all(type(j) is int and 0 <= j < len(nodes) for j in joints), 'invalid joint')
    lookup = {j: i for i, j in enumerate(joints)}
    bones, world = [], []
    for i, node_index in enumerate(joints):
        node = nodes[node_index]; parent_node = parents.get(node_index)
        parent = lookup.get(parent_node, -1)
        require(parent_node is None or parent >= 0, 'joint parent must be a joint')
        require(parent < i and (i == 0 or parent >= 0), 'connected parent-first skeleton required')
        require('matrix' not in node and node.get('scale', [1, 1, 1]) == [1, 1, 1], 'joint unit-scale TRS required')
        name = node.get('name', '')
        require(isinstance(name, str) and re.fullmatch(r'[A-Za-z][A-Za-z0-9_]{0,30}', name), 'bounded ASCII bone name required')
        require(name not in [b['name'] for b in bones], 'duplicate bone name')
        local = static.node_matrix(node)
        combined = static.matrix_mul(world[parent], local) if parent >= 0 else local
        require(all(abs(x) <= 10000 for row in combined for x in row), 'skeleton coordinate range')
        world.append(combined)
        # Quaternion matrix -> Euler XYZ, matching native Rz*Ry*Rx.
        y = math.asin(max(-1., min(1., -local[2][0])))
        if abs(math.cos(y)) > 1e-6: x, z = math.atan2(local[2][1], local[2][2]), math.atan2(local[1][0], local[0][0])
        else: x, z = math.atan2(-local[1][2], local[1][1]), 0.
        bones.append(dict(name=name, parent=parent, translation=[100*local[r][3] for r in range(3)], rotation=[x, y, z], inverse=inverse_rigid(combined)))
    if 'skeleton' in skin: require(skin['skeleton'] == joints[0], 'skin root mismatch')
    inv = accessor(doc, binary, skin['inverseBindMatrices'], 'MAT4', (5126,))
    require(len(inv) == len(bones), 'inverse-bind count mismatch')
    for authored, bone in zip(inv, bones):
        require(all(abs(authored[c*4+r]-bone['inverse'][r][c]) <= 1e-4 for r in range(4) for c in range(4)), 'inverse bind differs from joint hierarchy')
        for r in range(3): bone['inverse'][r][3] *= 100
    mesh_nodes = [(i, n) for i, n in enumerate(nodes) if 'mesh' in n]
    require(len(mesh_nodes) == 1, 'one skinned mesh instance required')
    mi, mesh_node = mesh_nodes[0]
    require(mi not in parents and mesh_node.get('skin') == 0 and mesh_node.get('mesh') == 0 and static.node_matrix(mesh_node) == static.node_matrix({}), 'mesh must be identity scene root with skin 0')
    primitive = doc['meshes'][0]['primitives'][0]; attrs = primitive['attributes']
    require(set(attrs) == {'POSITION','NORMAL','TEXCOORD_0','JOINTS_0','WEIGHTS_0'}, 'skinned attributes required')
    indices = accessor(doc, binary, attrs['JOINTS_0'], 'VEC4', (5121, 5123))
    weights = accessor(doc, binary, attrs['WEIGHTS_0'], 'VEC4', (5126,))
    influences = []
    require(len(indices) == len(weights), 'skin attribute counts differ')
    for js, ws in zip(indices, weights):
        require(all(j < len(bones) for j in js) and all(w >= 0 for w in ws) and abs(sum(ws)-1) < 1e-4, 'invalid skin weights/joints')
        used = [(j, w) for j, w in zip(js, ws) if w > 0]
        require(1 <= len(used) <= 2 and len(set(j for j, _ in used)) == len(used), 'at most two distinct influences required')
        if len(used) == 1: used.append((used[0][0], 0.))
        influences.append(used)
    # Reuse the strict static geometry/material/PNG/span validator on a sanitized
    # projection; skin data is independently validated above, never ignored.
    clean = copy.deepcopy(doc); clean.pop('skins'); clean.pop('animations', None)
    for n in clean['nodes']: n.pop('skin', None)
    for k in ('JOINTS_0','WEIGHTS_0'): clean['meshes'][0]['primitives'][0]['attributes'].pop(k)
    with tempfile.NamedTemporaryFile(suffix='.glb') as tmp:
        tmp.write(pack_glb(clean, binary)); tmp.flush(); geometry, info = static.read_glb(tmp.name)
    require(len(geometry[0]) == len(influences), 'skin vertex count mismatch')
    info.update(kind='skinned', bones=len(bones), weights_per_vertex=2, bounds_min=[min(p[k] for p in geometry[0]) for k in range(3)], bounds_max=[max(p[k] for p in geometry[0]) for k in range(3)])
    model = dict(geometry=geometry, bones=bones, influences=influences, document=doc, binary=binary)
    import actor_animation
    info['clips'] = actor_animation.inspect_clips(model)
    radius = animation_envelope(model)
    info['bind_bounds_min'], info['bind_bounds_max'] = info['bounds_min'], info['bounds_max']
    info['bounds_min'], info['bounds_max'] = [-radius]*3, [radius]*3
    info['animation_radius'] = radius
    return model, info


def animation_envelope(model):
    """A rigorous bound for all clip translations and arbitrary joint rotations.

    Unit bone scales preserve lengths. Each joint origin lies inside the sum
    of ancestor translation lengths; each inverse-bind vertex adds its local
    radius. Positive approximately normalized weights preserve this bound up
    to their checked rounding tolerance. Include every authored key, so short
    peaks between native30Hz samples are covered as well.
    """
    import actor_animation
    bones=model['bones']; maximum=[math.sqrt(sum(x*x for x in b['translation'])) for b in bones]
    doc,binary=model['document'],model['binary']; lookup={n:i for i,n in enumerate(doc['skins'][0]['joints'])}
    for clip in doc.get('animations',[]):
        for channel in clip['channels']:
            if channel['target']['path']!='translation':continue
            sampler=clip['samplers'][channel['sampler']]
            values=actor_animation._accessor(doc,binary,sampler['output'],3)
            i=lookup[channel['target']['node']]
            maximum[i]=max(maximum[i],*(100*math.sqrt(sum(x*x for x in row)) for row in values))
    chain=[]
    for i,bone in enumerate(bones):chain.append(maximum[i]+(chain[bone['parent']] if bone['parent']>=0 else 0))
    radius=0.
    for vertex,influences in zip(model['geometry'][0],model['influences']):
        for joint,weight in influences:
            if weight<=0:continue
            inverse=bones[joint]['inverse']
            local=[sum(inverse[r][k]*vertex[k] for k in range(3))+inverse[r][3] for r in range(3)]
            radius=max(radius,chain[joint]+math.sqrt(sum(x*x for x in local)))
    # Reserve for the accepted1e-4 weight-sum tolerance and float32 arithmetic
    # along up to32 hierarchy multiplies, plus0.01cm absolute roundoff.
    radius=radius*(1.0002+1e-5*len(bones))+.01
    require(math.isfinite(radius) and 0<radius<=1000000,'animated envelope exceeds coordinate limit')
    return radius


def append_string(f, value):
    body = value.encode(); return f.append(struct.pack('>I', len(body))+body+b'\0', 4)+4


def append_dictionary(f, entries):
    """Create the native descending-bit Patricia tree, including its sentinel."""
    count = len(entries); records = [[0xffffffff, 0, 0, '', 0]]
    def bit(key, at):
        body = key.encode(); return ((body[at//8] >> (at%8)) & 1) if at//8 < len(body) else 0
    for key, target in entries:
        require(key and key not in [r[3] for r in records], 'duplicate dictionary key')
        parent, cur = 0, records[0][1]
        while records[parent][0] > records[cur][0]:
            parent, cur = cur, records[cur][1+bit(key, records[cur][0])]
        other = records[cur][3]; a, b = key.encode(), other.encode()
        differing = [(i*8+k) for i in range(max(len(a), len(b))) for k in range(8) if bit(key, i*8+k) != bit(other, i*8+k)]
        require(differing, 'duplicate dictionary key'); split = max(differing)
        parent, cur = 0, records[0][1]
        while records[parent][0] > records[cur][0] and records[cur][0] > split:
            parent, cur = cur, records[cur][1+bit(key, records[cur][0])]
        index = len(records); side = bit(key, split)
        child = [cur, cur]; child[side] = index
        records.append([split, *child, key, target])
        branch = 0 if parent == 0 else bit(key, records[parent][0])
        records[parent][1+branch] = index
    at = f.append(b'\0'*(8+16*(count+1)), 4); f.put('II', at, 8+16*(count+1), count)
    for i, (ref, left, right, name, target) in enumerate(records):
        p = at+8+16*i; f.put('IHH', p, ref, left, right)
        if name: f.set_rel(p+8, append_string(f, name))
        if target: f.set_rel(p+12, target)
    return at


def build_bfres(template, model, identity):
    require(re.fullmatch(r'[a-z][a-z0-9_]{0,30}', identity), 'invalid identity')
    f = base.Bfres(template); models = f.dict(f.rel(0x20)); require(len(models) == 1 and models[0][0] == 'fobj00', 'wrong skin template')
    fm = models[0][1]; fv = f.rel(fm+16); sk = f.rel(fm+12); shapes = f.dict(f.rel(fm+20))
    require(len(shapes) == 1 and len(f.dict(f.rel(fm+24))) == 1, 'one mesh/material template required')
    shape = shapes[0][1]; mesh = f.rel(shape+0x24); vb = f.rel(fv+24); ib = f.rel(mesh+20)
    expected = [('_p0',0,0,0x811),('_n0',0,16,0x811),('_u0',0,32,0x80d),('_i0',0,40,0x10c),('_w0',0,48,0x80d)]
    aa = f.rel(fv+16); actual = [(f.cstr(f.rel(aa+i*12)),f.b[aa+i*12+4],f.u16(aa+i*12+6),f.u32(aa+i*12+8)) for i in range(f.b[fv+4])]
    require(actual == expected and f.u16(vb+12) == 64 and f.b[fv+12] == 2, 'unexpected skinned template vertex layout')
    bones = model['bones']; count = len(bones); ba = f.append(b'\0'*(64*count), 4)
    for i, bone in enumerate(bones):
        p = ba+i*64; f.set_rel(p, append_string(f,bone['name'])); f.put('HhhhHHI',p+4,i,bone['parent'],i,-1,0xffff,0,0x1001)
        f.put('3f',p+20,1,1,1); f.put('4f',p+32,*bone['rotation'],1); f.put('3f',p+48,*bone['translation'])
    dictionary = append_dictionary(f, [(b['name'],ba+i*64) for i,b in enumerate(bones)])
    palette = f.append(struct.pack('>'+str(count)+'H',*range(count)),4)
    inverse = f.append(b''.join(struct.pack('>12f',*(x for row in b['inverse'][:3] for x in row)) for b in bones),4)
    f.put('IHHHH',sk+4,0x1200,count,count,0,0)
    for off,target in [(16,dictionary),(20,ba),(24,palette),(28,inverse)]:f.set_rel(sk+off,target)
    f.put('I',sk+32,0)
    pos,normals,uv,indices,png = model['geometry']; data = bytearray()
    for p,n,t,influences in zip(pos,normals,uv,model['influences']):
        data += struct.pack('>3f4x3f4x2f2I2f8x',*p,*n,*t,*(v[0] for v in influences),*(v[1] for v in influences))
    at=f.append(data,0x1000);f.put('I',vb+4,len(data));f.set_rel(vb+20,at);f.put('I',fv+8,len(pos));f.put('I',fm+40,len(pos))
    data=struct.pack('>'+str(len(indices))+'I',*indices);at=f.append(data,0x1000);f.put('I',ib+4,len(data));f.set_rel(ib+20,at)
    f.put('IIIH',mesh,4,9,len(indices),1);f.put('II',f.rel(mesh+16),0,len(indices));f.put('I',mesh+24,0)
    f.put('HHH',shape+16,0,0,count);f.b[shape+22]=2;f.set_rel(shape+40,palette)
    low=[min(p[k] for p in pos) for k in range(3)];high=[max(p[k] for p in pos) for k in range(3)];center=[(a+b)/2 for a,b in zip(low,high)];extent=[(b-a)/2 for a,b in zip(low,high)]
    require(f.u16(shape+0x1a)==1, 'one native submesh bounding node required')
    radius=animation_envelope(model)
    box=f.rel(shape+52);f.put('6f',box,0,0,0,radius,radius,radius);f.put('f',shape+28,radius)
    tex=dict(f.dict(f.rel(0x24)))['Fobj00'];w,h,rgba=static.png(png);at=f.append(rgba,0x2000);surf=tex+4
    for off,value in [(4,w),(8,h),(16,1),(20,base.FMT_RGBA8_SRGB),(32,len(rgba)),(40,0),(48,1),(52,0),(56,0x100),(60,w)]:f.put('I',surf+off,value)
    for k in range(13):f.put('I',surf+64+4*k,0)
    f.put('I',tex+0x7c,1)
    for k in range(5):f.put('I',tex+0x8c+4*k,0)
    f.set_rel(tex+0xb0,at);f.put('i',tex+0xb4,0)
    f.set_rel(fm+4,append_string(f,identity));f.set_rel(0x20,append_dictionary(f,[(identity,fm)]))
    # No retail clips are reachable; the independent clip writer replaces this.
    f.put('I',0x28,0);f.put('H',0x54,0)
    f.append(b'',0x1000);f.put('I',12,len(f.b))
    import actor_animation
    return actor_animation.append_clips(bytes(f.b), model)


def converter_sources():
    """Named code fingerprints, independent of the installation directory."""
    import actor_animation
    result = {}
    for name, path in (('skinned_actor_model', __file__), ('actor_model', static.__file__),
                       ('notebook_model', base.__file__), ('shaderprep', base.shaderprep.__file__),
                       ('actor_animation', actor_animation.__file__)):
        with open(path, 'rb') as source:
            result[name] = hashlib.sha256(source.read()).hexdigest()
    return result


def build(game, path, identity, out):
    model,info=read_glb(path)
    with open(os.path.join(game,ARCHIVE),'rb') as source: template_bytes=source.read()
    files=dict(base.shaderprep.sarc(base.shaderprep.yaz0(template_bytes)))
    require(set(files)=={'Fobj00.bfres','fobj00.sharcfb'},'unexpected skin archive')
    bfres=build_bfres(files['Fobj00.bfres'],model,identity)
    packed=base.yaz0_stored(base.sarc({identity+'.bfres':bfres,identity+'.sharcfb':files['fobj00.sharcfb']}))
    import actor_animation
    source_hashes = converter_sources()
    code_digest = json.dumps(source_hashes, sort_keys=True, separators=(',', ':')).encode()
    digest=hashlib.sha256()
    with open(path,'rb') as source: source_bytes=source.read(static.MAX_SOURCE+1)
    for data in (b'skinned',str(VERSION).encode(),str(actor_animation.VERSION).encode(),code_digest,identity.encode(),source_bytes,files['Fobj00.bfres'],files['fobj00.sharcfb']):digest.update(struct.pack('>I',len(data)));digest.update(data)
    info.update(identity=identity,converter_version=VERSION,animation_converter_version=actor_animation.VERSION,converter_sources=source_hashes,cache_key=digest.hexdigest(),archive_bytes=len(packed),archive_sha256=hashlib.sha256(packed).hexdigest())
    os.makedirs(os.path.dirname(os.path.abspath(out)),exist_ok=True)
    with open(out+'.tmp','wb') as target:target.write(packed)
    os.replace(out+'.tmp',out)
    with open(out+'.json.tmp','w') as target:json.dump(info,target,sort_keys=True);target.write('\n')
    os.replace(out+'.json.tmp',out+'.json');return info


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--glb',required=True);parser.add_argument('--inspect',action='store_true');parser.add_argument('--game');parser.add_argument('--identity');parser.add_argument('--out');args=parser.parse_args()
    try:
        result=read_glb(args.glb)[1] if args.inspect else build(args.game,args.glb,args.identity,args.out)
    except (BuildError,OSError,ValueError,KeyError,IndexError,TypeError,struct.error,OverflowError,RecursionError) as exc:parser.exit(1,'skinned_actor_model: '+str(exc)+'\n')
    print(json.dumps(result,sort_keys=True))

if __name__=='__main__':main()
