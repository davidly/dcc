#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
exec pwsh -NoProfile -File scripts/runall.ps1 -mode full -extended -ti "$@"
