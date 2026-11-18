"""Portable DCC package installation helpers; Python 3.7+, no PowerShell."""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import sys
TOOLS=('dcc','dccpeep','dccrtlstrip','dccmake','m80c','l80c','ntvcm')
MARKER='.dcc-install.json'


def owned(prefix):
    marker=prefix/MARKER
    if marker.is_file():
        try:return json.loads(marker.read_text()).get('package')=='dcc-cpm-z80'
        except ValueError:return False
    return (prefix/'PACKAGE-README.md').is_file() and (prefix/'DCCRTL.MAC').is_file() and (prefix/'bin').is_dir()


def checked_prefix(value,package=None):
    raw=Path(value).expanduser()
    if raw.is_symlink():raise RuntimeError('Install directory must not be a symbolic link')
    prefix=raw.resolve()
    if prefix==Path(prefix.anchor) or prefix==Path.home().resolve():raise RuntimeError('Refusing to replace a filesystem root or home directory')
    if package:
        package=Path(package).resolve()
        if prefix==package or prefix in package.parents or package in prefix.parents:
            raise RuntimeError('Install directory and package directory must not overlap')
    if prefix.exists() and any(prefix.iterdir()) and not owned(prefix):
        raise RuntimeError('Directory is not an existing DCC installation: '+str(prefix))
    return prefix


def environment(prefix):
    return {'DCC_HOME':str(prefix),'DCC_INCLUDE':str(prefix/'include'),
            'DCC_LIB':str(prefix/'lib')+os.pathsep+str(prefix),
            'DCC_RUNTIME':str(prefix/'lib/DCCRTL.MAC'),'M80':str(prefix/'m80.com'),'L80':str(prefix/'l80.com')}


def shell_environment(prefix):
    return ('# dcc CP/M Z80 package environment\nexport DCC_HOME='+shlex.quote(str(prefix))+'\n'+
        'export DCC_INCLUDE="$DCC_HOME/include${DCC_INCLUDE:+:$DCC_INCLUDE}"\n'+
        'export DCC_LIB="$DCC_HOME/lib:$DCC_HOME${DCC_LIB:+:$DCC_LIB}"\n'+
        'export DCC_RUNTIME="${DCC_RUNTIME:-$DCC_HOME/lib/DCCRTL.MAC}"\n'+
        'export M80="${M80:-$DCC_HOME/m80.com}"\nexport L80="${L80:-$DCC_HOME/l80.com}"\n')


def windows_environment(prefix,remove=False,change_path=False,registry=None):
    if registry is None:import winreg as registry
    with registry.CreateKey(registry.HKEY_CURRENT_USER,'Environment') as key:
        def get(name):
            try:return registry.QueryValueEx(key,name)
            except FileNotFoundError:return '',registry.REG_EXPAND_SZ
        for name,value in environment(prefix).items():
            current,_=get(name)
            if remove:
                if current.casefold()==value.casefold():registry.DeleteValue(key,name)
            else:registry.SetValueEx(key,name,0,registry.REG_EXPAND_SZ,value)
        if change_path:
            current,kind=get('Path');target=str(prefix/'bin')
            entries=[s for s in current.split(';') if s]
            if remove:entries=[s for s in entries if s.casefold()!=target.casefold()]
            elif target.casefold() not in {s.casefold() for s in entries}:entries.append(target)
            registry.SetValueEx(key,'Path',0,kind,';'.join(entries))


def install(package,prefix,link_dir,add_path=False,windows=None):
    windows=os.name=='nt' if windows is None else windows
    package=Path(package).resolve();prefix=checked_prefix(prefix,package)
    if not (package/'PACKAGE-README.md').is_file() or not (package/'bin').is_dir():raise RuntimeError('Not a DCC binary package')
    link_dir=Path(link_dir).expanduser().resolve()
    if not windows:
        for tool in TOOLS:
            link=link_dir/tool
            if (link.exists() or link.is_symlink()) and not (link.is_symlink() and link.resolve()==(prefix/'bin'/tool).resolve()):
                raise RuntimeError('Refusing to replace unrelated command: '+str(link))
    if prefix.exists():shutil.rmtree(str(prefix))
    shutil.copytree(str(package),str(prefix))
    (prefix/MARKER).write_text(json.dumps({'package':'dcc-cpm-z80'})+'\n')
    if windows:windows_environment(prefix,change_path=add_path)
    else:
        (prefix/'dcc-env.sh').write_text(shell_environment(prefix))
        link_dir.mkdir(parents=True,exist_ok=True)
        for tool in TOOLS:
            target=prefix/'bin'/tool;link=link_dir/tool
            if target.is_file():
                if link.is_symlink():link.unlink()
                link.symlink_to(target)
    print('dcc installed to: '+str(prefix))
    print('Restart the terminal for Windows user environment changes, or source '+str(prefix/'dcc-env.sh')+' on Unix.')


def uninstall(prefix,link_dir,remove_path=False,windows=None):
    windows=os.name=='nt' if windows is None else windows
    prefix=checked_prefix(prefix)
    if not windows:
        for tool in TOOLS:
            link=Path(link_dir).expanduser()/tool
            if link.is_symlink() and link.resolve()==(prefix/'bin'/tool).resolve():link.unlink()
    else:windows_environment(prefix,remove=True,change_path=remove_path)
    if prefix.exists():shutil.rmtree(str(prefix))
    print('dcc removed from: '+str(prefix))


def main(action,argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    default=str(Path(os.environ.get('LOCALAPPDATA',str(Path.home()))) / 'dcc-cpm-z80') if os.name=='nt' else str(Path.home()/'.local/dcc-cpm-z80')
    p.add_argument('--install-dir','-InstallDir',default=os.environ.get('PREFIX',default))
    p.add_argument('--link-dir',default=os.environ.get('LINK_DIR',str(Path.home()/'.local/bin')))
    p.add_argument('--add-to-user-path','-AddToUserPath',action='store_true')
    p.add_argument('--remove-from-user-path','-RemoveFromUserPath',action='store_true')
    a=p.parse_args(argv)
    try:
        if action=='install':install(Path(__file__).resolve().parent,a.install_dir,a.link_dir,a.add_to_user_path)
        else:uninstall(a.install_dir,a.link_dir,a.remove_from_user_path)
        return 0
    except (OSError,RuntimeError) as error:print('ERROR: '+str(error),file=sys.stderr);return 1
