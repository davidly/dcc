#!/usr/bin/env python3
"""Check matcher rejection/restoration evidence, distinguishing kills from errors."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('compiler','source','include-directory','output-path','machine-mutation','expected-reject','expected-failure'):
        p.add_argument('--'+name,required=True)
    args=p.parse_args(argv)
    env=dict(os.environ,DCC_MIR_MACHINE_MUTATE=args.machine_mutation,DCC_MIR_MACHINE_MUTATE_FUNCTION='main')
    output=Path(args.output_path)
    if output.exists():output.unlink()
    run=subprocess.run([args.compiler,'-I',args.include_directory,'-fstack-check','-c',args.source,'-o',str(output)],
                       env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    print(run.stdout,end='',flush=True)
    if run.returncode or not output.is_file():
        print('Matcher restoration compiler probe failed');return 2
    def match(pattern):return re.search(pattern,run.stdout,re.MULTILINE) is not None
    rejected=match(r'^; MIR machine function=main template=allocation-lifetime-runner reject='+re.escape(args.expected_reject)+r'\r?$')
    generic=match(r'^; MIR selection function=main selector=spilled-scalar-cfg result=mir(?: .*)?\r?$')
    accepted=match(r'^; MIR machine function=main template=allocation-lifetime-runner accept=emitted\r?$')
    scheduled=match(r'^; MIR selection function=main selector=scheduled-machine-cfg result=mir(?: .*)?\r?$')
    if rejected and generic and not accepted and not scheduled:
        print('MIR matcher restoration failures=0');return 0
    if accepted and scheduled and not rejected and not generic:
        print(args.expected_failure);print('MIR matcher restoration failures=1');return 1
    print('Matcher restoration probe produced unrelated selection evidence');return 2


if __name__=='__main__':sys.exit(main())
