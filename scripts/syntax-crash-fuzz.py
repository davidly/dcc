#!/usr/bin/env python3
"""Mutation fuzzer for dcc's handling of broken C, using Python 3.7+.

Each seed picks a program from tests/*.c and breaks it with one to three
token-level mutations: a token deleted, duplicated, replaced, inserted or
swapped with its neighbour, a run of tokens deleted or moved, a line
deleted, the file truncated, or an expression nested very deeply. dcc must
reject or accept the result cleanly. A run is reported when dcc

  crash     dies on a signal, or a sanitizer reports an error
  hang      runs past --timeout
  silent    fails without printing a diagnostic or a fatal message
  badline   reports an error on a line the input does not have
  fatal     stops with a `dcc: fatal:` message and no diagnostic before it
  accepted  with --accepts: compiles an input clang rejects, from a program
            clang accepts

With --directives every mutation lands on a preprocessor directive line,
in programs that have directives other than #include, to exercise #define,
#if and the other directives' handling of mistakes.

Build dcc with -fsanitize=address,undefined and pass it as --dcc to catch
memory errors that do not crash. Failing inputs are kept as <work>/<seed>.c
with dcc's output beside them; one input per distinct signature is listed.

  syntax-crash-fuzz.py --seeds 1-2000 --dcc build/asan/dcc
  syntax-crash-fuzz.py --seeds 1-2000 --accepts
  syntax-crash-fuzz.py --seeds 1-2000 --directives --dcc build/asan/dcc
  syntax-crash-fuzz.py --reduce 17 --dcc build/asan/dcc
  syntax-crash-fuzz.py --generate 17 broken.c
"""
import argparse
import collections
import concurrent.futures
from contextlib import contextmanager
from datetime import datetime
import os
from pathlib import Path
import random
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]


@contextmanager
def progress_terminal():
    if sys.stdout.isatty():
        yield sys.stdout
        return
    try:
        terminal = open('/dev/tty', 'w', encoding='utf-8')
    except OSError:
        yield None
    else:
        with terminal:
            yield terminal


def progress_reporter(total, terminal):
    started = time.monotonic()
    progress = ''

    def update(completed):
        nonlocal progress
        filled = 24 * completed // total
        finish = 'ETA --:--:--'
        if completed:
            now = time.time()
            remaining = (time.monotonic() - started) * (total - completed) / completed
            finish_at = datetime.fromtimestamp(now + remaining)
            clock_format = '%H:%M:%S'
            if finish_at.date() != datetime.fromtimestamp(now).date():
                clock_format = '%Y-%m-%d ' + clock_format
            label = 'Finished' if completed == total else 'ETA'
            finish = '%s %s' % (label, finish_at.strftime(clock_format))
        progress = '\r[%-24s] %d/%d programs  %s\033[K' % (
            '#' * filled, completed, total, finish)
        print(progress, end='', file=terminal, flush=True)

    def report(message):
        nonlocal progress
        if progress:
            print('\r\033[K', end='', file=terminal, flush=True)
            if terminal is not sys.stdout:
                progress = ''
        print(message, flush=True)
        if progress:
            print(progress, end='', file=terminal, flush=True)

    return update, report

PUNCTUATORS = sorted((
    '...', '<<=', '>>=', '->', '++', '--', '<<', '>>', '<=', '>=', '==', '!=', '&&',
    '||', '*=', '/=', '%=', '+=', '-=', '&=', '^=', '|=', '##', '[', ']', '(', ')',
    '{', '}', '.', '&', '*', '+', '-', '~', '!', '/', '%', '<', '>', '^', '|', '?',
    ':', ';', '=', ',', '#'), key=len, reverse=True)

TOKEN = re.compile(r'''
    (?P<space>\s+)
  | (?P<comment>/\*.*?\*/|//[^\n]*)
  | (?P<string>"(?:\\.|[^"\\\n])*")
  | (?P<char>'(?:\\.|[^'\\\n])*')
  | (?P<number>\.?[0-9](?:[eEpP][+-]|[0-9A-Za-z_.])*)
  | (?P<ident>[A-Za-z_][A-Za-z_0-9]*)
  | (?P<punct>%s)
  | (?P<other>.)
''' % '|'.join(re.escape(p) for p in PUNCTUATORS), re.S | re.X)

KEYWORDS = (
    'auto', 'break', 'case', 'char', 'const', 'continue', 'default', 'do', 'else',
    'enum', 'extern', 'float', 'for', 'goto', 'if', 'int', 'long', 'register',
    'return', 'short', 'signed', 'sizeof', 'static', 'struct', 'switch', 'typedef',
    'union', 'unsigned', 'void', 'volatile', 'while', 'inline', '_Bool', 'double',
    '__asm', 'asm')

