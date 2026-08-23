#!/usr/bin/env bash
# note: this is not the official test script. 
# It's for use on machines without PowerShell (old Raspberry Pi 4, etc.) and is not guaranteed to be kept in sync with the PowerShell version.
# note: this script is intended to be run from the repository root, not from scripts/

set -u
set -o pipefail

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." 2>/dev/null && pwd || pwd)
cd "$repo_root" || exit 1

mode=fast
emulator=${NTVCM:-ntvcm}
build_root=build/runall
baseline_dir=tests/baselines
run_timeout=600
keep_build=0
run_diagnostics=1
use_emulated_m80=0
jobs_count="${RUNALL_JOBS:-}"
no_stack_check=0
global_stack_size="${STACK_SIZE:-}"
ma_script=${DCC_MA:-$script_dir/ma.sh}

usage() {
    cat <<'USAGE'
usage: scripts/runall.sh [options]

  -mode, --mode fast|nopeep|full
                            build mode (default: fast)
  --emulator COMMAND       emulator command (default: ntvcm)
  --build-dir DIR          per-run build root (default: build/runall)
  --baseline-dir DIR       expected output directory (default: tests/baselines)
  --run-timeout SECONDS    per-test execution timeout (default: 600; 0 disables)
  --jobs N                  parallel test workers (default: all available CPUs; 1 is sequential)
  --ma FILE                ma.sh-compatible build helper
  --no-stack-check         do not force compiler stack checking
  --emulated-m80           use M80.COM through the emulator
  --keep-build             retain temporary build directories
  --no-diagnostics         skip run-diagnostics.sh
  -h, --help               show this help

Each test uses a separate build directory. Build and emulator commands run in
separate process groups. Ctrl+C terminates all workers and their descendants.
USAGE
}

while [ $# -gt 0 ]; do
    case "$1" in
        -mode|--mode|-Mode) mode=${2:?missing value for $1}; shift 2 ;;
        --emulator|-Emulator) emulator=${2:?missing value for $1}; shift 2 ;;
        --build-dir|-BuildDir) build_root=${2:?missing value for $1}; shift 2 ;;
        --baseline-dir|-BaselineDir) baseline_dir=${2:?missing value for $1}; shift 2 ;;
        --run-timeout|-RunTimeout) run_timeout=${2:?missing value for $1}; shift 2 ;;
        --jobs|-Jobs|-j) jobs_count=${2:?missing value for $1}; shift 2 ;;
        --ma) ma_script=${2:?missing value for $1}; shift 2 ;;
        --no-stack-check|-NoStackCheck) no_stack_check=1; shift ;;
        --emulated-m80|--use-emulated-m80|-UseEmulatedM80) use_emulated_m80=1; shift ;;
        --keep-build|-KeepBuild) keep_build=1; shift ;;
        --no-diagnostics) run_diagnostics=0; shift ;;
        -h|--help|-Help) usage; exit 0 ;;
        *) echo "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

mode=$(printf '%s' "$mode" | tr '[:upper:]' '[:lower:]')
case "$mode" in
    fast|nopeep|full) ;;
    *) echo "invalid mode: $mode" >&2; exit 2 ;;
esac
case "$run_timeout" in
    ''|*[!0-9]*) echo "invalid timeout: $run_timeout" >&2; exit 2 ;;
esac
if [ -z "$jobs_count" ]; then
    jobs_count=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
    case "$jobs_count" in ''|*[!0-9]*) jobs_count=1 ;; esac
fi
case "$jobs_count" in
    ''|*[!0-9]*|0) echo "invalid jobs count: $jobs_count" >&2; exit 2 ;;
esac

for command_name in find sort perl; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "required command not found: $command_name" >&2
        exit 2
    }
done

# setsid(1) (util-linux) puts a command in a fresh session/process group so
# the whole subtree can be killed at once via "kill -- -PGID". macOS/BSD
# don't ship it, but Perl's POSIX::setsid() gives the same effect (and perl
# is already required above) - exec from within the backgrounded function
# itself, not a plain call, so no extra fork sits between $! and the new
# group's actual PGID.
if command -v setsid >/dev/null 2>&1; then
    setsid_cmd() { setsid "$@"; }
else
    setsid_cmd() {
        exec perl -e 'use POSIX qw(setsid); setsid(); exec { $ARGV[0] } @ARGV or die "exec failed: $!"' -- "$@"
    }
