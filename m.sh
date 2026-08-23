#!/bin/sh
# Compatibility entry point for the complete cross-platform host build.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

if ! command -v pwsh >/dev/null 2>&1; then
    echo "PowerShell 7 (pwsh) is required to run scripts/build-dcc.ps1." >&2
    case "$(uname -s)" in
        Darwin)
            echo "Install it with: brew install powershell" >&2
            ;;
        Linux)
            echo "Install it with: sudo snap install powershell --classic" >&2
            echo "(or see https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-linux for your distro's package manager)" >&2
            ;;
        *)
            echo "See https://learn.microsoft.com/powershell/scripting/install/installing-powershell for install instructions." >&2
            ;;
    esac
    exit 1
fi

exec pwsh -NoProfile -File "$SCRIPT_DIR/scripts/build-dcc.ps1" "$@"
