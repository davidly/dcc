"""Shared native compiler proof probes, using Python 3.7+ only."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
try:
    from .process_supervision import Process
except ImportError:
    from process_supervision import Process

ROOT=Path(__file__).resolve().parents[1]


def controlled(values=None,remove=(),base=None):
    env=dict(os.environ if base is None else base)
    names=set(remove)|set(values or {})
    for key in list(env):
        if key.upper() in {name.upper() for name in names}:env.pop(key,None)
    env.update({key:str(value) for key,value in (values or {}).items() if value is not None})
    return env


def compiler(explicit=None):
    if explicit:
        return str(Path(explicit).resolve()) if '/' in explicit or '\\' in explicit else explicit
    return str(ROOT/('dcc.exe' if os.name=='nt' else 'dcc'))


def execute(argv,env=None,timeout=60):
    env=controlled(base=env) if env is not None else dict(os.environ)
    child=Process(list(map(str,argv)),ROOT,environment=env,remove_environment=list(os.environ),
                  parent_scope=env.get('DCC_PROCESS_SCOPE'))
    try:return child.complete(timeout)
    finally:child.stop()


def compile_file(dcc,source,output,env=None,options=(),compile_only=True,include_root=True):
    output=Path(output)
    if output.exists():output.unlink()
    flags=(['-I',str(ROOT)] if include_root else [])+(['-c'] if compile_only else [])
    result=execute([dcc]+list(options)+flags+[str(source),'-o',str(output)],env)
    result['Assembly']=output.read_text() if output.is_file() else ''
    result['Artifact']=output.is_file()
    return result


def require(condition,message):
    if not condition:raise RuntimeError(message)


def success(result,name):
    require(not result['TimedOut'] and result['ExitCode']==0 and result['Artifact'],
            name+' compile failed:\n'+result['Output'])


def workspace():
    build=ROOT/'build';build.mkdir(exist_ok=True)
    return tempfile.TemporaryDirectory(prefix='mir-probe-',dir=str(build))


def ast_dump(dcc):
    names=('DCC_AST_DUMP','DCC_AST_BUILD','DCC_MIR_CANDIDATES','DCC_MIR_GENERAL_CANDIDATES')
    source='''int twice(int value)
{
    return value * 2;
}

int main(void)
{
    int value = 3;
    if (value)
        value = twice(value);
    else
        value = 0;
    return value;
}
'''
    with workspace() as folder:
        folder=Path(folder);path=folder/'tadump.c';path.write_text(source,encoding='ascii')
        env=controlled(remove=names)
        normal=compile_file(dcc,path,folder/'NORMAL.MAC',env);success(normal,'normal AST control')
        retired=compile_file(dcc,path,folder/'RETIRED.MAC',controlled(
            {'DCC_AST_BUILD':'2','DCC_MIR_CANDIDATES':'1','DCC_MIR_GENERAL_CANDIDATES':'1'},base=env))
        success(retired,'retired AST control')
        require((retired['Assembly'],retired['Output'])==(normal['Assembly'],normal['Output']),
                'Retired rollout controls changed compiler output')
        dump=compile_file(dcc,path,folder/'TADUMP.MAC',controlled({'DCC_AST_DUMP':'1'},base=env));success(dump,'AST dump')
        require(dump['Assembly']==normal['Assembly'],'AST dump changed generated assembly')
        for kind in ('if','assign','call','return','binary','ident','int'):
            require(re.search(r'^\s*'+kind+r'\b',dump['Output'],re.M) is not None,'AST dump omitted '+kind)
        require(re.search(r'^\s*<null>\r?$',dump['Output'],re.M) is not None,'AST dump omitted null-child markers')
    print('AST diagnostic dump and retired-control isolation passed')


CANDIDATES=('baseline','rhs-forward','store-address','wide-binary-lhs','stable-pointer-argument',
    'global-argument','stack-argument','promoted-local-slot','all','phi-slot','boolean-phi-branch','boolean-phi-branch-no-prepack')
MATRIX_PROBES=(('shadow','tfpshad.c','invoke',False),('time','ttime.c','main',False),
    ('switch','tswitch.c','main',False),('postidx','tpostidx.c','main',False),('ptrarr','tptrarr.c','main',False),
    ('widen','tlongopt.c','test_widen_mul_edges',True),('mulmod','tm1mu.c','mulmod',True),('ldiv','tstdlib.c','check_ldiv',False))


def candidate_matrix(dcc):
    with workspace() as folder:
        folder=Path(folder)
        for name,source,function,widen in MATRIX_PROBES:
            env=controlled({'DCC_MIR_REQUIRE_COMPLETE':'1','DCC_MIR_REQUIRE_EMIT':'1',
                'DCC_MIR_CANDIDATE_MATRIX':None,'DCC_MIR_SPILLED_POLICY':None,
                'DCC_MIR_WIDEN_CACHE_VERIFY':'1' if widen else None})
            normal=compile_file(dcc,ROOT/'tests'/source,folder/(name+'-control.MAC'),env);success(normal,name+' matrix control')
            diagnostic=compile_file(dcc,ROOT/'tests'/source,folder/(name+'-matrix.MAC'),controlled(
                {'DCC_MIR_CANDIDATE_MATRIX':'1','DCC_MIR_SPILLED_POLICY':'cost-v1'},base=env));success(diagnostic,name+' matrix diagnostic')
            require(normal['Assembly']==diagnostic['Assembly'],name+' candidate-matrix diagnostics changed compiler output')
            for candidate in CANDIDATES:
                pattern=r'^; MIR candidate-matrix\s+function='+re.escape(function)+r'\s+candidate='+re.escape(candidate)+r'\s+'
                require(len(re.findall(pattern,diagnostic['Output'],re.M))==1,name+' matrix row missing or duplicated: '+candidate)
            pattern=r'^; MIR candidate-matrix-selected\s+function='+re.escape(function)+r'\s+candidate=\S+\s+score='
            require(len(re.findall(pattern,diagnostic['Output'],re.M))==1,name+' matrix selected row missing or duplicated')
    print('MIR candidate-matrix isolation passed for {} probes'.format(len(MATRIX_PROBES)))


def pointer_variants(source):
    start=source.find('int main()');require(start>=0,'Pointer-condition mutation anchor changed: main')
    main=source[start:];inactive=[]
    for macro in ('PTRW25_POINTER_TRUTHINESS','PTRW63_ALT_PICKW','PTRW63_LOOP_FUNCTION_ALIAS'):
        a=main.find('#ifdef '+macro);b=main.find('#else',a) if a>=0 else -1
        require(a>=0 and b>=0,'Pointer-condition mutation anchor changed: inactive '+macro)
        inactive.append((a,b))
    matches=list(re.finditer(r'(?P<operator>==|!=|<=|>=|<|>)\s*(?P<number>\d+)(?P<suffix>[LUlu]*)',main))
    require(len(matches)==85,'Pointer-condition mutation inventory changed: '+str(len(matches)))
    for index,match in enumerate(matches):
        a,b=match.span('number')
        # Preserve the original campaign's exact spelling, including suffix replay.
        replacement=str(int(match.group('number'))+1)+match.group('suffix')
        yield index,source[:start]+main[:a]+replacement+main[b:],any(x<a<y for x,y in inactive)


def pointer_conditions(dcc):
    source=(ROOT/'tests/tptrcnd.c').read_text()
    env=controlled({n:'1' for n in ('DCC_MIR_MACHINE_REPORT','DCC_MIR_SELECT_REPORT','DCC_MIR_REQUIRE_COMPLETE','DCC_MIR_REQUIRE_EMIT')})
    with workspace() as folder:
        folder=Path(folder);path=folder/'TPMUT.C';out=folder/'TPMUT.MAC'
        path.write_text(source,encoding='ascii');control=compile_file(dcc,path,out,env);success(control,'Pointer-condition exact control')
        require('MIR selection function=main selector=scheduled-machine-cfg' in control['Output'],
                'Pointer-condition exact control did not select')
        count=0
        for index,text,inactive in pointer_variants(source):
            path.write_text(text,encoding='ascii');result=compile_file(dcc,path,out,env);success(result,'Pointer-condition mutation '+str(index))
            scheduled='MIR selection function=main selector=scheduled-machine-cfg' in result['Output']
            if inactive:
                require(scheduled and result['Assembly']==control['Assembly'],
                        'Pointer-condition inactive mutation {} changed output'.format(index))
            else:
                require('MIR machine function=main template=pointer-condition-main reject=' in result['Output'] and not scheduled and
                    re.search(r'MIR selection function=main selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir',result['Output']) is not None,
                    'Pointer-condition mutation {} escaped exact rejection'.format(index))
            count+=1
    print('Pointer-condition matcher validated {} source mutations'.format(count))


SCOPE_DIAGNOSTICS=('6:type:4','6:immediate:11','7:type:4','7:src1:11','7:memory_size:4','7:identity:88',
    '12:type:2','43:src1:31','43:src2:29','43:immediate:45','153:src1:83','153:identity:88',
    '159:src1:79','223:type:4','223:identity:88','246:type:4','635:type:2','675:immediate:29','697:src1:419')
SCOPE_SOURCES=(('operator','{ int a = 3; s += a; }','{ int a = 3; s -= a; }'),
    ('control-flow','while (w < 2)','while (w <= 2)'),('local-width','long n = 100000L;','int n = 10000;'),
    ('volatile-local','int x = 20;\n            chk(x, 20L','volatile int x = 20;\n            chk(x, 20L'),
    ('alias-dataflow','sum += outer;               /* 0 + 1 */','sum += w;                   /* changed dataflow */'),
    ('call-argument','chk(x, 20L, "inner x");','chk(20L, x, "inner x");'),
    ('call-prototype','static void chk(long got, long want, char *name)','static void chk(unsigned long got, long want, char *name)'),
    ('return-flow','return fails != 0;','return fails == 0;'))


def replace_once(text,before,after,name):
    require(text.count(before)==1,name+' mutation anchor is not unique')
    return text.replace(before,after)


def scope_blocks(dcc):
    source=(ROOT/'tests/tforblk.c').read_text()
    env=controlled({n:'1' for n in ('DCC_MIR_MACHINE_REPORT','DCC_MIR_SELECT_REPORT','DCC_MIR_REQUIRE_COMPLETE','DCC_MIR_REQUIRE_EMIT')})
    with workspace() as folder:
        folder=Path(folder)
        def compile(name,text=source,options=(),mutation=None):
            path=folder/(name+'.c');path.write_text(text,encoding='ascii')
            return compile_file(dcc,path,folder/(name+'.MAC'),controlled({'DCC_MIR_MACHINE_MUTATE':mutation,
                'DCC_MIR_MACHINE_MUTATE_FUNCTION':'main' if mutation else None},base=env),options)
        def exact(name,result):
            success(result,name)
            require('MIR machine function=main template=scope-block-runner accept=emitted' in result['Output'] and
                'MIR selection function=main selector=scheduled-machine-cfg' in result['Output'],name+' did not select scope-block exact schedule')
        def generic(name,result):
            success(result,name)
            require('MIR machine function=main template=scope-block-runner reject=' in result['Output'] and
                'MIR machine function=main template=scope-block-runner accept=emitted' not in result['Output'] and
                re.search(r'MIR selection function=main selector=(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir',result['Output']) is not None,
                name+' did not reject into a generic MIR emitter')
        for name,options in [('normal',()),('stack',('-fstack-check',)),('line-debug',('-gline',))]:exact(name,compile(name,options=options))
        debug=compile('debug',options=('-g',));success(debug,'full debug')
        require('MIR machine function=main template=scope-block-runner accept=emitted' not in debug['Output'] and
            'MIR selection function=main selector=spilled-scalar-cfg result=mir' in debug['Output'],
            'Full-debug control did not use generic emitter')
        for mutation in SCOPE_DIAGNOSTICS:generic(mutation,compile('mir-'+mutation.replace(':','-'),mutation=mutation))
        for name,before,after in SCOPE_SOURCES:generic(name,compile('source-'+name,replace_once(source,before,after,name)))
        exact('string spelling',compile('string-spelling',replace_once(source,'"inner x"','"inner value"','string spelling')))
    print('Scope-block exact matcher: 4 controls passed, {} semantic mutations rejected'.format(len(SCOPE_DIAGNOSTICS)+len(SCOPE_SOURCES)))


def require_emit(dcc,build_dir='build/mir-require-emit-test'):
    build=(ROOT/Path(build_dir)).resolve();build.mkdir(parents=True,exist_ok=True)
    def run(name,arguments,values):
        result=execute([dcc]+list(map(str,arguments)),controlled(values))
        return result
    def pass_compile(name,arguments,values):
        result=run(name,arguments,values)
        require(not result['TimedOut'] and result['ExitCode']==0,name+' failed unexpectedly:\n'+result['Output'])
        print('PASS '+name)
        return result
    for name,options,filename in [('all-MIR compile',[],'NORMAL.MAC'),('all-MIR stack-check compile',['-fstack-check'],'STACK.MAC')]:
        pass_compile(name,options+['-stack','512','-I','.','tests/tmircfg.c','-o',build/filename],{'DCC_MIR_REQUIRE_EMIT':'1'})
    def diagnostic(result,source,baseline):
        text=result['Output'].replace('\r\n','\n').replace('\r','\n')
        text=text.replace(str((ROOT/source).resolve()),'<source>').replace(str(source),'<source>')
        expected=(ROOT/baseline).read_text().replace('\r\n','\n').replace('\r','\n')
        require(text.rstrip('\n')==expected.rstrip('\n'),'Strict diagnostic changed for '+source+'\n'+text)
    invalid='tests/diagnostics/ast-local-init-unsupported-member.c'
    result=run('invalid initializer',[invalid,'-o',build/'INVALID.MAC'],{'DCC_MIR_REQUIRE_COMPLETE':'1','DCC_MIR_REQUIRE_EMIT':'1'})
    require(not result['TimedOut'] and result['ExitCode']!=0,'invalid initializer strict diagnostic succeeded unexpectedly')
    diagnostic(result,invalid,'tests/diagnostics/baselines/ast-local-init-unsupported-member.txt')
    print('PASS invalid initializer preserves DCC-E1002')
    source='tests/diagnostics/warn-nonmain-fallthrough.c';out=build/'FALLTHROUGH.MAC'
    if out.exists():out.unlink()
    result=pass_compile('non-main fallthrough emits MIR',[source,'-o',out],{'DCC_MIR_REQUIRE_EMIT':'1'})
    diagnostic(result,source,'tests/diagnostics/baselines/warn-nonmain-fallthrough.txt')
    assembly=out.read_text();a=assembly.find('_helper:');b=assembly.find('_main:',a+1)
    require(a>=0 and b>=0,'non-main fallthrough assembly is missing helper/main labels')
    require('\tld hl,0' not in assembly[a:b],'non-main fallthrough incorrectly forces HL=0')
    print('PASS non-main fallthrough leaves return value undefined')
    source=build/'oversized.c'
    source.write_text('int oversized(int x) {\n'+('    x = x + 1;\n'*3000)+
        '    return x;\n}\nint main(void) { return oversized(0) != 3000; }\n',encoding='ascii')
    result=run('oversized',['-g','-stack','512','-I','.',source,'-o',build/'OVERSIZED-REQUIRED.MAC'],{'DCC_MIR_REQUIRE_EMIT':'1'})
    require(not result['TimedOut'] and result['ExitCode']!=0 and
        'MIR emission failed for function oversized: no generated candidate (reason=oversized)' in result['Output'],
        'oversized MIR boundary did not fail for the required reason:\n'+result['Output'])
    print('PASS oversized MIR boundary\nMIR require-emit diagnostics passed.')


def physical_lifetime(dcc,timeout=30):
    env=controlled({'DCC_MIR_REQUIRE_COMPLETE':'1','DCC_MIR_REQUIRE_EMIT':'1'})
    with workspace() as folder:
        folder=Path(folder)
        for no_stack in (False,True):
            config='nostack' if no_stack else 'stack'
            options=([] if no_stack else ['-fstack-check'])+['-stack','512']
            probe=compile_file(dcc,ROOT/'tests/tmirlife.c',folder/('reject-'+config+'.MAC'),controlled({
                'DCC_MIR_SELECT_FUNCTION':None,'DCC_MIR_SELECT_CANDIDATE':None,'DCC_MIR_MACHINE_REPORT':'1'},base=env),options,compile_only=False,include_root=False)
            success(probe,'tmirlife.parse_signed_byte')
            require('function=parse_signed_byte template=bounded-decimal-parse-schedule reject=argument-conversion' in probe['Output'],
                'tmirlife.parse_signed_byte did not reject bounded-decimal-parse-schedule for argument-conversion')
        for app,function,candidate,stack in [('tmirlife','regional_address','regional',512),('cint','primary','spilled-phi-slot',768)]:
            forced=controlled({'DCC_MIR_SELECT_FUNCTION':function,'DCC_MIR_SELECT_CANDIDATE':candidate,'DCC_MIR_COST_REPORT':'1',
                              'DCC_MIR_MACHINE_REPORT':None},base=env)
            for no_stack in (False,True):
                config='nostack' if no_stack else 'stack'
                options=([] if no_stack else ['-fstack-check'])+['-stack',str(stack)]
                probe=compile_file(dcc,ROOT/'tests'/(app+'.c'),folder/(app+'-'+config+'.MAC'),forced,options,compile_only=False,include_root=False)
                success(probe,app+'.'+function)
                require('MIR cost-selected function='+function+' candidate='+candidate+' ' in probe['Output'],
                    app+'.'+function+' did not select forced candidate '+candidate)
                command=[sys.executable,str(ROOT/'scripts/runall.py'),'--apps',app,'--mode','full','--timeout',str(timeout),
                    '--build-dir',str(folder/(app+'-'+candidate+'-'+config)),'--no-ram-disk','--no-perf-check']
                if no_stack:command.append('--no-stack-check')
                result=execute(command,controlled({'DCC_MIR_COST_REPORT':None},base=forced),timeout=float('inf'))
                print(result['Output'],end='',flush=True)
                require(not result['TimedOut'] and result['ExitCode']==0,app+'.'+function+' failed with '+candidate+' ('+config+')')
    print('MIR physical-lifetime forced tests passed')


PROBES={'ast-dump':ast_dump,'candidate-matrix':candidate_matrix,'pointer-condition-mutations':pointer_conditions,
        'scope-block-mutations':scope_blocks,'require-emit':require_emit,'lifetime':physical_lifetime}


def main(kind,argv=None):
    p=argparse.ArgumentParser(description='Run '+kind+' compiler proof with Python 3.7+.')
    p.add_argument('--dcc','-Dcc')
    if kind=='require-emit':p.add_argument('--build-dir','-BuildDir',default='build/mir-require-emit-test')
    if kind=='lifetime':p.add_argument('--timeout','-RunTimeout',type=int,default=30)
    args=p.parse_args(argv)
    kwargs={}
    if kind=='require-emit':kwargs['build_dir']=args.build_dir
    if kind=='lifetime':
        if args.timeout<1:p.error('--timeout must be positive')
        kwargs['timeout']=args.timeout
    try:PROBES[kind](compiler(args.dcc),**kwargs)
    except (OSError,RuntimeError) as error:
        print('ERROR: '+str(error),file=sys.stderr);return 1
    return 0
