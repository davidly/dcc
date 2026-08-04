#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'EOF'
usage: dcc-ma name [full|fast|nopeep] [--build-dir DIR] [--source-path FILE] [--emulator COMMAND] [--emulated-m80] [--emulated-l80]
    dcc-ma --help

examples:
  dcc-ma sieve fast
  dcc-ma hello --source-path ./hello.c --build-dir build
  DCC_STACK_SIZE=1024 dcc-ma sieve fast
  DCC_ARGS="-DDEBUG=1" NTVCM_ARGS="-p -s:4000000" dcc-ma hello fast
  dcc-ma sieve fast --emulated-m80   # assemble with real M80.COM under ntvcm instead of native m80c
  dcc-ma sieve fast --emulated-l80   # link with real L80.COM under ntvcm instead of native l80c

build modes:
    fast       run dccpeep after dcc (default)
    nopeep     skip dccpeep
    full       build fast then nopeep, in that order - since both write to
               the same output path, nopeep's un-optimized build is what
               is left behind; mainly useful to see both dccpeep and no-
               dccpeep console output in one run, not to keep both files

script options:
    --source-path FILE  explicit C source path
    --build-dir DIR     build artifact directory (default: build)
    --emulator COMMAND  emulator command used for CP/M tools (default: ntvcm)
    --emulated-m80      assemble with real M80.COM under ntvcm instead of
                        native m80c (default); same as DCC_USE_EMULATED_M80=1
    --emulated-l80      link with real L80.COM under ntvcm instead of native
                        l80c (default); same as DCC_USE_EMULATED_L80=1. Real
                        L80 runs inside ntvcm's emulated 64K CP/M address
                        space, so its own symbol/relocation workspace can run
                        out of memory on large nopeep builds well before the
                        target program itself would not fit - l80c has no
                        such ceiling.
    --mode MODE         full, fast, or nopeep
    --help, -h          show this help

dcc pipeline:
    dcc -> dccpeep (fast mode) -> m80c -> dccrtlstrip -> m80c -> l80c
    (or ntvcm M80.COM/L80.COM in place of m80c/l80c, with --emulated-m80/--emulated-l80)

dcc options controlled by this helper:
    dcc option                  how to set it
    -I <dir>                    DCC_INCLUDE, path-separator separated; package include/ is added automatically
    -D<name>[=value], -U<name>  DCC_ARGS="-DNAME=1 -UOLD"
    -s, -stack <bytes>          DCC_STACK_SIZE=<bytes>; omitted when unset
    -fstack-check               DCC_FORCE_STACK_CHECK=1 (or #pragma stack_check(on) in the source)
    -f, -ffloatio               DCC_FLOATIO=1
    -fl, -flongio               DCC_LONGIO=1
    -o <file>                   managed by dcc-ma
    input.c                     selected by name or --source-path

dccpeep options controlled by this helper:
    dccpeep option               how to set it
    -fundocumented-z80            DCC_ALLOW_UNDOCUMENTED_Z80=1; off by default. Allows
                                   peephole passes that rely on undocumented Z80 opcodes
                                   (IYH/IYL register promotion for byte loop counters).
                                   These are well-established on real NMOS Z80 silicon
                                   and its common clones, and verified under ntvcm, but
                                   are not part of the documented Z80 instruction set.

dcc options not suitable for dcc-ma:
    -c, -module                 use a manual dcc/M80/L80 pipeline for multi-module builds
    -v, --version, -h, --help   run dcc directly

tool and asset overrides:
    DCC, DCCPEEP, DCCRTLSTRIP   host tool paths or command names
    M80C                        native assembler command (default: m80c); used unless
                                 --emulated-m80/DCC_USE_EMULATED_M80=1 selects real M80.COM
    L80C                        native linker command (default: l80c); used unless
                                 --emulated-l80/DCC_USE_EMULATED_L80=1 selects real L80.COM
    NTVCM, M80, L80             emulator and CP/M tool command names (M80/L80 emulated-path only)
    DCC_HOME                    package/install root for bin/, include/, lib/, m80.com, l80.com
    DCC_LIB                     extra runtime/tool asset roots, path-separator separated
    DCC_RUNTIME                 explicit DCCRTL.MAC path

environment:
    DCC_ARGS                    extra whitespace-separated dcc options
    NTVCM_ARGS                  extra whitespace-separated ntvcm options before M80/L80

ntvcm options:
    NTVCM_ARGS="-p -s:4000000"  add ntvcm options before M80/L80
    Common ntvcm options: -p performance, -s:X clock Hz, -t trace, -i instruction trace,
    -8 use 8080 instruction set, -f:<file> keystroke input, -V version.
EOF
}

