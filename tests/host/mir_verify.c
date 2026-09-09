#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"
#include "dcc_ast_gen_internal.h"
#include "dcc_mir_machine_internal.h"
#include <limits.h>

static int failures;

static void set_test_environment(const char *name, const char *value)
{
#ifdef _WIN32
    if (_putenv_s(name, value) != 0)
        fatal("cannot set test environment");
#else
    if (setenv(name, value, 1) != 0)
        fatal("cannot set test environment");
#endif
}

static void clear_test_environment(const char *name)
{
#ifdef _WIN32
    if (_putenv_s(name, "") != 0)
        fatal("cannot clear test environment");
#else
    if (unsetenv(name) != 0)
        fatal("cannot clear test environment");
#endif
}

static void clear_liveness(void)
{
    free(mir.live_in);
    free(mir.live_out);
    mir.live_in = NULL;
    mir.live_out = NULL;
}

static void setup(int count, int values, int labels)
{
    int instruction;

    mir_begin_function("verify_test", "_verify_test", EMIT_SINK_FINAL, 0, 0, 0);
    mir.count = count;
    mir.next_value = values;
    mir.next_label = labels;
    for (instruction = 0; instruction < count; ++instruction) {
        struct MirInsn *insn = &mir.insns[instruction];
        memset(insn, 0, sizeof(*insn));
        insn->opcode = MIR_NOP;
        insn->src1 = -1;
        insn->src2 = -1;
        insn->dst = -1;
        insn->object = -1;
        insn->label = -1;
        insn->phi_pred1 = -1;
        insn->phi_pred2 = -1;
        insn->type = TYPE_INT;
    }
    mir.insns[0].opcode = MIR_LABEL;
    mir.insns[0].label = 0;
    mir.insns[1].opcode = MIR_CONST;
    mir.insns[1].dst = 0;
    mir.insns[count - 1].opcode = MIR_RETURN;
    mir.insns[count - 1].src1 = 0;
}

static void expect_verification(const char *name, int valid)
{
    if (mir_verify_and_dump() != valid) {
        fprintf(stderr, "FAIL %s\n", name);
        ++failures;
    }
    clear_liveness();
}

static int ast_assignment_probe(
    struct AstNode *assign, struct AstNode *lhs,
    struct AstNode *rhs, int op)
{
    memset(assign, 0, sizeof(*assign));
    assign->kind = AST_ASSIGN;
    assign->op = op;
    assign->a = lhs;
    assign->b = rhs;
    assign->type = lhs->type;
    ast_support_cache_begin();
    return ast_gen_supported(assign);
}

