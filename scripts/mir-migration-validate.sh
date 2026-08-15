#!/bin/sh
# Fail-fast validation ladder for one MIR selection change.
#
# Chains the mandatory SKILL.md validation tiers end-to-end and stops at the
# first failure, instead of a human re-checking each stage's tail output by
# hand before deciding whether to proceed to the next (expensive) tier.
#
# Usage:
#   scripts/mir-migration-validate.sh <label> <baseline.tsv> <focused-apps>
#
#   <label>          short name for this item, used to name the output
#                     census snapshot: build/mir-<label>.tsv
#   <baseline.tsv>    path to the --compare baseline snapshot
#   <focused-apps>    comma-separated app list for the -Mode full focused
#                     tier (usually the census delta's "Focused validation"
#                     command's app list)
#
# Each tier uses runall.ps1's own -FailFast so a single bad app aborts that
# tier immediately rather than running the rest of a doomed suite.
#
# Example:
#   scripts/mir-migration-validate.sh item2 build/mir-before.tsv app1,app2

set -e

if [ "$#" -ne 3 ]; then
    echo "usage: $0 <label> <baseline.tsv> <focused-apps>" >&2
    exit 2
fi

label="$1"
baseline="$2"
apps="$3"

echo "== build =="
sh src/dcc/build-dcc.sh

echo "== strict generated-MIR census (fail-on-regression) =="
python3 scripts/mir-migration-census.py \
    --output "build/mir-${label}.tsv" --compare "$baseline" \
    --fail-on-regression

echo "== focused -Mode full ($apps) =="
pwsh ./scripts/runall.ps1 -Apps "$apps" -Mode full -FailFast -RunTimeout 20

echo "== wide -Mode fast safety net =="
pwsh ./scripts/runall.ps1 -Mode fast -FailFast -RunTimeout 20

echo "== full -Mode full -Extended safety net =="
pwsh ./scripts/runall.ps1 -Mode full -Extended -FailFast -RunTimeout 20

echo "== all tiers passed for '${label}' =="
