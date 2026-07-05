#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: ./mrel.sh name [peep|nopeep]"
    echo "examples: ./mrel.sh qsort peep"
    echo "          ./mrel.sh qsort nopeep"
}

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    usage
    exit 1
fi

name_arg="$1"
base="${name_arg%.*}"
lower_base="$(printf '%s' "$(basename "$base")" | tr '[:upper:]' '[:lower:]')"
upper_base="$(printf '%s' "$(basename "$base")" | tr '[:lower:]' '[:upper:]')"

mode="${2:-peep}"
mode_lc="$(printf '%s' "$mode" | tr '[:upper:]' '[:lower:]')"
case "$mode_lc" in
    peep|opt|optimized|o|-o|1|yes|true)
        use_peep=1
        ;;
    nopeep|nopeepopt|noopt|unopt|u|-u|0|no|false)
        use_peep=0
        ;;
    *)
        echo "unknown optimization mode: $mode" >&2
        usage
        exit 1
        ;;
esac

# Prefer the user's spelling, but also support CP/M-style uppercase/lowercase names
# on case-sensitive Linux/macOS filesystems.
source_file="${base}.c"
if [ ! -f "$source_file" ]; then
    source_file="${base}.C"
fi
if [ ! -f "$source_file" ]; then
    source_file="${lower_base}.c"
fi
if [ ! -f "$source_file" ]; then
    source_file="${upper_base}.C"
fi
if [ ! -f "$source_file" ]; then
    echo "source file not found for: $name_arg" >&2
    exit 1
fi

DCC=${DCC:-dcc}
DCCPEEP=${DCCPEEP:-dccpeep}
NTVCM=${NTVCM:-ntvcm}
M80=${M80:-m80}

# M80/L80 on CP/M want uppercase filenames. Keep all generated CP/M-facing
# modules uppercase: FOO.MAC, FOO.REL, RTLMIN.MAC, RTLMIN.REL.
app_mac="${upper_base}.MAC"
app_rel="${upper_base}.REL"
peep_tmp="_PEEPOUT.MAC"

# CRLF conversion helper for files consumed by M80.
to_crlf() {
    if command -v unix2dos >/dev/null 2>&1; then
        unix2dos "$1" >/dev/null 2>&1 || true
    else
        perl -0pi -e 's/\r?\n/\r\n/g' "$1"
    fi
}

# Detect optional compiler/runtime roots from source. Keep this conservative so
# normal non-float programs do not pull in float printf code.
dcc_flags=()

rm -f "$app_mac" "$app_rel" "$app_com" "${upper_base}.PRN" \
      "$peep_tmp" "$rtl_min" RTLMIN.REL RTLMIN.PRN

# Compile directly to an uppercase .MAC file.
"$DCC" -c "$source_file" -o "$app_mac"

if [ "$use_peep" -eq 1 ]; then
    "$DCCPEEP" "$app_mac" "$peep_tmp"
    mv -f "$peep_tmp" "$app_mac"
fi

to_crlf "$app_mac"

# Assemble app. Explicit uppercase .MAC is important on Linux/macOS.
"$NTVCM" "$M80" "=${app_mac}" /X /O /Z /L


