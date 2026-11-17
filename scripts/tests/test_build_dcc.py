"""Native host build contracts, without requiring MSVC on Unix or vice versa."""
from contextlib import redirect_stdout, redirect_stderr
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('build_dcc', ROOT/'scripts/build-dcc.py')
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)


class Toolchains(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dcc build tests ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.env = {'ProgramFiles': str(self.root/'Program Files'),
                    'ProgramFiles(x86)': str(self.root/'Program Files (x86)')}

    def vcvars(self, arch='amd64'):
        p = (Path(self.env['ProgramFiles'])/'Microsoft Visual Studio/2022/Preview/VC/Auxiliary/Build'/
             build.msvc_toolchain(arch)['vcvars'])
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('@echo off\n')
        return p

    def test_preview_fallback_for_x64_and_arm64(self):
        for arch in ('AMD64', 'ARM64'):
            p = self.vcvars(arch)
            self.assertEqual(build.find_msvc(self.env, arch.lower()), p)
            p.unlink()

    def test_uppercase_environment_from_dict_os_environ(self):
        p = self.vcvars()
        env = {key.upper(): value for key, value in self.env.items()}
        self.assertEqual(build.find_msvc(env, 'amd64'), p)

    def test_vswhere_includes_preview_and_native_component(self):
        p = self.vcvars('arm64')
        instance = p.parents[3]
        where = Path(self.env['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
        where.parent.mkdir(parents=True)
        where.touch()
        result = subprocess.CompletedProcess([], 0, (str(instance)+'\n').encode('utf-8'), b'')
        with patch.object(build.subprocess, 'run', return_value=result) as run:
            self.assertEqual(build.find_msvc(self.env, 'arm64'), p)
        argv = run.call_args[0][0]
        self.assertIn('-prerelease', argv)
        self.assertIn('Microsoft.VisualStudio.Component.VC.Tools.ARM64', argv)

    def test_missing_toolchain_and_cmake_have_no_false_positive(self):
        p = self.vcvars()
        cmake = p.parents[3]/'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
        self.assertIsNone(build.visual_studio_cmake(p))
        cmake.parent.mkdir(parents=True)
        cmake.touch()
        self.assertEqual(build.visual_studio_cmake(p), cmake)
        p.unlink()
        with self.assertRaisesRegex(build.BuildError, 'MSVC x64'):
            build.find_msvc(self.env, 'amd64')

    def test_native_arch_overrides_emulated_python_arch(self):
        with patch.object(build, 'windows_native_machine', return_value='arm64'):
            self.assertEqual(build.native_arch({'PROCESSOR_ARCHITECTURE':'AMD64'}), 'arm64')
        with patch.object(build, 'windows_native_machine', return_value=None):
            self.assertEqual(build.native_arch({'PROCESSOR_ARCHITECTURE':'AMD64',
                                               'PROCESSOR_ARCHITEW6432':'ARM64'}), 'arm64')

    def test_environment_preserves_unicode_and_replaces_path_case_insensitively(self):
        raw = 'Path=C:\\Tools;C:\\Users\\Dávìd\nINCLUDE=C:\\headers\n=C:=internal\nDCC_VCVARS_BAT=temporary\n'
        result = subprocess.CompletedProcess([], 0, raw.encode('utf-16-le'), b'')
        with patch.object(build.subprocess, 'run', return_value=result) as run:
            env = build.msvc_environment(self.vcvars(), {'PATH':'old','KEEP':'yes'})
        self.assertEqual(build.env_path(env), 'C:\\Tools;C:\\Users\\Dávìd')
        self.assertEqual(env['KEEP'], 'yes')
        self.assertNotIn('PATH', env)
        self.assertNotIn('DCC_VCVARS_BAT', env)
        self.assertNotIn('', env)
        self.assertIn('/u /s /c', run.call_args[0][0])

    def test_environment_setup_failure_is_reported(self):
        result = subprocess.CompletedProcess([], 1, b'', b'vcvars failed')
        with patch.object(build.subprocess, 'run', return_value=result):
            with self.assertRaisesRegex(build.BuildError, 'vcvars failed'):
                build.msvc_environment(self.vcvars(), {})


class BuildContracts(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dcc native build ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in build.TARGETS:
            p = self.root/'src'/name
            p.mkdir(parents=True)
            (p/'main.c').write_text('int main(void) { return 0; }\n')
        self.args = build.parse_args(['--jobs','2'])

    def test_compiles_once_and_links_all_targets_after_compile_wave(self):
        calls = []
        def wave(jobs, *args):
            calls.append(jobs)
            if len(calls) == 2:
                for name in build.TARGETS:
                    (self.root/name).touch()
        with patch.object(build, 'unix_compiler', return_value=(['cc'],['-O2'])), \
             patch.object(build, 'parallel_wave', side_effect=wave):
            outputs, _, _ = build.build_tools(self.root, self.root/'objects with spaces',
                                              self.args, {}, 'Linux')
        self.assertEqual(len(calls), 2)
        self.assertEqual(len(calls[0]), 6)
        self.assertEqual(len(calls[1]), 6)
        self.assertTrue(all('-c' in argv for _, argv in calls[0]))
        self.assertTrue(all('-static' in argv and '-c' not in argv for _, argv in calls[1]))
        self.assertEqual([p.name for p in outputs], list(build.TARGETS))

    def test_compile_failure_prevents_linking(self):
        with patch.object(build, 'unix_compiler', return_value=(['cc'],[])), \
             patch.object(build, 'parallel_wave', side_effect=build.BuildError('compile failed')) as wave:
            with self.assertRaisesRegex(build.BuildError, 'compile failed'):
                build.build_tools(self.root, self.root/'objects', self.args, {}, 'Linux')
        self.assertEqual(wave.call_count, 1)

    def test_no_static_only_changes_linking(self):
        self.args.no_static = True
        calls = []
        def wave(jobs, *unused):
            calls.extend(jobs)
            for name in build.TARGETS:
                (self.root/name).touch()
        with patch.object(build, 'unix_compiler', return_value=(['cc'],[])), \
             patch.object(build, 'parallel_wave', side_effect=wave):
            build.build_tools(self.root, self.root/'objects', self.args, {}, 'Linux')
        self.assertTrue(all('-static' not in argv for _, argv in calls))

    def test_msvc_flags_and_outputs_remain_in_object_directories(self):
        calls = []
        vcvars = self.root/'VS/VC/Auxiliary/Build/vcvars64.bat'
        def wave(jobs, *unused):
            calls.extend(jobs)
            for name in build.TARGETS:
                (self.root/(name+'.exe')).touch()
        with patch.object(build, 'find_msvc', return_value=vcvars), \
             patch.object(build, 'msvc_environment', return_value={'PATH':'MSVC'}), \
             patch.object(build.shutil, 'which', return_value='cl.exe'), \
             patch.object(build, 'parallel_wave', side_effect=wave):
            build.build_tools(self.root, self.root/'objects with spaces', self.args, {}, 'Windows')
        for _, argv in calls[:6]:
            self.assertIn('/GL', argv)
            self.assertIn('/Oti2', argv)
            self.assertIn('/FS', argv)
            self.assertTrue(any(str(x).startswith('/Fo:') for x in argv))
        for _, argv in calls[6:]:
            self.assertIn('/LTCG', argv)
            self.assertIn('/DEBUG', argv)

    def test_parallel_process_failures_return_nonzero_with_diagnostics(self):
        jobs = [('first',[sys.executable,'-c','print("one")']),
                ('broken',[sys.executable,'-c','print("useful error");raise SystemExit(7)'])]
        output = io.StringIO()
        with redirect_stdout(output):
            with self.assertRaisesRegex(build.BuildError, 'broken.*exit 7'):
                build.parallel_wave(jobs, 2, self.root, dict(os.environ), False)
        self.assertIn('useful error', output.getvalue())
        self.assertLess(output.getvalue().index('first'), output.getvalue().index('broken'))

    def test_debugger_cmake_configuration_and_expected_artifacts(self):
        calls = []
        def invoke(argv, *unused):
            calls.append(argv)
            if '--build' in argv:
                (self.root/'dcc-debug-host').touch()
                (self.root/'libdcc-debug-io-adapter-example.so').touch()
        with patch.object(build.shutil, 'which', return_value='cmake'), \
             patch.object(build, 'invoke', side_effect=invoke):
            outputs = build.build_debug_host(self.root, self.root/'objects with spaces', self.args, {}, 'Linux', None)
        self.assertIn('-DCMAKE_BUILD_TYPE=Release', calls[0])
        self.assertIn('-DBUILD_TESTING=OFF', calls[0])
        self.assertIn('-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE='+str(self.root), calls[0])
        self.assertIn('--parallel', calls[1])
        self.assertEqual(len(outputs), 2)

    def test_missing_cmake_explains_minimal_build_option(self):
        with patch.object(build.shutil, 'which', return_value=None):
            with self.assertRaisesRegex(build.BuildError, '--skip-debug-host'):
                build.build_debug_host(self.root, self.root/'build', self.args, {}, 'Linux', None)

    def test_compiler_and_cflags_overrides_and_macos_flag(self):
        env = {'PATH':'tools','CC':'ccache clang','CFLAGS':'-O1 -DNAME="two words"'}
        with patch.object(build.shutil, 'which', side_effect=lambda x, **kw: None if ' ' in x else x), \
             patch.object(build, 'invoke', return_value='clang version 18'):
            cc, flags = build.unix_compiler(self.args, env, 'Darwin')
        self.assertEqual(cc, ['ccache','clang'])
        self.assertEqual(flags, ['-O1','-DNAME=two words','-fno-common'])

    def test_legacy_flags_and_new_controls(self):
        args = build.parse_args(['-OutputPath','build with spaces','-CC','clang','-NoStatic',
                                 '-VerboseCommands','-Jobs','3','--skip-debug-host'])
        self.assertEqual((args.output_path,args.cc,args.no_static,args.verbose,args.jobs),
                         ('build with spaces','clang',True,True,3))
        self.assertTrue(args.skip_debug_host)
        with redirect_stderr(io.StringIO()):
            with self.assertRaises(SystemExit):
                build.parse_args(['--jobs','0'])


class Wrappers(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='dcc wrappers with spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root/'scripts').mkdir()
        (self.root/'scripts/build-dcc.py').write_text(
            'import sys,json\nprint(json.dumps(sys.argv[1:]))\nraise SystemExit(23)\n')
        for name in ('m.sh','m.bat'):
            shutil.copyfile(ROOT/name, self.root/name)

    @unittest.skipIf(os.name=='nt', 'POSIX wrapper')
    def test_sh_forwards_arguments_from_other_cwd_and_exit_status(self):
        args = ['--output-path','objects with spaces','-Jobs','2']
        result = subprocess.run(['sh',str(self.root/'m.sh')]+args, cwd=str(self.root.parent),
                                env=dict(os.environ,PYTHON=sys.executable), stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual(json.loads(result.stdout), args)

    @unittest.skipUnless(os.name=='nt', 'Windows wrapper')
    def test_batch_forwards_quoted_arguments_and_exit_status(self):
        command = 'cmd.exe /d /s /c ""{}" --output-path "objects with spaces" -Jobs 2"'.format(self.root/'m.bat')
        result = subprocess.run(command, cwd=str(self.root.parent), env=dict(os.environ,PYTHON=sys.executable),
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True)
        self.assertEqual(result.returncode, 23, result.stderr)
        self.assertEqual(json.loads(result.stdout), ['--output-path','objects with spaces','-Jobs','2'])


if __name__ == '__main__':
    unittest.main()