static void verify_ast_assignment_support(void)
{
    static const int compound_ops[] = {
        TOK_ADDEQ, TOK_SUBEQ, TOK_MULEQ, TOK_DIVEQ, TOK_MODEQ,
        TOK_ANDEQ, TOK_OREQ, TOK_XOREQ, TOK_SHLEQ, TOK_SHREQ
    };
    struct AstNode assign;
    struct AstNode lhs;
    struct AstNode integer;
    struct AstNode wide;
    struct AstNode real;
    struct AstNode invalid_lvalue;
    struct AstNode unsupported_rhs;
    struct AstNode index;
    struct AstNode inner_index;
    struct AstNode dereference;
    struct AstNode address;
    struct AstNode member;
    struct AstNode owner;
    struct Sym *symbol;
    int saved_dead = expr_result_dead;
    int item;
    int ok = 1;

    memset(&lhs, 0, sizeof(lhs));
    memset(&integer, 0, sizeof(integer));
    memset(&wide, 0, sizeof(wide));
    memset(&real, 0, sizeof(real));
    memset(&invalid_lvalue, 0, sizeof(invalid_lvalue));
    memset(&unsupported_rhs, 0, sizeof(unsupported_rhs));
    memset(&index, 0, sizeof(index));
    memset(&inner_index, 0, sizeof(inner_index));
    memset(&dereference, 0, sizeof(dereference));
    memset(&address, 0, sizeof(address));
    memset(&member, 0, sizeof(member));
    memset(&owner, 0, sizeof(owner));
    lhs.kind = AST_IDENT;
    integer.kind = AST_INT_LIT;
    integer.type = TYPE_INT;
    integer.ival = 3;
    wide.kind = AST_INT_LIT;
    wide.type = TYPE_LONG;
    wide.ival = 5;
    real.kind = AST_FLOAT_LIT;
    real.type = TYPE_FLOAT;
    expr_result_dead = 1;

    lhs.type = TYPE_INT;
    lhs.sval = "__missing_assignment_symbol";
    ok = ok && !ast_assignment_probe(
        &assign, &lhs, &integer, '=');
    ok = ok && !ast_assignment_probe(
        &assign, &lhs, &integer, '?');

    invalid_lvalue.kind = AST_INT_LIT;
    invalid_lvalue.type = TYPE_INT;
    ok = ok && !ast_assignment_probe(
        &assign, &invalid_lvalue, &integer, '=');
    dereference.kind = AST_UNARY;
    dereference.op = '*';
    dereference.a = &invalid_lvalue;
    dereference.type = TYPE_INT;
    ok = ok && !ast_assignment_probe(
        &assign, &dereference, &integer, '=');
    index.kind = AST_INDEX;
    index.a = &invalid_lvalue;
    index.b = &integer;
    index.type = TYPE_INT;
    ok = ok && !ast_assignment_probe(
        &assign, &index, &integer, '=');

    symbol = add_global(
        "verify_assignment_word", TYPE_INT, SC_GLOBAL);
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    ok = ok && ast_assignment_probe(
        &assign, &lhs, &integer, '=');
    unsupported_rhs.kind = AST_BREAK;
    unsupported_rhs.type = TYPE_INT;
    ok = ok && !ast_assignment_probe(
        &assign, &lhs, &unsupported_rhs, '=');
    for (item = 0;
         item < (int)(sizeof(compound_ops) / sizeof(compound_ops[0]));
         ++item)
        ok = ok && ast_assignment_probe(
            &assign, &lhs, &integer, compound_ops[item]);

    symbol = add_global(
        "verify_assignment_long", TYPE_LONG, SC_GLOBAL);
    lhs.type = TYPE_LONG;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    ok = ok && ast_assignment_probe(&assign, &lhs, &wide, '=') &&
         ast_assignment_probe(&assign, &lhs, &integer, '=') &&
         ast_assignment_probe(&assign, &lhs, &real, '=') &&
         ast_assignment_probe(&assign, &lhs, &wide, TOK_ADDEQ) &&
         ast_assignment_probe(&assign, &lhs, &integer, TOK_SHLEQ);

    symbol = add_global(
        "verify_assignment_float", TYPE_FLOAT, SC_GLOBAL);
    lhs.type = TYPE_FLOAT;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    ok = ok && ast_assignment_probe(&assign, &lhs, &real, '=') &&
         ast_assignment_probe(&assign, &lhs, &integer, '=') &&
         ast_assignment_probe(&assign, &lhs, &wide, '=') &&
         ast_assignment_probe(&assign, &lhs, &real, TOK_MULEQ) &&
         !ast_assignment_probe(&assign, &lhs, &real, TOK_MODEQ);

    symbol = add_global(
        "verify_assignment_array", TYPE_INT, SC_GLOBAL);
    symbol->is_array = 1;
    symbol->array_len = 2;
    symbol->elem_size = 2;
    lhs.type = TYPE_INT;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    ok = ok && !ast_assignment_probe(
        &assign, &lhs, &integer, '=');
    symbol->is_array = 0;
    symbol->is_const_value = 1;
    ok = ok && !ast_assignment_probe(
        &assign, &lhs, &integer, '=');

    symbol = add_global(
        "verify_assignment_pointer", type_add_ptr(TYPE_INT), SC_GLOBAL);
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    integer.ival = 0;
    ok = ok && ast_assignment_probe(
        &assign, &lhs, &integer, '=');
    integer.ival = 3;
    ok = ok && !ast_assignment_probe(
        &assign, &lhs, &integer, '=');

    symbol = add_global(
        "verify_assignment_pointer_array", type_add_ptr(TYPE_INT), SC_GLOBAL);
    symbol->is_array = 1;
    symbol->dim_count = 1;
    symbol->dims[0] = 2;
    symbol->array_len = 2;
    symbol->elem_size = 2;
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    index.kind = AST_INDEX;
    index.a = &lhs;
    index.b = &integer;
    index.type = symbol->type;
    integer.ival = 0;
    ok = ok && ast_assignment_probe(
        &assign, &index, &integer, '=') &&
         !ast_assignment_probe(
             &assign, &index, &integer, TOK_ADDEQ);
    integer.ival = 3;
    ok = ok && !ast_assignment_probe(
        &assign, &index, &integer, '=');

    symbol = add_global(
        "verify_assignment_pointer_row", type_add_ptr(TYPE_INT), SC_GLOBAL);
    symbol->dim_count = 1;
    symbol->dims[0] = 4;
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    dereference.kind = AST_UNARY;
    dereference.op = '*';
    dereference.a = &lhs;
    dereference.type = TYPE_INT;
    index.a = &dereference;
    index.type = TYPE_INT;
    ok = ok && ast_assignment_probe(
        &assign, &index, &integer, '=');

    symbol = add_global(
        "verify_assignment_pointer_pointer_row",
        type_add_ptr(type_add_ptr(TYPE_INT)), SC_GLOBAL);
    symbol->dim_count = 1;
    symbol->dims[0] = 4;
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    dereference.a = &lhs;
    dereference.type = type_add_ptr(TYPE_INT);
    index.a = &dereference;
    index.type = type_add_ptr(TYPE_INT);
    integer.ival = 0;
    ok = ok && ast_assignment_probe(
        &assign, &index, &integer, '=') &&
         !ast_assignment_probe(
             &assign, &index, &integer, TOK_ADDEQ);
    integer.ival = 3;
    ok = ok && !ast_assignment_probe(
        &assign, &index, &integer, '=');

    symbol = add_global(
        "verify_assignment_pointer_expr_base", TYPE_INT, SC_GLOBAL);
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    address.kind = AST_UNARY;
    address.op = '&';
    address.a = &lhs;
    address.type = type_add_ptr(TYPE_INT);
    index.a = &address;
    index.type = TYPE_INT;
    ok = ok && ast_assignment_probe(
        &assign, &index, &integer, '=') &&
         !ast_assignment_probe(
             &assign, &index, &integer, TOK_ADDEQ);

    symbol = add_global(
        "verify_assignment_pointer_expr_pointer",
        type_add_ptr(TYPE_INT), SC_GLOBAL);
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    address.a = &lhs;
    address.type = type_add_ptr(symbol->type);
    index.a = &address;
    index.type = symbol->type;
    integer.ival = 0;
    ok = ok && ast_assignment_probe(
        &assign, &index, &integer, '=') &&
         !ast_assignment_probe(
             &assign, &index, &integer, TOK_ADDEQ);
    integer.ival = 3;
    ok = ok && !ast_assignment_probe(
        &assign, &index, &integer, '=');

    symbol = add_global(
        "verify_assignment_nd_pointer", type_add_ptr(TYPE_INT), SC_GLOBAL);
    symbol->is_array = 1;
    symbol->dim_count = 2;
    symbol->dims[0] = 2;
    symbol->dims[1] = 2;
    symbol->array_len = 2;
    symbol->elem_size = 4;
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    inner_index.kind = AST_INDEX;
    inner_index.a = &lhs;
    inner_index.b = &integer;
    inner_index.type = symbol->type;
    index.a = &inner_index;
    index.type = symbol->type;
    integer.ival = 0;
    ok = ok && ast_assignment_probe(
        &assign, &index, &integer, '=') &&
         !ast_assignment_probe(
             &assign, &index, &integer, TOK_ADDEQ);
    integer.ival = 3;
    ok = ok && !ast_assignment_probe(
        &assign, &index, &integer, '=');
    expr_result_dead = 0;
    ok = ok && !ast_assignment_probe(
        &assign, &index, &integer, TOK_ADDEQ);
    expr_result_dead = 1;

    symbol = add_global(
        "verify_assignment_nd_long", TYPE_LONG, SC_GLOBAL);
    symbol->is_array = 1;
    symbol->dim_count = 2;
    symbol->dims[0] = 2;
    symbol->dims[1] = 2;
    symbol->array_len = 2;
    symbol->elem_size = 8;
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    inner_index.a = &lhs;
    inner_index.type = symbol->type;
    index.type = symbol->type;
    ok = ok && ast_assignment_probe(
        &assign, &index, &wide, '=') &&
         ast_assignment_probe(
             &assign, &index, &integer, TOK_SHLEQ);

    symbol = add_global(
        "verify_assignment_nd_float", TYPE_FLOAT, SC_GLOBAL);
    symbol->is_array = 1;
    symbol->dim_count = 2;
    symbol->dims[0] = 2;
    symbol->dims[1] = 2;
    symbol->array_len = 2;
    symbol->elem_size = 8;
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    inner_index.a = &lhs;
    inner_index.type = symbol->type;
    index.type = symbol->type;
    ok = ok && ast_assignment_probe(
        &assign, &index, &real, '=') &&
         ast_assignment_probe(
             &assign, &index, &real, TOK_MULEQ);

    if (nfield_defs < MAX_FIELDS) {
        struct FieldDef *field;
        int sid = add_struct_def("verify_assignment_record");

        field = &field_defs[nfield_defs++];
        memset(field, 0, sizeof(*field));
        strcpy(field->name, "pointer");
        field->parent_struct_id = sid;
        field->type = type_add_ptr(TYPE_INT);
        symbol = add_global(
            "verify_assignment_record_value", make_struct_type(sid), SC_GLOBAL);
        owner.kind = AST_IDENT;
        owner.type = symbol->type;
        owner.sval = symbol->name;
        owner.sym = symbol;
        member.kind = AST_MEMBER;
        member.op = '.';
        member.a = &owner;
        member.sval = field->name;
        member.type = field->type;
        index.a = &member;
        index.type = TYPE_INT;
        ok = ok && ast_assignment_probe(
            &assign, &index, &integer, '=');
        field->type = type_add_ptr(field->type);
        member.type = field->type;
        index.type = type_decay_ptr(field->type);
        integer.ival = 0;
        ok = ok && ast_assignment_probe(
            &assign, &index, &integer, '=') &&
             !ast_assignment_probe(
                 &assign, &index, &integer, TOK_ADDEQ);
        integer.ival = 3;
    } else {
        ok = 0;
    }

    expr_result_dead = saved_dead;
    if (!ok) {
        fprintf(stderr, "FAIL AST assignment support matrix\n");
        ++failures;
    }
}

static void diamond(void);

static void verify_diamond_edge_liveness(void)
{
    size_t left;
    size_t right;

    diamond();
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL diamond edge liveness verification\n");
        ++failures;
        clear_liveness();
        return;
    }
    left = (size_t)5 * mir.next_value;
    right = (size_t)7 * mir.next_value;
    if (!mir.live_out[left + 1] || mir.live_out[left + 2] ||
        mir.live_out[right + 1] || !mir.live_out[right + 2]) {
        fprintf(stderr, "FAIL PHI values must be live only on their own edges\n");
        ++failures;
    }
    clear_liveness();
}

static void verify_immediate_phi_consumer_forwarding(void)
{
    setup(12, 5, 4);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 2;
    mir.insns[3].opcode = MIR_BRANCH_FALSE;
    mir.insns[3].src1 = 0;
    mir.insns[3].label = 2;
    mir.insns[4].opcode = MIR_LABEL;
    mir.insns[4].label = 1;
    mir.insns[5].opcode = MIR_CONST;
    mir.insns[5].dst = 1;
    mir.insns[6].opcode = MIR_JUMP;
    mir.insns[6].label = 3;
    mir.insns[7].opcode = MIR_LABEL;
    mir.insns[7].label = 2;
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 3;
    mir.insns[9].opcode = MIR_PHI;
    mir.insns[9].dst = 3;
    mir.insns[9].src1 = 1;
    mir.insns[9].src2 = 2;
    mir.insns[9].phi_pred1 = 1;
    mir.insns[9].phi_pred2 = 2;
    mir.insns[10].opcode = MIR_UNARY;
    mir.insns[10].dst = 4;
    mir.insns[10].src1 = 3;
    mir.insns[10].immediate = '-';
    mir.insns[11].src1 = 4;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL immediate PHI consumer control\n");
        ++failures;
        clear_liveness();
        return;
    }
    mir_reset_phi_return_forwarding_count();
    mir_forward_immediate_phi_returns();
    if (mir_phi_return_forwarding_count_value() != 1 ||
        mir.insns[6].opcode != MIR_UNARY ||
        mir.insns[6].src1 != 1 ||
        mir.insns[7].opcode != MIR_RETURN ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[9].opcode != MIR_UNARY ||
        mir.insns[9].src1 != 2 ||
        mir.insns[10].opcode != MIR_RETURN ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        !mir_verify_and_dump()) {
        fprintf(stderr, "FAIL immediate PHI consumer forwarding\n");
        ++failures;
    }
    clear_liveness();
}

