/**
 * @file dcc_fold.c
 * @brief Evaluates typed compile-time expressions into ConstVal results.
 *
 * @par Role
 * Implements the constant-expression precedence parser, target-width masks,
 * promotions, casts, arithmetic, comparisons, logical operations, conditional
 * expressions, integer/float literals, and sizeof/offsetof folding.
 *
 * @par Key entry points
 * try_parse_const_expr_value(), try_parse_integer_const_expr_value(),
 * cf_parse_cond(), cf_convert_to_type(), and emit_const_value().
 *
 * @par Boundary
 * dcc_constexpr.c applies declaration-specific validity and range rules.
 * Runtime expression capture and production function-body selection belong to
 * the AST and MIR layers.
 */

#include "dcc.h"

unsigned long cf_mask_for_type(int type)
{
    int sz;
    if (type & (TYPE_PTR | TYPE_PTR2))
        return 0xffffUL;
    sz = type_size(type);
    if (sz == 1) return 0xffUL;
    if (sz == 2) return 0xffffUL;
    return 0xffffffffUL;
}

unsigned long cf_sign_bit_for_type(int type)
{
    int sz;
    if (type & (TYPE_PTR | TYPE_PTR2))
        return 0x8000UL;
    sz = type_size(type);
    if (sz == 1) return 0x80UL;
    if (sz == 2) return 0x8000UL;
    return 0x80000000UL;
}

long cf_signed_value(struct ConstVal v)
{
    unsigned long mask;
    unsigned long sign;
    unsigned long u;

    mask = cf_mask_for_type(v.type);
    sign = cf_sign_bit_for_type(v.type);
    u = v.u & mask;
    if ((v.type & TYPE_UNSIGNED) || (v.type & (TYPE_PTR | TYPE_PTR2)))
        return (long)u;
    if (u & sign)
        return (long)(u | ~mask);
    return (long)u;
}

int cf_promote_type(int type)
{
    if (type & (TYPE_PTR | TYPE_PTR2 | TYPE_STRUCT))
        return type;
    if (type_is_float(type))
        return type;
    if (type_is_long(type))
        return type;
    if ((type & 15) == TYPE_CHAR || (type & 15) == TYPE_BOOL)
        return TYPE_INT;
    return (type & TYPE_UNSIGNED) ? (TYPE_INT | TYPE_UNSIGNED) : TYPE_INT;
}

int cf_common_arith_type(int a, int b)
{
    a = cf_promote_type(a);
    b = cf_promote_type(b);
    if (type_is_long(a) || type_is_long(b)) {
        if ((type_is_long(a) && (a & TYPE_UNSIGNED)) ||
            (type_is_long(b) && (b & TYPE_UNSIGNED)))
            return TYPE_LONG | TYPE_UNSIGNED;
        return TYPE_LONG;
    }
    if ((a & TYPE_UNSIGNED) || (b & TYPE_UNSIGNED))
        return TYPE_INT | TYPE_UNSIGNED;
    return TYPE_INT;
}

void cf_cast_to_type(struct ConstVal *v, int type)
{
    unsigned long u;
    unsigned long mask;
    unsigned long sign;

    v->type = type;
    if (type_is_bool(type)) {
        v->u = v->u ? 1UL : 0UL;
        return;
    }
    mask = cf_mask_for_type(type);
    u = v->u & mask;

    if (!(type & TYPE_UNSIGNED) && !(type & (TYPE_PTR | TYPE_PTR2)) &&
        type_size(type) < 4) {
        sign = cf_sign_bit_for_type(type);
        if (u & sign)
            u |= ~mask;
    }

    v->u = u & 0xffffffffUL;
}

void cf_convert_to_type(struct ConstVal *v, int type)
{
    if (!(v->type & TYPE_UNSIGNED) && !(v->type & (TYPE_PTR | TYPE_PTR2)))
        v->u = (unsigned long)cf_signed_value(*v);
    cf_cast_to_type(v, type);
}

static int cf_no_eval;
static int cf_ice_mode;
static int cf_ice_invalid;

static long cf_signed_min_for_type(int type)
{
    return type_is_long(type) ? (-2147483647L - 1L) : -32768L;
}

static long cf_signed_max_for_type(int type)
{
    return type_is_long(type) ? 2147483647L : 32767L;
}

