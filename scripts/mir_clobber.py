"""MIR emission-clobber inventory, assertions and isolated workers (Python 3.7+)."""
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
    from .mir_probes import controlled
except ImportError:
    from process_supervision import Process
    from mir_probes import controlled

ROOT=Path(__file__).resolve().parents[1]
DATA=Path(__file__).with_name('mir-clobber-builtins.json')
ENVIRONMENT_NAMES=tuple('DCC_MIR_'+n for n in (
    'COST_REPORT','CACHE_VERIFY','EMIT_FUNCTION','MACHINE_REPORT','MACHINE_FUNCTION',
    'MACHINE_TEMPLATE','MACHINE_MUTATE','MACHINE_MUTATE_FUNCTION','REPORT','FUNCTION',
    'REQUIRE_COMPLETE','REQUIRE_EMIT','PREFER_REGIONAL','SELECT_CANDIDATE','SELECT_FUNCTION',
    'SELECT_REPORT_FUNCTION','SELECT_REPORT'))
SPECIAL_GROUPS=('allocmut','attentionmut','catalanmut','ctypemut','fuzz','inlines',
    'lazywide','minimax','minimaxmut','ndivmut','oldloops','pairedbytes','primemut','vlaend','vlaok')
STRINGS=('Name','Group','ExactTemplate','ExactFunction','RequiredGenericFunction',
    'RequiredSelectorFunction','RequiredSelector','RequiredCandidate','MachineMutation','MachineMutationFunction')
ARRAYS=('Sources','FixturePaths','Defines','Expected','Args','AssemblyPatterns','ForbiddenAssemblyPatterns','DebugModes')
BOOLS=('RequireExact','RequireRejected','OddUpperRuntime')


def require(ok,message):
    if not ok:raise RuntimeError(message)


def integer(value,minimum=0,maximum=2147483647):
    return type(value) is int and minimum<=value<=maximum


def access_expectations(expectations):
    require(isinstance(expectations,list) and expectations,'MirExpectations must be a nonempty array')
    names=set()
    allowed={'Function','Loads','Volatile','Width','ByteVolatile','Opcode','FullDebugLoads','FullDebugWidth'}
    for e in expectations:
        require(isinstance(e,dict) and not set(e)-allowed,'Invalid MIR access expectation')
        name=e.get('Function')
        require(isinstance(name,str) and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*',name) and name not in names,
                'Invalid or duplicate MIR access function')
        names.add(name)
        for key in ('Loads','Volatile','Width','ByteVolatile','FullDebugLoads','FullDebugWidth'):
            require(key in e or key not in ('Loads','Volatile'),'Missing MIR access '+key)
            if key in e:require(integer(e[key],1 if key in ('Width','FullDebugWidth') else 0),'Invalid MIR access '+key)
        require(e['Volatile']<=e['Loads'] and e['Volatile']<=e.get('FullDebugLoads',e['Loads']) and
                e.get('ByteVolatile',0)<=e['Volatile'] and e.get('Opcode','loadind') in ('loadind','storeind'),
                'Inconsistent MIR access expectation')


def access_evidence(output,expectations,debug=''):
    access_expectations(expectations)
    for e in expectations:
        name=re.escape(e['Function'])
        bodies=re.findall(r'^; MIR function='+name+r' [^\r\n]*\r?\n(?:(?!^; MIR function=).)*?^; MIR summary function='+name+r' ',output,re.M|re.S)
        require(len(bodies)==1,'Missing or duplicate MIR access evidence for '+e['Function'])
        body=bodies[0];opcode=re.escape(e.get('Opcode','loadind'))
        loads=len(re.findall(r'\b'+opcode+r'\b',body))
        volatile=len(re.findall(r'\b'+opcode+r'\b[^\r\n]*\bmem=\d+v\b',body))
        expected=e.get('FullDebugLoads',e['Loads']) if debug=='true' else e['Loads']
        width=e.get('FullDebugWidth',e.get('Width')) if debug=='true' else e.get('Width')
        require(loads==expected and volatile==e['Volatile'],e['Function']+' has incorrect MIR access counts/flags:\n'+body)
        if width:require(len(re.findall(r'\b'+opcode+r'\b[^\r\n]*\bmem='+str(width)+r'v?\b',body))==loads,
                         e['Function']+' has incorrect MIR access width:\n'+body)
        if 'ByteVolatile' in e:
            require(len(re.findall(r'\b'+opcode+r'\b[^\r\n]*\bmem=1v\b',body))==e['ByteVolatile'],
                    e['Function']+' has incorrect pointer-level volatility:\n'+body)