static void verify_call_argument_liveness(void)
{
    size_t call;

    setup(6, 3, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[3].opcode = MIR_CALL;
    mir.insns[3].dst = 1;
    mir.insns[4].opcode = MIR_CONST;
    mir.insns[4].dst = 2;
    mir.insns[5].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL call argument liveness verification\n");
        ++failures;
        clear_liveness();
        return;
    }
    call = (size_t)3 * mir.next_value;
    if (!mir.live_in[call] || mir.live_out[call]) {
        fprintf(stderr, "FAIL argument must remain live through its matching call\n");
        ++failures;
    }
    clear_liveness();
}

static void verify_mir_stream_io(void)
{
    static const char input[] = "abcdef";
    char output[8];
    MirStream *stream = mir_stream_open();
    MirStream *copy = mir_stream_open();
    MirStream *empty = mir_stream_open();
    FILE *file = tmpfile();
    unsigned long hash;

    if (stream == NULL || copy == NULL || empty == NULL || file == NULL) {
        fprintf(stderr, "FAIL MIR stream allocation\n");
        ++failures;
        mir_stream_close(stream);
        mir_stream_close(copy);
        mir_stream_close(empty);
        if (file != NULL)
            fclose(file);
        return;
    }
    memset(output, 0, sizeof(output));
    if (mir_stream_write(input, 0, 3, stream) != 0 ||
        mir_stream_write(input, 2, 0, stream) != 0 ||
        mir_stream_write(input, 2, 3, stream) != 3 ||
        mir_stream_tell(stream) != 6 ||
        mir_stream_seek(stream, -2, SEEK_END) != 0 ||
        mir_stream_tell(stream) != 4 ||
        mir_stream_seek(stream, -2, SEEK_CUR) != 0 ||
        mir_stream_tell(stream) != 2 ||
        mir_stream_seek(stream, 0, SEEK_SET) != 0 ||
        mir_stream_read(output, 0, 4, stream) != 0 ||
        mir_stream_read(output, 2, 0, stream) != 0 ||
        mir_stream_read(output, 2, 2, stream) != 2 ||
        mir_stream_read(output + 4, 2, 2, stream) != 1 ||
        memcmp(output, input, 6) != 0 ||
        mir_stream_read(output, 1, 1, stream) != 0 ||
        mir_stream_seek(stream, -1, SEEK_SET) == 0 ||
        mir_stream_seek(stream, 0, 12345) == 0) {
        fprintf(stderr, "FAIL MIR stream block I/O contract\n");
        ++failures;
    }
    mir_stream_rewind(stream);
    mir_stream_puts("prefix:", copy);
    mir_stream_copy(stream, copy);
    mir_stream_rewind(copy);
    memset(output, 0, sizeof(output));
    if (mir_stream_read(output, 1, 7, copy) != 7 ||
        memcmp(output, "prefix:", 7) != 0 ||
        mir_stream_getc(copy) != 'a') {
        fprintf(stderr, "FAIL MIR stream copy contract\n");
        ++failures;
    }
    hash = mir_stream_copy_to_file(stream, file);
    rewind(file);
    memset(output, 0, sizeof(output));
    if (hash == 2166136261UL ||
        fread(output, 1, 6, file) != 6 ||
        memcmp(output, input, 6) != 0 ||
        mir_stream_copy_to_file(empty, file) != 2166136261UL) {
        fprintf(stderr, "FAIL MIR stream file transfer contract\n");
        ++failures;
    }
    fclose(file);
    mir_stream_close(empty);
    mir_stream_close(copy);
    mir_stream_close(stream);
    mir_stream_close(NULL);
}

static void verify_ast_kind_names(void)
{
    static const char *names[] = {
        "none", "int", "float", "str", "ident", "call", "index", "member",
        "unary", "postfix", "binary", "logand", "logor", "assign", "cond",
        "cast", "compound-literal", "comma", "sizeof-expr", "sizeof-type",
        "expr-stmt", "compound", "decl", "if", "while", "do-while", "for",
        "switch", "case", "default", "return", "break", "continue", "goto",
        "label", "empty"
    };
    int kind;

    if ((int)(sizeof(names) / sizeof(names[0])) != AST_EMPTY + 1) {
        fprintf(stderr, "FAIL AST kind name inventory\n");
        ++failures;
        return;
    }
    for (kind = AST_NONE; kind <= AST_EMPTY; ++kind)
        if (strcmp(ast_kind_name(kind), names[kind]) != 0) {
            fprintf(stderr, "FAIL AST kind name %d\n", kind);
            ++failures;
        }
    if (strcmp(ast_kind_name(-1), "?") != 0 ||
        strcmp(ast_kind_name(AST_DIVMOD_CALL), "?") != 0) {
        fprintf(stderr, "FAIL synthetic AST kind names\n");
        ++failures;
    }
}

static void verify_simple_mir_feature_queries(void)
{
    const struct MirInsn *parameter = (const struct MirInsn *)1;
    long constant = -1;
    int ok = 1;

    setup(3, 1, 1);
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL simple MIR feature query verification\n");
        ++failures;
        clear_liveness();
        return;
    }
    mir_reset_phi_return_forwarding_count();
    mir_reset_boolean_phi_branch_simplification_count();
    ok = ok && mir_affine_value(0, &parameter, &constant, 0) &&
         parameter == NULL && constant == 0;
    ok = ok && mir_first_nonlabel_successor(0) == 1;
    ok = ok && mir_boolean_phi_branch_candidate_count() == 0;
    ok = ok && mir_phi_return_forwarding_count_value() == 0;
    ok = ok && mir_extended_integer_constant_conversion_folds() == 0;
    ok = ok && mir_repeated_named_pointer_load_count() == 0;
    ok = ok && mir_value_number_global_field_loads() == 0;
    ok = ok && mir_global_field_value_numbering_count() == 0;
    ok = ok && mir_eliminate_common_block_expressions() == 0;
    ok = ok && mir_common_block_expression_elimination_count() == 0;
    ok = ok && mir_eliminate_common_region_expressions() == 0;
    ok = ok && mir_lazy_byte_parameter_count() == 0;
    ok = ok && mir_homed_rematerializable_wide_candidate_count() == 0;
    mir_begin_strict_phi_fallthrough();
    ok = ok && !mir_strict_phi_fallthrough_was_used();
    mir_end_strict_phi_fallthrough();
    ok = ok && !mir_strict_phi_fallthrough_was_used();
    mir_begin_block_cse_address_rematerialization();
    ok = ok && mir_address_rematerialization_candidate_count() == 0;
    mir_end_block_cse_address_rematerialization();
    ok = ok && mir_begin_rematerialized_home_allocation();
    ok = ok && mir_rematerialized_home_allocation_is_active();
    ok = ok && !mir_begin_rematerialized_home_allocation();
    mir_end_rematerialized_home_allocation();
    ok = ok && !mir_rematerialized_home_allocation_is_active();
    mir_end_rematerialized_home_allocation();
    if (!ok) {
        fprintf(stderr, "FAIL simple MIR feature query contract\n");
        ++failures;
    }
    clear_liveness();
}

