/**
 * @file dcc_pp_expr.c
 * @brief Evaluates preprocessor #if and #elif expression text.
 *
 * @par Role
 * Expands the macros in a conditional's text (object-like and function-like,
 * leaving the operand of defined alone), then evaluates it with a private
 * recursive-descent parser over integer and character constants, defined(),
 * unary, binary, logical, conditional and comma operators.
 *
 * Arithmetic follows the target's long and unsigned long: 32 bits, with the
 * usual conversions to unsigned, so the result does not depend on the host's
 * long. The first malformed construct is recorded for the caller to report:
 * a missing operand or ')', an invalid or floating constant, a string, an
 * undefined function-like macro, stray tokens, and division by zero in an
 * evaluated operand.
 *
 * @par Key entry points
 * pp_eval_simple_expr(), pp_eval_expr_error().
 *
 * @par Boundary
 * Macro-table lookup, argument reading and character escape decoding are
 * provided by dcc_preproc.c. This evaluator implements preprocessing
 * arithmetic, not the typed C constant semantics in dcc_fold.c.
 */

#include "dcc_preproc_internal.h"

#define PP_MASK 0xffffffffUL
#define PP_SIGN 0x80000000UL
#define PP_MAX_DEPTH 256
#define PP_MAX_EXPAND_DEPTH 32

struct PPVal {
    unsigned long v;    /* the 32-bit value */
    int u;              /* unsigned long rather than long */
};

static const char *pp_expr_p;
static int pp_expr_depth;
static int pp_expr_eval;        /* operands are evaluated, not skipped */
static char pp_expr_err[160];

static void pp_expr_fail(const char *msg)
{
    if (!pp_expr_err[0]) {
        strncpy(pp_expr_err, msg, sizeof(pp_expr_err) - 1);
        pp_expr_err[sizeof(pp_expr_err) - 1] = 0;
    }
}

static void pp_expr_fail_token(const char *fmt, const char *tok, int len)
{
    char buf[160];
    char text[40];

    if (len > (int)sizeof(text) - 1)
        len = (int)sizeof(text) - 1;
    memcpy(text, tok, (size_t)len);
    text[len] = 0;
    sprintf(buf, fmt, text);
    pp_expr_fail(buf);
}

static long pp_signed(struct PPVal a)
{
    unsigned long v = a.v & PP_MASK;
    if (v == PP_SIGN)
        return -2147483647L - 1;
    return (v & PP_SIGN) ? -(long)((~v & PP_MASK) + 1UL) : (long)v;
}

static struct PPVal pp_make(unsigned long v, int u)
{
    struct PPVal r;
    r.v = v & PP_MASK;
    r.u = u;
    return r;
}

static struct PPVal pp_bool(int b)
{
    return pp_make(b ? 1UL : 0UL, 0);
}

/* ------------------------------------------------------------ expansion */

static const char *pp_disabled[PP_MAX_EXPAND_DEPTH];
static int pp_ndisabled;

static int pp_is_disabled(const char *name)
{
    int i;
    for (i = 0; i < pp_ndisabled; ++i)
        if (!strcmp(pp_disabled[i], name))
            return 1;
    return 0;
}

struct PPOut {
    char *buf;
    int len;
    int cap;
};

static void pp_out_put(struct PPOut *o, const char *s, int n)
{
    if (o->len + n >= o->cap) {
        pp_expr_fail("preprocessor expression too long");
        n = o->cap - 1 - o->len;
        if (n <= 0)
            return;
    }
    memcpy(o->buf + o->len, s, (size_t)n);
    o->len += n;
    o->buf[o->len] = 0;
}

static int pp_expand(const char *in, struct PPOut *o, int depth);

/* Copy `defined X` or `defined ( X )` unexpanded; p is just past "defined". */
static const char *pp_copy_defined_operand(const char *p, struct PPOut *o)
{
    const char *q = p;
    int paren = 0;

    while (*q && isspace((unsigned char)*q))
        q++;
    if (*q == '(') {
        paren = 1;
        q++;
        while (*q && isspace((unsigned char)*q))
            q++;
    }
    while (is_ident_char((unsigned char)*q))
        q++;
    if (paren) {
        while (*q && isspace((unsigned char)*q))
            q++;
        if (*q == ')')
            q++;
    }
    pp_out_put(o, p, (int)(q - p));
    return q;
}

/* Expand macro di, whose name ends at *pp. Returns 0, leaving *pp alone, when
 * a function-like name is not followed by '(' and so is not a call. */
