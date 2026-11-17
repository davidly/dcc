"""Host validation policy and failure paths, independent of installed compilers."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('scripts.host_validate',ROOT/'scripts/validate-unit-test.py')
validator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


class HostValidationTests(unittest.TestCase):
    def test_placeholder_matching_is_anchored_and_case_sensitive(self):
        self.assertTrue(validator.matches_baseline('Nov 17 2026 12:34:56 a/b 123',
            '{{DATE}} {{TIME}} a{{SEP}}b {{UINT}}'))
        for actual in ('X 123 suffix','x 123','prefix X 123'):
            self.assertFalse(validator.matches_baseline(actual,'X {{UINT}}'))
        self.assertTrue(validator.matches_baseline('{{UNKNOWN}}','{{UNKNOWN}}'))
        self.assertFalse(validator.matches_baseline('anything','{{UNKNOWN}}'))

    def test_host_exclusions_and_32bit_exception(self):
        skip=validator.skip_config
        self.assertEqual(skip({'ignore':True,'host':True},'unix','Linux',True),'config')
        o={'host':True,'requires-32bit-linux-host-compiler':True}
        self.assertEqual(skip(o,'unix','Linux',False),'host')
        self.assertIsNone(skip(o,'unix','Linux',True))
        self.assertEqual(skip({'requires-non-msvc-host-compiler':True},'msvc','Windows',False),'host')
        self.assertEqual(skip({'requires-non-macos-host-compiler':True},'unix','Darwin',False),'host')
        self.assertIsNone(skip({'requires-non-msvc-host-compiler':True},'unix','Linux',False))

    def test_cpm_only_features_remain_excluded(self):
        for code in ('#asm\nret\n#endasm','bdos(9, 0)','inp(0)','getch()','__BDOS','*((int *)6)'):
            self.assertIsNotNone(validator.cpm_reason(code),code)
        self.assertIsNone(validator.cpm_reason('int main(void) { return 0; }'))
        self.assertIsNone(validator.cpm_reason('my_bdos(0)'))

    def test_compile_flags_respect_host_overrides_and_target_char(self):
        c={'kind':'unix','command':'gcc','flags':['-m32'],'env':{'CFLAGS':'-O1'}}
        cmd=validator.compile_command(c,Path('tests/a.c'),Path('/tmp/a'),{'host-cflags':'-std=gnu89 -O0'},ROOT,'Darwin')
        self.assertIn('-O0',cmd)
        self.assertNotIn('-O1',cmd)
        for flag in ('-std=gnu89','-fsigned-char','-m32','-fno-common','-include','-lm'):
            self.assertIn(flag,cmd)
        self.assertEqual(cmd[-4:],[str(Path('tests/a.c')),'-o',str(Path('/tmp/a')),'-lm'])

    def test_msvc_command_keeps_shims_prelude_and_per_app_objects(self):
        c={'kind':'msvc','command':'cl','flags':[],'env':{}}
        exe=Path('folder with spaces')/'a.exe'
        cmd=validator.compile_command(c,Path('tests/a.c'),exe,{},ROOT,'Windows')
        self.assertIn('/std:c11',cmd)
        self.assertIn(str(ROOT/'tests/msvc-shims'),cmd)
        self.assertIn(str(ROOT/'tests/host-validate-prelude.h'),cmd)
        self.assertIn('/Fo:'+str(exe.with_suffix('.obj')),cmd)

    def test_m32_probe_accepts_only_a_successful_executable(self):
        with tempfile.TemporaryDirectory() as folder:
            def compiler(argv,**kwargs):
                from types import SimpleNamespace
                if argv[-1]=='--version':
                    return SimpleNamespace(stdout='gcc (GNU) 11.4\n',returncode=0)
                Path(argv[-1]).write_text('executable')
                return SimpleNamespace(returncode=0)
            with mock.patch.object(validator.shutil,'which',return_value='/tools/gcc'), \
                 mock.patch.object(validator.subprocess,'run',side_effect=compiler):
                c=validator.host_compiler(validator.parse_args([]),{},'Linux',Path(folder))
                self.assertEqual(c['flags'],['-m32'])
            self.assertEqual(list(Path(folder).iterdir()),[])

    def test_fixture_stdin_args_output_and_failure_paths(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'tests').mkdir()
            (root/'tests/a.c').write_text('int main(void) { return 0; }')
            fixture=root/'tests/fixture.dat';fixture.write_text('data')
            baselines=root/'baselines';baselines.mkdir();(baselines/'a.txt').write_text('ok\n')
            compiler={'kind':'unix','command':'gcc','env':{'CUSTOM':'value'},'flags':[]}
            def validate(outputs):
                calls=[]
                def run(cmd,cwd,timeout,stdin='',env=None):
                    calls.append((cmd,cwd,timeout,stdin,env))
                    if len(calls)==1:
                        Path(cmd[cmd.index('-o')+1]).write_text('fake executable')
                    return outputs[len(calls)-1]
                with mock.patch.object(validator.runall,'run',side_effect=run):
                    result=validator.validate_app('a',compiler,[fixture],root/'build',baselines,2,
                        {'args':'one two','stdin':'input'},root,'Linux')
                return result,calls
            result,calls=validate([(0,False,''),(3,False,'ok\r\n')])
            self.assertEqual(result[1],'Passed') # Preserve warning-only nonzero host exits.
            self.assertIn('WARNING',result[3][0])
            self.assertEqual(calls[1][0][1:],['one','two'])
            self.assertEqual(calls[1][3:],('input', compiler['env']))
            self.assertEqual((root/'build/a/FIXTURE.DAT').read_text(),'data')
            for outputs,count in (([(1,False,'error')],1), ([(0,True,'')],1),
                ([(0,False,''),(0,True,'')],2), ([(0,False,''),(0,False,'wrong')],2)):
                result,calls=validate(outputs)
                self.assertEqual(result[1],'Failed')
                self.assertEqual(len(calls),count)
            (baselines/'a.txt').unlink()
            self.assertEqual(validate([(0,False,''),(0,False,'ok')])[0][1],'Failed')

    def test_missing_sources_and_bad_arguments_fail(self):
        with mock.patch.object(validator,'ROOT',Path('/nonexistent')):
            self.assertEqual(validator.main(['--app','absent','--build-dir',tempfile.gettempdir()]),1)
        for argv in (['--jobs','0'],['--timeout','0']):
            with self.assertRaises(SystemExit):validator.parse_args(argv)


if __name__=='__main__':
    unittest.main()
