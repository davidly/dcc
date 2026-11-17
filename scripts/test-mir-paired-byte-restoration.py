#!/usr/bin/env python3
"""Check paired-byte adjacency positive and negative controls with Python 3.7+."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('compiler','source','include-directory','output-directory'):
        p.add_argument('--'+name,required=True)
    p.add_argument('--expected-failure',default='FAIL paired-byte matcher accepted nonadjacent fields')
    args=p.parse_args(argv)
    output=Path(args.output_directory);output.mkdir(parents=True,exist_ok=True)
    env=dict(os.environ,DCC_MIR_SELECT_FUNCTION='read_pair',DCC_MIR_SELECT_CANDIDATE='regional')
    failures=0
    for name,defines in [('adjacent',[]),('nonadjacent',['-DMIR_CLOBBER_PAIRED_GAP=1'])]:
        target=output/(name+'.MAC')
        if target.exists():target.unlink()
        run=subprocess.run([args.compiler,'-I',args.include_directory,'-fstack-check','-c']+defines+
                           [args.source,'-o',str(target)],env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if run.returncode or not target.is_file():
            print(run.stdout,end='');print('Paired-byte mutation control did not compile: '+name);return 2
        present=';@dcc.mir paired-byte-call' in target.read_text()
        if name=='adjacent' and not present:
            print('FAIL paired-byte matcher rejected adjacent fields');failures+=1
        if name=='nonadjacent' and present:
            print(args.expected_failure);failures+=1
    print('MIR paired-byte mutation failures='+str(failures))
    return 1 if failures else 0


if __name__=='__main__':sys.exit(main())