static int pp_expand_macro(int di, const char *name, const char **pp,
                           struct PPOut *o, int depth)
{
    char *body;

    if (depth >= PP_MAX_EXPAND_DEPTH) {
        pp_expr_fail("macro expansion nested too deeply in preprocessor expression");
        return 0;
    }
    if (defs[di].is_func) {
        char (*args)[MAX_MACRO_ARG_LEN];
        const char *q = *pp;
        int nargs = 0;

        while (*q && isspace((unsigned char)*q))
            q++;
        if (*q != '(')
            return 0;
        args = (char (*)[MAX_MACRO_ARG_LEN])xmalloc(
            sizeof(char[MAX_MACRO_ARGS][MAX_MACRO_ARG_LEN]));
        q = *pp;
        if (!read_macro_call_args_text(&q, args, &nargs,
                defs[di].is_variadic ? defs[di].nargs - 1 : -1)) {
            free(args);
            pp_expr_fail_token("unterminated function-like macro invocation of '%s'",
                               name, (int)strlen(name));
            return 0;
        }
        if (macro_call_arg_overflow) {
            free(args);
            pp_expr_fail("macro argument too long in function-like macro invocation");
            return 0;
        }
        if (!macro_call_arity_matches(di, &nargs)) {
            free(args);
            pp_expr_fail_token(nargs > defs[di].nargs
                ? "too many arguments provided to function-like macro invocation of '%s'"
                : "too few arguments provided to function-like macro invocation of '%s'",
                name, (int)strlen(name));
            return 0;
        }
        body = (char *)xmalloc(MAX_MACRO_TEXT);
        expand_function_macro(di, args, body, MAX_MACRO_TEXT);
        free(args);
        *pp = q;
    } else {
        body = (char *)xmalloc(MAX_MACRO_TEXT);
        strncpy(body, defs[di].value, MAX_MACRO_TEXT - 1);
        body[MAX_MACRO_TEXT - 1] = 0;
    }
    pp_disabled[pp_ndisabled++] = defs[di].name;
    pp_out_put(o, " ", 1);
    pp_expand(body, o, depth + 1);
    pp_out_put(o, " ", 1);
    pp_ndisabled--;
    free(body);
    return 1;
}

/* Expand macros in `in`, appending to o. Strings, character constants and
 * numbers are copied whole so nothing inside them is taken for a name. */
static int pp_expand(const char *in, struct PPOut *o, int depth)
{
    const char *p = in;

    while (*p && !pp_expr_err[0]) {
        if (*p == '"' || *p == '\'') {
            const char *q = p + 1;
            while (*q && *q != *p) {
                if (*q == '\\' && q[1])
                    q++;
                q++;
            }
            if (*q)
                q++;
            pp_out_put(o, p, (int)(q - p));
            p = q;
        } else if (isdigit((unsigned char)*p) ||
                   (*p == '.' && isdigit((unsigned char)p[1]))) {
            const char *q = p + 1;
            while (is_ident_char((unsigned char)*q) || *q == '.' ||
                   ((*q == '+' || *q == '-') &&
                    (q[-1] == 'e' || q[-1] == 'E' || q[-1] == 'p' || q[-1] == 'P')))
                q++;
            pp_out_put(o, p, (int)(q - p));
            p = q;
        } else if (is_ident_start((unsigned char)*p)) {
            char name[64];
            const char *q = p;
            int n = 0;
            int di;

            while (is_ident_char((unsigned char)*q)) {
                if (n < (int)sizeof(name) - 1)
                    name[n++] = *q;
                q++;
            }
            name[n] = 0;
            if (!strcmp(name, "defined")) {
                pp_out_put(o, p, (int)(q - p));
                p = pp_copy_defined_operand(q, o);
                continue;
            }
            if (!strcmp(name, "__LINE__")) {
                char num[24];
                sprintf(num, "%d", g_lex.line_no);
                pp_out_put(o, num, (int)strlen(num));
                p = q;
                continue;
            }
            di = find_define(name);
            if (di >= 0 && !pp_is_disabled(defs[di].name)) {
                const char *after = q;
                if (pp_expand_macro(di, name, &after, o, depth)) {
                    p = after;
                    continue;
                }
                if (pp_expr_err[0])
                    return 0;
            }
            pp_out_put(o, p, (int)(q - p));
            p = q;
        } else {
            pp_out_put(o, p, 1);
            p++;
        }
    }
    return !pp_expr_err[0];
}

