#!/bin/zsh
# Fast AST byte-safety check: for every tests/*.c, compile with the AST emitter
# OFF (baseline streaming) and ON, and diff the generated .mac. Any difference
# means the AST path diverged from streaming for that app. Compiler-only (no
# emulator), so it is much faster than `runall.ps1 -Report`.
#
# Usage:  ./scripts/ast-corpus-diff.sh
# Exit 0 + "CORPUS_IDENTICAL" when every app matches; exit 1 listing mismatches.
set -u
cd "$(dirname "$0")/.." || exit 2
dcc=./dcc
tmp=$(mktemp -d)
mismatch=0
fail=0
for src in tests/*.c; do
    app=$(basename "$src" .c)
    "$dcc" "$src" -o "$tmp/off.mac" >/dev/null 2>&1 || { echo "COMPILE-FAIL(off): $app"; fail=1; continue; }
    DCC_AST_GEN=1 "$dcc" "$src" -o "$tmp/on.mac" >/dev/null 2>&1 || { echo "COMPILE-FAIL(on): $app"; fail=1; continue; }
    if ! diff -q "$tmp/off.mac" "$tmp/on.mac" >/dev/null; then
        echo "DRIFT: $app"
        mismatch=1
    fi
done
rm -rf "$tmp"
if [ "$mismatch" = 0 ] && [ "$fail" = 0 ]; then
    echo "CORPUS_IDENTICAL"
    exit 0
fi
exit 1