ODD = (
    '"unterminated', "'", "'ab'", "''", '0x', '1e+', '09', '0b2', '1.2.3', '99999999999999999999',
    '4294967296', '0x1ffffffff', '\\', '@', '`', '$', '#', '##', '#define', '#if', '#endif',
    '#else', '#include', '#undef', '#pragma', '#line', '"\\x"', "'\\777'", 'L"w"',
    'x' * 300, '__LINE__', '__FILE__', 'defined', '...')

DIAGNOSTIC = re.compile(r'^(.*?):(-?\d+): (error|warning) DCC-[EW]\d+: (.*)$', re.M)
FATAL = re.compile(r'^dcc: fatal: (.*)$', re.M)
ASAN = re.compile(r'ERROR: AddressSanitizer: ([\w-]+)')
FRAME = re.compile(r'#\d+ 0x[0-9a-f]+ in (\w+) [^\n]*src/dcc/')
UBSAN = re.compile(r'(src/dcc/[\w.]+):\d+:\d+: runtime error: ([^\n]*)')
CLANG_ERROR = re.compile(r'^[^\n]*?: (?:fatal )?error: ([^\n]*)', re.M)
# Only syntax counts: dcc is allowed C89 leniencies clang reports as errors.
CLANG = ['clang', '-std=gnu99', '-fsigned-char', '-fsyntax-only', '-w', '-ferror-limit=1',
         '-Wno-error=implicit-function-declaration', '-Wno-error=int-conversion',
         '-Wno-error=incompatible-pointer-types', '-Wno-error=implicit-int',
         '-Wno-error=return-type', '-I', str(ROOT), '-I', str(ROOT / 'tests')]


DIRECTIVE = re.compile(r'^[ \t]*#[ \t]*(define|undef|if|ifdef|ifndef|elif|else|endif|line|pragma)\b',
                       re.M)

# Set by --directives: mutate only tokens on directive lines.
directives_only = False


def corpus():
    files = sorted(p for p in (ROOT / 'tests').glob('*.c'))
    if directives_only:
        files = [p for p in files if DIRECTIVE.search(p.read_text(encoding='latin-1'))]
    return files


def tokenize(text):
    return [m.group(0) for m in TOKEN.finditer(text)]


def significant(tokens):
    return [i for i, t in enumerate(tokens) if not t.isspace() and not t.startswith(('/*', '//'))]


def directive_tokens(tokens):
    """Indexes of significant tokens on directive lines, continuations
    included."""
    found = []
    line_start = True
    in_directive = False
    previous = ''
    for i, t in enumerate(tokens):
        if t.isspace() or t.startswith(('/*', '//')):
            if '\n' in t and previous != '\\':
                line_start = True
                in_directive = False
            continue
        if line_start:
            in_directive = t == '#'
            line_start = False
        if in_directive:
            found.append(i)
        previous = t
    return found


def random_token(rng, tokens, sig):
    pick = rng.random()
    if pick < 0.35:
        return rng.choice(PUNCTUATORS)
    if pick < 0.55:
        return rng.choice(KEYWORDS)
    if pick < 0.75 and sig:
        return tokens[rng.choice(sig)]
    if pick < 0.95:
        return rng.choice(ODD)
    return str(rng.choice((0, 1, -1, 255, 32767, 65535)))


def mutate_once(rng, tokens):
    sig = significant(tokens)
    if not sig:
        return tokens
    at = rng.choice((directives_only and directive_tokens(tokens)) or sig)
    kind = rng.randrange(11)
    if kind == 0:
        tokens[at] = ''
    elif kind == 1:
        n = rng.randint(2, 8)
        for i in sig[sig.index(at):sig.index(at) + n]:
            tokens[i] = ''
    elif kind == 2:
        tokens[at] = tokens[at] + ' ' + tokens[at]
    elif kind == 3:
        tokens[at] = random_token(rng, tokens, sig)
    elif kind == 4:
        tokens[at] = random_token(rng, tokens, sig) + ' ' + tokens[at]
    elif kind == 5:
        k = sig.index(at)
        if k + 1 < len(sig):
            j = sig[k + 1]
            tokens[at], tokens[j] = tokens[j], tokens[at]
    elif kind == 6:
        k = sig.index(at)
        run = sig[k:k + rng.randint(1, 12)]
        moved = ' '.join(tokens[i] for i in run)
        for i in run:
            tokens[i] = ''
        dest = rng.choice(sig)
        tokens[dest] = moved + ' ' + tokens[dest]
    elif kind == 7:
        del tokens[at:]
    elif kind == 8:
        text = ''.join(tokens)
        lines = text.split('\n')
        del lines[rng.randrange(len(lines))]
        tokens[:] = tokenize('\n'.join(lines))
    elif kind == 9:
        depth = rng.choice((64, 256, 1000, 5000))
        tokens[at] = '(' * depth + tokens[at] + ')' * depth
    else:
        depth = rng.choice((64, 256, 1000))
        opener = rng.choice(('{', '[', 'if (1)', '-', '!', '*', '&'))
        tokens[at] = (opener + ' ') * depth + tokens[at]
    return tokens