fi

# GNU timeout isn't available on macOS/BSD either (no coreutils by
# default). Rather than reimplementing --foreground/--kill-after semantics,
# per-test timeout enforcement is simply unavailable there: run_timeout=0
# already means "no timeout" (see run_group_with_timeout), so route through
# that same path. Warn once, here, at startup - not inside
# run_group_with_timeout itself, since each per-app test invocation wraps
# that whole function call with "...>output_raw 2>&1", so anything it
# prints (even conditionally) would leak into every test's captured
# output and corrupt the baseline comparison.
have_timeout=1
if ! command -v timeout >/dev/null 2>&1; then
    have_timeout=0
    if [ "$run_timeout" -ne 0 ]; then
        echo "warning: 'timeout' not found; per-test timeouts are disabled on this platform" >&2
    fi
fi

# wait -n (block until any one background job finishes, instead of a named
# one) needs bash 4.3+; macOS ships 3.2. Prefer it wherever it's actually
# available (any normal Linux bash) - only fall back to a short poll loop
# where it genuinely isn't, since the poll adds up to ~0.1s of scheduling
# latency per worker-slot wait, which is measurable in a run with hundreds
# of short tests even though it's negligible for any single one.
have_wait_n=1
if [ "${BASH_VERSINFO[0]}" -lt 4 ] || { [ "${BASH_VERSINFO[0]}" -eq 4 ] && [ "${BASH_VERSINFO[1]}" -lt 3 ]; }; then
    have_wait_n=0
fi
wait_for_a_worker() {
    if [ "$have_wait_n" -eq 1 ]; then
        wait -n "${worker_pids[@]}" 2>/dev/null || true
    else
        sleep 0.1
    fi
}

# Make the selected ma.sh absolute. Workers and ma.sh both enter other
# directories, so no helper or native-tool path may remain relative.
case "$ma_script" in
    /*) ;;
    *) ma_script="$repo_root/${ma_script#./}" ;;
esac
if [ ! -x "$ma_script" ]; then
    echo "build helper is not executable: $ma_script" >&2
    exit 2
fi

# The supplied ma.sh resolves command names through PATH, but an override such
# as M80C=./m80c remains relative and is later invoked after `cd $build_dir`.
# Resolve all path-like tool overrides here and explicitly pass them to ma.sh.
resolve_tool_override() {
    local value=${1-}
    [ -n "$value" ] || return 0
    case "$value" in
        /*) printf '%s' "$value" ;;
        */*) printf '%s/%s' "$repo_root" "${value#./}" ;;
        *) printf '%s' "$value" ;;
    esac
}

resolved_DCC=$(resolve_tool_override "${DCC-}")
resolved_DCCPEEP=$(resolve_tool_override "${DCCPEEP-}")
resolved_DCCRTLSTRIP=$(resolve_tool_override "${DCCRTLSTRIP-}")
resolved_M80C=$(resolve_tool_override "${M80C-}")
resolved_L80C=$(resolve_tool_override "${L80C-}")
resolved_DCC_RUNTIME=$(resolve_tool_override "${DCC_RUNTIME-}")
resolved_DCC_HOME=$(resolve_tool_override "${DCC_HOME-}")

# The repository normally keeps the native assembler/linker beside scripts/.
# Use those executables automatically when M80C/L80C were not exported into
# this script. This is deliberately absolute because ma.sh invokes them
# after cd'ing into a per-test build directory.
if [ -z "$resolved_M80C" ] && [ -x "$repo_root/m80c" ]; then
    resolved_M80C="$repo_root/m80c"
fi
if [ -z "$resolved_L80C" ] && [ -x "$repo_root/l80c" ]; then
    resolved_L80C="$repo_root/l80c"
fi

# Also normalize an inherited bare/relative value that names the repository
# copy.  This covers shells where M80C=./m80c was set but not exported before
# runall.sh was started, as well as wrappers that pass the default name.
case "$resolved_M80C" in
    '' ) ;;
    m80c|./m80c) resolved_M80C="$repo_root/m80c" ;;
esac
case "$resolved_L80C" in
    '' ) ;;
    l80c|./l80c) resolved_L80C="$repo_root/l80c" ;;
esac

if [ -n "$resolved_M80C" ] && [ ! -x "$resolved_M80C" ]; then
    echo "native assembler is not executable: $resolved_M80C" >&2
    exit 2
