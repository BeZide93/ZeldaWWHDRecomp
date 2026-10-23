"""Real game metadata reader versus setup's atomic publisher; synthetic temporary data only."""
from pathlib import Path
import subprocess
import sys
import tempfile
import threading

import code_mods


def main():
    with tempfile.TemporaryDirectory(prefix='wwhd-status-') as scratch:
        path = Path(scratch) / 'status, progress.json'
        value = {'stage': 'building', 'payload': 'x' * (512 * 1024)}
        code_mods.atomic_json(path, value)
        with subprocess.Popen([sys.argv[1], str(path)], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, text=True) as reader:
            errors = []
            def publish():
                try:
                    for i in range(500):
                        code_mods.atomic_json(path, dict(value, done=i))
                except BaseException as error:
                    errors.append(error)
            writer = threading.Thread(target=publish)
            writer.start()
            reads = 0
            while writer.is_alive() or reads < 100:
                reader.stdin.write('read\n')
                reader.stdin.flush()
                assert reader.stdout.readline().strip() == 'read'
                reads += 1
            writer.join()
            reader.stdin.write('stop\n')
            reader.stdin.flush()
            assert reader.wait(timeout=10) == 0
            assert not errors, errors


if __name__ == '__main__':
    main()
