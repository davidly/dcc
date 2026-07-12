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

static int const_expr_no_eval;

long parse_const_long_primary(void)
{
    long v;
    int sign;

    sign = 1;
    if (tok.kind == '!') {
        next_token();
        v = parse_const_long_primary();
        return const_expr_no_eval ? 0 : !v;
    }
    if (tok.kind == '~') {
        next_token();
        v = parse_const_long_primary();
        return const_expr_no_eval ? 0 : ~v;
    }
    if (tok.kind == '-') {
        sign = -1;
        next_token();
    } else if (tok.kind == '+') {
        next_token();
    }

    if (tok.kind == TOK_ID && strcmp(tok.text, "__offsetof") == 0) {
        v = parse_offsetof_value();
        return const_expr_no_eval ? 0 : sign * v;
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
            int t;
            int sz;
            /* sizeof unary-expression without parentheses.  Do not consume
             * following binary operators: sizeof a + 1 is (sizeof a) + 1,
             * not sizeof(a + 1). */
            if (!sizeof_parse_primary_type(&t, &sz))
                sz = 2;
            v = sz;
        }
        return const_expr_no_eval ? 0 : sign * v;
    }

    if (tok.kind == '(') {
        next_token();

        /*
         * Casts are allowed in C constant expressions, and lzpack uses
         * forms such as (size_t)(MAXDIST * 2).  This legacy value-only ladder
         * applies the immediate cast below; contexts needing typed arithmetic
         * throughout use the ConstVal parser in dcc_fold.c.
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
                if (!const_expr_no_eval) {
                    ftmp = (float)v;
                    v = (long)ftmp;
                }
            } else if (!const_expr_no_eval && type_ptr_depth(t) == 0 &&
                       !(t & TYPE_STRUCT) && (t & 15) != TYPE_VOID) {
                /* Apply an integer cast the way the target would: truncate to
                 * the destination width and re-extend per its signedness, so
                 * (uint16_t)-1 is 65535 and (signed char)0x1ff is -1.  Reuses
                 * the fold engine's masking for a single source of truth. */
                struct ConstVal cv;
                cv.u = (unsigned long)v;
                cv.type = t;
                cf_cast_to_type(&cv, t);
                v = cf_signed_value(cv);
            }
            return const_expr_no_eval ? 0 : sign * v;
        }

        v = parse_const_long_expr();
        expect(')');
        return const_expr_no_eval ? 0 : sign * v;
    }

    if (tok.kind == TOK_NUM || tok.kind == TOK_CHARLIT) {
        v = tok.val;
        next_token();
        return const_expr_no_eval ? 0 : sign * v;
    }

    if (tok.kind == TOK_ID) {
        int i;
        for (i = 0; i < nenum_consts; ++i) {
            if (!strcmp(enum_const_names[i], tok.text)) {
                v = enum_const_values[i];
                next_token();
                return const_expr_no_eval ? 0 : sign * v;
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
        if (const_expr_no_eval) {
            v = 0;
        } else if (op == '*') v *= r;
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
        if (const_expr_no_eval) v = 0;
        else if (op == '+') v += r;
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
        if (const_expr_no_eval) {
            v = 0;
            continue;
        }
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
        if (const_expr_no_eval) v = 0;
        else if (op == '<') v = (v < r);
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
        if (const_expr_no_eval) v = 0;
        else if (op == TOK_EQ) v = (v == r);
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
        if (const_expr_no_eval) {
            (void)parse_const_long_eq();
            v = 0;
        } else {
            v &= parse_const_long_eq();
        }
    }
    return v;
}

long parse_const_long_xor(void)
{
    long v;

    v = parse_const_long_band();
    while (tok.kind == '^') {
        next_token();
        if (const_expr_no_eval) {
            (void)parse_const_long_band();
            v = 0;
        } else {
            v ^= parse_const_long_band();
        }
    }
    return v;
}

long parse_const_long_bitor(void)
{
    long v;

    v = parse_const_long_xor();
    while (tok.kind == '|') {
        next_token();
        if (const_expr_no_eval) {
            (void)parse_const_long_xor();
            v = 0;
        } else {
            v |= parse_const_long_xor();
        }
    }
    return v;
}

long parse_const_long_andand(void)
{
    long v;
    long r;

    v = parse_const_long_bitor();
    while (tok.kind == TOK_ANDAND) {
        next_token();
        if (const_expr_no_eval || !v) {
            const_expr_no_eval++;
            (void)parse_const_long_bitor();
            const_expr_no_eval--;
            v = 0;
        } else {
            r = parse_const_long_bitor();
            v = (v && r);
        }
    }
    return v;
}

long parse_const_long_oror(void)
{
    long v;
    long r;

    v = parse_const_long_andand();
    while (tok.kind == TOK_OROR) {
        next_token();
        if (const_expr_no_eval || v) {
            const_expr_no_eval++;
            (void)parse_const_long_andand();
            const_expr_no_eval--;
            v = const_expr_no_eval ? 0 : 1;
        } else {
            r = parse_const_long_andand();
            v = (v || r);
        }
    }
    return v;
}

long parse_const_long_expr(void)
{
    long v;
    long t;
    long f;

    v = parse_const_long_oror();
    if (tok.kind == '?') {
        next_token();
        if (const_expr_no_eval) {
            const_expr_no_eval++;
            (void)parse_const_long_expr();
            expect(':');
            (void)parse_const_long_expr();
            const_expr_no_eval--;
            v = 0;
        } else if (v) {
            t = parse_const_long_expr();
            expect(':');
            const_expr_no_eval++;
            (void)parse_const_long_expr();
            const_expr_no_eval--;
            v = t;
        } else {
            const_expr_no_eval++;
            (void)parse_const_long_expr();
            const_expr_no_eval--;
            expect(':');
            f = parse_const_long_expr();
            v = f;
        }
    }
    return v;
}

int parse_const_int_expr(void)
{
    return (int)(parse_const_long_expr() & 0xffffL);
}

static void recover_static_assert_decl(void)
{
    while (tok.kind != TOK_EOF && tok.kind != ';')
        next_token();
    if (tok.kind == ';')
        next_token();
}

void parse_static_assert_decl(void)
{
    struct Token assert_tok;
    int assert_line;
    long assert_pos;
    int errors_before;
    struct ConstVal value;
    const char *prefix;
    char *message;
    size_t capacity;
    size_t used;

    assert_tok = tok;
    assert_line = tok_line;
    assert_pos = tok_start_pos;
    expect(TOK_STATIC_ASSERT);
    if (tok.kind != '(') {
        error_here("expected '(' in static assertion");
        recover_static_assert_decl();
        return;
    }
    next_token();
    errors_before = g_diag_error_count;
    if (!try_parse_const_expr_value(&value)) {
        if (g_diag_error_count == errors_before)
            error_here("constant integer expression expected");
        recover_static_assert_decl();
        return;
    }
    if (g_diag_error_count != errors_before) {
        recover_static_assert_decl();
        return;
    }
    if (tok.kind != ',') {
        error_here("expected ',' in static assertion");
        recover_static_assert_decl();
        return;
    }
    next_token();
    if (tok.kind != TOK_STR && tok.kind != TOK_WSTR) {
        error_here("string literal expected in static assertion");
        recover_static_assert_decl();
        return;
    }

    prefix = "static assertion failed: ";
    used = strlen(prefix);
    capacity = used + 1;
    message = (char *)xmalloc(capacity);
    strcpy(message, prefix);
    while (tok.kind == TOK_STR || tok.kind == TOK_WSTR) {
        size_t piece_len;
        size_t needed;

        piece_len = strlen(tok.text);
        needed = used + piece_len + 1;
        if (needed > capacity) {
            char *grown;

            capacity = needed * 2;
            grown = (char *)xmalloc(capacity);
            memcpy(grown, message, used + 1);
            free(message);
            message = grown;
        }
        memcpy(message + used, tok.text, piece_len + 1);
        used += piece_len;
        next_token();
    }
    if (tok.kind != ')') {
        error_here("expected ')' after static assertion message");
        free(message);
        recover_static_assert_decl();
        return;
    }
    next_token();
    if (tok.kind != ';') {
        error_here("expected ';' after static assertion");
        free(message);
        return;
    }
    next_token();
    if (cf_signed_value(value) == 0) {
        dcc_error_at(assert_tok.file, assert_line, assert_pos,
                     message, assert_tok.text);
    }
    free(message);
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

