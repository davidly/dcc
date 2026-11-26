"""Generated MIR fuzz inventory and isolated worker contracts."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from scripts import mir_clobber as m
from scripts.tests.test_process_supervision_python import process_alive


class MirClobberRunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='clobber runner ')
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name).resolve()

    def test_custom_fuzz_seed_function_and_width_coverage(self):
        plan=m.plan(['fuzz'],[0,1],environment={})
        self.assertEqual(len(plan),48)
        for seed in (0,1):
            p=plan['fuzzforced-{}-spilled-all|stack-peep'.format(seed)]
            self.assertEqual(p['Parameters']['RequiredSelectorFunction'],'fuzz'+str((seed+(seed>>8))%12))
            self.assertEqual(p['Environment']['DCC_MIR_SELECT_FUNCTION'],p['Parameters']['RequiredSelectorFunction'])
        with self.assertRaisesRegex(RuntimeError,'both 8-bit and 16-bit'):m.plan(['fuzz'],[0,2])

    def test_manifests_and_shards_reject_missing_extra_and_duplicate_leaves(self):
        m.assert_manifest(['a','b'],['b','a']);m.assert_manifest([],[])
        for expected,actual in [(['a','b'],['a','a']),(['a','a'],['a']),(['a','b'],['a']),(['a'],['a','c']),(['a'],{}),(['a'],[1])]:
            with self.assertRaises(RuntimeError):m.assert_manifest(expected,actual)
        self.assertEqual(m.shard(['d','a','c','b'],1,2),['b','d'])
        with self.assertRaises(RuntimeError):m.shard(['a'],2,2)
        p={};args={'Name':'example','StackCheck':False,'Peep':True,'StackBytes':1024}
        m.add_execution(p,'Assert-RunCase',args,{})
        args['StackBytes']=1;self.assertEqual(p['example|nostack-peep']['Parameters']['StackBytes'],1024)
        with self.assertRaises(RuntimeError):m.add_execution(p,'Assert-RunCase',args,{})
        for values in ([],['a'],['a','b']):
            path=self.root/'manifest.json';m.write_json(path,values)
            self.assertEqual(json.loads(path.read_text()),values)

    def test_target_exit_evidence(self):
        result={'TimedOut':False,'ExitCode':0,'Output':'correct'}
        m.target_result(result,'fuzz','release',0,['correct'])
        for bad,code,text in [(result,1,['correct']),(dict(result,TimedOut=True),0,['correct']),(result,0,['wrong'])]:
            with self.assertRaises(RuntimeError):m.target_result(bad,'fuzz','release',code,text)

    def test_build_timeout_distinct_and_environment_not_mutated(self):
        args=m.parse_args(['--build-timeout','17','--run-timeout','3'])
        runner=m.Runner(args,self.root,base={'DCC_MIR_SELECT_FUNCTION':'parent'})
        entry={'Key':'probe|stack-nopeep','Command':'Assert-RunCase','Environment':{},
               'Parameters':{'Name':'probe','Sources':['input.c'],'StackCheck':True,'Peep':False}}
        with patch.object(m,'execute',return_value={'TimedOut':True,'ExitCode':-1,'Output':'partial compiler output'}) as run:
            with self.assertRaisesRegex(RuntimeError,r'probe build timed out after 17s \(stack-nopeep\)') as error:runner.run(entry)
            self.assertIn('partial compiler output',str(error.exception))
            self.assertEqual(run.call_args[0][3],17)
        self.assertEqual(runner.base,{'DCC_MIR_SELECT_FUNCTION':'parent'})

    def test_list_works_without_tools_and_argument_validation(self):
        path=self.root/'list.json'
        with patch.dict(os.environ,{'PATH':''}):
            self.assertEqual(m.main(['--cases','fuzz','--fuzz-seeds','0','1','--emulator','deliberately-missing','--list-executions',str(path)]),0)
        self.assertEqual(len(json.loads(path.read_text())),48)
        for args in (['--jobs','0'],['--build-timeout','0'],['--shard-index','2','--shard-count','2'],['--jobs','2','--shard-count','2']):
            with self.assertRaises(SystemExit):m.parse_args(args)

    def fake_worker(self):
        script=self.root/'worker.py'
        script.write_text('''import argparse,json,os,subprocess,sys,time
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--shard-index',type=int);p.add_argument('--shard-count',type=int);p.add_argument('--execution-manifest');a,_=p.parse_known_args()
r=Path(a.execution_manifest).parent;i=a.shard_index
print('worker-pid='+str(os.getpid()),flush=True)
print('worker-profile='+os.getenv('LLVM_PROFILE_FILE',''),flush=True)
print('worker-dcc='+os.getenv('DCC',''),flush=True)
keys=['a','b','c','d'][i::a.shard_count];behavior=os.getenv('CLOBBER_BEHAVIOR','success')
if behavior=='duplicate':keys=keys+keys
if behavior=='missing':keys=[]
if behavior=='unexpected':keys=['extra']
if behavior=='malformed':keys={}
Path(a.execution_manifest).write_text(json.dumps(keys))
if behavior in ('orphan','timeout'):
 child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'])
 (r/('descendant-'+str(i)+'.pid')).write_text(str(child.pid))
 (r/('leader-'+str(i)+'.pid')).write_text(str(os.getpid()))
 if behavior=='timeout':time.sleep(30)
if behavior=='failed':sys.exit(1)
''')
        return script

    def test_isolated_shards_profiles_and_negative_manifest_controls(self):
        script=self.fake_worker();args=m.parse_args(['--jobs','2'])
        base=dict(os.environ,DCC='instrumented-compiler',LLVM_PROFILE_FILE=str(self.root/'raw/dcc-%8m.profraw'))
        actual=m.run_shards(args,['a','b','c','d'],self.root,base,script=script,timeout=10)
        m.assert_manifest(['a','b','c','d'],actual)
        logs=[(self.root/('shard-'+str(i)+'.log')).read_text() for i in (0,1)]
        self.assertNotEqual(logs[0].splitlines()[0],logs[1].splitlines()[0])
        for index,log in enumerate(logs):
            self.assertIn('dcc-%8m-clobber-{}-{}.profraw'.format(os.getpid(),index),log)
            self.assertIn('worker-dcc=instrumented-compiler',log)
        self.assertEqual(base['LLVM_PROFILE_FILE'],str(self.root/'raw/dcc-%8m.profraw'))
        for behavior in ('duplicate','missing','unexpected','malformed','failed'):
            with self.subTest(behavior=behavior),self.assertRaises(RuntimeError):
                m.run_shards(args,['a','b','c','d'],self.root,dict(base,CLOBBER_BEHAVIOR=behavior),script=script,timeout=10)

    def test_interrupt_during_shard_startup_stops_already_started_worker(self):
        args=m.parse_args(['--jobs','2'])
        child=unittest.mock.Mock()
        with patch.object(m,'Process',side_effect=[child,KeyboardInterrupt]):
            with self.assertRaises(KeyboardInterrupt):
                m.run_shards(args,['a','b'],self.root,dict(os.environ),timeout=10)
        child.stop.assert_called_once_with()

    def test_timeout_and_orphan_pipes_kill_all_worker_trees(self):
        args=m.parse_args(['--jobs','2']);script=self.fake_worker()
        for behavior in ('timeout','orphan'):
            with self.subTest(behavior=behavior):
                with self.assertRaisesRegex(RuntimeError,'timed out'):
                    m.run_shards(args,['a','b','c','d'],self.root,dict(os.environ,CLOBBER_BEHAVIOR=behavior),script=script,timeout=4)
                pids=list(self.root.glob('*.pid'));self.assertEqual(len(pids),4)
                for path in pids:
                    pid=int(path.read_text());end=time.monotonic()+2
                    while process_alive(pid) and time.monotonic()<end:time.sleep(.01)
                    self.assertFalse(process_alive(pid),'owned process survived cleanup: '+str(pid));path.unlink()


if __name__=='__main__':unittest.main()
