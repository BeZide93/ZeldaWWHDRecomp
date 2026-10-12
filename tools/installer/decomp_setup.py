"""Setup's optional decomp version (docs/decomp-build.md, "In setup"): a second game binary built on the
player's machine with the decompiled game functions of the public decompilation (ZeldaWWHDDecomp/wwhd)
as compiled source, next to the normal game. Experimental and opt-in: offered on macOS in development
builds only, off by default; the normal game is always built first and stays playable when this fails.

What the release brings (tools/release/package.py --decomp-build), described in sdk/manifest.json
"decomp":
  decomp/                 the decompilation's source at the pinned commit (no game code or data),
                          checked against tree_sha256 before it is used
  tools/decomp_build/     gen.py (function list, glue, unit list), glue.cpp, frames_vis.h
  sdk/decomp/obj/         the runtime objects that differ in the decomp build (main, build info, the
                          decomp runtime); everything else is the normal release's sdk/obj and sdk/lib
  sdk/decomp/include/     decomp/decomp.h for the generated hooks.cpp
  "link"                  the decomp game's link line: {gamecode} as usual, {decomp_glue} = hooks.o and
                          the game units
  "unit_flags", "hooks_flags"  how a game unit and hooks.cpp are compiled ({sdk}, {decomp}, {tools}, {out})

What is made here, from the player's own cking.rpx, in DATA/decomp-work (removed afterwards unless
--keep-work): gen.py's output (the game image, the model data headers, the glued sources, hooks.cpp,
the function list), the game code translated with that list, the objects. The result is
bin/wwhd-decomp (portable releases) or decomp/bin/wwhd (with its runtime files) otherwise.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor, as_completed

DECOMP_TEXT = (
    "Builds a second version of the game in which the game functions of the public decompilation "
    "(ZeldaWWHDDecomp/wwhd) run as compiled source instead of translated code. Experimental, for testing "
    "the decompilation; the normal version stays the default and is built first. Adds 10 to 30 minutes "
    "to setup and about 100 MB on disk.")
DOCS_URL = "https://github.com/ZeldaWWHDRecomp/ZeldaWWHDRecomp/blob/devel/docs/decomp-build.md"
DEV_VERSION = re.compile(r"-devel\.[0-9a-f]+$|^development build$")


def is_dev_version(version):
    """A development build (release.yml devel builds: VERSION-devel.SHA8)."""
    return bool(DEV_VERSION.search(version or ""))


def offered(manifest, is_mac):
    """Setup offers the decomp version only on macOS, in a development build that brings its parts."""
    return bool(is_mac and isinstance(manifest.get("decomp"), dict) and is_dev_version(manifest.get("version")))


OS_FILES = re.compile(r"^(\.DS_Store|\._.*|Thumbs\.db|desktop\.ini)$", re.I)  # what file browsers add


def tree_digest(root):
    """SHA-256 over the relative path and SHA-256 of every file under root (sorted; '/' separators; files a
    file browser adds, such as Finder's .DS_Store, left out). tools/release/package.py writes it into the
    manifest for the bundled decompilation."""
    entries = []
    for dp, dns, fns in os.walk(root):
        dns.sort()
        for fn in fns:
            if OS_FILES.match(fn):
                continue
            p = os.path.join(dp, fn)
            h = hashlib.sha256()
            with open(p, "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            entries.append("%s %s\n" % (h.hexdigest(), os.path.relpath(p, root).replace(os.sep, "/")))
    return hashlib.sha256("".join(sorted(entries, key=lambda e: e[65:])).encode("utf-8")).hexdigest()


def check_source(pkg, manifest, error):
    """The bundled decompilation is the one the release names (revision, tree_sha256)."""
    d = manifest["decomp"]
    root = os.path.join(pkg, "decomp")
    if not os.path.isdir(root):
        raise error("the decompilation (decomp/) is missing from this release: unpack it again")
    if tree_digest(root) != d.get("tree_sha256"):
        raise error("the decompilation in %s does not match this release (SHA-256 check failed): unpack it again"
                    % root)
    return d.get("revision", "")


def subst(arg, m):
    for k, v in m.items():
        arg = arg.replace("{%s}" % k, v)
    return arg


def fwd(p):
    return p.replace("\\", "/")


def exe_path(data_dir, portable):
    """Where the decomp game goes: next to the normal one in a portable release (it shares bin/'s runtime
    files and portable.txt, so settings, saves and shader caches), else decomp/bin/ in the data folder."""
    if portable:
        return os.path.join(data_dir, "bin", "wwhd-decomp")
    return os.path.join(data_dir, "decomp", "bin", "wwhd")


def build(s, ctx, tc, jobs, step=None):
    """Builds the decomp version; `s` is the setup module (run_logged, Progress, compile_gamecode, link_game,
    say, LOG, SetupError, PKG). Returns the path of the built game."""
    manifest, data_dir = ctx.manifest, ctx.data_dir
    d = manifest["decomp"]
    t_all = time.time()
    rev = check_source(s.PKG, manifest, s.SetupError)
    rpx = os.path.join(ctx.game_dir, "code", "cking.rpx")
    build_id = s.game_builds.identify(rpx)
    if build_id is not None and not build_id.canonical:
        raise s.SetupError("the decomp version works with the USA game only (yours is the %s build)" % build_id.name)
    work = os.path.join(data_dir, "decomp-work")
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    out = os.path.join(work, "gen")
    times = {}

    # 1. function list, glue and units: gen.py, reading the game for the model data and the source glue
    t0 = time.time()
    s.say("  Preparing the decompiled source (decomp %s) with your game..." % (rev[:7] or "?"))
    s.run_logged([sys.executable, os.path.join(s.PKG, "tools", "decomp_build", "gen.py"), os.path.join(s.PKG, "decomp"),
                  out, "--game", rpx, "--jobs", str(jobs)], what="preparing the decompiled source (gen.py)")
    with open(os.path.join(out, "units.json")) as f:
        units = json.load(f)["units"]
    times["gen"] = time.time() - t0

    # 2. the game code, translated with the decomp build's function list
    t0 = time.time()
    s.say("  Translating the game code for the decomp version...")
    gen_dir = os.path.join(work, "code")
    s.run_logged([sys.executable, os.path.join(s.PKG, "tools", "recomp", "recomp.py"), rpx, gen_dir,
                  "--decomp-hooks", os.path.join(out, "decomp_hooks.txt")],
                 env=dict(os.environ, WWHD_RECOMP_MOD_HOOKS="0"), what="translating the game code (decomp version)")
    times["translate"] = time.time() - t0

    # 3. compile: the game code, the game units, hooks.cpp
    t0 = time.time()
    objs = s.compile_gamecode(tc, manifest, gen_dir, os.path.join(work, "obj"), jobs)
    times["gamecode"] = time.time() - t0
    t0 = time.time()
    m = {"sdk": fwd(os.path.join(s.PKG, "sdk")), "decomp": fwd(os.path.join(s.PKG, "decomp")),
         "tools": fwd(os.path.join(s.PKG, "tools")), "out": fwd(out)}
    unit_flags = [subst(a, m) for a in d["unit_flags"]]
    hooks_flags = [subst(a, m) for a in d["hooks_flags"]]
    jobs_list = []  # (unit index, source, object, extra flags)
    for i, u in enumerate(units):
        udir = os.path.join(work, "units", str(i))
        os.makedirs(udir, exist_ok=True)
        for k, src in enumerate([x["path"] for x in u["sources"]] + [u["glue"]]):
            quote = u["sources"][k]["quote"] if k < len(u["sources"]) else None
            jobs_list.append((i, src, os.path.join(udir, "%d_%s.o" % (k, os.path.basename(src))),
                              ["-iquote", quote] if quote else []))
    total = len(jobs_list)
    pr = s.Progress("compiling the decompiled game")
    done, failures, lock = [0], [], threading.Lock()

    def one(job):
        _, src, obj, extra = job
        cmd = tc.cxx + unit_flags + extra + ["-c", fwd(src), "-o", fwd(obj)]
        p = subprocess.run(cmd, env=tc.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        with lock:
            if p.returncode:
                failures.append((src, p.stdout.decode("utf-8", "replace")))
                s.LOG.write("$ " + " ".join(cmd) + "\n" + p.stdout.decode("utf-8", "replace"))
            done[0] += 1
            pr.update(done[0], total, "%d of %d files" % (done[0], total))

    s.LOG.write("decomp unit flags: " + " ".join(tc.cxx + unit_flags))
    pr.update(0, total, "0 of %d files (%d at a time)" % (total, jobs), force=True)
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        for f in as_completed([ex.submit(one, j) for j in jobs_list]):
            f.result()
    if failures:
        src, text = failures[0]
        raise s.SetupError("compiling %s failed:\n%s" % (os.path.basename(src), "\n".join(text.strip().splitlines()[-20:])))
    pr.done("%d files" % total)
    # each unit becomes one object: hidden symbols turn local (`ld -r`), so units never see each other
    glue = []
    by_unit = {}
    for i, _, obj, _ in jobs_list:
        by_unit.setdefault(i, []).append(obj)
    for i, u in enumerate(units):
        o = os.path.join(work, "units", "%s.o" % u["name"])
        s.run_logged(["xcrun", "ld", "-r", "-o", o] + by_unit[i], env=tc.env, what="merging the game unit %s" % u["name"])
        glue.append(o)
    hooks_o = os.path.join(work, "hooks.o")
    s.run_logged(tc.cxx + hooks_flags + ["-c", os.path.join(out, "hooks.cpp"), "-o", hooks_o], env=tc.env,
                 what="compiling the decomp build's hooks")
    times["units"] = time.time() - t0

    # 4. link: the release's decomp link line with this game code, hooks and units
    t0 = time.time()
    exe = exe_path(data_dir, s.PORTABLE)
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    new = exe + ".new"
    s.link_game(tc, manifest, objs, work, new, recipe=d["link"], glue=[hooks_o] + glue)
    if not s.PORTABLE:
        for rf in manifest.get("runtime_files", []):
            shutil.copyfile(os.path.join(s.PKG, "sdk", "runtime", rf), os.path.join(os.path.dirname(exe), rf))
    os.replace(new, exe)
    times["link"] = time.time() - t0
    times["total"] = time.time() - t_all
    s.say("  Built the decomp version: %s (%d s: %s)" % (exe, times["total"], ", ".join(
        "%s %d s" % (k, v) for k, v in times.items() if k != "total")))
    s.LOG.write("decomp version times: " + json.dumps({k: round(v, 1) for k, v in times.items()}))
    if not getattr(ctx.args, "keep_work", False):
        shutil.rmtree(work, ignore_errors=True)
    return exe, rev, len(units)
