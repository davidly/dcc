#!/bin/bash
# Build dcc (C compiler), dccpeep, dccrtlstrip, dccmake, and m80c using gcc.
# Equivalent of m.bat on Linux/macOS. Each tool is an independent build (its
# own source, its own output binary), so all five run in parallel; only the
# final reporting is serialized so output doesn't interleave.

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

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT

echo "Building dcc, dccpeep, dccrtlstrip, dccmake, and m80c in parallel..."

src/dcc/build-dcc.sh                                                  > "$tmpdir/dcc.log"         2>&1 &
pid_dcc=$!

gcc -O2 -g $STATICFLAGS -o dccpeep src/dccpeep/dccpeep.c              > "$tmpdir/dccpeep.log"     2>&1 &
pid_dccpeep=$!
# cp dccpeep /mnt/c/users/david/onedrive/ntvcm/dcc

gcc -O2 -g $STATICFLAGS -o dccrtlstrip src/dccrtlstrip/dccrtlstrip.c  > "$tmpdir/dccrtlstrip.log" 2>&1 &
pid_dccrtlstrip=$!
# cp dccrtlstrip /mnt/c/users/david/onedrive/ntvcm/dcc

gcc -O2 -g $STATICFLAGS -o dccmake src/dccmake/dccmake.c              > "$tmpdir/dccmake.log"     2>&1 &
pid_dccmake=$!

gcc -std=c89 -O2 $STATICFLAGS -o m80c src/m80c/m80c.c                 > "$tmpdir/m80c.log"        2>&1 &
pid_m80c=$!

failed=0
for name in dcc dccpeep dccrtlstrip dccmake m80c; do
    pid_var="pid_$name"
    echo ""
    if wait "${!pid_var}"; then
        echo "--- $name: OK ---"
    else
        echo "--- $name: FAILED ---"
        failed=1
    fi
    cat "$tmpdir/$name.log"
done

if [ "$failed" -ne 0 ]; then
    echo ""
    echo "Build FAILED." >&2
    exit 1
fi

echo ""
echo "Done."
