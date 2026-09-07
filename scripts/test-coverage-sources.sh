#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
manifest="$repo_root/scripts/ast-mir-coverage.tsv"
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT HUP INT TERM

expect_failure()
{
    if sh "$repo_root/scripts/coverage-sources.sh" "$workspace/invalid.tsv" \
        >"$workspace/output" 2>"$workspace/error"; then
        echo "FAIL: invalid manifest accepted: $1" >&2
        exit 1
    fi
    if [ -s "$workspace/output" ]; then
        echo "FAIL: invalid manifest published partial source list: $1" >&2
        exit 1
    fi
}

sh "$repo_root/scripts/coverage-sources.sh" >"$workspace/sources"
if grep -E 'dcc_ast_gen|dcc_mir_(schedule|target)\.c' "$workspace/sources"; then
    echo "FAIL: excluded modules entered active coverage" >&2
    exit 1
fi
awk 'NR == 2 { print } { print }' "$manifest" >"$workspace/invalid.tsv"
expect_failure duplicate
awk 'NR != 2' "$manifest" >"$workspace/invalid.tsv"
expect_failure unclassified
awk 'NR == 2 { sub(/active-owner/, "unknown") } { print }' \
    "$manifest" >"$workspace/invalid.tsv"
expect_failure category
awk 'NR == 2 { sub(/dcc_ast.c/, "dcc_ast_missing.c") } { print }' \
    "$manifest" >"$workspace/invalid.tsv"
expect_failure missing
printf '%s\n' "Coverage source manifest tests passed"