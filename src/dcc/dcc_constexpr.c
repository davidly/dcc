/**
 * @file dcc_constexpr.c
 * @brief Applies declaration-context rules to typed constant expressions.
 *
 * @par Role
 * Wraps the ConstVal evaluator with range and validity checks for array
 * bounds, designators, enumerators, integer and long contexts; parses
 * _Static_assert declarations; and recognizes tokens that can start a type.
 *
 * @par Key entry points
 * parse_typed_const_int_expr(), parse_typed_array_bound_expr(),
 * parse_typed_const_long_expr(), parse_typed_enum_const_expr(),
 * parse_static_assert_decl(), and starts_type().
 *
 * @par Boundary
 * dcc_fold.c owns typed expression evaluation. This module validates specific
 * declaration contexts and emits no function-body code.
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
    while (g_lex.tok.kind != TOK_EOF && g_lex.tok.kind != ';')
        next_token();
    if (g_lex.tok.kind == ';')
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

    assert_tok = g_lex.tok;
    assert_line = g_lex.tok_line;
    assert_pos = g_lex.tok_start_pos;
    expect(TOK_STATIC_ASSERT);
    if (g_lex.tok.kind != '(') {
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
    if (g_lex.tok.kind != ',') {
        error_here("expected ',' in static assertion");
        recover_static_assert_decl();
        return;
    }
    next_token();
    if (g_lex.tok.kind != TOK_STR && g_lex.tok.kind != TOK_WSTR) {
        error_here("string literal expected in static assertion");
        recover_static_assert_decl();
        return;
    }

    prefix = "static assertion failed: ";
    used = strlen(prefix);
    capacity = used + 1;
    message = (char *)xmalloc(capacity);
    strcpy(message, prefix);
    while (g_lex.tok.kind == TOK_STR || g_lex.tok.kind == TOK_WSTR) {
        size_t piece_len;
        size_t needed;

        piece_len = strlen(g_lex.tok.text);
        needed = used + piece_len + 1;
        if (needed > capacity) {
            char *grown;

            capacity = needed * 2;
            grown = (char *)xmalloc(capacity);
            memcpy(grown, message, used + 1);
            free(message);
            message = grown;
        }
        memcpy(message + used, g_lex.tok.text, piece_len + 1);
        used += piece_len;
        next_token();
    }
    if (g_lex.tok.kind != ')') {
        error_here("expected ')' after static assertion message");
        free(message);
        recover_static_assert_decl();
        return;
    }
    next_token();
    if (g_lex.tok.kind != ';') {
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
    return g_lex.tok.kind == TOK_INT || g_lex.tok.kind == TOK_LONG || g_lex.tok.kind == TOK_SHORT || g_lex.tok.kind == TOK_FLOAT || g_lex.tok.kind == TOK_CHAR || g_lex.tok.kind == TOK_VOID ||
            g_lex.tok.kind == TOK_BOOL ||
           g_lex.tok.kind == TOK_UNSIGNED || g_lex.tok.kind == TOK_SIGNED || g_lex.tok.kind == TOK_CONST || g_lex.tok.kind == TOK_VOLATILE ||
           g_lex.tok.kind == TOK_EXTERN || g_lex.tok.kind == TOK_STATIC || g_lex.tok.kind == TOK_REGISTER || g_lex.tok.kind == TOK_AUTO ||
           g_lex.tok.kind == TOK_INLINE || g_lex.tok.kind == TOK_NORETURN ||
           g_lex.tok.kind == TOK_TYPEDEF || g_lex.tok.kind == TOK_STRUCT ||
           g_lex.tok.kind == TOK_UNION || g_lex.tok.kind == TOK_ENUM ||
           (g_lex.tok.kind == TOK_ID && find_typedef(g_lex.tok.text) >= 0);
}