static int cf_signed_add_overflows(long left, long right, int type)
{
    long min_value;
    long max_value;

    min_value = cf_signed_min_for_type(type);
    max_value = cf_signed_max_for_type(type);
    return (right > 0 && left > max_value - right) ||
           (right < 0 && left < min_value - right);
}

static int cf_signed_sub_overflows(long left, long right, int type)
{
    long min_value;
    long max_value;

    min_value = cf_signed_min_for_type(type);
    max_value = cf_signed_max_for_type(type);
    return (right < 0 && left > max_value + right) ||
           (right > 0 && left < min_value + right);
}

static int cf_signed_mul_overflows(long left, long right, int type)
{
    long min_value;
    long max_value;

    min_value = cf_signed_min_for_type(type);
    max_value = cf_signed_max_for_type(type);
    if (left == 0 || right == 0)
        return 0;
    if (left == -1)
        return right == min_value;
    if (right == -1)
        return left == min_value;
    if (left > 0)
        return right > 0 ? left > max_value / right
                         : right < min_value / left;
    return right > 0 ? left < min_value / right
                     : left < max_value / right;
}

int cf_parse_cond(struct ConstVal *out);

unsigned long cf_parse_integer_literal_bits(const char *text)
{
    const char *p;
    unsigned long v;
    int base;

    p = text;
    while (*p && isspace((unsigned char)*p))
        p++;

    /* The constant-folder handles unary +/- separately.  If a macro has
     * already become a signed textual token, accept the sign here too so
     * speculative snippet folding remains harmless. */
    if (*p == '+')
        p++;
    else if (*p == '-') {
        p++;
        v = cf_parse_integer_literal_bits(p);
        return (0UL - v) & 0xffffffffUL;
    }

    v = 0;
    base = 10;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16;
        p += 2;
    } else if (p[0] == '0') {
        base = 8;
        p++;
    }

    if (base == 16) {
        while (isxdigit((unsigned char)*p)) {
            v <<= 4;
            if (*p >= '0' && *p <= '9') v += (unsigned long)(*p - '0');
            else if (*p >= 'a' && *p <= 'f') v += (unsigned long)(*p - 'a' + 10);
            else v += (unsigned long)(*p - 'A' + 10);
            p++;
        }
    } else {
        while (*p >= '0' && *p <= (base == 8 ? '7' : '9')) {
            v = v * (unsigned long)base + (unsigned long)(*p - '0');
            p++;
        }
    }

    return v & 0xffffffffUL;
}

static float cf_float_literal_value(void)
{
    union {
        float f;
        unsigned char b[4];
    } value;
    unsigned long bits;

    bits = (unsigned long)g_lex.tok.val & 0xffffffffUL;
    value.b[0] = (unsigned char)(bits & 0xffUL);
    value.b[1] = (unsigned char)((bits >> 8) & 0xffUL);
    value.b[2] = (unsigned char)((bits >> 16) & 0xffUL);
    value.b[3] = (unsigned char)((bits >> 24) & 0xffUL);
    return value.f;
}

