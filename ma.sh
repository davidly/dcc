#!/bin/sh
set -eu
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
DCC_PYTHON=${PYTHON:-python3}
exec "$DCC_PYTHON" "$SCRIPT_DIR/scripts/ma.py" "$@"
