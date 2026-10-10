"""Prepare private shader caches from the player's files; never package the output.

A failed/cancelled generation never replaces a previously usable generation.
The runtime consumes active.json; native warming is deliberately separate from
head-start assembly so Android can finish warming inside its renderer lifecycle.
"""
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

SCHEMA = 1


def file_hash(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def identity(game, template, package, renderer):
    game = Path(game)
    archives = sorted(p for p in game.rglob('*') if p.is_file() and
                      p.suffix.lower() in ('.sharcfb', '.rpx', '.szs', '.pack', '.sarc'))
    if not archives:
        raise ValueError('No game shader archives or executable found')
    return dict(schema=SCHEMA, renderer=renderer,
                template=file_hash(template), translator=file_hash(Path(package) / 'tools/shaderprep.py'),
                format=file_hash(Path(package) / 'tools/shaderprep_format.py') if (Path(package) / 'tools/shaderprep_format.py').is_file() else None,
                game={str(p.relative_to(game)): file_hash(p) for p in archives})


def atomic_json(path, value):
    path = Path(path)
    # Different setup/startup processes must never share a temporary filename.
    descriptor, name = tempfile.mkstemp(prefix=path.name + '.new-', dir=path.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            stream.write(json.dumps(value, sort_keys=True, indent=2) + '\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


@contextmanager
def activation_lock(root, timeout=5.0):
    """Shared with native publication: exclusive first-byte OS lock, bounded wait.

    The file persists; only the OS lock conveys ownership and process death
    releases it. Compilation deliberately happens outside this short lock.
    """
    root = Path(root)
    root.mkdir(parents=True, exist_ok=True)
    with (root / 'activation.lock').open('a+b') as stream:
        if os.fstat(stream.fileno()).st_size == 0:
            stream.write(b'\0')
            stream.flush()
        if os.name == 'nt':
            import msvcrt
            def acquire():
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            def release():
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
        else:
            import fcntl
            def acquire():
                fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            def release():
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)
        deadline = time.monotonic() + timeout
        while True:
            try:
                acquire()
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise TimeoutError('Shader cache activation is busy')
                time.sleep(.05)
        try:
            yield
        finally:
            release()


def cache_environment(generation, game, renderer):
    generation = Path(generation)
    return dict(WWHD_GAME=str(game), WWHD_HEADSTART=str(generation / 'headstart.bin'),
                WWHD_SHADER_CACHE=str(generation / 'shaders.bin'),
                WWHD_VK_SHADER_CACHE=str(generation / 'vulkan-shaders'),
                WWHD_VK_PIPELINE_CACHE=str(generation / 'vulkan-pipelines.bin'),
                WWHD_RENDERER_RUNTIME=renderer, WWHD_NO_AUDIO='1', WWHD_HIDDEN_WINDOWS='1')


def run_process(command, env, cancel, progress):
    # Pipe-free log prevents a blocked child when verbose compilers fill stdout.
    with tempfile.TemporaryFile(mode='w+b') as log:
        child = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            while child.poll() is None:
                if cancel():
                    raise InterruptedError('Shader preparation skipped')
                time.sleep(.1)
            log.seek(0)
            text = log.read().decode(errors='replace')
            for line in text.splitlines():
                progress(line)
            if child.returncode:
                raise RuntimeError('Shader preparation command failed (%d)' % child.returncode)
        finally:
            if child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait()


def prepare(game, template, executable, cache_root, package, renderer='vulkan',
            progress=lambda text: None, cancel=lambda: False, skip=False,
            runner=run_process, python=None, builder=None, runtime_identity=None):
    """Return a nonfatal status. executable=None assembles Android's pending warm.

    Paths are private local output only. No caller-supplied source is removed.
    """
    if not Path(template).is_file():
        message = 'Shader template unavailable; shaders will compile during play'
        progress(message)
        return dict(status='skipped', message=message)
    root = Path(cache_root)
    staging = None
    wanted = None
    try:
        wanted = identity(game, template, package, renderer)
        runtime = runtime_identity or executable
        if runtime and Path(runtime).is_file():
            wanted['runtime'] = file_hash(runtime)
        if skip or cancel():
            with activation_lock(root):
                atomic_json(root / 'skip.json', dict(schema=SCHEMA, identity=wanted))
                (root / 'rebuild.request').unlink(missing_ok=True)
            return dict(status='skipped', message='Shaders will compile during play')
        if (root / 'skip.json').is_file() and not (root / 'rebuild.request').exists():
            skipped = json.loads((root / 'skip.json').read_text())
            if skipped.get('schema') == SCHEMA and skipped.get('identity') == wanted:
                return dict(status='skipped', message='Shaders will compile during play')
        active = root / 'active.json'
        if active.is_file() and not (root / 'rebuild.request').exists():
            previous = json.loads(active.read_text())
            name = previous.get('generation', '')
            generation = root / name
            if (previous.get('identity') == wanted and previous.get('status') == 'ready'
                    and name.startswith('generation-') and Path(name).name == name
                    and generation.parent == root and (generation / 'headstart.bin').is_file()):
                return dict(status='ready', generation=str(generation), reused=True)
        root.mkdir(parents=True, exist_ok=True)
        staging = Path(tempfile.mkdtemp(prefix='generation-', dir=root))
        progress('Resolving shaders from your game files')
        import sys
        command = [python or sys.executable, str(Path(package) / 'tools/shaderprep.py'),
                   'build', str(staging / 'headstart.bin'), '--game', str(game), '--template', str(template),
                   '--max', '0']
        env = os.environ.copy()
        for key in list(env):
            if key.startswith('WWHD_VK_TEST_') or key == 'WWHD_VK_GRAPH_DIAGNOSTICS':
                env.pop(key)
        if builder:
            builder(game, staging / 'headstart.bin', template)
        else:
            runner(command, env, cancel, progress)
        if cancel():
            raise InterruptedError('Shader preparation skipped')
        if not (staging / 'headstart.bin').is_file():
            raise RuntimeError('Shader preparation produced no head start')
        status = 'warm_pending'
        if executable:
            progress('Compiling shaders and pipelines for this computer')
            env.update(cache_environment(staging, game, renderer))
            runner([str(executable), '--game', str(game), '--warm-shaders'], env, cancel, progress)
            status = 'ready'
        result = dict(schema=SCHEMA, status=status, identity=wanted, generation=staging.name)
        with activation_lock(root):
            atomic_json(staging / 'manifest.json', result)
            atomic_json(active, result)
            if status == 'ready':
                (root / 'rebuild.request').unlink(missing_ok=True)
            (root / 'skip.json').unlink(missing_ok=True)
        return dict(status=status, generation=str(staging), reused=False)
    except Exception as error:
        if staging:
            shutil.rmtree(staging, ignore_errors=True)
        if isinstance(error, InterruptedError) and wanted is not None:
            try:
                with activation_lock(root):
                    atomic_json(root / 'skip.json', dict(schema=SCHEMA, identity=wanted))
                    (root / 'rebuild.request').unlink(missing_ok=True)
            except OSError:
                pass
        progress('Shader preparation unavailable: %s. The game can still run.' % error)
        return dict(status='skipped' if isinstance(error, InterruptedError) else 'failed', message=str(error))


def default_cache_root(data_dir=None, portable=False):
    if os.environ.get('WWHD_USER_DIR'):
        base = Path(os.environ['WWHD_USER_DIR'])
    elif portable:
        base = Path(data_dir) / 'user'
    elif os.sys.platform == 'darwin':
        base = Path.home() / 'Library/Application Support/WWHD'
    elif os.sys.platform.startswith('win'):
        base = Path(os.environ.get('APPDATA', '.')) / 'WWHD'
    else:
        base = Path(os.environ.get('XDG_CONFIG_HOME', str(Path.home() / '.config'))) / 'wwhd'
    return base / 'shader-prebuild'


def main(argv=None):
    import argparse
    import sys
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime-prepare', action='store_true', required=True)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--template', required=True, type=Path)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--cache-root', required=True, type=Path)
    parser.add_argument('--package', required=True, type=Path)
    parser.add_argument('--renderer', choices=('metal', 'vulkan'), required=True)
    parser.add_argument('--skip', action='store_true')
    parser.add_argument('--cancel-file', type=Path)
    args = parser.parse_args(argv)
    result = prepare(args.game, args.template, args.executable, args.cache_root, args.package,
                     renderer=args.renderer, skip=args.skip,
                     progress=lambda text: print(text, file=sys.stderr, flush=True),
                     cancel=lambda: bool(args.cancel_file and args.cancel_file.exists()))
    print(json.dumps(result), flush=True)
    return 0 if result['status'] == 'ready' else 1


if __name__ == '__main__':
    raise SystemExit(main())
