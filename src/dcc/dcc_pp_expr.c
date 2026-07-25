/*
 * dcc_pp_expr.c - preprocessor #if/#elif constant-expression evaluator.
 *
 * The evaluator parses an already-expanded expression string using a compact
 * recursive-descent precedence ladder. Only pp_eval_simple_expr is shared;
 * cursor state and precedence helpers are private to this translation unit.
 */

#include "dcc_preproc_internal.h"

static const char *pp_expr_p;
static int pp_expr_depth;

static void pp_expr_skip_ws(void)
{
    while (*pp_expr_p && isspace((unsigned char)*pp_expr_p))
        pp_expr_p++;
}

static int pp_expr_match_word(const char *w)
{
    int n;
    n = (int)strlen(w);
    pp_expr_skip_ws();
    if (strncmp(pp_expr_p, w, n) != 0)
        return 0;
    if (is_ident_char((unsigned char)pp_expr_p[n]))
        return 0;
    pp_expr_p += n;
    return 1;
}

static long pp_expr_number(void)
{
    unsigned long v;
    int base;

    pp_expr_skip_ws();

    v = 0;
    base = 10;
    if (pp_expr_p[0] == '0') {
        if (pp_expr_p[1] == 'x' || pp_expr_p[1] == 'X') {
            base = 16;
            pp_expr_p += 2;
        } else {
            base = 8;
            pp_expr_p++;
        }
    }

    if (base == 16) {
        while (isxdigit((unsigned char)*pp_expr_p)) {
            v *= 16;
            if (*pp_expr_p >= '0' && *pp_expr_p <= '9') v += *pp_expr_p - '0';
            else if (*pp_expr_p >= 'a' && *pp_expr_p <= 'f') v += *pp_expr_p - 'a' + 10;
            else v += *pp_expr_p - 'A' + 10;
            pp_expr_p++;
        }
    } else {
        while (*pp_expr_p >= '0' && *pp_expr_p <= (base == 8 ? '7' : '9')) {
            v = v * (unsigned long)base + (unsigned long)(*pp_expr_p - '0');
            pp_expr_p++;
        }
    }

    while (*pp_expr_p == 'u' || *pp_expr_p == 'U' ||
           *pp_expr_p == 'l' || *pp_expr_p == 'L')
        pp_expr_p++;

    return (long)v;
}

static long pp_expr_charlit(void)
{
    int c;
    long v;

    pp_expr_skip_ws();
    if (*pp_expr_p != '\'')
        return 0;
    pp_expr_p++;

    if (*pp_expr_p == '\\') {
        pp_expr_p++;
        c = (unsigned char)*pp_expr_p;
        if (c == 'n') v = '\n';
        else if (c == 'r') v = '\r';
        else if (c == 't') v = '\t';
        else if (c == '0') v = 0;
        else v = c;
        if (*pp_expr_p)
            pp_expr_p++;
    } else {
        v = (unsigned char)*pp_expr_p;
        if (*pp_expr_p)
            pp_expr_p++;
    }

    while (*pp_expr_p && *pp_expr_p != '\'')
        pp_expr_p++;
    if (*pp_expr_p == '\'')
        pp_expr_p++;

    return v;
}

static long pp_expr_primary(void);
static long pp_expr_unary(void);
static long pp_expr_mul(void);
static long pp_expr_add(void);
static long pp_expr_shift(void);
static long pp_expr_rel(void);
static long pp_expr_eq(void);
static long pp_expr_bitand(void);
static long pp_expr_bitxor(void);
static long pp_expr_bitor(void);
static long pp_expr_andand(void);
static long pp_expr_oror(void);
static long pp_expr_cond(void);

static long pp_expr_defined(void)
{
    char name[64];
    int i;

    if (!pp_expr_match_word("defined"))
        return 0;

    pp_expr_skip_ws();
    if (*pp_expr_p == '(') {
        pp_expr_p++;
        pp_expr_skip_ws();
        i = 0;
        while (is_ident_char((unsigned char)*pp_expr_p) && i < 63)
            name[i++] = *pp_expr_p++;
        name[i] = 0;
        pp_expr_skip_ws();
        if (*pp_expr_p == ')')
            pp_expr_p++;
    } else {
        i = 0;
        while (is_ident_char((unsigned char)*pp_expr_p) && i < 63)
            name[i++] = *pp_expr_p++;
        name[i] = 0;
    }

    return name[0] && find_define(name) >= 0;
}

