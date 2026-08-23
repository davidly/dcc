#!/bin/sh
# Compatibility alias for the canonical cross-platform host build.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

exec sh "$SCRIPT_DIR/m.sh" "$@"