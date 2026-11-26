"""Generated MIR differential tests and isolated workers (Python 3.7+)."""
import argparse
import copy
import json
import importlib.util
import os
from pathlib import Path
import re
import shutil
import sys
import tempfile
import time
try:
    from .process_supervision import Process
except ImportError:
    from process_supervision import Process

ROOT=Path(__file__).resolve().parents[1]
ENVIRONMENT_NAMES=tuple('DCC_MIR_'+n for n in (
    'COST_REPORT','CACHE_VERIFY','EMIT_FUNCTION','MACHINE_REPORT','MACHINE_FUNCTION',
    'MACHINE_TEMPLATE','MACHINE_MUTATE','MACHINE_MUTATE_FUNCTION','REPORT','FUNCTION',
    'REQUIRE_COMPLETE','REQUIRE_EMIT','PREFER_REGIONAL','SELECT_CANDIDATE','SELECT_FUNCTION',
    'SELECT_REPORT_FUNCTION','SELECT_REPORT'))


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def controlled(values, base):
    names = {name.upper() for name in values}
    env = {key: value for key, value in base.items() if key.upper() not in names}
    env.update({key: str(value) for key, value in values.items() if value is not None})
    return env


def shard(keys,index,count):
    require(count>=1 and 0<=index<count,'Invalid MIR clobber shard {}/{}'.format(index,count))
    return sorted(keys)[index::count]


def assert_manifest(expected,actual):
    require(isinstance(actual,list) and all(isinstance(v,str) for v in actual),'Invalid MIR clobber shard manifest')
    require(len(set(expected))==len(expected),'duplicate MIR clobber expected key')
    require(len(set(actual))==len(actual),'duplicate MIR clobber execution')
    require(not set(actual)-set(expected),'unexpected MIR clobber execution')
    require(not set(expected)-set(actual),'missing MIR clobber execution')


def write_json(path,data):
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    fd,temp=tempfile.mkstemp(prefix=path.name+'.',dir=str(path.parent))
    try:
        with os.fdopen(fd,'w') as f:json.dump(data,f,indent=2);f.write('\n')
        os.replace(temp,str(path))
    finally:
        if os.path.exists(temp):os.unlink(temp)


def configuration(parameters):
    result=('stack' if parameters['StackCheck'] else 'nostack')+'-'+('peep' if parameters['Peep'] else 'nopeep')
    if parameters.get('DebugMode'):result+='-debug-'+parameters['DebugMode']
    return result


def add_execution(plan,command,parameters,environment):
    name=parameters['Name']
    key=name+'|'+configuration(parameters)
    require(key not in plan,'duplicate MIR clobber inventory key: '+key)
    plan[key]={'Key':key,'Command':command,'Parameters':copy.deepcopy(parameters),'Environment':dict(environment)}