fi
if [ -n "$resolved_L80C" ] && [ ! -x "$resolved_L80C" ]; then
    echo "native linker is not executable: $resolved_L80C" >&2
    exit 2
fi

emulator_path=$(command -v "$emulator" 2>/dev/null || true)
if [ -z "$emulator_path" ]; then
    case "$emulator" in
        */*) [ -x "$emulator" ] && emulator_path=$(CDPATH= cd -- "$(dirname -- "$emulator")" && printf '%s/%s' "$PWD" "$(basename -- "$emulator")") ;;
    esac
fi
if [ -z "$emulator_path" ]; then
    echo "emulator not found: $emulator" >&2
    exit 2
fi
emulator=$emulator_path

overrides=tests/_test_overrides.json
if ! perl -MJSON::PP -e 1 >/dev/null 2>&1; then
    echo "Perl JSON::PP is required to read $overrides" >&2
    exit 2
fi

# Parse the whole overrides file exactly once, here, rather than re-spawning
# perl -MJSON::PP per app (json_object_for_app) and then again per field
# (json_string/json_bool) on every single run_one_app call - that was up to
# ~9 perl startups per app. Each app's fields come back as one NUL-terminated,
# shell-eval-able block (first line = name, rest = quoted assignments);
# load_app_config below does a pure-bash linear scan over override_app_names
# and evals the matching block - no subprocess per lookup at all.
override_app_names=()
override_app_blocks=()
if [ -f "$overrides" ]; then
    while IFS= read -r -d '' override_block; do
        override_app_names+=("${override_block%%$'\n'*}")
        override_app_blocks+=("$override_block")
    done < <(perl -MJSON::PP -0777 -e '
        use strict;
        use warnings;

        sub shquote {
            my ($s) = @_;
            $s = "" unless defined $s;
            $s =~ s/'"'"'/'"'"'\\'"'"''"'"'/g;
            return "'"'"'" . $s . "'"'"'";
        }
        sub scalar_str {
            my ($v) = @_;
            return "" unless defined $v;
            return ref($v) ? "" : "$v";
        }
        sub bool_str {
            my ($v, $default) = @_;
            return $default ? "true" : "false" unless defined $v;
            return ($v && ref($v) =~ /Boolean/ ? 1 : (!ref($v) ? $v : 0)) ? "true" : "false";
        }

        my $text = <>;
        $text =~ s/^\x{EF}\x{BB}\x{BF}//;
        my $root = decode_json($text);
        exit 0 unless ref($root) eq "HASH" && ref($root->{apps}) eq "ARRAY";

        for my $entry (@{$root->{apps}}) {
            next unless ref($entry) eq "HASH";
            next unless defined($entry->{name});
            my @fix_lines;
            if (ref($entry->{fixtures}) eq "ARRAY") {
                for my $fixture (@{$entry->{fixtures}}) {
                    my ($fname, $fsource) = ("", "");
                    if (!ref($fixture)) { $fname = defined($fixture) ? "$fixture" : ""; }
                    elsif (ref($fixture) eq "HASH") {
                        $fname = defined($fixture->{name}) ? "$fixture->{name}" : "";
                        $fsource = defined($fixture->{source}) ? "$fixture->{source}" : "";
                    }
                    $fname =~ s/[\t\r\n]/ /g;
                    $fsource =~ s/[\t\r\n]/ /g;
                    push @fix_lines, "$fname\t$fsource" if length($fname);
                }
            }
            print "$entry->{name}\n";
            print "ignore=" . shquote(bool_str($entry->{ignore}, 0)) . "\n";
            print "app_timeout=" . shquote(scalar_str($entry->{run_timeout})) . "\n";
            print "run_args=" . shquote(scalar_str($entry->{args})) . "\n";
            print "run_stdin=" . shquote(scalar_str($entry->{stdin})) . "\n";
            print "stack_size=" . shquote(scalar_str($entry->{stack_size})) . "\n";
            print "dcc_args=" . shquote(scalar_str($entry->{dcc_args})) . "\n";
            print "floatio=" . shquote(bool_str($entry->{dcc_floatio}, 1)) . "\n";
            print "longio=" . shquote(bool_str($entry->{dcc_longio}, 1)) . "\n";
            print "fixtures_tsv=" . shquote(join("\n", @fix_lines));
            print "\0";
        }
    ' "$overrides" 2>"/tmp/runall-json-error-$$")
    parse_status=$?
    if [ "$parse_status" -ne 0 ]; then
        echo "cannot parse $overrides with Perl JSON::PP:" >&2
        cat "/tmp/runall-json-error-$$" >&2
        rm -f "/tmp/runall-json-error-$$"
        exit 2
    fi
    rm -f "/tmp/runall-json-error-$$"
fi

# Pure-bash lookup: linear scan over override_app_names, then eval the
# matching pre-quoted block into the caller's already-local variables
# (ignore/app_timeout/run_args/run_stdin/stack_size/dcc_args/floatio/
# longio/fixtures_tsv). Returns 1 (all defaults) if the app has no entry -
# same as json_object_for_app returning "{}" before.
load_app_config() {
    local wanted=$1 idx=0 name
    ignore=false; app_timeout=''; run_args=''; run_stdin=''; stack_size=''
    dcc_args=''; floatio=true; longio=true; fixtures_tsv=''
    for name in "${override_app_names[@]+${override_app_names[@]}}"; do
        if [ "$name" = "$wanted" ]; then
            eval "${override_app_blocks[$idx]#*$'\n'}"
            return 0
        fi
        idx=$((idx + 1))
    done
    return 1
}

run_dir="$build_root/run-$$"
mkdir -p "$run_dir"
saved_stty=$(stty -g 2>/dev/null || true)
active_pgid=''
interrupted=0
worker_pids=()

terminate_active_group() {
    if [ -n "$active_pgid" ]; then
        kill -TERM -- "-$active_pgid" 2>/dev/null || true
        sleep 1
        kill -KILL -- "-$active_pgid" 2>/dev/null || true
        wait "$active_pgid" 2>/dev/null || true
        active_pgid=''
    fi
}

terminate_workers() {
    local pid
    for pid in "${worker_pids[@]+${worker_pids[@]}}"; do
        kill -TERM "$pid" 2>/dev/null || true
    done
    sleep 1
    for pid in "${worker_pids[@]+${worker_pids[@]}}"; do
        kill -KILL "$pid" 2>/dev/null || true
        wait "$pid" 2>/dev/null || true
    done
    worker_pids=()
}

on_signal() {
    interrupted=1
    printf '\nStopping all test workers and child processes...\n' >&2
    terminate_workers
    terminate_active_group
    exit 130
}

cleanup() {
    terminate_workers
    terminate_active_group
    if [ -n "$saved_stty" ]; then
        stty "$saved_stty" 2>/dev/null || true
    fi
    if [ "$keep_build" -eq 0 ]; then
        rm -rf -- "$run_dir"
    else
        echo "Build files retained in $run_dir"
    fi
}

trap on_signal INT TERM HUP
trap cleanup EXIT

run_group() {
    # Run a command in a fresh session/process group and return its status.
    setsid_cmd "$@" &
    active_pgid=$!
    wait "$active_pgid"
    local status=$?
    active_pgid=''
    return "$status"
}

run_group_with_timeout() {
    local seconds=$1
    shift
    if [ "$seconds" -eq 0 ] || [ "$have_timeout" -eq 0 ]; then
        run_group "$@"
        return $?
    fi

    # Put GNU timeout and the command it supervises in one fresh process
    # group. This avoids polling sleeps, leaked watchdogs, and up-to-one-second
    # delays before a worker notices that a test has finished.
    setsid_cmd timeout \
        --foreground \
        --signal=TERM \
        --kill-after=2s \
        "${seconds}s" \
        "$@" &
    active_pgid=$!

    local status
    wait "$active_pgid"
    status=$?
    active_pgid=''
    return "$status"
}

copy_fixtures() {
    # fixtures_tsv is pre-extracted by load_app_config (one "name<TAB>source"
    # line per fixture) - no perl call needed here anymore.
    local tsv=$1 destination=$2
    [ -n "$tsv" ] || return 0

    printf '%s\n' "$tsv" |
    while IFS=$'\t' read -r fixture_name fixture_source; do
        [ -n "$fixture_name" ] || continue
        local source=''
        if [ -n "$fixture_source" ] && [ -f "$fixture_source" ]; then
            source=$fixture_source
        else
            source=$(find tests . -maxdepth 1 -type f -iname "$fixture_name" -print -quit 2>/dev/null || true)
        fi
        if [ -n "$source" ]; then
            cp -f -- "$source" "$destination/$(printf '%s' "$fixture_name" | tr '[:lower:]' '[:upper:]')"
        else
            echo "    WARNING: fixture not found: $fixture_name"
        fi
    done
}

normalize_output() {
    perl -0777 -pe 's/\r\n?/\n/g; s/\n\s*elapsed milliseconds:.*\z//si; s/\n+\z//' "$1"
}

baseline_matches() {
    local expected=$1 actual=$2
    perl -0777 - "$expected" "$actual" <<'PERL'
use strict;
use warnings;
my ($expected_file, $actual_file) = @ARGV;
sub read_normalized {
    my ($file) = @_;
    open my $fh, '<:raw', $file or die "$file: $!";
    local $/;
    my $text = <$fh>;
    $text =~ s/\r\n?/\n/g;
    $text =~ s/\n+\z//;
    return $text;
}
my $expected = read_normalized($expected_file);
my $actual = read_normalized($actual_file);
my %placeholder = (
    DATE => '[A-Z][a-z]{2}\\s+\\d{1,2}\\s+\\d{4}',
    TIME => '\\d{2}:\\d{2}:\\d{2}',
    SEP  => '[/\\\\]',
    UINT => '\\d+',
    HEX4 => '[0-9A-Fa-f]{4}',
);

if ($expected !~ /\{\{[A-Z][A-Z0-9]*\}\}/) {
    exit($expected eq $actual ? 0 : 1);
}

my $regex = '';
my $cursor = 0;
while ($expected =~ /\{\{([A-Z][A-Z0-9]*)\}\}/g) {
    my $name = $1;
    my $begin = $-[0];
    my $finish = $+[0];
    $regex .= quotemeta(substr($expected, $cursor, $begin - $cursor));
    if (exists $placeholder{$name}) {
        $regex .= '(?:' . $placeholder{$name} . ')';
    }
    else {
        $regex .= quotemeta(substr($expected, $begin, $finish - $begin));
    }
    $cursor = $finish;
}
$regex .= quotemeta(substr($expected, $cursor));
exit($actual =~ /\A$regex\z/s ? 0 : 1);
PERL
}
# mapfile/readarray needs bash 4+ (macOS ships 3.2); -printf is a GNU find
# extension BSD find lacks. Both avoided here for portability: a plain
# read loop builds the array, and stripping the directory with sed instead
# of -printf works identically on GNU and BSD find.
applications=()
while IFS= read -r app_name; do
    applications+=("$app_name")
done < <(find tests -maxdepth 1 -type f -name '*.c' | sed 's#.*/##; s/\.c$//' | sort)
if [ "${#applications[@]}" -eq 0 ]; then
    echo "no test applications found in tests" >&2
    exit 2