static void verify_parameter_emitters(void)
{
    struct MirInsn parameter;
    MirStream *stream;
    char output[512];
    size_t length;
    int ok = 1;

    memset(&parameter, 0, sizeof(parameter));
    parameter.opcode = MIR_PARAM;
    parameter.object = 0;
    mir.object_count = 1;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    mir.objects[0].storage = SC_PARAM;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = 4;
    stream = mir_stream_open();
    if (stream == NULL) {
        fprintf(stderr, "FAIL MIR parameter emitter stream allocation\n");
        ++failures;
        return;
    }
    ok = ok && !mir_emit_load_param(stream, NULL);
    ok = ok && mir_emit_load_param(stream, &parameter);
    ok = ok && mir_emit_load_param_de(stream, &parameter);
    mir_emit_iy_prologue(stream);
    mir.objects[0].type = TYPE_LONG;
    mir.objects[0].offset = 6;
    ok = ok && mir_emit_load_param_wide(stream, &parameter);
    mir.objects[0].offset = 126;
    ok = ok && !mir_emit_load_param_wide(stream, &parameter);
    mir_stream_rewind(stream);
    memset(output, 0, sizeof(output));
    length = mir_stream_read(output, 1, sizeof(output) - 1, stream);
    ok = ok && length > 0;
    ok = ok && strstr(output, "\tld l,(ix+4)\n\tld h,(ix+5)\n") != NULL;
    ok = ok && strstr(output, "\tld e,(ix+4)\n\tld d,(ix+5)\n") != NULL;
    ok = ok && strstr(output, "\tpush iy\n\tpush ix\n") != NULL;
    ok = ok && strstr(output,
        "\tld l,(ix+6)\n\tld h,(ix+7)\n"
        "\tld e,(ix+8)\n\tld d,(ix+9)\n") != NULL;
    if (!ok) {
        fprintf(stderr, "FAIL MIR parameter emitter contract\n");
        ++failures;
    }
    mir_stream_close(stream);
}

static void verify_member_metadata_and_address(void)
{
    struct AstNode ident;
    struct AstNode member;
    struct FieldDef *field;
    struct Sym *global;
    struct MirResolvedNamedAddress resolved;
    int sid = add_struct_def("verify_record_type");
    int ok = 1;

    if (nfield_defs >= MAX_FIELDS) {
        fprintf(stderr, "FAIL field table capacity in member metadata test\n");
        ++failures;
        return;
    }
    field = &field_defs[nfield_defs++];
    memset(field, 0, sizeof(*field));
    strcpy(field->name, "value");
    field->parent_struct_id = sid;
    field->type = TYPE_INT;
    field->offset = 2;
    global = add_global(
        "verify_record_value", make_struct_type(sid), SC_GLOBAL);
    global->is_static = 1;
    memset(&ident, 0, sizeof(ident));
    memset(&member, 0, sizeof(member));
    ident.kind = AST_IDENT;
    ident.sval = global->name;
    member.kind = AST_MEMBER;
    member.a = &ident;
    member.sval = field->name;
    ok = ok && ast_member_field_value_type(&member) == TYPE_INT;
    field->is_array = 1;
    field->elem_type = TYPE_CHAR;
    ok = ok && ast_member_field_value_type(&member) == TYPE_CHAR;
    field->is_array = 0;

    setup(4, 2, 1);
    mir.insns[1].opcode = MIR_ADDRESS;
    mir.insns[1].type = type_add_ptr(global->type);
    strcpy(mir.insns[1].name, global->name);
    mir.insns[2].opcode = MIR_MEMBER_ADDRESS;
    mir.insns[2].dst = 1;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_INT | TYPE_PTR;
    mir.insns[2].immediate = field->offset;
    strcpy(mir.insns[2].name, field->name);
    mir.insns[3].src1 = 1;
    ok = ok && mir_resolve_isolated_global_field_address(1, &resolved);
    ok = ok && resolved.root == &mir.insns[1];
    ok = ok && resolved.storage == SC_GLOBAL && resolved.offset == 2;
    ok = ok && resolved.member_depth == 1 && !resolved.has_index;
    ok = ok && strcmp(resolved.base_name, global->name) == 0;
    ok = ok && strcmp(resolved.leaf_member_name, field->name) == 0;
    mir.insns[2].opcode = MIR_INDEX_ADDRESS;
    ok = ok && !mir_resolve_isolated_global_field_address(1, &resolved);
    if (!ok) {
        fprintf(stderr, "FAIL member metadata and isolated address contract\n");
        ++failures;
    }

    setup(7, 5, 1);
    mir.insns[1].opcode = MIR_ADDRESS;
    mir.insns[1].type = type_add_ptr(global->type);
    strcpy(mir.insns[1].name, global->name);
    mir.insns[2].opcode = MIR_MEMBER_ADDRESS;
    mir.insns[2].dst = 1;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_INT | TYPE_PTR;
    mir.insns[2].immediate = field->offset;
    strcpy(mir.insns[2].name, field->name);
    mir.insns[3].opcode = MIR_LOAD_INDIRECT;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 1;
    mir.insns[3].memory_size = 2;
    mir.insns[4].opcode = MIR_LOAD_INDIRECT;
    mir.insns[4].dst = 3;
    mir.insns[4].src1 = 1;
    mir.insns[4].memory_size = 2;
    mir.insns[5].opcode = MIR_BINARY;
    mir.insns[5].dst = 4;
    mir.insns[5].src1 = 2;
    mir.insns[5].src2 = 3;
    mir.insns[5].immediate = '+';
    mir.insns[5].secondary_offset = TYPE_INT;
    mir.insns[6].src1 = 4;
    if (!mir_verify_and_dump() ||
        mir_value_number_global_field_loads() != 1 ||
        mir_global_field_value_numbering_count() != 1 ||
        mir.insns[4].opcode != MIR_NOP ||
        mir.insns[5].src1 != 2 || mir.insns[5].src2 != 2 ||
        !mir_verify_and_dump()) {
        fprintf(stderr, "FAIL isolated global field value numbering\n");
        ++failures;
    }
    clear_liveness();

    setup(7, 5, 1);
    mir.insns[1].opcode = MIR_ADDRESS;
    mir.insns[1].type = type_add_ptr(global->type);
    strcpy(mir.insns[1].name, global->name);
    mir.insns[2].opcode = MIR_MEMBER_ADDRESS;
    mir.insns[2].dst = 1;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_INT | TYPE_PTR;
    mir.insns[2].immediate = field->offset;
    strcpy(mir.insns[2].name, field->name);
    mir.insns[3].opcode = MIR_LOAD_INDIRECT;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 1;
    mir.insns[3].memory_size = 2;
    mir.insns[4].opcode = MIR_LOAD_INDIRECT;
    mir.insns[4].dst = 3;
    mir.insns[4].src1 = 1;
    mir.insns[4].memory_size = 2;
    mir.insns[5].opcode = MIR_BINARY;
    mir.insns[5].dst = 4;
    mir.insns[5].src1 = 2;
    mir.insns[5].src2 = 3;
    mir.insns[5].immediate = '+';
    mir.insns[5].secondary_offset = TYPE_INT;
    mir.insns[6].src1 = 4;
    if (!mir_verify_and_dump() ||
        mir_eliminate_common_block_expressions() != 1 ||
        mir.insns[4].opcode != MIR_NOP ||
        mir.insns[5].src1 != 2 || mir.insns[5].src2 != 2 ||
        !mir_verify_and_dump()) {
        fprintf(stderr, "FAIL isolated field block CSE\n");
        ++failures;
    }
    clear_liveness();
}

static void verify_five_call_arguments(void)
{
    int arguments[5];
    int argument;
    int ok = 1;

    setup(9, 1, 1);
    mir.next_call_id = 1;
    for (argument = 0; argument < 5; ++argument) {
        struct MirInsn *insn = &mir.insns[argument + 2];
        insn->opcode = MIR_ARG;
        insn->src1 = 0;
        insn->immediate = argument;
    }
    mir.insns[7].opcode = MIR_CALL;
    ok = ok && mir_machine_five_call_arguments(&mir.insns[7], arguments);
    for (argument = 0; argument < 5; ++argument)
        ok = ok && arguments[argument] == 0;
    mir.insns[6].immediate = 3;
    ok = ok && !mir_machine_five_call_arguments(&mir.insns[7], arguments);
    if (!ok) {
        fprintf(stderr, "FAIL five-argument call recovery contract\n");
        ++failures;
    }
}