def plan(cases=(), seeds=(23117, 1, 65535), environment=None):
    for case in cases:
        require(case.casefold() == 'fuzz', 'Unknown MIR clobber case: ' + case)
    source_env = os.environ if environment is None else environment
    env = {name: source_env.get(name) for name in ENVIRONMENT_NAMES}
    env.update(DCC_MIR_MACHINE_MUTATE=None, DCC_MIR_MACHINE_MUTATE_FUNCTION=None,
               DCC_MIR_REQUIRE_COMPLETE='1', DCC_MIR_REQUIRE_EMIT='1',
               DCC_MIR_MACHINE_REPORT='1', DCC_MIR_SELECT_REPORT='1',
               DCC_MIR_CACHE_VERIFY='1')
    functions = [(seed + (seed >> 8)) % 12 for seed in seeds]
    require({number % 2 for number in functions} == {0, 1},
            'Forced fuzz functions must cover both 8-bit and 16-bit data')
    result = {}
    for seed, number in zip(seeds, functions):
        function = 'fuzz' + str(number)
        for stack in (True, False):
            for peep in (True, False):
                common = dict(Sources=['@work/fz{}.c'.format(seed)], Defines=[],
                              Expected=['MIR fuzz seed={} checks=96 failures=0'.format(seed)],
                              ExpectedExit=0, StackCheck=stack, Peep=peep)
                for debug in ('', 'true', 'lines'):
                    normal = dict(common, Name='fuzz-{}'.format(seed), DebugMode=debug,
                                  RunArguments=None)
                    add_execution(result, 'Assert-RunCase', normal, env)
                mutant = dict(common, Name='fuzz-mutant-{}'.format(seed), DebugMode='',
                              RunArguments=None, Defines=['FUZZ_MUTATE=1'], ExpectedExit=1,
                              Expected=['MIR fuzz seed={} checks=96 failures=96'.format(seed)])
                add_execution(result, 'Assert-RunCase', mutant, env)
                for candidate in ('spilled-baseline', 'spilled-all'):
                    parameters = dict(common, Name='fuzzforced-{}-{}'.format(seed, candidate),
                                      DebugMode='', RequiredCandidate=candidate,
                                      RequiredSelector='spilled-scalar-cfg',
                                      RequiredSelectorFunction=function,
                                      RequiredGenericFunction=function)
                    selected_env = dict(env, DCC_MIR_SELECT_FUNCTION=function,
                                        DCC_MIR_SELECT_CANDIDATE=candidate)
                    add_execution(result, 'Assert-RunCase', parameters, selected_env)
    return result


def target_result(result,name,config,expected_exit,expected):
    require(not result['TimedOut'] and result['ExitCode']==expected_exit,
            '{} exited {}, expected {} ({}), timedOut={}:\n{}'.format(name,result['ExitCode'],expected_exit,config,result['TimedOut'],result['Output']))
    for text in expected:require(text in result['Output'],name+" did not emit '"+text+"' ("+config+"):\n"+result['Output'])


def execute(argv,cwd,environment,timeout,log=None):
    child=Process(list(map(str,argv)),cwd,environment=environment,remove_environment=list(os.environ),
                  parent_scope=environment.get('DCC_PROCESS_SCOPE'),drain_timeout=1,log=log)
    try:return child.complete(timeout)
    finally:child.stop()


def local_tool(name,root=ROOT):
    return str(root/(name+('.exe' if os.name=='nt' else '')))


def expand_path(value,work,root=ROOT):
    return str(work/value[6:]) if value.startswith('@work/') else str(root/value)


