"""Generate small, defined programs that exercise dcc's C frontend."""
from dataclasses import dataclass
import random


@dataclass(frozen=True)
class Probe:
    name: str
    prelude: tuple[str, ...]
    expressions: tuple[str, ...]
    expected: tuple[int, ...]
    headers: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True)
class Program:
    seed: int
    probes: tuple[Probe, ...]

    def expected(self):
        return ''.join(probe.name + ' ' + ' '.join(map(str, probe.expected)) + '\n'
                       for probe in self.probes)

    def headers(self):
        return tuple(header for probe in self.probes for header in probe.headers)


def branch_probe(name, route, condition, selected, definitions, header='', marker=''):
    result = 'OR_' + name.replace('-', '_').upper()
    prelude = list(definitions)
    if route == 'elif':
        prelude.extend(('#if 0', '#elif ' + condition))
    else:
        prelude.append('#if ' + condition)
    if route == 'include':
        prelude.extend((f'#include <{header}>', '#endif', '#ifdef ' + marker))
    prelude.extend((f'#define {result} 1', '#else', f'#define {result} 0', '#endif'))
    return Probe(name, tuple(prelude), (result,), (int(selected),))


def macro_probes(seed, rng):
    for index, route in enumerate(('if', 'elif', 'include')):
        name = 'OR_SUM_' + route.upper()
        scale = rng.randint(2, 9)
        left, right = rng.randint(10, 60), rng.randint(1, 9)
        op = ('+', '-')[seed % 2]
        tail = right if op == '+' else -right
        definitions = [f'#define {name} {left} {op} {right}']
        if (seed // 4 + index) % 2:
            definitions.append(f'#define {name}_ALIAS {name}')
            name += '_ALIAS'
        selected = (seed // 2 + index) % 2 == 0
        relation = '==' if selected else '!='
        condition = f'{scale} * {name} {relation} {scale * left + tail}'
        yield branch_probe('pp-macro-' + route, route, condition, selected,
                           definitions, 'limits.h', 'CHAR_BIT')


def unsigned_probes(seed, rng):
    for index, route in enumerate(('if', 'elif', 'include')):
        negative = 2 * rng.randint(0, 15) + 1
        positive = rng.randint(1, 32)
        modulus = rng.choice((2, 4, 8, 16))
        shift = rng.randint(1, 8)
        # A power-of-two modulus gives the same residue at either host width.
        predicates = (
            f'-{negative} > {positive}U',
            f'{positive}u < -{negative}',
            f'(-{negative}U % {modulus}U) == {-negative % modulus}',
            f'(-1U >> {shift}) > 0',
        )
        variant = seed + index
        predicate = predicates[variant % len(predicates)]
        selected = variant // len(predicates) % 2 == 0
        condition = predicate if selected else f'!({predicate})'
        yield branch_probe('pp-unsigned-' + route, route, condition, selected,
                           (), 'float.h', 'FLT_RADIX')


def literal_checks(literal, value, base, suffix):
    suffix = suffix.upper()
    width = 'sizeof(long)' if 'L' in suffix else 'sizeof(int)'
    # For these magnitudes, only dcc's 16-bit int can need a wider type.
    # Derive that width from a named type so the same checks run on native clang.
    # Comparing to -1 also checks signedness at equal signed and unsigned widths.
    signed = '0' if 'U' in suffix else '1'
    if suffix == 'U' and value > 65535:
        width = '(sizeof(int) == 2 ? sizeof(unsigned long) : sizeof(unsigned int))'
    elif not suffix and value > 32767:
        if value > 65535 or base == 'decimal':
            width = '(sizeof(int) == 2 ? sizeof(long) : sizeof(int))'
        else:
            signed = '(sizeof(int) != 2)'
    return (f'(unsigned long)({literal}) == {value}UL',
            f'sizeof({literal}) == {width}',
            f'(({literal}) > -1) == {signed}')


def literal_probes(seed, rng):
    word = (32768, 65535, rng.randint(32768, 65535))[seed % 3]
    boundary = rng.choice((0, 1, 32766, 32767, 32768, 32769, 65534, 65535, 65536, 65537))
    suffix = rng.choice(('', 'u', 'U', 'l', 'L', 'ul', 'LU'))
    for group, value, ending in (('word', word, ''), ('boundary', boundary, suffix)):
        for base, spelling in (('decimal', str(value)), ('octal', f'0{value:o}'),
                               ('hex', f'0x{value:x}')):
            name = f'literal-{group}-{base}'
            literal = spelling + ending
            yield Probe(name, (), literal_checks(literal, value, base, ending), (1, 1, 1))
            macro = f'OR_{group.upper()}_{base.upper()}'
            yield Probe(name + '-macro', (f'#define {macro} {literal}',),
                        literal_checks(macro, value, base, ending), (1, 1, 1))


def rescan_probes(_seed, rng):
    index = rng.randint(1, 9)
    value = rng.randint(11, 99)
    names = (
        '#define OR_CAT(a, b) a ## b',
        '#define OR_JOIN(a, b) OR_CAT(a, b)',
        f'#define OR_SUFFIX {index}',
        f'#define OR_ITEM{index} {value}',
    )
    route = rng.choice(('if', 'elif'))
    yield branch_probe('pp-rescan', route,
                       f'OR_JOIN(OR_ITEM, OR_SUFFIX) == {value}', True, names)
    raw_value = rng.randint(3, 12)
    yield Probe('pp-raw-paste', (
        '#define OR_RAW_CAT(a, b) a ## b',
        '#define OR_RAW_TAG 9',
        f'#define OR_RAW_ITEM_OR_RAW_TAG {raw_value}',
    ), ('OR_RAW_CAT(OR_RAW_ITEM_, OR_RAW_TAG)',), (raw_value,))

    left, right = rng.randint(1, 16), rng.randint(1, 16)
    op = rng.choice(('+', '-', '*'))
    total = {'+': left + right, '-': left - right, '*': left * right}[op]
    macros = (
        f'#define OR_BIN(a, b) ((a) {op} (b))',
        '#define OR_FORWARD(f, ...) f(__VA_ARGS__)',
        '#define OR_EMPTY(a) (a + 7)',
    )
    yield branch_probe('pp-variadic', route,
                       f'OR_FORWARD(OR_BIN, {left}, {right}) == {total} && OR_EMPTY() == 7',
                       True, macros)


def constant_expression(rng, depth):
    if not depth or rng.randrange(4) == 0:
        value = rng.randint(0, 6)
        return str(value), value
    left, a = constant_expression(rng, depth - 1)
    right, b = constant_expression(rng, depth - 1)
    op = rng.choice(('+', '*', '^', '&', '|'))
    return f'({left} {op} {right})', {'+': a + b, '*': a * b, '^': a ^ b,
                                     '&': a & b, '|': a | b}[op]


def conditional_probes(_seed, rng):
    value = rng.randint(1, 31)
    first = rng.randrange(2)
    expr, expected = constant_expression(rng, 2)
    return (Probe('pp-conditional', (
        '#define OR_LIVE 1',
        f'#if {first}',
        '#define OR_RESULT 1',
        f'#elif defined(OR_LIVE) && !defined OR_MISSING && (1 ? {expr} : 1/0) == {expected}',
        f'#define OR_RESULT {value + 1}',
        '#else',
        '#define OR_RESULT 0',
        '#endif',
        '#if 0 && (1/0)',
        '#define OR_SKIPPED 1',
        '#endif',
        '#undef OR_LIVE',
        '#if defined OR_LIVE || defined(OR_SKIPPED)',
        '#define OR_CLEARED 0',
        '#else',
        '#define OR_CLEARED 1',
        '#endif',
    ), ('OR_RESULT', 'OR_CLEARED'), (1 if first else value + 1, 1)),)


def include_probes(seed, rng):
    value = rng.randint(1, 63)
    mode = rng.randrange(2)
    variant = rng.choice(('quoted', 'macro', 'guarded-macro'))
    # Prefix the names with the seed so exported programs can share a directory.
    prefix = f'oracle_{seed}_'
    outer, inner = prefix + 'outer.h', prefix + 'inner.h'
    quoted_inner = '\n'.join((
        '#ifndef OR_INNER_GUARD',
        '#define OR_INNER_GUARD',
        f'#if OR_INCLUDE_MODE == {mode}',
        f'#define OR_INCLUDED {value}',
        '#else',
        f'#define OR_INCLUDED {value + 1}',
        '#endif',
        '#endif',
        '',
    ))
    nested = (f'#include "{inner}"',) if variant == 'quoted' else (
        f'#define OR_NESTED_HEADER "{inner}"',
        '#include OR_NESTED_HEADER',
        '#undef OR_NESTED_HEADER',
    )
    quoted_outer = '\n'.join((
        '#ifndef OR_OUTER_GUARD',
        '#define OR_OUTER_GUARD',
        *nested,
        '#endif',
        '',
    ))
    repeated = (f'#include "{outer}"',) if variant != 'macro' else ()
    return (Probe(f'pp-nested-includes-{variant}', (
        f'#define OR_INCLUDE_MODE {mode}',
        f'#define OR_OUTER_HEADER "{outer}"',
        '#include OR_OUTER_HEADER',
        '#undef OR_INCLUDE_MODE',
        f'#define OR_INCLUDE_MODE {1 - mode}',
        *repeated,
        f'#include "{inner}"',
        '#ifndef OR_INCLUDED',
        '#define OR_INCLUDED 0',
        '#endif',
    ), ('OR_INCLUDED',), (value,), ((outer, quoted_outer), (inner, quoted_inner))),)


def lexical_probes(_seed, rng):
    value = rng.randint(48, 90)
    spelling = rng.choice((rf'\x{value:02x}', rf'\{value:03o}'))
    yield Probe('lex-string', (
        '#define OR_STR_RAW(x) #x',
        '#define OR_STR(x) OR_STR_RAW(x)',
        '#define OR_WORD xy',
        rf'static const char or_text[] = "{spelling}" OR_STR(OR_WORD) "\n";',
    ), ('sizeof(or_text)', 'or_text[0]', 'or_text[1]', 'or_text[2]', 'or_text[3]'),
       (5, value, ord('x'), ord('y'), 10))

    number = rng.randint(100, 200)
    line_directive = rng.choice(('#line OR_LINE_NUM', f'#line {number}'))
    yield Probe('lex-line', (
        f'#define OR_LINE_NUM {number}',
        line_directive,
        'enum { or_line_num = __LINE__ };',
        f'#if __LINE__ == {number + 1}',
        '#define OR_LINE_MATCH 1',
        '#else',
        '#define OR_LINE_MATCH 0',
        '#endif',
    ), ('or_line_num', 'OR_LINE_MATCH'), (number, 1))


def declarator_probes(_seed, rng):
    val = rng.randint(1, 40)
    index = rng.randrange(3)
    callback = rng.randrange(2)
    qualifiers = rng.choice(('const', 'static restrict', 'volatile'))
    callback_parameter = rng.choice(('OR_Call fn', 'OR_Call *fn', 'int fn(int)', 'int (*fn)(int)'))
    yield Probe('decl-callback-array', (
        'typedef int OR_Row[3];',
        'typedef int OR_Call(int);',
        'static int or_add(int x) { return x + 2; }',
        'static int or_sub(int x) { return x - 3; }',
        f'static int or_apply({callback_parameter}, int row[{qualifiers} 3]) {{',
        f'    return fn(row[{index}]);',
        '}',
        'static int or_decl(void) {',
        f'    OR_Row rows[2] = {{{{1, 2, 3}}, {{{val}, {val + 1}, {val + 2}}}}};',
        '    OR_Call *table[2] = { or_add, or_sub };',
        f'    return or_apply(table[{callback}], rows[1]);',
        '}',
    ), ('or_decl()',), (val + index + (2 if callback == 0 else -3),))

    yield Probe('decl-typedef-scope', (
        'typedef int OR_Word;',
        'static int or_typedef_scope(void) {',
        f'    OR_Word total = {val};',
        '    {',
        '        typedef signed char OR_Word;',
        '        OR_Word byte = -2;',
        '        total += byte + (int)sizeof(OR_Word);',
        '    }',
        '    return total + (sizeof(OR_Word) == sizeof(int));',
        '}',
    ), ('or_typedef_scope()',), (val,))

    yield Probe('decl-old-style', (
        'static int or_old(a, b)',
        '    int a;',
        '    long b;',
        '{',
        '    return a + (int)b;',
        '}',
    ), (f'or_old({val}, {index + 1}L)',), (val + index + 1,))


def aggregate_probes(_seed, rng):
    val = rng.randint(1, 40)
    slot = rng.randrange(4)
    other = (slot + 1) % 4
    nested = rng.choice((f'[{other}].left = {val + 2}',
                         f'[{other}] = {{ .left = {val + 2} }}'))
    yield Probe('init-designated', (
        'struct OR_Cell { int left; int right; };',
        'static struct OR_Cell or_grid[4] = {',
        f'    [{slot}] = {{ .right = {val + 1}, .left = {val} }},',
        f'    {nested}',
        '};',
        'static int or_local_init(void) {',
        f'    struct OR_Cell cells[4] = {{ [{slot}] = {{ .left = {val} }} }};',
        f'    return cells[{slot}].left + cells[{other}].right;',
        '}',
    ), (f'or_grid[{slot}].right', f'or_grid[{other}].left',
        f'or_grid[{other}].right', 'or_local_init()'), (val + 1, val + 2, 0, val))

    yield Probe('init-compound-anonymous', (
        'struct OR_Record { int tag; union { int payload; unsigned int bits; }; };',
        f'static struct OR_Record *or_record = &(struct OR_Record){{ .payload = {val}, .tag = 3 }};',
        'static int or_compound(void) {',
        f'    struct OR_Record *local = &(struct OR_Record){{ .tag = 4, .payload = {val + 1} }};',
        '    return or_record->payload + or_record->tag + local->payload + local->tag;',
        '}',
    ), ('or_compound()',), (2 * val + 8,))


def constant_probes(_seed, rng):
    width = rng.randint(2, 6)
    shift = rng.randrange(4)
    expr, expected = constant_expression(rng, 2)
    return (Probe('parse-constant-expr', (
        f'enum OR_Choice {{ OR_COUNT = {width}, OR_ICE = {expr},',
        f'                 OR_CASE = (OR_COUNT << {shift}) + (OR_ICE % 5), }};',
        'struct OR_Sized {',
        '    _Static_assert(OR_COUNT > 1, "positive dimension");',
        '    int row[OR_COUNT];',
        '};',
        f'_Static_assert(OR_ICE == {expected}, "enum expression");',
        'static int or_const(int n) {',
        '    struct OR_Sized item;',
        '    _Static_assert(sizeof item.row / sizeof(int) == OR_COUNT, "array bound");',
        '    switch (n) {',
        '        case OR_CASE: return (int)(sizeof item.row / sizeof(int));',
        '        default: return 0;',
        '    }',
        '}',
    ), ('or_const(OR_CASE)', 'or_const(OR_CASE + 1)', '(_Bool)OR_ICE'),
       (width, 0, int(expected != 0))),)


def vla_probes(_seed, rng):
    n = rng.randint(2, 5)
    cols = rng.randint(1, 4)
    row = '' if cols == 1 else f'[{cols}]'
    element = 'matrix[n - 1]' if cols == 1 else f'matrix[n - 1][{cols - 1}]'
    exit_kind = rng.choice(('fallthrough', 'continue', 'break', 'goto', 'return'))
    jump = {'continue': 'continue;', 'break': 'break;', 'goto': 'goto done;',
            'return': 'return total;'}.get(exit_kind)
    branch = (f'        if (i == 1) {jump}',) if jump else ()
    label = ('done:',) if exit_kind == 'goto' else ()
    amount = n * cols
    expected = {'fallthrough': 3 * amount + 6, 'continue': 3 * amount + 5}.get(
        exit_kind, 2 * amount + 2)
    return (Probe('parse-vla-scope', (
        'static int or_vla(int n) {',
        '    int total = 0;',
        '    int i;',
        '    for (i = 0; i < 3; ++i) {',
        '        int count = n;',
        f'        int matrix[count]{row};',
        f'        {element} = i;',
        '        ++count;',
        f'        total += (int)(sizeof matrix / sizeof(int)) + {element};',
        *branch,
        '        ++total;',
        '    }',
        *label,
        '    return total;',
        '}',
    ), (f'or_vla({n})',), (expected,)),)


REGRESSION_GENERATORS = {
    'pp-macro': macro_probes,
    'pp-unsigned': unsigned_probes,
    'literal-types': literal_probes,
}
BROAD_GENERATORS = {
    'pp-rescan': rescan_probes,
    'pp-conditional': conditional_probes,
    'pp-includes': include_probes,
    'lex-tokens': lexical_probes,
    'declarators': declarator_probes,
    'initializers': aggregate_probes,
    'constant-expr': constant_probes,
    'vla-scope': vla_probes,
}
FEATURES = frozenset(REGRESSION_GENERATORS) | frozenset(BROAD_GENERATORS)


def generate(seed, off=frozenset()):
    rng = random.Random(seed)
    probes = []
    for feature, create in REGRESSION_GENERATORS.items():
        group = tuple(create(seed, rng))
        if feature not in off:
            probes.extend(group)
    # Rotate the broader families so a compile rejection in one does not
    # prevent the others from reaching the runtime comparisons.
    enabled = [feature for feature in BROAD_GENERATORS if feature not in off]
    if enabled:
        feature = enabled[seed % len(enabled)]
        probes.extend(BROAD_GENERATORS[feature](seed, random.Random(f'{seed}:{feature}')))
    return Program(seed, tuple(probes))


def render(prog, title=''):
    lines = [f'/* generated by clang-oracle-fuzz.py --gen frontend{title} */']
    for probe in prog.probes:
        lines.extend(probe.prelude)
    # Keep this after the probes so stdio cannot supply their include markers.
    lines.extend(('', '#include <stdio.h>', '', 'int main(void)', '{'))
    for probe in prog.probes:
        fmt = probe.name + ' %d' * len(probe.expressions)
        args = ', '.join(f'(int)({expr})' for expr in probe.expressions)
        lines.append(f'    printf("{fmt}\\n", {args});')
    lines.extend(('    return 0;', '}'))
    return '\n'.join(lines) + '\n'
