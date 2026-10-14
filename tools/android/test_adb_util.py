"""Transport faults must recover without turning remote failures into success."""
import io
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr
from unittest.mock import patch

import adb_util


class AdbTests(unittest.TestCase):
    def result(self, rc=0, stderr=b'', stdout=b'ok'):
        return subprocess.CompletedProcess(['adb'], rc, stdout, stderr)

    def test_transient_errors_retry_and_discard_partial_output(self):
        faults = [self.result(1, message, b'partial') for message in adb_util.TRANSIENT]
        faults += [self.result(255), subprocess.TimeoutExpired(['adb'], 1)]
        for fault in faults:
            with self.subTest(fault=fault), patch('adb_util.subprocess.run', side_effect=[fault, self.result()]) as run, patch('adb_util.time.sleep') as sleep, redirect_stderr(io.StringIO()):
                self.assertEqual(adb_util.run(['adb', 'get-state'], check=True).stdout, b'ok')
                self.assertEqual(run.call_count, 2)
                sleep.assert_called_once_with(.5)

    def test_exit_one_is_optional_or_fatal_without_retry(self):
        for check in (False, True):
            with patch('adb_util.subprocess.run', return_value=self.result(1, b'No such file')) as run:
                if check:
                    with self.assertRaises(adb_util.AdbError) as caught:
                        adb_util.run(['adb', 'shell', 'test -e missing'], check=True)
                    self.assertIn('after 1 attempt(s)', str(caught.exception))
                else:
                    self.assertEqual(adb_util.run(['adb', 'shell', 'test -e missing']).returncode, 1)
                self.assertEqual(run.call_count, 1)

    def test_final_failure_has_diagnostics_and_bounded_backoff(self):
        with patch('adb_util.subprocess.run', return_value=self.result(255, b'error: closed')) as run, patch('adb_util.time.sleep') as sleep, redirect_stderr(io.StringIO()):
            with self.assertRaises(adb_util.AdbError) as caught:
                adb_util.run(['adb', '-s', 'emulator-test', 'get-state'])
            self.assertEqual(run.call_count, 4)
            self.assertEqual([c.args[0] for c in sleep.call_args_list], [.5, 1, 2])
            for text in ('4 attempt(s)', 'rc=255', 'error: closed', 'emulator-test'):
                self.assertIn(text, str(caught.exception))

    def test_raw_read_frame_preserves_binary_and_remote_failure(self):
        payload = bytes(range(256)) * 4096 + b'\n'
        def fake(command, **kwargs):
            self.assertIn('exec-out', command)
            script = shlex.split(command[-1])[-1]
            marker = script.split("printf '", 1)[1].split('%s', 1)[0].encode()
            return subprocess.CompletedProcess(command, 0, payload + marker + b'0\n', b'')
        with patch('adb_util.subprocess.run', side_effect=fake):
            self.assertEqual(adb_util.check_output(['adb', 'shell', "run-as pkg cat 'path with space'"]), payload)
        def failed(command, **kwargs):
            result = fake(command, **kwargs)
            result.stdout = result.stdout[:-2] + b'1\n'
            return result
        with patch('adb_util.subprocess.run', side_effect=failed):
            result = adb_util.run(['adb', 'exec-out', 'cat', 'absent'])
            self.assertEqual((result.returncode, result.stdout, result.stderr), (1, b'', b''))
            with self.assertRaises(adb_util.AdbError):
                adb_util.check_output(['adb', 'exec-out', 'cat', 'absent'])

    def test_incomplete_frame_is_retried(self):
        with patch('adb_util.subprocess.run', return_value=self.result(stdout=b'partial')), patch('adb_util.time.sleep'), redirect_stderr(io.StringIO()):
            with self.assertRaises(adb_util.AdbError) as caught:
                adb_util.check_output(['adb', 'shell', 'cat', 'file'])
            self.assertIn('incomplete exec-out read', str(caught.exception))

    def test_retry_input_and_destination_are_binary_safe(self):
        data = bytes(range(256))
        destination = io.BytesIO()
        with patch('adb_util.subprocess.run', side_effect=[self.result(255, stdout=b'partial'), self.result(stdout=data)]) as run, patch('adb_util.time.sleep'), redirect_stderr(io.StringIO()):
            adb_util.run(['adb', 'shell', "run-as pkg sh -c 'cat > file'"], input=data, stdout=destination, check=True)
            self.assertEqual(destination.getvalue(), data)
            for call in run.call_args_list:
                self.assertEqual(call.kwargs['input'], data)
                self.assertEqual(call.args[0][1:3], ['shell', '-T'])

    def test_text_and_timeout_final_failure(self):
        with patch('adb_util.subprocess.run', return_value=self.result(stdout=b'hello\r\n')):
            self.assertEqual(adb_util.check_output(['adb', 'get-state'], text=True), 'hello\n')
        with patch('adb_util.subprocess.run', side_effect=subprocess.TimeoutExpired(['adb'], 1)), patch('adb_util.time.sleep'), redirect_stderr(io.StringIO()):
            with self.assertRaisesRegex(adb_util.AdbError, 'TimeoutExpired'):
                adb_util.run(['adb', 'get-state'], attempts=2)

    def test_executable_fault_injection_and_real_missing_file(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            fake = root / 'adb'
            counter = root / 'calls'
            fake.write_text('#!' + sys.executable + '\n' + '''import os, pathlib, sys, time
p = pathlib.Path(os.environ['ADB_TEST_COUNTER'])
n = int(p.read_text()) if p.exists() else 0
p.write_text(str(n + 1))
if n < int(os.environ['ADB_TEST_FAILURES']):
    print('error: closed', file=sys.stderr)
    sys.exit(255)
os.execvp('sh', ['sh', '-c', sys.argv[-1]])
''')
            fake.chmod(0o700)
            env = {**os.environ, 'ADB_TEST_COUNTER': str(counter), 'ADB_TEST_FAILURES': '2'}
            with redirect_stderr(io.StringIO()):
                self.assertEqual(adb_util.check_output([str(fake), 'shell', 'printf recovered'], env=env, backoff=0), b'recovered')
            self.assertEqual(counter.read_text(), '3')
            with self.assertRaises(adb_util.AdbError):
                adb_util.check_output([str(fake), 'shell', 'cat', str(root / 'missing')], env=env, backoff=0)
            self.assertEqual(counter.read_text(), '4')
            payload = bytes(range(256)) * 4096
            binary = root / 'binary with spaces'
            counter.write_text('0')
            with redirect_stderr(io.StringIO()):
                adb_util.run([str(fake), 'shell', '-T', 'cat > ' + shlex.quote(str(binary))],
                             input=payload, check=True, env=env, backoff=0)
            self.assertEqual(counter.read_text(), '3')
            counter.write_text('0')
            with redirect_stderr(io.StringIO()):
                self.assertEqual(adb_util.check_output([str(fake), 'exec-out', 'cat', str(binary)],
                                                      env=env, backoff=0), payload)
            self.assertEqual(counter.read_text(), '3')

    def test_failed_read_never_includes_partial_payload_in_diagnostics(self):
        def failed(command, **kwargs):
            script = shlex.split(command[-1])[-1]
            marker = script.split("printf '", 1)[1].split('%s', 1)[0].encode()
            return subprocess.CompletedProcess(command, 0, b'private payload' + marker + b'1\nread failed', b'')
        with patch('adb_util.subprocess.run', side_effect=failed):
            with self.assertRaises(adb_util.AdbError) as caught:
                adb_util.check_output(['adb', 'shell', 'cat', 'fixture'])
            self.assertIn('read failed', str(caught.exception))
            self.assertNotIn('private payload', str(caught.exception))


if __name__ == '__main__':
    unittest.main()
