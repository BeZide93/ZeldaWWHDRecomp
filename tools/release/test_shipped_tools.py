#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The release's Python tools start as the Windows release starts them (issue #280).

The Windows release runs its tools with the official embeddable Python, which is isolated: the folder of the script is
not on sys.path, so a tool that imports a sibling module (actor_model.py imports notebook_model.py) must put its own
folder there itself. On Windows every own model of the world editor failed to prepare with "No module named
'notebook_model'", and the editor said it would appear after a restart.

Checks, with no game files:
- every tools/story_mods script the game starts (runtime/src/mods/*.cpp) is a file the release ships (TOOL_FILES);
- every shipped story_mods tool, copied alone into a folder laid out as the release is, starts with "python -I" (the
  isolated mode the embeddable Python has) from another working directory: its imports resolve.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import package  # noqa: E402


def started_by_the_game():
    """story_mods scripts the runtime starts: the "<name>.py" strings of its mod sources that are such scripts"""
    names = set()
    folder = os.path.join(ROOT, "runtime", "src", "mods")
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".cpp"):
            continue
        with open(os.path.join(folder, name), encoding="utf-8", errors="replace") as f:
            text = f.read()
        if '"story_mods"' not in text:
            continue
        for script in re.findall(r'"([a-z_]+\.py)"', text):
            if os.path.isfile(os.path.join(ROOT, "tools", "story_mods", script)):
                names.add("tools/story_mods/" + script)
    return names


class ShippedTools(unittest.TestCase):
    def test_every_tool_the_game_starts_is_shipped(self):
        started = started_by_the_game()
        self.assertIn("tools/story_mods/actor_model.py", started)
        self.assertIn("tools/story_mods/kit_model.py", started)
        missing = sorted(started - set(package.TOOL_FILES))
        self.assertEqual(missing, [], "the game starts these tools, but the release doesn't ship them")

    def test_shipped_story_tools_start_isolated(self):
        shipped = [rel for rel in package.TOOL_FILES if rel.startswith("tools/story_mods/") and rel.endswith(".py")]
        self.assertTrue(shipped)
        with tempfile.TemporaryDirectory() as temporary:
            for rel in package.TOOL_FILES:
                target = os.path.join(temporary, rel)
                os.makedirs(os.path.dirname(target), exist_ok=True)
                shutil.copy2(os.path.join(ROOT, rel), target)
            elsewhere = os.path.join(temporary, "elsewhere")
            os.makedirs(elsewhere)
            env = {k: v for k, v in os.environ.items() if k not in ("PYTHONPATH", "PYTHONHOME", "PYTHONSTARTUP")}
            env["PYTHONDONTWRITEBYTECODE"] = "1"
            for rel in shipped:
                with self.subTest(tool=rel):
                    result = subprocess.run([sys.executable, "-I", os.path.join(temporary, rel), "--help"], cwd=elsewhere, env=env,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=120)
                    self.assertNotIn("ModuleNotFoundError", result.stderr)
                    self.assertNotIn("ImportError", result.stderr)
                    self.assertEqual(result.returncode, 0, result.stderr[-400:])


if __name__ == "__main__":
    unittest.main()
