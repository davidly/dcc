"""Prepare/build DEB, macOS PKG and WiX MSI installers (Python 3.7+)."""
import argparse
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
try:
    from .release_package import package_name
    from .package_install import TOOLS,shell_environment
except ImportError:
    from release_package import package_name
    from package_install import TOOLS,shell_environment


def version_text(value):
    match=re.search(r'\d+\.\d+\.\d+',value)
    return match.group() if match else '0.0.0'


def wix_source(package,version):
    namespace='http://wixtoolset.org/schemas/v4/wxs';ET.register_namespace('',namespace)
    def element(parent,name,**attributes):return ET.SubElement(parent,'{'+namespace+'}'+name,attributes)
    root=ET.Element('{'+namespace+'}Wix')
    product=element(root,'Package',Name='dcc CP/M Z80',Manufacturer='gloveboxes',Version=version_text(version),
                    UpgradeCode='{6B38A8A4-61F7-4D3F-8F7A-3B0220E86F40}',Scope='perMachine')
    element(product,'MajorUpgrade',DowngradeErrorMessage='A newer version of dcc CP/M Z80 is already installed.')
    element(product,'MediaTemplate',EmbedCab='yes')
    base=element(element(product,'StandardDirectory',Id='ProgramFiles64Folder'),'Directory',Id='INSTALLFOLDER',Name='dcc-cpm-z80')
    components=[];counter=[0]
    def identifier(prefix):counter[0]+=1;return prefix+str(counter[0])
    def visit(parent,directory):
        for path in sorted(directory.iterdir(),key=lambda p:p.name):
            if path.is_dir():visit(element(parent,'Directory',Id=identifier('dir'),Name=path.name),path)
            else:
                component=identifier('cmp');components.append(component)
                node=element(parent,'Component',Id=component,Guid='*')
                element(node,'File',Id=identifier('fil'),Source=str(path.resolve()),Name=path.name,KeyPath='yes')
    visit(base,Path(package))
    components.append('cmpPathEnvironment');env=element(base,'Component',Id=components[-1],Guid='*')
    values={'Path':'[INSTALLFOLDER]bin','DCC_HOME':'[INSTALLFOLDER]',
            'DCC_INCLUDE':'[INSTALLFOLDER]include','DCC_LIB':'[INSTALLFOLDER]lib;[INSTALLFOLDER]',
            'DCC_RUNTIME':'[INSTALLFOLDER]lib\\DCCRTL.MAC','M80':'[INSTALLFOLDER]m80.com','L80':'[INSTALLFOLDER]l80.com'}
    for index,(name,value) in enumerate(values.items()):
        attributes=dict(Id='env'+str(index),Name=name,Value=value,Action='set',System='no',Permanent='no')
        if name=='Path':attributes['Part']='last'
        element(env,'Environment',**attributes)
    element(env,'RegistryValue',Root='HKLM',Key='Software\\gloveboxes\\dcc-cpm-z80',Name='PathEnvironment',Type='integer',Value='1',KeyPath='yes')
    feature=element(product,'Feature',Id='MainFeature',Title='dcc CP/M Z80',Level='1')
    for component in components:element(feature,'ComponentRef',Id=component)
    return b'<?xml version="1.0" encoding="utf-8"?>\n'+ET.tostring(root,encoding='utf-8')


def unix_payload(package,work,prefix,link_dir):
    payload=work/prefix.lstrip('/');shutil.copytree(str(package),str(payload))
    links=work/link_dir.lstrip('/');links.mkdir(parents=True,exist_ok=True)
    for tool in TOOLS:
        if (payload/'bin'/tool).is_file():(links/tool).symlink_to(prefix+'/bin/'+tool)
    wrapper=links/'dcc-ma'
    wrapper.write_text('#!/bin/sh\n'+shell_environment(Path(prefix))+'exec "'+prefix+'/bin/dccmake" "$@"\n')
    wrapper.chmod(0o755)
    script=shell_environment(Path(prefix));profile=work/'etc/profile.d/dcc-cpm-z80.sh'
    profile.parent.mkdir(parents=True,exist_ok=True);profile.write_text(script)
    (payload/'dcc-env.sh').write_text(script)


