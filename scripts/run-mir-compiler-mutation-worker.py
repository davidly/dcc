#!/usr/bin/env python3
"""Internal isolated mutation worker; invoked by run-mir-compiler-mutations.py."""
import argparse
from mir_compiler_mutations import bounded,worker


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('repo-root','workspace','output-directory','name'):
        p.add_argument('--'+name,required=True)
    p.add_argument('--build-jobs',type=bounded(1,1024),default=2)
    args=p.parse_args()
    worker(args.repo_root,args.workspace,args.output_directory,args.name,args.build_jobs)


if __name__=='__main__':
    main()
