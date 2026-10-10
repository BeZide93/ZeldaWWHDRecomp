#!/usr/bin/env python3
"""Extract the game's GX2 shaders and build a shader "head start" for the runtime cache.

Usage:
  shaderprep.py scan     [--game DIR]                           list containers and shader counts
  shaderprep.py coverage RUNTIME_CACHE                           runtime programs found in the game files
  shaderprep.py template RUNTIME_CACHE [OUT] [--merge T]         distill your recorded runtime cache into the
                                                                 state template (default OUT:
                                                                 tools/shadercache/template.bin);
                                                                 --merge keeps what template T already has
  shaderprep.py build    [OUT] [--template T] [--max N]          head start from the game files + template
                         [--no-pipelines]                        (default OUT: game/shadercache/headstart.bin)

Where the shaders are (Wind Waker HD, USA):
  - SHARCFB archives ("BAHS" = byte-swapped "SHAB", AGL binary shader archive, version 9, little-endian
    header): standalone in content/Common/Shaders/{Prim,particle,render_buffer}/*.sharcfb, inside the AGL
    resource SARCs (content/Cafe/Common/agl_resource_cafe*.sarc) and inside many Yaz0-compressed SARC
    archives (*.szs, and the *.pack SARCs that bundle szs files) - e.g. model.sharcfb (8976 shaders),
    agl_technique_{pfx,lght,shdw}.sharcfb, wii_pipeline.sharcfb and per-stage archives (zl, pz, vf_*...).
    About 30,000 distinct shaders in total.
  - SHARCFB layout (kinnay/Nintendo-File-Formats documents v8; v9 matches for what we use):
      header: magic, version, filesize, endianness, 0, name length, name
      binary section: section size, count, then entries {entry size, type (0 VS, 1 PS, 2 GS), unknown,
        data size, data[]}. The data is the GX2VertexShader / GX2PixelShader structure stored
        little-endian, pointers as offsets from the start of the data. The microcode is stored
        byte-for-byte as the game later hands it to the GPU (95% of the programs the game uses from boot
        to Outset Island are found this way).
      program section: named programs referencing binaries (variations = macro combinations).
  - GX2VertexShader: 52 register words (0x00..0xCC), shader size @0xD0, shader offset @0xD4.
    GX2PixelShader: 41 register words (0x00..0xA0), shader size @0xA4, shader offset @0xA8.
  - .gsh (GFX2) files only hold the AGL primitive renderer's few shaders; not used by this tool.

The head start is written in the runtime's recipe format (runtime/src/gfx/metal_draw.mm, cache_write):
records of {u32 type, u32 raw size, u32 compressed size, zlib(raw)}; a shader record is
{u32 vertex, u32 size, u32 fsSize, u32 nregs, microcode, fetch microcode, (u32 reg, u32 value) * nregs};
type 2 is the Metal PipelineRecipe struct (168 bytes). Type 4 is a checked,
versioned Vulkan database containing exact keys, SPIR-V, descriptors and fixed
state, plus the local device/driver/features fingerprint. Handles are never stored.

The distributable template is WWHT0003 + bounded zlib canonical JSON. Strict
fields contain only program hashes/stages, register pairs, semantic fetch
attributes (buffer/location/format/numeric/signed/endian/component selectors,
offset/fetch type/signed divisor), and explicitly typed pipeline state. No
program bytes, fetch instructions, shader source, SPIR-V or native cache data
are accepted. See shaderprep_format.py for the versioned schema. Legacy local
templates can be converted using template --merge; unsupported fetch features
fail closed. Build resolves program hashes from the player's own archives and
writes a LOCAL code-carrying headstart. Never distribute that generated file.
"""
import shaderprep_format as template_format
import json
import collections
import ctypes
import glob
import os
import shlex
import shutil
import struct
import subprocess
import sys
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
TEMPLATE = os.path.join(HERE, "shadercache", "template.bin")  # guarded code-free release template

REC_SHADER, REC_PIPELINE, REC_SPECULATIVE = 1, 2, 3
REC_VULKAN_PIPELINE = 4
VS, PS, GS = 0, 1, 2


# ---------------------------------------------------------------- containers
_yaz0 = None