def import_cases(directory,root,existing=(),reserved=()):
    names={n.casefold() for n in list(existing)+list(reserved)};definitions=[]
    for path in sorted(Path(directory).glob('*.json')):
        data=json.loads(path.read_text(encoding='utf-8-sig'))
        require(isinstance(data,list),'MIR clobber campaign must be a JSON array: '+str(path))
        for original in data:
            require(isinstance(original,dict),'MIR clobber case must be an object: '+str(path))
            d=copy.deepcopy(original)
            require(not set(d)-set(STRINGS+ARRAYS+BOOLS+('Exit','StackBytes','StackModes','MirExpectations')),
                    'Unknown MIR clobber case property: '+str(path))
            for key in ('Name','Sources','Expected','Exit'):require(key in d,'Missing MIR clobber case property '+key)
            for key in STRINGS:
                if key in d:require(isinstance(d[key],str),'MIR clobber '+key+' must be a string')
            for key in ('Name','Group'):
                if key in d:require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*',d[key]),'Invalid MIR clobber '+key)
            require(d['Name'].casefold() not in names,'Duplicate or reserved MIR clobber case name: '+d['Name'])
            names.add(d['Name'].casefold())
            for key in ARRAYS:
                if key in d:require(isinstance(d[key],list) and all(isinstance(v,str) for v in d[key]),'MIR clobber '+key+' must be an array of strings')
            require(d['Sources'],'MIR clobber Sources must not be empty')
            for key in BOOLS:
                if key in d:require(type(d[key]) is bool,'MIR clobber '+key+' must be a boolean')
            for key in ('Exit','StackBytes'):
                if key in d:require(integer(d[key],1 if key=='StackBytes' else -2147483648),'Invalid MIR clobber '+key)
            if 'StackModes' in d:
                modes=d['StackModes']
                require(isinstance(modes,list) and modes and all(type(v) is bool for v in modes) and len(set(modes))==len(modes),
                        'StackModes must contain distinct booleans')
            if 'DebugModes' in d:
                modes=d['DebugModes']
                require(all(v in ('true','lines') for v in modes) and len(set(modes))==len(modes),
                        "DebugModes must contain distinct 'true' or 'lines' values")
            if 'MirExpectations' in d:access_expectations(d['MirExpectations'])
            require(not (d.get('RequireExact') and d.get('RequireRejected')),'RequireExact and RequireRejected cannot both be true')
            if d.get('RequireExact') or d.get('RequireRejected'):
                require(d.get('ExactTemplate') and d.get('ExactFunction'),'Exact assertions require ExactTemplate and ExactFunction')
            if d.get('RequiredSelector') or d.get('RequiredSelectorFunction') or d.get('RequiredCandidate'):
                require(d.get('RequiredSelector') and d.get('RequiredSelectorFunction'),'Selector assertions require RequiredSelector and RequiredSelectorFunction')
            if d.get('MachineMutation'):require(d.get('MachineMutationFunction'),'MachineMutation requires MachineMutationFunction')
            for key in ('Sources','FixturePaths'):
                for value in d.get(key,[]):
                    candidate=(Path(root)/value).resolve()
                    require(value.strip() and not Path(value).is_absolute() and not re.match(r'^[A-Za-z]:|^\\',value),key+' must use repo-relative paths')
                    require(Path(root).resolve() in candidate.parents and candidate.is_file(),'Invalid repo-relative '+key+' path: '+value)
            definitions.append(d)
    case_names={n.casefold() for n in list(existing)+[d['Name'] for d in definitions]}
    for d in definitions:require(d.get('Group','').casefold() not in case_names,'MIR clobber Group collides with case name')
    return definitions


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
    name=parameters['Name']+('-forced' if command=='Assert-ForcedRegionalSafe' else '')
    key=name+'|'+configuration(parameters)
    require(key not in plan,'duplicate MIR clobber inventory key: '+key)
    plan[key]={'Key':key,'Command':command,'Parameters':copy.deepcopy(parameters),'Environment':dict(environment)}


