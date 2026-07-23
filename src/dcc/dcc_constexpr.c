/*
 * dcc_constexpr.c - integer constant-expression context wrappers.
 *
 * Typed constant evaluation is implemented by the ConstVal engine in
 * dcc_fold.c.  This module provides the context-specific conversions used by
 * declarations and parses the C11 _Static_assert declaration.
 */

#include "dcc.h"

static int parse_integer_const_expr(struct ConstVal *value)
{
    int errors_before;

    errors_before = g_diag_error_count;
    if (try_parse_integer_const_expr_value(value))
        return 1;
    if (g_diag_error_count == errors_before)
        error_here("constant integer expression expected");
    return 0;
}

int parse_typed_const_int_expr(void)
{
    struct ConstVal value;
    unsigned long raw_value;

    if (!parse_integer_const_expr(&value))
        return 0;
    if (value.type & TYPE_UNSIGNED) {
        raw_value = value.u & cf_mask_for_type(value.type);
        if (raw_value > 0x7fffffffUL) {
            error_here("integer constant expression out of range");
            return 0;
        }
        return (int)raw_value;
    }
    return (int)cf_signed_value(value);
}

int parse_typed_array_bound_expr(void)
{
    int errors_before;
    int bound;

    errors_before = g_diag_error_count;
    bound = parse_typed_const_int_expr();
    if (g_diag_error_count != errors_before)
        return 1;
    if (bound <= 0 || (unsigned long)bound > 0xffffUL) {
        error_here("invalid array bound for 16-bit target");
        return 1;
    }
    return bound;
}

int parse_typed_designator_index_expr(void)
{
    int errors_before;
    int index;

    errors_before = g_diag_error_count;
    index = parse_typed_const_int_expr();
    if (g_diag_error_count != errors_before)
        return 0;
    if (index < 0) {
        error_here("negative array initializer designator");
        return 0;
    }
    if ((unsigned long)index > 0xffffUL) {
        error_here("array initializer designator out of range");
        return 0;
    }
    return index;
}

long parse_typed_const_expr_long(void)
{
    struct ConstVal value;

    if (try_parse_const_expr_value(&value))
        return cf_signed_value(value);
    return 0;
}

int parse_typed_enum_const_expr(void)
{
    struct ConstVal value;
    long enum_value;

    if (!parse_integer_const_expr(&value))
        return 0;
    if (value.type & TYPE_UNSIGNED) {
        unsigned long raw_value;

        raw_value = value.u & cf_mask_for_type(value.type);
        if (raw_value > 32767UL) {
            error_here("enumerator value is not representable as 16-bit int");
            return 0;
        }
        return (int)raw_value;
    }
    enum_value = cf_signed_value(value);
    if (enum_value < -32768L || enum_value > 32767L) {
        error_here("enumerator value is not representable as 16-bit int");
        return 0;
    }
    return (int)enum_value;
}

long parse_typed_const_long_expr(void)
{
    struct ConstVal value;

    if (parse_integer_const_expr(&value))
        return cf_signed_value(value);
    return 0;
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
    if (!try_parse_integer_const_expr_value(&value)) {
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
           tok.kind == TOK_INLINE || tok.kind == TOK_NORETURN ||
           tok.kind == TOK_TYPEDEF || tok.kind == TOK_STRUCT ||
           tok.kind == TOK_UNION || tok.kind == TOK_ENUM ||
           (tok.kind == TOK_ID && find_typedef(tok.text) >= 0);
}
