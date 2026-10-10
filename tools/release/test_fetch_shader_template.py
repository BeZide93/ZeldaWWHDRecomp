"""Private-template CI behavior using synthetic payloads and mocked git only."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import fetch_shader_template as fetcher
from test_guard_template import encode, fixture


class FetchTemplateTests(unittest.TestCase):
    def test_no_secret_skips_network_and_removes_stale_payload(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            target = root / 'tools/shadercache/template.bin'
            target.parent.mkdir(parents=True)
            target.write_bytes(b'synthetic stale payload')
            with patch.object(fetcher, 'ROOT', root), patch.dict(os.environ, {}, clear=True), patch.object(subprocess, 'run') as run:
                fetcher.fetch()
                run.assert_not_called()
            self.assertFalse(target.exists())

    def test_git_failure_and_guard_rejection_never_publish_and_remove_key(self):
        for failure in ('fetch', 'guard'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                folder = root / 'tools/shadercache'
                folder.mkdir(parents=True)
                commit = 'a' * 40
                # Correct hash alone must never bypass the code-free validator.
                data = b'synthetic invalid template'
                (folder / 'template.ref').write_text(json.dumps(dict(commit=commit,
                    sha256=hashlib.sha256(data).hexdigest())))
                def git(command, **kwargs):
                    if failure == 'fetch' and 'fetch' in command:
                        raise subprocess.CalledProcessError(128, command)
                    output = (commit + '\n').encode() if 'rev-parse' in command else data if 'show' in command else b''
                    return subprocess.CompletedProcess(command, 0, stdout=output)
                with patch.object(fetcher, 'ROOT', root), patch.dict(os.environ,
                    {'RUNNER_TEMP': tmp, 'SHADER_TEMPLATE_DEPLOY_KEY': 'synthetic test key'}, clear=True), patch.object(subprocess, 'run', side_effect=git):
                    with self.assertRaises(subprocess.CalledProcessError if failure == 'fetch' else ValueError):
                        fetcher.fetch()
                self.assertFalse((folder / 'template.bin').exists())
                self.assertFalse((root / 'shader-template-deploy-key').exists())

    def test_verified_payload_and_failed_hash_both_remove_key(self):
        for valid in (True, False):
            with self.subTest(valid=valid), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                folder = root / 'tools/shadercache'
                folder.mkdir(parents=True)
                data = encode(fixture())
                commit = 'a' * 40
                (folder / 'template.ref').write_text(json.dumps(dict(commit=commit,
                    sha256=hashlib.sha256(data).hexdigest() if valid else 'b' * 64)))
                def git(command, **kwargs):
                    self.assertNotIn('SHADER_TEMPLATE_DEPLOY_KEY', kwargs['env'])
                    self.assertIn('IdentitiesOnly=yes', kwargs['env']['GIT_SSH_COMMAND'])
                    key = root / 'shader-template-deploy-key'
                    self.assertTrue(key.is_file())
                    if os.name != 'nt':
                        self.assertEqual(key.stat().st_mode & 0o777, 0o600)
                    output = (commit + '\n').encode() if 'rev-parse' in command else data if 'show' in command else b''
                    return subprocess.CompletedProcess(command, 0, stdout=output)
                with patch.object(fetcher, 'ROOT', root), patch.dict(os.environ,
                    {'RUNNER_TEMP': tmp, 'SHADER_TEMPLATE_DEPLOY_KEY': 'synthetic test key'}, clear=True), patch.object(subprocess, 'run', side_effect=git):
                    if valid:
                        fetcher.fetch()
                        self.assertEqual((folder / 'template.bin').read_bytes(), data)
                    else:
                        with self.assertRaisesRegex(ValueError, 'checksum'):
                            fetcher.fetch()
                        self.assertFalse((folder / 'template.bin').exists())
                self.assertFalse((root / 'shader-template-deploy-key').exists())


if __name__ == '__main__':
    unittest.main()
