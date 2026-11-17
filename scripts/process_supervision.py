#!/usr/bin/env python3
"""Bounded process-tree lifetime and pipe draining using Python 3.7+ only.

Unix commands own a session. Windows commands enter a kill-on-close Job Object
before launching the target. Explicit parent scopes register nested Unix
sessions so an outer watchdog can clean them after a worker exits or crashes.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import threading
import time
import uuid


def windows_job(name, create=False):
    import ctypes
    from ctypes import wintypes as w
    k = ctypes.WinDLL('kernel32',use_last_error=True)
    k.CreateJobObjectW.argtypes=(ctypes.c_void_p,w.LPCWSTR);k.CreateJobObjectW.restype=w.HANDLE
    k.OpenJobObjectW.argtypes=(w.DWORD,w.BOOL,w.LPCWSTR);k.OpenJobObjectW.restype=w.HANDLE
    k.CloseHandle.argtypes=(w.HANDLE,);k.CloseHandle.restype=w.BOOL
    k.TerminateJobObject.argtypes=(w.HANDLE,w.UINT);k.TerminateJobObject.restype=w.BOOL
    k.AssignProcessToJobObject.argtypes=(w.HANDLE,w.HANDLE);k.AssignProcessToJobObject.restype=w.BOOL
    k.GetCurrentProcess.restype=w.HANDLE
    handle = k.CreateJobObjectW(None,name) if create else k.OpenJobObjectW(1,False,name)
    if not handle:raise ctypes.WinError(ctypes.get_last_error())
    try:
        if create:
            class Basic(ctypes.Structure):
                _fields_=[('process_time',ctypes.c_longlong),('job_time',ctypes.c_longlong),
                    ('flags',w.DWORD),('min_working',ctypes.c_size_t),('max_working',ctypes.c_size_t),
                    ('active',w.DWORD),('affinity',ctypes.c_size_t),('priority',w.DWORD),('scheduling',w.DWORD)]
            class Counters(ctypes.Structure):
                _fields_=[(n,ctypes.c_ulonglong) for n in ('read_ops','write_ops','other_ops','read_bytes','write_bytes','other_bytes')]
            class Limits(ctypes.Structure):
                _fields_=[('basic',Basic),('io',Counters),('process_memory',ctypes.c_size_t),
                    ('job_memory',ctypes.c_size_t),('peak_process',ctypes.c_size_t),('peak_job',ctypes.c_size_t)]
            limits=Limits();limits.basic.flags=0x2000 # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            k.SetInformationJobObject.argtypes=(w.HANDLE,ctypes.c_int,ctypes.c_void_p,w.DWORD)
            k.SetInformationJobObject.restype=w.BOOL
            if not k.SetInformationJobObject(handle,9,ctypes.byref(limits),ctypes.sizeof(limits)):
                raise ctypes.WinError(ctypes.get_last_error())
        elif not k.AssignProcessToJobObject(handle,k.GetCurrentProcess()):
            raise ctypes.WinError(ctypes.get_last_error())
        return k,handle
    except BaseException:
        k.CloseHandle(handle)
        raise


class Capture:
    def __init__(self,stream):
        self.stream=stream
        self.chunks=[]
        self.done=threading.Event()
        self.error=None
        self.thread=threading.Thread(target=self.read,daemon=True)
        self.thread.start()

    def read(self):
        try:
            while True:
                block=os.read(self.stream.fileno(),65536)
                if not block:break
                self.chunks.append(block)
        except OSError as error:
            self.error=error
        finally:
            self.done.set()

    def text(self):
        import locale
        return b''.join(self.chunks).decode(locale.getpreferredencoding(False),errors='replace')


class Process:
    def __init__(self,argv,cwd,log=None,environment=None,remove_environment=(),parent_scope=None,drain_timeout=5):
        if not 0.01 <= drain_timeout <= 86400:raise ValueError('invalid drain timeout')
        cwd=Path(cwd).resolve()
        self._ownership_lock=threading.RLock()
        self._owned_stopped=False
        self.started=time.monotonic()
        self.completed=False
        self.result=None
        self.log=Path(log) if log else None
        self.drain_timeout=drain_timeout
        self.drain_deadline=None
        self.job=None
        env=dict(os.environ)
        # Environment keys are case insensitive on Windows, including in a copy.
        removed={n.upper() if os.name=='nt' else n for n in remove_environment}
        for n in list(env):
            if (n.upper() if os.name=='nt' else n) in removed:env.pop(n,None)
        for n,v in (environment or {}).items():
            if os.name=='nt':
                for existing in list(env):
                    if existing.upper()==n.upper():env.pop(existing,None)
            if v is None:env.pop(n,None)
            else:env[n]=str(v)
        base=Path(parent_scope)/'children' if parent_scope else Path(cwd)/'scratch'
        self.scope=base/('p-'+uuid.uuid4().hex[:12])
        self.scope.mkdir(parents=True)
        env['DCC_PROCESS_SCOPE']=str(self.scope)
        job_name='Local\\dcc-process-'+uuid.uuid4().hex
        request=self.scope/'request.json'
        request.write_text(json.dumps({'argv':list(map(str,argv)),'scope':str(self.scope),'job':job_name}))
        try:
            if os.name=='nt':self.job=windows_job(job_name,create=True)
            options={'creationflags':subprocess.CREATE_NEW_PROCESS_GROUP} if os.name=='nt' else {}
            self.process=subprocess.Popen([sys.executable,str(Path(__file__).resolve()),'--launch',str(request)],
                cwd=str(cwd),env=env,stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.PIPE,**options)
            self.stdout=Capture(self.process.stdout)
            self.stderr=Capture(self.process.stderr)
        except BaseException:
            if hasattr(self,'process'):
                self.kill_owned()
                try:self.process.wait(timeout=1)
                except subprocess.TimeoutExpired:pass
            self.close_job()
            self.remove_scope()
            raise

    @property
    def elapsed(self):return time.monotonic()-self.started

    def ready(self,timeout=float('inf')):
        if self.completed:return True
        if self.process.poll() is not None:
            if self.stdout.done.is_set() and self.stderr.done.is_set():return True
            if self.drain_deadline is None:self.drain_deadline=self.elapsed+self.drain_timeout
        return self.elapsed>=timeout or (self.drain_deadline is not None and self.elapsed>=self.drain_deadline)

    def kill_owned(self):
        with self._ownership_lock:
            if self._owned_stopped:return
            if self.job:
                k,handle=self.job
                if not k.TerminateJobObject(handle,1):
                    import ctypes
                    raise ctypes.WinError(ctypes.get_last_error())
            else:
                def terminate(pid):
                    # A registered bootstrap may not have moved into its own
                    # session yet. Kill both forms, including a transition race.
                    for action in (os.killpg,os.kill,os.killpg):
                        try:action(pid,signal.SIGKILL)
                        except ProcessLookupError:pass
                        except PermissionError:pass
                terminate(self.process.pid)
                def descendants(scope):
                    # Stop each parent before listing its children, so it cannot
                    # register new independent sessions behind the traversal.
                    directory=scope/'children'
                    if not directory.is_dir():return
                    for child in list(directory.iterdir()):
                        try:pid=int((child/'process-id').read_text())
                        except (FileNotFoundError,ValueError):pass # bootstrap still shared its parent's group
                        else:terminate(pid)
                        descendants(child)
                descendants(self.scope)
            if self.process.poll() is None:
                try:self.process.kill()
                except ProcessLookupError:pass
            self._owned_stopped=True

    def close_job(self):
        with self._ownership_lock:
            if self.job:
                k,handle=self.job;k.CloseHandle(handle);self.job=None

    def remove_scope(self):
        import shutil
        shutil.rmtree(self.scope,ignore_errors=True)

    def complete(self,timeout=60):
        if self.completed:return self.result
        end=time.monotonic()+max(0,timeout)
        try:
            while not self.ready() and time.monotonic()<end:time.sleep(0.01)
            timed=(self.process.poll() is None or not self.stdout.done.is_set() or
                   not self.stderr.done.is_set() or self.stdout.error is not None or self.stderr.error is not None)
            self.kill_owned() # Even successful commands may leave descendants behind.
            cleanup=time.monotonic()+1
            try:self.process.wait(timeout=max(0,cleanup-time.monotonic()))
            except subprocess.TimeoutExpired:pass
            for capture in (self.stdout,self.stderr):capture.done.wait(max(0,cleanup-time.monotonic()))
            text=self.stdout.text()+self.stderr.text()
            if self.log:
                with self.log.open('w',newline='') as log:log.write(text)
            self.result={'ExitCode':self.process.returncode if self.process.returncode is not None else -1,
                         'TimedOut':timed,'Output':text}
            return self.result
        finally:
            try:
                self.kill_owned() # Also clean up if interrupted before normal completion.
            finally:
                self.close_job()
            # An interrupted wait still needs a bounded reap/drain before closing.
            deadline=time.monotonic()+1
            try:self.process.wait(timeout=max(0,deadline-time.monotonic()))
            except subprocess.TimeoutExpired:pass
            for capture in (self.stdout,self.stderr):capture.done.wait(max(0,deadline-time.monotonic()))
            if self.log and self.result is None:
                with self.log.open('w',newline='') as log:log.write(self.stdout.text()+self.stderr.text())
            # Readers own their descriptors until EOF; never block on a held pipe.
            for capture in (self.stdout,self.stderr):
                if capture.done.is_set():capture.stream.close()
            self.remove_scope()
            self.completed=True

    def stop(self):
        if not self.completed:self.complete(0)


def launch(request_path):
    request=json.loads(Path(request_path).read_text())
    job=None
    try:
        if os.name=='nt':job=windows_job(request['job'])
        scope=Path(request['scope'])
        (scope/'process-id').write_text(str(os.getpid()))
        # Registration must precede session separation: a cancelled parent can
        # then find us, or kill us in its group before registration completes.
        if os.name!='nt':os.setsid()
        (scope/'ready').touch()
        if os.name!='nt':
            os.execvpe(request['argv'][0],request['argv'],os.environ)
        return subprocess.Popen(request['argv']).wait()
    finally:
        if job:
            k,handle=job;k.CloseHandle(handle)


if __name__=='__main__':
    if len(sys.argv)!=3 or sys.argv[1]!='--launch':raise SystemExit('internal process launcher')
    sys.exit(launch(sys.argv[2]))