def plan(cases=(),seeds=(23117,1,65535),environment=None,data=None,campaign_directory=None,root=ROOT):
    data=json.loads(DATA.read_text()) if data is None else data
    definitions=copy.deepcopy(data['Cases']);builtins={d['Name'] for d in definitions}
    campaigns=import_cases(campaign_directory or ROOT/'scripts/mir-clobber-cases',root,builtins,SPECIAL_GROUPS)
    known=builtins|{d['Name'] for d in campaigns}|{d.get('Group','') for d in definitions+campaigns}|set(SPECIAL_GROUPS)
    lookup={n.casefold() for n in known}
    for name in cases:require(name.casefold() in lookup,'Unknown MIR clobber case: '+name)
    chosen={v.casefold() for v in cases}
    selected=lambda names:not chosen or bool(chosen & {v.casefold() for v in names})
    env={n:(os.environ if environment is None else environment).get(n) for n in ENVIRONMENT_NAMES}
    env.update(DCC_MIR_MACHINE_MUTATE=None,DCC_MIR_MACHINE_MUTATE_FUNCTION=None,DCC_MIR_REQUIRE_COMPLETE='1',
               DCC_MIR_REQUIRE_EMIT='1',DCC_MIR_MACHINE_REPORT='1',DCC_MIR_SELECT_REPORT='1')
    if selected(['fuzz']):
        env['DCC_MIR_CACHE_VERIFY']='1'
        forced=[(seed+(seed>>8))%12 for seed in seeds]
        require(any(v%2==0 for v in forced) and any(v%2 for v in forced),
                'Forced fuzz functions must cover both 8-bit and 16-bit data')
        for seed in seeds:
            definitions.extend([
                {'Name':'fuzz-'+str(seed),'Group':'fuzz','Sources':['@work/fz'+str(seed)+'.c'],'Defines':[],
                 'Expected':['MIR fuzz seed={} checks=96 failures=0'.format(seed)],'Exit':0,'DebugModes':['true','lines']},
                {'Name':'fuzz-mutant-'+str(seed),'Group':'fuzz','Sources':['@work/fz'+str(seed)+'.c'],'Defines':['FUZZ_MUTATE=1'],
                 'Expected':['MIR fuzz seed={} checks=96 failures=96'.format(seed)],'Exit':1}])
    definitions+=campaigns;result={}
    for d in definitions:
        if not selected([d['Name'],d.get('Group','')]):continue
        for stack in d.get('StackModes',[True,False]):
            for peep in (True,False):
                p={k:copy.deepcopy(v) for k,v in d.items() if k not in ('Group','Exit','StackModes','DebugModes','Args')}
                p.update(ExpectedExit=d['Exit'],StackCheck=stack,Peep=peep,RunArguments=d.get('Args'))
                for debug in ['']+d.get('DebugModes',[]):
                    parameters=copy.deepcopy(p)
                    if debug and d['Name'] in builtins:
                        keep={'Name','Sources','Defines','Expected','ExpectedExit','StackCheck','Peep','RequiredGenericFunction',
                              'RequiredSelectorFunction','RequiredSelector','RunArguments','FixturePaths'}
                        parameters={k:v for k,v in p.items() if k in keep}
                    parameters['DebugMode']=debug
                    add_execution(result,'Assert-RunCase',parameters,env)
    for special in data['Specials']:
        if not selected(special['Groups']):continue
        variants=seeds if special['Groups']==['fuzz'] else [None]
        for seed in variants:
            text=json.dumps(special)
            if seed is not None:
                text=text.replace('23117',str(seed)).replace('fuzz11','fuzz'+str((seed+(seed>>8))%12))
            item=json.loads(text)
            for stack,peep,debug in item['Configurations']:
                p=dict(item['Parameters'],StackCheck=stack,Peep=peep)
                if item['Command']=='Assert-RunCase':p['DebugMode']=debug
                add_execution(result,item['Command'],p,dict(env,**item['Environment']))
    return result


