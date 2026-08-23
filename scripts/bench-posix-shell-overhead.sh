#!/bin/sh
# Isolates the cost of dccmake's run_cmd going through an extra shell layer
# (as system() does: fork+exec /bin/sh -c "<cmd>", which itself forks+execs
# the real program - two process creations per call) versus a direct
# fork+execvp of the same program with no shell involved at all (what
# run_cmd does now on POSIX, matching the Windows _spawnvp path).
#
# Runs both shapes serially and under N-way parallel load (via xargs -P),
# since a shell layer that's negligible serially could behave differently
# under the contention the real test suite runs under (runall.ps1's default
# throttle = processor count). Prints wall-clock totals for each so the
# extra-shell-layer cost can be read directly rather than inferred.
#
# Usage:
#   sh scripts/bench-posix-shell-overhead.sh [iterations] [parallelism]
#
#   iterations   number of calls per phase (default 800, matching roughly
#                a few hundred apps x ~2 m80c-shaped calls each in the full
#                suite)
#   parallelism  xargs -P value for the parallel phases (default: nproc)

set -eu

ITERATIONS=${1:-800}
PARALLEL=${2:-$(nproc 2>/dev/null || echo 8)}

# Trivial real external process - the fastest possible thing that's still
# an actual child process (not a shell builtin). `command -v true` resolves
# to the shell's own builtin (POSIX requires one), which "$NOOP" would then
# invoke without spawning any process at all, silently measuring nothing -
# so this checks known absolute paths to the real /usr/bin/true or /bin/true
# binary directly instead, matching the shape of run_cmd's real invocations
# of dcc/dccpeep/m80c/etc.
NOOP=/usr/bin/true
[ -x "$NOOP" ] || NOOP=/bin/true
[ -x "$NOOP" ] || { echo "no external true binary found" >&2; exit 1; }

time_phase() {
    name=$1
    shift
    start=$(date +%s.%N)
    "$@"
    end=$(date +%s.%N)
    elapsed=$(awk -v a="$start" -v b="$end" 'BEGIN { printf "%.3f", b - a }')
    printf '%-28s %ss total, %s calls\n' "$name" "$elapsed" "$ITERATIONS"
}

run_direct_serial() {
    i=0
    while [ "$i" -lt "$ITERATIONS" ]; do
        "$NOOP"
        i=$((i + 1))
    done
}

run_shell_serial() {
    i=0
    while [ "$i" -lt "$ITERATIONS" ]; do
        sh -c "$NOOP"
        i=$((i + 1))
    done
}

run_direct_parallel() {
    seq 1 "$ITERATIONS" | xargs -P "$PARALLEL" -I{} "$NOOP"
}

run_shell_parallel() {
    seq 1 "$ITERATIONS" | xargs -P "$PARALLEL" -I{} sh -c "$NOOP"
}

echo "iterations=$ITERATIONS parallelism=$PARALLEL noop=$NOOP"
echo ""
time_phase "DirectExec (serial)"     run_direct_serial
time_phase "ShellWrapped (serial)"   run_shell_serial
time_phase "DirectExec (parallel)"   run_direct_parallel
time_phase "ShellWrapped (parallel)" run_shell_parallel
