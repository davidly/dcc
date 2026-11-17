"""Single-app Python driver contracts and native build integration."""
from contextlib import redirect_stderr
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('dcc_ma',ROOT/'scripts/ma.py')
ma = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(ma)
EXE = '.exe' if os.name=='nt' else ''


class Driver(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='dcc ma ')
        self.addCleanup(temporary.cleanup)
        # Resolve macOS /var aliases and Windows short temp-directory names,
        # just as the build driver does when resolving package assets.
        self.folder = Path(temporary.name).resolve()
        previous = Path.cwd()
        os.chdir(self.folder)
        self.addCleanup(os.chdir,previous)
        self.package = self.folder/'package'
        self.package.mkdir()
        (self.package/'DCCRTL.MAC').write_text('runtime')
        (self.package/'stdio.h').touch()
        self.patch = patch.object(ma,'ROOT',self.package)
        self.patch.start();self.addCleanup(self.patch.stop)
        Path('tests').mkdir()
        Path('tests/hello.c').write_text('int main(void) { return 0; }\n')

    def commands(self,*args,env=None):
        return ma.build_commands(ma.parse_args(['hello']+list(args)),env or {})

    def test_default_is_optimized_and_ignores_project_configuration(self):
        Path('dccmake.txt').write_text('not even a valid project file')
        command = self.commands()[0]
        self.assertIn('--no-config',command)
        self.assertIn('dcc-input='+str(Path('tests/hello.c')),command)
        self.assertIn('dcc-peep=true',command)
        self.assertIn('dcc-strip-unused=true',command)

    def test_legacy_modes_and_intermixed_options(self):
        for value in ['peep','opt','-O','-o','true']:
            self.assertEqual(ma.parse_args(['hello',value]).mode,'fast')
        for value in ['nopeep','nop','noopt','unopt','u','-u','false']:
            self.assertEqual(ma.parse_args(['hello',value]).mode,'nopeep')
        args = ma.parse_args(['hello','--build-dir','custom','nopeep'])
        self.assertEqual((args.mode,args.build_dir),('nopeep','custom'))

    def test_full_builds_both_modes_in_order(self):
        commands = self.commands('full')
        self.assertEqual(len(commands),2)
        self.assertIn('dcc-peep=true',commands[0])
        self.assertIn('dcc-peep=false',commands[1])

    def test_source_selection_and_explicit_source(self):
        Path('hello.c').write_text('local')
        self.assertTrue(ma.find_source(ma.parse_args(['hello'])).samefile('tests/hello.c'))
        # Case-insensitive filesystems may retain the caller's spelling.
        self.assertTrue(ma.find_source(ma.parse_args(['HELLO.C'])).samefile('tests/hello.c'))
        self.assertTrue(ma.find_source(ma.parse_args(['hello','-SourcePath','hello.c'])).samefile('hello.c'))

    def test_missing_source_and_runtime_fail_before_running(self):
        with self.assertRaises(ma.BuildError):
            ma.find_source(ma.parse_args(['missing']))
        with self.assertRaisesRegex(ma.BuildError,'Runtime not found'):
            self.commands(env={'DCC_RUNTIME':'missing.mac'})

    def test_float_and_long_detection_and_explicit_enabling(self):
        Path('tests/hello.c').write_text('printf("%8.3f %lu", f, n);')
        command = self.commands()[0]
        self.assertIn('dcc-floatio=true',command)
        self.assertIn('dcc-flongio=true',command)
        Path('tests/hello.c').write_text('int main() { return 0; }')
        command = self.commands(env={'DCC_FLOATIO':'1','DCC_LONGIO':'1'})[0]
        self.assertIn('dcc-floatio=true',command)
        self.assertIn('dcc-flongio=true',command)

    def test_tool_and_stack_overrides(self):
        env={'DCC':'./custom/dcc','DCCMAKE':'./custom/dccmake','DCC_STACK_SIZE':'768',
             'DCC_STRIP_UNUSED':'0','DCC_USE_EMULATED_M80':'1'}
        command = self.commands(env=env)[0]
        self.assertEqual(command[0],str((self.folder/'custom/dccmake').resolve()))
        self.assertIn('dcc-tool='+str((self.folder/'custom/dcc').resolve()),command)
        self.assertIn('dcc-strip-unused=false',command)
        self.assertIn('dcc-use-emulated-m80=true',command)
        self.assertEqual(command[command.index('-s')+1],'768')
        command = self.commands('-StackSize','1024',env=env)[0]
        self.assertEqual(command[command.index('-s')+1],'1024')

    def test_package_assets_headers_and_emulated_tools(self):
        package = self.folder/'installed package'
        (package/'lib').mkdir(parents=True)
        (package/'include').mkdir()
        (package/'lib/DCCRTL.MAC').write_text('installed runtime')
        (package/'m80.com').touch();(package/'l80.com').touch()
        command = self.commands('--emulated-m80','--emulated-l80',env={'DCC_HOME':str(package)})[0]
        self.assertIn('dcc-runtime='+str(package/'lib/DCCRTL.MAC'),command)
        self.assertIn(str(package/'include'),command)
        self.assertIn('m80-command='+str(package/'m80.com'),command)
        self.assertIn('l80-command='+str(package/'l80.com'),command)
        self.assertIn('dcc-use-emulated-l80=true',command)

    def test_full_stops_on_failure_and_propagates_exit_code(self):
        with patch.object(ma,'build_commands',return_value=[['first'],['second']]), \
             patch.object(ma.subprocess,'run',return_value=subprocess.CompletedProcess([],7)) as run:
            self.assertEqual(ma.main(['hello','full','--build-dir','nested/build']),7)
        self.assertEqual(run.call_count,1)
        self.assertTrue(Path('nested/build').is_dir())

    def test_missing_input_does_not_create_build_directory(self):
        with redirect_stderr(io.StringIO()),patch.object(ma.subprocess,'run') as run:
            self.assertEqual(ma.main(['missing','--build-dir','untouched']),1)
        run.assert_not_called()
        self.assertFalse(Path('untouched').exists())


@unittest.skipUnless((ROOT/('dccmake'+EXE)).is_file(),'built dccmake is required')
class NativeDriver(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='dcc-ma-native-')
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        (self.folder/'MAIN.C').write_text('int main(void) { return 0; }\n')

    def command(self,*args):
        env=dict(os.environ,DCC_ARGS='')
        return subprocess.run([str(ROOT/('dccmake'+EXE)),*args],cwd=self.folder,env=env,
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True,timeout=30)

    def test_no_config_skips_malformed_project_without_changing_normal_behavior(self):
        (self.folder/'dccmake.txt').write_text('this project is invalid\n')
        args=['dcc-input=MAIN.C','dcc-output=MAIN','dcc-runtime='+str(ROOT/'DCCRTL.MAC')]
        for name in ('dcc','dccpeep','dccrtlstrip','m80c','l80c'):
            args.append(name+'-tool='+str(ROOT/(name+EXE)))
        self.assertNotEqual(self.command(*args).returncode,0)
        result=self.command('--no-config',*args)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertTrue((self.folder/'build/MAIN.COM').is_file())

    def test_native_help_documents_emulator_flags_and_standalone_build(self):
        result=self.command('--help')
        self.assertEqual(result.returncode,0)
        self.assertIn('--no-config',result.stdout)
        self.assertIn('ntvcm-args',result.stdout)


if __name__=='__main__':
    unittest.main()
