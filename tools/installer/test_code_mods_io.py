"""The game's metadata reader (runtime/tools/code_mods_io_test.cpp) polls in a tight loop while setup's
atomic publisher replaces the file 500 times (issue #282). Every publish and every read must succeed.
On Windows every publish must take the POSIX-semantics rename, not the os.replace fallback.
Synthetic temporary data only."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import threading

import code_mods

PUBLISHES = 500


def main():
    with tempfile.TemporaryDirectory(prefix='wwhd-status-') as scratch:
        path = Path(scratch) / 'status, progress.json'
        value = {'stage': 'building', 'payload': 'x' * (512 * 1024), 'done': -1}
        code_mods.atomic_json(path, value)
        fallbacks = []
        real_replace = code_mods.os.replace

        def replace(src, dst):
            fallbacks.append(src)
            real_replace(src, dst)
        code_mods.os.replace = replace
        with subprocess.Popen([sys.argv[1], str(path)], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, text=True) as reader:
            assert reader.stdout.readline().strip() == 'ready'
            errors = []
            try:
                for i in range(PUBLISHES):
                    code_mods.atomic_json(path, dict(value, done=i))
            except BaseException as error:
                errors.append(error)
            reader.stdin.write('stop\n')
            reader.stdin.flush()
            reads = reader.stdout.readline().strip()
            assert reader.wait(timeout=30) == 0, 'the game-side reader failed a read'
        code_mods.os.replace = real_replace
        assert not errors, errors
        assert reads.isdigit() and int(reads) > 0, reads
        if os.name == 'nt':
            assert not fallbacks, '%d of %d publishes fell back to os.replace' % (len(fallbacks), PUBLISHES)
        print('%d publishes, %d concurrent reads, %d os.replace fallbacks' % (PUBLISHES, int(reads), len(fallbacks)))


if __name__ == '__main__':
    main()
