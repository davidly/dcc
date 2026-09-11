#!/bin/sh
# Build an instrumented dcc, exercise the full main and C11 extended suites,
# and write LLVM line/branch coverage reports without replacing the normal
# compiler binary in the repository root.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${DCC_COVERAGE_BUILD_DIR:-"$repo_root/build/compiler-coverage"}
case "$build_dir" in
    /*) ;;
    *) build_dir="$repo_root/$build_dir" ;;
esac
binary_dir="$build_dir/bin"
raw_dir="$build_dir/raw"
report_dir="$build_dir/report"

stage=${DCC_COVERAGE_STAGE:-all}
detected_jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || true)
case "$detected_jobs" in
    ''|*[!0-9]*|0*) detected_jobs=8 ;;
esac
jobs=${DCC_COVERAGE_JOBS:-$detected_jobs}
default_clobber_jobs=$jobs
if [ "$default_clobber_jobs" -gt 4 ]; then
    default_clobber_jobs=4
fi
clobber_jobs=${DCC_COVERAGE_CLOBBER_JOBS:-$default_clobber_jobs}
mutation_jobs=${DCC_COVERAGE_MUTATION_JOBS:-$jobs}
census_jobs=${DCC_COVERAGE_CENSUS_JOBS:-$jobs}
case "$stage" in
    all|build|collect|report) ;;
    *) echo "compiler-coverage: stage must be all, build, collect, or report" >&2; exit 1 ;;
esac
validate_jobs() {
    case "$2" in
        ''|*[!0-9]*|0*)
            echo "compiler-coverage: $1 must be a positive integer" >&2
            exit 1
            ;;
    esac
}
validate_jobs DCC_COVERAGE_JOBS "$jobs"
validate_jobs DCC_COVERAGE_CLOBBER_JOBS "$clobber_jobs"
validate_jobs DCC_COVERAGE_MUTATION_JOBS "$mutation_jobs"
validate_jobs DCC_COVERAGE_CENSUS_JOBS "$census_jobs"
mkdir -p "$build_dir"
if ! mkdir "$build_dir/.coverage-lock" 2>/dev/null; then
    echo "compiler-coverage: build directory is already in use: $build_dir" >&2
    exit 1
fi
trap 'rmdir "$build_dir/.coverage-lock"' EXIT
checkpoint="$repo_root/scripts/coverage-checkpoint.py"

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

coverage_sources=$(sh "$repo_root/scripts/coverage-sources.sh")
python3 "$repo_root/scripts/ast-function-coverage.py" --clang "$clang_cmd"

if [ "$stage" = all ] || [ "$stage" = build ]; then
python3 "$checkpoint" prepare --build-dir "$build_dir" \
    --tool "clang=$(command -v "$clang_cmd")" \
    --tool "pwsh=$(command -v "$pwsh_cmd")" \
    --tool "llvm-cov=$(command -v "$llvm_cov")" \
    --tool "llvm-profdata=$(command -v "$llvm_profdata")" \
    --tool "dccmake=$repo_root/dccmake" --tool "dccpeep=$repo_root/dccpeep" \
    --tool "m80c=$repo_root/m80c" --tool "l80c=$repo_root/l80c" \
    --tool "dccrtlstrip=$repo_root/dccrtlstrip" \
    --tool "ntvcm=$(command -v ntvcm)"
cmake -S "$repo_root/src/dcc" -B "$build_dir/cmake" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="$clang_cmd" \
    -DDCC_ENABLE_COVERAGE=ON \
    -DDCC_BUILD_MIR_TESTS=ON \
    -DDCC_RUNTIME_OUTPUT_DIRECTORY="$binary_dir"
cmake --build "$build_dir/cmake" --parallel "$jobs"
python3 "$checkpoint" built --build-dir "$build_dir" \
    --tool "dcc=$binary_dir/dcc" \
    --tool "host=$build_dir/cmake/mir-verify-test" \
    --tool "host-scalar-dag=$build_dir/cmake/mir-scalar-dag-test" \
    --tool "host-consteval=$build_dir/cmake/mir-consteval-isolation-test" \
    --tool "host-vla-smooth=$build_dir/cmake/mir-vla-smooth-isolation-test" \
    --tool "host-selector-isolation=$build_dir/cmake/mir-selector-isolation-test"
fi
if [ "$stage" = build ]; then
    echo "Coverage build checkpoint: $build_dir/build.json"
    exit 0
fi

if [ "$stage" = all ] || [ "$stage" = collect ]; then
python3 "$checkpoint" start --build-dir "$build_dir"
mkdir -p "$raw_dir" "$report_dir"
find "$raw_dir" -type f -name '*.profraw' -delete

export DCC="$binary_dir/dcc"
# A long corpus can reuse short-lived compiler PIDs. `%p-%m` then overwrites
# an earlier profile with the same PID/signature and makes totals depend on
# process scheduling. LLVM's `%Nm` form merges concurrently into an N-file
# signature-keyed pool instead.
export LLVM_PROFILE_FILE="$raw_dir/dcc-%8m.profraw"

cd "$repo_root"
"$pwsh_cmd" -NoProfile -File scripts/runall.ps1 -Mode full -ThrottleLimit "$jobs"
"$pwsh_cmd" -NoProfile -File scripts/runall.ps1 -Mode full -NoStackCheck -ThrottleLimit "$jobs"
"$pwsh_cmd" -NoProfile -File scripts/runall-extended.ps1 -C11 -Mode full -ThrottleLimit "$jobs"
"$pwsh_cmd" -NoProfile -File scripts/run-mir-clobber-tests.ps1 \
    -Jobs "$clobber_jobs" -ExecutionManifest "$report_dir/mir-clobber-executions.json"
"$pwsh_cmd" -NoProfile -File scripts/run-mir-lifetime-tests.ps1
"$pwsh_cmd" -NoProfile -File scripts/test-mir-require-emit.ps1 -Dcc "$DCC"
"$pwsh_cmd" -NoProfile -File scripts/test-ast-dump.ps1 -Dcc "$DCC"
"$pwsh_cmd" -NoProfile -File scripts/test-mir-candidate-matrix.ps1 -Dcc "$DCC"
"$pwsh_cmd" -NoProfile -File scripts/test-mir-pointer-condition-mutations.ps1 -Dcc "$DCC"
"$pwsh_cmd" -NoProfile -File scripts/test-mir-scope-block-mutations.ps1 -Dcc "$DCC"
campaign_jobs=$(( (mutation_jobs + 4) / 5 ))
campaign_dir="$report_dir/mutation-campaigns"
mkdir -p "$campaign_dir"
LLVM_PROFILE_FILE="$raw_dir/dcc-endgame-%8m.profraw" \
    python3 scripts/endgame-scope-wave18-campaign.py \
    --jobs "$campaign_jobs" >"$campaign_dir/endgame.log" 2>&1 &
endgame_pid=$!
LLVM_PROFILE_FILE="$raw_dir/dcc-directory-%8m.profraw" \
    python3 scripts/directory-wave19-campaign.py \
    --jobs "$campaign_jobs" --skip-runtime \
    >"$campaign_dir/directory.log" 2>&1 &
directory_pid=$!
LLVM_PROFILE_FILE="$raw_dir/dcc-softmax-%8m.profraw" \
    python3 scripts/softmax-wave19-campaign.py \
    --jobs "$campaign_jobs" >"$campaign_dir/softmax.log" 2>&1 &
softmax_pid=$!
# Compile-only audits; runtime controls already ran through the clobber suite.
LLVM_PROFILE_FILE="$raw_dir/dcc-byte-math-%8m.profraw" \
    python3 scripts/byte-math-wave19-audit.py \
    --jobs "$campaign_jobs" \
    --output-dir "$build_dir/byte-math-wave19-audit" \
    >"$campaign_dir/byte-math.log" 2>&1 &
byte_math_pid=$!
LLVM_PROFILE_FILE="$raw_dir/dcc-multidim-%8m.profraw" \
    python3 scripts/multidim-wave19-audit.py \
    --jobs "$campaign_jobs" \
    --output-dir "$build_dir/multidim-wave19-audit" \
    >"$campaign_dir/multidim.log" 2>&1 &
multidim_pid=$!
campaign_status=0
for campaign in \
    "endgame:$endgame_pid" \
    "directory:$directory_pid" \
    "softmax:$softmax_pid" \
    "byte-math:$byte_math_pid" \
    "multidim:$multidim_pid"
do
    campaign_name=${campaign%%:*}
    campaign_pid=${campaign#*:}
    if wait "$campaign_pid"; then
        cat "$campaign_dir/$campaign_name.log"
    else
        cat "$campaign_dir/$campaign_name.log" >&2
        campaign_status=1
    fi
done
if [ "$campaign_status" -ne 0 ]; then
    echo "compiler-coverage: one or more mutation campaigns failed" >&2
    exit 1
fi
env -u DCC_MIR_MACHINE_MUTATE DCC_MIR_MACHINE_MUTATE_FUNCTION=main \
    "$DCC" -c "$repo_root/tests/mir-clobber/logserie.c" \
    -o "$build_dir/no-machine-mutation.MAC"
for debug_args in "-g" "-gline" "-g -fstack-check" "-gline -fstack-check"; do
    debug_name=$(printf '%s' "$debug_args" | tr ' -' '__')
    DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
        python3 "$repo_root/scripts/mir-migration-census.py" \
        --compiler "$DCC" \
        --output "$build_dir/debug-census-$debug_name.tsv" \
        --jobs "$census_jobs" \
        --extra-args="$debug_args"
done
ctest --test-dir "$build_dir/cmake" --parallel "$jobs" --output-on-failure
python3 "$checkpoint" collected --build-dir "$build_dir"
fi
if [ "$stage" = collect ]; then
    echo "Coverage collection checkpoint: $build_dir/collection.json"
    exit 0
fi

python3 "$checkpoint" report --build-dir "$build_dir"
set -- "$raw_dir"/*.profraw
if [ ! -e "$1" ]; then
    echo "compiler-coverage: no raw profiles were produced" >&2
    exit 1
fi
"$llvm_profdata" merge -sparse "$raw_dir"/*.profraw -o "$build_dir/dcc.profdata"
"$llvm_cov" report "$binary_dir/dcc" \
    -object "$build_dir/cmake/mir-verify-test" \
    -object "$build_dir/cmake/mir-scalar-dag-test" \
    -object "$build_dir/cmake/mir-consteval-isolation-test" \
    -object "$build_dir/cmake/mir-vla-smooth-isolation-test" \
    -object "$build_dir/cmake/mir-selector-isolation-test" \
    -instr-profile="$build_dir/dcc.profdata" \
    "$repo_root"/src/dcc/*.c >"$report_dir/summary.txt"
set --
while IFS= read -r source; do
    set -- "$@" "$source"
done <<EOF
$coverage_sources
EOF
printf '%s\n' "$@" >"$report_dir/ast-mir-sources.txt"
"$llvm_cov" report "$binary_dir/dcc" \
    -object "$build_dir/cmake/mir-verify-test" \
    -object "$build_dir/cmake/mir-scalar-dag-test" \
    -object "$build_dir/cmake/mir-consteval-isolation-test" \
    -object "$build_dir/cmake/mir-vla-smooth-isolation-test" \
    -object "$build_dir/cmake/mir-selector-isolation-test" \
    -instr-profile="$build_dir/dcc.profdata" \
    "$@" >"$report_dir/ast-mir-summary.txt"
"$llvm_cov" export "$binary_dir/dcc" \
    -object "$build_dir/cmake/mir-verify-test" \
    -object "$build_dir/cmake/mir-scalar-dag-test" \
    -object "$build_dir/cmake/mir-consteval-isolation-test" \
    -object "$build_dir/cmake/mir-vla-smooth-isolation-test" \
    -object "$build_dir/cmake/mir-selector-isolation-test" \
    -instr-profile="$build_dir/dcc.profdata" \
    "$@" >"$report_dir/ast-mir-coverage.json"
python3 "$repo_root/scripts/ast-function-coverage.py" --clang "$clang_cmd" \
    --coverage "$report_dir/ast-mir-coverage.json" \
    --allowlist "$report_dir/ast-mir-functions.txt"
"$llvm_cov" report "$binary_dir/dcc" \
    -object "$build_dir/cmake/mir-verify-test" \
    -object "$build_dir/cmake/mir-scalar-dag-test" \
    -object "$build_dir/cmake/mir-consteval-isolation-test" \
    -object "$build_dir/cmake/mir-vla-smooth-isolation-test" \
    -object "$build_dir/cmake/mir-selector-isolation-test" \
    -instr-profile="$build_dir/dcc.profdata" \
    -name-allowlist="$report_dir/ast-mir-functions.txt" -show-functions \
    "$@" "$repo_root"/src/dcc/dcc_ast_gen*.c >"$report_dir/ast-mir-function-detail.txt"
require_complete=
if [ "${DCC_COVERAGE_REQUIRE_COMPLETE:-0}" = 1 ]; then
    require_complete=--require-complete
fi
python3 "$repo_root/scripts/ast-function-coverage.py" --clang "$clang_cmd" \
    --coverage "$report_dir/ast-mir-coverage.json" \
    --allowlist "$report_dir/ast-mir-functions.txt" \
    --native-report "$report_dir/ast-mir-function-detail.txt" \
    --summary "$report_dir/ast-mir-function-coverage.json" \
    --gaps "$report_dir/ast-mir-gaps.json" \
    $require_complete \
    >"$report_dir/ast-mir-function-summary.txt"
"$llvm_cov" show "$binary_dir/dcc" \
    -object "$build_dir/cmake/mir-verify-test" \
    -object "$build_dir/cmake/mir-scalar-dag-test" \
    -object "$build_dir/cmake/mir-consteval-isolation-test" \
    -object "$build_dir/cmake/mir-vla-smooth-isolation-test" \
    -object "$build_dir/cmake/mir-selector-isolation-test" \
    -instr-profile="$build_dir/dcc.profdata" \
    -format=html \
    -output-dir="$report_dir/html" \
    -show-branches=count \
    "$repo_root"/src/dcc/*.c

python3 "$checkpoint" report --build-dir "$build_dir"
echo "Unfiltered collection summary (not a target): $report_dir/summary.txt"
echo "Legacy-excluded AST/MIR summary: $report_dir/ast-mir-summary.txt"
echo "AST/MIR source manifest:   $report_dir/ast-mir-sources.txt"
echo "AST/MIR coverage data:     $report_dir/ast-mir-coverage.json"
echo "Function-scoped AST/MIR summary: $report_dir/ast-mir-function-summary.txt"
echo "Compiler coverage HTML:    $report_dir/html/index.html"