def rejection_into_generic(output,template,function):
    prefix='MIR machine function='+re.escape(function)+' template='+re.escape(template)+' reject='
    selector='MIR selection function='+re.escape(function)+' selector='
    return bool(re.search(prefix,output,re.I) and not re.search(selector+'scheduled-machine-cfg',output,re.I) and
                re.search(selector+r'(?:homed-scalar-cfg|hybrid-homed-scalar-cfg|regional-homed-scalar-cfg|spilled-scalar-cfg) result=mir',output,re.I))


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
        self.dcc=self.base.get('DCC') or local_tool('dcc',self.root)
        self.make=local_tool('dccmake',self.root)
        self.emulator=args.emulator

    def run(self,entry):
        p=entry['Parameters'];name=p['Name'];config=configuration(p)
        env=controlled(entry['Environment'],base=self.base)
        forced=entry['Command']=='Assert-ForcedRegionalSafe'
        directory=self.work/(name+('-forced' if forced else '')+'-'+config)
        directory.mkdir(parents=True,exist_ok=True)
        output=re.sub('[^A-Za-z0-9]','',name).upper()[:8]
        sources=[p['Source']] if forced else p['Sources']
        argv=[self.make,'dcc-input='+','.join(expand_path(s,self.work,self.root) for s in sources),
              'dcc-output='+output,'dcc-build-dir='+str(directory),'dcc-peep='+str(p['Peep']),
              'dcc-stack-check='+str(p['StackCheck']),'dcc-stack-bytes='+str(p.get('StackBytes',512))]
        if p.get('DebugMode'):argv.append('dcc-debug='+p['DebugMode'])
        if p.get('OddUpperRuntime'):
            runtime=(self.root/'DCCRTL.MAC').read_text();start=runtime.find('        public  __ctu');end=runtime.find('; ---- _isspace',start)
            require(start>=0 and end>=0,name+' could not locate __ctu in DCCRTL.MAC')
            runtime=runtime[:start]+"        public  __ctu\n__ctu:\n        ld      hl,'!'\n        ld      de,0\n        ret\n\n"+runtime[end:]
            path=self.work/(name+'-'+config+'-runtime.MAC');path.write_text(runtime,encoding='ascii');argv.append('dcc-runtime='+str(path))
        argv+=['dcc-define='+d for d in (p.get('Defines') or [])]
        if forced:
            values={'DCC_MIR_SELECT_FUNCTION':None if p.get('PreferRegional') else p['Function'],
                    'DCC_MIR_SELECT_CANDIDATE':None if p.get('PreferRegional') else 'regional',
                    'DCC_MIR_PREFER_REGIONAL':'1' if p.get('PreferRegional') else None,'DCC_MIR_COST_REPORT':'1'}
        else:
            machine=bool(p.get('ExactTemplate'))
            select=machine or any(p.get(k) for k in ('RequiredGenericFunction','RequiredSelectorFunction','RequiredCandidate'))
            values={'DCC_MIR_MACHINE_REPORT':'1' if machine else None,
                    'DCC_MIR_MACHINE_FUNCTION':p.get('ExactFunction') if machine else None,
                    'DCC_MIR_MACHINE_TEMPLATE':p.get('ExactTemplate') if machine else None,
                    'DCC_MIR_SELECT_REPORT':'1' if select else None,
                    'DCC_MIR_SELECT_REPORT_FUNCTION':(p.get('ExactFunction') or p.get('RequiredSelectorFunction')) if select else None,
                    'DCC_MIR_REPORT':'1' if p.get('MirExpectations') else None,'DCC_MIR_FUNCTION':None,
                    'DCC_MIR_GENERAL_FUNCTION':None,'DCC_MIR_COST_REPORT':'1' if p.get('RequiredCandidate') else None}
            if p.get('RequiredSelector')!='specialized':values['DCC_MIR_EMIT_FUNCTION']=None
            if not p.get('RequiredCandidate'):values.update(DCC_MIR_SELECT_FUNCTION=None,DCC_MIR_SELECT_CANDIDATE=None)
            if p.get('MachineMutation'):values.update(DCC_MIR_MACHINE_MUTATE=p['MachineMutation'],DCC_MIR_MACHINE_MUTATE_FUNCTION=p['MachineMutationFunction'])
        build=execute(argv,self.root,controlled(values,base=env),self.args.build_timeout,directory/'build.log')
        require(not build['TimedOut'],'{} build timed out after {}s ({}):\n{}'.format(name,self.args.build_timeout,config,build['Output']))
        if forced:return self.forced(p,build,output,directory,env)
        require(build['ExitCode']==0,name+' failed to build ('+config+'):\n'+build['Output'])
        if p.get('MirExpectations'):access_evidence(build['Output'],p['MirExpectations'],p.get('DebugMode',''))
        assembly_path=directory/(output+'.MAC')
        require(assembly_path.is_file(),name+' build did not create '+str(assembly_path)+' ('+config+')')
        assembly=assembly_path.read_text();self.evidence(p,build['Output'],assembly)
        for value in p.get('FixturePaths',[]):shutil.copyfile(expand_path(value,self.work,self.root),directory/Path(value).name)
        run=execute([self.emulator,'-p','-s:0',output+'.COM']+(p.get('RunArguments') or []),directory,env,self.args.run_timeout,directory/'run.log')
        target_result(run,name,config,p['ExpectedExit'],p['Expected'])

    def evidence(self,p,text,assembly):
        def found(pattern):return re.search(pattern,text,re.I) is not None
        name=p['Name']
        if p.get('RequiredGenericFunction'):
            require(found('MIR selection function='+re.escape(p['RequiredGenericFunction'])+' selector=spilled-scalar-cfg'),name+' did not use the required generic emitter:\n'+text)
        if p.get('RequiredSelectorFunction'):
            require(found('MIR selection function='+re.escape(p['RequiredSelectorFunction'])+' selector='+re.escape(p['RequiredSelector'])+' result=mir'),name+' did not use the required selector:\n'+text)
        if p.get('RequiredCandidate'):
            require(found('MIR cost-selected function='+re.escape(p['RequiredSelectorFunction'])+' candidate='+re.escape(p['RequiredCandidate'])+' selector='+re.escape(p['RequiredSelector'])+' '),name+' did not select the required candidate:\n'+text)
        if p.get('ExactTemplate'):
            function=p.get('ExactFunction','');template=p['ExactTemplate']
            rejected=found(('MIR machine function='+re.escape(function)+' ' if function else '')+'template='+re.escape(template)+' reject=')
            selected=found('MIR selection function='+re.escape(function)+' selector=scheduled-machine-cfg') if function else ';@dcc.mir exact-kernel' in assembly
            accepted=bool(function) and found('MIR machine function='+re.escape(function)+' template='+re.escape(template)+' accept=emitted')
            if p.get('RequireRejected'):require(rejection_into_generic(text,template,function),name+' did not reject exact template into generic code:\n'+text)
            if p.get('RequireExact'):require(selected and accepted,name+' did not select required exact template:\n'+text)
            if not p.get('RequireExact'):require(selected or rejected,name+' neither selected nor explicitly rejected exact template:\n'+text)
        for pattern in p.get('AssemblyPatterns',[]):require(re.search(pattern,assembly,re.I),name+' assembly did not match '+pattern)
        for pattern in p.get('ForbiddenAssemblyPatterns',[]):require(not re.search(pattern,assembly,re.I),name+' assembly unexpectedly matched '+pattern)

    def forced(self,p,build,output,directory,env):
        text=build['Output'];name=p['Name'];function=p['Function']
        if p.get('RequireReject'):
            require(build['ExitCode']!=0,name+' forced regional unexpectedly accepted')
            require('MIR regional candidate is not validated for function '+function in text and
                    'DCC_MIR_SELECT_CANDIDATE rejected unsafe regional stream' in text,
                    name+' forced regional failed without the exact function rejection:\n'+text)
            return
        if build['ExitCode']!=0:
            require(not p.get('PreferRegional') and re.search(r'regional.*(invalid|reject|safe|valid)',text,re.I),
                    name+' forced regional failed to build:\n'+text)
            return
        require('MIR cost-selected function='+function+' candidate=regional selector=regional-homed-scalar-cfg' in text,
                name+' did not select the regional candidate:\n'+text)
        result=execute([self.emulator,'-p','-s:0',output+'.COM'],directory,env,self.args.run_timeout,directory/'run.log')
        target_result(result,name,configuration(p),0,[p['Expected']])


