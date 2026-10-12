#!/usr/bin/env python3
"""The AppImage's AppRun passes the environment through unchanged (issue #55).

Portable use is the AppImage runtime's standard one (a "<AppImage>.home" folder becomes $HOME,
"<AppImage>.config" becomes $XDG_CONFIG_HOME); the runtime sets those before AppRun runs. AppRun
must not add a mode of its own (the "<AppImage>.data" folder of v0.2.13 is gone) and must not
touch HOME or the XDG variables.
"""
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import appimage  # noqa: E402

KEYS = ("HOME", "XDG_DATA_HOME", "XDG_CONFIG_HOME", "XDG_CACHE_HOME")


def code_lines(script):
    return [line for line in script.splitlines() if line.strip() and not line.lstrip().startswith("#")]


def run(env_extra, make=()):
    with tempfile.TemporaryDirectory() as temporary:
        appdir = os.path.join(temporary, "AppDir")
        os.makedirs(appdir)
        apprun = os.path.join(appdir, "AppRun")
        with open(apprun, "w") as f:
            f.write(appimage.APPRUN)
        launcher = os.path.join(appdir, "wind-waker-hd")
        with open(launcher, "w") as f:  # stands in for the launcher: report what it was given
            f.write('#!/bin/sh\necho "home=$HOME data=$XDG_DATA_HOME config=$XDG_CONFIG_HOME '
                    'cache=$XDG_CACHE_HOME args=$*"\n')
        os.chmod(apprun, 0o755)
        os.chmod(launcher, 0o755)
        image = os.path.join(temporary, "Wind Waker [HD].AppImage")
        for suffix in make:
            os.makedirs(image + suffix)
        env = {k: v for k, v in os.environ.items() if not k.startswith("XDG_") and k not in ("APPIMAGE", "HOME")}
        env.update({k: v.replace("@IMAGE@", image) for k, v in env_extra.items()})
        out = subprocess.run(["sh", apprun, "--setup"], env=env, capture_output=True, text=True,
                             check=True).stdout.strip()
        return out, image


def main():
    code = "\n".join(code_lines(appimage.APPRUN))
    assert ".data" not in code, code  # no AppRun-specific portable folder any more
    for key in KEYS:
        assert not re.search(r"\b%s=" % key, code), (key, code)  # leaves HOME and XDG_* to the runtime
    assert "APPIMAGE" not in code, code

    # outside an AppImage, and inside one without portable folders: nothing changes
    out, _ = run({"HOME": "/home/player"})
    assert out == "home=/home/player data= config= cache= args=--setup", out
    out, _ = run({"HOME": "/home/player", "APPIMAGE": "@IMAGE@"}, make=(".data",))
    assert out == "home=/home/player data= config= cache= args=--setup", out  # a leftover .data folder is ignored

    # what the runtime does for "<image>.home" / "<image>.config": AppRun hands it on as it is
    # (spaces and brackets in the path)
    out, image = run({"APPIMAGE": "@IMAGE@", "HOME": "@IMAGE@.home", "XDG_CONFIG_HOME": "@IMAGE@.config"},
                     make=(".home", ".config"))
    want = "home=%s.home data= config=%s.config cache= args=--setup" % (image, image)
    assert out == want, (out, want)
    print("AppRun: environment passed through, no .data mode")


if __name__ == "__main__":
    main()