/* ------------------------------------------------------------ evaluation */

static void pp_expr_skip_ws(void)
{
    while (*pp_expr_p && isspace((unsigned char)*pp_expr_p))
        pp_expr_p++;
}

static struct PPVal pp_expr_number(void)
{
    const char *start = pp_expr_p;
    const char *p = pp_expr_p + 1;
    const char *digits;
    const char *suffix;
    const char *end;
    unsigned long v = 0;
    int base = 10;
    int too_big = 0;
    int nl = 0;
    int nu = 0;

    /* The whole preprocessing number, so 1.5, 1e3 and 09 are seen whole. */
    while (is_ident_char((unsigned char)*p) || *p == '.' ||
           ((*p == '+' || *p == '-') && (p[-1] == 'e' || p[-1] == 'E')))
        p++;
    end = p;
    pp_expr_p = end;

    if (memchr(start, '.', (size_t)(end - start)) != NULL) {
        pp_expr_fail_token("floating constant '%s' in preprocessor expression",
                           start, (int)(end - start));
        return pp_make(0, 0);
    }

    p = start;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (p[0] == '0') {
        base = 8;
    }
    digits = p;
    for (;;) {
        int d;
        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if (base == 16 && *p >= 'a' && *p <= 'f')
            d = *p - 'a' + 10;
        else if (base == 16 && *p >= 'A' && *p <= 'F')
            d = *p - 'A' + 10;
        else
            break;
        if (d >= base)
            break;
        if (v > (PP_MASK - (unsigned long)d) / (unsigned long)base)
            too_big = 1;
        v = (v * (unsigned long)base + (unsigned long)d) & PP_MASK;
        p++;
    }
    suffix = p;
    while (p < end) {
        if (*p == 'u' || *p == 'U')
            nu++;
        else if ((*p == 'l' || *p == 'L') && (nl == 0 || p[-1] == *p))
            nl++;
        else
            break;
        p++;
    }
    if (p != end || nu > 1 || nl > 2 || (base == 16 && suffix == digits)) {
        if (base == 10 && (*suffix == 'e' || *suffix == 'E'))
            pp_expr_fail_token("floating constant '%s' in preprocessor expression",
                               start, (int)(end - start));
        else
            pp_expr_fail_token("invalid integer constant '%s' in preprocessor expression",
                               start, (int)(end - start));
        return pp_make(0, 0);
    }
    if (too_big) {
        pp_expr_fail_token("integer constant '%s' is too large for a preprocessor expression",
                           start, (int)(end - start));
        return pp_make(0, 0);
    }
    /* A constant beyond long's range is unsigned long. */
    return pp_make(v, nu > 0 || (v & PP_SIGN) != 0);
}

static struct PPVal pp_expr_charlit(void)
{
    const char *p = pp_expr_p + 1;
    unsigned long v = 0;
    int n = 0;

    while (*p && *p != '\'') {
        int c = parse_escape_string_char(&p);
        v = (v << 8) | (unsigned long)(c & 255);
        n++;
    }
    if (*p != '\'') {
        pp_expr_p = p;
        pp_expr_fail("unterminated character constant in preprocessor expression");
        return pp_make(0, 0);
    }
    pp_expr_p = p + 1;
    if (n == 0) {
        pp_expr_fail("empty character constant in preprocessor expression");
        return pp_make(0, 0);
    }
    /* In #if a character constant keeps its byte value: '\377' is 255
     * (C leaves its sign to the implementation; tests/tppreg.c pins it). */
    return pp_make(v, 0);
}

static struct PPVal pp_expr_comma(void);
static struct PPVal pp_expr_cond(void);

static struct PPVal pp_expr_defined(void)
{
    char name[64];
    int i = 0;
    int paren = 0;

    pp_expr_skip_ws();
    if (*pp_expr_p == '(') {
        paren = 1;
        pp_expr_p++;
        pp_expr_skip_ws();
    }
    if (!is_ident_start((unsigned char)*pp_expr_p)) {
        if (*pp_expr_p == 0 || *pp_expr_p == ')')
            pp_expr_fail("macro name missing after 'defined'");
        else
            pp_expr_fail("macro name after 'defined' must be an identifier");
        return pp_make(0, 0);
    }
    while (is_ident_char((unsigned char)*pp_expr_p)) {
        if (i < (int)sizeof(name) - 1)
            name[i++] = *pp_expr_p;
        pp_expr_p++;
    }
    name[i] = 0;
    if (paren) {
        pp_expr_skip_ws();
        if (*pp_expr_p == ')')
            pp_expr_p++;
        else
            pp_expr_fail("missing ')' in preprocessor expression");
    }
    return pp_bool(find_define(name) >= 0);
}