int cf_parse_primary(struct ConstVal *out)
{
    int i;
    long v;
    int t;
    int sz;

    if (g_lex.tok.kind == TOK_ID && strcmp(g_lex.tok.text, "__offsetof") == 0) {
        out->u = (unsigned long)parse_offsetof_value();
        out->type = TYPE_INT | TYPE_UNSIGNED;
        cf_cast_to_type(out, out->type);
        return 1;
    }

    if (g_lex.tok.kind == TOK_NUM) {
        /* Do not use tok.val here.  On MSVC host long is 32-bit, so tokens
         * such as 2147483648UL/4294967295UL cannot be represented as a
         * positive signed long.  Re-read tok.text as raw 32-bit bits and then
         * apply the target C89 literal type. */
        out->u = cf_parse_integer_literal_bits(g_lex.tok.text);
        out->type = g_tok_long_suffix ? TYPE_LONG : TYPE_INT;
        if (g_tok_unsigned_suffix)
            out->type |= TYPE_UNSIGNED;
        cf_cast_to_type(out, out->type);
        next_token();
        return 1;
    }

    if (g_lex.tok.kind == TOK_CHARLIT) {
        out->u = (unsigned long)g_lex.tok.val;
        out->type = TYPE_INT;
        cf_cast_to_type(out, out->type);
        next_token();
        return 1;
    }

    if (g_lex.tok.kind == TOK_ID) {
        for (i = 0; i < nenum_consts; ++i) {
            if (!strcmp(enum_const_names[i], g_lex.tok.text)) {
                out->u = (unsigned long)enum_const_values[i];
                out->type = TYPE_INT;
                cf_cast_to_type(out, out->type);
                next_token();
                return 1;
            }
        }
    }

    if (g_lex.tok.kind == TOK_SIZEOF) {
        next_token();
        if (accept('(')) {
            if (starts_type()) {
                parse_type_name_decl(&t, &sz);
                v = sz;
            } else {
                v = parse_sizeof_expr_operand();
            }
            expect(')');
        } else {
            /* sizeof unary-expression without parentheses.  Do not consume
             * following binary operators: sizeof a + 1 is (sizeof a) + 1,
             * not sizeof(a + 1). */
            if (!sizeof_parse_primary_type(&t, &sz))
                return 0;
            v = sz;
        }
        out->u = (unsigned long)v;
        out->type = TYPE_INT | TYPE_UNSIGNED;
        cf_cast_to_type(out, out->type);
        return 1;
    }

    if (g_lex.tok.kind == '(') {
        next_token();
        if (starts_type()) {
            float float_value;
            parse_type_name_decl(&t, &sz);
            expect(')');
            if (g_lex.tok.kind == TOK_FLOATLIT && !type_is_float(t) &&
                type_ptr_depth(t) == 0 && !(t & TYPE_STRUCT) &&
                (t & 15) != TYPE_VOID) {
                float_value = cf_float_literal_value();
                out->u = (unsigned long)(long)float_value;
                out->type = t;
                cf_cast_to_type(out, t);
                next_token();
                return 1;
            }
            if (!cf_parse_unary(out))
                return 0;
            if (type_is_float(t)) {
                float rounded;

                if (cf_ice_mode)
                    cf_ice_invalid = 1;
                if (out->type & TYPE_UNSIGNED)
                    rounded = (float)(out->u & cf_mask_for_type(out->type));
                else
                    rounded = (float)cf_signed_value(*out);
                out->u = (unsigned long)(long)rounded;
                out->type = TYPE_LONG;
                cf_cast_to_type(out, out->type);
                return 1;
            }
            if (cf_ice_mode && type_ptr_depth(t) != 0)
                cf_ice_invalid = 1;
            cf_cast_to_type(out, t);
            return 1;
        }
        if (!cf_parse_cond(out))
            return 0;
        if (!accept(')'))
            return 0;
        return 1;
    }

    /* String literals and identifiers are not integer constants. */
    return 0;
}

int cf_parse_unary(struct ConstVal *out)
{
    int op;

    if (g_lex.tok.kind == '+' || g_lex.tok.kind == '-' || g_lex.tok.kind == '~' || g_lex.tok.kind == '!') {
        op = g_lex.tok.kind;
        next_token();
        if (!cf_parse_unary(out))
            return 0;
        cf_convert_to_type(out, cf_promote_type(out->type));
        if (cf_no_eval) {
            out->u = 0;
            return 1;
        }
        if (op == '+') {
            return 1;
        } else if (op == '-') {
            if (cf_ice_mode && !(out->type & TYPE_UNSIGNED) &&
                cf_signed_value(*out) == cf_signed_min_for_type(out->type))
                cf_ice_invalid = 1;
            out->u = (0UL - out->u) & cf_mask_for_type(out->type);
            cf_cast_to_type(out, out->type);
            return 1;
        } else if (op == '~') {
            out->u = (~out->u) & cf_mask_for_type(out->type);
            cf_cast_to_type(out, out->type);
            return 1;
        } else {
            out->u = cf_signed_value(*out) != 0 ? 0UL : 1UL;
            out->type = TYPE_INT;
            return 1;
        }
    }

    return cf_parse_primary(out);
}

