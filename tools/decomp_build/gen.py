#!/usr/bin/env python3
"""Decomp build (CMake WWHD_DECOMP=ON, docs/decomp-build.md): which game functions run as compiled
decompiled source, and the generated glue for them.

usage: gen.py DECOMP_DIR OUT_DIR

DECOMP_DIR is the decompilation (the `decomp` submodule): DECOMP_DIR/wwhd_src (the source),
DECOMP_DIR/tools/verify/{include,src,units}. Nothing is read from the game: the outputs depend only
on the decompilation and on this repository's runtime hook lists (tools/recomp/hooks*.txt).
Every output goes to OUT_DIR (the CMake build directory) and is rewritten only when it changes:

  decomp_hooks.txt  the decompiled functions (canonical addresses). recomp.py --decomp-hooks routes
                    them through the runtime (hook_X); setup passes the same list to the recompiler.
  hooks.cpp         one slot per listed function and its hook_X entry point
                    (runtime/src/decomp/decomp.h: decompiled code or the recompiled f_X_orig)
  units.cmake       the game units and their source files, for CMakeLists.txt
  glue/N.cpp        per unit: tools/decomp_build/glue.cpp with the unit's name
  functions.tsv     address, unit, name of every listed function
  excluded.txt      decompiled functions that stay recompiled, with the reason

Game units: verify units (tools/verify/units/*.txt) that share source files form one game unit
(connected components), named after its shortest verify-unit name. Every `VERIFY(0xADDR, fn)` in its
sources is a candidate for ADDR. Excluded here: addresses the runtime already hooks (hooks*.txt; the
decompiled function would bypass that hook) and addresses verified in more than one game unit (the
first unit by name keeps it). recomp.py excludes the rest, which needs the game's code: an address
that is not a function entry, or a function containing an instruction-level hook ("@ADDR").
"""
import collections
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "recomp"))
import builds as game_builds  # noqa: E402


def read_units(src):
    units = {}
    for f in sorted(glob.glob(os.path.join(src, "tools/verify/units/*.txt"))):
        u = os.path.basename(f)[:-4]
        srcs = []
        with open(f, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                t = line.split("#")[0].split()
                if t and t[0] == "src":
                    srcs += t[1:]
        if srcs:
            units[u] = srcs
    return units


def components(units):
    parent = {}

    def find(x):
        while parent.setdefault(x, x) != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    for u, srcs in units.items():
        for s in srcs:
            parent[find("u:" + u)] = find("s:" + s)
    groups = collections.defaultdict(lambda: ([], set()))
    for u, srcs in units.items():
        g = groups[find("u:" + u)]
        g[0].append(u)
        g[1].update(srcs)
    out = {}
    for us, srcs in groups.values():
        name = min(us, key=lambda x: (len(x), x))
        out[name] = (sorted(us), sorted(srcs))
    return out


def verify_marks(path):
    with open(path, encoding="utf-8", errors="replace") as fh:
        text = fh.read()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)   # commented-out VERIFY lines do not count
    text = re.sub(r"//[^\n]*", "", text)
    return [(int(m.group(1), 16), m.group(2).strip())
            for m in re.finditer(r"VERIFY\(\s*(0x[0-9A-Fa-f]+)\s*,\s*([^)]*)\)", text)]


def runtime_hooks():
    """The functions the runtime hooks itself (canonical addresses; instruction sites are left to recomp.py)."""
    entries, _ = game_builds.read_hooks(game_builds.hook_files(os.path.join(ROOT, "tools", "recomp")),
                                        game_builds.canonical_build())
    return {canon for site, canon, _, _ in entries if not site}


def write_if_changed(path, text):
    try:
        with open(path, encoding="utf-8") as fh:
            if fh.read() == text:
                return
    except OSError:
        pass
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(text)
    os.replace(tmp, path)


def c_string(s):
    return '"%s"' % s.replace("\\", "\\\\").replace('"', '\\"')


def cmake_path(p):
    return p.replace("\\", "/")


