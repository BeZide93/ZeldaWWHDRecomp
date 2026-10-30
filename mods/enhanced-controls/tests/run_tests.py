#!/usr/bin/env python3
"""Test the policy and execute the actual translated PPC module for USA and EU."""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import zipfile

HERE=Path(__file__).resolve().parent
REPO=HERE.parents[2]
OUT=REPO/'build/enhanced-controls'
CC=shlex.split(os.environ.get('CC','clang'))
sys.path.insert(0,str(REPO/'tools/recomp'))
from builds import by_name


def run(args):
    subprocess.run([str(a) for a in args],check=True)


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    manifest=json.loads((HERE.parent/'manifest.json').read_text())
    archive=OUT/f"enhanced-controls-{manifest['version']}.wwhdmod"
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        assert json.loads(z.read('manifest.json'))==manifest
        assert z.read('src/mod.c')==(HERE.parent/'mod.c').read_bytes()
        assert z.read('src/core.h')==(HERE.parent/'core.h').read_bytes()
    run(CC+['-std=c11','-Wall','-Wextra','-Werror',HERE/'core_test.c','-lm','-o',OUT/'core-test'])
    run([OUT/'core-test'])
    for region in ('USA','EU'):
        b=by_name(region)
        defs={'PLAY':b.data(0x1046f0b0),'SAVE_PTR':b.data(0x101f84dc),
              'MENU_FLAG':b.data(0x101ea069),'LINK_EXECUTE':b.code(0x0240ebb0),
              'STORE':b.code(0x024f8430),'COLLISION':b.code(0x024fcbe8)}
        (OUT/'fixture.h').write_text(''.join(f'#define {k} 0x{v:08X}u\n' for k,v in defs.items()))
        run(CC+['-std=c11','-O1','-I',REPO/'runtime/include','-I',OUT,
                OUT/f'translated-{region}.c',HERE/'module_test.c','-lm','-o',OUT/f'module-test-{region}'])
        run([OUT/f'module-test-{region}'])
        print(region,'passed',flush=True)


if __name__=='__main__':
    main()
