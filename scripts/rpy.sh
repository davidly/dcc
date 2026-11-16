#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
exec python3 scripts/runall.py -mode full -extended --timing-breakdown "$@"
