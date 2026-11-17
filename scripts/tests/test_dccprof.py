"""Build/profile orchestration tests; emulator samples are exercised separately."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('dccprof', ROOT/'scripts/dccprof.py')
dccprof = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dccprof)
# The standalone script imports the neighboring ma module.
sys.path.insert(0, str(ROOT/'scripts'))
import ma
sys.path.pop(0)


class ProfileTests(unittest.TestCase):
    def invoke(self, failure=None, empty=False):
        with tempfile.TemporaryDirectory(prefix='dcc profile ') as folder:
            build = Path(folder)/'nested/build'
            out = Path(folder)/'reports'
            csv = build/'sample_profile.csv'
            calls = []
            def run(command, **kwargs):
                calls.append((command, kwargs))
                stage = len(calls)
                if stage == 1:
                    build.mkdir(parents=True)
                    csv.write_text('stale capture')
                if stage == 3:
                    self.assertFalse(csv.exists(), 'old profile must be removed')
                    if not empty:
                        csv.write_text('pc,count,asm\n')
                return subprocess.CompletedProcess(command, 19 if stage == failure else 0)
            with mock.patch.dict(sys.modules, {'ma':ma}), \
                 mock.patch.object(ma, 'tool', return_value='assembler'), \
                 mock.patch.object(ma, 'resolve_command', return_value='emulator'), \
                 mock.patch.object(subprocess, 'run', side_effect=run):
                status = dccprof.main(['Sample', '-BuildDir', str(build), '--out-dir', str(out),
                    '-SourcePath', 'tests/custom.c', '--clock', '123', '--', '-n', '2'])
            return status, calls, str(build), str(out)

    def test_builds_profiles_and_correlates_with_same_python(self):
        status, calls, build, out = self.invoke()
        self.assertEqual(status, 0)
        self.assertEqual(len(calls), 4)
        command, kwargs = calls[0]
        self.assertEqual(command, [sys.executable,str(ROOT/'scripts/ma.py'), 'Sample', 'fast',
            '--build-dir',build,'--source-path','tests/custom.c'])
        self.assertEqual(kwargs['cwd'], str(ROOT))
        self.assertEqual(calls[1], (['assembler','=RTLMIN.MAC','/X','/O','/Z','/L'], {'cwd':build}))
        self.assertEqual(calls[2], (['emulator','-p','-s:123','-g:sample_profile.csv','SAMPLE.COM','-n','2'], {'cwd':build}))
        self.assertEqual(calls[3][0], [sys.executable,str(ROOT/'scripts/dccprof.py'),
            '--app','sample','--build-dir',build,'--profile-csv',str(Path(build)/'sample_profile.csv'),'--out-dir',out])

    def test_failures_stop_each_stage(self):
        for stage in range(1,5):
            with self.subTest(stage=stage):
                status,calls,_,_=self.invoke(failure=stage)
                self.assertEqual(status,19)
                self.assertEqual(len(calls),stage)

    def test_missing_profile_rejects_stale_capture(self):
        status,calls,_,_=self.invoke(empty=True)
        self.assertEqual(status,1)
        self.assertEqual(len(calls),3)

    def test_program_app_option_does_not_select_analysis_mode(self):
        with mock.patch.object(dccprof, 'profile_app', return_value=0) as profile:
            self.assertEqual(dccprof.main(['sample', '--', '--app', 'argument']), 0)
            profile.assert_called_once_with(['sample', '--', '--app', 'argument'])

    def test_analysis_mode_remains_available(self):
        with self.assertRaises(SystemExit) as exit:
            dccprof.main(['--app','sample','--build-dir','missing','--profile-csv','missing','--help'])
        self.assertEqual(exit.exception.code,0)


if __name__ == '__main__':
    unittest.main()