fi

case "$mode" in
    fast) build_modes=(fast) ;;
    nopeep) build_modes=(nopeep) ;;
    full) build_modes=(fast nopeep) ;;
esac

run_one_app() {
    local app=$1
    local result_file=$2
    local app_ok app_timeout run_args run_stdin stack_size dcc_args floatio longio
    local ignore fixtures_tsv
    local build_mode app_dir build_log com_file output_raw output_clean run_status baseline stdin_file
    local multi_file_arg word
    local -a build_env program_args emulator_args

    load_app_config "$app"

    if [ "$ignore" = true ]; then
        echo "SKIP  $app"
        printf 'SKIP\n' >"$result_file"
        return 0
    fi

    echo "Testing $app..."
    app_ok=1
    case "$app_timeout" in ''|*[!0-9]*) app_timeout=$run_timeout ;; esac
    if [ -n "$global_stack_size" ]; then stack_size=$global_stack_size; fi

    # ma.sh (this script's build helper) invokes dcc directly, a single-file
    # compiler with no concept of extra positional .c files as additional
    # -module inputs to link in - unlike dccmake, which runall.ps1 drives
    # instead. An app whose dcc_args references another .c file (e.g. calc's
    # "tests/calc1024.c tests/calcdoub.c") is a multi-file app that can only
    # build correctly through dccmake, so skip it here rather than failing
    # with a misleading "undefined symbol" from the linker.
    multi_file_arg=""
    for word in $dcc_args; do
        case "$word" in
            *.c|*.C) multi_file_arg=$word; break ;;
        esac
    done
    if [ -n "$multi_file_arg" ]; then
        echo "SKIP  $app (multi-file app: dcc_args references $multi_file_arg," \
             "which ma.sh cannot link in - build via dccmake or runall.ps1 instead)"
        printf 'SKIP\n' >"$result_file"
        return 0
    fi

    for build_mode in "${build_modes[@]}"; do
        app_dir="$run_dir/$app-$build_mode"
        mkdir -p "$app_dir"
        copy_fixtures "$fixtures_tsv" "$app_dir"

        if [ -n "$stack_size" ]; then
            echo "  Building $app ($build_mode, stack=$stack_size)..."
        else
            echo "  Building $app ($build_mode, default stack)..."
        fi
        build_log="$app_dir/build.log"
        build_env=(env)
        [ -n "$resolved_DCC" ] && build_env+=("DCC=$resolved_DCC")
        [ -n "$resolved_DCCPEEP" ] && build_env+=("DCCPEEP=$resolved_DCCPEEP")
        [ -n "$resolved_DCCRTLSTRIP" ] && build_env+=("DCCRTLSTRIP=$resolved_DCCRTLSTRIP")
        [ -n "$resolved_M80C" ] && build_env+=("M80C=$resolved_M80C")
        [ -n "$resolved_L80C" ] && build_env+=("L80C=$resolved_L80C")
        [ -n "$resolved_DCC_RUNTIME" ] && build_env+=("DCC_RUNTIME=$resolved_DCC_RUNTIME")
        [ -n "$resolved_DCC_HOME" ] && build_env+=("DCC_HOME=$resolved_DCC_HOME")
        if [ "$no_stack_check" -eq 1 ]; then
            build_env+=("DCC_FORCE_STACK_CHECK=0")
        else
            build_env+=("DCC_FORCE_STACK_CHECK=1")
        fi
        [ -n "$stack_size" ] && build_env+=("DCC_STACK_SIZE=$stack_size")
        [ -n "$dcc_args" ] && build_env+=("DCC_ARGS=$dcc_args")
        [ "$floatio" = true ] && build_env+=(DCC_FLOATIO=1)
        [ "$longio" = true ] && build_env+=(DCC_LONGIO=1)
        [ "$use_emulated_m80" -eq 1 ] && build_env+=(DCC_USE_EMULATED_M80=1)

        if ! run_group "${build_env[@]}" "$ma_script" "$app" "$build_mode" \
                --build-dir "$app_dir" --emulator "$emulator" >"$build_log" 2>&1; then
            cat "$build_log"
            echo "    ERROR: build failed"
            app_ok=0
            break
        fi

        com_file="$app_dir/$(printf '%s' "$app" | tr '[:lower:]' '[:upper:]').COM"
        if [ ! -f "$com_file" ]; then
            echo "    ERROR: build succeeded but $com_file was not produced"
            app_ok=0
            break
        fi

        echo "  Running $app ($build_mode)..."
        output_raw="$app_dir/output.raw"
        output_clean="$app_dir/output.txt"
        read -r -a program_args <<<"$run_args"
        emulator_args=()
        case "$(basename -- "$emulator")" in ntvcm*) emulator_args=(-p -s:0) ;; esac

        if [ -n "$run_stdin" ]; then
            stdin_file="$app_dir/input.txt"
            printf '%s\n' "$run_stdin" >"$stdin_file"
            run_group_with_timeout "$app_timeout" bash -c '
                directory=$1; input=$2; shift 2
                cd "$directory" || exit 1
                exec "$@" <"$input"
            ' bash "$app_dir" "$(basename -- "$stdin_file")" "$emulator" "${emulator_args[@]+${emulator_args[@]}}" \
                "$(basename -- "$com_file")" "${program_args[@]+${program_args[@]}}" >"$output_raw" 2>&1
            run_status=$?
        else
            run_group_with_timeout "$app_timeout" bash -c '
                directory=$1; shift
                cd "$directory" || exit 1
                exec "$@"
            ' bash "$app_dir" "$emulator" "${emulator_args[@]+${emulator_args[@]}}" \
                "$(basename -- "$com_file")" "${program_args[@]+${program_args[@]}}" >"$output_raw" 2>&1
            run_status=$?
        fi

        normalize_output "$output_raw" >"$output_clean"
        if [ "$run_status" -eq 124 ]; then
            echo "    ERROR: timeout after ${app_timeout}s"
            app_ok=0
            break
        elif [ "$run_status" -ne 0 ]; then
            echo "    NOTE: emulator exited with status $run_status; checking output anyway"
        fi

        baseline="$baseline_dir/$app.txt"
        if [ ! -f "$baseline" ]; then
            echo "    ERROR: missing baseline $baseline"
            app_ok=0
            break
        elif baseline_matches "$baseline" "$output_clean"; then
            echo "    Output matches baseline"
        else
            echo "    OUTPUT MISMATCH"
            diff -u <(normalize_output "$baseline") "$output_clean" || true
            app_ok=0
            break
        fi
    done

    if [ "$app_ok" -eq 1 ]; then
        echo "PASS  $app"
        printf 'PASS\n' >"$result_file"
        return 0
    fi
    echo "FAIL  $app"
    printf 'FAIL\n' >"$result_file"
    return 1
}

