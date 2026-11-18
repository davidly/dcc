"""CI native emulator build and optional Windows signing, Python 3.7+."""
import argparse
import base64
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]


def builder():
    spec=importlib.util.spec_from_file_location('ci_builder',ROOT/'scripts/build-dcc.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module


def build_ntvcm(source,output=None,release=False,arch=None):
    source=Path(source).resolve();output=Path(output).resolve() if output else source/'ntvcm.exe'
    for name in ('ntvcm.cxx','x80.cxx'):
        if not (source/name).is_file():raise RuntimeError('Missing ntvcm source: '+str(source/name))
    output.parent.mkdir(parents=True,exist_ok=True)
    module=builder();env=dict(os.environ)
    try:
        vcvars=module.find_msvc(env,arch or module.native_arch(env))
        env=module.msvc_environment(vcvars,env)
    except module.BuildError as error:
        raise RuntimeError(str(error))
    compiler=shutil.which('cl',path=module.env_path(env))
    if not compiler:raise RuntimeError('MSVC cl.exe not found after environment setup')
    with tempfile.TemporaryDirectory(prefix='dcc-ntvcm-') as folder:
        argv=[compiler,'/nologo',str(source/'ntvcm.cxx'),str(source/'x80.cxx'),'/I'+str(source),'/GS-','/EHsc',
              '/Fo'+folder+os.sep,'/Fe:'+str(output)]
        argv+=['/GL','/Oti2','/Ob3','/Zi','/Fd'+str(Path(folder)/'compiler.pdb')] if release else ['/O2']
        argv+=['/link','user32.lib']+(['/OPT:REF'] if release else [])
        subprocess.run(argv,cwd=folder,env=env,check=True)
    return output


def sign(files,environment=None):
    env=dict(os.environ if environment is None else environment)
    encoded=env.get('WINDOWS_CODESIGN_PFX_BASE64');password=env.get('WINDOWS_CODESIGN_PASSWORD')
    if not encoded or not password:
        print('Skipping Windows signing: certificate or password is not configured.');return
    tool=shutil.which('signtool.exe',path=env.get('PATH'))
    if not tool:
        kits=Path(env.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Windows Kits/10/bin'
        architecture='arm64' if env.get('PROCESSOR_ARCHITECTURE','').lower()=='arm64' else 'x64'
        matches=sorted(kits.glob('*/'+architecture+'/signtool.exe'),reverse=True)
        if matches:tool=str(matches[0])
    if not tool:raise RuntimeError('signtool.exe was not found')
    # SignTool requires the password in argv. Never log that argv or expose
    # it through CalledProcessError, and redact its diagnostics.
    with tempfile.TemporaryDirectory(prefix='dcc-codesign-') as folder:
        certificate=Path(folder)/'codesign.pfx';certificate.write_bytes(base64.b64decode(encoded,validate=True))
        certificate.chmod(0o600)
        for file in files:
            if not Path(file).is_file():raise RuntimeError('Signing input not found: '+str(file))
            argv=[tool,'sign','/fd','SHA256','/tr',env.get('WINDOWS_CODESIGN_TIMESTAMP_URL') or 'http://timestamp.digicert.com',
                  '/td','SHA256','/f',str(certificate),'/p',password,str(file)]
            result=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,universal_newlines=True,env=env)
            # Tool diagnostics can echo command-line values; redact before output.
            if result.stdout:print(result.stdout.replace(password,'[redacted]'))
            if result.returncode:raise RuntimeError('Signing failed for '+str(file)+' (exit '+str(result.returncode)+')')


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__);sub=p.add_subparsers(dest='action')
    build=sub.add_parser('build-ntvcm');build.add_argument('--source-dir',default='ntvcm');build.add_argument('--output')
    build.add_argument('--release',action='store_true');build.add_argument('--arch',choices=['x64','arm64'])
    signing=sub.add_parser('sign');signing.add_argument('files',nargs='+')
    a=p.parse_args(argv)
    try:
        if a.action=='build-ntvcm':build_ntvcm(ROOT/a.source_dir,a.output,a.release,a.arch)
        elif a.action=='sign':sign(a.files)
        else:p.error('choose build-ntvcm or sign')
        return 0
    except (OSError,RuntimeError,ValueError,subprocess.CalledProcessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);return 1


if __name__=='__main__':raise SystemExit(main())
