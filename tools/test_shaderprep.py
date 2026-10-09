"""Backend tags must survive template merge/build; legacy Metal templates remain readable."""
import pathlib
import struct
import tempfile
import unittest
import zlib
from unittest.mock import patch

import shaderprep as prep


class RecipeCompatibility(unittest.TestCase):
    def test_legacy_and_typed_pipeline_merge(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            legacy = root / 'legacy.bin'
            legacy.write_bytes(b'WWHT' + zlib.compress(struct.pack('<IIII', 0, 0, 1, 5) + b'metal'))
            self.assertEqual(prep.read_template(legacy), ([], [(2, b'metal')]))
            recording = root / 'recording.bin'
            code = b'12345678'
            with recording.open('wb') as output:
                prep.write_record(output, 1, prep.shader_record(1, code, b'', []))
                prep.write_record(output, 4, b'vulkan')
            template = root / 'typed.bin'
            prep.cmd_template(recording, template, legacy)
            self.assertTrue(template.read_bytes().startswith(b'WWHT0002'))
            states, pipelines = prep.read_template(template)
            self.assertEqual(len(states), 1)
            self.assertEqual(pipelines, [(2, b'metal'), (4, b'vulkan')])
            merged = root / 'merged.bin'
            prep.cmd_template(recording, merged, template)
            self.assertEqual(prep.read_template(merged), (states, pipelines))
            headstart = root / 'headstart.bin'
            with patch.object(prep, 'game_shaders', return_value={(prep.VS, code): ([0] * 64, 'fixture')}):
                prep.cmd_build('', headstart, merged, 0)
                self.assertEqual([kind for kind, _ in prep.read_records(headstart)], [1, 2, 4])
                prep.cmd_build('', headstart, merged, 0, with_pipelines=False)
                self.assertEqual([kind for kind, _ in prep.read_records(headstart)], [1])

    def test_corrupt_or_truncated_record(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'record.bin'
            with path.open('wb') as output:
                prep.write_record(output, 4, b'pipeline')
            valid = path.read_bytes()
            for bad in (valid[:-1], valid[:11], valid + b'x',
                        struct.pack('<III', 4, 1, len(valid) - 12) + valid[12:]):
                path.write_bytes(bad)
                with self.assertRaises(ValueError):
                    list(prep.read_records(path))
            with self.assertRaises(ValueError):
                prep.decompress_bounded(zlib.compress(b'x' * 100), 10)
            with self.assertRaises(ValueError):
                prep.decompress_bounded(zlib.compress(b'valid') + b'trailing', 100)


if __name__ == '__main__':
    unittest.main()
