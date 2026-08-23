#!/usr/bin/env bash
set -uo pipefail
script_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(CDPATH= cd -- "$script_dir/.." 2>/dev/null && pwd || pwd)
cd "$repo_root" || exit 1

dcc="$repo_root/dcc"; test_dir=tests/diagnostics; baseline_dir=tests/diagnostics/baselines; build_dir=build/diagnostics; update=0
while (($#)); do
 case "$1" in
  --dcc|-Dcc) dcc=${2:?}; shift 2;; --test-dir|-TestDir) test_dir=${2:?}; shift 2;;
  --baseline-dir|-BaselineDir) baseline_dir=${2:?}; shift 2;; --build-dir|-BuildDir) build_dir=${2:?}; shift 2;;
  --update|-Update) update=1; shift;; -h|--help) echo "usage: run-diagnostics.sh [--dcc CMD] [--update]"; exit 0;;
  *) echo "unknown argument: $1" >&2; exit 2;; esac
done
[[ -x $dcc ]] || command -v "$dcc" >/dev/null 2>&1 || { echo "dcc not found: $dcc" >&2; exit 2; }
mkdir -p "$build_dir" "$baseline_dir"
tests=()
while IFS= read -r diag_test; do tests+=("$diag_test"); done < <(find "$test_dir" -maxdepth 1 -type f -name '*.c' | sort)
((${#tests[@]})) || { echo "No diagnostic tests found in $test_dir" >&2; exit 1; }
fail=0
for src in "${tests[@]}"; do
 name=$(basename "$src" .c); raw="$build_dir/$name.raw"; actual="$build_dir/$name.txt"; out="$build_dir/$name.mac"; baseline="$baseline_dir/$name.txt"
 "$dcc" "$src" -o "$out" >"$raw" 2>&1; rc=$?
 resolved=$(readlink -f "$src" 2>/dev/null || printf '%s' "$src")
 resolved_source=$resolved source_path=$src perl -0777 -pe 's/\r\n?/\n/g; s/\Q$ENV{resolved_source}\E/<source>/g; s/\Q$ENV{source_path}\E/<source>/g' "$raw" > "$actual"
 [[ -s $actual ]] && [[ $(tail -c1 "$actual" | od -An -t u1) != *10* ]] && printf '\n' >> "$actual"
 # Tests named "warn-*" must compile successfully (exit 0) - they prove a
 # warning fires (or is correctly suppressed) without it being a hard
 # error. Every other diagnostic test is a compile-fail test and must
 # exit nonzero. Mirrors run-diagnostics.ps1's $expectSuccess check.
 if [[ $name == warn-* ]]; then
  if ((rc!=0)); then echo "FAIL   $name (expected compile success, got failure)"; ((fail++)); continue; fi
 else
  if ((rc==0)); then echo "FAIL   $name (compiler unexpectedly succeeded)"; ((fail++)); continue; fi
 fi
 if ((update)); then cp "$actual" "$baseline"; echo "UPDATE $name"; continue; fi
 if [[ ! -f $baseline ]]; then echo "FAIL   $name (missing baseline)"; ((fail++)); continue; fi
 if cmp -s "$baseline" "$actual"; then echo "PASS   $name"; else echo "FAIL   $name (diagnostic mismatch)"; diff -u "$baseline" "$actual" || true; ((fail++)); fi
done
if ((fail)); then echo "diagnostics: $fail failure(s)"; exit 1; fi
((update)) && echo "diagnostics: baselines updated" || echo "diagnostics: all ${#tests[@]} passed"
