import json
from pathlib import Path
import tempfile
import subprocess
import sys
import time
import unittest

import shader_prebuild as prebuild


class ShaderPrebuildTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'own-game'
        self.game.mkdir()
        (self.game / 'synthetic.sharcfb').write_text('synthetic archive identity only')
        self.package = self.root / 'package'
        (self.package / 'tools').mkdir(parents=True)
        (self.package / 'tools/shaderprep.py').write_text('synthetic translator identity')
        self.template = self.root / 'template'
        self.template.write_text('synthetic template identity')
        self.cache = self.root / 'private-cache'
        self.calls = []

    def runner(self, command, env, cancel, progress):
        self.calls.append((command, env))
        if command[1] == str(self.package / 'tools/shaderprep.py'):
            Path(command[3]).write_text('synthetic assembled output')

    def prepare(self, **kwargs):
        return prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache,
                               self.package, runner=self.runner, **kwargs)

    def test_runtime_cli_missing_template_is_nonfatal_and_failure_stays_nonzero(self):
        import contextlib
        import io
        from unittest.mock import patch
        self.template.unlink()
        arguments = ['--runtime-prepare', '--game', str(self.game),
            '--template', str(self.template), '--executable', '/synthetic/exe',
            '--cache-root', str(self.cache), '--package', str(self.package),
            '--renderer', 'vulkan']
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(prebuild.main(arguments), 0)
        self.assertEqual(json.loads(output.getvalue())['status'], 'skipped')
        self.assertFalse(self.cache.exists())
        with patch.object(prebuild, 'prepare', return_value=dict(status='failed')), \
                contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(prebuild.main(arguments), 1)

    def test_launch_identity_failure_is_nonfatal_and_records_failure(self):
        from unittest.mock import patch
        with patch.object(prebuild, 'launch_identity', side_effect=PermissionError('scan denied')):
            result = self.prepare()
        self.assertEqual(result['status'], 'failed')
        self.assertEqual(result['message'], 'scan denied')
        marker = json.loads((self.cache / 'failed.json').read_text())
        self.assertIsNone(marker['launch'])
        self.assertEqual(self.calls, [])

    def test_preparation_uses_device_cache_directory_not_inherited_file(self):
        from unittest.mock import patch
        import os
        with patch.dict(os.environ, {'WWHD_VK_PIPELINE_CACHE': '/synthetic/old-override.bin'}):
            self.assertEqual(self.prepare()['status'], 'ready')
        env = self.calls[-1][1]
        self.assertEqual(env['WWHD_VK_PIPELINE_CACHE_DIR'], str(self.cache / 'live'))
        self.assertNotIn('WWHD_VK_PIPELINE_CACHE', env)

    def test_missing_template_skips_without_commands_or_cache_changes(self):
        self.template.unlink()
        messages = []
        result = self.prepare(progress=messages.append)
        self.assertEqual(result['status'], 'skipped')
        self.assertIn('compile during play', result['message'])
        self.assertEqual(self.calls, [])
        self.assertFalse(self.cache.exists())
        self.assertEqual(messages, [result['message']])

    def test_optout_survives_runtime_and_translator_update(self):
        executable = self.root / 'runtime'
        executable.write_text('first build')
        def prepare(**kwargs):
            return prebuild.prepare(self.game, self.template, executable, self.cache,
                                    self.package, runner=self.runner, **kwargs)
        self.assertEqual(prepare(skip=True)['status'], 'skipped')
        executable.write_text('updated build')
        (self.package / 'tools/shaderprep.py').write_text('updated translator')
        self.assertEqual(prepare()['status'], 'skipped')
        self.assertEqual(self.calls, [])

    def test_corrupt_markers_and_failed_inputs_suppression(self):
        self.cache.mkdir()
        for name in ('active.json', 'skip.json', 'failed.json'):
            (self.cache / name).write_text('{corrupt')
        self.assertEqual(self.prepare()['status'], 'ready')
        self.template.write_text('updated template')
        calls = []
        def failure(*args):
            calls.append(1)
            raise RuntimeError('synthetic failure')
        def prepare():
            return prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache,
                                    self.package, runner=failure)
        self.assertEqual(prepare()['status'], 'failed')
        self.assertEqual(prepare()['status'], 'skipped')
        self.assertEqual(len(calls), 1)
        (self.cache / 'rebuild.request').touch()
        self.assertEqual(prepare()['status'], 'failed')
        self.assertEqual(len(calls), 2)

    @unittest.skipIf(sys.platform == 'win32', 'POSIX flock reader lease; Windows native lease checked by crossbuild')
    def test_cleanup_preserves_generation_with_active_reader(self):
        import fcntl
        first = self.prepare()
        directory = Path(first['generation'])
        with (directory / 'in-use.lock').open('a+b') as lease:
            fcntl.flock(lease.fileno(), fcntl.LOCK_SH | fcntl.LOCK_NB)
            (self.cache / 'rebuild.request').touch()
            self.prepare()
            self.assertTrue(directory.exists())
        selected = prebuild.read_marker(self.cache / 'active.json')['generation']
        prebuild.cleanup_generations(self.cache, selected)
        self.assertFalse(directory.exists())

    def test_legacy_cache_migration_never_overwrites_live_entries(self):
        generation = self.cache / 'generation-legacy'
        (generation / 'vulkan-shaders').mkdir(parents=True)
        (generation / 'shaders.bin').write_text('legacy metal cache')
        (generation / 'vulkan-shaders/old.bin').write_text('legacy shader')
        (self.cache / 'active.json').write_text(json.dumps(dict(generation=generation.name)))
        live = self.cache / 'live/vulkan-shaders'
        live.mkdir(parents=True)
        (live / 'old.bin').write_text('current live shader')
        prebuild.migrate_legacy_caches(self.cache)
        self.assertEqual((self.cache / 'live/shaders.bin').read_text(), 'legacy metal cache')
        self.assertEqual((live / 'old.bin').read_text(), 'current live shader')

    def test_live_caches_survive_successful_generation_replacement(self):
        first = self.prepare()
        live = self.cache / 'live/shaders.bin'
        live.write_text('synthetic player cache')
        (self.cache / 'rebuild.request').touch()
        second = self.prepare()
        self.assertEqual(live.read_text(), 'synthetic player cache')
        self.assertNotEqual(first['generation'], second['generation'])
        self.assertFalse(Path(first['generation']).exists())
        self.assertTrue(Path(second['generation']).exists())
        self.assertEqual(self.calls[1][1]['WWHD_SHADER_CACHE'], self.calls[3][1]['WWHD_SHADER_CACHE'])

    def test_success_reuses_identity_and_private_outputs(self):
        result = self.prepare()
        self.assertEqual(result['status'], 'ready')
        self.assertEqual(len(self.calls), 2)
        # Setup compiles recorded states only, without speculative expansion.
        command = self.calls[0][0]
        self.assertEqual(command[command.index('--max') + 1], '0')
        env = self.calls[1][1]
        self.assertEqual(env['WWHD_HIDDEN_WINDOWS'], '1')
        self.assertEqual(env['WWHD_NO_AUDIO'], '1')
        self.assertTrue(Path(env['WWHD_HEADSTART']).is_relative_to(self.cache))
        self.assertTrue(self.prepare()['reused'])
        self.assertEqual(len(self.calls), 2)
        self.assertEqual((self.game / 'synthetic.sharcfb').read_text(), 'synthetic archive identity only')

    def test_game_and_translator_change_invalidate(self):
        self.prepare()
        (self.game / 'synthetic.sharcfb').write_text('changed synthetic archive')
        self.assertFalse(self.prepare()['reused'])
        (self.package / 'tools/shaderprep.py').write_text('changed synthetic translator')
        self.assertFalse(self.prepare()['reused'])
        self.assertEqual(len(self.calls), 6)

    def test_nested_container_and_runtime_update_invalidate(self):
        executable = self.root / 'synthetic-executable'
        executable.write_text('synthetic version one')
        def prepare():
            return prebuild.prepare(self.game, self.template, executable, self.cache,
                                    self.package, runner=self.runner)
        prepare()
        self.assertTrue(prepare()['reused'])
        (self.game / 'synthetic.pack').write_text('synthetic packed archive')
        self.assertFalse(prepare()['reused'])
        executable.write_text('synthetic version two')
        self.assertFalse(prepare()['reused'])

    def test_failed_rebuild_keeps_previous_active_and_cleans_staging(self):
        self.prepare()
        active = (self.cache / 'active.json').read_bytes()
        dirs = sorted(p.name for p in self.cache.iterdir())
        self.template.write_text('new synthetic identity')
        def failure(*args):
            raise RuntimeError('synthetic failure')
        result = prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache,
                                 self.package, runner=failure)
        self.assertEqual(result['status'], 'failed')
        self.assertEqual((self.cache / 'active.json').read_bytes(), active)
        self.assertEqual(sorted(p.name for p in self.cache.iterdir() if p.name != "failed.json"), dirs)

    def test_skip_and_cancel_leave_sources_and_active_untouched(self):
        self.assertEqual(self.prepare(skip=True)['status'], 'skipped')
        self.assertEqual(self.prepare(cancel=lambda: True)['status'], 'skipped')
        self.assertTrue((self.cache / 'skip.json').is_file())
        self.assertEqual(self.calls, [])

    def test_rebuild_marker_forces_new_generation_and_clears_only_on_success(self):
        first = self.prepare()
        marker = self.cache / 'rebuild.request'
        marker.touch()
        second = self.prepare()
        self.assertNotEqual(first['generation'], second['generation'])
        self.assertFalse(marker.exists())
        marker.touch()
        def failure(*args):
            raise RuntimeError('synthetic failure')
        prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache,
                         self.package, runner=failure)
        self.assertTrue(marker.exists())

    def test_skip_persists_for_identity_until_rebuild_or_update(self):
        self.assertEqual(self.prepare(skip=True)['status'], 'skipped')
        self.assertEqual(self.prepare()['status'], 'skipped')
        self.assertEqual(self.calls, [])
        (self.cache / 'rebuild.request').touch()
        self.assertEqual(self.prepare()['status'], 'ready')
        self.assertFalse((self.cache / 'skip.json').exists())
        self.prepare(skip=True)
        self.template.write_text('new synthetic template identity')
        self.assertEqual(self.prepare()['status'], 'ready')

    def test_crossprocess_publication_waits_for_activation_lock(self):
        self.prepare()
        active_path = self.cache / 'active.json'
        old = active_path.read_bytes()
        self.template.write_text('new synthetic template generation')
        code = """
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import shader_prebuild as p
def builder(game, output, template):
    output.write_text('synthetic child generation')
r = p.prepare(sys.argv[2], sys.argv[3], None, sys.argv[4], sys.argv[5], builder=builder)
print(r['status'], flush=True)
"""
        child = None
        try:
            with prebuild.activation_lock(self.cache):
                child = subprocess.Popen([sys.executable, '-c', code,
                    str(Path(prebuild.__file__).parent), str(self.game), str(self.template),
                    str(self.cache), str(self.package)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    if any(p.read_text() == 'synthetic child generation'
                           for p in self.cache.glob('generation-*/headstart.bin')):
                        break
                    time.sleep(.01)
                else:
                    self.fail('Child did not finish assembly outside activation lock')
                self.assertIsNone(child.poll())
                self.assertEqual(active_path.read_bytes(), old)
            output, error = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 0, error.decode())
            self.assertIn(b'warm_pending', output)
            self.assertNotEqual(active_path.read_bytes(), old)
        finally:
            if child is not None and child.poll() is None:
                child.terminate()
                child.wait(timeout=5)

    def test_android_identity_uses_selected_library_and_invalidates_update(self):
        library = self.root / 'synthetic-library.so'
        library.write_text('synthetic library version one')
        def builder(game, output, template):
            output.write_text('synthetic assembled output')
        def prepare():
            return prebuild.prepare(self.game, self.template, None, self.cache,
                self.package, builder=builder, runtime_identity=library)
        prepare()
        active_path = self.cache / 'active.json'
        active = json.loads(active_path.read_text())
        self.assertEqual(active['identity']['runtime'], prebuild.file_hash(library))
        # The native warm handshake alone changes pending to ready.
        active['status'] = 'ready'
        active_path.write_text(json.dumps(active))
        self.assertTrue(prepare()['reused'])
        library.write_text('synthetic library version two')
        self.assertFalse(prepare()['reused'])

    def test_android_assembly_never_claims_native_warm(self):
        def builder(game, output, template):
            output.write_text('synthetic assembled output')
        result = prebuild.prepare(self.game, self.template, None, self.cache,
                                 self.package, builder=builder)
        self.assertEqual(result['status'], 'warm_pending')
        self.assertEqual(json.loads((self.cache / 'active.json').read_text())['status'], 'warm_pending')


class ShaderPrebuildProgressTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.game = self.root / 'own-game'
        self.game.mkdir()
        (self.game / 'synthetic.sharcfb').write_text('synthetic archive identity only')
        self.package = self.root / 'package'
        (self.package / 'tools').mkdir(parents=True)
        (self.package / 'tools/shaderprep.py').write_text('synthetic translator identity')
        self.template = self.root / 'template'
        self.template.write_text('synthetic template identity')
        self.cache = self.root / 'private-cache'

    def test_parse_progress(self):
        self.assertEqual(prebuild.parse_progress('WWHD-PROGRESS 5 10 5 of 10 archives'), (5, 10, '5 of 10 archives'))
        self.assertEqual(prebuild.parse_progress('WWHD-PROGRESS 0 0'), (0, 0, ''))
        for line in ('[warm] 5 of 10', 'WWHD-PROGRESS x 10', 'WWHD-PROGRESS 5', 'WWHD-PROGRESS -1 4', ''):
            self.assertIsNone(prebuild.parse_progress(line))

    def runner(self, lines_by_phase, cancel_after=None):
        calls = []
        def runner(command, env, cancel, progress):
            calls.append(command)
            phase = len(calls) - 1
            if phase == 0:
                Path(command[3]).write_text('synthetic assembled output')
            for line in lines_by_phase[phase]:
                progress(line)
        return runner, calls

    def test_fractions_cover_both_phases_and_never_move_backwards(self):
        runner, calls = self.runner([
            ['shaderprep: log line', 'WWHD-PROGRESS 0 1000 0 of 3 game archives',
             'WWHD-PROGRESS 500 1000 2 of 3 game archives', 'WWHD-PROGRESS 1000 1000 30 shader records'],
            ['[warm] translating', 'WWHD-PROGRESS 0 40 0 of 40 shaders and pipelines',
             'WWHD-PROGRESS 10 40 10 of 40 shaders and pipelines', 'WWHD-PROGRESS 40 40 40 of 40 shaders and pipelines']])
        events, logged = [], []
        from unittest.mock import patch
        with patch.object(prebuild.time, 'monotonic', side_effect=[float(i) for i in range(1000)]):
            result = prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache, self.package,
                runner=runner, progress=lambda text, fraction=None: events.append((text, fraction)), log=logged.append)
        self.assertEqual(result['status'], 'ready')
        self.assertIn('--progress', calls[0])
        self.assertEqual(logged, ['shaderprep: log line', '[warm] translating'])
        self.assertEqual(events[0], ('Resolving shaders from your game files', None))  # indeterminate at first
        fractions = [f for _, f in events if f is not None]
        self.assertEqual(fractions, sorted(fractions))
        self.assertAlmostEqual(fractions[-1], 1.0)
        share = prebuild.ASSEMBLY_SHARE
        self.assertIn(('Resolving shaders from your game files: 2 of 3 game archives', share / 2), events)
        self.assertIn(('Compiling shaders and pipelines for this computer', share), events)
        self.assertIn(('Compiling shaders and pipelines for this computer: 10 of 40 shaders and pipelines',
                       share + (1 - share) / 4), events)

    def test_unknown_total_stays_indeterminate_and_text_only_callers_get_phases(self):
        runner, _ = self.runner([['WWHD-PROGRESS 0 0 counting'], ['WWHD-PROGRESS 0 0']])
        events = []
        prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache, self.package,
                         runner=runner, progress=lambda text, fraction=None: events.append(fraction))
        self.assertEqual(events, [None, None])
        messages = []
        runner, _ = self.runner([['WWHD-PROGRESS 1 2 half', 'raw'], ['WWHD-PROGRESS 2 2 all']])
        prebuild.prepare(self.game, self.template, '/synthetic/exe', self.root / 'other-cache', self.package,
                         runner=runner, progress=messages.append)
        self.assertEqual(messages, ['Resolving shaders from your game files', 'raw',
                                    'Compiling shaders and pipelines for this computer'])

    def test_cancel_while_progress_arrives_skips(self):
        state = {'lines': 0}
        def runner(command, env, cancel, progress):
            for i in range(10):
                progress('WWHD-PROGRESS %d 10 step' % i)
                state['lines'] += 1
            Path(command[3]).write_text('synthetic assembled output')
        result = prebuild.prepare(self.game, self.template, '/synthetic/exe', self.cache, self.package,
                                  runner=runner, cancel=lambda: state['lines'] >= 3)
        self.assertEqual(result['status'], 'skipped')
        self.assertEqual(state['lines'], 3)
        self.assertTrue((self.cache / 'skip.json').is_file())

    def test_in_process_builder_reports_whole_range(self):
        events = []
        def builder(game, output, template, progress=None):
            for done in (0, 250, 1000):
                progress(done, 1000, '%d of 1000' % done)
            output.write_text('synthetic assembled output')
        result = prebuild.prepare(self.game, self.template, None, self.cache, self.package, builder=builder,
                                  progress=lambda text, fraction=None: events.append(fraction))
        self.assertEqual(result['status'], 'warm_pending')
        self.assertEqual(events[0], None)
        self.assertAlmostEqual(events[-1], 1.0)

    def test_run_process_streams_lines_while_child_runs(self):
        flag = self.root / 'seen'
        code = ("import sys, time, pathlib\n"
                "print('WWHD-PROGRESS 1 2 first', flush=True)\n"
                "deadline = time.time() + 20\n"
                "while not pathlib.Path(sys.argv[1]).exists():\n"
                "    if time.time() > deadline: sys.exit(3)\n"
                "    time.sleep(.02)\n"
                "sys.stdout.write('partial')\n"
                "print(' line', flush=True)\n")
        lines = []
        def progress(line):
            lines.append(line)
            flag.write_text('seen')
        prebuild.run_process([sys.executable, '-c', code, str(flag)], None, lambda: False, progress)
        self.assertEqual(lines, ['WWHD-PROGRESS 1 2 first', 'partial line'])

    def test_run_process_cancel_terminates_child(self):
        code = "import time\nprint('started', flush=True)\ntime.sleep(60)\n"
        lines = []
        started = time.monotonic()
        with self.assertRaises(InterruptedError):
            prebuild.run_process([sys.executable, '-c', code], None, lambda: bool(lines), lines.append)
        self.assertLess(time.monotonic() - started, 30)
        self.assertEqual(lines, ['started'])


if __name__ == '__main__':
    unittest.main()
