"""Shared diagnostic baseline validation and CLI, using Python 3.7+."""
import argparse
import concurrent.futures
from dataclasses import dataclass
import os
from pathlib import Path
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]


@dataclass
class Result:
    name: str
    passed: bool
    updated: bool = False
    detail: str = ''
    expected: str = ''
    actual: str = ''


def normalize(text,source=None):
    text=text.replace('\r\n','\n').replace('\r','\n')
    if source is not None:
        paths=[str(source.resolve()),str(source)]
        for path in paths+ [p.replace('\\','/') for p in paths]:
            text=text.replace(path,'<source>')
    return text+'\n' if text and not text.endswith('\n') else text


def write_baseline(path,text):
    path.parent.mkdir(parents=True,exist_ok=True)
    descriptor,temp=tempfile.mkstemp(prefix=path.name+'.',dir=str(path.parent))
    try:
        with os.fdopen(descriptor,'w',encoding='utf-8',newline='\n') as stream:stream.write(text)
        os.replace(temp,str(path))
    finally:
        if os.path.exists(temp):os.unlink(temp)


def validate(source,dcc,build,baselines,execute,root,update=False,timeout=60):
    name=source.stem;output=build/(name+'.MAC');baseline=baselines/(name+'.txt')
    try:
        try:output.unlink()
        except FileNotFoundError:pass
        code,timed,text=execute([dcc,str(source),'-o',str(output)],root,timeout)
        actual=normalize(text,source)
        if timed:return Result(name,False,detail='compiler timed out after {} seconds'.format(timeout),actual=actual)
        if code<0 or code>255 or (code==127 and text.startswith('ERROR starting')):
            return Result(name,False,detail='compiler failed to start or crashed (exit {})'.format(code),actual=actual)
        if (code==0)!=name.startswith('warn-'):
            detail='expected compile success, got failure (exit {})'.format(code) if name.startswith('warn-') else 'expected compile failure, got success'
            return Result(name,False,detail=detail,actual=actual)
        if update:
            write_baseline(baseline,actual)
            return Result(name,True,updated=True)
        if not baseline.is_file():return Result(name,False,detail='missing baseline '+str(baseline),actual=actual)
        expected=normalize(baseline.read_text(encoding='utf-8'))
        return Result(name,actual==expected,detail='' if actual==expected else 'diagnostic mismatch',expected=expected,actual=actual)
    except OSError as error:
        return Result(name,False,detail=str(error))


def run_suite(dcc,test_dir,baselines,build,workers,execute,root=ROOT,update=False,timeout=60,report=None):
    tests=sorted(p for p in Path(test_dir).glob('*.c') if p.is_file())
    if not tests:raise RuntimeError('No diagnostic tests found in '+str(test_dir))
    build=Path(build);build.mkdir(parents=True,exist_ok=True)
    def one(source):return validate(source,dcc,build,Path(baselines),execute,root,update,timeout)
    results=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(workers,len(tests))) as pool:
        pending=[pool.submit(one,source) for source in tests]
        for future in concurrent.futures.as_completed(pending):
            result=future.result();results.append(result)
            if report:report(result,len(results),len(tests))
    return sorted(results,key=lambda r:r.name)


def positive(value):
    result=int(value)
    if result<1:raise argparse.ArgumentTypeError('must be positive')
    return result


def parse_args(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dcc','-Dcc')
    parser.add_argument('--test-dir','-TestDir',default='tests/diagnostics')
    parser.add_argument('--baseline-dir','-BaselineDir',default='tests/diagnostics/baselines')
    parser.add_argument('--build-dir','-BuildDir',default='build/diagnostics')
    parser.add_argument('--update','-Update',action='store_true')
    parser.add_argument('--serial','-Serial',action='store_true')
    parser.add_argument('--jobs','--throttle-limit','-ThrottleLimit',type=positive,default=os.cpu_count() or 1)
    parser.add_argument('--timeout',type=positive,default=60)
    parser.add_argument('-Help',action='help',help=argparse.SUPPRESS)
    return parser.parse_args(argv)


def show(result,index,total):
    status='UPDATE' if result.updated else 'PASS' if result.passed else 'FAIL'
    print('[{}/{}] {} {}.c{}'.format(index,total,status,result.name,': '+result.detail if result.detail else ''),flush=True)
    if result.detail=='diagnostic mismatch':
        print('--- expected\n'+result.expected+'--- actual\n'+result.actual,flush=True)
    elif not result.passed and result.actual:print(result.actual,flush=True)


def main(argv=None):
    args=parse_args(argv)
    try:
        try:from . import runall
        except ImportError:import runall
        dcc=args.dcc or os.environ.get('DCC')
        if dcc and ('/' in dcc or '\\' in dcc):dcc=str((ROOT/dcc).resolve())
        if not dcc:
            local=ROOT/('dcc.exe' if os.name=='nt' else 'dcc')
            dcc=str(local) if local.is_file() else 'dcc'
        results=run_suite(dcc,ROOT/args.test_dir,ROOT/args.baseline_dir,ROOT/args.build_dir,
                          1 if args.serial else args.jobs,runall.run,root=ROOT,update=args.update,
                          timeout=args.timeout,report=show)
        failures=sum(not r.passed for r in results)
        print('diagnostics: {} failure(s)'.format(failures) if failures else
              'diagnostics: baselines updated' if args.update else 'diagnostics: all {} passed'.format(len(results)))
        return int(failures!=0)
    except (OSError,RuntimeError,ValueError) as error:
        print('ERROR: '+str(error),file=sys.stderr)
        return 1


if __name__=='__main__':raise SystemExit(main())
