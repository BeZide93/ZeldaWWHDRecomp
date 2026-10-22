"""Preparing shaders: models. The game draws every model of every object archive once, hidden and muted, from
its title screen (no save needed), so the renderer records the pipelines those models need for this build and
this computer; in play they are compiled when their archive loads instead of when a model first appears.

Nothing is shipped: the archive list is made of names from the player's own game files, and the results are the
player's normal shader caches (the live folder of the shader preparation). The sweep keeps its progress (names
only) next to those caches, so a cancelled or crashed sweep resumes where it stopped; a model that crashes the
game is skipped on the next attempt (skip.txt, names only).
"""
from contextlib import ExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import tempfile
import time

SCHEMA = 1
# Rooms, stages and the sea's island LODs are stage resources (<stage>_<archive>.szs), loaded by the stage loader.
NOT_OBJECTS = re.compile(r'(_Stage|_Room\d+|_LOD\d+|_LODALL)$')
STAGE_ARCHIVE = re.compile(r'^(.+)_(Stage|Room\d+|LOD\d+|LODALL)$')
MAX_ATTEMPTS = 40          # attempts in a row that finish no archive or actor (each skips what it stopped at)
MAX_ATTEMPTS_TOTAL = 1000  # attempts in all (actors that stop the game: a few seconds each)
DEFAULT_SKIP = Path(__file__).with_name('model_sweep_skip.txt')
# models drawn per batch (two frames each); WWHD_SETUP_MODEL_SWEEP_BATCH=n for measurements only
MODELS_PER_BATCH = max(1, min(8, int(os.environ.get('WWHD_SETUP_MODEL_SWEEP_BATCH', '1') or 1)))
STARTUP_SECONDS = 300     # until an attempt's first progress line (the game starting up)
STALL_SECONDS = 60         # no new line in progress.txt for this long: the attempt counts as a crash (a model, room or
                           # actor takes well under a second; a stuck game writes nothing more)


def _pack_entries(path):
    """Names in a SARC .pack file (the bundled .szs archives); no file data is read beyond the name table."""
    with open(path, 'rb') as stream:
        data = stream.read()
    if data[:4] != b'SARC' or len(data) < 0x20:
        return []
    order = '>' if data[6:8] == b'\xfe\xff' else '<'
    header = struct.unpack_from(order + 'H', data, 4)[0]
    count = struct.unpack_from(order + 'H', data, header + 6)[0]
    names_at = header + 12 + 16 * count
    out = []
    for i in range(count):
        _, attributes, _, _ = struct.unpack_from(order + 'IIII', data, header + 12 + 16 * i)
        if attributes & 0x01000000:
            at = names_at + 8 + (attributes & 0xFFFF) * 4
            end = data.find(b'\0', at)
            if end > at:
                out.append(data[at:end].decode('ascii', errors='replace'))
    return out


def object_archives(game):
    """Object archive names of the player's game: content/Common/Object/*.szs and the .szs archives bundled in
    content/Common/Pack/*.pack (not the 2D layout packs), without rooms, stages and island LODs."""
    common = Path(game) / 'content' / 'Common'
    names = set()
    objects = common / 'Object'
    if objects.is_dir():
        names.update(p.stem for p in objects.iterdir() if p.suffix == '.szs' and p.is_file())
    packs = common / 'Pack'
    if packs.is_dir():
        for pack in sorted(packs.glob('*.pack')):
            if pack.name.startswith('permanent_2d'):
                continue
            for entry in _pack_entries(pack):
                if entry.endswith('.szs'):
                    names.add(entry.rsplit('/', 1)[-1][:-4])
    return sorted(n for n in names if n and len(n) < 0x40 and n.isprintable() and not NOT_OBJECTS.search(n))


def stage_archives(game):
    """The stage resources of the player's game, {stage: [archive, ...]}: content/Common/Stage/<stage>_<archive>.szs
    and those bundled in the packs (Stage, Room<n>, LOD<n>, LODALL); names only."""
    common = Path(game) / 'content' / 'Common'
    names = set()
    stages = common / 'Stage'
    if stages.is_dir():
        names.update(p.stem for p in stages.iterdir() if p.suffix == '.szs' and p.is_file())
    packs = common / 'Pack'
    if packs.is_dir():
        for pack in sorted(packs.glob('*.pack')):
            if not pack.name.startswith('permanent_2d'):
                names.update(e.rsplit('/', 1)[-1][:-4] for e in _pack_entries(pack) if e.endswith('.szs'))
    out = {}
    for name in names:
        match = STAGE_ARCHIVE.match(name)
        if match and len(name) < 0x40 and name.isprintable() and ' ' not in name:
            out.setdefault(match.group(1), []).append(match.group(2))

    def order(archive):  # the stage archive first, then rooms by number, then LODs (LODALL last)
        digits = re.sub(r'\D', '', archive)
        return (0 if archive == 'Stage' else 1 if archive.startswith('Room') else 2, int(digits) if digits else 1 << 30)
    return {stage: sorted(archives, key=order) for stage, archives in sorted(out.items())}


