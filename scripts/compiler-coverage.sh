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
if [ "$default_clobber_jobs" -gt 8 ]; then
    default_clobber_jobs=8
fi
clobber_jobs=${DCC_COVERAGE_CLOBBER_JOBS:-$default_clobber_jobs}
# Instrumentation and cache-verification probes can make a single compile
# exceed the ordinary release runner's 60-second build budget.
clobber_build_timeout=${DCC_COVERAGE_CLOBBER_BUILD_TIMEOUT:-300}
census_jobs=${DCC_COVERAGE_CENSUS_JOBS:-$jobs}
# The census compiles every app with the instrumented compiler; its standalone
# 20-second per-app default is too short for large apps under that load.
census_timeout=${DCC_COVERAGE_CENSUS_TIMEOUT:-120}
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
validate_jobs DCC_COVERAGE_CLOBBER_BUILD_TIMEOUT "$clobber_build_timeout"
validate_jobs DCC_COVERAGE_CENSUS_JOBS "$census_jobs"
validate_jobs DCC_COVERAGE_CENSUS_TIMEOUT "$census_timeout"
mkdir -p "$build_dir"
if ! mkdir "$build_dir/.coverage-lock" 2>/dev/null; then
    echo "compiler-coverage: build directory is already in use: $build_dir" >&2
    exit 1
fi
trap 'rmdir "$build_dir/.coverage-lock"' EXIT
checkpoint="$repo_root/scripts/coverage-checkpoint.py"

clang_cmd=${CC:-clang}

if ! command -v "$clang_cmd" >/dev/null 2>&1; then
    echo "compiler-coverage: clang compiler not found: $clang_cmd" >&2
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
    --tool "llvm-cov=$(command -v "$llvm_cov")" \
    --tool "llvm-profdata=$(command -v "$llvm_profdata")" \
    --tool "dccmake=$repo_root/dccmake" --tool "dccpeep=$repo_root/dccpeep" \
    --tool "m80c=$repo_root/m80c" --tool "l80c=$repo_root/l80c" \
    --tool "dccrtlstrip=$repo_root/dccrtlstrip" \
    --tool "ntvcm=$(command -v ntvcm)"
cmake -S "$repo_root/src/dcc" -B "$build_dir/cmake" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="$clang_cmd" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
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
python3 scripts/runall.py --mode full --throttle-limit "$jobs"
python3 scripts/runall.py --mode full --no-stack-check --throttle-limit "$jobs"
python3 scripts/runall.py --mode full --extended --throttle-limit "$jobs"
# Exercise the apps affected by the regional regressions against baselines.
regional_apps=tgcdphi,tfracphi,tiyreg,tdmfuse,tlimbsh,tvolopt,tnestfor,tflddecl,tinlinfb,tszswap,too,targs,tdblnot,tsvbmod,tvlaparm,tisnan,tcalb11,tctxflt
DCC_MIR_PREFER_REGIONAL=1 python3 scripts/runall.py \
    --apps "$regional_apps" --mode full --no-stack-check --no-perf-check \
    --throttle-limit "$jobs"
# A lazy wide parameter loads nothing, so homed-lazy must not take it as
# already in HL:DE when a comparison follows it (tlongsub's if_lt).
DCC_MIR_SELECT_CANDIDATE=homed-lazy python3 scripts/runall.py \
    --apps tlongsub --mode full --no-stack-check --no-perf-check \
    --throttle-limit "$jobs"
# A constant argument suffix must not be pushed after a nested call's
# argument the call-argument cache has already staged (texpfsp's main).
DCC_MIR_SELECT_CANDIDATE=spilled-all python3 scripts/runall.py \
    --apps texpfsp --mode full --no-stack-check --no-perf-check \
    --throttle-limit "$jobs"
# A constant return suffix whose branch the boolean-PHI fold removes must
# still produce its 0/1 result (tctxflt's truth_and and truth_or).
DCC_MIR_SELECT_CANDIDATE=spilled-boolean-phi-branch python3 scripts/runall.py \
    --apps tctxflt --mode full --no-stack-check --no-perf-check \
    --throttle-limit "$jobs"
