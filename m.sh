#!/bin/bash
# Build dcc (C compiler), dccpeep, dccrtlstrip, and dccmake using gcc.
# Equivalent of m.bat on Linux/macOS.

set -e

rm -f dcc dccpeep dccrtlstrip dccmake m80c

# These are host build tools, not the Z80 target, so link statically on
# Linux by default: the binaries are then copyable/runnable on another Linux
# box without matching the exact glibc version. Not possible on macOS (no
# static libSystem to link against). Set STATICFLAGS before calling this
# script to override (e.g. STATICFLAGS= to force dynamic linking if the
# static libc dev package, e.g. glibc-static, isn't installed). Exported so
# build-dcc.sh (below) picks up the same override for the dcc binary itself.
if [ -z "${STATICFLAGS+set}" ]; then
    case "$(uname)" in
        Linux) STATICFLAGS=-static ;;
        *)     STATICFLAGS= ;;
    esac
fi
export STATICFLAGS

echo "Building dcc..."
pushd src/dcc
./build-dcc.sh
popd

echo "Building dccpeep..."
gcc -O2 -g $STATICFLAGS -o dccpeep src/dccpeep/dccpeep.c
# cp dccpeep /mnt/c/users/david/onedrive/ntvcm/dcc

echo "Building dccrtlstrip..."
gcc -O2 -g $STATICFLAGS -o dccrtlstrip src/dccrtlstrip/dccrtlstrip.c
# cp dccrtlstrip /mnt/c/users/david/onedrive/ntvcm/dcc

echo "Building dccmake..."
gcc -O2 -g $STATICFLAGS -o dccmake src/dccmake/dccmake.c

echo "Building m80c..."
gcc -std=c89 -O2 $STATICFLAGS -o m80c src/m80c/m80c.c

echo "Done."