def prepare(name,package,target,output,version):
    package_name(name);package=Path(package).resolve();output=Path(output).resolve()
    if not (package/'bin').is_dir():raise RuntimeError('Package bin directory not found: '+str(package))
    tool='dcc.exe' if target.startswith('windows') else 'dcc'
    if not (package/'bin'/tool).is_file():raise RuntimeError('Package tool not found: '+str(package/'bin'/tool))
    output.mkdir(parents=True,exist_ok=True)
    if target in ('windows-x64','windows-arm64'):
        source=output/(name+'.wxs');source.write_bytes(wix_source(package,version))
        return ['wix','build',str(source),'-arch','arm64' if target.endswith('arm64') else 'x64','-out',str(output/(name+'.msi'))]
    if target not in ('linux-x64','linux-arm64','macos-apple-silicon','macos-x64'):raise RuntimeError('Unsupported installer target: '+target)
    linux=target.startswith('linux');work=output/(name+('-debroot' if linux else '-pkgroot'))
    if work.is_symlink() or work==package or work in package.parents or package in work.parents:raise RuntimeError('Installer staging tree overlaps package or is a symbolic link')
    if work.exists():shutil.rmtree(str(work))
    prefix='/opt/dcc-cpm-z80' if linux else '/usr/local/dcc-cpm-z80'
    unix_payload(package,work,prefix,'/usr/bin' if linux else '/usr/local/bin')
    if linux:
        control=work/'DEBIAN';control.mkdir()
        (control/'control').write_text('Package: dcc-cpm-z80\nVersion: '+version_text(version)+'\nSection: devel\nPriority: optional\nArchitecture: '+('arm64' if target.endswith('arm64') else 'amd64')+'''
Maintainer: gloveboxes <noreply@github.com>
Provides: dcc-4-cpm-z80
Conflicts: dcc-4-cpm-z80
Replaces: dcc-4-cpm-z80
Description: dcc C compiler toolchain for CP/M 2.2 on Z80
 Includes dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c, ntvcm, runtime and headers.
''')
        return ['dpkg-deb','--build','--root-owner-group',str(work),str(output/(name+'.deb'))]
    return ['pkgbuild','--root',str(work),'--identifier','com.gloveboxes.dcc-cpm-z80','--version',version_text(version),'--install-location','/',str(output/(name+'.pkg'))]


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('PackageName','PackageRoot','Target'):
        key=re.sub('([a-z])([A-Z])',r'\1-\2',name).lower();p.add_argument('--'+key,'-'+name,required=True)
    p.add_argument('--output-dir','-OutputDir',default='dist');p.add_argument('--version','-Version',default='0.0.0')
    p.add_argument('--prepare-only',action='store_true')
    a=p.parse_args(argv)
    try:
        command=prepare(a.package_name,a.package_root,a.target,a.output_dir,a.version)
        if a.prepare_only:print(subprocess.list2cmdline(command));return 0
        expected='Windows' if a.target.startswith('windows') else 'Linux' if a.target.startswith('linux') else 'Darwin'
        if platform.system()!=expected:raise RuntimeError('Native installer must be built on '+expected)
        if command[0]=='wix' and not shutil.which('wix'):
            candidates=[Path(os.environ.get('USERPROFILE',str(Path.home())))/'.dotnet/tools/wix.exe',Path.home()/'.dotnet/tools/wix']
            found=next((x for x in candidates if x.is_file()),None)
            if found:command[0]=str(found)
        subprocess.run(command,check=True);print('Created: '+command[-1]);return 0
    except (OSError,RuntimeError,ValueError,subprocess.CalledProcessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);return 1
