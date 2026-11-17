"""Proof inventory, isolation, scheduling and receipts without long campaigns."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch
from scripts import mir_proof_suite as proof
from scripts.tests.test_process_supervision_python import process_alive

ROOT=Path(__file__).resolve().parents[2]


class ProofSuiteTests(unittest.TestCase):
    def test_inventory_requires_only_python_and_runs_no_gates(self):
        with tempfile.TemporaryDirectory() as cwd:
            p=subprocess.run([sys.executable,str(ROOT/'scripts/run-mir-proof-suite.py'),'--list'],
                cwd=cwd,env=dict(os.environ,PATH=''),capture_output=True,text=True,timeout=10)
        self.assertEqual(p.returncode,0,p.stderr)
        self.assertEqual(p.stdout.splitlines(),['{}. {}'.format(i,name) for i,name in enumerate(proof.PHASES,1)])
        self.assertEqual(len(proof.PHASES),11)

    def test_worker_budget_and_aliases(self):
        for jobs in (1,2,3,4,8,24):
            args=proof.parse_args(['-Jobs',str(jobs),'-All','-RequireComplete'])
            self.assertLessEqual(args.mutation_build_jobs,jobs)
            self.assertGreaterEqual(args.mutation_jobs,1)
            self.assertTrue(args.all and args.require_complete)
        args=proof.parse_args(['--jobs','24','--mutation-jobs','6','--mutation-build-jobs','4'])
        self.assertEqual((args.mutation_jobs,args.mutation_build_jobs),(6,4))
        for flag in ('--jobs','--mutation-build-jobs','--timeout'):
            with self.assertRaises(SystemExit):proof.parse_args([flag,'0'])

    def test_ambient_controls_are_cleaned_case_insensitively(self):
        env={'DCC':'bad','DCCMAKE':'bad','dcc_mir_cache_verify':'bad','DCC_FORCE_STACK_CHECK':'bad',
             'dcc_home':'home','DCC_LIB':'lib','DCC_PROCESS_SCOPE':'scope','OTHER':'keep'}
        clean=proof.clean_controls(env)
        self.assertEqual(clean,{'dcc_home':'home','DCC_LIB':'lib','DCC_PROCESS_SCOPE':'scope','OTHER':'keep'})
        self.assertEqual(env['DCC'],'bad')

    def test_newest_versioned_clang_and_colocated_llvm_companions(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);ext='.exe' if os.name=='nt' else ''
            for name in ('clang','clang-9','clang-18','clang-21','clang-not-a-version'):
                p=folder/(name+ext);p.touch();p.chmod(0o755)
            self.assertEqual(Path(proof.available_clang({'PATH':str(folder)})).name,'clang-21'+ext)
            for tool in ('llvm-cov','llvm-profdata'):
                for name in (tool,tool+'-21'):
                    p=folder/(name+ext);p.touch();p.chmod(0o755)
                self.assertEqual(Path(proof.llvm_peer(str(folder/('clang-21'+ext)),tool)).name,tool+'-21'+ext)
                (folder/(tool+'-21'+ext)).unlink()
                self.assertEqual(Path(proof.llvm_peer(str(folder/('clang-21'+ext)),tool)).name,tool+ext)
            with patch.object(proof,'llvm_major',return_value=21):
                self.assertEqual(Path(proof.llvm_peer(str(folder/('clang'+ext)),'llvm-cov')).name,'llvm-cov'+ext)

    def test_llvm_major_profdata_fallback_and_mismatch_evidence(self):
        result=lambda code,text:subprocess.CompletedProcess([],code,text)
        with patch.object(proof.subprocess,'run',side_effect=[result(1,'unknown'),result(0,'llvm-profdata\nLLVM version 18.1')]) as run:
            self.assertEqual(proof.llvm_major('llvm-profdata-18'),18)
            self.assertEqual(run.call_args[0][0],['llvm-profdata-18','merge','--version'])
        with patch.object(proof.shutil,'which',side_effect=lambda c,**kwargs:c), \
             patch.object(proof,'llvm_major',side_effect=lambda c:17 if c=='llvm-cov-17' else 18), \
             patch.object(proof,'version_output',return_value=(0,'LLVM version 18')):
            with self.assertRaisesRegex(RuntimeError,'Coverage tools must all match Clang LLVM 18') as error:
                proof.preflight({'CC':'clang-18','LLVM_COV':'llvm-cov-17','LLVM_PROFDATA':'llvm-profdata-18'})
            for text in ('Expected versus found:','CC: expected','LLVM_COV: expected','LLVM_PROFDATA: expected','export CC='):
                self.assertIn(text,str(error.exception))

    def test_isolated_builds_sanitizers_and_parallel_release_plan(self):
        output=Path('/artifact root');tasks=proof.preparation_tasks(output,8,'clang-18')
        self.assertEqual([t['phase'] for t in tasks],[2,2,2,3,4,5,6])
        self.assertEqual(tasks[0]['commands'][0]['env'],{'CC':'clang-18'})
        self.assertEqual(tasks[1]['commands'][0]['argv'],[sys.executable,'scripts/test-mir-fuzz-source.py'])
        cmake=[c['argv'] for t in tasks for c in t['commands'] if c['argv'][:2]==['cmake','-S']]
        outputs=[a for command in cmake for a in command if a.startswith('-DDCC_RUNTIME_OUTPUT_DIRECTORY=')]
        self.assertEqual(len(outputs),3)
        self.assertTrue(all(str(output) in a and a.endswith('bin') for a in outputs))
        sanitized=next(t for t in tasks if t['phase']==5)
        self.assertIn('-DCMAKE_C_COMPILER=clang-18',sanitized['commands'][0]['argv'])
        self.assertIn('-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer',sanitized['commands'][0]['argv'])
        self.assertEqual(sanitized['commands'][-1]['env'],{'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
        for item in proof.release_tasks(8,90):
            command=item['commands'][0]
            for arg in ('--mode','full','--extended','--timeout','90','--throttle-limit','4'):self.assertIn(arg,command['argv'])
            self.assertEqual(command['env'],{'DCC_MIR_REQUIRE_COMPLETE':'1','DCC_MIR_REQUIRE_EMIT':'1'})
            self.assertEqual('--no-stack-check' in command['argv'],item['phase']==9)

    def test_failed_group_stops_new_tasks_and_retains_logs(self):
        with tempfile.TemporaryDirectory() as folder:
            runner=proof.Runner(folder,os.environ)
            started=[];second=threading.Event()
            def command(c,log=None):
                started.append(c['name'])
                if c['name']=='first':
                    self.assertTrue(second.wait(2));Path(log).write_text('failure evidence');raise RuntimeError('failed')
                if c['name']=='second':second.set();time.sleep(.05)
            tasks=[proof.task(2,n,[proof.cmd(n,[n])]) for n in ('first','second','never')]
            with patch.object(runner,'command',side_effect=command):
                with self.assertRaisesRegex(RuntimeError,'Parallel proof tasks failed'):runner.group(tasks,2)
            self.assertEqual(set(started),{'first','second'})
            self.assertIn('failure evidence',(Path(folder)/'logs/first.log').read_text())

    def test_commands_register_under_an_inherited_parent_scope(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder).resolve();outer=folder/'outer';marker=folder/'scope'
            runner=proof.Runner(folder,dict(os.environ,DCC_PROCESS_SCOPE=str(outer)))
            code='import os,pathlib;pathlib.Path('+repr(str(marker))+').write_text(os.environ["DCC_PROCESS_SCOPE"])'
            runner.command(proof.cmd('nested scope',[sys.executable,'-c',code]))
            Path(marker.read_text()).relative_to(outer/'children')
            self.assertFalse(list(folder.rglob('process-id')))

    def test_interrupted_group_cancels_owned_processes_before_waiting_for_threads(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);pid_file=folder/'pid';runner=proof.Runner(folder,os.environ)
            original=runner.command
            def command(c,log=None):
                if c['name']=='interrupt':
                    end=time.monotonic()+5
                    while not pid_file.exists():
                        if time.monotonic()>end:raise RuntimeError('peer did not start')
                        time.sleep(.01)
                    raise KeyboardInterrupt()
                return original(c,log)
            code='import os,pathlib,time;pathlib.Path('+repr(str(pid_file))+').write_text(str(os.getpid()));time.sleep(30)'
            tasks=[proof.task(2,'interrupt',[proof.cmd('interrupt',[])]),proof.task(3,'peer',[proof.cmd('peer',[sys.executable,'-c',code])])]
            began=time.monotonic()
            with patch.object(runner,'command',side_effect=command):
                with self.assertRaises(KeyboardInterrupt):runner.group(tasks,2)
            self.assertLess(time.monotonic()-began,3)
            self.assertFalse(process_alive(int(pid_file.read_text())))
            self.assertFalse(runner.active)
            self.assertFalse(list(folder.rglob('process-id')))

    def test_complete_plan_writes_receipt_and_failure_has_no_success_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder);commands=[]
            args=proof.parse_args(['--jobs','3','--output-directory',str(folder),'--require-complete','--all'])
            with patch.object(proof,'preflight',return_value=('clang-18','llvm-cov-18','llvm-profdata-18')), \
                 patch.object(proof,'shell_path',side_effect=str), \
                 patch.object(proof.Runner,'command',side_effect=lambda command,log=None:commands.append(command)):
                proof.run(args)
            receipt=json.loads((folder/'receipt.json').read_text())
            self.assertEqual([p['name'] for p in receipt['phases']],list(proof.PHASES))
            self.assertTrue(all(p['startedAt'] and p['completedAt'] for p in receipt['phases']))
            self.assertEqual(receipt['parameters'],{'Jobs':3,'MutationJobs':1,'MutationBuildJobs':3,'RequireComplete':True,'All':True})
            coverage=commands[-1]
            self.assertEqual(coverage['argv'],['sh','scripts/compiler-coverage.sh'])
            self.assertEqual(coverage['env']['DCC_COVERAGE_REQUIRE_COMPLETE'],'1')
            self.assertEqual(coverage['env']['DCC_COVERAGE_CLOBBER_JOBS'],'3')
            (folder/'receipt.json').unlink()
            with patch.object(proof,'preflight',return_value=('clang-18',None,None)), \
                 patch.object(proof.Runner,'command',side_effect=RuntimeError('canonical build failed')):
                with self.assertRaisesRegex(RuntimeError,'canonical build failed'):proof.run(args)
            self.assertFalse((folder/'receipt.json').exists())

    def test_windows_shell_path_preserves_argument_boundaries(self):
        with patch.object(proof.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'/c/has space\n')) as run:
            converted=proof.shell_path('C:/has space')
        if os.name=='nt':
            self.assertEqual(converted,'/c/has space')
            self.assertEqual(run.call_args[0][0],['sh','-lc','cygpath -u -- "$1"','sh','C:/has space'])
        else:
            self.assertEqual(converted,'C:/has space');run.assert_not_called()


if __name__=='__main__':unittest.main()