int cf_parse_mul(struct ConstVal *out)
{
    struct ConstVal rhs;
    int op;
    int common;
    long ls, rs;

    if (!cf_parse_unary(out))
        return 0;
    while (g_lex.tok.kind == '*' || g_lex.tok.kind == '/' || g_lex.tok.kind == '%') {
        op = g_lex.tok.kind;
        next_token();
        if (!cf_parse_unary(&rhs))
            return 0;
        common = cf_common_arith_type(out->type, rhs.type);
        cf_convert_to_type(out, common);
        cf_convert_to_type(&rhs, common);
        if (cf_no_eval) {
            out->u = 0;
        } else if (op == '*') {
            if (cf_ice_mode && !(common & TYPE_UNSIGNED) &&
                cf_signed_mul_overflows(cf_signed_value(*out),
                                        cf_signed_value(rhs), common))
                cf_ice_invalid = 1;
            out->u = (out->u * rhs.u) & cf_mask_for_type(common);
        } else if (common & TYPE_UNSIGNED) {
            if ((rhs.u & cf_mask_for_type(common)) == 0) {
                error_here("division by zero in constant expression");
                return 0;
            }
            if (op == '/')
                out->u = (out->u & cf_mask_for_type(common)) / (rhs.u & cf_mask_for_type(common));
            else
                out->u = (out->u & cf_mask_for_type(common)) % (rhs.u & cf_mask_for_type(common));
        } else {
            rs = cf_signed_value(rhs);
            if (rs == 0) {
                error_here("division by zero in constant expression");
                return 0;
            }
            ls = cf_signed_value(*out);
            if (cf_ice_mode && ls == cf_signed_min_for_type(common) && rs == -1) {
                cf_ice_invalid = 1;
                out->u = (unsigned long)ls & cf_mask_for_type(common);
                out->type = common;
                cf_cast_to_type(out, common);
                continue;
            }
            if (op == '/')
                out->u = (unsigned long)(ls / rs);
            else
                out->u = (unsigned long)(ls % rs);
        }
        out->type = common;
        cf_cast_to_type(out, common);
    }
    return 1;
}

int cf_parse_add(struct ConstVal *out)
{
    struct ConstVal rhs;
    int op;
    int common;

    if (!cf_parse_mul(out))
        return 0;
    while (g_lex.tok.kind == '+' || g_lex.tok.kind == '-') {
        op = g_lex.tok.kind;
        next_token();
        if (!cf_parse_mul(&rhs))
            return 0;
        if ((out->type & (TYPE_PTR | TYPE_PTR2)) || (rhs.type & (TYPE_PTR | TYPE_PTR2)))
            return 0;
        common = cf_common_arith_type(out->type, rhs.type);
        cf_convert_to_type(out, common);
        cf_convert_to_type(&rhs, common);
        if (cf_no_eval)
            out->u = 0;
        else if (op == '+') {
            if (cf_ice_mode && !(common & TYPE_UNSIGNED) &&
                cf_signed_add_overflows(cf_signed_value(*out),
                                        cf_signed_value(rhs), common))
                cf_ice_invalid = 1;
            out->u = (out->u + rhs.u) & cf_mask_for_type(common);
        } else {
            if (cf_ice_mode && !(common & TYPE_UNSIGNED) &&
                cf_signed_sub_overflows(cf_signed_value(*out),
                                        cf_signed_value(rhs), common))
                cf_ice_invalid = 1;
            out->u = (out->u - rhs.u) & cf_mask_for_type(common);
        }
        out->type = common;
        cf_cast_to_type(out, common);
    }
    return 1;
}

int cf_parse_shift(struct ConstVal *out)
{
    struct ConstVal rhs;
    int op;
    int lhs_type;
    int width;
    long sc;
    long sv;

    if (!cf_parse_add(out))
        return 0;
    while (g_lex.tok.kind == TOK_SHL || g_lex.tok.kind == TOK_SHR) {
        op = g_lex.tok.kind;
        next_token();
        if (!cf_parse_add(&rhs))
            return 0;
        lhs_type = cf_promote_type(out->type);
        cf_convert_to_type(out, lhs_type);
        if (cf_no_eval) {
            out->u = 0;
            out->type = lhs_type;
            continue;
        }
        sc = cf_signed_value(rhs);
        if (sc < 0)
            return 0;
        width = type_is_long(lhs_type) ? 32 : 16;
        if (sc >= width)
            return 0;
        if (op == TOK_SHL) {
            if (cf_ice_mode && !(lhs_type & TYPE_UNSIGNED) &&
                (cf_signed_value(*out) < 0 ||
                 cf_signed_value(*out) >
                    (cf_signed_max_for_type(lhs_type) >> (int)sc)))
                cf_ice_invalid = 1;
            out->u = (out->u << (int)sc) & cf_mask_for_type(lhs_type);
        } else if (lhs_type & TYPE_UNSIGNED) {
            out->u = (out->u & cf_mask_for_type(lhs_type)) >> (int)sc;
        } else {
            sv = cf_signed_value(*out);
            out->u = (unsigned long)(sv >> (int)sc);
        }
        out->type = lhs_type;
        cf_cast_to_type(out, lhs_type);
    }
    return 1;
}