class Runner:
    def __init__(self,args,work,base=None,root=ROOT):
        self.args=args;self.work=Path(work);self.root=Path(root)
        self.base=dict(os.environ if base is None else base)
        self.make=local_tool('dccmake',self.root)
        self.emulator=args.emulator

    def run(self, entry):
        p = entry['Parameters']
        name = p['Name']
        config = configuration(p)
        env = controlled(entry['Environment'], base=self.base)
        directory = self.work / (name + '-' + config)
        directory.mkdir(parents=True, exist_ok=True)
        output = re.sub('[^A-Za-z0-9]', '', name).upper()[:8]
        sources = ','.join(expand_path(source, self.work, self.root) for source in p['Sources'])
        argv = [self.make, 'dcc-input=' + sources, 'dcc-output=' + output,
                'dcc-build-dir=' + str(directory), 'dcc-peep=' + str(p['Peep']),
                'dcc-stack-check=' + str(p['StackCheck']), 'dcc-stack-bytes=512']
        if p.get('DebugMode'):
            argv.append('dcc-debug=' + p['DebugMode'])
        argv.extend('dcc-define=' + define for define in p.get('Defines', []))
        candidate = p.get('RequiredCandidate')
        function = p.get('RequiredSelectorFunction')
        values = dict(DCC_MIR_MACHINE_REPORT=None, DCC_MIR_MACHINE_FUNCTION=None,
                      DCC_MIR_MACHINE_TEMPLATE=None, DCC_MIR_REPORT=None,
                      DCC_MIR_FUNCTION=None, DCC_MIR_GENERAL_FUNCTION=None,
                      DCC_MIR_EMIT_FUNCTION=None, DCC_MIR_COST_REPORT='1' if candidate else None,
                      DCC_MIR_SELECT_REPORT='1' if function else None,
                      DCC_MIR_SELECT_REPORT_FUNCTION=function)
        if not candidate:
            values.update(DCC_MIR_SELECT_FUNCTION=None, DCC_MIR_SELECT_CANDIDATE=None)
        build = execute(argv, self.root, controlled(values, base=env),
                        self.args.build_timeout, directory / 'build.log')
        require(not build['TimedOut'],
                '{} build timed out after {}s ({}):\n{}'.format(
                    name, self.args.build_timeout, config, build['Output']))
        require(build['ExitCode'] == 0,
                '{} failed to build ({}):\n{}'.format(name, config, build['Output']))
        assembly = directory / (output + '.MAC')
        require(assembly.is_file(), '{} build did not create {} ({})'.format(name, assembly, config))
        if candidate:
            self.evidence(p, build['Output'])
        run = execute([self.emulator, '-p', '-s:0', output + '.COM'], directory,
                      env, self.args.run_timeout, directory / 'run.log')
        target_result(run, name, config, p['ExpectedExit'], p['Expected'])

    @staticmethod
    def evidence(parameters, output):
        function = re.escape(parameters['RequiredSelectorFunction'])
        selector = re.escape(parameters['RequiredSelector'])
        candidate = re.escape(parameters['RequiredCandidate'])
        checks = (
            ('MIR selection function=' + function + ' selector=' + selector + ' result=mir',
             'did not use the required selector'),
            ('MIR cost-selected function=' + function + ' candidate=' + candidate +
             ' selector=' + selector + ' ', 'did not select the required candidate'),
        )
        for pattern, message in checks:
            require(re.search(pattern, output, re.I),
                    '{} {}:\n{}'.format(parameters['Name'], message, output))


def prepare_sources(entries,work,seeds):
    sources={s for e in entries for s in e['Parameters'].get('Sources',[])}
    spec=importlib.util.spec_from_file_location('clobber_fuzz',Path(__file__).with_name('new-mir-fuzz-source.py'))
    generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
    for seed in seeds:
        name='fz'+str(seed)+'.c'
        if '@work/'+name in sources:(work/name).write_text(generator.generate(seed),encoding='ascii')


def run_shards(args,keys,work,base,script=None,timeout=None):
    children=[];actual=[]
    script=script or Path(__file__).with_name('run-mir-clobber-tests.py')
    try:
        for index in range(args.jobs):
            manifest=work/('shard-'+str(index)+'.json')
            argv=[sys.executable,str(script),'--run-timeout',str(args.run_timeout),'--build-timeout',str(args.build_timeout),
                  '--emulator',args.emulator,'--shard-index',str(index),'--shard-count',str(args.jobs),'--execution-manifest',str(manifest),'--work-parent',str(work)]
            if args.cases:argv+=['--cases',','.join(args.cases)]
            argv+=['--fuzz-seeds']+list(map(str,args.fuzz_seeds))
            env=dict(base)
            if env.get('LLVM_PROFILE_FILE'):
                profile=Path(env['LLVM_PROFILE_FILE'])
                env['LLVM_PROFILE_FILE']=str(profile.with_name(profile.stem+'-clobber-{}-{}'.format(os.getpid(),index)+profile.suffix))
            child=Process(argv,ROOT,log=work/('shard-'+str(index)+'.log'),environment=env,
                          remove_environment=list(os.environ),parent_scope=env.get('DCC_PROCESS_SCOPE'),drain_timeout=1)
            budget=timeout if timeout is not None else 1200+len(shard(keys,index,args.jobs))*(args.build_timeout+args.run_timeout)
            children.append((index,child,manifest,time.monotonic()+budget))
        pending=list(children)
        while pending:
            for item in list(pending):
                index,child,manifest,deadline=item
                if not child.ready() and time.monotonic()<deadline:continue
                result=child.complete(max(0,deadline-time.monotonic()))
                require(not result['TimedOut'],'MIR clobber shard {} timed out (exit or output drain):\n{}'.format(index,result['Output']))
                require(result['ExitCode']==0,'MIR clobber shard {} failed:\n{}'.format(index,result['Output']))
                require(manifest.is_file(),'Missing MIR clobber shard manifest: '+str(manifest))
                values=json.loads(manifest.read_text(encoding='utf-8-sig'))
                assert_manifest(shard(keys,index,args.jobs),values);actual.extend(values);pending.remove(item)
            if pending:time.sleep(.01)
        assert_manifest(keys,actual)
        return actual
    finally:
        for _,child,_,_ in children:child.stop()