def yaz0_python(b):
    """Portable fallback when no local C compiler is available."""
    if len(b) < 16 or b[:4] != b"Yaz0":
        return b""
    n = struct.unpack_from(">I", b, 4)[0]
    if n > 256 * 1024 * 1024:
        return b""
    out, at = bytearray(), 16
    try:
        while len(out) < n:
            code = b[at]
            at += 1
            for bit in range(7, -1, -1):
                if len(out) >= n:
                    break
                if code & (1 << bit):
                    out.append(b[at])
                    at += 1
                else:
                    first, second = b[at], b[at + 1]
                    at += 2
                    distance = ((first & 15) << 8 | second) + 1
                    length = first >> 4
                    if not length:
                        length = b[at] + 18
                        at += 1
                    else:
                        length += 2
                    if distance > len(out):
                        return b""
                    for _ in range(min(length, n - len(out))):
                        out.append(out[-distance])
    except IndexError:
        return b""
    return bytes(out)


def yaz0(b):
    global _yaz0
    if _yaz0 is None:
        suffix = ".dll" if os.name == "nt" else ".dylib" if sys.platform == "darwin" else ".so"
        lib = os.path.join(ROOT, "build", "libyaz0" + suffix)
        source = os.path.join(HERE, "yaz0.c")
        try:
            if not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(source):
                os.makedirs(os.path.dirname(lib), exist_ok=True)
                compiler = shlex.split(os.environ["CC"]) if os.environ.get("CC") else [shutil.which("clang") or shutil.which("gcc") or "clang"]
                subprocess.check_call(compiler + ["-O2", "-shared"] + ([] if os.name == "nt" else ["-fPIC"]) + ["-o", lib, source])
            _yaz0 = ctypes.CDLL(lib)
            _yaz0.yaz0_decompress.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
            _yaz0.yaz0_decompress.restype = ctypes.c_long
        except (OSError, subprocess.CalledProcessError):
            print("shaderprep: native Yaz0 helper unavailable; using portable Python decoder", file=sys.stderr)
            _yaz0 = False
    if _yaz0 is False:
        return yaz0_python(b)
    if len(b) < 16 or b[:4] != b"Yaz0":
        return b""
    n = struct.unpack_from(">I", b, 4)[0]
    if n > 256 * 1024 * 1024:
        return b""
    out = ctypes.create_string_buffer(n)
    r = _yaz0.yaz0_decompress(b, len(b), out, n)
    return out.raw[:r] if r == n else b""


def sarc(b):
    e = ">" if b[6:8] == b"\xfe\xff" else "<"
    hl = struct.unpack_from(e + "H", b, 4)[0]
    doff = struct.unpack_from(e + "I", b, 0xC)[0]
    n = struct.unpack_from(e + "H", b, hl + 6)[0]
    sfnt = hl + 12 + 16 * n
    for i in range(n):
        h, attr, s, t = struct.unpack_from(e + "IIII", b, hl + 12 + 16 * i)
        name = "%08x" % h
        if attr & 0x01000000:
            off = sfnt + 8 + (attr & 0xFFFF) * 4
            name = b[off:b.index(b"\0", off)].decode(errors="replace")
        yield name, b[doff + s:doff + t]


def walk(b, path):
    """yield (path, bytes) for every file inside Yaz0/SARC nesting"""
    if b[:4] == b"Yaz0":
        yield from walk(yaz0(b), path)
    elif b[:4] == b"SARC":
        for n, f in sarc(b):
            yield from walk(f, path + "/" + n)
    else:
        yield path, b


def sharcfb_shaders(b):
    """yield (type, register words, microcode) for every vertex/pixel shader binary"""
    if b[:4] != b"BAHS":
        return
    u = lambda o: struct.unpack_from("<I", b, o)[0]
    o = 0x18 + u(0x14)
    count = u(o + 4)
    p = o + 8
    for _ in range(count):
        size, typ, _unk, dsz = u(p), u(p + 4), u(p + 8), u(p + 12)
        data = b[p + 16:p + 16 + dsz]
        p += size
        w = lambda off: struct.unpack_from("<I", data, off)[0]
        if typ == VS and len(data) >= 0xDC:
            nregs, sz_off, ptr_off = 52, 0xD0, 0xD4
        elif typ == PS and len(data) >= 0xAC:
            nregs, sz_off, ptr_off = 41, 0xA4, 0xA8
        else:
            continue
        size_c, ptr = w(sz_off), w(ptr_off)
        if ptr + size_c > len(data):
            continue
        yield typ, [w(4 * i) for i in range(nregs)], data[ptr:ptr + size_c]


