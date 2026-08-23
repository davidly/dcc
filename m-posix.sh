#!/bin/sh
#
# m-posix.sh - build every dcc host tool without requiring PowerShell.
#
# scripts/build-dcc.ps1 (invoked by m.sh/m.bat) is the canonical build and
# needs PowerShell 7, which isn't packaged at all for some platforms this
# project targets - RISC-V64 boards, Raspberry Pi OS on a Pi 4, etc. This
# script builds the same six C tools build-dcc.ps1 does - dcc, dccpeep,
# dccrtlstrip, dccmake, m80c, l80c - using nothing but /bin/sh, a C
# compiler, and GNU findutils' xargs (both already required by
# src/dcc/build-dcc.sh, which this script's compile/link strategy mirrors).
#
# It also attempts the CMake-based dcc-debug-host and its example I/O
# adapter, best-effort: if cmake or a C++ compiler isn't available, that
# step is skipped with a clear message rather than failing the whole build,
# since the six C tools above are what's needed to actually use dcc.
#
# Usage:
#   sh m-posix.sh
#   CC=clang sh m-posix.sh
#   sh m-posix.sh -NoStatic       # link dynamically instead of -static (Linux)
#
# Output matches build-dcc.ps1: final tools/binaries land at the repo root,
# intermediate object files under ./build/<target>/.

set -eu

REPO_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SRC="$REPO_ROOT/src"
BUILD_ROOT="$REPO_ROOT/build"
TARGETS="dcc dccpeep dccrtlstrip dccmake m80c l80c"

NOSTATIC=0
for arg in "$@"; do
    case "$arg" in
        -NoStatic|--no-static|-nostatic)
            NOSTATIC=1
            ;;
        *)
            echo "m-posix.sh: unknown argument '$arg'" >&2
            echo "usage: sh m-posix.sh [-NoStatic]" >&2
            exit 1
            ;;
    esac
done

if [ -z "${CC:-}" ]; then
    case "$(uname)" in
        Darwin) CC=clang ;;
        *)      CC=gcc ;;
    esac
fi

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "C compiler '$CC' was not found." >&2
    case "$(uname)" in
        Darwin)
            echo "" >&2
            echo "Install Apple's Command Line Tools, then rerun this script:" >&2
            echo "    xcode-select --install" >&2
            ;;
        *)
            echo "" >&2
            echo "Install a C build toolchain, then rerun this script. Common Linux commands:" >&2
            echo "    Debian/Ubuntu/Raspberry Pi OS: sudo apt update && sudo apt install build-essential" >&2
            echo "    Fedora/RHEL:                   sudo dnf groupinstall \"Development Tools\"" >&2
            echo "    Arch:                          sudo pacman -S base-devel" >&2
            echo "    Alpine:                        sudo apk add build-base" >&2
            ;;
    esac
    exit 1
fi

CC_VERSION_LINE=$("$CC" --version 2>&1 | head -n 1) || {
    echo "C compiler '$CC' failed to run ('$CC --version' exited nonzero)." >&2
    exit 1
}
echo "C compiler: $CC_VERSION_LINE"

if [ -z "${CFLAGS:-}" ]; then
    # Same portable C11 baseline as build-dcc.ps1/build-dcc.sh: these are
    # host build tools, not the Z80 target, so a plain -O2 -g is plenty.
    CFLAGS="-std=c11 -w -O2 -g"
    if [ "$(uname)" = "Darwin" ]; then
        CFLAGS="$CFLAGS -fno-common"
    fi
    case "$CC_VERSION_LINE" in
        *gcc*|*GCC*)
            # gcc's conservative _FORTIFY_SOURCE/indentation heuristics warn
            # on patterns clang/MSVC don't; -w above already silences all
            # warnings, so this only matters if a caller overrides CFLAGS
            # without -w. Harmless either way.
            CFLAGS="$CFLAGS -Wno-misleading-indentation -Wno-format-overflow -Wno-stringop-truncation"
            ;;
    esac
fi

LINKFLAGS=""
if [ "$(uname)" = "Linux" ] && [ "$NOSTATIC" -eq 0 ]; then
    # Purely so the resulting binaries are copyable/runnable on a different
    # Linux box without matching the exact glibc version - not something
    # Apple's libSystem supports, so this only applies on Linux. Pass
    # -NoStatic if the static libc dev package (e.g. glibc-static) isn't
    # installed.
    LINKFLAGS="-static"
fi

if [ -z "${JOBS:-}" ]; then
    if command -v nproc >/dev/null 2>&1; then
        JOBS=$(nproc)
    elif command -v sysctl >/dev/null 2>&1; then
        JOBS=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)
    else
        JOBS=4
    fi
fi

echo "Build artifacts will go to: build"
echo "Commands will be placed in: $REPO_ROOT"

# Clean stray object files left at the repo root or directly under a
# target's source directory by an old non-out-of-tree build.
for pattern in '*.o'; do
    find "$REPO_ROOT" -maxdepth 1 -name "$pattern" -type f -delete 2>/dev/null || true
    find "$SRC" -maxdepth 2 -name "$pattern" -type f -delete 2>/dev/null || true
done

mkdir -p "$BUILD_ROOT"
OBJDIR=$(mktemp -d "${TMPDIR:-/tmp}/dcc-build-posix.XXXXXX")
trap 'rm -rf "$OBJDIR"' EXIT

