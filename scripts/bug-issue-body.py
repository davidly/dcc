#!/usr/bin/env python3
"""Write a GitHub issue body for an open entry in compiler-bugs.md.

The body is the entry's text, the parked reproducer from tests/known-bugs/,
the commands to build it, the host compiler's output (checked against the
baseline) and what dcc actually does, captured by running it now with and
without dccpeep. The title is the entry's row in the summary table, and
references to other entries become issue links through the table's Issue
column.

  bug-issue-body.py bug054              write build/bug-issues/bug054.md
  bug-issue-body.py bug054 --create     also open the issue with gh
  bug-issue-body.py                     every entry without an issue number

An entry must have a summary row before its issue can be written, and the
compiler must be the upstream one: local commits may add tests and scripts,
not change src/ or DCCRTL.MAC.
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / 'build' / 'bug-issues'
KNOWN = REPO / 'tests' / 'known-bugs'
HOST = ['clang', '-std=gnu99', '-fsigned-char']


def upstream_commit():
    """The upstream commit the compiler is built from."""
    commit = subprocess.run(['git', 'merge-base', 'HEAD', 'origin/main'], cwd=REPO,
                            capture_output=True, text=True).stdout.strip()[:8]
    changed = subprocess.run(['git', 'diff', '--name-only', commit, 'HEAD', '--',
                              'src', 'DCCRTL.MAC'],
                             cwd=REPO, capture_output=True, text=True).stdout.strip()
    if changed:
        sys.exit('local commits change the compiler: issues must not cite upstream %s'
                 % commit)
    return commit


def table():
    """bugNNN -> (issue number or '', kind, defect) from the summary table."""
    out = {}
    text = (REPO / 'compiler-bugs.md').read_text()
    for m in re.finditer(r'^\| \[BUG-(\d+)\]\S* \| (#?\d*|—|-)? ?\| ([^|]*) \| (.*) \|$',
                         text, re.M):
        issue = (m.group(2) or '').strip('# ')
        out['bug' + m.group(1)] = (issue if issue.isdigit() else '',
                                   m.group(3).strip(),
                                   m.group(4).strip().replace(r'\|', '|'))
    return out


def sections():
    text = (REPO / 'compiler-bugs.md').read_text()
    out = {}
    for m in re.finditer(r'^## (BUG-(\d+))\n(.*?)(?=^## |^---$)', text, re.M | re.S):
        out['bug' + m.group(2)] = m.group(3).strip()
    return out


def run(cmd, cwd, timeout):
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, timeout=timeout,
                           stdin=subprocess.DEVNULL)
    except subprocess.TimeoutExpired as exc:
        partial = (exc.stdout or b'').decode('latin-1').replace('\r', '')
        return None, partial[:600]
    return r.returncode, (r.stdout + r.stderr).decode('latin-1').replace('\r', '')


def dcc(name, src, peep):
    """('compile', text) | ('hang', text) | ('output', text)"""
    build = OUT / 'work' / ('%s-%s' % (name, 'peep' if peep else 'nopeep'))
    shutil.rmtree(build, ignore_errors=True)
    code, log = run([str(REPO / 'dccmake'), 'dcc-input=%s' % src, 'dcc-output=%s' % name.upper(),
                     'dcc-build-dir=%s' % build, 'dcc-peep=%s' % ('true' if peep else 'false')],
                    REPO, 120)
    if code != 0:
        lines = [ln for ln in log.splitlines() if not ln.startswith('+')
                 and not ln.startswith('command failed') and ln.strip()]
        return 'compile', '\n'.join(lines).replace(str(src), name + '.c')
    diag = [ln for ln in log.splitlines() if ln.startswith('; MIR ')]
    code, out = run(['ntvcm', name.upper() + '.COM'], build, 60)
    if code is None:
        return 'hang', out
    return 'output', ('\n'.join(diag) + '\n' if diag else '') + out


def title(name, rows):
    issue, kind, defect = rows[name]
    if kind.startswith('wrong result'):
        prefix = 'Wrong code'
    elif kind.startswith('hang'):
        prefix = 'Hang'
    elif kind.startswith('compile'):
        prefix = 'Valid C rejected'
    else:
        prefix = kind[0].upper() + kind[1:]
    if 'dccpeep' in kind:
        prefix += ' (dccpeep)'
    return '%s: %s' % (prefix, defect)


def body(name, entry, rows, commit):
    src = OUT / 'work' / (name + '.c')
    src.parent.mkdir(parents=True, exist_ok=True)
    source = (KNOWN / (name + '.c')).read_text()
    src.write_text(source)
    # the local log's id means nothing upstream
    shown = re.sub(r'^/\* BUG-\d+: (.)', lambda m: '/* ' + m.group(1).upper(), source)
    host_exe = OUT / 'work' / (name + '.host')
    subprocess.run(HOST + [str(src), '-o', str(host_exe)], check=True, capture_output=True)
    _, expected = run([str(host_exe)], OUT, 20)
    baseline = (KNOWN / 'baselines' / (name + '.txt')).read_text()
    if expected != baseline:
        sys.exit('%s: the host output differs from its baseline' % name)
    nopeep = dcc(name, src, False)
    peep = dcc(name, src, True)
    for other, (issue, _, _) in rows.items():
        if issue:
            ref = 'BUG-' + other[3:]
            entry = entry.replace(ref, '#' + issue)
            shown = shown.replace(ref, '#' + issue)
    left = sorted(set(re.findall(r'BUG-\d+', entry + shown)))
    if left:
        sys.exit('%s refers to %s, which have no issue yet' % (name, ', '.join(left)))
    upper = name.upper()
    parts = [entry, '', '## Reproduce', '',
             'Compiler: `main` at `%s`, built with `pwsh ./scripts/build-dcc.ps1`. '
             'Emulator: ntvcm.' % commit, '',
             'Save as `%s.c` in the repository root:' % name, '', '```c', shown.rstrip(), '```', '',
             '```sh',
             './dccmake dcc-input=%s.c dcc-output=%s dcc-build-dir=build/%s dcc-peep=false' % (
                 name, upper, name),
             '(cd build/%s && ntvcm %s.COM)' % (name, upper),
             '',
             '# reference result from the host compiler',
             '%s %s.c -o %s.host && ./%s.host' % (' '.join(HOST), name, name, name),
             '```', '',
             '## Expected', '',
             'The host compiler\'s output. The program uses fixed-width types only, so the '
             'result does not depend on `int` being 16 or 32 bits, and it runs clean under '
             '`-fsanitize=address,undefined`.', '',
             '```', expected.rstrip(), '```', '', '## Actual', '']
    kind, text = nopeep
    if kind == 'compile':
        parts += ['The compile fails:', '', '```', text.rstrip(), '```']
    elif kind == 'hang':
        if text.strip():
            parts += ['The program is still running after 60 seconds. It printed this first '
                      '(truncated):', '', '```', text.rstrip(), '```']
        else:
            parts += ['The program prints nothing and is still running after 60 seconds.']
    else:
        parts += ['```', text.rstrip(), '```']
    parts.append('')
    if peep == nopeep:
        parts.append('The result is the same with `dcc-peep=true`.')
    elif peep[0] == 'output' and nopeep[0] == 'output':
        parts += ['With `dcc-peep=true` the output differs again:', '', '```', peep[1].rstrip(), '```']
    else:
        parts += ['With `dcc-peep=true`: %s.' % ('the compile fails the same way' if peep[0] == 'compile'
                                                else 'the program hangs' if peep[0] == 'hang'
                                                else 'see below')]
    parts += ['', '## Notes', '',
              '- Found by differential testing against clang with generated programs '
              '(`scripts/clang-oracle-fuzz.py`), then narrowed by hand. The reproducer is '
              '`tests/known-bugs/%s.c`; `python3 scripts/run-known-bugs.py %s` checks it.'
              % (name, name)]
    return '\n'.join(parts) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('names', nargs='*', help='entries, e.g. bug054; default: all without an issue')
    parser.add_argument('--create', action='store_true', help='open each issue with gh')
    parser.add_argument('--repo', default='davidly/dcc')
    args = parser.parse_args()
    rows, entries = table(), sections()
    names = args.names or sorted(n for n, (issue, _, _) in rows.items() if not issue)
    if not names:
        sys.exit('every entry in the table has an issue')
    for name in names:
        if name not in rows:
            sys.exit('%s has no row in the summary table of compiler-bugs.md' % name)
        if name not in entries:
            sys.exit('%s has no ## BUG- section in compiler-bugs.md' % name)
    commit = upstream_commit()
    OUT.mkdir(parents=True, exist_ok=True)
    for name in names:
        text = body(name, entries[name], rows, commit)
        path = OUT / (name + '.md')
        path.write_text(text)
        heading = title(name, rows)
        (OUT / (name + '.title')).write_text(heading + '\n')
        print('%s: %d bytes\n  %s' % (name, len(text), heading))
        if args.create:
            r = subprocess.run(['gh', 'issue', 'create', '--repo', args.repo, '--label', 'bug',
                                '--title', heading, '--body-file', str(path)],
                               capture_output=True, text=True)
            print('  ' + (r.stdout.strip() or r.stderr.strip()))


if __name__ == '__main__':
    main()
