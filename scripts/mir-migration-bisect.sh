#!/bin/sh
# Automatic regression bisector for a MIR migration item.
#
# When a wide validation run fails after a gate/selector change, the manual
# process is: read the census delta's "newly MIR-emitted" list for the
# failing app, then guess-and-check with DCC_MIR_FORCE_FALLBACK_FUNCTION one
# candidate at a time until the failure disappears. This script automates
# that loop instead of doing it by hand, one candidate at a time, across
# several separate tool calls.
#
# For each candidate function, it forces *only that function* back onto the
# legacy fallback path (DCC_MIR_FORCE_FALLBACK_FUNCTION) and re-runs the
# failing app in fast mode. If the app passes with that one function forced
# to fallback, that function is a necessary condition for the observed
# failure and is reported as implicated. This isolates the common case (a
# single newly-emitted function causing the failure) in one command instead
# of a manual multi-step investigation; it does not attempt to find
# combinations of co-operating functions if no single one is implicated.
#
# Usage:
#   scripts/mir-migration-bisect.sh <app> <candidate1,candidate2,...>
#
# Example (this session's forint.assign_pre investigation, done by hand):
#   scripts/mir-migration-bisect.sh forint assign_pre,bump_sym_val

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <app> <candidate-function-list>" >&2
    exit 2
fi

app="$1"
candidates="$2"

echo "Bisecting failure in app '${app}' across candidates: ${candidates}"
echo

old_ifs="$IFS"
IFS=','
implicated=""
for function in $candidates; do
    IFS="$old_ifs"
    printf '== forcing %s to fallback ==\n' "$function"
    if DCC_MIR_FORCE_FALLBACK_FUNCTION="$function" \
        pwsh ./scripts/runall.ps1 -Apps "$app" -Mode fast -RunTimeout 20 \
        > /tmp/mir-bisect-"$function".log 2>&1; then
        echo "  PASS with ${function} forced to fallback -> IMPLICATED"
        implicated="${implicated} ${function}"
    else
        echo "  still FAILS with ${function} forced to fallback -> not solely responsible"
    fi
    IFS=','
done
IFS="$old_ifs"

echo
if [ -n "$implicated" ]; then
    echo "Implicated function(s):${implicated}"
    echo "(full logs in /tmp/mir-bisect-<function>.log)"
else
    echo "No single candidate's fallback alone fixed the failure."
    echo "The regression likely needs two or more of these functions"
    echo "together, or is unrelated to this candidate list - inspect"
    echo "/tmp/mir-bisect-*.log and consider a wider candidate list."
fi