def decomp_revision(src):
    try:
        r = subprocess.run(["git", "-C", src, "rev-parse", "HEAD"], capture_output=True, text=True, timeout=30)
        return r.stdout.strip() if r.returncode == 0 else ""
    except (OSError, subprocess.SubprocessError):
        return ""


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[3])
    src = os.path.abspath(sys.argv[1])
    out = os.path.abspath(sys.argv[2])
    for need in ("wwhd_src", "tools/verify/include/gabi.h", "tools/verify/units"):
        if not os.path.exists(os.path.join(src, need)):
            sys.exit("gen.py: %s has no %s (run: git submodule update --init decomp)" % (src, need))
    os.makedirs(os.path.join(out, "glue"), exist_ok=True)

    comps = components(read_units(src))
    alias = {u: c for c, (us, _) in comps.items() for u in us}
    hooks = runtime_hooks()

    owner = {}          # addr -> (unit, name, used)
    excluded = []       # (addr, unit, name, why)
    for c in sorted(comps):
        for s in comps[c][1]:
            for addr, name in verify_marks(os.path.join(src, s)):
                if addr in owner:
                    if owner[addr][0] != c:
                        excluded.append((addr, c, name, "also verified in %s (kept there)" % owner[addr][0]))
                    continue
                if addr in hooks:
                    excluded.append((addr, c, name, "already a runtime hook"))
                    owner[addr] = (c, name, False)
                else:
                    owner[addr] = (c, name, True)
    listed = sorted(x for x, v in owner.items() if v[2])
    units_used = sorted({owner[x][0] for x in listed})
    uidx = {u: i for i, u in enumerate(units_used)}
    rev = decomp_revision(src)

    lines = ["# generated by tools/decomp_build/gen.py: the decompiled functions of the decomp build",
             "# (canonical addresses; recomp.py --decomp-hooks)%s" % (" from decomp %s" % rev if rev else "")]
    lines += ["%08X  # %s %s" % (x, owner[x][0], owner[x][1]) for x in listed]
    write_if_changed(os.path.join(out, "decomp_hooks.txt"), "\n".join(lines) + "\n")
    write_if_changed(os.path.join(out, "excluded.txt"), "".join(
        "%08X %-28s %-40s %s\n" % e for e in sorted(excluded)))
    write_if_changed(os.path.join(out, "functions.tsv"), "".join(
        "%08X\t%s\t%s\n" % (x, owner[x][0], owner[x][1]) for x in listed))

    h = ["// generated by tools/decomp_build/gen.py", '#include "decomp/decomp.h"', "", 'extern "C" {']
    h += ["void f_%08X_orig(Cpu*);" % x for x in listed]
    h += ["}", "", "namespace decomp {", "const char* const kUnits[] = {"]
    h += ["    %s," % c_string(u) for u in units_used]
    h += ["};", "const unsigned kNumUnits = %d;" % len(units_used), "const UnitAlias kAliases[] = {"]
    al = sorted((u, c) for u, c in alias.items() if c in uidx)
    h += ["    {%s, %d}," % (c_string(u), uidx[c]) for u, c in al]
    h += ["};", "const unsigned kNumAliases = %d;" % len(al), "Slot g_slots[] = {"]
    h += ["    {0x%08Xu, %d, %s, f_%08X_orig}," % (x, uidx[owner[x][0]], c_string(owner[x][1]), x) for x in listed]
    h += ["};", "const unsigned kNumSlots = %d;" % len(listed), "}  // namespace decomp", "", 'extern "C" {']
    h += ["void hook_%08X(Cpu* c) { decomp::call(c, %d); }" % (x, i) for i, x in enumerate(listed)]
    h += ["}", ""]
    write_if_changed(os.path.join(out, "hooks.cpp"), "\n".join(h))

    glue = cmake_path(os.path.join(HERE, "glue.cpp"))
    cm = ["# generated by tools/decomp_build/gen.py", "set(WWHD_DECOMP_UNIT_COUNT %d)" % len(units_used)]
    deps = set()
    for i, u in enumerate(units_used):
        gp = os.path.join(out, "glue", "%d.cpp" % i)
        write_if_changed(gp, "// generated by tools/decomp_build/gen.py\n#define DECOMP_UNIT %s\n#include \"%s\"\n"
                         % (c_string(u), glue))
        srcs = [cmake_path(os.path.join(src, s)) for s in comps[u][1]]
        deps.update(srcs)
        cm.append('set(WWHD_DECOMP_UNIT_%d_NAME "%s")' % (i, u))
        cm.append("set(WWHD_DECOMP_UNIT_%d_SOURCES\n  %s)" % (i, "\n  ".join('"%s"' % s for s in srcs)))
    # what the lists above depend on: CMake configures again when one of these changes
    deps.update(cmake_path(p) for p in glob.glob(os.path.join(src, "tools/verify/units/*.txt")))
    deps.update(cmake_path(p) for p in game_builds.hook_files(os.path.join(ROOT, "tools", "recomp")))
    deps.add(cmake_path(os.path.abspath(__file__)))
    cm.append("set(WWHD_DECOMP_GEN_DEPENDS\n  %s)" % "\n  ".join('"%s"' % d for d in sorted(deps)))
    write_if_changed(os.path.join(out, "units.cmake"), "\n".join(cm) + "\n")
    print("decomp: %d game units, %d functions decompiled, %d excluded (%s)" % (
        len(units_used), len(listed), len(excluded), os.path.join(out, "excluded.txt")))


if __name__ == "__main__":
    main()