int cf_parse_rel(struct ConstVal *out)
{
    struct ConstVal rhs;
    int op;
    int common;
    int r;

    if (!cf_parse_shift(out))
        return 0;
    while (g_lex.tok.kind == '<' || g_lex.tok.kind == '>' || g_lex.tok.kind == TOK_LE || g_lex.tok.kind == TOK_GE) {
        op = g_lex.tok.kind;
        next_token();
        if (!cf_parse_shift(&rhs))
            return 0;
        common = cf_common_arith_type(out->type, rhs.type);
        cf_convert_to_type(out, common);
        cf_convert_to_type(&rhs, common);
        if (cf_no_eval) {
            r = 0;
        } else if (common & TYPE_UNSIGNED) {
            unsigned long a = out->u & cf_mask_for_type(common);
            unsigned long b = rhs.u & cf_mask_for_type(common);
            r = (op == '<') ? (a < b) : (op == '>') ? (a > b) : (op == TOK_LE) ? (a <= b) : (a >= b);
        } else {
            long a = cf_signed_value(*out);
            long b = cf_signed_value(rhs);
            r = (op == '<') ? (a < b) : (op == '>') ? (a > b) : (op == TOK_LE) ? (a <= b) : (a >= b);
        }
        out->u = r ? 1UL : 0UL;
        out->type = TYPE_INT;
    }
    return 1;
}

int cf_parse_eq(struct ConstVal *out)
{
    struct ConstVal rhs;
    int op;
    int common;
    int r;

    if (!cf_parse_rel(out))
        return 0;
    while (g_lex.tok.kind == TOK_EQ || g_lex.tok.kind == TOK_NE) {
        op = g_lex.tok.kind;
        next_token();
        if (!cf_parse_rel(&rhs))
            return 0;
        common = cf_common_arith_type(out->type, rhs.type);
        cf_convert_to_type(out, common);
        cf_convert_to_type(&rhs, common);
        r = cf_no_eval ? 0 :
            ((out->u & cf_mask_for_type(common)) == (rhs.u & cf_mask_for_type(common)));
        if (op == TOK_NE)
            r = !r;
        out->u = r ? 1UL : 0UL;
        out->type = TYPE_INT;
    }
    return 1;
}

int cf_parse_band(struct ConstVal *out)
{
    struct ConstVal rhs;
    int common;
    if (!cf_parse_eq(out)) return 0;
    while (g_lex.tok.kind == '&') {
        next_token();
        if (!cf_parse_eq(&rhs)) return 0;
        common = cf_common_arith_type(out->type, rhs.type);
        cf_convert_to_type(out, common);
        cf_convert_to_type(&rhs, common);
        out->u = cf_no_eval ? 0 : (out->u & rhs.u) & cf_mask_for_type(common);
        out->type = common;
    }
    return 1;
}

int cf_parse_bxor(struct ConstVal *out)
{
    struct ConstVal rhs;
    int common;
    if (!cf_parse_band(out)) return 0;
    while (g_lex.tok.kind == '^') {
        next_token();
        if (!cf_parse_band(&rhs)) return 0;
        common = cf_common_arith_type(out->type, rhs.type);
        cf_convert_to_type(out, common);
        cf_convert_to_type(&rhs, common);
        out->u = cf_no_eval ? 0 : (out->u ^ rhs.u) & cf_mask_for_type(common);
        out->type = common;
    }
    return 1;
}

int cf_parse_bor(struct ConstVal *out)
{
    struct ConstVal rhs;
    int common;
    if (!cf_parse_bxor(out)) return 0;
    while (g_lex.tok.kind == '|') {
        next_token();
        if (!cf_parse_bxor(&rhs)) return 0;
        common = cf_common_arith_type(out->type, rhs.type);
        cf_convert_to_type(out, common);
        cf_convert_to_type(&rhs, common);
        out->u = cf_no_eval ? 0 : (out->u | rhs.u) & cf_mask_for_type(common);
        out->type = common;
    }
    return 1;
}

