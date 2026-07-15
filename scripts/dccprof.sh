#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
usage: dccprof.sh name [--source-path FILE] [--build-dir DIR] [--out-dir DIR]
                   [--clock HZ] [--] [program-args...]
    dccprof.sh --help

Builds an app (peep-optimized, via scripts/ma.sh), runs it under ntvcm's
per-PC execution-count profiler, and correlates the result against the
build's .PRN/.SYM listings (scripts/dccprof.py) to produce:
  - <build-dir>/<name>_profile_summary.md  - ranked hot-function table
  - <build-dir>/<name>_profile_app.txt     - the app's own .MAC, annotated
                                              with a per-line hit count
  - <build-dir>/<name>_profile_rtl.txt     - same, for RTL routines that
                                              were actually hit

Open the summary .md directly, or open either annotated .txt listing and
use your editor's search/go-to-line to find a hot address or line called
out in the summary.

options:
  --source-path FILE   explicit C source path (default: tests/<name>.c)
  --build-dir DIR       build artifact directory (default: build/dccprof/<name>)
  --out-dir DIR         where to write the report (default: same as build-dir)
  --clock HZ            ntvcm clock speed in Hz for -s: (default: 0, i.e. as
                        fast as possible - profiling counts executions, not
                        wall time, so this only affects how long the run
                        takes, not the results)
  --                    everything after this is passed as argv to the
                        profiled program itself

examples:
  dccprof.sh tbig
  dccprof.sh tbig -- 20000
  dccprof.sh mm --build-dir /tmp/profmm
EOF
}

case "${1:-}" in
    --help|-h|"")
        usage
        exit 0
        ;;
esac

name_arg="$1"
shift

source_path=""
build_dir=""
out_dir=""
clock_hz="0"
program_args=()

while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)
            usage
            exit 0
            ;;
        --source-path)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            source_path="$2"
            shift 2
            ;;
        --build-dir)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            build_dir="$2"
            shift 2
            ;;
        --out-dir)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            out_dir="$2"
            shift 2
            ;;
        --clock)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            clock_hz="$2"
            shift 2
            ;;
        --)
            shift
            program_args=("$@")
            break
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." && pwd)
cd "$repo_root"

lower_base=$(printf '%s' "$name_arg" | tr '[:upper:]' '[:lower:]')
upper_base=$(printf '%s' "$name_arg" | tr '[:lower:]' '[:upper:]')

if [ -z "$build_dir" ]; then
    build_dir="build/dccprof/$lower_base"
fi
if [ -z "$out_dir" ]; then
    out_dir="$build_dir"
fi
mkdir -p "$build_dir" "$out_dir"

resolve_command() {
    local command_value="$1"
    if command -v "$command_value" >/dev/null 2>&1; then
        command -v "$command_value"
    else
        printf '%s' "$command_value"
    fi
}

M80C="${M80C:-m80c}"
NTVCM="${NTVCM:-ntvcm}"
PYTHON="${PYTHON:-python3}"

echo "=== building $name_arg (peep-optimized) into $build_dir ==="
ma_args=("$name_arg" fast --build-dir "$build_dir")
if [ -n "$source_path" ]; then
    ma_args+=(--source-path "$source_path")
fi
bash "$script_dir/ma.sh" "${ma_args[@]}"

# ma.sh's normal build assembles the app with the /L listing flag (giving
# <NAME>.PRN with addresses) but assembles RTLMIN.MAC without it (RTLMIN.PRN
# is only ever needed for this kind of address correlation, not for a
# normal build) - so it has to be regenerated here, same as this session's
# own ad-hoc profiling investigations needed to do by hand every time.
echo "=== regenerating RTLMIN.PRN (not produced by a normal build) ==="
(
    cd "$build_dir"
    "$(resolve_command "$M80C")" '=RTLMIN.MAC' '/X' '/O' '/Z' '/L'
)

profile_csv="$build_dir/${lower_base}_profile.csv"
rm -f "$profile_csv"

echo "=== running $lower_base under the profiler ==="
(
    cd "$build_dir"
    "$(resolve_command "$NTVCM")" -p -s:"$clock_hz" -g:"$(basename "$profile_csv")" \
        "${lower_base}.com" "${program_args[@]+${program_args[@]}}"
)

if [ ! -s "$profile_csv" ]; then
    echo "error: ntvcm did not produce a profile CSV at $profile_csv" >&2
    echo "(the run may have crashed or been interrupted before exit)" >&2
    exit 1
fi

echo "=== correlating profile against listings ==="
"$(resolve_command "$PYTHON")" "$script_dir/dccprof.py" \
    --app "$lower_base" --build-dir "$build_dir" \
    --profile-csv "$profile_csv" --out-dir "$out_dir"

echo
echo "done: open $out_dir/${lower_base}_profile_summary.md"