def _lines(path):
    try:
        return [l.strip() for l in Path(path).read_text(encoding='utf-8', errors='replace').splitlines() if l.strip()]
    except OSError:
        return []


def culprit(progress_lines, skip):
    """The model, archive or actor the last attempt stopped at (the last one it started), unless it is skipped
    already."""
    start = len(progress_lines) - 1 - progress_lines[::-1].index('start') if 'start' in progress_lines else 0
    for line in reversed(progress_lines[start:]):
        if line.startswith(('model ', 'load ', 'actor ', 'env ', 'visit ', 'particles ')):
            name = line.split(' ', 1)[1]
            return None if name in skip else name
    return None


def state_dir(cache_root, renderer):
    return Path(cache_root) / 'model-sweep' / renderer


def status(cache_root, renderer, identity):
    """'done', 'partial' (progress kept for this identity) or 'none'."""
    state = state_dir(cache_root, renderer)
    try:
        saved = json.loads((state / 'identity.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return 'none'
    if saved != identity:
        return 'none'
    return 'done' if 'sweep-done' in _lines(state / 'progress.txt') else 'partial'


def environment(base, game, cache_env, renderer, state, archives, scratch, stages=None):
    """The sweep's environment: hidden, muted, no host input, settings and states in a scratch folder (the
    player's own are never touched), the shader caches of the preparation (cache_env)."""
    env = {k: v for k, v in base.items()
           if not (k.startswith('WWHD_') and k not in ('WWHD_USER_DIR', 'WWHD_INSTALL_DIR'))}
    env.update(cache_env)
    env.update(WWHD_RENDERER_RUNTIME=renderer, WWHD_NO_AUDIO='1', WWHD_HIDDEN='1', WWHD_HIDDEN_WINDOWS='1',
               WWHD_NO_HOST_INPUT='1', WWHD_NO_GAMEPAD='1', WWHD_LAST_SESSION='0',
               WWHD_SETTINGS=str(scratch / 'settings.ini'), WWHD_DISPLAY_SETTINGS=str(scratch / 'display.plist'),
               WWHD_CONTROLS=str(scratch / 'controls.json'), WWHD_STATE_DIR=str(scratch / 'states'),
               WWHD_DSWEEP='title,%d,2' % MODELS_PER_BATCH, WWHD_DSWEEP_ARCHIVES=str(archives),
               WWHD_DSWEEP_STATE=str(state),
               WWHD_DSWEEP_EXIT='1',
               # no frame limit: the game runs as fast as it can draw (faster than real time; nobody watches)
               WWHD_UNCAPPED='1')
    default_skip = [n for n in _lines(DEFAULT_SKIP) if not n.startswith('#')]
    if default_skip:
        env['WWHD_DSWEEP_SKIP'] = ','.join(default_skip)
    if stages:
        env['WWHD_DSWEEP_STAGES'] = str(stages)
        # then the title's scene at other times of day and in a storm, every actor the rooms place, and the file
        # select (dsweep.cpp)
        env.update(WWHD_DSWEEP_ENV='1', WWHD_DSWEEP_PARTICLES='1', WWHD_DSWEEP_ACTORS='1', WWHD_DSWEEP_FILE_SELECT='1')
    env['WWHD_HEADSTART'] = '0'  # the sweep records pipelines; it warms nothing (and holds no generation)
    return env


def run(executable, game, cache_root, renderer, identity, cache_env, line=lambda text: None,
        cancel=lambda: False, log=lambda text: None, popen=subprocess.Popen, clock=time.monotonic,
        sleep=time.sleep, attempts=MAX_ATTEMPTS, stall=STALL_SECONDS):
    """Sweeps (or resumes) for one renderer. line(text) gets the game's output (its 'WWHD-PROGRESS done total
    archive' lines carry the progress). Returns 'done' or 'failed'; raises InterruptedError when cancel()."""
    state = state_dir(cache_root, renderer)
    state.mkdir(parents=True, exist_ok=True)
    try:
        saved = json.loads((state / 'identity.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        saved = None
    if saved != identity:
        # another build or game: its pipelines do not match; start over (crashing models stay skipped for the
        # same game files)
        (state / 'progress.txt').unlink(missing_ok=True)
        if not isinstance(saved, dict) or saved.get('game') != identity.get('game'):
            (state / 'skip.txt').unlink(missing_ok=True)
        (state / 'identity.json').write_text(json.dumps(identity, sort_keys=True), encoding='utf-8')
    archives = state / 'archives.txt'
    archives.write_text(''.join(n + '\n' for n in object_archives(game)), encoding='utf-8')
    # stage visits: enter every stage at its first room and draw it under its own environment (dsweep.cpp);
    # WWHD_SETUP_MODEL_SWEEP_VISITS=0 leaves them out
    visits = None
    if os.environ.get('WWHD_SETUP_MODEL_SWEEP_VISITS', '1') != '0':
        visits = state / 'visits.txt'
        rooms = {s: sorted(int(a[4:]) for a in names if a.startswith('Room')) for s, names in stage_archives(game).items()}
        visits.write_text(''.join('%s %d\n' % (s, r[0]) for s, r in rooms.items() if r and s not in ('sea_T', 'Name')),
                          encoding='utf-8')
    stages = state / 'stages.txt'
    stages.write_text(''.join('%s %s\n' % (stage, ' '.join(names)) for stage, names in stage_archives(game).items()),
                      encoding='utf-8')
    progress_file = state / 'progress.txt'
    def finished():
        return sum(1 for l in _lines(progress_file) if l.startswith(('archive-done ', 'actor-done ')))
    without_progress, before = 0, finished()
    for attempt in range(1, MAX_ATTEMPTS_TOTAL + 1):
        if without_progress >= attempts:
            break
        if 'sweep-done' in _lines(progress_file):
            return 'done'
        if cancel():
            raise InterruptedError('Model preparation paused')
        with ExitStack() as stack:
            try:  # Windows: a terminated game may briefly keep its output open
                folder = tempfile.TemporaryDirectory(prefix='wwhd-model-sweep-', ignore_cleanup_errors=True)
            except TypeError:  # Python < 3.10
                folder = tempfile.TemporaryDirectory(prefix='wwhd-model-sweep-')
            scratch = Path(stack.enter_context(folder))
            env = environment(os.environ, game, cache_env, renderer, state, archives, scratch, stages)
            if visits:
                # each stage's first room; WWHD_SETUP_MODEL_SWEEP_VISITS=all: every room with a player start, once per
                # scene layer (measured on 19 places: no gain over the first rooms, 22 more minutes)
                env['WWHD_DSWEEP_VISIT'] = 'all' if os.environ.get('WWHD_SETUP_MODEL_SWEEP_VISITS') == 'all' else str(visits)
            (scratch / 'save').mkdir()
            output_path = scratch / 'output.log'
            output = stack.enter_context(output_path.open('wb'))
            reader = stack.enter_context(output_path.open('rb'))
            child = popen([str(executable), '--game', str(game), '--save', str(scratch / 'save')], env=env,
                          stdout=output, stderr=subprocess.STDOUT, cwd=str(scratch))
            pending = b''
            last_change, last_size, started = clock(), -1, False
            try:
                last_size = progress_file.stat().st_size  # lines of earlier attempts are not this one's start
            except OSError:
                pass

            def forward(final=False):
                nonlocal pending
                pending += reader.read()
                parts = pending.split(b'\n')
                pending = b'' if final else parts.pop()
                for part in parts:
                    text = part.decode(errors='replace').rstrip('\r')
                    if text:
                        line(text)
            stalled = False
            try:
                while child.poll() is None:
                    if cancel():
                        raise InterruptedError('Model preparation paused')
                    forward()
                    try:
                        size = progress_file.stat().st_size
                    except OSError:
                        size = -1
                    if size != last_size:
                        last_size, last_change = size, clock()
                        started = True
                    # (before the attempt's first line the game is still starting: a busy machine takes longer)
                    elif clock() - last_change > (stall if started else max(stall, STARTUP_SECONDS)):
                        stalled = True
                        log('Model preparation: no progress for %d s; restarting' % stall)
                        break
                    sleep(.2)
                forward(final=True)
            finally:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
        done_lines = _lines(progress_file)
        if 'sweep-done' in done_lines:
            return 'done'
        if done_lines and done_lines[-1] == 'restart':  # an orderly restart the sweep asked for (not a stop)
            continue
        skip = set(_lines(state / 'skip.txt'))
        name = culprit(done_lines, skip)
        log('Model preparation attempt %d stopped (%s); %s' % (
            attempt, 'stalled' if stalled else 'exit %s' % child.returncode,
            'skipping %s' % name if name else 'nothing new to skip'))
        if not name:
            return 'failed'
        if 'actor ' + name in done_lines:  # an actor: the second placement of a name that stops the game skips them all
            base = name.split(':', 1)[0]
            if any(s.split(':', 1)[0] == base for s in skip if s.count(':') == 3):
                name = base
        with (state / 'skip.txt').open('a', encoding='utf-8') as stream:
            stream.write(name + '\n')
        now = finished()
        without_progress = 0 if now > before else without_progress + 1
        before = now
    return 'failed'


def identity_of(wanted, renderer):
    """The sweep's identity: the preparation identity (game files, runtime, template) and the renderer."""
    digest = hashlib.sha256(json.dumps(wanted, sort_keys=True).encode()).hexdigest()
    return {'schema': SCHEMA, 'renderer': renderer, 'preparation': digest,
            'game': hashlib.sha256(json.dumps(wanted.get('game'), sort_keys=True).encode()).hexdigest()}


def clear(cache_root):
    shutil.rmtree(Path(cache_root) / 'model-sweep', ignore_errors=True)
