#!/bin/sh
# Complete native host build; also works with Raspberry Pi OS's Python 3.
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DCC_PYTHON=${PYTHON:-python3}
if ! command -v "$DCC_PYTHON" >/dev/null 2>&1; then
    echo "Python 3.7 or newer is required (set PYTHON to its executable)." >&2
    exit 1
fi
exec "$DCC_PYTHON" "$SCRIPT_DIR/scripts/build-dcc.py" "$@"