if [ $# -lt 1 ]; then
    usage
    exit 1
fi

case "$1" in
    --help|-h)
        usage
        exit 0
        ;;
esac

name_arg="$1"
shift

mode="fast"
build_dir="build"
source_path=""
emulator="ntvcm"
use_emulated_m80="${DCC_USE_EMULATED_M80:-0}"
use_emulated_l80="${DCC_USE_EMULATED_L80:-0}"

while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h)
            usage
            exit 0
            ;;
        --build-dir|-BuildDir)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            build_dir="$2"
            shift 2
            ;;
        --source-path|-SourcePath)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            source_path="$2"
            shift 2
            ;;
        --emulator|-Emulator)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            emulator="$2"
            shift 2
            ;;
        --emulated-m80|-EmulatedM80)
            use_emulated_m80=1
            shift
            ;;
        --emulated-l80|-EmulatedL80)
            use_emulated_l80=1
            shift
            ;;
        --mode|-Mode)
            if [ $# -lt 2 ]; then echo "missing value for $1" >&2; exit 1; fi
            mode="$2"
            shift 2
            ;;
        full|fast|peep|nopeep|opt|optimized|o|-o|noopt|unopt|u|-u|1|0|yes|no|true|false)
            mode="$1"
            shift
            ;;
        *)
            echo "unknown argument: $1" >&2
            usage
            exit 1
            ;;
    esac
done

script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
script_asset_root=$(CDPATH= cd -- "$script_dir/.." && pwd)

trim() {
    printf '%s' "$1" | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
}

