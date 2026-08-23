#!/bin/sh
#
# build-dcc.sh - build the modularised dcc compiler (separate compilation).
#
# Compiles each module translation unit (dcc.c and the dcc_*.c files, all of
# which include the shared dcc contracts) and links them into the `dcc`
# executable at the repo root using the portable C11 host baseline.
#
# Usage (from anywhere):
#   sh src/dcc/build-dcc.sh            # build ./dcc at the repo root
#   sh src/dcc/build-dcc.sh -o out/dcc # build to a custom path
#
# The canonical `scripts/build-dcc.ps1` build owns the complete host toolchain;
# this internal helper only builds the modular dcc front end.

set -e

# Resolve the directory this script lives in, so it works from any CWD.
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)

if [ -z "$CC" ]; then
    case "$(uname)" in
        Darwin) CC=clang ;;
        *)      CC=gcc ;;
    esac
fi
# -O3 -flto=auto measured ~3.8% faster dcc-compile throughput than -O2 on
# gcc/Linux (non-overlapping across 3 interleaved A/B rounds via
# scripts/runall.ps1 -TimingBreakdown), with build time itself unaffected
# (=auto lets LTRANS parallelize across cores - plain -flto without a job
# count falls back to serial LTRANS, which measured ~7x slower to build).
#
# clang measured its own, larger win from -O3 alone (~5.75%, non-overlapping
# across 3 rounds on Ubuntu clang-14/WSL) - but -flto=thin on top of that
# added no measurable further improvement (heavily overlapping ranges)
# while costing ~2x build time, so LTO is not enabled for clang here.
#
# MSVC keeps the -O2 baseline this script and build-dcc.ps1 have always
# used - MSVC's /GL+/LTCG has a different cost/benefit tradeoff that
# hasn't been measured on this project.
case "$CC" in
    *gcc*)   CFLAGS=${CFLAGS:--std=c11 -Wall -Wextra -O3 -flto=auto -g} ;;
    *clang*) CFLAGS=${CFLAGS:--std=c11 -Wall -Wextra -O3 -g} ;;
    *)       CFLAGS=${CFLAGS:--std=c11 -Wall -Wextra -O2 -g} ;;
esac

# On macOS, clang can emit large tentative definitions into __DATA,__common
# with very high alignment (for large objects), which triggers an ld warning
# about reducing alignment. Force normal definitions to avoid __common.
#
# On Linux, link statically by default so the binary is copyable/runnable on
# another Linux box without matching the exact glibc version - this is a host
# build tool, not the Z80 target, so that's the only reason to link it any
# particular way. Not possible on macOS (no static libSystem to link
# against), so this never applies there. Set STATICFLAGS before calling this
# script to override (e.g. STATICFLAGS= to force dynamic linking if the
# static libc dev package, e.g. glibc-static, isn't installed).
case "$(uname)" in
    Darwin)
        CFLAGS="$CFLAGS -fno-common"
        ;;
esac
if [ -z "${STATICFLAGS+set}" ]; then
    case "$(uname)" in
        Linux) STATICFLAGS=-static ;;
        *)     STATICFLAGS= ;;
    esac
fi

OUT="$REPO_ROOT/dcc"

# Allow "-o <path>" to override the output location.
if [ "$1" = "-o" ] && [ -n "$2" ]; then
    OUT="$2"
fi

# Determine a parallelism level. gcc/clang do not parallelize compilation
# across multiple input files within one invocation, so a single "$CC ...
# *.c" call compiling 37+ translation units runs effectively single-threaded
# (measured ~11.6s wall on a 24-core box, ~11s of it in one thread) even
# though the individual files compile quickly in isolation (the largest,
# ~8,700-line file alone compiles in ~1.5s). Compile each file to its own
# object in parallel, then link once, for the same output with no behavior
# change - only wall-clock time differs. JOBS=1 restores the old serial
# per-file behavior if xargs -P misbehaves in some environment.
if [ -z "${JOBS+set}" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS=$(nproc)
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    else
        JOBS=4
    fi
fi

OBJDIR=$(mktemp -d "${TMPDIR:-/tmp}/dcc-build-obj.XXXXXX")
trap 'rm -rf "$OBJDIR"' EXIT

echo "Building modular dcc -> $OUT (parallel compile, $JOBS jobs)"
# All .c files in this directory are module translation units linked together.
# STATICFLAGS is a linker-only flag; it is intentionally applied only at the
# final link step below, not to the per-file -c compiles.
( cd "$SCRIPT_DIR" && \
  ls ./*.c | xargs -P "$JOBS" -I{} sh -c \
    "$CC $CFLAGS -I . -c '{}' -o \"$OBJDIR/\$(basename '{}' .c).o\"" )
$CC $CFLAGS $STATICFLAGS -o "$OUT" "$OBJDIR"/*.o
echo "Done."