static void verify_spilled_feature_defaults(void)
{
    int ok = 1;

    setup(3, 1, 1);
    ok = ok && !mir_spilled_cfg_has_divmod_pair();
    ok = ok && !mir_spilled_cfg_divmod_has_dead_result();
    ok = ok && !mir_spilled_cfg_has_wide_mulmod_fusion();
    ok = ok && !mir_spilled_cfg_depends_on_dead_store_forwarding();
    ok = ok && !mir_spilled_cfg_depends_on_direct_byte_param();
    ok = ok && !mir_spilled_cfg_depends_on_constant_index_absolute();
    ok = ok && !mir_spilled_cfg_depends_on_constant_absolute();
    ok = ok && !mir_spilled_cfg_depends_on_dynamic_index_base_forwarding();
    ok = ok && !mir_spilled_cfg_depends_on_wide_constant_rematerialization();
    ok = ok && !mir_spilled_cfg_depends_on_indirect_incdec();
    ok = ok && !mir_spilled_cfg_depends_on_pointer_difference_shift();
    ok = ok && !mir_spilled_cfg_depends_on_wide_call_constant_comparison();
    ok = ok && !mir_spilled_cfg_depends_on_local_constant_byte_store();
    ok = ok &&
        !mir_spilled_cfg_depends_only_on_unsigned_wide_constant_relational();
    ok = ok && !mir_spilled_cfg_depends_on_unary_not_branch_fusion();
    ok = ok && !mir_spilled_cfg_depends_on_planned_stack_handoff();
    ok = ok && !mir_spilled_cfg_depends_on_planned_index_base_handoff();
    ok = ok && !mir_spilled_cfg_depends_on_stable_pointer_local_home();
    ok = ok && !mir_spilled_cfg_depends_on_stable_pointer_local_slot();
    ok = ok && !mir_spilled_cfg_depends_on_rhs_stack_forwarding();
    ok = ok && !mir_spilled_cfg_depends_on_binary_load_pair_forwarding();
    ok = ok && !mir_spilled_cfg_depends_on_dense_byte_switch();
    ok = ok && !mir_spilled_cfg_dense_byte_switch_case_count();
    ok = ok && mir_spilled_cfg_dense_byte_switch_width() == 1;
    ok = ok && !mir_spilled_cfg_dense_byte_switch_uses_direct_condition();
    ok = ok &&
        !mir_spilled_cfg_dense_byte_switch_uses_postincrement_index();
    ok = ok && !mir_spilled_cfg_inline_postincrement_uses();
    ok = ok && !mir_spilled_cfg_inline_indexed_stack_store_uses();
    ok = ok && !mir_spilled_cfg_inline_simple_indexed_store_uses();
    ok = ok && !mir_spilled_cfg_small_selfstore_add_uses();
    ok = ok && !mir_spilled_cfg_uses_exact_semantic_kernel();
    ok = ok &&
        !mir_spilled_cfg_depends_on_indirect_store_value_forwarding();
    ok = ok && !mir_spilled_cfg_indirect_store_value_forwarding_uses();
    ok = ok && !mir_spilled_cfg_depends_on_branch_condition_forwarding();
    ok = ok && !mir_spilled_cfg_branch_condition_forwarding_uses();
    ok = ok &&
        !mir_spilled_cfg_depends_on_indirect_store_address_forwarding();
    ok = ok && !mir_spilled_cfg_depends_on_promoted_local_slot_reuse();
    ok = ok && !mir_spilled_cfg_depends_on_wide_store_forwarding();
    ok = ok && !mir_spilled_cfg_indirect_store_address_forwarding_uses();
    ok = ok && !mir_wide_binary_rhs_forwarding_use_count();
    ok = ok && !mir_homed_cfg_depends_on_unary_not_branch();
    ok = ok && !mir_homed_cfg_was_frameless();
    if (!ok) {
        fprintf(stderr, "FAIL MIR candidate feature state leaked between attempts\n");
        ++failures;
    }
}

static int spilled_candidate_result(void)
{
    MirStream *stream;
    int result;

    mir_invalidate_use_cache();
    stream = mir_stream_open();
    if (stream == NULL) {
        fprintf(stderr, "FAIL spilled preflight stream allocation\n");
        ++failures;
        clear_liveness();
        return -1;
    }
    result = mir_try_emit_spilled_scalar_cfg(stream);
    mir_stream_close(stream);
    clear_liveness();
    return result;
}

static void expect_spilled_candidate(const char *name, int expected)
{
    int actual = spilled_candidate_result();

    if (actual != expected) {
        fprintf(stderr, "FAIL spilled preflight %s\n", name);
        ++failures;
    }
}

