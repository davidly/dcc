#!/usr/bin/env bash
# Quick regression helper: build + run given apps in peep AND nopeep,
# compare against baseline_test_dcc.txt (CR-normalized).
# Usage: ./qtest.sh app1 [app2 ...]
set -uo pipefail
export PATH="/Users/dave/GitHub/ntvcm:/Users/dave/GitHub/dcc:$PATH"
export DCC=./dcc DCCPEEP=./dccpeep DCCRTLSTRIP=./dccrtlstrip

run_args() {
    case "$1" in
        ttt) echo "10" ;;
        wumpus|tchess) echo "-c" ;;
        targs) echo "a bb ccc dddd eeeee" ;;
        *) echo "" ;;
    esac
}

baseline_block() { awk -v t="test $1" '$0==t{f=1;next} /^test /{f=0} f' baseline_test_dcc.txt; }

fails=0
for app in "$@"; do
    upper="$(printf '%s' "$app" | tr '[:lower:]' '[:upper:]')"
    args="$(run_args "$app")"
    exp="$(baseline_block "$app")"
    for mode in peep nopeep; do
        if ! ./ma.sh "$app" "$mode" >/tmp/qt_build.log 2>&1; then
            echo "BUILD-FAIL $app ($mode)"; cat /tmp/qt_build.log; fails=$((fails+1)); continue
        fi
        got="$(ntvcm "$upper" $args 2>/dev/null | tr -d '\r')"
        if [ "$got" = "$exp" ]; then
            echo "PASS $app ($mode)"
        else
            echo "FAIL $app ($mode)"; diff <(echo "$exp") <(echo "$got") | head -40; fails=$((fails+1))
        fi
    done
done
echo "----"
if [ "$fails" -eq 0 ]; then echo "ALL PASS"; else echo "$fails FAILURE(S)"; fi
exit $fails