def generate(seed):
    rng = random.Random(seed)
    files = corpus()
    source = rng.choice(files)
    tokens = tokenize(source.read_text(encoding='latin-1'))
    for _ in range(rng.choice((1, 1, 1, 2, 3))):
        tokens = mutate_once(rng, tokens)
    return source.name, ''.join(tokens)


def run_dcc(dcc, path, timeout):
    env = dict(os.environ)
    env.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    env.setdefault('UBSAN_OPTIONS', 'print_stacktrace=1')
    command = [dcc, '-I', str(ROOT), '-I', str(ROOT / 'tests'), str(path),
               '-o', str(path.with_suffix('.mac'))]
    try:
        r = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        return None, ''
    finally:
        try:
            path.with_suffix('.mac').unlink()
        except OSError:
            pass
    return r.returncode, r.stdout.decode('latin-1')


def clang_error(path):
    """clang's first error for path, or None if clang accepts it."""
    r = subprocess.run(CLANG + [str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode == 0:
        return None
    m = CLANG_ERROR.search(r.stdout.decode('latin-1'))
    return m.group(1) if m else 'error'


_clang_clean = {}


def clang_accepts_original(name):
    if name not in _clang_clean:
        _clang_clean[name] = clang_error(ROOT / 'tests' / name) is None
    return _clang_clean[name]


def classify(rc, out, path, text, accepts=None):
    """Return (class, signature), or (None, None) for a clean result.
    accepts names the unmutated program when dcc's acceptance is checked."""
    if rc is None:
        return 'hang', 'hang'
    m = ASAN.search(out)
    if m:
        frame = FRAME.search(out, m.end())
        return 'crash', 'asan %s in %s' % (m.group(1), frame.group(1) if frame else '?')
    m = UBSAN.search(out)
    if m:
        return 'crash', 'ubsan %s: %s' % (m.group(1), re.sub(r'-?\d+', 'N', m.group(2)))
    if rc < 0 or rc > 128:
        return 'crash', 'signal %d' % (-rc if rc < 0 else rc - 128)
    lines = text.count('\n') + 1
    diagnostics = DIAGNOSTIC.findall(out)
    for file, line, _, message in diagnostics:
        if Path(file).name == path.name and not 0 < int(line) <= lines:
            return 'badline', 'line %s: %s' % ('<= 0' if int(line) <= 0 else 'past end',
                                              re.sub(r" near '.*", '', message).strip())
    if rc == 0:
        if accepts is None or not clang_accepts_original(accepts):
            return None, None
        message = clang_error(path)
        # dcc drops a system header it cannot find, and clang's own nesting
        # limit is not a C rule.
        if message is None or 'file not found' in message or 'nesting level' in message:
            return None, None
        return 'accepted', re.sub(r"'[^']*'", "'X'", re.sub(r'\d+', 'N', message))
    if any(severity == 'error' for _, _, severity, _ in diagnostics):
        return None, None
    m = FATAL.search(out)
    if m:
        return 'fatal', re.sub(r'\d+', 'N', m.group(1))
    tail = out.strip().splitlines()[-1] if out.strip() else '<no output>'
    return 'silent', re.sub(r'\d+', 'N', tail)[:120]


def check(seed, dcc, work, timeout, accepts):
    name, text = generate(seed)
    path = work / ('s%d.c' % seed)
    path.write_text(text, encoding='latin-1')
    rc, out = run_dcc(dcc, path, timeout)
    kind, signature = classify(rc, out, path, text, name if accepts else None)
    messages = [re.sub(r" near '.*", '', m).strip() for _, _, _, m in DIAGNOSTIC.findall(out)]
    if kind is None:
        path.unlink()
    else:
        path.with_suffix('.out').write_text(out, encoding='latin-1')
    return seed, name, kind, signature, messages


def parse_seeds(text):
    seeds = []
    for part in text.split(','):
        lo, _, hi = part.partition('-')
        seeds.extend(range(int(lo), int(hi or lo) + 1))
    return seeds


def fuzz(args):
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    counts = collections.Counter()
    signatures = collections.OrderedDict()
    messages = collections.Counter()
    seeds = parse_seeds(args.seeds)
    total = len(seeds)
    with progress_terminal() as terminal, concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        show_progress = total > 0 and terminal is not None
        update_progress, report = progress_reporter(total, terminal) if show_progress else (None, print)
        jobs = [pool.submit(check, s, args.dcc, work, args.timeout, args.accepts) for s in seeds]
        if show_progress:
            update_progress(0)
        completed = 0
        for job in concurrent.futures.as_completed(jobs):
            completed += 1
            if show_progress and completed % 10 == 0 and completed < total:
                update_progress(completed)
            seed, name, kind, signature, seen = job.result()
            counts[kind or 'ok'] += 1
            messages.update(seen)
            if kind is not None:
                key = (kind, signature)
                if key not in signatures:
                    report('seed %d (%s): %s: %s' % (seed, name, kind, signature))
                signatures.setdefault(key, []).append(seed)
        if show_progress:
            update_progress(total)
            print(file=terminal, flush=True)
    print('\n%d inputs: %s' % (sum(counts.values()),
          ', '.join('%s %d' % (k, counts[k]) for k in sorted(counts))))
    for (kind, signature), seeds in signatures.items():
        print('  %-7s %4d  seed %-6d %s' % (kind, len(seeds), min(seeds), signature))
    if args.messages:
        print('\nDiagnostics by message:')
        for message, n in messages.most_common():
            print('  %6d  %s' % (n, message))
    return 1 if signatures else 0


def reduce(args):
    """Delete tokens while dcc's result keeps the same class and signature."""
    work = Path(args.work)
    work.mkdir(parents=True, exist_ok=True)
    name, text = generate(args.reduce)
    path = work / ('r%d.c' % args.reduce)

    def result(candidate):
        path.write_text(candidate, encoding='latin-1')
        rc, out = run_dcc(args.dcc, path, args.timeout)
        return classify(rc, out, path, candidate, name if args.accepts else None)

    target = result(text)
    if target[0] is None:
        print('seed %d is clean' % args.reduce)
        return 1
    print('reducing seed %d (%s): %s: %s' % (args.reduce, name, target[0], target[1]), flush=True)
    tokens = [t for t in tokenize(text) if not t.isspace() and not t.startswith(('/*', '//'))]
    lines = text.split('\n')
    # First whole lines, which keeps preprocessor directives intact, then tokens.
    for units, join in ((lines, '\n'), (tokens, ' ')):
        n = max(1, len(units) // 2)
        while n >= 1:
            i = 0
            while i < len(units):
                trial = units[:i] + units[i + n:]
                if trial and result(join.join(trial)) == target:
                    units[:] = trial
                else:
                    i += n
            n //= 2
        if join == '\n':
            tokens[:] = [t for t in tokenize('\n'.join(units))
                         if not t.isspace() and not t.startswith(('/*', '//'))]
    reduced = ' '.join(tokens)
    out = work / ('reduced-%d.c' % args.reduce)
    out.write_text(reduced + '\n', encoding='latin-1')
    print('%d tokens -> %s' % (len(tokens), out))
    print(reduced[:2000])
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--seeds', default='1-200')
    parser.add_argument('--dcc', default=str(ROOT / ('dcc.exe' if os.name == 'nt' else 'dcc')))
    parser.add_argument('--work', default=str(ROOT / 'build' / 'syntax-crash-fuzz'))
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--accepts', action='store_true',
                        help='also report inputs dcc compiles that clang rejects')
    parser.add_argument('--messages', action='store_true', help='list every diagnostic message and its count')
    parser.add_argument('--reduce', type=int, help='shrink this seed\'s failure')
    parser.add_argument('--generate', nargs=2, metavar=('SEED', 'C'), help='only write seed SEED\'s input')
    parser.add_argument('--directives', action='store_true',
                        help='mutate only preprocessor directive lines')
    args = parser.parse_args()
    global directives_only
    directives_only = args.directives
    if args.generate:
        name, text = generate(int(args.generate[0]))
        Path(args.generate[1]).write_text(text, encoding='latin-1')
        print('from ' + name)
        return 0
    return reduce(args) if args.reduce is not None else fuzz(args)


if __name__ == '__main__':
    sys.exit(main())
