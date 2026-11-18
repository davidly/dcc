"""Repeated isolated suite runs, retaining failure logs (Python 3.7+)."""
import argparse
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time
try:from .process_supervision import Process
except ImportError:from process_supervision import Process
ROOT=Path(__file__).resolve().parents[1]


def positive(value):
    n=int(value)
    if n<1:raise argparse.ArgumentTypeError('must be positive')
    return n


def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--iterations','-Iterations',type=positive,default=10)
    p.add_argument('--serial','-Serial',action='store_true')
    p.add_argument('--jobs','--throttle-limit','-ThrottleLimit',type=positive,default=os.cpu_count() or 1)
    p.add_argument('--log-dir','-LogDir')
    p.add_argument('--keep-logs','-KeepLogs',action='store_true')
    p.add_argument('suite_args',nargs=argparse.REMAINDER,help='additional runall.py arguments after --')
    args=p.parse_args(argv)
    if args.suite_args[:1]==['--']:args.suite_args.pop(0)
    for arg in args.suite_args:
        flag=arg.split('=')[0]
        if flag in ('-buildDir','-keepBuild') or (flag.startswith('--') and any(full.startswith(flag) for full in ('--build-dir','--keep-build'))):
            p.error('the stress runner owns its build directories')
    return args


def run(args,root=ROOT,runner=None):
    root=Path(root);(root/'build').mkdir(exist_ok=True)
    logs=(root/args.log_dir).resolve() if args.log_dir else Path(tempfile.mkdtemp(prefix='dcc-stress-'))
    logs.mkdir(parents=True,exist_ok=True);print('Logs: '+str(logs),flush=True)
    work=Path(tempfile.mkdtemp(prefix='stress-',dir=str(root/'build')))
    try:
        for index in range(1,args.iterations+1):
            log=logs/('stress_'+str(index)+'.log')
            argv=[sys.executable,str(runner or root/'scripts/runall.py'),'--verbose','--no-ram-disk',
                  '--build-dir',str(work/str(index))]
            argv+=['--serial'] if args.serial else ['--throttle-limit',str(args.jobs)]
            argv+=args.suite_args
            child=Process(argv,root,log=log,environment={'DCC_STRESS_SUPERVISE':'1'},
                          parent_scope=os.environ.get('DCC_PROCESS_SCOPE'),drain_timeout=1)
            try:
                while not child.ready():time.sleep(.01)
                result=child.complete(0)
            finally:child.stop()
            if result['ExitCode'] or result['TimedOut']:
                print('[{}/{}] FAIL\nFull log: {}'.format(index,args.iterations,log),flush=True)
                lines=[line for line in result['Output'].splitlines() if re.search('FAIL|MISMATCH|WARNING|ERROR',line)][:20]
                print('\n'.join(lines));return 1
            print('[{}/{}] PASS'.format(index,args.iterations),flush=True)
            if not args.keep_logs:log.unlink()
        print('STRESS PASSED: {}/{} iterations succeeded'.format(args.iterations,args.iterations))
        return 0
    finally:
        shutil.rmtree(str(work)) # Never remove other builds, proof artifacts or logs.


def main(argv=None):
    try:return run(parse_args(argv))
    except KeyboardInterrupt:return 130
    except (OSError,RuntimeError) as error:
        print('ERROR: '+str(error),file=sys.stderr);return 1
