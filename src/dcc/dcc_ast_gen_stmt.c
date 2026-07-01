/*
 * dcc_ast_gen_stmt.c - switch/for/statement emitters, ast_try_emit_statement.
 *
 * Split from dcc_ast_gen.c; part of the AST codegen module.  Shared
 * prototypes live in dcc_ast_gen_internal.h.
 */
#include <string.h>
#include "dcc_ast_gen_internal.h"


void ast_switch_collect_stmt(const struct AstNode *n, int *case_vals,
                                    int *ncasep, int *have_defaultp)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE) {
        case_vals[(*ncasep)++] = (int)n->ival;
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_DEFAULT) {
        *have_defaultp = 1;
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_collect_stmt(n->list[i], case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        ast_switch_collect_stmt(n->c, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_collect_stmt(n->d, case_vals, ncasep, have_defaultp);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
}

void ast_switch_collect(const struct AstNode *n, int *case_vals,
                               int *ncasep, int *have_defaultp)
{
    *ncasep = 0;
    *have_defaultp = 0;
    ast_switch_collect_stmt(n->b, case_vals, ncasep, have_defaultp);
}

void ast_switch_consume_scan_labels_stmt(const struct AstNode *n)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE || n->kind == AST_DEFAULT) {
        (void)new_label();
        ast_switch_consume_scan_labels_stmt(n->b);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_consume_scan_labels_stmt(n->list[i]);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_consume_scan_labels_stmt(n->b);
        ast_switch_consume_scan_labels_stmt(n->c);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_consume_scan_labels_stmt(n->b);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_consume_scan_labels_stmt(n->d);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_consume_scan_labels_stmt(n->b);
}

void ast_switch_consume_scan_labels(const struct AstNode *n)
{
    ast_switch_consume_scan_labels_stmt(n->b);
}

void ast_switch_assign_labels_stmt(const struct AstNode *n, int *case_vals,
                                          int *case_labs, int ncase,
                                          int *default_labp)
{
    int i;

    if (n == NULL || n->kind == AST_SWITCH)
        return;
    if (n->kind == AST_CASE) {
        i = ast_switch_find_case((int)n->ival, case_vals, ncase);
        if (i >= 0)
            case_labs[i] = new_label();
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_DEFAULT) {
        *default_labp = new_label();
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_COMPOUND) {
        for (i = 0; i < n->list_len; ++i)
            ast_switch_assign_labels_stmt(n->list[i], case_vals, case_labs,
                                          ncase, default_labp);
        return;
    }
    if (n->kind == AST_IF) {
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        ast_switch_assign_labels_stmt(n->c, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_WHILE || n->kind == AST_DOWHILE) {
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_FOR) {
        ast_switch_assign_labels_stmt(n->d, case_vals, case_labs, ncase, default_labp);
        return;
    }
    if (n->kind == AST_LABEL)
        ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
}

void ast_switch_assign_labels(const struct AstNode *n, int *case_vals,
                                     int *case_labs, int ncase,
                                     int *default_labp)
{
    ast_switch_assign_labels_stmt(n->b, case_vals, case_labs, ncase, default_labp);
}

void ast_gen_switch_stmt(const struct AstNode *n)
{
    int case_vals[MAX_SWITCH_CASES];
    int case_labs[MAX_SWITCH_CASES];
    int ncase;
    int have_default;
    int default_lab;
    int minv;
    int maxv;
    int table_ok;
    int lend;
    int i;

    ast_switch_collect(n, case_vals, &ncase, &have_default);
    table_ok = ast_switch_table_ok(case_vals, ncase, &minv, &maxv);

    /* Preserve the historical label allocation order: consume scan labels for
     * every top-level case/default before expression code can allocate labels.
     * The scan allocates in source order, so `default:` before later cases
     * matters. */
    ast_switch_consume_scan_labels(n);

    default_lab = -1;
    if (!table_ok)
        ast_switch_assign_labels(n, case_vals, case_labs, ncase, &default_lab);

    ast_gen_expr(n->a);

    if (table_ok)
        ast_switch_assign_labels(n, case_vals, case_labs, ncase, &default_lab);

    lend = new_label();
    if (table_ok) {
        emit_switch_jump_table(minv, maxv, case_vals, case_labs, ncase,
                               default_lab, lend);
    } else {
        emit("\tex de,hl\n");
        for (i = 0; i < ncase; ++i) {
            fprintf(outf, "\tld hl,%ld\n", (long)(case_vals[i] & 0xffff));
            emit("\tor a\n\tsbc hl,de\n");
            emit_jp_label("jp z,", case_labs[i]);
        }
        emit_jp_label("jp", default_lab >= 0 ? default_lab : lend);
    }

    enter_scope();
    break_stack[nflow] = lend;
    cont_stack[nflow] = (nflow > 0) ? cont_stack[nflow - 1] : lend;
    nflow++;

    if (ast_sw_depth < AST_MAX_SW_NEST) {
        ast_sw_ctx[ast_sw_depth].vals    = case_vals;
        ast_sw_ctx[ast_sw_depth].labs    = case_labs;
        ast_sw_ctx[ast_sw_depth].n       = ncase;
        ast_sw_ctx[ast_sw_depth].def_lab = default_lab;
        ast_sw_depth++;
    }

    ast_gen_stmt(n->b);

    if (ast_sw_depth > 0)
        ast_sw_depth--;
    nflow--;
    leave_scope();
    emit_label(lend);
}

void ast_gen_for_stmt(const struct AstNode *n)
{
    int ltop;
    int linc;
    int lend;
    int for_seq;
    int rename_count;

    for_seq = g_for_seq++;
    if (for_seq >= MAX_FOR_SCOPES)
        fatal("too many for statements");
    rename_count = g_for_rename_count[for_seq];

    ltop = new_label();
    linc = new_label();
    lend = new_label();

    if (n->a != NULL && n->a->kind == AST_DECL) {
        /* C99 for-init declaration: drive declaration codegen
         * through the captured span with the for-scope rename context set, so
         * the loop variable is renamed to its unique internal slot and pushed
         * onto the active rename stack for the body/cond/inc to resolve. */
        int old_for_decl_seq = g_for_decl_seq;
        int old_for_decl_rename_index = g_for_decl_rename_index;
        int old_for_decl_recording = g_for_decl_recording;
        g_for_decl_seq = for_seq;
        g_for_decl_rename_index = 0;
        g_for_decl_recording = 0;
        ast_emit_decl_span(n->a);
        if (g_for_decl_rename_index != rename_count)
            fatal("for-init scope mismatch");
        g_for_decl_seq = old_for_decl_seq;
        g_for_decl_rename_index = old_for_decl_rename_index;
        g_for_decl_recording = old_for_decl_recording;
    } else {
        if (rename_count != 0)
            fatal("unsupported AST for-init scope");
        if (n->a != NULL)
            ast_gen_expr(n->a);
    }

    emit_label(ltop);
    if (n->b != NULL)
        ast_gen_cond_branch(n->b, lend, 0);

    break_stack[nflow] = lend;
    cont_stack[nflow] = linc;
    nflow++;
    ast_gen_stmt(n->d);
    nflow--;

    emit_label(linc);
    if (n->c != NULL) {
        int old_dead = expr_result_dead;
        expr_result_dead = 1;
        if ((n->c->kind == AST_UNARY || n->c->kind == AST_POSTFIX) &&
            (n->c->op == TOK_INC || n->c->op == TOK_DEC)) {
            struct Sym *s = ast_deadincdec_sym_direct(n->c);
            if (s != NULL) {
                emit_incdec_sym_direct(s, n->c->op);
            } else {
                int vt;
                gen_deadincdec_addr_lvalue_ast(n->c, &vt);
                emit_incdec_addr(vt, n->c->op);
            }
        } else {
            ast_gen_expr(n->c);
        }
        expr_result_dead = old_dead;
    }
    emit_jp_label("jp", ltop);
    emit_label(lend);

    /* Close the for-init scope so source names resolve to outer symbols. */
    while (rename_count > 0) {
        pop_for_rename();
        rename_count--;
    }
}

/* Emit statement node `n` (gated by ast_stmt_supported). */
void ast_gen_stmt(const struct AstNode *n)
{
    switch (n->kind) {
    case AST_EMPTY:
        break;                            /* empty statement: emit nothing */
    case AST_DECL:
        ast_emit_decl_span(n);            /* declaration codegen replay */
        break;
    case AST_EXPR_STMT: {
        /* Expression statement results are dead, so emit with
         * expr_result_dead set. */
        int old_dead = expr_result_dead;
        expr_result_dead = 1;
        if ((n->a->kind == AST_UNARY || n->a->kind == AST_POSTFIX) &&
            (n->a->op == TOK_INC || n->a->op == TOK_DEC)) {
            struct Sym *s = ast_deadincdec_sym_direct(n->a);
            if (s != NULL) {
                emit_incdec_sym_direct(s, n->a->op);
            } else {
                int vt;
                gen_deadincdec_addr_lvalue_ast(n->a, &vt);
                emit_incdec_addr(vt, n->a->op);
            }
        } else if (ast_is_local_self_add_stmt(n->a)) {
            ast_emit_local_self_add_stmt(n->a);
        } else {
            ast_gen_expr(n->a);
        }
        expr_result_dead = old_dead;
        break;
    }
    case AST_RETURN:
        gen_return_ast(n);
        break;
    case AST_BREAK:
        emit_jp_label("jp", break_stack[nflow - 1]);
        break;
    case AST_CONTINUE:
        emit_jp_label("jp", cont_stack[nflow - 1]);
        break;
    case AST_GOTO:
        emit_jp_label("jp", mark_user_label_reference(n->sval));
        break;
    case AST_LABEL:
        emit_label(define_user_label(n->sval));
        ast_gen_stmt(n->b);
        break;
    case AST_CASE: {
        long cv;
        int i;
        int lab;
        struct AstSwCtx *sw;

        if (ast_sw_depth <= 0)
            fatal("case label outside switch");
        cv = n->ival;
        lab = -1;
        sw = &ast_sw_ctx[ast_sw_depth - 1];
        for (i = 0; i < sw->n; ++i) {
            if ((sw->vals[i] & 0xffff) == ((int)cv & 0xffff)) {
                lab = sw->labs[i];
                break;
            }
        }
        if (lab >= 0)
            emit_label(lab);
        ast_gen_stmt(n->b);
        break;
    }
    case AST_DEFAULT: {
        struct AstSwCtx *sw;

        if (ast_sw_depth <= 0)
            fatal("default label outside switch");
        sw = &ast_sw_ctx[ast_sw_depth - 1];
        if (sw->def_lab >= 0)
            emit_label(sw->def_lab);
        ast_gen_stmt(n->b);
        break;
    }
    case AST_IF: {
        /* Generic if/else condition shape, including the label allocation
         * order (lelse, lend before the condition). */
        int lelse = new_label();
        int lend = new_label();
        ast_gen_cond_branch(n->a, lelse, 0);
        ast_gen_stmt(n->b);
        emit_jp_label("jp", lend);
        emit_label(lelse);
        if (n->c != NULL)
            ast_gen_stmt(n->c);
        emit_label(lend);
        break;
    }
    case AST_WHILE: {
        /* Generic while shape: ltop, lend allocated up front;
         * label(ltop); test condition -> lend; body inside nflow scope;
         * jp ltop; label(lend). */
        int ltop = new_label();
        int lend = new_label();
        emit_label(ltop);
        if (ast_is_const_nonzero_condition(n->a)) {
            ast_gen_expr(n->a);
            emit_test_expr_nonzero(g_expr_type, lend, 0);
        } else {
            ast_gen_cond_branch(n->a, lend, 0);
        }
        break_stack[nflow] = lend;
        cont_stack[nflow] = ltop;
        nflow++;
        ast_gen_stmt(n->b);
        nflow--;
        emit_jp_label("jp", ltop);
        emit_label(lend);
        break;
    }
    case AST_DOWHILE: {
        /* Generic do-while shape: ltop, lcont, lend
         * allocated up front; label(ltop); body inside nflow scope (break->
         * lend, continue->lcont); label(lcont); test condition -> ltop when
         * TRUE (branch sense 1); label(lend).  For `while(0)`, keep the labels
         * but omit the test/back-edge. */
        int ltop = new_label();
        int lcont = new_label();
        int lend = new_label();
        emit_label(ltop);
        break_stack[nflow] = lend;
        cont_stack[nflow] = lcont;
        nflow++;
        ast_gen_stmt(n->b);
        nflow--;
        emit_label(lcont);
        if (ast_is_const_nonzero_condition(n->a))
            emit_jp_label("jp", ltop);
        else if (!ast_is_const_zero_condition(n->a))
            ast_gen_cond_branch(n->a, ltop, 1);
        emit_label(lend);
        break;
    }
    case AST_FOR:
        ast_gen_for_stmt(n);
        break;
    case AST_SWITCH:
        ast_gen_switch_stmt(n);
        break;
    case AST_COMPOUND: {
        /* enter_scope(); emit each child (statements and AST_DECL declaration
         * spans, the latter running the declaration codegen); leave_scope().
         * enter/leave emit nothing. */
        int i;
        enter_scope();
        for (i = 0; i < n->list_len; ++i)
            ast_gen_stmt(n->list[i]);
        leave_scope();
        break;
    }
    default:
        fatal("ast_gen_stmt: unsupported node");
    }
}

int ast_try_emit_statement(void)
{
    long sv_pos;
    long sv_tok_start;
    int sv_line;
    int sv_tok_line;
    int sv_for_seq;
    struct Token sv_tok;
    struct AstNode *n;
    int report;

    if (scan_mode)
        return 0;

    report = getenv("DCC_AST_REPORT") != NULL;

    sv_pos = posi;
    sv_tok_start = tok_start_pos;
    sv_line = line_no;
    sv_tok_line = tok_line;
    sv_for_seq = g_for_seq;
    sv_tok = tok;

    n = ast_build_stmt(&g_ast_arena);

    if (n != NULL && ast_stmt_supported(n)) {
        g_for_seq = sv_for_seq;
        if (g_ast_build_enabled == 2)
            ast_dump(n, 0);
        ast_gen_stmt(n);
        ast_arena_reset(&g_ast_arena);
        return 1;
    }

    if (report) {
        if (n == NULL) {
                fprintf(stderr, "; AST-unsupported stmt build token=%d text='%s' line=%d\n",
                    sv_tok.kind, sv_tok.text, sv_tok_line);
        } else {
            fprintf(stderr, "; AST-unsupported stmt gate kind=%s line=%d\n",
                    ast_kind_name(n->kind), sv_tok_line);
        }
    }

    posi = sv_pos;
    tok_start_pos = sv_tok_start;
    line_no = sv_line;
    tok_line = sv_tok_line;
    tok = sv_tok;
    g_for_seq = sv_for_seq;
    ast_arena_reset(&g_ast_arena);
    return 0;
}
