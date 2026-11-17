"""Shared diagnostic verdicts, baseline updates and bounded parallel execution."""
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from scripts import diagnostics as d
from scripts import runall


class DiagnosticsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='dcc diagnostics ');self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve();self.tests=self.root/'tests';self.tests.mkdir()
        self.build=self.root/'build';self.build.mkdir();self.baselines=self.root/'baselines';self.baselines.mkdir()
        self.source=self.tests/'bad.c';self.source.write_text('bad source')
        (self.baselines/'bad.txt').write_text('<source>: error\n')

    def validate(self,result,update=False,source=None):
        def execute(argv,cwd,timeout):
            self.assertEqual(argv[1],str(source or self.source))
            self.assertEqual(cwd,self.root);self.assertEqual(timeout,17)
            self.assertFalse(Path(argv[-1]).exists(),'stale compiler output was reused')
            return result
        return d.validate(source or self.source,'compiler',self.build,self.baselines,execute,self.root,update,17)

    def test_paths_line_endings_final_newlines_and_stale_output(self):
        (self.build/'bad.MAC').write_text('stale')
        result=self.validate((1,False,str(self.source)+': error\r\n'))
        self.assertTrue(result.passed,result)
        self.assertEqual(d.normalize('',self.source),'')
        self.assertEqual(d.normalize('a\rb\r\nc'),'a\nb\nc\n')
        self.assertEqual(d.normalize(str(self.source).replace('\\','/'),self.source), '<source>\n')

    def test_warning_success_error_failure_and_mismatches(self):
        warning=self.tests/'warn-empty.c';warning.write_text('int main(void){}')
        (self.baselines/'warn-empty.txt').write_text('')
        self.assertTrue(self.validate((0,False,''),source=warning).passed)
        self.assertFalse(self.validate((1,False,''),source=warning).passed)
        self.assertFalse(self.validate((0,False,str(self.source)+': error')).passed)
        mismatch=self.validate((1,False,'other error'))
        self.assertEqual(mismatch.detail,'diagnostic mismatch');self.assertEqual(mismatch.actual,'other error\n')
        (self.baselines/'bad.txt').unlink()
        self.assertIn('missing baseline',self.validate((1,False,'error')).detail)

    def test_update_is_utf8_without_bom_and_refuses_invalid_evidence(self):
        (self.baselines/'bad.txt').unlink()
        result=self.validate((1,False,str(self.source)+': errör\r\n'),update=True)
        self.assertTrue(result.passed);self.assertTrue(result.updated)
        baseline=self.baselines/'bad.txt';expected='<source>: errör\n'.encode('utf-8')
        self.assertEqual(baseline.read_bytes(),expected)
        for result in [(0,False,'error'),(1,True,'partial'),(-11,False,'crash'),
                       (3221225477,False,'crash'),(127,False,'ERROR starting compiler')]:
            with self.subTest(result=result):
                self.assertFalse(self.validate(result,update=True).passed)
                self.assertEqual(baseline.read_bytes(),expected)
        self.assertEqual(list(self.baselines.iterdir()),[baseline])

    def test_parallel_suite_waits_for_each_result_and_uses_separate_outputs(self):
        other=self.tests/'warn-ok.c';other.write_text('valid')
        (self.baselines/'warn-ok.txt').write_text('')
        barrier=threading.Barrier(2);outputs=[];reports=[]
        def execute(argv,cwd,timeout):
            outputs.append(argv[-1]);barrier.wait(timeout=5)
            return (0,False,'') if Path(argv[1]).stem.startswith('warn-') else (1,False,str(self.source)+': error\n')
        results=d.run_suite('compiler',self.tests,self.baselines,self.build,2,execute,self.root,
                            report=lambda result,index,total:reports.append((index,total)))
        self.assertEqual([r.name for r in results],['bad','warn-ok'])
        self.assertTrue(all(r.passed for r in results));self.assertEqual(len(set(outputs)),2)
        self.assertEqual(reports,[(1,2),(2,2)])
        with self.assertRaisesRegex(RuntimeError,'No diagnostic tests'):
            d.run_suite('compiler',self.root/'absent',self.baselines,self.build,2,execute,self.root)

    def test_runall_uses_shared_verdict_and_fails_on_timeout_or_empty_suite(self):
        root=self.root/'repo';tests=root/'tests/diagnostics';tests.mkdir(parents=True)
        (tests/'bad.c').write_text('bad');baselines=tests/'baselines';baselines.mkdir()
        (baselines/'bad.txt').write_text('<source>: error\n')
        with patch.object(runall,'ROOT',root),patch.object(runall,'command',return_value='compiler'), \
             patch.object(runall,'run',return_value=(1,False,str(tests/'bad.c')+': error\n')) as execute:
            self.assertTrue(runall.diagnostics(self.build,1))
            execute.return_value=(1,True,str(tests/'bad.c')+': error\n')
            self.assertFalse(runall.diagnostics(self.build,1))
            (tests/'bad.c').unlink();self.assertFalse(runall.diagnostics(self.build,1))

    def test_cli_aliases_defaults_and_invalid_limits(self):
        args=d.parse_args(['-Dcc','custom','-Update','-Serial','-ThrottleLimit','2','-TestDir','inputs'])
        self.assertEqual((args.dcc,args.update,args.serial,args.jobs,args.test_dir),('custom',True,True,2,'inputs'))
        for args in (['--jobs','0'],['--timeout','0']):
            with self.assertRaises(SystemExit):d.parse_args(args)
        with patch.object(d,'ROOT',self.root):
            self.assertEqual(d.main(['--test-dir','absent']),1)


if __name__=='__main__':unittest.main()
