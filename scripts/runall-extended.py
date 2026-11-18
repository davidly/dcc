#!/usr/bin/env python3
"""Standalone extended corpus runner sharing runall.py's build and verdicts (Python 3.7+)."""
import argparse
import os
from pathlib import Path
import shutil
import sys
import tempfile
try:from . import runall
except ImportError:import runall
ROOT=runall.ROOT


def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for short,long in [('C89','c89'),('C99','c99'),('C11','c11'),('All','all'),
                       ('Serial','serial'),('KeepBuild','keep_build'),('NoStackCheck','no_stack_check'),
                       ('UseEmulatedM80','emulated_m80'),('UseEmulatedL80','emulated_l80')]:
        p.add_argument('-'+short,'--'+long.replace('_','-'),dest=long,action='store_true')
    for short,long,default in [('Emulator','emulator','ntvcm'),('BuildDir','build_dir','build/extended-tests'),
                              ('SuiteDir','suite_dir','tests/extended-tests/tests/single-exec'),
                              ('SkipFile','skip_file','tests/_extended_test_overrides.json')]:
        p.add_argument('-'+short,'--'+long.replace('_','-'),dest=long,default=default)
    p.add_argument('-Mode','-mode','--mode',choices=['fast','nopeep','full'],default='fast')
    p.add_argument('-Test','--test',nargs='+',default=[])
    p.add_argument('-RunTimeout','--timeout',type=int,default=60)
    p.add_argument('-ThrottleLimit','--jobs','--throttle-limit',dest='throttle_limit',type=int,default=os.cpu_count() or 1)
    p.add_argument('-Help',action='help',help=argparse.SUPPRESS)
    a=p.parse_args(argv)
    if a.timeout<0 or a.throttle_limit<1:p.error('timeout must be nonnegative and jobs positive')
    a.failures_only=False;a.fail_fast=False;a.report=False;a.build_timeout=a.timeout
    return a


def main(argv=None):
    a=parse_args(argv);work=None;failed=True
    saved_stack=os.environ.get("DCC_FORCE_STACK_CHECK")
    if a.no_stack_check:os.environ.pop("DCC_FORCE_STACK_CHECK",None)
    else:os.environ["DCC_FORCE_STACK_CHECK"]="1"
    try:
        suite=(ROOT/a.suite_dir).resolve()
        if suite==(ROOT/'tests/extended-tests/tests/single-exec').resolve():
            if not runall.ensure_extended_suite(ROOT,suite):return 1
        standards=None if a.all else [s for s in ('c89','c99','c11') if getattr(a,s)] or ['c89']
        base=ROOT/a.build_dir;base.mkdir(parents=True,exist_ok=True)
        work=Path(tempfile.mkdtemp(prefix='run-',dir=str(base)))
        modes=['nopeep','peep'] if a.mode=='full' else ['peep' if a.mode=='fast' else 'nopeep']
        items,error=runall.prepare_extended_suite(a,work,modes,suite,ROOT/a.skip_file,standards,a.test,allow_empty=True)
        if error:raise RuntimeError(error)
        for name,reason in a.extended_skipped:print("SKIP {}: {}".format(name,reason))
        # A selection consisting entirely of explicitly ignored cases is valid.
        results=runall.execute(items,1 if a.serial else a.throttle_limit,False).results
        failed=any(not r.passed for r in results)
        print('extended: {} configurations, {} failure(s)'.format(len(results),sum(not r.passed for r in results)))
        return int(failed)
    except (OSError,ValueError,RuntimeError) as error:
        print('ERROR: '+str(error),file=sys.stderr);return 1
    finally:
        if saved_stack is None:os.environ.pop("DCC_FORCE_STACK_CHECK",None)
        else:os.environ["DCC_FORCE_STACK_CHECK"]=saved_stack
        if work:
            if a.keep_build:print('Build artifacts: '+str(work))
            else:shutil.rmtree(str(work))


if __name__=='__main__':raise SystemExit(main())