int cf_parse_land(struct ConstVal *out)
{
    struct ConstVal rhs;
    if (!cf_parse_bor(out)) return 0;
    while (g_lex.tok.kind == TOK_ANDAND) {
        next_token();
        if (cf_no_eval || cf_signed_value(*out) == 0) {
            cf_no_eval++;
            if (!cf_parse_bor(&rhs)) {
                cf_no_eval--;
                return 0;
            }
            cf_no_eval--;
            out->u = 0;
        } else {
            if (!cf_parse_bor(&rhs)) return 0;
            out->u = cf_signed_value(rhs) != 0 ? 1UL : 0UL;
        }
        out->type = TYPE_INT;
    }
    return 1;
}

int cf_parse_lor(struct ConstVal *out)
{
    struct ConstVal rhs;
    if (!cf_parse_land(out)) return 0;
    while (g_lex.tok.kind == TOK_OROR) {
        next_token();
        if (cf_no_eval || cf_signed_value(*out) != 0) {
            cf_no_eval++;
            if (!cf_parse_land(&rhs)) {
                cf_no_eval--;
                return 0;
            }
            cf_no_eval--;
            out->u = cf_no_eval ? 0UL : 1UL;
        } else {
            if (!cf_parse_land(&rhs)) return 0;
            out->u = cf_signed_value(rhs) != 0 ? 1UL : 0UL;
        }
        out->type = TYPE_INT;
    }
    return 1;
}

int cf_parse_cond(struct ConstVal *out)
{
    struct ConstVal true_value;
    struct ConstVal false_value;
    int condition;
    int common;

    if (!cf_parse_lor(out))
        return 0;
    if (g_lex.tok.kind != '?')
        return 1;

    condition = cf_signed_value(*out) != 0;
    next_token();
    if (cf_no_eval || !condition)
        cf_no_eval++;
    if (!cf_parse_cond(&true_value)) {
        if (cf_no_eval || !condition)
            cf_no_eval--;
        return 0;
    }
    if (cf_no_eval || !condition)
        cf_no_eval--;
    if (!accept(':'))
        return 0;
    if (cf_no_eval || condition)
        cf_no_eval++;
    if (!cf_parse_cond(&false_value)) {
        if (cf_no_eval || condition)
            cf_no_eval--;
        return 0;
    }
    if (cf_no_eval || condition)
        cf_no_eval--;

    common = cf_common_arith_type(true_value.type, false_value.type);
    *out = condition ? true_value : false_value;
    cf_convert_to_type(out, common);
    if (cf_no_eval)
        out->u = 0;
    return 1;
}

int try_parse_const_expr_value(struct ConstVal *out)
{
    return cf_parse_cond(out);
}

int try_parse_integer_const_expr_value(struct ConstVal *out)
{
    int parsed;
    int saved_invalid;

    /* Save the caller's ICE-invalid state so a nested evaluation tracks its
     * own validity without clobbering an outer pending result. */
    saved_invalid = cf_ice_invalid;
    cf_ice_mode++;
    cf_ice_invalid = 0;
    parsed = cf_parse_cond(out);
    cf_ice_mode--;
    parsed = parsed && !cf_ice_invalid &&
             type_ptr_depth(out->type) == 0 && !(out->type & TYPE_STRUCT) &&
             !type_is_float(out->type);
    cf_ice_invalid = saved_invalid;
    return parsed;
}

void emit_const_value(struct ConstVal v)
{
    cf_cast_to_type(&v, v.type);
    if (type_size(v.type) == 4) {
        fprintf(g_emit_sink.stream, "\tld hl,%lu\n", v.u & 0xffffUL);
        fprintf(g_emit_sink.stream, "\tld de,%lu\n", (v.u >> 16) & 0xffffUL);
    } else if (type_size(v.type) == 1) {
        unsigned long b = v.u & 0xffUL;
        fprintf(g_emit_sink.stream, "\tld l,%lu\n", b);
        if (v.type & TYPE_UNSIGNED)
            emit("\tld h,0\n");
        else if (b & 0x80UL)
            emit("\tld h,255\n");
        else
            emit("\tld h,0\n");
    } else {
        fprintf(g_emit_sink.stream, "\tld hl,%lu\n", v.u & 0xffffUL);
    }
    g_expr.type = v.type;
}


