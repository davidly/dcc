#!/usr/bin/env bash
#
# stacksize.sh - find the minimum C stack reserve an app needs under dcc's
# lightweight stack-overflow guard (-fstack-check).
#
# It builds the app with the guard forced on, then sweeps the -stack reserve
# upward (default: from 256 bytes in 64-byte steps) until the program runs
# without tripping the guard ("?stack overflow").  The first size that runs
# clean is the minimum requirement; the script also prints a rounded-up
# recommendation with a little headroom.
#
# Usage:
#   scripts/stacksize.sh <app> [-- emulator-args...]
#
#   <app>              test/app name as passed to ma.sh (e.g. triangle, cobint)
#   -- args...         arguments to pass to the emulated program (e.g. a data
#                      file like e.cob)
#
# Options (environment variables):
#   START=256          first stack size to try (bytes)
#   STEP=64            increment between tries (bytes)
#   MAX=8192           give up after this size (apps that overflow on purpose,
#                      e.g. spsmash, never pass and would loop forever)
#   MODE=peep          build mode passed to ma.sh (peep|nopeep)
#   EMU=ntvcm          emulator command used to run the .COM
#
# Examples:
#   scripts/stacksize.sh triangle
#   scripts/stacksize.sh cobint -- e.cob
#   START=512 STEP=128 scripts/stacksize.sh triangle
#
set -uo pipefail

prog="$(basename "$0")"

usage() {
    sed -n '3,33p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

# ---- parse args -----------------------------------------------------------
app=""
emu_args=()
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage 0 ;;
        --) shift; emu_args=("$@"); break ;;
        -*) echo "$prog: unknown option: $1" >&2; usage 1 ;;
        *)  if [ -z "$app" ]; then app="$1"; else emu_args+=("$1"); fi ;;
    esac
    shift
done

if [ -z "$app" ]; then
    echo "$prog: missing <app>" >&2
    usage 1
fi

START="${START:-256}"
STEP="${STEP:-64}"
MAX="${MAX:-8192}"
MODE="${MODE:-peep}"
EMU="${EMU:-ntvcm}"

# ---- locate repo root (the parent of this script's dir) -------------------
script_dir="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
cd "$repo_root"

# Use the in-repo compiler binaries by default, like the other helper scripts.
export DCC="${DCC:-./dcc}"
export DCCPEEP="${DCCPEEP:-./dccpeep}"
export DCCRTLSTRIP="${DCCRTLSTRIP:-./dccrtlstrip}"

BUILD_DIR="${BUILD_DIR:-build}"
upper="$(printf '%s' "$app" | tr '[:lower:]' '[:upper:]')"

if ! command -v "$EMU" >/dev/null 2>&1; then
    echo "$prog: emulator '$EMU' not found on PATH (set EMU=...)" >&2
    exit 1
fi

# Force the stack-overflow guard on for every build in this sweep.
export DCC_FORCE_STACK_CHECK=1

echo "Finding minimum stack for '$app' (guard on): start=$START step=$STEP max=$MAX mode=$MODE"
[ "${#emu_args[@]}" -gt 0 ] && echo "  emulator args: ${emu_args[*]}"
echo

found=""
size="$START"
while [ "$size" -le "$MAX" ]; do
    # Build with this reserve.
    if ! DCC_STACK_SIZE="$size" ./ma.sh "$app" "$MODE" >/dev/null 2>&1; then
        printf '  stack=%-6s : BUILD FAILED\n' "$size"
        size=$(( size + STEP ))
        continue
    fi

    # Run and capture output.
    out="$(cd "$BUILD_DIR" && "$EMU" "$upper" "${emu_args[@]}" 2>&1)"

    if printf '%s' "$out" | grep -q '?stack overflow'; then
        printf '  stack=%-6s : overflow\n' "$size"
    else
        printf '  stack=%-6s : OK\n' "$size"
        found="$size"
        break
    fi
    size=$(( size + STEP ))
done

echo
if [ -z "$found" ]; then
    echo "No passing stack size up to $MAX bytes."
    echo "This app may overflow on purpose (e.g. unbounded recursion), or raise MAX=... and retry."
    exit 1
fi

# Recommend a rounded-up value with a little headroom (next 128-byte multiple
# above found + one step).
rec=$(( found + STEP ))
rem=$(( rec % 128 ))
[ "$rem" -ne 0 ] && rec=$(( rec + (128 - rem) ))

echo "Minimum passing stack reserve : $found bytes"
echo "Recommended (with headroom)   : $rec bytes"
echo
echo "Build it with:  DCC_STACK_SIZE=$rec ./ma.sh $app $MODE"
echo "Or compile direct:  $DCC -fstack-check -stack $rec tests/$app.c -o $upper.mac"
