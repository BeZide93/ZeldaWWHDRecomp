#!/usr/bin/env python3
"""Build a source-only PowerPC mod, test it, translate it, and package .wwhdmod."""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import zipfile

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]


def stage_name(mod, stage):
    # Match runtime/src/mods/mod_stages_manifest.h::allocate_name, attempt 0.
    value = 1469598103934665603
    for byte in (mod + "\0" + stage).encode():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    result = "Z"
    for _ in range(6):
        result += "0123456789abcdefghijklmnopqrstuvwxyz"[value % 36]
        value //= 36
    return result


def run(command):
    subprocess.run([str(x) for x in command], check=True, cwd=REPO)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=REPO / "build/hyrule-survivors")
    parser.add_argument("--tests-only", action="store_true")
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(os.environ.get("CC", "cc"))
    cxx = shlex.split(os.environ.get("CXX", "c++"))
    manifest = json.loads((HERE / "manifest.json").read_text())
    expected = stage_name(manifest["id"], manifest["stages"][0]["id"])
    (out / "stage_name.h").write_text(f'#define HS_STAGE_NAME "{expected}"\n')
    # The runtime parser includes the catalogue. Embed its checked-in text
    # recipes as CMake does, without configuring the entire game or its assets.
    for folder, namespace, filename in (
        ("game_objects", "story_game_objects", "game_object_recipes.inc"),
        ("game_pieces", "story_game_pieces", "game_piece_recipes.inc"),
    ):
        recipes = []
        for path in sorted((REPO / "runtime/assets" / folder).glob("*.recipe.json")):
            recipe = path.read_text()
            if ')wwhd"' in recipe:
                raise ValueError("recipe contains raw string delimiter: " + str(path))
            recipes.append('R"wwhd(' + recipe + ')wwhd",')
        (out / filename).write_text("namespace " + namespace + " {\n"
            'inline const char* const kRecipes[] = {\n' + "\n".join(recipes) + '\n""};\n}\n')
    run(cc + ["-std=c11", "-Wall", "-Wextra", "-Werror",
              "-O2", "-I", HERE, HERE / "core.c", HERE / "tests/core_test.c", "-o", out / "core-test"])
    run([out / "core-test"])
    run(cxx + ["-std=c++20", "-O1", "-I", REPO / "runtime/src", "-I", out,
               HERE / "tests/manifest_test.cpp", "-o", out / "manifest-test"])
    run([out / "manifest-test", HERE / "manifest.json"])
    if args.tests_only:
        return
    clang = os.environ.get("WWHD_PPC_CLANG", "clang")
    lld = os.environ.get("WWHD_PPC_LLD", "ld.lld")
    host_cc = shlex.split(os.environ.get("WWHD_HOST_CC", clang))
    flags = ["--target=powerpc-unknown-eabi", "-mcpu=750", "-std=c11", "-O2",
             "-ffreestanding", "-fno-builtin", "-nostdlib", "-fno-jump-tables",
             "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
             "-I", REPO / "runtime/guest/include", "-I", out]
    objects = []
    for stem in ("core", "game", "hud"):
        obj = out / (stem + ".o")
        run([clang] + flags + ["-c", HERE / (stem + ".c"), "-o", obj])
        objects.append(obj)
    package = out / "package"
    package.mkdir(exist_ok=True)
    run([lld, "-m", "elf32ppc", "-r", *objects, "-o", package / "mod.elf"])
    (package / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (package / "README.md").write_text((HERE / "README.md").read_text())
    # Exercise the exact install-time translator and host compiler on BOTH game
    # regions. No game dump is needed to translate a mod's own code.
    sys.path.insert(0, str(REPO / "tools/guestmod"))
    import build_guest_mod as builder
    from builds import by_name
    elf = (package / "mod.elf").read_bytes()
    for region in ("USA", "EU"):
        game_build = by_name(region)
        translated = builder.translator_for(elf, 0x7F000000, game_build)
        code = out / ("translated-" + region + ".c")
        code.write_text(translated.emit(manifest["id"]))
        run(host_cc + ["-O1", "-fPIC", "-shared", "-ffp-contract=off", "-fno-strict-aliasing", "-w",
                  "-I", REPO / "runtime/include", code, "-o", out / ("translated-" + region + ".so")])
        # A synthetic host executes the shipped PowerPC adapter after translation.
        symbols = {s["name"]: translated.sec_addr[s["shndx"]] + s["value"]
                   for s in translated.elf.symbols if s["shndx"] in translated.sec_addr}
        addresses = {"ADDR_HS": symbols["hs"], "ADDR_ACTION": symbols["hs_action"],
                     "ADDR_SESSION": symbols["hs_session"], "DATA_SAVE": game_build.data(0x101F84DC)}
        for name, address in {"LINK": 0x0240EBB0, "PLAY": 0x025200D4, "GROUND": 0x024F1350,
                              "SEARCH": 0x025D54C4, "DELETE": 0x025D57E4, "CREATE": 0x025D5834,
                              "DAMAGE": 0x023F51D0, "SERIALIZE": 0x025BA9FC, "LOAD": 0x02721908}.items():
            addresses["GAME_" + name] = game_build.code(address)
        (out / "test_addresses.h").write_text("".join(f"#define {name} 0x{address:08X}u\n"
                                                       for name, address in addresses.items()))
        run(host_cc + ["-O1", "-I", REPO / "runtime/include", "-I", HERE, "-I", out,
                       HERE / "tests/adapter_test.c", "-ldl", "-lm", "-o", out / "adapter-test"])
        run([out / "adapter-test", out / ("translated-" + region + ".so")])
        print(region, "translation passed:", len(translated.hooks), "hooks", flush=True)
    archive = out / ("hyrule-survivors-" + manifest["version"] + ".wwhdmod")
    with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as z:
        for name in ("manifest.json", "mod.elf", "README.md"):
            z.write(package / name, name)
    print(archive)


if __name__ == "__main__":
    main()
