#!/bin/zsh
# Summarise remaining AST fallback sites across tests/*.c.
# Requires a freshly built ./dcc. Output is grouped by fallback reason and a
# short per-app fallback count so the next AST migration slice can be chosen by
# impact instead of guesswork.
set -u
cd "$(dirname "$0")/.." || exit 2
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
all="$tmp/all.txt"
: > "$all"
for src in tests/*.c; do
    app=$(basename "$src" .c)
    DCC_AST_GEN=1 DCC_AST_REPORT=1 ./dcc "$src" -o "$tmp/$app.mac" > /dev/null 2> "$tmp/$app.txt"
    if [ -s "$tmp/$app.txt" ]; then
        sed "s/^/$app: /" "$tmp/$app.txt" >> "$all"
    fi
done
printf '== fallback reasons ==\n'
sed 's/^[^:]*: //' "$all" | sed -E "s/ line=[0-9]+//" | sort | uniq -c | sort -nr
printf '\n== per app fallback counts ==\n'
awk -F: '{count[$1]++} END {for (app in count) print count[app], app}' "$all" | sort -nr | head -40
printf '\n== total fallback sites ==\n'
wc -l < "$all"