add_unique() {
    # Bash 3.2 compatible append to a named array.
    local array_name="$1"
    local path_value
    path_value=$(trim "${2:-}")
    if [ -z "$path_value" ]; then return 0; fi
    if [ -e "$path_value" ]; then
        path_value=$(CDPATH= cd -- "$path_value" 2>/dev/null && pwd || printf '%s' "$path_value")
    fi

    local existing
    eval "for existing in \"\${${array_name}[@]+\${${array_name}[@]}}\"; do
        if [ \"\$existing\" = \"\$path_value\" ]; then return 0; fi
    done
    ${array_name}[\${#${array_name}[@]}]=\"\$path_value\""
}

add_path_list() {
    local array_name="$1"
    local list_value="${2:-}"
    local old_ifs="$IFS"
    IFS=:
    # shellcheck disable=SC2206
    local entries=($list_value)
    IFS="$old_ifs"
    local entry
    for entry in "${entries[@]+${entries[@]}}"; do
        add_unique "$array_name" "$entry"
    done
}

resolve_command() {
    local command_value="$1"
    local found
    if found="$(command -v "$command_value" 2>/dev/null)"; then
        case "$found" in
            /*)
                # already absolute
                printf '%s' "$found"
                ;;
            *)
                # command -v can return a path relative to the CURRENT
                # directory (e.g. "./m80c", when "." is on $PATH). Some
                # callers cache that string and invoke it later from a
                # different cwd (e.g. after `cd "$build_dir"`), where the
                # same relative path can resolve to something else entirely
                # - such as build/m80c/, m80c's own leftover object-file
                # directory, giving a baffling "is a directory" error.
                # Canonicalize to absolute here so the result stays valid
                # regardless of any later `cd`.
                printf '%s' "$(cd "$(dirname "$found")" && pwd)/$(basename "$found")"
                ;;
        esac
    else
        printf '%s' "$command_value"
    fi
}

run_one() {
    local build_mode="$1"

    local base lower_base upper_base source_file
    base="${name_arg%.*}"
    lower_base=$(printf '%s' "$(basename "$base")" | tr '[:upper:]' '[:lower:]')
    upper_base=$(printf '%s' "$(basename "$base")" | tr '[:lower:]' '[:upper:]')

    source_file=""
    if [ -n "$source_path" ]; then
        if [ -f "$source_path" ]; then
            source_file="$source_path"
        fi
    else
        local candidate
        for candidate in \
            "tests/${base}.c" \
            "tests/${base}.C" \
            "tests/${lower_base}.c" \
            "tests/${upper_base}.C" \
            "${base}.c" \
            "${base}.C" \
            "${lower_base}.c" \
            "${upper_base}.C"
        do
            if [ -f "$candidate" ]; then
                source_file="$candidate"
                break
            fi
        done
    fi

    if [ -z "$source_file" ]; then
        if [ -n "$source_path" ]; then
            echo "source file not found: $source_path" >&2
        else
            echo "source file not found for: $name_arg" >&2
        fi
        return 1
    fi

    local mode_lc use_peep
    mode_lc=$(printf '%s' "$build_mode" | tr '[:upper:]' '[:lower:]')
    case "$mode_lc" in
        fast|peep|opt|optimized|o|-o|1|yes|true) use_peep=1 ;;
        nopeep|noopt|unopt|u|-u|0|no|false) use_peep=0 ;;
        *) echo "unknown optimization mode: $build_mode" >&2; return 1 ;;
    esac

    # Each `local NAME="${NAME:-default}"` must combine the declaration and
    # the default-value assignment in one statement - a bare `local NAME`
    # followed by a separate `NAME=${NAME:-default}` shadows any inherited
    # exported override with an empty local first, silently discarding it.
    local DCC="${DCC:-dcc}"
    local DCCPEEP="${DCCPEEP:-dccpeep}"
    local DCCRTLSTRIP="${DCCRTLSTRIP:-dccrtlstrip}"
    local NTVCM="${NTVCM:-$emulator}"
    local M80="${M80:-m80}"
    local M80C="${M80C:-m80c}"
    local L80="${L80:-l80}"
    local L80C="${L80C:-l80c}"
    # Resolved once here, then invoked later from inside the "cd $build_dir"
    # subshells used for the assembly/link steps below - unlike
    # DCC/DCCPEEP/DCCRTLSTRIP above, which resolve_command resolves right
    # at their own call site, before any cd. That's only safe because
    # resolve_command always returns an absolute path now; it used to be
    # able to return a cwd-relative one (e.g. "./m80c", when "." is on
    # $PATH), which broke here once the cwd changed underneath it.
    local m80c_resolved l80c_resolved
    m80c_resolved="$(resolve_command "$M80C")"
    l80c_resolved="$(resolve_command "$L80C")"

    mkdir -p "$build_dir"

    local asset_roots include_dirs dcc_home
    asset_roots=()
    include_dirs=()
    add_unique asset_roots "$PWD"

    dcc_home=$(trim "${DCC_HOME:-}")
    if [ -n "$dcc_home" ]; then
        add_unique asset_roots "$dcc_home"
        add_unique asset_roots "$dcc_home/lib"
        add_unique include_dirs "$dcc_home/include"
    fi

    add_path_list asset_roots "${DCC_LIB:-}"
    add_unique asset_roots "$script_asset_root"
    add_unique asset_roots "$script_asset_root/lib"
    add_path_list include_dirs "${DCC_INCLUDE:-}"

    local asset_root candidate_include_dir
    for asset_root in "${asset_roots[@]+${asset_roots[@]}}"; do
        candidate_include_dir="$asset_root/include"
        if [ -d "$candidate_include_dir" ]; then
            add_unique include_dirs "$candidate_include_dir"
        elif [ -f "$asset_root/stdio.h" ]; then
            add_unique include_dirs "$asset_root"
        fi
    done

    local tool_file tool_src
    for tool_file in m80.com l80.com; do
        tool_src=""
        for asset_root in "${asset_roots[@]+${asset_roots[@]}}"; do
            if [ -f "$asset_root/$tool_file" ]; then
                tool_src="$asset_root/$tool_file"
                break
            fi
        done
        if [ -n "$tool_src" ]; then
            cp -f "$tool_src" "$build_dir/$tool_file"
        fi
    done

    local app_mac app_rel app_com peep_tmp rtl_src rtl_min rtl_min_rel root_rtl_src
    app_mac="$build_dir/${upper_base}.MAC"
    app_rel="$build_dir/${upper_base}.REL"
    app_com="$build_dir/${upper_base}.COM"
    peep_tmp="$build_dir/_PEEPOUT.MAC"
    rtl_src="$build_dir/DCCRTL.MAC"
    rtl_min="$build_dir/RTLMIN.MAC"
    rtl_min_rel="$build_dir/RTLMIN.REL"
    root_rtl_src=""

    if [ -n "${DCC_RUNTIME:-}" ]; then
        if [ -f "$DCC_RUNTIME" ]; then
            root_rtl_src="$DCC_RUNTIME"
        else
            echo "runtime not found from DCC_RUNTIME: $DCC_RUNTIME" >&2
            return 1
        fi
    else
        for asset_root in "${asset_roots[@]+${asset_roots[@]}}"; do
            if [ -f "$asset_root/DCCRTL.MAC" ]; then
                root_rtl_src="$asset_root/DCCRTL.MAC"
                break
            fi
        done
    fi

    if [ -z "$root_rtl_src" ]; then
        echo "runtime not found: DCCRTL.MAC" >&2
        return 1
    fi

    to_crlf() {
        if command -v unix2dos >/dev/null 2>&1; then
            unix2dos "$1" >/dev/null 2>&1 || true
        else
            perl -0pi -e 's/\r?\n/\r\n/g' "$1"
        fi
    }

    local dcc_floatio dcc_longio dcc_stackchk dcc_stack_size
    dcc_floatio="${DCC_FLOATIO:-0}"
    dcc_longio="${DCC_LONGIO:-0}"

    # Auto-detect float/long printf format usage from the source, so ordinary
    # programs don't pull in the larger _pffio/_pflng runtime helpers unless
    # they actually use %f or %ld/%lu/%lx/%ls. An explicit DCC_FLOATIO=1/
    # DCC_LONGIO=1 in the environment always wins; this only fills in the
    # default when neither was already forced on.
    if [ "$dcc_floatio" != "1" ] && grep -Eiq '%[-+ #0-9.*]*[fF]' "$source_file"; then
        dcc_floatio=1
    fi
    if [ "$dcc_longio" != "1" ] && grep -Eiq '%[-+ #0-9.*]*l[duxXs]' "$source_file"; then
        dcc_longio=1
    fi

    dcc_stackchk=""
    if [ "${DCC_FORCE_STACK_CHECK:-0}" = "1" ]; then
        dcc_stackchk="-fstack-check"
    fi
    dcc_stack_size="${DCC_STACK_SIZE:-}"

    local dcc_extra_args ntvcm_args
    dcc_extra_args=()
    ntvcm_args=()
    if [ -n "${DCC_ARGS:-}" ]; then
        dcc_extra_args=($DCC_ARGS)
    fi
    if [ -n "${NTVCM_ARGS:-}" ]; then
        ntvcm_args=($NTVCM_ARGS)
    fi

    rm -f "$app_mac" "$app_rel" "$app_com" "$build_dir/${upper_base}.PRN" \
        "$peep_tmp" "$rtl_src" "$rtl_min" "$rtl_min_rel" "$build_dir/RTLMIN.PRN"

    local dcc_args include_dir strip_args
    dcc_args=()
    if [ -n "$dcc_stackchk" ]; then dcc_args[${#dcc_args[@]}]="$dcc_stackchk"; fi
    if [ "$dcc_stack_size" ]; then dcc_args[${#dcc_args[@]}]="-stack"; dcc_args[${#dcc_args[@]}]="$dcc_stack_size"; fi
    if [ "$dcc_floatio" = "1" ]; then dcc_args[${#dcc_args[@]}]="-ffloatio"; fi
    if [ "$dcc_longio" = "1" ]; then dcc_args[${#dcc_args[@]}]="-flongio"; fi
    for include_dir in "${dcc_extra_args[@]+${dcc_extra_args[@]}}"; do
        dcc_args[${#dcc_args[@]}]="$include_dir"
    done
    for include_dir in "${include_dirs[@]+${include_dirs[@]}}"; do
        dcc_args[${#dcc_args[@]}]="-I"
        dcc_args[${#dcc_args[@]}]="$include_dir"
    done
    dcc_args[${#dcc_args[@]}]="$source_file"
    dcc_args[${#dcc_args[@]}]="-o"
    dcc_args[${#dcc_args[@]}]="$app_mac"

    "$(resolve_command "$DCC")" "${dcc_args[@]}"

    if [ "$use_peep" -eq 1 ]; then
        local dccpeep_args=()
        if [ "${DCC_ALLOW_UNDOCUMENTED_Z80:-0}" = "1" ]; then
            dccpeep_args[${#dccpeep_args[@]}]="-fundocumented-z80"
        fi
        "$(resolve_command "$DCCPEEP")" "${dccpeep_args[@]+${dccpeep_args[@]}}" "$app_mac" "$peep_tmp"
        mv -f "$peep_tmp" "$app_mac"
    fi

    to_crlf "$app_mac"

    (
        cd "$build_dir"
        if [ "$use_emulated_m80" = "1" ]; then
            "$(resolve_command "$NTVCM")" "${ntvcm_args[@]+${ntvcm_args[@]}}" "$M80" "=${upper_base}.MAC" /X /O /Z /L
        else
            "$m80c_resolved" "=${upper_base}.MAC" /X /O /Z /L /C
        fi
    )

    cp -f "$root_rtl_src" "$rtl_src"
    to_crlf "$rtl_src"
    strip_args=()
    if [ "$dcc_floatio" = "1" ]; then strip_args[${#strip_args[@]}]="-k"; strip_args[${#strip_args[@]}]="_pffio"; fi
    if [ "$dcc_longio" = "1" ]; then strip_args[${#strip_args[@]}]="-k"; strip_args[${#strip_args[@]}]="_pflng"; fi
    "$(resolve_command "$DCCRTLSTRIP")" "${strip_args[@]+${strip_args[@]}}" -r "$rtl_src" -o "$rtl_min" "$app_mac"
    to_crlf "$rtl_min"

    (
        cd "$build_dir"
        if [ "$use_emulated_m80" = "1" ]; then
            "$(resolve_command "$NTVCM")" "${ntvcm_args[@]+${ntvcm_args[@]}}" "$M80" "=RTLMIN.MAC" /X /O /Z
        else
            "$m80c_resolved" "=RTLMIN.MAC" /X /O /Z /C
        fi
        if [ "$use_emulated_l80" = "1" ]; then
            "$(resolve_command "$NTVCM")" "${ntvcm_args[@]+${ntvcm_args[@]}}" "$L80" "/P:100,RTLMIN,${upper_base},${upper_base}/N/E/Y"
        else
            "$l80c_resolved" "/P:100,RTLMIN,${upper_base},${upper_base}/N/E/Y"
        fi
    )

    local lower_com
    lower_com="$build_dir/${lower_base}.com"
    if [ -f "$app_com" ] && [ "$lower_base" != "$upper_base" ]; then
        if [ ! -e "$lower_com" ] || [ ! "$app_com" -ef "$lower_com" ]; then
            cp -f "$app_com" "$lower_com"
        fi
    fi
}

mode_lc=$(printf '%s' "$mode" | tr '[:upper:]' '[:lower:]')
if [ "$mode_lc" = "full" ]; then
    run_one fast
    run_one nopeep
else
    run_one "$mode"
fi