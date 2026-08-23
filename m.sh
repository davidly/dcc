#!/bin/sh
# Compatibility entry point for the complete cross-platform host build.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if ! command -v pwsh >/dev/null 2>&1; then
    echo "PowerShell 7 (pwsh) is required to run scripts/build-dcc.ps1." >&2
    exit 1
fi

exec pwsh -NoProfile -File "$SCRIPT_DIR/scripts/build-dcc.ps1" "$@"