def bounded(low,high):
    def value(text):
        number=int(text)
        if not low<=number<=high:raise argparse.ArgumentTypeError('must be {}..{}'.format(low,high))
        return number
    return value


def parse_args(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run-timeout','--timeout','-RunTimeout',type=bounded(1,2147483647),default=30)
    p.add_argument('--build-timeout','-BuildTimeout',type=bounded(1,3600),default=60)
    p.add_argument('--emulator','-Emulator',default='ntvcm')
    p.add_argument('--cases','-Cases',nargs='+',default=[])
    p.add_argument('--fuzz-seeds','-FuzzSeeds',nargs='+',type=bounded(0,65535),default=[23117,1,65535])
    p.add_argument('--execution-manifest','-ExecutionManifest')
    p.add_argument('--list-executions','-ListExecutions')
    p.add_argument('--jobs','-Jobs',type=bounded(1,256),default=1)
    p.add_argument('--work-parent',help=argparse.SUPPRESS)
    p.add_argument('--shard-index','-ShardIndex',type=bounded(0,2147483647),default=0)
    p.add_argument('--shard-count','-ShardCount',type=bounded(1,2147483647),default=1)
    args=p.parse_args(argv)
    args.cases=[v.strip() for item in args.cases for v in item.split(',') if v.strip()]
    if args.shard_index>=args.shard_count:p.error('ShardIndex must be less than ShardCount')
    if args.jobs>1 and (args.shard_index!=0 or args.shard_count!=1):p.error('Jobs cannot be combined with an explicit shard')
    if len(set(args.fuzz_seeds))!=len(args.fuzz_seeds):p.error('FuzzSeeds must be distinct')
    return args


def main(argv=None):
    args=parse_args(argv);work=None;failed=True
    try:
        inventory=plan(args.cases,args.fuzz_seeds)
        keys=shard(inventory,args.shard_index,args.shard_count)
        if args.list_executions:
            write_json(ROOT/args.list_executions,keys)
            print('Listed {} MIR clobber target configurations'.format(len(keys)))
            return 0
        emulator=shutil.which(args.emulator)
        require(emulator,'Emulator not found: '+args.emulator);args.emulator=emulator
        build=Path(args.work_parent).resolve() if args.work_parent else ROOT/'build';build.mkdir(exist_ok=True)
        work=Path(tempfile.mkdtemp(prefix='mir-clobber-tests-',dir=str(build)))
        if args.jobs>1:actual=run_shards(args,keys,work,dict(os.environ))
        else:
            runner=Runner(args,work)
            prepare_sources([inventory[key] for key in keys],work,args.fuzz_seeds)
            actual=[]
            for key in keys:
                runner.run(inventory[key]);actual.append(key)
            assert_manifest(keys,actual)
        if args.execution_manifest:write_json(ROOT/args.execution_manifest,sorted(actual))
        print('Generated MIR fuzz tests passed {} target configurations'.format(len(actual)))
        failed=False
        return 0
    except (OSError,RuntimeError,ValueError) as error:
        print(str(error),file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print('MIR clobber interrupted',file=sys.stderr)
        return 130
    finally:
        if work:
            if failed:print('Failure source and build artifacts retained: '+str(work),file=sys.stderr)
            else:shutil.rmtree(str(work))
