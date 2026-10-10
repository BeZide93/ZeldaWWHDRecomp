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

    def test_success_reuses_identity_and_private_outputs(self):
        result = self.prepare()
        self.assertEqual(result['status'], 'ready')
        self.assertEqual(len(self.calls), 2)
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
        self.assertEqual(sorted(p.name for p in self.cache.iterdir()), dirs)

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


if __name__ == '__main__':
    unittest.main()