echo ""
echo "=== Compiling dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c in parallel ==="

# Flatten every target's source files into one job list instead of finishing
# one target fully before starting the next, so the five single/few-file
# tools (dccpeep, dccrtlstrip, dccmake, m80c, l80c) don't wait behind dcc's
# ~56 files - they compile alongside it, sharing every core.
JOBLIST="$OBJDIR/compile-jobs.txt"
: > "$JOBLIST"
for target in $TARGETS; do
    mkdir -p "$OBJDIR/$target" "$BUILD_ROOT/$target"
    for srcfile in "$SRC/$target"/*.c; do
        echo "$target $srcfile" >> "$JOBLIST"
    done
done

xargs -P "$JOBS" -L 1 sh -c "
    target=\$1; srcfile=\$2
    base=\$(basename \"\$srcfile\" .c)
    $CC $CFLAGS -I \"$SRC/\$target\" -c \"\$srcfile\" -o \"$OBJDIR/\$target/\$base.o\"
" _ < "$JOBLIST"

# Object files are kept under build/<target>/ afterward (matching
# build-dcc.ps1's intermediate-artifact convention) rather than left in the
# tempdir that's about to be removed.
for target in $TARGETS; do
    cp "$OBJDIR/$target"/*.o "$BUILD_ROOT/$target/" 2>/dev/null || true
done

echo ""
echo "=== Linking dcc, dccpeep, dccrtlstrip, dccmake, m80c, l80c in parallel ==="

LINKLIST="$OBJDIR/link-jobs.txt"
: > "$LINKLIST"
for target in $TARGETS; do
    echo "$target" >> "$LINKLIST"
done

xargs -P "$JOBS" -L 1 sh -c "
    target=\$1
    $CC $CFLAGS \"$OBJDIR/\$target\"/*.o $LINKFLAGS -o \"$REPO_ROOT/\$target\"
" _ < "$LINKLIST"

EXECUTABLES=""
for target in $TARGETS; do
    EXECUTABLES="$EXECUTABLES $REPO_ROOT/$target"
done

# Best-effort: the CMake-based C++ debugger host and its example I/O
# adapter aren't needed to use dcc itself, and pull in a second toolchain
# (cmake + a C++17 compiler) that a minimal board image may not have. Skip
# with a clear message instead of failing the whole build.
DEBUG_HOST_BUILT=0
if command -v cmake >/dev/null 2>&1; then
    echo ""
    echo "=== Building dcc-debug-host and example I/O adapter ==="
    DEBUG_HOST_SRC="$SRC/dcc_debug_host"
    DEBUG_HOST_BUILD="$BUILD_ROOT/dcc_debug_host"
    mkdir -p "$DEBUG_HOST_BUILD"
    DEBUG_HOST_OUT="$REPO_ROOT/dcc-debug-host"
    if [ "$(uname)" = "Darwin" ]; then
        ADAPTER_OUT="$REPO_ROOT/libdcc-debug-io-adapter-example.dylib"
    else
        ADAPTER_OUT="$REPO_ROOT/libdcc-debug-io-adapter-example.so"
    fi
    rm -f "$DEBUG_HOST_OUT" "$ADAPTER_OUT"

    if cmake -S "$DEBUG_HOST_SRC" -B "$DEBUG_HOST_BUILD" \
            -DBUILD_TESTING=OFF \
            -DDCC_DEBUG_HOST_BUILD_EXAMPLES=ON \
            -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE="$REPO_ROOT" \
            -DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE="$REPO_ROOT" \
            >"$DEBUG_HOST_BUILD/configure.log" 2>&1 \
       && cmake --build "$DEBUG_HOST_BUILD" --config Release \
            --target dcc-debug-host dcc-debug-io-adapter-example \
            >"$DEBUG_HOST_BUILD/build.log" 2>&1; then
        if [ -f "$DEBUG_HOST_OUT" ] && [ -f "$ADAPTER_OUT" ]; then
            EXECUTABLES="$EXECUTABLES $DEBUG_HOST_OUT $ADAPTER_OUT"
            DEBUG_HOST_BUILT=1
        else
            echo "dcc-debug-host build reported success but artifacts are missing - see:" >&2
            echo "    $DEBUG_HOST_BUILD/configure.log" >&2
            echo "    $DEBUG_HOST_BUILD/build.log" >&2
        fi
    else
        echo "dcc-debug-host build failed (not required to use dcc) - see:" >&2
        echo "    $DEBUG_HOST_BUILD/configure.log" >&2
        echo "    $DEBUG_HOST_BUILD/build.log" >&2
    fi
else
    echo ""
    echo "cmake not found - skipping dcc-debug-host and its example I/O adapter" \
         "(not required to use dcc)."
    echo "Install it to also build the debugger host, e.g.:" \
         "sudo apt install cmake g++"
fi

echo ""
echo "=== Build complete ==="
echo "Root outputs:"
for executable in $EXECUTABLES; do
    echo "  $executable"
done
echo "Intermediate build artifacts: build"
if [ "$DEBUG_HOST_BUILT" -eq 0 ] && command -v cmake >/dev/null 2>&1; then
    echo ""
    echo "Note: dcc-debug-host was attempted but did not produce output; see logs above."
fi
