#!/bin/sh
#
# build-dcc.sh - build the modularised dcc compiler (separate compilation).
#
# Compiles each module translation unit (dcc.c and the dcc_*.c files, all of
# which include the umbrella header dcc.h) and links them into the `dcc`
# executable at the repo root, matching the flags used for the monolithic
# build so the produced binary and its generated assembly are equivalent.
#
# Usage (from anywhere):
#   sh src/dcc/build-dcc.sh            # build ./dcc at the repo root
#   sh src/dcc/build-dcc.sh -o out/dcc # build to a custom path
#
# The companion peephole/runtime tools are built by mmacos.sh / m.sh / m.bat
# from src/dccpeep/dccpeep.c and src/dccrtlstrip/dccrtlstrip.c; this script
# only builds the modular dcc front end.

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
CFLAGS=${CFLAGS:--std=c89 -Wall -Wextra -O2 -g}

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

echo "Building modular dcc -> $OUT"
# All .c files in this directory are module translation units linked together.
( cd "$SCRIPT_DIR" && $CC $CFLAGS $STATICFLAGS -I . -o "$OUT" ./*.c )
echo "Done."