printf 'Found %d test applications\n' "${#applications[@]}"
printf 'Mode: %s; timeout: %ss; workers: %s\n' "$mode" "$run_timeout" "$jobs_count"

start_seconds=$SECONDS
log_dir="$run_dir/logs"
result_dir="$run_dir/results"
mkdir -p "$log_dir" "$result_dir"

reap_finished_workers() {
    local -a still_running=()
    local pid
    for pid in "${worker_pids[@]+${worker_pids[@]}}"; do
        if kill -0 "$pid" 2>/dev/null; then
            still_running+=("$pid")
        else
            wait "$pid" 2>/dev/null || true
        fi
    done
    worker_pids=("${still_running[@]+${still_running[@]}}")
}

for app in "${applications[@]}"; do
    while :; do
        reap_finished_workers
        [ "${#worker_pids[@]}" -lt "$jobs_count" ] && break
        wait_for_a_worker
        reap_finished_workers
    done

    echo "START $app"
    (
        active_pgid=''
        trap 'terminate_active_group; exit 130' INT TERM HUP
        trap 'terminate_active_group' EXIT
        run_one_app "$app" "$result_dir/$app"
        status=$?
        case "$(cat "$result_dir/$app" 2>/dev/null || echo FAIL)" in
            PASS) echo "DONE  $app: PASS" >&3 ;;
            SKIP) echo "DONE  $app: SKIP" >&3 ;;
            *)
                echo "DONE  $app: FAIL" >&3
                echo "===== FAILURE LOG: $app =====" >&3
                cat "$log_dir/$app.log" >&3
                ;;
        esac
        exit "$status"
    ) 3>&1 >"$log_dir/$app.log" 2>&1 &
    worker_pids+=("$!")
