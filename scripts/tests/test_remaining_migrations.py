"""Portable extended/stress runners and package/release preparation contracts."""
import importlib.util
import json
import io
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
import xml.etree.ElementTree as ET
from unittest.mock import patch,Mock
from scripts import runall,stress_runall,release_package as pkg,package_install as install,native_installer as native,publish_package as publish,ci_tools

ROOT=Path(__file__).resolve().parents[2]


class MigrationTests(unittest.TestCase):
    def setUp(self):
        self.output=io.StringIO();quiet=patch('sys.stdout',self.output);quiet.start();self.addCleanup(quiet.stop)
        t=tempfile.TemporaryDirectory(prefix='dcc migration ');self.addCleanup(t.cleanup);self.root=Path(t.name).resolve()

    def payload(self,windows=False):
        source=self.root/'source';source.mkdir(exist_ok=True)
        for name in list(pkg.HEADERS)+['m80.com','l80.com','DCCRTL.MAC','README.md','LICENSE']:
            (source/name).write_text(name)
        for tool in pkg.TOOLS:
            path=source/(tool+('.exe' if windows else ''));path.write_text('binary');path.chmod(0o755)
        emulator=self.root/('ntvcm.exe' if windows else 'ntvcm');emulator.write_text('emulator');emulator.chmod(0o755)
        return pkg.stage('dcc-test',emulator,self.root/'dist','v2.3.4',source,windows)

    def test_extended_standard_inheritance_names_skips_and_custom_paths(self):
        suite=self.root/'suite';suite.mkdir();overrides=self.root/'overrides.json'
        overrides.write_text(json.dumps({'tests':[{'name':'three','ignore':True}]}))
        for name,tag in [('one','c89'),('two','c99'),('three','c11'),('four','other')]:
            (suite/(name+'.c')).write_text('source');(suite/(name+'.c.tags')).write_text('# tags\n'+tag+'\n')
            (suite/(name+'.c.expected')).write_text('expected')
        args=Mock()
        def selected(standards,names=(),empty=False):
            return runall.prepare_extended_suite(args,self.root,['peep','nopeep'],suite,overrides,standards,names,empty)
        for standard,count in [(['c89'],2),(['c99'],4),(['c11'],4),(None,6)]:
            items,error=selected(standard);self.assertIsNone(error);self.assertEqual(len(items),count)
        self.assertEqual(selected(['c11'],['three'],True),([],None))
        self.assertIn('Unknown extended tests',selected(None,['missing'])[1])
        self.assertEqual(len(selected(None,['two.c'])[0]),2)

    def test_standalone_build_deadline_preserves_main_runner_timeout_floor(self):
        from types import SimpleNamespace
        args=SimpleNamespace(timeout=17,build_timeout=17,emulator="ntvcm",emulated_m80=False,emulated_l80=False)
        with patch.object(runall,'command',return_value='dccmake'),patch.object(runall,'run',return_value=(0,False,'')) as execute:
            runall.build('probe',Path('probe.c'),self.root,'peep',{},args)
            self.assertEqual(execute.call_args[0][2],17)
            del args.build_timeout
            runall.build('probe',Path('probe.c'),self.root,'peep',{},args)
            self.assertEqual(execute.call_args[0][2],60)

    def test_package_layout_archive_and_missing_input_does_not_destroy_output(self):
        payload=self.payload()
        self.assertEqual((payload/'lib/DCCRTL.MAC').read_bytes(),(payload/'DCCRTL.MAC').read_bytes())
        self.assertTrue(all((payload/'bin'/t).is_file() for t in install.TOOLS))
        self.assertTrue((payload/'install.py').is_file());self.assertFalse(list(payload.glob('*.ps1')))
        import zipfile,tarfile
        for kind in ('zip','tar.gz'):
            artifact=pkg.archive(payload,kind)
            if kind=='zip':
                with zipfile.ZipFile(artifact) as archive:self.assertIn('dcc-test/bin/dccmake',archive.namelist())
            else:
                with tarfile.open(artifact) as archive:self.assertIn('dcc-test/bin/dccmake',archive.getnames())
        (self.root/'source/stdio.h').unlink()
        with self.assertRaises(RuntimeError):pkg.stage('dcc-test',self.root/'ntvcm',self.root/'dist',root=self.root/'source')
        self.assertTrue((payload/'bin/dcc').exists())
        for name in ('../escape','/absolute','..'):
            with self.assertRaises(ValueError):pkg.package_name(name)

    @unittest.skipIf(os.name=='nt','Unix command links')
    def test_install_uninstall_owns_only_its_prefix_and_links(self):
        payload=self.payload();prefix=self.root/'installed';links=self.root/'commands';links.mkdir()
        (links/'dcc').write_text('unrelated')
        with self.assertRaisesRegex(RuntimeError,'unrelated'):install.install(payload,prefix,links,windows=False)
        self.assertFalse(prefix.exists());(links/'dcc').unlink()
        install.install(payload,prefix,links,windows=False)
        self.assertTrue(install.owned(prefix));self.assertTrue((links/'dccmake').is_symlink())
        self.assertIn('DCC_RUNTIME',(prefix/'dcc-env.sh').read_text())
        other=self.root/'other';other.write_text('other');(links/'dcc').unlink();(links/'dcc').symlink_to(other)
        install.uninstall(prefix,links,windows=False)
        self.assertFalse(prefix.exists());self.assertTrue((links/'dcc').is_symlink());self.assertFalse((links/'dccmake').exists())
        unknown=self.root/'unknown';unknown.mkdir();(unknown/'keep').write_text('keep')
        with self.assertRaises(RuntimeError):install.uninstall(unknown,links,windows=False)
        with self.assertRaises(RuntimeError):install.checked_prefix(payload,payload)

    def test_windows_environment_preserves_overrides_and_deduplicates_path(self):
        values={'Path':('C:\\Other',2)};registry=Mock();key=Mock();registry.CreateKey.return_value.__enter__=Mock(return_value=key);registry.CreateKey.return_value.__exit__=Mock(return_value=False)
        registry.REG_EXPAND_SZ=2
        def read(_,name):
            if name not in values:raise FileNotFoundError(name)
            return values[name]
        registry.QueryValueEx.side_effect=read
        registry.SetValueEx.side_effect=lambda _,name,reserved,kind,value:values.__setitem__(name,(value,kind))
        registry.DeleteValue.side_effect=lambda _,name:values.pop(name)
        prefix=self.root/'prefix'
        install.windows_environment(prefix,change_path=True,registry=registry)
        path=values['Path'];install.windows_environment(prefix,change_path=True,registry=registry);self.assertEqual(path,values['Path'])
        values['DCC_INCLUDE']=('custom',2)
        install.windows_environment(prefix,remove=True,change_path=True,registry=registry)
        self.assertEqual(values['DCC_INCLUDE'],('custom',2));self.assertNotIn('DCC_HOME',values);self.assertEqual(values['Path'],('C:\\Other',2))

    def test_native_manifests_have_complete_tools_and_no_missing_wrapper(self):
        payload=self.payload();out=self.root/'native'
        if os.name!='nt':
            command=native.prepare('dcc-test',payload,'linux-arm64',out,'v2.3.4-rc1')
            self.assertEqual(command[:3],['dpkg-deb','--build','--root-owner-group'])
            tree=out/'dcc-test-debroot'
            self.assertIn('Architecture: arm64',(tree/'DEBIAN/control').read_text())
            self.assertTrue((tree/'usr/bin/m80c').is_symlink());self.assertIn('/bin/dccmake',(tree/'usr/bin/dcc-ma').read_text())
            self.assertNotIn('/scripts/ma.sh',(tree/'usr/bin/dcc-ma').read_text())
            command=native.prepare('dcc-test',payload,'macos-apple-silicon',out,'v2.3.4')
            self.assertEqual(command[0],'pkgbuild');self.assertTrue((out/'dcc-test-pkgroot/usr/local/bin/l80c').is_symlink())
        payload=self.payload(windows=True)
        command=native.prepare('dcc-test',payload,'windows-arm64',out,'v2.3.4')
        self.assertIn('arm64',command)
        xml=ET.parse(str(out/'dcc-test.wxs'));ns={'w':'http://wixtoolset.org/schemas/v4/wxs'}
        files=xml.findall('.//w:File',ns)
        self.assertEqual(len(files),sum(p.is_file() for p in payload.rglob('*')))
        self.assertEqual(len({p.attrib['Id'] for p in files}),len(files))
        self.assertTrue(all(Path(p.attrib['Source']).is_file() for p in files))
        self.assertEqual(native.version_text('v2.3.4-rc1'),'2.3.4')

    def test_publish_dry_run_and_checked_replacement_order(self):
        calls=[]
        def command(argv,check=True):
            calls.append(argv)
            text='main\n' if argv[1:3]==['rev-parse','--abbrev-ref'] else ''
            if argv[:2]==['gh','api']:text='existing release'
            if argv[1]=='ls-remote' or argv[1:3]==['tag','--list']:text='tag'
            return subprocess.CompletedProcess(argv,0,text)
        args=publish.parse_args(['--version','v2.3.4','--dry-run'])
        publish.publish(args,command);self.assertTrue(all(c[0]=='git' and c[1] in ('rev-parse','status') for c in calls))
        calls[:]=[];args.dry_run=False;publish.publish(args,command)
        self.assertLess(calls.index(['gh','release','delete','v2.3.4','--cleanup-tag','--yes']),calls.index(['git','tag','v2.3.4']))
        self.assertIn(['git','push','origin','refs/tags/v2.3.4'],calls)
        args.version='--bad'
        with self.assertRaises(RuntimeError):publish.publish(args,command)
        args.version='v2.3.4'
        def unavailable(argv,check=True):
            if argv[:2]==['gh','api']:return subprocess.CompletedProcess(argv,1,'HTTP 401')
            return command(argv,check)
        calls[:]=[]
        with self.assertRaises(RuntimeError):publish.publish(args,unavailable)
        self.assertNotIn(['git','tag','v2.3.4'],calls)

    def test_signing_does_not_log_password_and_removes_certificate(self):
        import base64
        password='secret-for-test';certificate=[]
        def invoke(argv,**kwargs):
            path=Path(argv[argv.index('/f')+1]);self.assertTrue(path.is_file());certificate.append(path)
            return subprocess.CompletedProcess(argv,1,password)
        binary=self.root/'binary.exe';binary.write_bytes(b'fake')
        env={'WINDOWS_CODESIGN_PFX_BASE64':base64.b64encode(b'fake').decode(),'WINDOWS_CODESIGN_PASSWORD':password,'PATH':''}
        import io
        output=io.StringIO()
        with patch.object(ci_tools.shutil,'which',return_value='signtool'),patch.object(ci_tools.subprocess,'run',side_effect=invoke),patch('sys.stdout',output):
            with self.assertRaises(RuntimeError) as error:ci_tools.sign([binary],env)
        self.assertNotIn(password,str(error.exception));self.assertNotIn(password,output.getvalue())
        self.assertFalse(certificate[0].exists())

    def test_stress_stops_on_failure_retains_log_and_leaves_other_builds(self):
        (self.root/'build').mkdir();other=self.root/'build/unrelated';other.mkdir();(other/'keep').touch()
        script=self.root/'fake.py';script.write_text('import sys;print("ERROR probe");sys.exit(1)')
        args=stress_runall.parse_args(['--iterations','3','--log-dir',str(self.root/'logs')])
        self.assertEqual(stress_runall.run(args,self.root,script),1)
        self.assertTrue((self.root/'logs/stress_1.log').is_file());self.assertFalse((self.root/'logs/stress_2.log').exists())
        self.assertTrue((other/'keep').exists());self.assertEqual(list((self.root/'build').iterdir()),[other])

    def test_supervised_stdin_and_nested_stress_runall_timeout(self):
        with patch.dict(os.environ,{'DCC_STRESS_SUPERVISE':'1'}):
            code,timed,text=runall.run([sys.executable,'-c','import sys;print(sys.stdin.read(),end="")'],self.root,5,'abc')
        self.assertEqual((code,timed,text),(0,False,'abc\n' if os.name!='nt' else 'abc\r\n'))

    def test_nested_stress_process_tree_is_cleaned_on_outer_timeout(self):
        from scripts.process_supervision import Process
        from scripts.tests.test_process_supervision_python import process_alive
        import time
        pidfile=self.root/'descendant.pid'
        leaf='import subprocess,sys,time; p=subprocess.Popen([sys.executable,"-c","import time;time.sleep(30)"]);open('+repr(str(pidfile))+',"w").write(str(p.pid));time.sleep(30)'
        worker='import sys;sys.path.insert(0,'+repr(str(ROOT))+');from scripts import runall;runall.run([sys.executable,"-c",'+repr(leaf)+'],'+repr(str(self.root))+',30)'
        child=Process([sys.executable,'-c',worker],self.root,environment={'DCC_STRESS_SUPERVISE':'1'})
        self.addCleanup(child.stop)
        deadline=time.monotonic()+5
        while not pidfile.exists() and time.monotonic()<deadline:time.sleep(.01)
        self.assertTrue(pidfile.exists())
        result=child.complete(.1);self.assertTrue(result['TimedOut'])
        pid=int(pidfile.read_text());deadline=time.monotonic()+2
        while process_alive(pid) and time.monotonic()<deadline:time.sleep(.01)
        self.assertFalse(process_alive(pid),'stress descendant survived outer watchdog')

    def test_ci_emulator_build_reuses_msvc_and_keeps_release_flags(self):
        source=self.root/'ntvcm';source.mkdir()
        for name in ('ntvcm.cxx','x80.cxx'):(source/name).touch()
        module=Mock();module.native_arch.return_value='arm64';module.find_msvc.return_value='vcvars';module.msvc_environment.return_value={'PATH':'compiler'}
        with patch.object(ci_tools,'builder',return_value=module),patch.object(ci_tools.shutil,'which',return_value='cl.exe'),patch.object(ci_tools.subprocess,'run') as execute:
            ci_tools.build_ntvcm(source,self.root/'out/ntvcm.exe',release=True)
        command=execute.call_args[0][0]
        for flag in ('/GL','/Oti2','/Ob3','/Zi','/OPT:REF'):self.assertIn(flag,command)
        module.find_msvc.assert_called_once_with(unittest.mock.ANY,'arm64')


if __name__=='__main__':unittest.main()