def game_shaders(game):
    """all distinct (type, microcode) -> (register words, container path)"""
    out = {}
    for f in sorted(glob.glob(os.path.join(game, "**", "*"), recursive=True)):
        if not (os.path.isfile(f) and f.endswith((".szs", ".pack", ".sarc", ".sharcfb"))):
            continue
        for path, b in walk(open(f, "rb").read(), os.path.relpath(f, game)):
            for typ, regs, code in sharcfb_shaders(b):
                out.setdefault((typ, code), (regs, path))
    return out


# ---------------------------------------------------------------- recipe records
def decompress_bounded(data, limit, expected=None):
    decoder = zlib.decompressobj()
    raw = decoder.decompress(data, limit + 1)
    if len(raw) > limit or decoder.unconsumed_tail or not decoder.eof or decoder.unused_data:
        raise ValueError("invalid or oversized compressed recipe")
    if expected is not None and len(raw) != expected:
        raise ValueError("recipe decoded size mismatch")
    return raw


def read_records(path):
    if os.path.getsize(path) > 256 * 1024 * 1024:
        raise ValueError("recipe file size limit")
    with open(path, "rb") as source:
        d = source.read()
    o = total = count = 0
    while o < len(d):
        if len(d) - o < 12:
            raise ValueError("truncated recipe header")
        t, raw, comp = struct.unpack_from("<III", d, o)
        o += 12
        count += 1
        total += raw
        if t not in (1, 2, 3, 4, 5, 6) or not 0 < raw <= 16 * 1024 * 1024 or \
                not 0 < comp <= 16 * 1024 * 1024 or comp > len(d) - o or \
                count > 100000 or total > 256 * 1024 * 1024:
            raise ValueError("invalid recipe bounds")
        yield t, decompress_bounded(d[o:o + comp], raw, raw)
        o += comp


def parse_shader_record(r):
    if len(r)<16: raise ValueError('truncated shader record')
    vertex, size, fs_size, n = struct.unpack_from('<IIII',r)
    if vertex not in (0,1) or not 0<size<=16*1024*1024 or fs_size>4096 or n>16384 or 16+size+fs_size+n*8!=len(r):
        raise ValueError('invalid shader record bounds')
    o=16
    code=bytes(r[o:o+size]);o+=size
    fs=bytes(r[o:o+fs_size]);o+=fs_size
    regs=[struct.unpack_from('<II',r,o+8*i) for i in range(n)]
    return vertex,code,fs,regs


def parse_linked_shader_record(raw):
    if len(raw)<20: raise ValueError('truncated linked shader record')
    vertex,ps,fs,n,vs=struct.unpack_from('<5I',raw)
    if vertex!=0 or not 0<ps<=16*1024*1024 or not 0<vs<=16*1024*1024 or not 0<fs<=4096 or n>16384 or 20+ps+fs+vs+n*8!=len(raw):
        raise ValueError('invalid linked shader bounds')
    o=20
    pixel=raw[o:o+ps];o+=ps
    fetch=raw[o:o+fs];o+=fs
    companion=raw[o:o+vs];o+=vs
    regs=[struct.unpack_from('<II',raw,o+i*8) for i in range(n)]
    return pixel,fetch,companion,regs


def linked_shader_record(pixel,fetch,companion,regs):
    return struct.pack('<5I',0,len(pixel),len(fetch),len(regs),len(companion))+pixel+fetch+companion+b''.join(struct.pack('<II',*r) for r in regs)


def write_record(f, t, raw):
    comp = zlib.compress(raw, 6)
    f.write(struct.pack("<III", t, len(raw), len(comp)))
    f.write(comp)


def shader_record(vertex, code, fs, regs):
    raw = struct.pack("<IIII", vertex, len(code), len(fs), len(regs)) + code + fs
    raw += b"".join(struct.pack("<II", r, v) for r, v in regs)
    return raw


