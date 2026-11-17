"""Portable process-tree cleanup and drain contracts, without PowerShell."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from scripts.process_supervision import Process

ROOT=Path(__file__).resolve().parents[2]


def process_alive(pid):
    if os.name=='nt':
        import ctypes
        from ctypes import wintypes as w
        k=ctypes.WinDLL('kernel32',use_last_error=True)
        k.OpenProcess.argtypes=(w.DWORD,w.BOOL,w.DWORD);k.OpenProcess.restype=w.HANDLE
        k.WaitForSingleObject.argtypes=(w.HANDLE,w.DWORD);k.WaitForSingleObject.restype=w.DWORD
        k.CloseHandle.argtypes=(w.HANDLE,)
        handle=k.OpenProcess(0x100000,False,pid) # SYNCHRONIZE
        if not handle:return False
        try:return k.WaitForSingleObject(handle,0)==258
        finally:k.CloseHandle(handle)
    try:os.kill(pid,0)
    except ProcessLookupError:return False
    if sys.platform.startswith('linux'):
        try:return Path('/proc/{}/stat'.format(pid)).read_text().rsplit(')',1)[1].split()[0]!='Z'
        except (FileNotFoundError,ProcessLookupError):return False # reaped after kill(0)
    result=subprocess.run(['/bin/ps','-p',str(pid),'-o','stat='],capture_output=True,text=True)
    return result.returncode==0 and not result.stdout.strip().startswith('Z')


class SupervisionTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory(prefix='dcc process ')
        self.addCleanup(temp.cleanup)
        self.root=Path(temp.name).resolve()

    def start(self,code,**kwargs):
        child=Process([sys.executable,'-c',code],self.root,**kwargs)
        self.addCleanup(child.stop)
        return child

    def wait(self,predicate,timeout=5):
        end=time.monotonic()+timeout
        while not predicate():
            self.assertLess(time.monotonic(),end,'process condition did not complete')
            time.sleep(.01)

    def gone(self,pid):
        end=time.monotonic()+2
        while process_alive(pid) and time.monotonic()<end:time.sleep(.01)
        self.assertFalse(process_alive(pid),'owned process survived cleanup: '+str(pid))

    def test_environment_inheritance_overrides_and_parent_unchanged(self):
        expected={'DCC':'parent','DCC_MIR_CACHE_VERIFY':'cache','LLVM_PROFILE_FILE':'parent-profile',
                  'TMP':'parent-temp','TEMP':'parent-temp','TMPDIR':'parent-temp'}
        code='import os,json;print(json.dumps({k:os.getenv(k) for k in '+repr(list(expected))+'}))'
        with patch.dict(os.environ,expected):
            inherited=self.start(code).complete()
            changed=self.start(code,environment={'DCC':'child','LLVM_PROFILE_FILE':'child-profile'},
                               remove_environment=['DCC_MIR_CACHE_VERIFY']).complete()
            self.assertEqual({k:os.environ[k] for k in expected},expected)
        self.assertFalse(inherited['TimedOut']);self.assertEqual(inherited['ExitCode'],0)
        self.assertEqual(json.loads(inherited['Output']),expected)
        self.assertEqual(json.loads(changed['Output']),dict(expected,DCC='child',LLVM_PROFILE_FILE='child-profile',DCC_MIR_CACHE_VERIFY=None))

    def test_zero_budget_collects_complete_results_and_stop_is_idempotent(self):
        child=self.start('print(42)')
        self.wait(lambda:child.ready(5))
        result=child.complete(0)
        self.assertEqual(result,{'ExitCode':0,'TimedOut':False,'Output':'42\n' if os.name!='nt' else '42\r\n'})
        child.stop();child.stop()
        self.assertFalse(child.scope.exists())

    def orphan(self,**kwargs):
        code='import subprocess,sys;child=subprocess.Popen([sys.executable,"-c","import time;time.sleep(30)"]);print(child.pid,flush=True)'
        child=self.start(code,**kwargs)
        self.wait(lambda:child.process.poll() is not None)
        self.assertFalse(child.stdout.done.is_set())
        return child

    def test_completion_budget_includes_inherited_pipe_and_preserves_log(self):
        child=self.orphan(log=self.root/'drain.log')
        began=time.monotonic();result=child.complete(.2)
        self.assertTrue(result['TimedOut'])
        self.assertLess(time.monotonic()-began,1.5)
        self.gone(int(result['Output'].strip()))
        self.assertEqual((self.root/'drain.log').read_text().strip(),result['Output'].strip())
        self.assertFalse(list(self.root.rglob('process-id')))

    def test_post_exit_grace_preempts_long_overall_deadline(self):
        child=self.orphan(drain_timeout=.2)
        self.wait(lambda:child.ready(20))
        result=child.complete(20-child.elapsed)
        self.assertTrue(result['TimedOut']);self.assertLess(child.elapsed,2)
        self.gone(int(result['Output'].strip()))

    def test_deadline_and_explicit_stop_kill_live_processes(self):
        child=self.start('import time;print("started",flush=True);time.sleep(30)')
        self.wait(lambda:child.ready(.2))
        result=child.complete(0)
        self.assertTrue(result['TimedOut'])
        self.gone(child.process.pid)
        other=self.start('import time;time.sleep(30)')
        other.stop();other.stop();self.gone(other.process.pid)

    def test_empty_path_needs_no_setsid_perl_or_powershell(self):
        child=self.start('print(43)',environment={'PATH':str(self.root/'empty')})
        result=child.complete()
        self.assertEqual(result['ExitCode'],0,result)
        self.assertFalse(result['TimedOut']);self.assertEqual(result['Output'].strip(),'43')

    def test_nested_independent_session_is_cleaned_after_worker_exit(self):
        marker=self.root/'grandchild'
        nested='import os,pathlib,time;pathlib.Path('+repr(str(marker))+').write_text(str(os.getpid()));time.sleep(30)'
        code=('import sys,os,time;sys.path.insert(0,'+repr(str(ROOT/'scripts'))+');'
              'from process_supervision import Process;'
              'child=Process([sys.executable,"-c",'+repr(nested)+'],'+repr(str(self.root))+',parent_scope=os.environ["DCC_PROCESS_SCOPE"]);'
              '\nwhile not __import__("pathlib").Path('+repr(str(marker))+').exists():time.sleep(.01)\n')
        worker=self.start(code)
        self.wait(lambda:marker.exists())
        result=worker.complete(2)
        self.assertEqual(result['ExitCode'],0,result)
        self.gone(int(marker.read_text()))
        self.assertFalse(list(self.root.rglob('process-id')))


if __name__=='__main__':unittest.main()