def qualifier_source():
    seeds=(0,1,127,255,256,32767,32768,65535)
    variants=('plain','cast','typedef','return','conditional','roundtrip')
    lines=['#include <stdio.h>'];names=[];expected=[]
    for width in (8,16):
        element='unsigned char' if width==8 else 'unsigned int'
        lines+=['typedef volatile {0} *Q{1};'.format(element,width),
                'volatile {0} *r{1}({0} *pointer) {{ return pointer; }}'.format(element,width)]
        for variant in variants:
            name='q'+str(len(names));names.append(name)
            expression={'plain':'plain','cast':'((volatile '+element+' *)plain)',
                'typedef':'((Q'+str(width)+')plain)','return':'r'+str(width)+'(plain)',
                'conditional':'(flag ? plain : observed)','roundtrip':'(('+element+' *)(volatile '+element+' *)plain)'}[variant]
            lines+=['unsigned int '+name+'(unsigned int seed, int index, int flag) {',
                    element+' data[4], other[4]; int slot;',
                    element+' *plain = data; volatile '+element+' *observed = other;',
                    'for (slot = 0; slot < 4; ++slot) { data[slot] = ('+element+')(seed + (unsigned int)slot * 257U); other[slot] = ('+element+')(seed + (unsigned int)slot * 257U + 19U); }',
                    'return (unsigned int)((unsigned int)'+expression+'[index] + seed) ^ (unsigned int)((unsigned int)'+expression+'[index + 1] * 257U); }']
    for seed in seeds:
        for index in range(3):
            for flag in (0,1):
                for width in (8,16):
                    for variant in variants:
                        bias=19 if variant=='conditional' and flag==0 else 0;mask=(1<<width)-1
                        left=(seed+index*257+bias)&mask;right=(seed+(index+1)*257+bias)&mask
                        expected.append(((left+seed)&65535)^((right*257)&65535))
    lines+=['static unsigned int seeds[8] = { '+','.join(map(str,seeds))+' };',
            'static unsigned int expected[576] = { '+','.join(map(str,expected))+' };',
            'static int checks, failures;',
            'static void check(unsigned int actual) { if (actual != expected[checks]) { printf("FAIL generated %d got=%u expected=%u\\n", checks, actual, expected[checks]); ++failures; } ++checks; }',
            'int main(void) { int sample, index, flag; for (sample = 0; sample < 8; ++sample) for (index = 0; index < 3; ++index) for (flag = 0; flag < 2; ++flag) {']
    lines+=['check('+name+'(seeds[sample], index, flag));' for name in names]
    lines+=['} printf("MIR generated qualifier checks=%d failures=%d\\n", checks, failures); return failures != 0; }']
    return '\n'.join(lines)+'\n'


