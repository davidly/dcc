#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
manifest=${1:-"$repo_root/scripts/ast-mir-coverage.tsv"}

awk -F '\t' '
NR == 1 {
    if ($0 != "category\tsource\treason") exit 1
    next
}
{
    if (NF != 3 || $3 == "" ||
        $1 !~ /^(active-owner|mixed-pending|optional-diagnostic|legacy)$/ ||
        $2 !~ /^src\/dcc\/dcc_(ast|mir)[a-z_]*\.c$/ || seen[$2]++) {
        print "Invalid coverage classification at row " NR > "/dev/stderr"
        failed = 1
    }
    if ($1 == "active-owner") included++
}
END { if (failed || !included) exit 1 }
' "$manifest"

tab=$(printf '\t')
while IFS="$tab" read -r category source reason; do
    if [ "$category" = category ]; then continue; fi
    if [ ! -f "$repo_root/$source" ]; then
        echo "Coverage manifest source does not exist: $source" >&2
        exit 1
    fi
done < "$manifest"

for source in "$repo_root"/src/dcc/dcc_ast*.c "$repo_root"/src/dcc/dcc_mir*.c; do
    relative=${source#"$repo_root/"}
    if ! awk -F '\t' -v source="$relative" '
        $2 == source { found = 1 }
        END { exit !found }
    ' "$manifest"; then
        echo "Unclassified AST/MIR module: $relative" >&2
        exit 1
    fi
done

awk -F '\t' -v root="$repo_root" '
    $1 == "active-owner" { print root "/" $2 }
' "$manifest"