def prog_hash(code):
    h = 0xCBF29CE484222325
    for i in range(0, len(code), 8):
        h = ((h ^ int.from_bytes(code[i:i + 8].ljust(8, b"\0"), "little")) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h ^ len(code)


# ---------------------------------------------------------------- shader register blocks
# registers GX2SetVertexShader / GX2SetPixelShader write from the shader structure
# (same sequence as runtime/src/gx2/gx2_shader_regs.cpp)
mmSQ_PGM_START_PS, mmSQ_PGM_RESOURCES_PS = 0xA210, 0xA214
mmSQ_PGM_START_VS, mmSQ_PGM_RESOURCES_VS, mmSQ_PGM_START_FS = 0xA216, 0xA21A, 0xA225
mmVGT_PRIMITIVEID_EN, mmSPI_VS_OUT_CONFIG, mmPA_CL_VS_OUT_CNTL = 0xA2A1, 0xA1B1, 0xA207
mmSPI_VS_OUT_ID_0, mmSQ_VTX_SEMANTIC_0, mmSQ_VTX_SEMANTIC_CLEAR = 0xA185, 0xA0E0, 0xA238
mmSPI_PS_IN_CONTROL_0, mmSPI_PS_INPUT_CNTL_0 = 0xA1B3, 0xA191
mmCB_SHADER_MASK, mmCB_SHADER_CONTROL, mmDB_SHADER_CONTROL, mmSPI_INPUT_Z = 0xA08F, 0xA1E8, 0xA203, 0xA1B6


def own_block(vertex, w):
    """register -> value pairs a shader structure sets (w = structure register words)"""
    r = {}
    if vertex:
        r[mmSQ_PGM_RESOURCES_VS] = w[0x00 // 4]
        r[mmVGT_PRIMITIVEID_EN] = w[0x04 // 4]
        r[mmSPI_VS_OUT_CONFIG] = w[0x08 // 4]
        r[mmPA_CL_VS_OUT_CNTL] = w[0x38 // 4]
        for i in range(min(w[0x0C // 4], 10)):
            r[mmSPI_VS_OUT_ID_0 + i] = w[0x10 // 4 + i]
        nsem = min(w[0x40 // 4], 32)
        if nsem:
            r[mmSQ_VTX_SEMANTIC_CLEAR] = 0xFFFFFFFF
            for i in range(nsem):
                r[mmSQ_VTX_SEMANTIC_0 + i] = w[0x44 // 4 + i]
    else:
        r[mmSQ_PGM_RESOURCES_PS] = w[0]
        r[mmSPI_PS_IN_CONTROL_0] = w[2]
        r[mmSPI_PS_IN_CONTROL_0 + 1] = w[3]
        for i in range(min(w[4], 0x20)):
            r[mmSPI_PS_INPUT_CNTL_0 + i] = w[5 + i]
        r[mmCB_SHADER_MASK] = w[37]
        r[mmCB_SHADER_CONTROL] = w[38]
        r[mmDB_SHADER_CONTROL] = w[39]
        r[mmSPI_INPUT_Z] = w[40]
    return r


# ---------------------------------------------------------------- template
def _codefree_state(h, vertex, fs, regs, linked=None):
    # ALU constant banks contain per-draw uniform values, not translation state.
    # Vulkan records whole context banks; never publish these values. Pixel
    # translation does not use fetch identity (translate seed is vertex-only).
    fetch = (fs if isinstance(fs, dict) else template_format.parse_fetch(fs, regs, prog_hash)) if vertex else None
    drop = (mmSQ_PGM_START_VS, mmSQ_PGM_START_PS, mmSQ_PGM_START_FS)
    state = {'hash': h, 'vertex': vertex, 'fetch': fetch,
             'registers': [[a, v] for a, v in regs if a not in drop and not 0xc000 <= a < 0xe000]}
    if linked is not None: state['linked_vertex']=linked
    return state


def _codefree_pipeline(kind, payload):
    if isinstance(payload, dict): return payload
    if kind == REC_PIPELINE: return template_format.metal_pipeline(payload)
    if kind == 5 and payload.startswith(b'WWVKST01'):
        return template_format.vulkan_recipe(payload[8:], codefree=True)
    raise ValueError('unsupported code-free pipeline type')


def cmd_template(cache, out, merge=None):
    states, pipelines = [], []
    if merge:
        old_states, old_pipelines = read_template(merge, allow_legacy=True)
        states.extend(_codefree_state(*s) for s in old_states)
        for kind,p in old_pipelines:
            pipelines.extend(template_format.vulkan_database(p) if kind==REC_VULKAN_PIPELINE and not isinstance(p,dict) else [_codefree_pipeline(kind,p)])
    for kind, raw in read_records(cache):
        if kind in (REC_PIPELINE, REC_VULKAN_PIPELINE, 5):
            pipelines.extend(template_format.vulkan_database(raw) if kind==REC_VULKAN_PIPELINE else [_codefree_pipeline(kind,raw)])
        elif kind == 6:
            code,fs,vs,regs=parse_linked_shader_record(raw)
            states.append(_codefree_state(prog_hash(code),0,b'',regs,{'hash':prog_hash(vs),'fetch':template_format.parse_fetch(fs,regs,prog_hash)}))
        elif kind == REC_SHADER:
            vertex, code, fs, regs = parse_shader_record(raw)
            states.append(_codefree_state(prog_hash(code), vertex, fs, regs))
    def unique(values):
        return list({json.dumps(v,sort_keys=True,separators=(',',':')):v for v in values}.values())
    doc = {'version':3,'states':unique(states),'pipelines':unique(pipelines)}
    data=template_format.encode(doc)
    os.makedirs(os.path.dirname(out) or '.',exist_ok=True)
    with open(out,'wb') as output: output.write(data)
    print('code-free template: %d states, %d pipelines, %d bytes -> %s' %
          (len(doc['states']),len(doc['pipelines']),len(data),out))


def read_template(path, allow_legacy=False):
    with open(path,'rb') as source: data=source.read()
    if data.startswith(template_format.MAGIC):
        doc=template_format.decode(data)
        return [(s['hash'],s['vertex'],s['fetch'],[tuple(r) for r in s['registers']],s.get('linked_vertex')) for s in doc['states']], [(5 if p['backend']=='vulkan' else REC_PIPELINE,p) for p in doc['pipelines']]
    if not allow_legacy:
        raise ValueError('legacy template carries fetch bytes; convert locally with template --merge')
    return read_legacy_template(path)


def read_legacy_template(path):
    if not os.path.isfile(path):
        sys.exit("no state template at %s\n"
                 "make one from your own runtime cache after playing for a while:\n"
                 "  tools/shaderprep.py template ~/Library/Caches/wwhd/shaders.bin" % path)
    with open(path, "rb") as source:
        d = source.read()
    if d[:4] != b"WWHT":
        sys.exit("not a template: " + path)
    typed = d.startswith(b"WWHT0002")
    raw = decompress_bounded(d[8:] if typed else d[4:], 256 * 1024 * 1024)
    o = 4
    fetch = []
    for _ in range(struct.unpack_from("<I", raw, 0)[0]):
        ln = struct.unpack_from("<I", raw, o)[0]
        fetch.append(raw[o + 4:o + 4 + ln])
        o += 4 + ln
    n = struct.unpack_from("<I", raw, o)[0]
    o += 4
    states = []
    for _ in range(n):
        h, vertex, fi, nr = struct.unpack_from("<QIiI", raw, o)
        o += 20
        regs = [struct.unpack_from("<II", raw, o + 8 * i) for i in range(nr)]
        o += 8 * nr
        states.append((h, vertex, fetch[fi] if fi >= 0 else b"", regs, None))
    pipelines = []
    if o + 4 <= len(raw):  # older templates end here
        n = struct.unpack_from("<I", raw, o)[0]
        o += 4
        for _ in range(n):
            kind = REC_PIPELINE
            if typed:
                kind = struct.unpack_from("<I", raw, o)[0]
                o += 4
                if kind not in (REC_PIPELINE, REC_VULKAN_PIPELINE):
                    raise ValueError("unknown template pipeline type")
            ln = struct.unpack_from("<I", raw, o)[0]
            if ln > len(raw) - o - 4:
                raise ValueError("truncated template pipeline")
            pipelines.append((kind, raw[o + 4:o + 4 + ln]))
            o += 4 + ln
    if o != len(raw):
        raise ValueError("trailing template bytes")
    return states, pipelines


# ---------------------------------------------------------------- commands
def cmd_scan(game):
    shaders = game_shaders(game)
    per = collections.Counter(p.split("/")[0] + "/" + os.path.basename(p) for (_, (_, p)) in shaders.items())
    kinds = collections.Counter(t for t, _ in shaders)
    print("distinct shaders: %d (VS %d, PS %d)" % (len(shaders), kinds[VS], kinds[PS]))
    for k, v in per.most_common(20):
        print("%6d  %s" % (v, k))


def cmd_coverage(game, cache):
    shaders = game_shaders(game)
    progs = set()
    for t, r in read_records(cache):
        if t == 6:
            code,fs,vs,regs=parse_linked_shader_record(r)
            progs.add((PS,code));progs.add((VS,vs))
        elif t in (REC_SHADER, REC_SPECULATIVE):
            vertex, code, fs, regs = parse_shader_record(r)
            progs.add((VS if vertex else PS, code))
    have = sum(1 for k in progs if k in shaders)
    print("programs: %d, found in archives: %d (%.1f%%)" % (len(progs), have, 100.0 * have / max(len(progs), 1)))
    missing = [k for k in progs if k not in shaders]
    print("missing by type:", dict(collections.Counter("VS" if t == VS else "PS" for t, _ in missing)))


def cmd_build(game, out, template, limit, with_pipelines=True):
    states, pipelines = read_template(template)
    shaders = game_shaders(game)
    if not with_pipelines:
        pipelines = []
    by_hash = collections.defaultdict(list)
    for h, vertex, fs, regs, linked in states:
        by_hash[(h, bool(vertex))].append((fs, regs, linked))
    # speculative states: those recorded for programs with an identical shader register block
    blocks = {}
    for (typ, code), (w, path) in shaders.items():
        blocks.setdefault((typ == VS, tuple(sorted(own_block(typ == VS, w)))), None)
    by_family = collections.defaultdict(list)
    for h, vertex, fs, regs, linked in states:
        regmap = dict(regs)
        for (tv, regset) in blocks:
            if tv == bool(vertex):
                by_family[(tv, tuple((a, regmap.get(a, 0)) for a in regset))].append((fs, regs, linked))
    pgm = {True: mmSQ_PGM_START_VS, False: mmSQ_PGM_START_PS}
    companion_programs=collections.defaultdict(set)
    for (typ,code) in shaders:
        if typ==VS: companion_programs[prog_hash(code)].add(code)
    unresolved_linked=0
    known = spec = 0
    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    with open(out, "wb") as f:
        for (typ, code), (w, path) in shaders.items():
            vertex = typ == VS
            for fs, regs, linked in by_hash.get((prog_hash(code), vertex), []):
                r = dict(regs)
                r[pgm[vertex] + 1] = len(code) >> 3  # the loader supplies the program address
                if linked is not None:
                    matches=companion_programs.get(linked['hash'],set())
                    if len(matches)!=1:
                        unresolved_linked+=1
                        continue
                    companion=next(iter(matches))
                    r[mmSQ_PGM_START_VS+1]=len(companion)>>3
                    write_record(f,6,linked_shader_record(code,template_format.fetch_payload(linked['fetch']),companion,sorted(r.items())))
                else:
                    write_record(f, REC_SHADER, shader_record(1 if vertex else 0, code, template_format.fetch_payload(fs) if fs else b"", sorted(r.items())))
                known += 1
        for kind, p in pipelines:
            write_record(f, kind, template_format.vulkan_payload(p) if kind==5 else template_format.metal_payload(p))
        for (typ, code), (w, path) in shaders.items():
            vertex = typ == VS
            if (prog_hash(code), vertex) in by_hash:
                continue
            blk = own_block(vertex, w)
            for fs, regs, linked in by_family.get((vertex, tuple(sorted(blk.items()))), [])[:limit]:
                if linked is not None: continue # Never invent a linked VS for a speculative PS.
                r = dict(regs)
                r.update(blk)
                r[pgm[vertex] + 1] = len(code) >> 3
                write_record(f, REC_SPECULATIVE, shader_record(1 if vertex else 0, code, template_format.fetch_payload(fs) if fs else b"", sorted(r.items())))
                spec += 1
    print("unresolved linked shader states: %d" % unresolved_linked)
    print("archive shaders %d; known records %d; pipeline records %d; speculative records %d -> %s (%d bytes)" %
          (len(shaders), known, len(pipelines), spec, out, os.path.getsize(out)))


def main():
    args = sys.argv[1:]
    opts = {"--game": os.path.join(ROOT, "game", "content"), "--max": "2", "--template": TEMPLATE, "--merge": None}
    no_pipelines = "--no-pipelines" in args
    if no_pipelines:
        args.remove("--no-pipelines")
    for k in list(opts):
        if k in args:
            i = args.index(k)
            opts[k] = args[i + 1]
            del args[i:i + 2]
    if not args:
        print(__doc__)
        return
    cmd, game = args[0], opts["--game"]
    if cmd == "scan":
        cmd_scan(game)
    elif cmd == "coverage":
        cmd_coverage(game, args[1])
    elif cmd == "template":
        cmd_template(args[1], args[2] if len(args) > 2 else TEMPLATE, opts["--merge"])
    elif cmd == "build":
        out = args[1] if len(args) > 1 else os.path.join(ROOT, "game", "shadercache", "headstart.bin")
        cmd_build(game, out, opts["--template"], int(opts["--max"]), not no_pipelines)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
