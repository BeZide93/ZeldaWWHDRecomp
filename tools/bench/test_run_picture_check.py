import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch
from PIL import Image
import run_picture_check as pictures


class CaptureEvidenceTest(unittest.TestCase):
    def run_fake(self, complete=True, correct_step=True):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'save').mkdir(); (root / 'states').mkdir(); (root / 'out').mkdir()
            (root / 'binary').write_bytes(b'synthetic executable fixture')
            (root / 'states/slot3.bin').write_bytes(b'synthetic state fixture')
            args = types.SimpleNamespace(out=root / 'out', save=root / 'save', state_dir=root / 'states',
                game=root / 'game', scene='outset', slot=3, load_frame=450, origin_load_steps=180,
                seconds=2, capture_load_step=200, validation=True, timeout=1)
            launched = []

            class Process:
                def __init__(self, command, **kwargs):
                    launched.append(kwargs['env'])
                    run = Path(kwargs['cwd'])
                    target = run / 'captures/fixture'; target.mkdir(parents=True)
                    Image.new('RGB', (2, 1), 'black').save(target / 'tv.png')
                    offset = 200 if correct_step else 201
                    kwargs['stdout'].write('[test] capture at loaded logic step +%d (game counter 00000100), full pass\n' % offset)
                    if complete: (run / 'test_done').touch()
                def poll(self): return 0

            with patch.object(pictures.subprocess, 'Popen', Process):
                result = pictures.capture(args, 'baseline_1', root / 'binary', 'fixture-revision')
            return result, launched[0]

    def test_requires_completed_endpoint_and_exact_checkpoint(self):
        with self.assertRaises(RuntimeError): self.run_fake(complete=False)
        with self.assertRaises(RuntimeError): self.run_fake(correct_step=False)

    def test_records_cold_local_capture_without_performance_mode(self):
        result, env = self.run_fake()
        self.assertEqual(result['name'], 'baseline_1')
        self.assertEqual(env['WWHD_TEST_ORIGIN_LOAD'], '180')
        self.assertEqual(env['WWHD_TEST_CAPTURE_LOAD_STEP'], '200')
        self.assertEqual(env['WWHD_HEADSTART'], '0')
        self.assertEqual(env['WWHD_VK_PIPELINE_WARMUP'], '0')
        self.assertNotIn('WWHD_PROFILE', env)


if __name__ == '__main__':
    unittest.main()