static struct PPVal pp_expr_primary(void)
{
    struct PPVal v;
    const char *p;

    pp_expr_skip_ws();
    p = pp_expr_p;

    if (*p == '(') {
        pp_expr_p++;
        v = pp_expr_comma();
        pp_expr_skip_ws();
        if (*pp_expr_p == ')')
            pp_expr_p++;
        else
            pp_expr_fail("missing ')' in preprocessor expression");
        return v;
    }
    if (*p == '\'')
        return pp_expr_charlit();
    if (isdigit((unsigned char)*p) || (*p == '.' && isdigit((unsigned char)p[1])))
        return pp_expr_number();
    if (*p == '"' || (*p == 'L' && (p[1] == '"' || p[1] == '\''))) {
        pp_expr_fail(*p == '"' || p[1] == '"'
                         ? "string literal in preprocessor expression"
                         : "wide character constant in preprocessor expression");
        return pp_make(0, 0);
    }
    if (is_ident_start((unsigned char)*p)) {
        const char *q = p;
        while (is_ident_char((unsigned char)*q))
            q++;
        pp_expr_p = q;
        if (q - p == 7 && !strncmp(p, "defined", 7))
            return pp_expr_defined();
        pp_expr_skip_ws();
        if (*pp_expr_p == '(') {
            pp_expr_fail_token("function-like macro '%s' is not defined",
                               p, (int)(q - p));
            return pp_make(0, 0);
        }
        /* An identifier that is not a macro is 0. */
        return pp_make(0, 0);
    }
    if (*p == 0)
        pp_expr_fail("expected value in preprocessor expression");
    else
        pp_expr_fail_token("expected value in preprocessor expression before '%s'",
                           p, 1);
    return pp_make(0, 0);
}

static struct PPVal pp_expr_unary(void)
{
    struct PPVal v;
    char op;

    pp_expr_skip_ws();
    op = *pp_expr_p;
    if ((op != '!' && op != '~' && op != '+' && op != '-') ||
        (op == '!' && pp_expr_p[1] == '='))
        return pp_expr_primary();
    if ((op == '+' || op == '-') && pp_expr_p[1] == op) {
        pp_expr_fail_token("'%s' is not valid in a preprocessor expression",
                           pp_expr_p, 2);
        return pp_make(0, 0);
    }
    pp_expr_p++;
    if (++pp_expr_depth > PP_MAX_DEPTH) {
        pp_expr_fail("preprocessor expression nested too deeply");
        return pp_make(0, 0);
    }
    v = pp_expr_unary();
    pp_expr_depth--;
    if (op == '!')
        return pp_bool(v.v == 0);
    if (op == '~')
        return pp_make(~v.v, v.u);
    if (op == '-')
        return pp_make(0UL - v.v, v.u);
    return v;
}

/* The usual arithmetic conversions: unsigned long if either operand is. */
static int pp_either_unsigned(struct PPVal a, struct PPVal b)
{
    return a.u || b.u;
}

static int pp_less(struct PPVal a, struct PPVal b)
{
    if (pp_either_unsigned(a, b))
        return a.v < b.v;
    return pp_signed(a) < pp_signed(b);
}

static struct PPVal pp_expr_mul(void)
{
    struct PPVal v = pp_expr_unary();

    for (;;) {
        char op;
        struct PPVal r;
        int u;

        pp_expr_skip_ws();
        op = *pp_expr_p;
        if ((op != '*' && op != '/' && op != '%') || pp_expr_p[1] == '=' ||
            pp_expr_err[0])
            break;
        pp_expr_p++;
        r = pp_expr_unary();
        u = pp_either_unsigned(v, r);
        if (op == '*') {
            v = pp_make(v.v * r.v, u);
        } else if (r.v == 0) {
            if (pp_expr_eval)
                pp_expr_fail(op == '/' ? "division by zero in preprocessor expression"
                                       : "remainder by zero in preprocessor expression");
            v = pp_make(0, u);
        } else if (u) {
            v = pp_make(op == '/' ? v.v / r.v : v.v % r.v, 1);
        } else {
            long a = pp_signed(v);
            long b = pp_signed(r);
            if (a == -2147483647L - 1 && b == -1)
                v = pp_make(op == '/' ? (unsigned long)a : 0UL, 0);
            else
                v = pp_make((unsigned long)(op == '/' ? a / b : a % b), 0);
        }
    }
    return v;
}