done

while [ "${#worker_pids[@]}" -gt 0 ]; do
    reap_finished_workers
    if [ "${#worker_pids[@]}" -gt 0 ]; then
        wait_for_a_worker
    fi
done

passed=0
failed=0
skipped=0
failed_apps=()
for app in "${applications[@]}"; do
    result=$(cat "$result_dir/$app" 2>/dev/null || echo FAIL)
    case "$result" in
        PASS) passed=$((passed + 1)) ;;
        SKIP) skipped=$((skipped + 1)) ;;
        *)
            failed=$((failed + 1))
            failed_apps+=("$app")
            ;;
    esac
done

if [ "$run_diagnostics" -eq 1 ] && [ -x "$script_dir/run-diagnostics.sh" ]; then
    echo
    echo "Running diagnostics suite..."
    if run_group "$script_dir/run-diagnostics.sh" --dcc "$repo_root/dcc"; then
        echo "Diagnostics passed"
    else
        echo "Diagnostics failed"
        failed=$((failed + 1))
    fi
fi

echo
echo "========================================"
echo "TEST SUITE SUMMARY"
echo "========================================"
echo "  Total apps:  ${#applications[@]}"
echo "  Passed:      $passed"
echo "  Failed:      $failed"
echo "  Skipped:     $skipped"
echo "  Workers:     $jobs_count"
echo "  Total time:  $((SECONDS - start_seconds))s"
if [ "${#failed_apps[@]}" -gt 0 ]; then
    printf '  Failed apps: %s\n' "${failed_apps[*]}"
fi

if [ "$failed" -eq 0 ]; then
    echo ">>> SUCCESS: All tests passed <<<"
    exit 0
fi
echo ">>> FAILURE: $failed test(s) failed <<<"
exit 1