# A spilled `!!` over a value just widened to long must not read its slot
# unwritten (tdblnotw).
DCC_MIR_SELECT_CANDIDATE=spilled-baseline python3 scripts/runall.py \
    --apps tdblnotw --mode full --no-stack-check --no-perf-check \
    --throttle-limit "$jobs"
# A homed wide compare that saves a live register must not push it between a
# stacked left operand and the right one (tregwide).
DCC_MIR_PREFER_REGIONAL=1 python3 scripts/runall.py \
    --apps tregwide --mode full --no-stack-check --no-perf-check \
    --throttle-limit "$jobs"
python3 scripts/run-mir-clobber-tests.py \
    --cases fuzz --jobs "$clobber_jobs" --build-timeout "$clobber_build_timeout" \
    --execution-manifest "$report_dir/mir-clobber-executions.json"
for debug_args in "-g" "-gline" "-g -fstack-check" "-gline -fstack-check"; do
    debug_name=$(printf '%s' "$debug_args" | tr ' -' '__')
    DCC_MIR_REQUIRE_COMPLETE=1 DCC_MIR_REQUIRE_EMIT=1 \
        python3 "$repo_root/scripts/mir-migration-census.py" \
        --compiler "$DCC" \
        --output "$build_dir/debug-census-$debug_name.tsv" \
        --jobs "$census_jobs" --timeout "$census_timeout" \
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
"$llvm_cov" export "$binary_dir/dcc" \
    -object "$build_dir/cmake/mir-verify-test" \
    -object "$build_dir/cmake/mir-scalar-dag-test" \
    -object "$build_dir/cmake/mir-consteval-isolation-test" \
    -object "$build_dir/cmake/mir-vla-smooth-isolation-test" \
    -object "$build_dir/cmake/mir-selector-isolation-test" \
    -instr-profile="$build_dir/dcc.profdata" >"$report_dir/compiler-coverage.json"
"$llvm_cov" report "$binary_dir/dcc" \
    -object "$build_dir/cmake/mir-verify-test" \
    -object "$build_dir/cmake/mir-scalar-dag-test" \
    -object "$build_dir/cmake/mir-consteval-isolation-test" \
    -object "$build_dir/cmake/mir-vla-smooth-isolation-test" \
    -object "$build_dir/cmake/mir-selector-isolation-test" \
    -instr-profile="$build_dir/dcc.profdata" -show-functions \
    "$repo_root"/src/dcc/*.c >"$report_dir/compiler-function-detail.txt"
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
    "$@" "$repo_root/src/dcc/dcc_ast_capture.c" \
    "$repo_root/src/dcc/dcc_ast_classify.c" \
    "$repo_root/src/dcc/dcc_ast_stmt_classify.c" \
    "$repo_root/src/dcc/dcc_ast_support.c" >"$report_dir/ast-mir-function-detail.txt"
require_complete=
allow_incomplete=--allow-incomplete
if [ "${DCC_COVERAGE_REQUIRE_COMPLETE:-0}" = 1 ]; then
    require_complete=--require-complete
    allow_incomplete=
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

python3 "$repo_root/scripts/compiler-function-coverage.py" \
    --compile-commands "$build_dir/cmake/compile_commands.json" \
    --coverage "$report_dir/compiler-coverage.json" \
    --native-report "$report_dir/summary.txt" \
    --output-dir "$report_dir" $allow_incomplete >"$report_dir/compiler-function-summary.txt"
python3 "$checkpoint" report --build-dir "$build_dir"
echo "Whole-compiler source-function summary: $report_dir/compiler-function-summary.txt"
echo "Unfiltered native coverage summary: $report_dir/summary.txt"
echo "Legacy-excluded AST/MIR summary: $report_dir/ast-mir-summary.txt"
echo "AST/MIR source manifest:   $report_dir/ast-mir-sources.txt"
echo "AST/MIR coverage data:     $report_dir/ast-mir-coverage.json"
echo "Function-scoped AST/MIR summary: $report_dir/ast-mir-function-summary.txt"
echo "Compiler coverage HTML:    $report_dir/html/index.html"
