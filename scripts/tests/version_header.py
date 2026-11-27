"""Shared helper for tests that compile dcc host tools directly.

The host tools include dcc_version.h, which build-dcc.py generates into its
output directory rather than the source tree. Tests that invoke the C compiler
themselves generate the header into their own workspace with this helper and
pass the returned include flags to the compiler.
"""
from pathlib import Path
import importlib.util
import os
import shlex
import shutil
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parents[2]


def version_include(directory):
    """Write dcc_version.h into directory and return the matching -I flags."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [sys.executable, str(REPO_ROOT / "scripts" / "generate-version.py"),
         "--root", str(REPO_ROOT), "--output", str(directory / "dcc_version.h")],
        check=True,
        cwd=str(REPO_ROOT),
        stdout=subprocess.DEVNULL,
    )
    return ["-I", str(directory)]


def compile_host_tool(sources, output, workspace, flags=()):
    """Compile a host-tool test binary with the platform's native compiler."""
    sources = [str(source) for source in sources]
    output = Path(output)
    workspace = Path(workspace)
    version_include(workspace)
    if os.name == "nt":
        build_script = REPO_ROOT / "scripts" / "build-dcc.py"
        spec = importlib.util.spec_from_file_location("build_dcc", build_script)
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        environment = dict(os.environ)
        vcvars = builder.find_msvc(environment, builder.native_arch(environment))
        environment = builder.msvc_environment(vcvars, environment)
        compiler = shutil.which("cl.exe", path=builder.env_path(environment))
        if not compiler:
            raise RuntimeError("MSVC cl.exe not found after vcvars setup")
        command = [compiler, "/nologo", "/O2", "/std:c11",
                   "/I" + str(workspace), "/Fe:" + str(output),
                   "/Fo:" + str(workspace) + os.sep]
        command += sources + ["/link", "user32.lib", "ntdll.lib"]
        subprocess.run(command, cwd=str(REPO_ROOT), env=environment, check=True)
        return

    compiler = shlex.split(os.environ.get("CC") or "cc")
    command = compiler + list(flags) + ["-I", str(workspace)]
    command += sources + ["-o", str(output)]
    subprocess.run(command, cwd=str(REPO_ROOT), check=True)
