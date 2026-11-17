"""Phase-gated AST/MIR proof orchestration using Python 3.7+.

Coverage still invokes compiler-coverage.sh and its remaining PowerShell
clobber runner. --list needs only Python and never executes gates.
"""
import argparse
import concurrent.futures
import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
try:
    from .process_supervision import Process
except ImportError:
    from process_supervision import Process

ROOT=Path(__file__).resolve().parents[1]
PHASES=('canonical tool build','script tests and static audits','independent release CMake build',
    'normal MIR host tests','ASan/UBSan MIR host tests','debugger-host tests','isolated compiler mutation campaign',
    'strict stack release suite','strict no-stack release suite','extended generated-MIR census',
    'instrumented compiler coverage and mutation campaigns')
PRESERVED={'DCC_DIR','DCC_HOME','DCC_INCLUDE','DCC_LIB','DCC_RUNTIME','DCC_PROCESS_SCOPE'}


def bounded(low,high):
    def parse(value):
        number=int(value)
        if not low<=number<=high:raise argparse.ArgumentTypeError('must be {}..{}'.format(low,high))
        return number
    return parse


def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--jobs','-Jobs',type=bounded(1,1024),default=os.cpu_count() or 1)
    p.add_argument('--mutation-jobs','-MutationJobs',type=bounded(0,1024),default=0)
    p.add_argument('--mutation-build-jobs','-MutationBuildJobs',type=bounded(1,1024),default=4)
    p.add_argument('--timeout','-RunTimeout',type=bounded(1,3600),default=30)
    p.add_argument('--output-directory','-OutputDirectory')
    p.add_argument('--list','-List',action='store_true')
    p.add_argument('--require-complete','-RequireComplete',action='store_true')
    p.add_argument('--all','-All',action='store_true')
    args=p.parse_args(argv)
    args.mutation_build_jobs=min(args.jobs,args.mutation_build_jobs)
    args.mutation_jobs=args.mutation_jobs or max(1,args.jobs//args.mutation_build_jobs)
    return args


def clean_controls(environment):
    return {k:v for k,v in environment.items() if k.upper() in PRESERVED or
        not (k.upper() in ('DCC','DCCMAKE') or k.upper().startswith('DCC_'))}


def version_output(command):
    p=subprocess.run([command,'--version'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    if p.returncode and re.fullmatch(r'llvm-profdata(?:-\d+)?',Path(command).stem):
        p=subprocess.run([command,'merge','--version'],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    return p.returncode,p.stdout


def llvm_major(command):
    code,text=version_output(command)
    match=re.search(r'\bversion\s+(\d+)',text)
    if code or not match:raise RuntimeError('Could not determine LLVM version for '+str(command))
    return int(match.group(1))


def available_clang(environment=None):
    env=os.environ if environment is None else environment
    found=[]
    for folder in env.get('PATH','').split(os.pathsep):
        if not folder:continue
        path=Path(folder)
        try:
            for candidate in path.glob('clang-*'):
                name=candidate.stem if os.name=='nt' else candidate.name
                match=re.fullmatch(r'clang-(\d+)',name)
                if match and candidate.is_file() and os.access(str(candidate),os.X_OK):
                    found.append((int(match.group(1)),str(candidate)))
        except OSError:continue
    return max(found,key=lambda x:x[0])[1] if found else shutil.which('clang',path=env.get('PATH'))


def llvm_peer(compiler,tool):
    path=Path(shutil.which(compiler) or compiler)
    name=path.stem if os.name=='nt' else path.name
    match=re.fullmatch(r'clang(?:-cl)?-(\d+)',name)
    suffix='-'+(match.group(1) if match else str(llvm_major(str(path))))
    extension='.exe' if os.name=='nt' and path.suffix.lower()=='.exe' else ''
    for base in (tool+suffix,tool):
        for candidate in (str(path.parent/(base+extension)),base):
            found=shutil.which(candidate)
            if found:return found
    return None


def tool_status(label,command,expected):
    resolved=shutil.which(command) if command else None
    text='not resolved' if not command else command+' (not found)' if not resolved else resolved
    if resolved:
        code,version=version_output(resolved)
        text+=' ['+(version.splitlines()[0] if not code and version.splitlines() else 'version unavailable')+']'
    return '  {}: expected {}; found {}'.format(label,expected,text)


def coverage_error(summary,compiler,cov=None,profdata=None,major=None):
    expected='LLVM '+str(major) if major else 'clang or clang-<major> on PATH, or CC=/path/to/clang-<major>'
    raise RuntimeError(summary+'\nExpected versus found:\n'+'\n'.join([
        tool_status('CC',compiler,expected),tool_status('LLVM_COV',cov,expected),tool_status('LLVM_PROFDATA',profdata,expected)])+
        '\n\nSet matching LLVM tools explicitly, for example:\n'+
        '  export CC=/path/to/clang-18 LLVM_COV=/path/to/llvm-cov-18 LLVM_PROFDATA=/path/to/llvm-profdata-18')


def preflight(environment):
    # Coverage collection still uses the portable shell driver.
    for command in (sys.executable,'cmake','ctest','sh','ntvcm'):
        if not shutil.which(command):raise RuntimeError('Required command not found: '+command)
    compiler=environment.get('CC') or available_clang(environment)
    if not compiler or not shutil.which(compiler):coverage_error('Could not resolve a Clang compiler for the coverage phase.',compiler)
    compiler=shutil.which(compiler);major=llvm_major(compiler)
    tools=[]
    for variable,tool in [('LLVM_COV','llvm-cov'),('LLVM_PROFDATA','llvm-profdata')]:
        value=environment.get(variable)
        resolved=shutil.which(value) if value else llvm_peer(compiler,tool)
        if value and not resolved:coverage_error('Could not resolve '+tool+' from '+variable+'.',compiler,
            resolved if tool=='llvm-cov' else environment.get('LLVM_COV'),
            resolved if tool=='llvm-profdata' else environment.get('LLVM_PROFDATA'),major)
        tools.append(resolved)
    cov,profdata=tools
    if not cov or not profdata:
        if not shutil.which('xcrun'):coverage_error('Could not resolve the LLVM coverage companions required for the final phase.',compiler,cov,profdata,major)
    elif llvm_major(cov)!=major or llvm_major(profdata)!=major:
        coverage_error('Coverage tools must all match Clang LLVM '+str(major)+'.',compiler,cov,profdata,major)
    return compiler,cov,profdata


def shell_path(path):
    if os.name!='nt':return str(path)
    p=subprocess.run(['sh','-lc','cygpath -u -- "$1"','sh',str(path)],capture_output=True,text=True)
    if p.returncode or not p.stdout.strip():raise RuntimeError('Could not convert path for POSIX shell: '+str(path))
    return p.stdout.strip()


def cmd(name,argv,env=None):return {'name':name,'argv':list(map(str,argv)),'env':env or {}}
def task(phase,name,commands):return {'phase':phase,'name':name,'commands':commands}


def preparation_tasks(output,jobs,compiler):
    output=Path(output);threads=max(1,jobs//min(4,jobs))
    tasks=[task(2,'Python script tests',[cmd('Python script tests',[sys.executable,'-m','unittest','discover','-s','scripts/tests','-p','test_*.py'],{'CC':compiler})]),
        task(2,'MIR fuzz generator proof',[cmd('MIR fuzz generator proof',[sys.executable,'scripts/test-mir-fuzz-source.py'])]),
        task(2,'static and script audits',[
            cmd('MSVC toolchain-detection test',[sys.executable,'-m','unittest','scripts.tests.test_build_dcc']),
            cmd('dccpeep tests',[sys.executable,'scripts/run-dccpeep-tests.py']),
            cmd('IY runtime safety audit',[sys.executable,'scripts/rtl-iy-safety.py']),
            cmd('runtime coverage audit',[sys.executable,'scripts/audit-runtime-coverage.py'])])]
    for phase,folder,source,options,run_tests,env in [
        (3,'cmake-release','src/dcc',['-DCMAKE_BUILD_TYPE=Release'],False,{}),
        (4,'mir-host','src/dcc',['-DCMAKE_BUILD_TYPE=Debug','-DDCC_BUILD_MIR_TESTS=ON'],True,{}),
        (5,'mir-host-sanitize','src/dcc',['-DCMAKE_BUILD_TYPE=Debug','-DDCC_BUILD_MIR_TESTS=ON','-DCMAKE_C_COMPILER='+compiler,
            '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'],True,{'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'}),
        (6,'debug-host','src/dcc_debug_host',['-DBUILD_TESTING=ON','-DCMAKE_BUILD_TYPE=Debug'],True,{})]:
        directory=output/folder
        configure=['cmake','-S',source,'-B',directory]+options
        if phase in (3,4,5):configure+=['-DDCC_RUNTIME_OUTPUT_DIRECTORY='+str(directory/'bin')]
        configuration='Release' if phase==3 else 'Debug'
        commands=[cmd('configure '+PHASES[phase-1],configure),
                  cmd('build '+PHASES[phase-1],['cmake','--build',directory,'--config',configuration,'--parallel',threads])]
        if run_tests:commands.append(cmd('run '+PHASES[phase-1],['ctest','--test-dir',directory,'-C',configuration,'--parallel',threads,'--output-on-failure'],env))
        tasks.append(task(phase,PHASES[phase-1],commands))
    return tasks


def release_tasks(jobs,timeout):
    threads=max(1,jobs//2);env={'DCC_MIR_REQUIRE_COMPLETE':'1','DCC_MIR_REQUIRE_EMIT':'1'}
    common=[sys.executable,'scripts/runall.py','--mode','full','--extended','--timeout',str(timeout),
            '--failures-only','--throttle-limit',str(threads)]
    return [task(8,PHASES[7],[cmd('run strict stack release suite',common,env)]),
            task(9,PHASES[8],[cmd('run strict no-stack release suite',common+['--no-stack-check'],env)])]


def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()


class Runner:
    def __init__(self,output,environment):
        self.output=Path(output)
        self.environment=clean_controls(environment)
        self.environment['PATH']=str(ROOT)+os.pathsep+self.environment.get('PATH','')
        self.records=[{'phase':i+1,'name':name,'startedAt':None,'completedAt':None} for i,name in enumerate(PHASES)]
        self.active=set()

    def start(self,phase):
        record=self.records[phase-1]
        if record['startedAt'] is None:record['startedAt']=now()

    def finish(self,phase):
        self.start(phase);self.records[phase-1]['completedAt']=now()

    def command(self,command,log=None):
        env=dict(self.environment);env.update(command['env'])
        child=Process(command['argv'],ROOT,log,env,list(os.environ),env.get('DCC_PROCESS_SCOPE') or self.output/'process-scope')
        try:
            self.active.add(child)
            result=child.complete(float('inf'))
        finally:
            self.active.discard(child);child.stop()
        if log is None:print(result['Output'],end='',flush=True)
        if result['TimedOut'] or result['ExitCode']:
            raise RuntimeError(command['name']+' failed with exit code '+str(result['ExitCode'])+(' (timeout/drain failure)' if result['TimedOut'] else ''))

    def phase(self,phase,command):
        self.start(phase);print('\n[{}/{}] {}'.format(phase,len(PHASES),PHASES[phase-1]),flush=True)
        self.command(command);self.finish(phase)

    def group(self,tasks,limit):
        logs=self.output/'logs';logs.mkdir(parents=True,exist_ok=True)
        next_task=0;active={};failures=[]
        def worker(item,path):
            with path.open('w') as log:
                for index,command in enumerate(item['commands']):
                    log.write('-> '+command['name']+'\n');log.flush()
                    part=path.with_suffix('.part-'+str(index)+'.log')
                    try:self.command(command,part)
                    finally:
                        if part.exists():log.write(part.read_text());log.flush();part.unlink()
        with concurrent.futures.ThreadPoolExecutor(max_workers=limit) as pool:
            try:
                while active or (not failures and next_task<len(tasks)):
                    while not failures and next_task<len(tasks) and len(active)<limit:
                        item=tasks[next_task];next_task+=1
                        self.start(item['phase']);print('  -> start [{}/{}] {}'.format(item['phase'],len(PHASES),item['name']),flush=True)
                        path=logs/(re.sub(r'[^A-Za-z0-9_.-]','-',item['name'])+'.log')
                        active[pool.submit(worker,item,path)]=(item,path)
                    if not active:break
                    completed,_=concurrent.futures.wait(active,return_when=concurrent.futures.FIRST_COMPLETED)
                    for future in completed:
                        item,path=active.pop(future)
                        try:future.result()
                        except Exception as error:
                            failures.append(item['name']);print('  !! fail '+item['name']+': '+str(error),flush=True)
                            if path.exists():print(path.read_text(),flush=True)
                        else:
                            self.finish(item['phase']);print('  <- pass '+item['name'],flush=True)
            except BaseException:
                self.stop()
                raise
        if failures:raise RuntimeError('Parallel proof tasks failed: '+', '.join(failures))

    def stop(self):
        for child in list(self.active):child.kill_owned()


def run(args):
    compiler,cov,profdata=preflight(os.environ)
    output=(ROOT/args.output_directory).resolve() if args.output_directory else ROOT/'build'/('mir-proof-suite-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S')+'-'+str(os.getpid()))
    output.mkdir(parents=True,exist_ok=True)
    runner=Runner(output,os.environ);started=now();clock=time.monotonic()
    try:
        runner.phase(1,cmd('build canonical tools',[sys.executable,'scripts/build-dcc.py','--jobs',args.jobs]))
        runner.group(preparation_tasks(output,args.jobs,compiler),min(6,args.jobs))
        runner.phase(7,cmd('run isolated compiler mutants',[sys.executable,'scripts/run-mir-compiler-mutations.py',
            '--jobs',args.mutation_jobs,'--build-jobs',args.mutation_build_jobs,'--output-directory',output/'compiler-mutations']))
        runner.group(release_tasks(args.jobs,args.timeout),min(2,args.jobs))
        runner.phase(10,cmd('run extended generated-MIR census',[sys.executable,'scripts/mir-extended-census.py','--mode','both',
            '--require-complete','--jobs',args.jobs,'--output',output/'mir-extended.tsv']))
        coverage={'CC':shell_path(compiler),'DCC_COVERAGE_BUILD_DIR':shell_path(output/'compiler-coverage'),
            'DCC_COVERAGE_JOBS':str(args.jobs),'DCC_COVERAGE_CLOBBER_JOBS':str(min(8,args.jobs)),
            'DCC_COVERAGE_MUTATION_JOBS':str(args.jobs),'DCC_COVERAGE_CENSUS_JOBS':str(args.jobs)}
        if cov:coverage['LLVM_COV']=shell_path(cov)
        if profdata:coverage['LLVM_PROFDATA']=shell_path(profdata)
        if args.require_complete:coverage['DCC_COVERAGE_REQUIRE_COMPLETE']='1'
        runner.phase(11,cmd('run instrumented coverage workflow',['sh','scripts/compiler-coverage.sh'],coverage))
        receipt={'startedAt':started,'completedAt':now(),'elapsedSeconds':round(time.monotonic()-clock,3),
            'parameters':{'Jobs':args.jobs,'MutationJobs':args.mutation_jobs,'MutationBuildJobs':args.mutation_build_jobs,
                          'RequireComplete':args.require_complete,'All':args.all},'phases':runner.records}
        (output/'receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
        print('\nAll AST/MIR proof gates passed. Artifacts: '+str(output),flush=True)
    finally:runner.stop()


def main(argv=None):
    args=parse_args(argv)
    if args.list:
        for index,name in enumerate(PHASES,1):print('{}. {}'.format(index,name))
        return 0
    try:run(args)
    except (OSError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);return 1
    return 0
