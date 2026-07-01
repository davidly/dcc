/*
 * dcc_constexpr.c - integer constant-expression parser.
 *
 * The parse_const_long_* precedence ladder that evaluates compile-time integer
 * constant expressions directly from the token stream, used for array bounds,
 * enum values, case labels and other contexts requiring a constant.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 3560-3828.
 */

#include "dcc.h"
long parse_const_long_primary(void)
{
    long v;
    int sign;

    sign = 1;
    if (tok.kind == '!') {
        next_token();
        return !parse_const_long_primary();
    }
    if (tok.kind == '~') {
        next_token();
        return ~parse_const_long_primary();
    }
    if (tok.kind == '-') {
        sign = -1;
        next_token();
    } else if (tok.kind == '+') {
        next_token();
    }

    if (tok.kind == TOK_ID && strcmp(tok.text, "__offsetof") == 0) {
        v = parse_offsetof_value();
        return sign * v;
    }

    if (tok.kind == TOK_SIZEOF) {
        next_token();
        if (accept('(')) {
            if (starts_type()) {
                int t;
                int sz;
                parse_type_name_decl(&t, &sz);
                v = sz;
                (void)t;
            } else {
                v = parse_sizeof_expr_operand();
            }
            expect(')');
        } else {
            error_here("'(' expected after sizeof in constant expression");
            v = 2;
        }
        return sign * v;
    }

    if (tok.kind == '(') {
        next_token();

        /*
         * Casts are allowed in C constant expressions, and lzpack uses
         * forms such as (size_t)(MAXDIST * 2).  DCC's small integer model
         * does not need to distinguish the cast for sizing/initializers, so
         * parse and ignore the type, then evaluate the cast operand.
         */
        if (starts_type()) {
            int t;
            t = parse_type();
            while (accept('*')) { skip_type_qualifiers(); t = type_add_ptr(t); }
            expect(')');
            v = parse_const_long_primary();
            /*
             * A cast to float rounds through single precision before any
             * outer integer cast consumes the value: (long)(float)16777217 is
             * 16777216, because 16777217 is not representable as a 32-bit
             * float.  This integer-only evaluator otherwise ignores the cast
             * type, which silently dropped that rounding.  The host build has
             * real floats, so round the operand the same way the Z80 runtime
             * would.  Pointer casts (t became a pointer above) are not float. */
            if (type_is_float(t)) {
                float ftmp;
                ftmp = (float)v;
                v = (long)ftmp;
            }
            return sign * v;
        }

        v = parse_const_long_expr();
        expect(')');
        return sign * v;
    }

    if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
        v = tok.val;
        next_token();
        return sign * v;
    }

    if (tok.kind == TOK_ID) {
        int i;
        for (i = 0; i < nenum_consts; ++i) {
            if (!strcmp(enum_const_names[i], tok.text)) {
                v = enum_const_values[i];
                next_token();
                return sign * v;
            }
        }
    }

    error_here("constant integer expression expected");
    return 0;
}

long parse_const_long_mul(void)
{
    long v;
    int op;
    long r;

    v = parse_const_long_primary();
    while (tok.kind == '*' || tok.kind == '/' || tok.kind == '%') {
        op = tok.kind;
        next_token();
        r = parse_const_long_primary();
        if (op == '*') v *= r;
        else if (op == '/') {
            if (r == 0) {
                error_here("division by zero in constant expression");
                r = 1;
            }
            v /= r;
        } else {
            if (r == 0) {
                error_here("division by zero in constant expression");
                r = 1;
            }
            v %= r;
        }
    }
    return v;
}

long parse_const_long_add(void)
{
    long v;
    int op;
    long r;

    v = parse_const_long_mul();
    while (tok.kind == '+' || tok.kind == '-') {
        op = tok.kind;
        next_token();
        r = parse_const_long_mul();
        if (op == '+') v += r;
        else v -= r;
    }
    return v;
}

long parse_const_long_shift(void)
{
    long v;
    int op;
    long r;

    v = parse_const_long_add();
    while (tok.kind == TOK_SHL || tok.kind == TOK_SHR) {
        op = tok.kind;
        next_token();
        r = parse_const_long_add();
        if (r < 0) r = 0;
        if (r > 31) r = 31;
        if (op == TOK_SHL) v <<= (int)r;
        else v = (long)((unsigned long)v >> (int)r);
    }
    return v;
}

long parse_const_long_rel(void)
{
    long v;
    int op;
    long r;

    v = parse_const_long_shift();
    while (tok.kind == '<' || tok.kind == '>' || tok.kind == TOK_LE || tok.kind == TOK_GE) {
        op = tok.kind;
        next_token();
        r = parse_const_long_shift();
        if (op == '<') v = (v < r);
        else if (op == '>') v = (v > r);
        else if (op == TOK_LE) v = (v <= r);
        else v = (v >= r);
    }
    return v;
}

long parse_const_long_eq(void)
{
    long v;
    int op;
    long r;

    v = parse_const_long_rel();
    while (tok.kind == TOK_EQ || tok.kind == TOK_NE) {
        op = tok.kind;
        next_token();
        r = parse_const_long_rel();
        if (op == TOK_EQ) v = (v == r);
        else v = (v != r);
    }
    return v;
}

long parse_const_long_band(void)
{
    long v;

    v = parse_const_long_eq();
    while (tok.kind == '&') {
        next_token();
        v &= parse_const_long_eq();
    }
    return v;
}

long parse_const_long_xor(void)
{
    long v;

    v = parse_const_long_band();
    while (tok.kind == '^') {
        next_token();
        v ^= parse_const_long_band();
    }
    return v;
}

long parse_const_long_bitor(void)
{
    long v;

    v = parse_const_long_xor();
    while (tok.kind == '|') {
        next_token();
        v |= parse_const_long_xor();
    }
    return v;
}

long parse_const_long_andand(void)
{
    long v;

    v = parse_const_long_bitor();
    while (tok.kind == TOK_ANDAND) {
        next_token();
        v = (v && parse_const_long_bitor());
    }
    return v;
}

long parse_const_long_expr(void)
{
    long v;

    v = parse_const_long_andand();
    while (tok.kind == TOK_OROR) {
        next_token();
        v = (v || parse_const_long_andand());
    }
    return v;
}

int parse_const_int_expr(void)
{
    return (int)(parse_const_long_expr() & 0xffffL);
}


int starts_type(void)
{
    return tok.kind == TOK_INT || tok.kind == TOK_LONG || tok.kind == TOK_SHORT || tok.kind == TOK_FLOAT || tok.kind == TOK_CHAR || tok.kind == TOK_VOID ||
            tok.kind == TOK_BOOL ||
           tok.kind == TOK_UNSIGNED || tok.kind == TOK_SIGNED || tok.kind == TOK_CONST || tok.kind == TOK_VOLATILE ||
           tok.kind == TOK_EXTERN || tok.kind == TOK_STATIC || tok.kind == TOK_REGISTER || tok.kind == TOK_AUTO ||
           tok.kind == TOK_INLINE ||
           tok.kind == TOK_TYPEDEF || tok.kind == TOK_STRUCT ||
           tok.kind == TOK_UNION || tok.kind == TOK_ENUM ||
           (tok.kind == TOK_ID && find_typedef(tok.text) >= 0);
}