static struct PPVal pp_expr_add(void)
{
    struct PPVal v = pp_expr_mul();

    for (;;) {
        char op;
        struct PPVal r;

        pp_expr_skip_ws();
        op = *pp_expr_p;
        if ((op != '+' && op != '-') || pp_expr_p[1] == '=' ||
            pp_expr_p[1] == op || pp_expr_err[0])
            break;
        pp_expr_p++;
        r = pp_expr_mul();
        v = pp_make(op == '+' ? v.v + r.v : v.v - r.v, pp_either_unsigned(v, r));
    }
    return v;
}

static struct PPVal pp_expr_shift(void)
{
    struct PPVal v = pp_expr_add();

    for (;;) {
        struct PPVal r;
        long n;
        int left;

        pp_expr_skip_ws();
        if (!((pp_expr_p[0] == '<' && pp_expr_p[1] == '<') ||
              (pp_expr_p[0] == '>' && pp_expr_p[1] == '>')) ||
            pp_expr_p[2] == '=' || pp_expr_err[0])
            break;
        left = pp_expr_p[0] == '<';
        pp_expr_p += 2;
        r = pp_expr_add();
        n = r.u ? (long)(r.v > 64 ? 64 : r.v) : pp_signed(r);
        if (n < 0 || n >= 32) {
            /* An out-of-range count is undefined; leave the bits a shift
             * one place at a time would. */
            if (!left && !v.u && (v.v & PP_SIGN))
                v = pp_make(PP_MASK, 0);
            else
                v = pp_make(0, v.u);
        } else if (left) {
            v = pp_make(v.v << n, v.u);
        } else if (v.u) {
            v = pp_make(v.v >> n, 1);
        } else {
            long s = pp_signed(v);
            v = pp_make(s < 0 ? ~(~(unsigned long)s >> n) : (unsigned long)s >> n, 0);
        }
    }
    return v;
}

static struct PPVal pp_expr_rel(void)
{
    struct PPVal v = pp_expr_shift();

    for (;;) {
        struct PPVal r;
        char op;
        int eq;

        pp_expr_skip_ws();
        op = pp_expr_p[0];
        if ((op != '<' && op != '>') || pp_expr_p[1] == op || pp_expr_err[0])
            break;
        eq = pp_expr_p[1] == '=';
        pp_expr_p += eq ? 2 : 1;
        r = pp_expr_shift();
        if (op == '<')
            v = pp_bool(eq ? !pp_less(r, v) : pp_less(v, r));
        else
            v = pp_bool(eq ? !pp_less(v, r) : pp_less(r, v));
    }
    return v;
}

static struct PPVal pp_expr_eq(void)
{
    struct PPVal v = pp_expr_rel();

    for (;;) {
        struct PPVal r;
        int ne;

        pp_expr_skip_ws();
        if (!((pp_expr_p[0] == '=' || pp_expr_p[0] == '!') && pp_expr_p[1] == '=') ||
            pp_expr_err[0])
            break;
        ne = pp_expr_p[0] == '!';
        pp_expr_p += 2;
        r = pp_expr_rel();
        v = pp_bool((v.v == r.v) != ne);
    }
    return v;
}

/* level 0 is &, 1 is ^, 2 is |. */
static struct PPVal pp_expr_bitop(int level)
{
    static const char ops[] = { '&', '^', '|' };
    struct PPVal v = level == 0 ? pp_expr_eq() : pp_expr_bitop(level - 1);
    char op = ops[level];

    for (;;) {
        struct PPVal r;
        int u;

        pp_expr_skip_ws();
        if (pp_expr_p[0] != op || pp_expr_p[1] == op || pp_expr_p[1] == '=' ||
            pp_expr_err[0])
            break;
        pp_expr_p++;
        r = level == 0 ? pp_expr_eq() : pp_expr_bitop(level - 1);
        u = pp_either_unsigned(v, r);
        if (op == '&')
            v = pp_make(v.v & r.v, u);
        else if (op == '^')
            v = pp_make(v.v ^ r.v, u);
        else
            v = pp_make(v.v | r.v, u);
    }
    return v;
}

static struct PPVal pp_expr_logic(int is_or)
{
    struct PPVal v = is_or ? pp_expr_logic(0) : pp_expr_bitop(2);
    char op = is_or ? '|' : '&';