static long pp_expr_primary(void)
{
    char name[64];
    int i;
    long v;

    pp_expr_skip_ws();

    if (!strncmp(pp_expr_p, "defined", 7) &&
        !is_ident_char((unsigned char)pp_expr_p[7]))
        return pp_expr_defined();

    if (*pp_expr_p == '(') {
        pp_expr_p++;
        v = pp_expr_cond();
        pp_expr_skip_ws();
        if (*pp_expr_p == ')')
            pp_expr_p++;
        return v;
    }

    if (*pp_expr_p == '\'')
        return pp_expr_charlit();

    if (isdigit((unsigned char)*pp_expr_p))
        return pp_expr_number();

    if (is_ident_start((unsigned char)*pp_expr_p)) {
        int di;
        const char *savep;

        i = 0;
        while (is_ident_char((unsigned char)*pp_expr_p) && i < 63)
            name[i++] = *pp_expr_p++;
        name[i] = 0;

        if (!strcmp(name, "__LINE__"))
            return g_lex.line_no;

        di = find_define(name);
        if (di >= 0 && !defs[di].is_func && pp_expr_depth < 16) {
            savep = pp_expr_p;
            pp_expr_p = defs[di].value;
            pp_expr_depth++;
            v = pp_expr_cond();
            pp_expr_depth--;
            pp_expr_p = savep;
            return v;
        }

        return 0;
    }

    return 0;
}

static long pp_expr_unary(void)
{
    pp_expr_skip_ws();
    if (*pp_expr_p == '!') {
        pp_expr_p++;
        return !pp_expr_unary();
    }
    if (*pp_expr_p == '~') {
        pp_expr_p++;
        return ~pp_expr_unary();
    }
    if (*pp_expr_p == '+') {
        pp_expr_p++;
        return pp_expr_unary();
    }
    if (*pp_expr_p == '-') {
        pp_expr_p++;
        return -pp_expr_unary();
    }
    return pp_expr_primary();
}

static long pp_expr_mul(void)
{
    long v;
    long r;

    v = pp_expr_unary();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '*') {
            pp_expr_p++;
            v = v * pp_expr_unary();
        } else if (*pp_expr_p == '/') {
            pp_expr_p++;
            r = pp_expr_unary();
            v = r ? (v / r) : 0;
        } else if (*pp_expr_p == '%') {
            pp_expr_p++;
            r = pp_expr_unary();
            v = r ? (v % r) : 0;
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_add(void)
{
    long v;

    v = pp_expr_mul();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '+') {
            pp_expr_p++;
            v = v + pp_expr_mul();
        } else if (*pp_expr_p == '-') {
            pp_expr_p++;
            v = v - pp_expr_mul();
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_shift(void)
{
    long v;
    long r;

    v = pp_expr_add();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '<' && pp_expr_p[1] == '<') {
            pp_expr_p += 2;
            r = pp_expr_add();
            if (r < 0 || r >= 32)
                v = 0;
            else
                v = v << (int)r;
        } else if (pp_expr_p[0] == '>' && pp_expr_p[1] == '>') {
            pp_expr_p += 2;
            r = pp_expr_add();
            if (r < 0 || r >= 32)
                v = 0;
            else
                v = v >> (int)r;
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_rel(void)
{
    long v;
    long r;

    v = pp_expr_shift();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '<' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_shift();
            v = (v <= r);
        } else if (pp_expr_p[0] == '>' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_shift();
            v = (v >= r);
        } else if (*pp_expr_p == '<') {
            pp_expr_p++;
            r = pp_expr_shift();
            v = (v < r);
        } else if (*pp_expr_p == '>') {
            pp_expr_p++;
            r = pp_expr_shift();
            v = (v > r);
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_eq(void)
{
    long v;
    long r;

    v = pp_expr_rel();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '=' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_rel();
            v = (v == r);
        } else if (pp_expr_p[0] == '!' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_rel();
            v = (v != r);
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_bitand(void)
{
    long v;

    v = pp_expr_eq();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '&' && pp_expr_p[1] != '&') {
            pp_expr_p++;
            v = v & pp_expr_eq();
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_bitxor(void)
{
    long v;

    v = pp_expr_bitand();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '^') {
            pp_expr_p++;
            v = v ^ pp_expr_bitand();
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_bitor(void)
{
    long v;

    v = pp_expr_bitxor();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '|' && pp_expr_p[1] != '|') {
            pp_expr_p++;
            v = v | pp_expr_bitxor();
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_andand(void)
{
    long v;
    v = pp_expr_bitor();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '&' && pp_expr_p[1] == '&') {
            pp_expr_p += 2;
            v = (pp_expr_bitor() && v);
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_oror(void)
{
    long v;
    v = pp_expr_andand();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '|' && pp_expr_p[1] == '|') {
            pp_expr_p += 2;
            v = (pp_expr_andand() || v);
        } else {
            break;
        }
    }
    return v;
}

static long pp_expr_cond(void)
{
    long v;
    long t;
    long f;

    v = pp_expr_oror();
    pp_expr_skip_ws();
    if (*pp_expr_p == '?') {
        pp_expr_p++;
        t = pp_expr_cond();
        pp_expr_skip_ws();
        if (*pp_expr_p == ':')
            pp_expr_p++;
        f = pp_expr_cond();
        v = v ? t : f;
    }
    return v;
}

int pp_eval_simple_expr(const char *s)
{
    pp_expr_p = s;
    pp_expr_depth = 0;
    return pp_expr_cond() != 0;
}