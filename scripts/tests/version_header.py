"""Shared helper for tests that compile dcc host tools directly.

The host tools include dcc_version.h, which build-dcc.py generates into its
output directory rather than the source tree. Tests that invoke the C compiler
themselves generate the header into their own workspace with this helper and
pass the returned include flags to the compiler.
"""
from pathlib import Path
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