static void verify_spilled_preflight_rejection(void)
{
    int sid;

    setup(3, 1, 1);
    set_test_environment("DCC_MIR_EXACT_SHAPE_REPORT", "1");
    set_test_environment("DCC_MIR_SLOT_ACCESS_REPORT", "1");
    expect_spilled_candidate("valid control", 1);
    clear_test_environment("DCC_MIR_SLOT_ACCESS_REPORT");
    clear_test_environment("DCC_MIR_EXACT_SHAPE_REPORT");

    setup(3, 1, 1);
    mir.local_bytes = 30001;
    expect_spilled_candidate("oversized frame rejection", 0);

    sid = add_struct_def("verify_spilled_invalid_return");
    struct_defs[sid - 1].size = 0;
    setup(3, 1, 1);
    mir.return_type = make_struct_type(sid);
    expect_spilled_candidate("invalid return rejection", 0);

    sid = add_struct_def("verify_spilled_wide_value");
    struct_defs[sid - 1].size = 6;
    setup(3, 1, 1);
    mir.insns[1].type = make_struct_type(sid);
    expect_spilled_candidate("wide value rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_OPAQUE;
    expect_spilled_candidate("opcode rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_LOAD;
    set_test_environment("DCC_MIR_SELECT_REPORT", "1");
    expect_spilled_candidate("memory location rejection", 0);
    clear_test_environment("DCC_MIR_SELECT_REPORT");

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_LOAD_INDIRECT;
    mir.insns[1].src1 = 0;
    mir.insns[1].memory_size = 3;
    expect_spilled_candidate("indirect width rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_STORE_INDIRECT;
    mir.insns[1].src1 = 0;
    expect_spilled_candidate("missing indirect width rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_LOAD_INDIRECT;
    mir.insns[1].src1 = 0;
    mir.insns[1].memory_size = 4;
    mir.insns[1].bit_width = 1;
    expect_spilled_candidate("bitfield indirect width rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_CALL;
    mir.insns[1].dst = -1;
    strcpy(mir.insns[1].name, "<indirect>");
    mir.next_value = 0;
    expect_spilled_candidate("call ABI rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_CALL;
    mir.insns[1].dst = -1;
    mir.insns[1].type = make_struct_type(sid);
    strcpy(mir.insns[1].name, "verify_wide_call");
    mir.next_value = 0;
    expect_spilled_candidate("wide call ABI rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_CALL_AGGREGATE;
    mir.insns[1].dst = -1;
    strcpy(mir.insns[1].name, "<indirect>");
    mir.next_value = 0;
    expect_spilled_candidate("aggregate call ABI rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_CALL_AGGREGATE;
    mir.insns[1].dst = -1;
    strcpy(mir.insns[1].name, "verify_aggregate_call");
    mir.next_value = 0;
    expect_spilled_candidate("aggregate call size rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_VA_ARG;
    mir.insns[1].immediate = -129;
    mir.insns[1].secondary_offset = 2;
    expect_spilled_candidate("negative va_arg offset rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_VA_ARG;
    mir.insns[1].immediate = 128;
    mir.insns[1].secondary_offset = 2;
    expect_spilled_candidate("va_arg offset rejection", 0);

    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_VA_ARG;
    mir.insns[1].secondary_offset = 3;
    expect_spilled_candidate("va_arg width rejection", 0);
}

static void verify_immediate_phi_return_forwarding(void)
{
    setup(11, 4, 4);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 2;
    mir.insns[3].opcode = MIR_BRANCH_FALSE;
    mir.insns[3].src1 = 0;
    mir.insns[3].label = 2;
    mir.insns[4].opcode = MIR_LABEL;
    mir.insns[4].label = 1;
    mir.insns[5].opcode = MIR_CONST;
    mir.insns[5].dst = 1;
    mir.insns[6].opcode = MIR_JUMP;
    mir.insns[6].label = 3;
    mir.insns[7].opcode = MIR_LABEL;
    mir.insns[7].label = 2;
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 3;
    mir.insns[9].opcode = MIR_PHI;
    mir.insns[9].dst = 3;
    mir.insns[9].src1 = 1;
    mir.insns[9].src2 = 2;
    mir.insns[9].phi_pred1 = 1;
    mir.insns[9].phi_pred2 = 2;
    mir.insns[10].src1 = 3;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL immediate PHI return control\n");
        ++failures;
        clear_liveness();
        return;
    }
    mir_reset_phi_return_forwarding_count();
    mir_forward_immediate_phi_returns();
    if (mir_phi_return_forwarding_count_value() != 1 ||
        mir.insns[6].opcode != MIR_RETURN ||
        mir.insns[6].src1 != 1 ||
        mir.insns[8].opcode != MIR_RETURN ||
        mir.insns[8].src1 != 2 ||
        mir.insns[9].opcode != MIR_LABEL ||
        !mir_verify_and_dump()) {
        fprintf(stderr, "FAIL immediate PHI return forwarding\n");
        ++failures;
    }
    clear_liveness();
}

static void verify_common_expression_elimination(void)
{
    struct Sym *mutable_global;
    int ok = 1;

    setup(5, 3, 1);
    mir.insns[1].opcode = MIR_ADDRESS;
    mir.insns[1].type = TYPE_INT | TYPE_PTR;
    strcpy(mir.insns[1].name, "verify_address");
    mir.insns[2].opcode = MIR_ADDRESS;
    mir.insns[2].dst = 1;
    mir.insns[2].type = TYPE_INT | TYPE_PTR;
    strcpy(mir.insns[2].name, "verify_address");
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].src1 = 2;
    ok = ok && mir_eliminate_common_block_expressions() == 1;
    ok = ok && mir_common_block_expression_elimination_count() == 1;
    ok = ok && mir.insns[2].opcode == MIR_NOP;
    ok = ok && mir.insns[3].src1 == 0 && mir.insns[3].src2 == 0;

    setup(5, 3, 1);
    mir.insns[1].opcode = MIR_ADDRESS;
    mir.insns[1].type = TYPE_INT | TYPE_PTR;
    strcpy(mir.insns[1].name, "verify_address");
    mir.insns[2].opcode = MIR_ADDRESS;
    mir.insns[2].dst = 1;
    mir.insns[2].type = TYPE_INT | TYPE_PTR;
    strcpy(mir.insns[2].name, "verify_address");
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].src1 = 2;
    ok = ok && mir_eliminate_common_region_expressions() == 1;
    ok = ok && mir.insns[2].opcode == MIR_NOP;
    ok = ok && mir.insns[3].src1 == 0 && mir.insns[3].src2 == 0;

    mutable_global =
        add_global("verify_mutable_global", TYPE_INT, SC_GLOBAL);
    mutable_global->is_static = 0;
    setup(7, 3, 2);
    mir.object_count = 1;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    strcpy(mir.objects[0].name, mutable_global->name);
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].storage = SC_GLOBAL;
    mir.insns[1].opcode = MIR_JUMP;
    mir.insns[1].dst = -1;
    mir.insns[1].label = 1;
    mir.insns[2].opcode = MIR_LABEL;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_LOAD;
    mir.insns[3].dst = 0;
    mir.insns[3].object = 0;
    strcpy(mir.insns[3].name, mutable_global->name);
    mir.insns[4].opcode = MIR_LOAD;
    mir.insns[4].dst = 1;
    mir.insns[4].object = 0;
    strcpy(mir.insns[4].name, mutable_global->name);
    mir.insns[5].opcode = MIR_BINARY;
    mir.insns[5].dst = 2;
    mir.insns[5].src1 = 0;
    mir.insns[5].src2 = 1;
    mir.insns[5].immediate = '+';
    mir.insns[5].secondary_offset = TYPE_INT;
    mir.insns[6].src1 = 2;
    ok = ok && mir_verify_and_dump();
    ok = ok && mir_eliminate_common_block_expressions() == 0;
    ok = ok && mir.insns[3].opcode == MIR_LOAD;
    ok = ok && mir.insns[4].opcode == MIR_LOAD;
    if (!ok) {
        fprintf(stderr, "FAIL common expression elimination contracts\n");
        ++failures;
    }
    clear_liveness();
}

static void verify_scalar_dag_emission(void)
{
    MirStream *stream;
    char output[256];
    int saved_stack_check = opt_stack_check;

    setup(3, 1, 1);
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL scalar DAG verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    stream = mir_stream_open();
    if (stream == NULL) {
        fprintf(stderr, "FAIL scalar DAG stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    opt_stack_check = 0;
    memset(output, 0, sizeof(output));
    if (!mir_try_emit_scalar_dag(stream)) {
        fprintf(stderr, "FAIL scalar DAG constant-return emission\n");
        ++failures;
    } else {
        mir_stream_rewind(stream);
        (void)mir_stream_read(
            output, 1, sizeof(output) - 1, stream);
        if (strstr(output,
                "\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
                "\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n") == NULL) {
            fprintf(stderr, "FAIL scalar DAG output contract:\n%s", output);
            ++failures;
        }
    }
    opt_stack_check = saved_stack_check;
    mir_stream_close(stream);
    clear_liveness();
}

static void diamond(void)
{
    setup(11, 4, 4);
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 2;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    mir.insns[4].opcode = MIR_CONST;
    mir.insns[4].dst = 1;
    mir.insns[5].opcode = MIR_JUMP;
    mir.insns[5].label = 3;
    mir.insns[6].opcode = MIR_LABEL;
    mir.insns[6].label = 2;
    mir.insns[7].opcode = MIR_CONST;
    mir.insns[7].dst = 2;
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 3;
    mir.insns[9].opcode = MIR_PHI;
    mir.insns[9].dst = 3;
    mir.insns[9].src1 = 1;
    mir.insns[9].src2 = 2;
    mir.insns[9].phi_pred1 = 1;
    mir.insns[9].phi_pred2 = 2;
    mir.insns[10].src1 = 3;
}

static void promotion_loop(int initialized)
{
    setup(12, 3, 4);
    mir.local_bytes = 2;
    mir.object_count = 1;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    strcpy(mir.objects[0].name, "loop_value");
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].storage = SC_LOCAL;
    mir.objects[0].offset = -2;
    mir.objects[0].entry_value = -1;
    if (initialized) {
        mir.insns[2].opcode = MIR_STORE;
        mir.insns[2].src1 = 0;
        mir.insns[2].object = 0;
    }
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    mir.insns[4].opcode = MIR_BRANCH_FALSE;
    mir.insns[4].src1 = 0;
    mir.insns[4].label = 3;
    mir.insns[5].opcode = MIR_CONST;
    mir.insns[5].dst = 1;
    mir.insns[6].opcode = MIR_STORE;
    mir.insns[6].src1 = 1;
    mir.insns[6].object = 0;
    mir.insns[7].opcode = MIR_LABEL;
    mir.insns[7].label = 2;
    mir.insns[8].opcode = MIR_JUMP;
    mir.insns[8].label = 1;
    mir.insns[9].opcode = MIR_LABEL;
    mir.insns[9].label = 3;
    mir.insns[10].opcode = MIR_LOAD;
    mir.insns[10].dst = 2;
    mir.insns[10].object = 0;
    mir.insns[11].src1 = 2;
}

static void verify_diamond_mutations(void)
{
    int instruction;
    int field;
    int mutation_count = 0;

    for (instruction = 0; instruction < 11; ++instruction) {
        for (field = 0; field < 6; ++field) {
            int *operand;
            int original;
            char name[96];
            diamond();
            switch (field) {
            case 0: operand = &mir.insns[instruction].src1; break;
            case 1: operand = &mir.insns[instruction].src2; break;
            case 2: operand = &mir.insns[instruction].dst; break;
            case 3: operand = &mir.insns[instruction].label; break;
            case 4: operand = &mir.insns[instruction].phi_pred1; break;
            default: operand = &mir.insns[instruction].phi_pred2; break;
            }
            if (*operand < 0)
                continue;
            original = *operand;
            *operand = 1000000;
            sprintf(name, "diamond invalid field %d at instruction %d", field, instruction);
            expect_verification(name, 0);
            *operand = original;
            sprintf(name, "diamond repaired field %d at instruction %d", field, instruction);
            expect_verification(name, 1);
            ++mutation_count;
        }
    }
    if (mutation_count != 16) {
        fprintf(stderr, "FAIL diamond mutation inventory: %d\n", mutation_count);
        ++failures;
    }
    printf("MIR diamond mutations=%d\n", mutation_count);
}

int main(void)
{
    struct Sym *callee;
    int mutation;
    setup(3, 1, 1);
    mir.count = 0;
    if (!mir_verify_dominance()) {
        fprintf(stderr, "FAIL empty dominance graph\n");
        ++failures;
    }
    mir.count = -1;
    if (mir_verify_dominance()) {
        fprintf(stderr, "FAIL negative dominance graph\n");
        ++failures;
    }
    mir.count = INT_MAX;
    if (mir_verify_dominance()) {
        fprintf(stderr, "FAIL oversized dominance graph\n");
        ++failures;
    }
    verify_diamond_mutations();
    verify_ast_assignment_support();
    verify_diamond_edge_liveness();
    verify_immediate_phi_consumer_forwarding();
    verify_call_argument_liveness();
    verify_mir_stream_io();
    verify_ast_kind_names();
    verify_simple_mir_feature_queries();
    verify_parameter_emitters();
    verify_member_metadata_and_address();
    verify_five_call_arguments();
    verify_spilled_feature_defaults();
    verify_spilled_preflight_rejection();
    verify_immediate_phi_return_forwarding();
    verify_common_expression_elimination();
    verify_scalar_dag_emission();
    for (mutation = 0; mutation < 5; ++mutation) {
        setup(5, 1, 1);
        mir.next_call_id = 1;
        mir.insns[2].opcode = MIR_ARG;
        mir.insns[2].src1 = 0;
        mir.insns[3].opcode = MIR_CALL;
        expect_verification("call mutation control", 1);
        switch (mutation) {
        case 0: mir.insns[3].secondary_offset = -1; break;
        case 1: mir.insns[3].secondary_offset = 1; break;
        case 2: mir.insns[2].secondary_offset = -1; break;
        case 3: mir.insns[2].secondary_offset = 1; break;
        default: mir.insns[2].immediate = -1; break;
        }
        expect_verification("invalid call/argument identity", 0);
    }
    setup(3, 1, 1);
    expect_verification("constant return", 1);
    setup(3, 1, 1);
    mir.insns[2].src1 = 1;
    expect_verification("out-of-range source", 0);
    setup(3, 1, 1);
    mir.insns[2].src1 = -2;
    expect_verification("negative source", 0);
    setup(3, 1, 1);
    mir.insns[1].dst = 1000000;
    expect_verification("out-of-range definition", 0);
    setup(3, 1, 1);
    mir.insns[1].object = 0;
    expect_verification("out-of-range object", 0);
    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_OPAQUE + 1;
    expect_verification("unknown opcode", 0);
    setup(3, 1, 2);
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    expect_verification("missing branch target", 0);
    setup(4, 1, 1);
    mir.insns[2].opcode = MIR_LABEL;
    mir.insns[2].label = 0;
    expect_verification("duplicate label", 0);
    setup(4, 1, 1);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 0;
    expect_verification("duplicate definition", 0);
    setup(3, 2, 1);
    mir.insns[2].src1 = 1;
    expect_verification("undefined value", 0);
    setup(3, 1, 1);
    mir.insns[1].opcode = MIR_BINARY;
    expect_verification("missing binary operands", 0);
    setup(3, 1, 1);
    mir.count = -1;
    expect_verification("negative instruction count", 0);
    setup(3, 1, 1);
    mir.object_count = 257;
    expect_verification("invalid object count", 0);
    diamond();
    expect_verification("valid diamond PHI", 1);
    diamond();
    mir.insns[10].src1 = 1;
    expect_verification("branch value cannot escape join", 0);
    diamond();
    mir.insns[9].src1 = 2;
    mir.insns[9].src2 = 1;
    expect_verification("PHI operands must dominate their own edges", 0);
    diamond();
    mir.insns[9].phi_pred1 = 0;
    expect_verification("PHI label must identify an incoming edge", 0);
    diamond();
    mir.insns[4].opcode = MIR_NOP;
    expect_verification("NOP cannot define a live PHI operand", 0);
    diamond();
    mir.next_value = 5;
    mir.insns[9].src2 = 4;
    expect_verification("undefined PHI input", 0);
    diamond();
    mir.next_label = 5;
    mir.insns[9].phi_pred2 = 4;
    expect_verification("missing PHI predecessor", 0);
    diamond();
    mir.insns[9].phi_pred2 = 1;
    expect_verification("duplicate PHI predecessor", 0);
    setup(8, 3, 3);
    mir.insns[2].opcode = MIR_LABEL;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_PHI;
    mir.insns[3].dst = 1;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 2;
    mir.insns[3].phi_pred1 = 0;
    mir.insns[3].phi_pred2 = 1;
    mir.insns[4].opcode = MIR_BINARY;
    mir.insns[4].dst = 2;
    mir.insns[4].src1 = 1;
    mir.insns[4].src2 = 0;
    mir.insns[4].immediate = '+';
    mir.insns[4].secondary_offset = TYPE_INT;
    mir.insns[5].opcode = MIR_BRANCH_FALSE;
    mir.insns[5].src1 = 2;
    mir.insns[5].label = 1;
    mir.insns[6].opcode = MIR_LABEL;
    mir.insns[6].label = 2;
    mir.insns[7].src1 = 2;
    expect_verification("valid backedge PHI", 1);
    mir.insns[3].src1 = 2;
    expect_verification("backedge value cannot supply loop entry", 0);
    setup(9, 2, 3);
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 2;
    mir.insns[4].opcode = MIR_RETURN;
    mir.insns[4].src1 = 1;
    mir.insns[5].opcode = MIR_LABEL;
    mir.insns[5].label = 1;
    mir.insns[6].opcode = MIR_CONST;
    mir.insns[6].dst = 1;
    mir.insns[7].opcode = MIR_JUMP;
    mir.insns[7].label = 2;
    expect_verification("CFG definition may follow use textually", 1);
    setup(4, 1, 1);
    mir.insns[1].opcode = MIR_UNARY;
    mir.insns[1].src1 = 0;
    expect_verification("ordinary definition cannot use itself", 0);
    diamond();
    mir.insns[10].opcode = MIR_CALL;
    mir.insns[10].src1 = -1;
    mir.next_call_id = 1;
    mir.insns[4].opcode = MIR_ARG;
    mir.insns[4].dst = -1;
    mir.insns[4].src1 = 0;
    mir.insns[9].opcode = MIR_NOP;
    mir.insns[9].src1 = -1;
    mir.insns[9].src2 = -1;
    expect_verification("argument must execute on every call path", 0);
    diamond();
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    mir.insns[9].src2 = 1;
    expect_verification("unreachable PHI predecessor does not constrain dominance", 1);
    diamond();
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    mir.insns[9].src1 = 2;
    expect_verification("unreachable definition cannot supply a reachable PHI edge", 0);
    diamond();
    mir.next_call_id = 1;
    mir.insns[9].opcode = MIR_ARG;
    mir.insns[9].dst = -1;
    mir.insns[9].src1 = 1;
    mir.insns[9].src2 = -1;
    mir.insns[10].opcode = MIR_CALL;
    mir.insns[10].src1 = -1;
    expect_verification("argument value must dominate a join call", 0);
    diamond();
    mir.count = 12;
    mir.next_value = 5;
    mir.insns[11] = mir.insns[10];
    mir.insns[10] = mir.insns[9];
    mir.insns[9].opcode = MIR_BINARY;
    mir.insns[9].dst = 4;
    mir.insns[9].src1 = 0;
    mir.insns[9].src2 = 0;
    mir.insns[9].immediate = '+';
    mir.insns[9].secondary_offset = TYPE_INT;
    expect_verification("late PHI retains logical block-entry dominance", 1);
    setup(8, 2, 3);
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 2;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    mir.insns[4].opcode = MIR_CONST;
    mir.insns[4].dst = 1;
    mir.insns[5].opcode = MIR_LABEL;
    mir.insns[5].label = 2;
    mir.insns[6].opcode = MIR_BRANCH_FALSE;
    mir.insns[6].src1 = 0;
    mir.insns[6].label = 1;
    expect_verification("irreducible CFG with entry-dominating value", 1);
    mir.insns[7].src1 = 1;
    expect_verification("irreducible CFG rejects one-entry definition", 0);
    setup(5, 2, 2);
    mir.insns[1].opcode = MIR_PHI;
    mir.insns[1].dst = 0;
    mir.insns[1].src1 = 1;
    mir.insns[1].src2 = 1;
    mir.insns[1].phi_pred1 = 0;
    mir.insns[1].phi_pred2 = 1;
    mir.insns[2].opcode = MIR_LABEL;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_CONST;
    mir.insns[3].dst = 1;
    mir.insns[4].opcode = MIR_JUMP;
    mir.insns[4].src1 = -1;
    mir.insns[4].label = 0;
    expect_verification("entry PHI cannot manufacture an initial value", 0);
    promotion_loop(0);
    expect_verification("undefined entry retains memory value", 1);
    if (mir.insns[10].opcode != MIR_LOAD || mir.insns[11].src1 != 2) {
        fprintf(stderr, "FAIL loop backedge invented an entry definition\n");
        ++failures;
    }
    promotion_loop(1);
    expect_verification("initialized entry remains valid through loop", 1);
    setup(4, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    expect_verification("orphan argument", 0);
    setup(5, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_CALL;
    mir.insns[3].opcode = MIR_CALL;
    expect_verification("duplicate call identity", 0);
    setup(4, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_CALL;
    strcpy(mir.insns[2].name, "<indirect>");
    expect_verification("indirect call requires a callee value", 0);
    setup(4, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_CALL_AGGREGATE;
    strcpy(mir.insns[2].name, "<indirect>");
    expect_verification("aggregate indirect call requires a callee value", 0);
    setup(6, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    mir.insns[4].opcode = MIR_CALL;
    expect_verification("duplicate argument position", 0);
    setup(5, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_CALL;
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    expect_verification("argument after its call", 0);
    callee = add_global("wide_target", TYPE_INT, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 1;
    callee->proto_types[0] = TYPE_LONG;
    setup(5, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[3].opcode = MIR_CALL;
    strcpy(mir.insns[3].name, "wide_target");
    expect_verification("incorrect prototype argument type", 0);
    mir.insns[2].type = TYPE_LONG;
    expect_verification("argument ABI widening", 1);
    mir.insns[2].opcode = MIR_NOP;
    mir.insns[2].src1 = -1;
    expect_verification("known prototype requires its argument", 0);
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].immediate = 1;
    expect_verification("known prototype rejects excess argument position", 0);
    callee->proto_variadic = 1;
    expect_verification("variadic call cannot omit fixed argument", 0);
    mir.insns[2].immediate = 0;
    expect_verification("variadic call with fixed argument only", 1);
    setup(6, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_LONG;
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    mir.insns[3].immediate = 1;
    mir.insns[4].opcode = MIR_CALL;
    strcpy(mir.insns[4].name, "wide_target");
    expect_verification("variadic call with extra argument", 1);
    mir.insns[3].immediate = 2;
    expect_verification("variadic argument positions must be contiguous", 0);
    mir.insns[3].immediate = 1;
    callee->proto_variadic = 0;
    expect_verification("nonvariadic call rejects extra argument", 0);
    callee->has_proto = 0;
    expect_verification("unprototyped call permits extra arguments", 1);
    mir.insns[2].immediate = 1;
    mir.insns[3].immediate = 0;
    expect_verification("argument records may be in reverse order", 1);
    mir.insns[2].immediate = 1000000;
    expect_verification("unprototyped argument positions cannot be sparse", 0);
    callee->has_proto = 1;
    callee->proto_nargs = 0;
    setup(4, 1, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_CALL;
    strcpy(mir.insns[2].name, "wide_target");
    expect_verification("void parameter list accepts no arguments", 1);
    callee = add_global("callback", TYPE_INT | TYPE_PTR, SC_GLOBAL);
    callee->has_proto = 1;
    callee->is_funcptr = 1;
    callee->proto_nargs = 1;
    callee->proto_types[0] = TYPE_LONG;
    setup(6, 2, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_LOAD;
    mir.insns[2].type = TYPE_INT | TYPE_PTR;
    mir.insns[2].dst = 1;
    strcpy(mir.insns[2].name, "callback");
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    mir.insns[4].opcode = MIR_CALL;
    mir.insns[4].src1 = 1;
    strcpy(mir.insns[4].name, "<indirect>");
    expect_verification("incorrect indirect argument type", 0);
    mir.insns[3].type = TYPE_LONG;
    expect_verification("indirect argument ABI widening", 1);
    mir.insns[3].opcode = MIR_NOP;
    mir.insns[3].src1 = -1;
    expect_verification("indirect prototype requires its argument", 0);
    {
        struct Sym local_callback;
        memset(&local_callback, 0, sizeof(local_callback));
        strcpy(local_callback.name, "local_callback");
        local_callback.type = TYPE_INT | TYPE_PTR;
        local_callback.storage = SC_PARAM;
        local_callback.offset = 4;
        local_callback.is_funcptr = 1;
        local_callback.has_proto = 1;
        local_callback.proto_nargs = 1;
        local_callback.proto_variadic = 1;
        local_callback.proto_types[0] = TYPE_LONG;
        setup(7, 2, 1);
        mir_note_declared_symbol(&local_callback);
        mir.next_call_id = 1;
        mir.insns[2].opcode = MIR_PARAM;
        mir.insns[2].dst = 1;
        mir.insns[2].type = local_callback.type;
        strcpy(mir.insns[2].name, local_callback.name);
        mir.insns[3].opcode = MIR_ARG;
        mir.insns[3].src1 = 0;
        mir.insns[3].type = TYPE_LONG;
        mir.insns[4].opcode = MIR_ARG;
        mir.insns[4].src1 = 0;
        mir.insns[4].immediate = 1;
        mir.insns[5].opcode = MIR_CALL;
        mir.insns[5].src1 = 1;
        strcpy(mir.insns[5].name, "<indirect>");
        expect_verification("local variadic callback retains fixed prefix", 1);
        mir.insns[3].immediate = 2;
        expect_verification("local variadic callback rejects missing fixed prefix", 0);
        mir.insns[3].immediate = 0;
        local_callback.proto_variadic = 0;
        mir_note_declared_symbol(&local_callback);
        expect_verification("local fixed callback rejects extra argument", 0);
        mir.insns[4].opcode = MIR_NOP;
        mir.insns[4].src1 = -1;
        expect_verification("local fixed callback accepts exact arity", 1);
    }
    {
        struct Sym local_callback;
        callee = add_global("shadow_callback", TYPE_INT | TYPE_PTR, SC_GLOBAL);
        callee->has_proto = 1;
        callee->is_funcptr = 1;
        callee->proto_nargs = 2;
        callee->proto_types[0] = TYPE_LONG;
        callee->proto_types[1] = TYPE_LONG;
        memset(&local_callback, 0, sizeof(local_callback));
        strcpy(local_callback.name, callee->name);
        local_callback.type = TYPE_INT | TYPE_PTR;
        local_callback.storage = SC_PARAM;
        local_callback.offset = 4;
        local_callback.is_funcptr = 1;
        setup(6, 2, 1);
        mir_note_declared_symbol(&local_callback);
        mir.next_call_id = 1;
        mir.insns[2].opcode = MIR_PARAM;
        mir.insns[2].dst = 1;
        mir.insns[2].type = local_callback.type;
        strcpy(mir.insns[2].name, local_callback.name);
        mir.insns[3].opcode = MIR_ARG;
        mir.insns[3].src1 = 0;
        mir.insns[4].opcode = MIR_CALL;
        mir.insns[4].src1 = 1;
        strcpy(mir.insns[4].name, "<indirect>");
        expect_verification(
            "unprototyped local callback ignores same-named global prototype", 1);
    }
    printf("MIR verifier failures=%d\n", failures);
    return failures != 0;
}