def prepare_sources(entries,work,seeds):
    sources={s for e in entries for s in e['Parameters'].get('Sources',[])}
    if '@work/qualgen.c' in sources:(work/'qualgen.c').write_text(qualifier_source(),encoding='ascii')
    spec=importlib.util.spec_from_file_location('clobber_fuzz',Path(__file__).with_name('new-mir-fuzz-source.py'))
    generator=importlib.util.module_from_spec(spec);spec.loader.exec_module(generator)
    for seed in seeds:
        name='fz'+str(seed)+'.c'
        if '@work/'+name in sources:(work/name).write_text(generator.generate(seed),encoding='ascii')


def setup_controls(runner,cases):
    values={'DCC_MIR_MACHINE_MUTATE':None,'DCC_MIR_MACHINE_MUTATE_FUNCTION':'main',
            'DCC_MIR_REQUIRE_COMPLETE':'1','DCC_MIR_REQUIRE_EMIT':'1',
            'DCC_MIR_MACHINE_REPORT':'1','DCC_MIR_SELECT_REPORT':'1'}
    if not cases or 'fuzz' in {v.casefold() for v in cases}:values['DCC_MIR_CACHE_VERIFY']='1'
    env=controlled(values,base=runner.base)
    source=runner.root/'tests/mir-clobber/logserie.c';output=runner.work/'PARSER.MAC'
    def compile_control(spec,expected):
        result=execute([runner.dcc,'-c',source,'-o',output],runner.root,
                       controlled({'DCC_MIR_MACHINE_MUTATE':spec},base=env),runner.args.build_timeout,runner.work/'parser.log')
        require(not result['TimedOut'] and ((result['ExitCode']==0) if spec is None else result['ExitCode']!=0) and
                (expected is None or expected in result['Output']),
                "MIR mutation '{}' did not {}:\n{}".format(spec,'succeed' if expected is None else 'fail with '+expected,result['Output']))
    compile_control(None,None)
    for spec in ('invalid','-1:immediate:0','x:immediate:0','2147483648:immediate:0','999:immediate:0'):
        compile_control(spec,'invalid DCC_MIR_MACHINE_MUTATE specification')
    for spec in ('1:type:not-a-number','1:immediate:999999999999999999999999'):
        compile_control(spec,'invalid DCC_MIR_MACHINE_MUTATE value')
    for spec in ('1:type:2147483648','1:type:-2147483649'):
        compile_control(spec,'DCC_MIR_MACHINE_MUTATE integer field is out of range')
    if os.name=='nt':
        for spec in ('1:immediate:2147483648','1:immediate:-2147483649'):
            compile_control(spec,'DCC_MIR_MACHINE_MUTATE value is out of range')
    for spec in ('1:unknown:0','3:identity:0','3:identity:256'):
        compile_control(spec,'unknown DCC_MIR_MACHINE_MUTATE field')
    chosen={v.casefold() for v in cases}
    for proof in json.loads(DATA.read_text())['Proofs']:
        if chosen and proof['Name'].casefold() not in chosen:continue
        result=execute([runner.dcc,'-c',runner.root/'tests/mir-clobber'/proof['Source'],'-o',runner.work/'SEMANTIC.MAC'],
                       runner.root,controlled({'DCC_MIR_REPORT':'1','DCC_MIR_MACHINE_MUTATE_FUNCTION':None},base=env),runner.args.build_timeout,runner.work/('proof-'+proof['Name']+'.log'))
        require(not result['TimedOut'] and result['ExitCode']==0,'MIR '+proof['Name']+' proof failed:\n'+result['Output'])
        access_evidence(result['Output'],proof['Expectations'])


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
            if args.shard_index==0:setup_controls(runner,args.cases)
            prepare_sources([inventory[key] for key in keys],work,args.fuzz_seeds)
            actual=[]
            for key in keys:
                runner.run(inventory[key]);actual.append(key)
            assert_manifest(keys,actual)
        if args.execution_manifest:write_json(ROOT/args.execution_manifest,sorted(actual))
        print('MIR emission-clobber regressions passed {} target configurations'.format(len(actual)))
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
