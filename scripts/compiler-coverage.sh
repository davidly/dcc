#!/bin/sh
# Build an instrumented dcc, exercise the full main and C11 extended suites,
# and write LLVM line/branch coverage reports without replacing the normal
# compiler binary in the repository root.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${DCC_COVERAGE_BUILD_DIR:-"$repo_root/build/compiler-coverage"}
binary_dir="$build_dir/bin"
raw_dir="$build_dir/raw"
report_dir="$build_dir/report"

clang_cmd=${CC:-clang}
pwsh_cmd=${PWSH:-pwsh}

if ! command -v "$clang_cmd" >/dev/null 2>&1; then
    echo "compiler-coverage: clang compiler not found: $clang_cmd" >&2
    exit 1
fi
if ! command -v "$pwsh_cmd" >/dev/null 2>&1; then
    echo "compiler-coverage: PowerShell not found: $pwsh_cmd" >&2
    exit 1
fi

llvm_cov=${LLVM_COV:-}
llvm_profdata=${LLVM_PROFDATA:-}
if [ -z "$llvm_cov" ] && command -v llvm-cov >/dev/null 2>&1; then
    llvm_cov=$(command -v llvm-cov)
fi
if [ -z "$llvm_profdata" ] && command -v llvm-profdata >/dev/null 2>&1; then
    llvm_profdata=$(command -v llvm-profdata)
fi
if [ -z "$llvm_cov" ] && command -v xcrun >/dev/null 2>&1; then
    llvm_cov=$(xcrun --find llvm-cov)
fi
if [ -z "$llvm_profdata" ] && command -v xcrun >/dev/null 2>&1; then
    llvm_profdata=$(xcrun --find llvm-profdata)
fi
if [ -z "$llvm_cov" ] || [ -z "$llvm_profdata" ]; then
    echo "compiler-coverage: llvm-cov and llvm-profdata are required" >&2
    exit 1
fi

cmake -S "$repo_root/src/dcc" -B "$build_dir/cmake" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="$clang_cmd" \
    -DDCC_ENABLE_COVERAGE=ON \
    -DDCC_RUNTIME_OUTPUT_DIRECTORY="$binary_dir"
cmake --build "$build_dir/cmake" --parallel

mkdir -p "$raw_dir" "$report_dir"
find "$raw_dir" -type f -name '*.profraw' -delete

export DCC="$binary_dir/dcc"
export LLVM_PROFILE_FILE="$raw_dir/dcc-%p.profraw"

cd "$repo_root"
"$pwsh_cmd" -NoProfile -File scripts/runall.ps1 -Mode full
"$pwsh_cmd" -NoProfile -File scripts/runall-extended.ps1 -C11 -Mode full

set -- "$raw_dir"/*.profraw
if [ ! -e "$1" ]; then
    echo "compiler-coverage: no raw profiles were produced" >&2
    exit 1
fi
"$llvm_profdata" merge -sparse "$raw_dir"/*.profraw -o "$build_dir/dcc.profdata"
"$llvm_cov" report "$binary_dir/dcc" \
    -instr-profile="$build_dir/dcc.profdata" \
    "$repo_root"/src/dcc/*.c >"$report_dir/summary.txt"
"$llvm_cov" show "$binary_dir/dcc" \
    -instr-profile="$build_dir/dcc.profdata" \
    -format=html \
    -output-dir="$report_dir/html" \
    -show-branches=count \
    "$repo_root"/src/dcc/*.c

echo "Compiler coverage summary: $report_dir/summary.txt"
echo "Compiler coverage HTML:    $report_dir/html/index.html"
