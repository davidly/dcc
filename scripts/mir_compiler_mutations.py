"""Isolated MIR compiler mutation controls; Python 3.7+ and standard library."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import sys
import time
import uuid
try:
    from .process_supervision import Process
except ImportError:
    from process_supervision import Process

ROOT=Path(__file__).resolve().parents[1]


def mutations():
    return json.loads((Path(__file__).with_name('mir-compiler-mutations.json')).read_text())


def start(argv,cwd,log=None,environment=None,parent_scope=None):
    cwd=Path(cwd).resolve()
    profiles,scratch=cwd/'profiles',cwd/'scratch'
    profiles.mkdir(parents=True,exist_ok=True);scratch.mkdir(parents=True,exist_ok=True)
    overrides={'LLVM_PROFILE_FILE':str(profiles/'%p-%m.profraw')}
    overrides.update({n:str(scratch) for n in ('TMPDIR','TMP','TEMP')})
    overrides.update(environment or {})
    removed=[n for n in os.environ if n.upper().startswith('DCC_') or n.upper()=='LLVM_PROFILE_FILE']
    return Process(argv,cwd,log,overrides,removed,parent_scope)


def classify(execution,mutation):
    if execution['TimedOut']:return 'invalid'
    text=execution['Output'];code=execution['ExitCode']
    def match(pattern):return re.search(pattern,text,re.MULTILINE) is not None
    if mutation.get('CompileProbe') or mutation.get('CacheVerifier'):
        mismatch=match(r'^; MIR CACHE MISMATCH mir_definition function=\S+ value=-?\d+ cached=-?\d+ uncached=-?\d+\r?$')
        fatal=match(r'^dcc: fatal: MIR use-cache mismatch\r?$')
        if code==1 and mismatch and fatal:return 'killed'
        if code==0 and not mismatch and not fatal:return 'survived'
    else:
        label=('MIR matcher restoration failures' if mutation.get('MatcherProbe') else
               'MIR paired-byte mutation failures' if mutation.get('PairedByteProbe') else 'MIR verifier failures')
        assertion=match('^'+re.escape(mutation.get('ExpectedFailure',''))+r'\r?$')
        failed=match('^'+re.escape(label)+r'=[1-9]\d*\r?$')
        if code==1 and assertion and failed:return 'killed'
        if code==0 and match('^'+re.escape(label)+r'=0\r?$') and not match('^FAIL ') and not failed:
            return 'survived'
    return 'invalid'


def find_binary(directory,name):
    directory=Path(directory)
    for relative in (name,name+'.exe','Debug/'+name,'Debug/'+name+'.exe'):
        if (directory/relative).is_file():return directory/relative
    raise RuntimeError('Mutation binary was not built: '+str(directory/name))


def write_json(path,value):
    path=Path(path)
    temp=path.with_name(path.name+'.tmp')
    temp.write_text(json.dumps(value,indent=2)+'\n')
    os.replace(str(temp),str(path))


def worker(repo,workspace,output,name,build_jobs):
    repo,workspace,output=map(lambda p:Path(p).resolve(),(repo,workspace,output))
    parent_scope=os.environ.get('DCC_PROCESS_SCOPE')
    result={'mutation':name,'outcome':'invalid','phase':'prepare','exitCode':None,'detail':'Worker did not complete'}
    def execute(argv,log,timeout,environment=None):
        execution=start(list(map(str,argv)),workspace/'output',output/log,environment,parent_scope).complete(timeout)
        result['exitCode']=execution['ExitCode']
        return execution
    def bad(execution):return execution['TimedOut'] or execution['ExitCode']!=0
    try:
        mutation=next((m for m in mutations() if m['Name']==name),None)
        if mutation is None:raise ValueError('Unknown compiler mutation: '+name)
        for directory in ('src','tests/host','include','bin','output'):(workspace/directory).mkdir(parents=True)
        shutil.copytree(repo/'src/dcc',workspace/'src/dcc')
        for source in (repo/'tests/host').glob('*.c'):shutil.copy2(source,workspace/'tests/host'/source.name)
        for source in repo.glob('*.h'):shutil.copy2(source,workspace/'include'/source.name)
        result['phase']='mutate'
        source=workspace/mutation.get('Source','src/dcc/dcc_mir.c')
        text=source.read_text()
        if mutation.get('Before'):
            if text.count(mutation['Before'])!=1:raise RuntimeError('Mutation anchor changed: '+name)
            text=text.replace(mutation['Before'],mutation['After'])
        if mutation.get('CompileProbe'):
            begin=text.find('static int mir_promote_objects(void)')
            if begin<0:raise RuntimeError('Promotion mutation start changed')
            end=text.find('struct MirAllocationSummary',begin)
            if end<=begin:raise RuntimeError('Promotion mutation end changed')
            body=text[begin:end]
            if body.count('mir_invalidate_use_cache();')!=3:raise RuntimeError('Promotion invalidation mutation inventory changed')
            text=text[:begin]+body.replace('mir_invalidate_use_cache();','(void)0;')+text[end:]
        source.write_text(text)
        spec=importlib.util.spec_from_file_location('mir_fuzz_source',repo/'scripts/new-mir-fuzz-source.py')
        generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
        (workspace/'probe.c').write_text(generator.generate(23117,1),encoding='ascii')
        result['phase']='configure'
        execution=execute(['cmake','-S',workspace/'src/dcc','-B',workspace/'cmake',
            '-DDCC_BUILD_MIR_TESTS=ON','-DDCC_ENABLE_COVERAGE=OFF','-DCMAKE_BUILD_TYPE=Debug',
            '-DDCC_RUNTIME_OUTPUT_DIRECTORY='+str(workspace/'bin')],'configure.log',120)
        if bad(execution):result['detail']='Configure failed or timed out; see configure.log';return
        result['phase']='build'
        execution=execute(['cmake','--build',workspace/'cmake','--target','mir-verify-test','dcc',
            '--config','Debug','--parallel',str(build_jobs)],'build.log',1800)
        if bad(execution):result['detail']='Build failed or timed out; see build.log';return
        if mutation.get('CompileProbe') or name=='baseline':
            result['phase']='compile'
            execution=execute([find_binary(workspace/'bin','dcc'),'-I',workspace/'include','-fstack-check','-c',
                workspace/'probe.c','-o',workspace/'output/PROBE.MAC'],'compile.log',60,{'DCC_MIR_CACHE_VERIFY':'1'})
            if mutation.get('CompileProbe'):
                result['outcome']=classify(execution,mutation);result['detail']='Cache probe; see compile.log';return
            if bad(execution) or not (workspace/'output/PROBE.MAC').is_file():
                result['detail']='Unmutated cache probe failed; see compile.log';return
        for flag,phase,script,source_name in [
            ('MatcherProbe','matcher','test-mir-matcher-restoration.py','tests/tmalloch.c'),
            ('PairedByteProbe','paired-byte','test-mir-paired-byte-restoration.py','tests/mir-clobber/pairbyte.c')]:
            probes=[m for m in mutations() if m.get(flag)] if name=='baseline' else [mutation] if mutation.get(flag) else []
            for probe in probes:
                result['phase']=phase
                log=(phase+'-'+probe['Name']+'.log') if name=='baseline' else 'test.log'
                argv=[sys.executable,repo/'scripts'/script,'--compiler',find_binary(workspace/'bin','dcc'),
                    '--source',repo/source_name,'--include-directory',workspace/'include','--expected-failure',probe['ExpectedFailure']]
                env={'DCC_MIR_REQUIRE_COMPLETE':'1','DCC_MIR_REQUIRE_EMIT':'1','DCC_MIR_CACHE_VERIFY':'1'}
                if flag=='MatcherProbe':
                    argv+=['--output-path',workspace/'output/TMALLOCH.MAC','--machine-mutation',probe[flag],
                           '--expected-reject',probe['MatcherReject']]
                    env.update(DCC_MIR_MACHINE_REPORT='1',DCC_MIR_SELECT_REPORT='1')
                else:argv+=['--output-directory',workspace/'output/paired-byte']
                execution=execute(argv,log,60,env)
                if name=='baseline':
                    if bad(execution):
                        result['detail']='Unmutated '+('matcher restoration' if phase=='matcher' else 'paired-byte')+' probe failed; see '+log
                        return
                else:
                    result['outcome']=classify(execution,probe)
                    result['detail']=('Matcher restoration' if phase=='matcher' else 'Paired-byte restoration')+' probe; see test.log'
                    return
        result['phase']='test'
        execution=execute([find_binary(workspace/'cmake','mir-verify-test')],'test.log',60,{'DCC_MIR_CACHE_VERIFY':'1'})
        result['outcome']=classify(execution,mutation)
        if name=='baseline':result['outcome']='passed' if result['outcome']=='survived' else 'invalid'
        result['detail']='Host verifier; see test.log'
    finally:
        write_json(output/'result.json',result)
        if (workspace/'output').is_dir():shutil.copytree(workspace/'output',output/'artifacts')


class OutputLock:
    def __init__(self,path):self.path=Path(path);self.file=None
    def __enter__(self):
        self.file=self.path.open('a+b')
        try:
            if os.name=='nt':
                import msvcrt
                self.file.seek(0);self.file.write(b'\0');self.file.flush();self.file.seek(0)
                msvcrt.locking(self.file.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(self.file.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BaseException:self.file.close();raise
        return self
    def __exit__(self,*args):self.file.close()


def campaign(output,jobs=1,build_jobs=2,worker_timeout=2100,names=()):
    output=(ROOT/Path(output)).resolve()
    cases=mutations()
    if names:
        names=set(name for value in names for name in value.split(','))
        unknown=names-{m['Name'] for m in cases}
        if unknown:raise ValueError('Unknown compiler mutation: '+', '.join(sorted(unknown)))
        cases=[m for m in cases if m['Name']=='baseline' or m['Name'] in names]
    output.mkdir(parents=True,exist_ok=True)
    results=[{'mutation':m['Name'],'outcome':'invalid','phase':'not-run','exitCode':None,
              'detail':'Baseline must pass before mutants run'} for m in cases]
    active=[];next_case=0;baseline_passed=False
    workspace=output/('work-'+uuid.uuid4().hex[:12])
    with OutputLock(output/'.lock'):
        try:
            workspace.mkdir()
            for case in cases:
                directory=output/case['Name']
                if directory.exists():shutil.rmtree(directory)
            while next_case<len(cases) or active:
                while next_case<len(cases) and len(active)<jobs and (next_case==0 or baseline_passed):
                    case=cases[next_case];directory=output/case['Name'];directory.mkdir()
                    results[next_case]['detail']='Worker did not complete'
                    write_json(directory/'result.json',results[next_case])
                    command=start([sys.executable,str(ROOT/'scripts/run-mir-compiler-mutation-worker.py'),
                        '--repo-root',str(ROOT),'--workspace',str(workspace/case['Name']),
                        '--output-directory',str(directory),'--name',case['Name'],'--build-jobs',str(build_jobs)],
                        directory,directory/'worker.log',parent_scope=os.environ.get('DCC_PROCESS_SCOPE'))
                    active.append((next_case,command,directory/'result.json'));next_case+=1
                if not active:break
                completed=[item for item in active if item[1].ready(worker_timeout)]
                if not completed:time.sleep(0.02);continue
                for item in completed:
                    active.remove(item);index,command,path=item
                    execution=command.complete(max(0,worker_timeout-command.elapsed))
                    try:result=json.loads(path.read_text())
                    except (OSError,ValueError):result=None
                    valid=(isinstance(result,dict) and execution['ExitCode']==0 and not execution['TimedOut']
                        and result.get('mutation')==cases[index]['Name']
                        and result.get('outcome') in ('passed','killed','survived','invalid')
                        and not (index>0 and result['outcome']=='passed'))
                    if valid:results[index]=result
                    else:
                        results[index].update(phase='worker',exitCode=execution['ExitCode'],
                            detail='Worker timed out; see worker.log' if execution['TimedOut'] else 'Worker failed; see worker.log')
                    if index==0:
                        baseline_passed=results[0]['outcome']=='passed'
                        if baseline_passed:
                            for pending in results[1:]:pending['detail']='Worker was not scheduled'
        finally:
            for _,command,_ in active:command.stop()
            write_json(output/'results.json',results)
            shutil.rmtree(workspace,ignore_errors=True)
    if not baseline_passed or any(r['outcome'] in ('survived','invalid') for r in results):
        raise RuntimeError('Compiler mutation checks failed: '+str(output/'results.json'))
    print('Compiler mutation controls: baseline passed, {} mutants killed'.format(len(cases)-1))
    return results


def bounded(low,high):
    def parse(value):
        number=int(value)
        if not low<=number<=high:raise argparse.ArgumentTypeError('must be {}..{}'.format(low,high))
        return number
    return parse


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output-directory','-OutputDirectory',default='build/mir-compiler-mutations')
    p.add_argument('--jobs','-Jobs',type=bounded(1,1024),default=1)
    p.add_argument('--build-jobs','-BuildJobs',type=bounded(1,1024),default=2)
    p.add_argument('--worker-timeout','-WorkerTimeout',type=bounded(1,86400),default=2100)
    p.add_argument('--names','-Names',nargs='*',default=[])
    args=p.parse_args(argv)
    try:campaign(args.output_directory,args.jobs,args.build_jobs,args.worker_timeout,args.names)
    except (OSError,RuntimeError,ValueError) as error:
        print('ERROR: '+str(error),file=sys.stderr);return 1
    return 0
