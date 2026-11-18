"""Publish or republish a versioned DCC release tag (Python 3.7+)."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]


def invoke(argv,cwd=ROOT,check=True):
    result=subprocess.run(argv,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,universal_newlines=True)
    if check and result.returncode:raise RuntimeError(subprocess.list2cmdline(argv)+' failed:\n'+result.stdout)
    return result


def publish(args,command=invoke):
    version=args.version or (ROOT/args.version_file).read_text().strip()
    version=version.strip()
    if not re.fullmatch(r'v\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?',version):raise RuntimeError('Package version must look like v2.0.0')
    branch=command(['git','rev-parse','--abbrev-ref','HEAD']).stdout.strip()
    if branch=='HEAD' and not args.no_push_branch:raise RuntimeError('Detached HEAD; use --no-push-branch or check out a branch')
    if command(['git','status','--porcelain']).stdout.strip() and not args.allow_dirty:raise RuntimeError('Commit or stash changes, or pass --allow-dirty')
    if args.dry_run:
        print('Would publish {} from {} via {} (remote {})'.format(version,branch,args.workflow,args.remote))
        return
    def run(argv):
        print('+ '+subprocess.list2cmdline(argv),flush=True);return command(argv)
    run(['git','fetch',args.remote,'--tags'])
    if not args.no_push_branch:run(['git','push',args.remote,branch])
    # Listing is checked: an authentication/network error must never be treated
    # as evidence that a release is absent.
    release=command(['gh','api','repos/{owner}/{repo}/releases/tags/'+version],check=False)
    if release.returncode and 'HTTP 404' not in release.stdout:
        raise RuntimeError('Could not query release: '+release.stdout)
    if release.returncode==0:
        run(['gh','release','delete',version,'--cleanup-tag','--yes'])
    if command(['git','ls-remote','--tags',args.remote,'refs/tags/'+version]).stdout.strip():
        run(['git','push',args.remote,':refs/tags/'+version])
    if command(['git','tag','--list',version]).stdout.strip():run(['git','tag','-d',version])
    run(['git','tag',version]);run(['git','push',args.remote,'refs/tags/'+version])
    run(['gh','run','list','--workflow',args.workflow,'--limit','5'])
    if args.watch:run(['gh','run','watch'])


def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name,default in [('Version',''),('VersionFile','scripts/package-version.txt'),('Remote','origin'),('Workflow','release.yml')]:
        key=re.sub('([a-z])([A-Z])',r'\1-\2',name).lower();p.add_argument('--'+key,'-'+name,default=default)
    for short,long in [('AllowDirty','allow-dirty'),('NoPushBranch','no-push-branch'),('Watch','watch')]:p.add_argument('--'+long,'-'+short,action='store_true')
    p.add_argument('--dry-run',action='store_true');return p.parse_args(argv)


def main(argv=None):
    try:publish(parse_args(argv));return 0
    except (OSError,RuntimeError,ValueError) as error:print('ERROR: '+str(error),file=sys.stderr);return 1
