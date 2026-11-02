#!/usr/bin/env python3
"""Build/validate an asset-free Dark Link package. Requires PowerPC clang (or Zig)."""
import argparse
import json
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import zipfile
import struct
import zlib
import binascii

SOURCE = Path(__file__).resolve().parent
ROOT = SOURCE.parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="clang", help="PowerPC-capable clang command, or 'python -m ziglang cc'")
    parser.add_argument("--host-cc", default="clang", help="Clang-compatible host compiler (the runtime uses musttail)")
    parser.add_argument("--out", type=Path, default=ROOT / "build/dark-link")
    args = parser.parse_args()
    out = args.out.resolve()
    package = out / "package"
    package.mkdir(parents=True, exist_ok=True)
    cc, host = shlex.split(args.cc), shlex.split(args.host_cc)
    # Zig's eabi defaults to soft float; the game's ABI passes floats in FPRs.
    target = "powerpc-freestanding-eabihf" if any("zig" in part for part in cc) else "powerpc-unknown-eabi"
    # A single translation unit already produces the relocatable ELF required by
    # the manager. No linker, game dump, native libraries or game assets needed.
    subprocess.run(cc + ["-target", target, "-mcpu=750", "-O2", "-ffreestanding", "-fno-builtin",
        "-nostdlib", "-fno-jump-tables", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra", "-Werror",
        "-I" + str(ROOT / "runtime/guest/include"), "-c", str(SOURCE / "mod.c"), "-o", str(package / "mod.elf")], check=True)
    for filename in ("manifest.json", "README.md"):
        shutil.copyfile(SOURCE / filename, package / filename)
    test = out / "test-state"
    subprocess.run(host + ["-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", str(SOURCE / "test_state.c"), "-o", str(test)], check=True)
    subprocess.run([str(test)], check=True)
    art_test, art_png = out / "test-art", out / "synthetic-art.png"
    subprocess.run(host + ["-std=c11", "-O2", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
        str(SOURCE / "test_art.c"), "-o", str(art_test)], check=True)
    subprocess.run([str(art_test), str(art_png)], check=True)
    # An independent standard-library PNG reader checks chunk CRCs and zlib.
    png, at, compressed = art_png.read_bytes(), 8, b""
    while at < len(png):
        size = struct.unpack_from(">I", png, at)[0]
        kind, data = png[at+4:at+8], png[at+8:at+8+size]
        assert binascii.crc32(kind+data) == struct.unpack_from(">I", png, at+8+size)[0]
        if kind == b"IDAT": compressed += data
        at += size + 12
    pixels = zlib.decompress(compressed)
    assert len(pixels) == 4 * 17 and pixels[0] == 0
    assert pixels[1:5] == bytes([255, 99, 99, 255])
    assert pixels[5:9] == bytes([255, 99, 99, 0])
    sys.path.insert(0, str(ROOT / "tools/guestmod"))
    import build_guest_mod as builder
    from builds import by_name
    for region in ("USA", "EU"):
        translated = builder.translator_for((package / "mod.elf").read_bytes(), 0x7F000000, by_name(region))
        translated.emit("dark-link")
        # Exercise the actual install-time translator AND host compilation.
        result = builder.build(str(package), str(out / "cache"), 0x7F000000,
            host, str(ROOT / "runtime/include"), game_build=by_name(region))
        supported = {"wwhd_hud_register", "wwhd_touch_register", "wwhd_input_filter_register",
            "wwhd_config_changed", "wwhd_config_bool", "wwhd_config_int", "wwhd_logic_step",
            "wwhd_touch_claim", "wwhd_logic_dt", "wwhd_hud_emit", "wwhd_hud_epoch",
            "wwhd_hud_texture", "wwhd_hud_release", "wwhd_file_write", "wwhd_setting_get", "memcpy", "memset"}
        unexpected = set(translated.services) - supported
        if unexpected:
            raise RuntimeError(f"Unsupported imports (check hard-float ABI): {sorted(unexpected)}")
        print(json.dumps({"region": region, **result}), flush=True)
        if sys.platform.startswith("linux") and region == "USA":
            driver = out / "test-hooks"
            subprocess.run(host + ["-std=c11", "-D_GNU_SOURCE", "-O2", "-UNDEBUG", "-I" + str(ROOT / "runtime/include"),
                str(SOURCE / "test_hooks.c"), "-ldl", "-o", str(driver)], check=True)
            trace = out / "hud-trace.bin"
            subprocess.run([str(driver), result["module"], str(trace)], check=True)
            # Replay the emitted guest ABI records with the real runtime validator.
            # C++ is a separate executable: no C driver ABI/linker assumptions.
            validator = out / "test-hud"
            stdlib = "-lc++" if any("zig" in part for part in host) else "-lstdc++"
            subprocess.run(host + ["-x", "c++", "-std=c++20", "-O2", "-UNDEBUG",
                "-Wall", "-Wextra", "-Werror", "-Wno-missing-field-initializers", str(SOURCE / "test_hud.cpp"), stdlib,
                "-o", str(validator)], check=True)
            subprocess.run([str(validator), str(trace)], check=True)
    archive = out / "dark-link.wwhdmod"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for name in ("manifest.json", "mod.elf", "README.md"):
            z.write(package / name, name)
    print(f"Package: {archive}")


if __name__ == "__main__":
    main()