    for (;;) {
        struct PPVal r;
        int saved = pp_expr_eval;

        pp_expr_skip_ws();
        if (pp_expr_p[0] != op || pp_expr_p[1] != op || pp_expr_err[0])
            break;
        pp_expr_p += 2;
        /* The right operand of a decided && or || is not evaluated. */
        if (is_or ? v.v != 0 : v.v == 0)
            pp_expr_eval = 0;
        r = is_or ? pp_expr_logic(0) : pp_expr_bitop(2);
        pp_expr_eval = saved;
        v = pp_bool(is_or ? (v.v || r.v) : (v.v && r.v));
    }
    return v;
}

static struct PPVal pp_expr_cond(void)
{
    struct PPVal c;
    struct PPVal t;
    struct PPVal f;
    int saved = pp_expr_eval;

    c = pp_expr_logic(1);
    pp_expr_skip_ws();
    if (*pp_expr_p != '?' || pp_expr_err[0])
        return c;
    pp_expr_p++;
    if (++pp_expr_depth > PP_MAX_DEPTH) {
        pp_expr_fail("preprocessor expression nested too deeply");
        return pp_make(0, 0);
    }
    pp_expr_eval = saved && c.v != 0;
    t = pp_expr_comma();
    pp_expr_skip_ws();
    if (*pp_expr_p != ':') {
        pp_expr_eval = saved;
        pp_expr_depth--;
        pp_expr_fail("missing ':' in preprocessor conditional expression");
        return pp_make(0, 0);
    }
    pp_expr_p++;
    pp_expr_eval = saved && c.v == 0;
    f = pp_expr_cond();
    pp_expr_eval = saved;
    pp_expr_depth--;
    return pp_make(c.v ? t.v : f.v, pp_either_unsigned(t, f));
}

static struct PPVal pp_expr_comma(void)
{
    struct PPVal v;

    if (++pp_expr_depth > PP_MAX_DEPTH) {
        pp_expr_fail("preprocessor expression nested too deeply");
        return pp_make(0, 0);
    }
    v = pp_expr_cond();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p != ',' || pp_expr_err[0])
            break;
        pp_expr_p++;
        v = pp_expr_cond();
    }
    pp_expr_depth--;
    return v;
}

/* Diagnose whatever follows a complete expression. */
static void pp_expr_trailing(void)
{
    const char *p;

    pp_expr_skip_ws();
    p = pp_expr_p;
    if (!*p || pp_expr_err[0])
        return;
    if (*p == ')') {
        pp_expr_fail("unexpected ')' in preprocessor expression");
    } else if (*p == '=' && p[1] != '=') {
        pp_expr_fail("'=' is not valid in a preprocessor expression (use '==' to compare)");
    } else if (is_ident_char((unsigned char)*p) || *p == '(' || *p == '\'' || *p == '"') {
        const char *q = p + 1;
        if (is_ident_char((unsigned char)*p))
            while (is_ident_char((unsigned char)*q))
                q++;
        pp_expr_fail_token("missing binary operator before '%s' in preprocessor expression",
                           p, (int)(q - p));
    } else {
        int n = (p[1] && strchr("=&|+-<>", p[1]) != NULL) ? 2 : 1;
        pp_expr_fail_token("'%s' is not valid in a preprocessor expression", p, n);
    }
}

int pp_eval_simple_expr(const char *s)
{
    struct PPOut o;
    struct PPVal v;

    pp_expr_err[0] = 0;
    pp_ndisabled = 0;
    o.cap = MAX_MACRO_TEXT;
    o.buf = (char *)xmalloc((size_t)o.cap);
    o.len = 0;
    o.buf[0] = 0;
    if (!pp_expand(s, &o, 0)) {
        free(o.buf);
        return 0;
    }
    pp_expr_p = o.buf;
    pp_expr_depth = 0;
    pp_expr_eval = 1;
    v = pp_expr_comma();
    pp_expr_trailing();
    free(o.buf);
    if (pp_expr_err[0])
        return 0;
    return v.v != 0;
}

/* The first problem the last pp_eval_simple_expr() met, or NULL. */
const char *pp_eval_expr_error(void)
{
    return pp_expr_err[0] ? pp_expr_err : NULL;
}

/* Whether the last pp_eval_simple_expr() met a malformed expression. */
int pp_eval_expr_malformed(void)
{
    return pp_expr_err[0] != 0;
}
