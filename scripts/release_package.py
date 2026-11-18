"""Stage and archive already-built DCC packages using Python 3.7+."""
import argparse
import os
from pathlib import Path
import re
import shutil
import sys
import tarfile
import tempfile
import zipfile
ROOT=Path(__file__).resolve().parents[1]
TOOLS=('dcc','dccpeep','dccrtlstrip','dccmake','m80c','l80c')
HEADERS=('assert.h','ctype.h','dirent.h','errno.h','fcntl.h','float.h','limits.h','locale.h','math.h','setjmp.h',
         'signal.h','stdarg.h','stdbool.h','stddef.h','stdint.h','stdio.h','stdlib.h','string.h','time.h','unistd.h')


def package_name(value):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]*',value):raise ValueError('Package name must be a single filename')
    return value


def stage(name,ntvcm,output,version='',root=ROOT,windows=None):
    name=package_name(name);root=Path(root);output=Path(output).resolve();ntvcm=Path(ntvcm).resolve()
    ext='.exe' if (os.name=='nt' if windows is None else windows) else ''
    inputs=[(root/(t+ext),'bin/'+t+ext) for t in TOOLS]+[(ntvcm,'bin/ntvcm'+ext)]+[(root/h,'include/'+h) for h in HEADERS]
    inputs += [(root/n,n) for n in ('m80.com','l80.com','DCCRTL.MAC','README.md','LICENSE')]+[(root/'DCCRTL.MAC','lib/DCCRTL.MAC')]
    for source,_ in inputs:
        if not source.is_file():raise RuntimeError('Required file not found: '+str(source))
    destination=output/name
    if destination==root.resolve() or destination in root.resolve().parents:raise RuntimeError('Package destination overlaps source tree')
    output.mkdir(parents=True,exist_ok=True)
    work=Path(tempfile.mkdtemp(prefix=name+'-',dir=str(output)))
    try:
        for source,target in inputs:
            path=work/target;path.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(str(source),str(path))
        shutil.copy2(str(Path(__file__).with_name('package_install.py')),str(work/'package_install.py'))
        for action in ('install','uninstall'):
            (work/(action+'.py')).write_text('from package_install import main\nraise SystemExit(main('+repr(action)+'))\n')
            (work/(action+'.sh')).write_text('#!/bin/sh\nset -eu\npackage_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\nexec python3 "$package_dir/'+action+'.py" "$@"\n')
            (work/(action+'.sh')).chmod(0o755)
            (work/(action+'.bat')).write_bytes(('@echo off\r\npython "%~dp0'+action+'.py" %*\r\n').encode())
        (work/'PACKAGE-README.md').write_text('''# dcc binary package

Version: '''+(version or 'development build')+'''

bin/ contains the six DCC tools and ntvcm; include/ contains public headers;
lib/DCCRTL.MAC is the runtime. m80.com/l80.com are optional emulated tools.
Native m80c/l80c are used by default.

Install with Python 3.7+: `python install.py --add-to-user-path` on Windows,
or `python3 install.py` on Unix. `install.bat` and `install.sh` are wrappers.
Unix defaults to ~/.local/dcc-cpm-z80 with links in ~/.local/bin. Override
with --install-dir/--link-dir or PREFIX/LINK_DIR. Source the installed dcc-env.sh
before using dccmake. Windows defaults to %LOCALAPPDATA%/dcc-cpm-z80 and sets
DCC_HOME, DCC_INCLUDE, DCC_LIB, DCC_RUNTIME, M80 and L80 in the user environment.
Restart the terminal after Windows environment changes.

Uninstall with `python uninstall.py --remove-from-user-path` (Windows) or
`python3 uninstall.py` (Unix), using the same directory overrides if applicable.
Only owned DCC installations and command links are removed.

For portable use, put bin/ on PATH, set DCC_HOME to the package directory,
DCC_INCLUDE to include/, DCC_RUNTIME to lib/DCCRTL.MAC, then run
`dccmake hello.c dcc-include=/path/to/package/include`.
''')
        if destination.is_symlink():raise RuntimeError('Package destination is a symbolic link')
        if destination.exists():shutil.rmtree(str(destination))
        work.rename(destination)
    finally:
        if work.exists():shutil.rmtree(str(work))
    return destination


def archive(package,kind):
    package=Path(package);suffix='.zip' if kind=='zip' else '.tar.gz'
    target=package.with_name(package.name+suffix)
    fd,temp=tempfile.mkstemp(prefix=package.name+'-',dir=str(package.parent));os.close(fd)
    try:
        if kind=='zip':
            with zipfile.ZipFile(temp,'w',zipfile.ZIP_DEFLATED) as z:
                for p in sorted(package.rglob('*')):z.write(str(p),str(p.relative_to(package.parent)))
        else:
            with tarfile.open(temp,'w:gz') as t:t.add(str(package),arcname=package.name)
        os.replace(temp,str(target))
    finally:
        if os.path.exists(temp):os.unlink(temp)
    return target


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package-name','-PackageName',required=True)
    p.add_argument('--ntvcm-path','-NtvcmPath',required=True)
    p.add_argument('--output-dir','-OutputDir',default='dist')
    p.add_argument('--version','-Version',default='')
    p.add_argument('--archive',choices=['zip','tar.gz'])
    a=p.parse_args(argv)
    try:
        dest=stage(a.package_name,ROOT/a.ntvcm_path,ROOT/a.output_dir,a.version)
        print('Package staged at: '+str(dest))
        if a.archive:print('Created: '+str(archive(dest,a.archive)))
        return 0
    except (OSError,RuntimeError,ValueError) as error:print('ERROR: '+str(error),file=sys.stderr);return 1
