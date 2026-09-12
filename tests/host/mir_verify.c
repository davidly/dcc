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

static void prepare_test_cfg_metadata(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        struct MirInsn *insn = &mir.insns[instruction];
        int target;

        insn->successor_count = 0;
        if (insn->opcode == MIR_JUMP ||
            insn->opcode == MIR_BRANCH_FALSE) {
            target = mir_find_label(insn->label);
            if (target >= 0)
                insn->successors[insn->successor_count++] = target;
        }
        if (insn->opcode == MIR_BRANCH_FALSE &&
            instruction + 1 < mir.count)
            insn->successors[insn->successor_count++] = instruction + 1;
        else if (insn->opcode != MIR_JUMP &&
                 insn->opcode != MIR_RETURN &&
                 instruction + 1 < mir.count)
            insn->successors[insn->successor_count++] = instruction + 1;
    }
}

static void expect_verification(const char *name, int valid)
{
    if (mir_verify_and_dump() != valid) {
        fprintf(stderr, "FAIL %s\n", name);
        ++failures;
    }
    clear_liveness();
}

static void setup_phi_indirect_call(
    int left_has_proto, int left_nargs,
    int right_has_proto, int right_nargs, int argument_type)
{
    struct Sym left;
    struct Sym right;

    memset(&left, 0, sizeof(left));
    strcpy(left.name, "phi_callback_left");
    left.type = TYPE_INT | TYPE_PTR;
    left.storage = SC_LOCAL;
    left.offset = -2;
    left.is_funcptr = 1;
    left.funcptr_return_type = TYPE_INT;
    left.has_proto = left_has_proto;
    left.proto_nargs = left_nargs;
    left.proto_types[0] = TYPE_LONG;
    memset(&right, 0, sizeof(right));
    strcpy(right.name, "phi_callback_right");
    right.type = TYPE_INT | TYPE_PTR;
    right.storage = SC_LOCAL;
    right.offset = -4;
    right.is_funcptr = 1;
    right.funcptr_return_type = TYPE_INT;
    right.has_proto = right_has_proto;
    right.proto_nargs = right_nargs;
    right.proto_types[0] = TYPE_LONG;
    right.proto_types[1] = TYPE_LONG;

    setup(14, 6, 4);
    mir_note_declared_symbol(&left);
    mir_note_declared_symbol(&right);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 2;
    mir.insns[4].opcode = MIR_LOAD;
    mir.insns[4].dst = 1;
    mir.insns[4].type = left.type;
    strcpy(mir.insns[4].name, left.name);
    mir.insns[5].opcode = MIR_JUMP;
    mir.insns[5].label = 3;
    mir.insns[6].opcode = MIR_LABEL;
    mir.insns[6].label = 1;
    mir.insns[7].opcode = MIR_LOAD;
    mir.insns[7].dst = 2;
    mir.insns[7].type = right.type;
    strcpy(mir.insns[7].name, right.name);
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 3;
    mir.insns[9].opcode = MIR_PHI;
    mir.insns[9].dst = 3;
    mir.insns[9].src1 = 1;
    mir.insns[9].src2 = 2;
    mir.insns[9].type = left.type;
    mir.insns[9].phi_pred1 = 2;
    mir.insns[9].phi_pred2 = 1;
    mir.insns[10].opcode = MIR_CONST;
    mir.insns[10].dst = 4;
    mir.insns[10].type = argument_type;
    mir.insns[11].opcode = MIR_ARG;
    mir.insns[11].src1 = 4;
    mir.insns[11].type = argument_type;
    mir.insns[11].secondary_offset = 0;
    mir.insns[12].opcode = MIR_CALL;
    mir.insns[12].dst = 5;
    mir.insns[12].src1 = 3;
    mir.insns[12].type = TYPE_INT;
    mir.insns[12].secondary_offset = 0;
    strcpy(mir.insns[12].name, "<indirect>");
    mir.insns[13].src1 = 5;
}

static void setup_recorded_indirect_call(
    const struct Sym *prototype, int argument_type)
{
    setup(6, 3, 1);
    mir.next_call_id = 1;
    mir_record_call_signature(0, prototype);
    mir.insns[2].opcode = MIR_UNARY;
    mir.insns[2].dst = 1;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_INT | TYPE_PTR;
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    mir.insns[3].type = argument_type;
    mir.insns[3].secondary_offset = 0;
    mir.insns[4].opcode = MIR_CALL;
    mir.insns[4].dst = 2;
    mir.insns[4].src1 = 1;
    mir.insns[4].type = TYPE_INT;
    mir.insns[4].secondary_offset = 0;
    strcpy(mir.insns[4].name, "<indirect>");
    mir.insns[5].src1 = 2;
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

static int expect_ast_assignment_support(
    const char *name, struct AstNode *assign, struct AstNode *lhs,
    struct AstNode *rhs, int op, int expected)
{
    int actual = ast_assignment_probe(assign, lhs, rhs, op);
    if (actual == expected)
        return 1;
    fprintf(stderr, "FAIL AST assignment support %s: got %d expected %d\n",
            name, actual, expected);
    return 0;
}

static void check_ast_binary_fold(
    const char *name, int op,
    int left_type, long left_value,
    int right_type, long right_value,
    int expected_ok, long expected_value)
{
    struct AstNode left;
    struct AstNode right;
    struct AstNode binary;
    long value = 0x13579bdfL;
    int ok;

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&binary, 0, sizeof(binary));
    left.kind = AST_INT_LIT;
    left.type = left_type;
    left.ival = left_value;
    right.kind = AST_INT_LIT;
    right.type = right_type;
    right.ival = right_value;
    binary.kind = AST_BINARY;
    binary.op = op;
    binary.a = &left;
    binary.b = &right;

    ok = ast_const_scalar_fold(&binary, &value);
    if (ok != expected_ok ||
        (expected_ok ? value != expected_value : value != 0x13579bdfL)) {
        fprintf(stderr, "FAIL %s: ok=%d value=%ld\n", name, ok, value);
        ++failures;
    }
}

static void check_ast_bool_cast_fold(const char *name, long input)
{
    struct AstNode value;
    struct AstNode cast;
    struct AstNode zero;
    struct AstNode binary;
    volatile long runtime_input = input;
    long expected_value = (_Bool)runtime_input;
    long folded = 0x13579bdfL;
    int ok;

    memset(&value, 0, sizeof(value));
    memset(&cast, 0, sizeof(cast));
    memset(&zero, 0, sizeof(zero));
    memset(&binary, 0, sizeof(binary));
    value.kind = AST_INT_LIT;
    value.type = TYPE_INT;
    value.ival = input;
    cast.kind = AST_CAST;
    cast.type = TYPE_BOOL;
    cast.a = &value;
    zero.kind = AST_INT_LIT;
    zero.type = TYPE_INT;
    binary.kind = AST_BINARY;
    binary.op = '+';
    binary.a = &cast;
    binary.b = &zero;

    ok = ast_const_scalar_fold(&binary, &folded);
    if (!ok || folded != expected_value) {
        fprintf(stderr, "FAIL %s: ok=%d value=%ld\n", name, ok, folded);
        ++failures;
    }
}

static void verify_ast_binary_folds(void)
{
    int int_pointer = type_add_ptr(TYPE_INT);
    int struct_type = TYPE_STRUCT | (1 << STRUCT_SHIFT);

    check_ast_binary_fold("signed int arithmetic shift", TOK_SHR,
                          TYPE_INT, -32768L, TYPE_INT, 15L, 1, -1L);
    check_ast_binary_fold("unsigned int logical shift", TOK_SHR,
                          TYPE_INT | TYPE_UNSIGNED, 0x8000L,
                          TYPE_INT, 15L, 1, 1L);
    check_ast_binary_fold("signed long arithmetic shift", TOK_SHR,
                          TYPE_LONG, (-2147483647L - 1L),
                          TYPE_INT, 31L, 1, -1L);
    check_ast_binary_fold("unsigned wrap", '+',
                          TYPE_INT | TYPE_UNSIGNED, 65535L,
                          TYPE_INT, 1L, 1, 0L);
    check_ast_binary_fold("usual arithmetic conversion", TOK_EQ,
                          TYPE_INT, -1L, TYPE_INT | TYPE_UNSIGNED,
                          65535L, 1, 1L);

    check_ast_binary_fold("negative shift count", TOK_SHL,
                          TYPE_INT, 1L, TYPE_INT, -1L, 0, 0L);
    check_ast_binary_fold("int-width shift count", TOK_SHL,
                          TYPE_INT, 1L, TYPE_INT, 16L, 0, 0L);
    check_ast_binary_fold("long-width shift count", TOK_SHR,
                          TYPE_LONG, 1L, TYPE_INT, 32L, 0, 0L);
    check_ast_binary_fold("division by zero", '/',
                          TYPE_INT, 7L, TYPE_INT, 0L, 0, 0L);
    check_ast_binary_fold("signed division overflow", '/',
                          TYPE_INT, -32768L, TYPE_INT, -1L, 0, 0L);

    check_ast_binary_fold("float left shift operand", TOK_SHL,
                          TYPE_FLOAT, 1L, TYPE_INT, 1L, 0, 0L);
    check_ast_binary_fold("float right shift operand", TOK_SHL,
                          TYPE_INT, 1L, TYPE_FLOAT, 1L, 0, 0L);
    check_ast_binary_fold("pointer arithmetic operand", '+',
                          int_pointer, 1L, TYPE_INT, 1L, 0, 0L);
    check_ast_binary_fold("pointer shift count", TOK_SHL,
                          TYPE_INT, 1L, int_pointer, 1L, 0, 0L);
    check_ast_binary_fold("void arithmetic operand", '+',
                          TYPE_VOID, 1L, TYPE_INT, 1L, 0, 0L);
    check_ast_binary_fold("struct arithmetic operand", '+',
                          struct_type, 1L, TYPE_INT, 1L, 0, 0L);

    check_ast_bool_cast_fold("positive bool cast", 2L);
    check_ast_bool_cast_fold("negative bool cast", -7L);
    check_ast_bool_cast_fold("zero bool cast", 0L);
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
    struct AstNode pointer_rhs;
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
    memset(&pointer_rhs, 0, sizeof(pointer_rhs));
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
        "verify_assignment_pointer_rhs", type_add_ptr(TYPE_INT), SC_GLOBAL);
    pointer_rhs.kind = AST_IDENT;
    pointer_rhs.type = symbol->type;
    pointer_rhs.sval = symbol->name;
    pointer_rhs.sym = symbol;

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
        &assign, &index, &integer, '=');
    ok = expect_ast_assignment_support(
        "pointer array +=", &assign, &index, &integer, TOK_ADDEQ, 1) && ok;
    ok = expect_ast_assignment_support(
        "pointer array -=", &assign, &index, &integer, TOK_SUBEQ, 1) && ok;
    ok = expect_ast_assignment_support(
        "pointer array invalid operator", &assign, &index, &integer,
        TOK_MULEQ, 0) && ok;
    ok = expect_ast_assignment_support(
        "pointer array invalid pointer rhs", &assign, &index, &pointer_rhs,
        TOK_ADDEQ, 0) && ok;
    expr_result_dead = 0;
    ok = expect_ast_assignment_support(
        "pointer array live result", &assign, &index, &integer,
        TOK_ADDEQ, 0) && ok;
    expr_result_dead = 1;
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
        &assign, &index, &integer, '=');
    ok = expect_ast_assignment_support(
        "dereferenced pointer row +=", &assign, &index, &integer,
        TOK_ADDEQ, 1) && ok;
    ok = expect_ast_assignment_support(
        "dereferenced pointer row -=", &assign, &index, &integer,
        TOK_SUBEQ, 1) && ok;
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
        &assign, &index, &integer, '=');
    ok = expect_ast_assignment_support(
        "pointer expression index +=", &assign, &index, &integer,
        TOK_ADDEQ, 1) && ok;
    ok = expect_ast_assignment_support(
        "pointer expression index -=", &assign, &index, &integer,
        TOK_SUBEQ, 1) && ok;
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
        &assign, &index, &integer, '=');
    ok = expect_ast_assignment_support(
        "multidimensional pointer index +=", &assign, &index, &integer,
        TOK_ADDEQ, 1) && ok;
    ok = expect_ast_assignment_support(
        "multidimensional pointer index -=", &assign, &index, &integer,
        TOK_SUBEQ, 1) && ok;
    integer.ival = 3;
    ok = ok && !ast_assignment_probe(
        &assign, &index, &integer, '=');
    expr_result_dead = 0;
    ok = expect_ast_assignment_support(
        "multidimensional pointer index live result", &assign, &index,
        &integer, TOK_ADDEQ, 0) && ok;
    expr_result_dead = 1;

    symbol = add_global(
        "verify_assignment_dereferenced_pointer",
        type_add_ptr(type_add_ptr(TYPE_INT)), SC_GLOBAL);
    lhs.type = symbol->type;
    lhs.sval = symbol->name;
    lhs.sym = symbol;
    dereference.a = &lhs;
    dereference.type = type_add_ptr(TYPE_INT);
    integer.ival = 1;
    ok = expect_ast_assignment_support(
        "dereferenced pointer +=", &assign, &dereference, &integer,
        TOK_ADDEQ, 1) && ok;
    ok = expect_ast_assignment_support(
        "dereferenced pointer -=", &assign, &dereference, &integer,
        TOK_SUBEQ, 1) && ok;
    ok = expect_ast_assignment_support(
        "dereferenced pointer invalid operator", &assign, &dereference,
        &integer, TOK_SHLEQ, 0) && ok;
    ok = expect_ast_assignment_support(
        "dereferenced pointer invalid pointer rhs", &assign, &dereference,
        &pointer_rhs, TOK_SUBEQ, 0) && ok;
    expr_result_dead = 0;
    ok = expect_ast_assignment_support(
        "dereferenced pointer live result", &assign, &dereference, &integer,
        TOK_SUBEQ, 0) && ok;
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
            &assign, &index, &integer, '=');
        ok = expect_ast_assignment_support(
            "member pointer index +=", &assign, &index, &integer,
            TOK_ADDEQ, 1) && ok;
        ok = expect_ast_assignment_support(
            "member pointer index -=", &assign, &index, &integer,
            TOK_SUBEQ, 1) && ok;
        ok = expect_ast_assignment_support(
            "member pointer index invalid rhs", &assign, &index, &pointer_rhs,
            TOK_ADDEQ, 0) && ok;
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

static void verify_call_lowering_preflight(void)
{
    struct AstNode call;
    struct AstNode callee;
    struct AstNode argument;
    struct AstNode indirect_callee[2];
    struct AstNode *arguments[1];
    struct Sym *indirect_symbol;
    int call_id;
    int case_index;
    int instruction;
    int value;

    memset(&call, 0, sizeof(call));
    memset(&callee, 0, sizeof(callee));
    memset(&argument, 0, sizeof(argument));
    memset(indirect_callee, 0, sizeof(indirect_callee));
    call.kind = AST_CALL;
    call.type = TYPE_INT;
    callee.kind = AST_IDENT;
    callee.type = TYPE_INT;
    callee.sval = "verify_missing_callee";
    argument.kind = AST_INT_LIT;
    argument.type = TYPE_INT;
    for (case_index = 0; case_index < 5; ++case_index) {
        callee.sval = case_index == 3 ? NULL : "verify_missing_callee";
        call.a = case_index == 0 ? NULL : &callee;
        call.list_len =
            case_index == 1 || case_index == 2 || case_index == 4 ? 1 : 0;
        call.list_cap = case_index == 1 || case_index == 2 ? 1 : 0;
        arguments[0] = case_index == 4 ? &argument : NULL;
        call.list = case_index == 2 || case_index == 4 ? arguments : NULL;
        mir_begin_function(
            "verify_malformed_call", "_verify_malformed_call",
            EMIT_SINK_FINAL, 0, 0, 0);
        instruction = mir.count;
        value = mir.next_value;
        call_id = mir.next_call_id;
        mir_capture_discarded_expr(&call);
        if (mir.count != instruction + 1 || mir.next_value != value + 1 ||
            mir.next_call_id != call_id ||
            mir.insns[instruction].opcode != MIR_OPAQUE ||
            mir.insns[instruction].dst != value ||
            mir.insns[instruction].type != TYPE_INT ||
            mir.insns[instruction].immediate != AST_CALL) {
            fprintf(stderr,
                    "FAIL malformed call lowering transaction case %d\n",
                    case_index);
            ++failures;
        }
    }

    indirect_callee[0].kind = AST_UNARY;
    indirect_callee[0].op = '*';
    indirect_callee[0].type = type_add_ptr(TYPE_INT);
    indirect_callee[1].kind = AST_UNARY;
    indirect_callee[1].op = '*';
    indirect_callee[1].type = type_add_ptr(TYPE_INT);
    indirect_callee[1].a = &indirect_callee[0];
    call.list_len = 0;
    call.list_cap = 0;
    call.list = NULL;
    for (case_index = 0; case_index < 2; ++case_index) {
        call.a = &indirect_callee[case_index];
        mir_begin_function(
            "verify_malformed_indirect_call",
            "_verify_malformed_indirect_call", EMIT_SINK_FINAL, 0, 0, 0);
        instruction = mir.count;
        value = mir.next_value;
        call_id = mir.next_call_id;
        mir_capture_discarded_expr(&call);
        if (mir.count != instruction + 1 || mir.next_value != value + 1 ||
            mir.next_call_id != call_id ||
            mir.insns[instruction].opcode != MIR_OPAQUE ||
            mir.insns[instruction].dst != value ||
            mir.insns[instruction].type != TYPE_INT ||
            mir.insns[instruction].immediate != AST_CALL) {
            fprintf(stderr,
                    "FAIL malformed indirect call transaction depth %d\n",
                    case_index + 1);
            ++failures;
        }
    }

    indirect_symbol = add_global(
        "verify_indirect_callee", type_add_ptr(TYPE_INT), SC_GLOBAL);
    indirect_symbol->is_funcptr = 1;
    callee.type = indirect_symbol->type;
    callee.sval = indirect_symbol->name;
    callee.sym = indirect_symbol;
    indirect_callee[0].a = &callee;
    for (case_index = 0; case_index < 2; ++case_index) {
        call.a = &indirect_callee[case_index];
        mir_begin_function(
            "verify_valid_indirect_call",
            "_verify_valid_indirect_call", EMIT_SINK_FINAL, 0, 0, 0);
        instruction = mir.count;
        value = mir.next_value;
        call_id = mir.next_call_id;
        mir_capture_discarded_expr(&call);
        if (mir.count != instruction + 2 || mir.next_value != value + 2 ||
            mir.next_call_id != call_id + 1 ||
            mir.insns[instruction].opcode != MIR_LOAD ||
            mir.insns[instruction].dst != value ||
            mir.insns[instruction + 1].opcode != MIR_CALL ||
            mir.insns[instruction + 1].dst != value + 1 ||
            mir.insns[instruction + 1].src1 != value ||
            mir.insns[instruction + 1].secondary_offset != call_id ||
            strcmp(mir.insns[instruction + 1].name, "<indirect>") != 0) {
            fprintf(stderr,
                    "FAIL valid indirect call transaction depth %d\n",
                    case_index + 1);
            ++failures;
        }
    }

    callee.sval = "verify_implicit_call";
    callee.sym = NULL;
    callee.type = TYPE_INT;
    call.a = &callee;
    call.list_len = 0;
    call.list_cap = 0;
    call.list = NULL;
    mir_begin_function(
        "verify_valid_call", "_verify_valid_call",
        EMIT_SINK_FINAL, 0, 0, 0);
    instruction = mir.count;
    value = mir.next_value;
    call_id = mir.next_call_id;
    mir_capture_discarded_expr(&call);
    if (mir.count != instruction + 1 || mir.next_value != value + 1 ||
        mir.next_call_id != call_id + 1 ||
        mir.insns[instruction].opcode != MIR_CALL ||
        mir.insns[instruction].dst != value ||
        mir.insns[instruction].secondary_offset != call_id ||
        strcmp(mir.insns[instruction].name, callee.sval) != 0) {
        fprintf(stderr, "FAIL valid call lowering control\n");
        ++failures;
    }
}

static int expect_malformed_expr_transaction(
    const char *name, const struct AstNode *expr)
{
    int aggregate_temp_bytes;
    int call_id;
    int has_indirect_incdec;
    int inline_temp_id;
    int instruction;
    int label;
    int value;

    mir_begin_function(name, name, EMIT_SINK_FINAL, 0, 0, 0);
    instruction = mir.count;
    value = mir.next_value;
    label = mir.next_label;
    call_id = mir.next_call_id;
    inline_temp_id = mir.next_inline_temp_id;
    aggregate_temp_bytes = mir.aggregate_temp_bytes;
    has_indirect_incdec = mir.has_indirect_incdec;
    mir_capture_discarded_expr(expr);
    if (mir.count != instruction + 1 || mir.next_value != value + 1 ||
        mir.next_label != label || mir.next_call_id != call_id ||
        mir.next_inline_temp_id != inline_temp_id ||
        mir.aggregate_temp_bytes != aggregate_temp_bytes ||
        mir.has_indirect_incdec != has_indirect_incdec ||
        mir.insns[instruction].opcode != MIR_OPAQUE ||
        mir.insns[instruction].dst != value ||
        mir.insns[instruction].type != expr->type ||
        mir.insns[instruction].immediate != expr->kind) {
        fprintf(stderr, "FAIL malformed expression transaction %s\n", name);
        return 0;
    }
    return 1;
}

static int expect_valid_expr_lowering(
    const char *name, const struct AstNode *expr, int expected_instructions,
    int expected_values, int expected_calls)
{
    int first_instruction;
    int instruction;
    int label;
    int call_id;
    int value;

    mir_begin_function(name, name, EMIT_SINK_FINAL, 0, 0, 0);
    first_instruction = mir.count;
    label = mir.next_label;
    call_id = mir.next_call_id;
    value = mir.next_value;
    mir_capture_discarded_expr(expr);
    if (mir.count != first_instruction + expected_instructions ||
        mir.next_value != value + expected_values ||
        mir.next_label != label ||
        mir.next_call_id != call_id + expected_calls) {
        fprintf(stderr,
                "FAIL valid expression lowering %s"
                " count=%d/%d values=%d/%d labels=%d/%d calls=%d/%d\n",
                name, mir.count - first_instruction, expected_instructions,
                mir.next_value - value, expected_values,
                mir.next_label, label,
                mir.next_call_id - call_id, expected_calls);
        return 0;
    }
    for (instruction = first_instruction; instruction < mir.count;
         ++instruction)
        if (mir.insns[instruction].opcode == MIR_OPAQUE) {
            fprintf(stderr, "FAIL unexpected opaque expression %s\n", name);
            return 0;
        }
    return 1;
}

static void verify_large_expression_preflight(void)
{
    struct AstNode argument;
    struct AstNode call;
    struct AstNode callee;
    struct AstNode *chain;
    struct AstNode **arguments;
    int argument_count;
    int depth;
    int index;
    int ok = 1;
    static const int depths[] = {256, 257, 4096};

    chain = (struct AstNode *)xmalloc(4097 * sizeof(*chain));
    memset(chain, 0, 4097 * sizeof(*chain));
    chain[4096].kind = AST_INT_LIT;
    chain[4096].type = TYPE_INT;
    chain[4096].ival = 1;
    for (index = 4095; index >= 0; --index) {
        chain[index].kind = AST_UNARY;
        chain[index].type = TYPE_INT;
        chain[index].op = '!';
        chain[index].a = &chain[index + 1];
    }
    for (index = 0; index < 3; ++index) {
        depth = depths[index];
        ok = expect_valid_expr_lowering(
            depth == 256 ? "verify_unary_depth_256" :
            depth == 257 ? "verify_unary_depth_257" :
                           "verify_unary_depth_4096",
            &chain[4096 - depth], depth + 1, depth + 1, 0) && ok;
    }
    free(chain);

    memset(&argument, 0, sizeof(argument));
    memset(&call, 0, sizeof(call));
    memset(&callee, 0, sizeof(callee));
    argument.kind = AST_INT_LIT;
    argument.type = TYPE_INT;
    argument.ival = 1;
    callee.kind = AST_IDENT;
    callee.type = TYPE_INT;
    callee.sval = "verify_large_variadic_call";
    call.kind = AST_CALL;
    call.type = TYPE_INT;
    call.a = &callee;
    arguments = (struct AstNode **)xmalloc(4097 * sizeof(*arguments));
    for (index = 0; index < 4097; ++index)
        arguments[index] = &argument;
    call.list = arguments;
    for (argument_count = 4096; argument_count <= 4097; ++argument_count) {
        call.list_len = argument_count;
        call.list_cap = argument_count;
        ok = expect_valid_expr_lowering(
            argument_count == 4096 ? "verify_call_arguments_4096" :
                                     "verify_call_arguments_4097",
            &call, argument_count * 2 + 1, argument_count + 1, 1) && ok;
    }
    free(arguments);

    if (!ok)
        ++failures;
}

static int check_opaque_call_transaction(
    const char *name, int instruction, int value, int label, int call_id,
    int inline_temp_id, int aggregate_temp_bytes, int root_kind,
    int has_return)
{
    int expected_count = instruction + 1 + has_return;

    if (mir.count != expected_count || mir.next_value != value + 1 ||
        mir.next_label != label || mir.next_call_id != call_id ||
        mir.next_inline_temp_id != inline_temp_id ||
        mir.aggregate_temp_bytes != aggregate_temp_bytes ||
        mir.insns[instruction].opcode != MIR_OPAQUE ||
        mir.insns[instruction].dst != value ||
        mir.insns[instruction].immediate != root_kind ||
        (has_return &&
         (mir.insns[instruction + 1].opcode != MIR_RETURN ||
          mir.insns[instruction + 1].src1 != value))) {
        fprintf(stderr, "FAIL call fast-path transaction %s\n", name);
        return 0;
    }
    return 1;
}

static void verify_call_fast_path_preflight(void)
{
    struct AstNode aggregate_call;
    struct AstNode aggregate_callee;
    struct AstNode assignment;
    struct AstNode inline_argument;
    struct AstNode inline_body_call;
    struct AstNode inline_body_callee;
    struct AstNode inline_call;
    struct AstNode inline_callee;
    struct AstNode lhs;
    struct AstNode return_stmt;
    struct AstNode *inline_arguments[1];
    struct Sym *aggregate_function;
    struct Sym *inline_function;
    struct Sym *target;
    int aggregate_temp_bytes;
    int call_id;
    int inline_temp_id;
    int instruction;
    int label;
    int struct_type = TYPE_STRUCT | (3 << STRUCT_SHIFT);
    int value;
    int ok = 1;

    memset(&aggregate_call, 0, sizeof(aggregate_call));
    memset(&aggregate_callee, 0, sizeof(aggregate_callee));
    memset(&assignment, 0, sizeof(assignment));
    memset(&inline_argument, 0, sizeof(inline_argument));
    memset(&inline_body_call, 0, sizeof(inline_body_call));
    memset(&inline_body_callee, 0, sizeof(inline_body_callee));
    memset(&inline_call, 0, sizeof(inline_call));
    memset(&inline_callee, 0, sizeof(inline_callee));
    memset(&lhs, 0, sizeof(lhs));
    memset(&return_stmt, 0, sizeof(return_stmt));

    aggregate_function = add_global(
        "verify_aggregate_preflight", struct_type, SC_FUNC);
    aggregate_callee.kind = AST_IDENT;
    aggregate_callee.type = struct_type;
    aggregate_callee.sval = aggregate_function->name;
    aggregate_callee.sym = aggregate_function;
    aggregate_call.kind = AST_CALL;
    aggregate_call.type = struct_type;
    aggregate_call.a = &aggregate_callee;
    aggregate_call.list_len = 1;
    aggregate_call.list_cap = 1;
    aggregate_call.list = NULL;
    target = add_global("verify_aggregate_target", struct_type, SC_GLOBAL);

    mir_begin_function(
        "verify_aggregate_initializer_preflight",
        "_verify_aggregate_initializer_preflight",
        EMIT_SINK_FINAL, 0, 0, 0);
    instruction = mir.count;
    value = mir.next_value;
    label = mir.next_label;
    call_id = mir.next_call_id;
    inline_temp_id = mir.next_inline_temp_id;
    aggregate_temp_bytes = mir.aggregate_temp_bytes;
    mir_capture_struct_initializer(target, &aggregate_call);
    ok = check_opaque_call_transaction(
        "aggregate initializer", instruction, value, label, call_id,
        inline_temp_id, aggregate_temp_bytes, AST_CALL, 0) && ok;

    return_stmt.kind = AST_RETURN;
    return_stmt.a = &aggregate_call;
    mir_begin_function(
        "verify_aggregate_return_preflight",
        "_verify_aggregate_return_preflight",
        EMIT_SINK_FINAL, 0, 0, 0);
    mir.return_type = struct_type;
    instruction = mir.count;
    value = mir.next_value;
    label = mir.next_label;
    call_id = mir.next_call_id;
    inline_temp_id = mir.next_inline_temp_id;
    aggregate_temp_bytes = mir.aggregate_temp_bytes;
    mir_capture_stmt(&return_stmt);
    ok = check_opaque_call_transaction(
        "aggregate return", instruction, value, label, call_id,
        inline_temp_id, aggregate_temp_bytes, AST_CALL, 1) && ok;

    lhs.kind = AST_IDENT;
    lhs.type = struct_type;
    lhs.sval = target->name;
    lhs.sym = target;
    assignment.kind = AST_ASSIGN;
    assignment.type = struct_type;
    assignment.op = '=';
    assignment.a = &lhs;
    assignment.b = &aggregate_call;
    ok = expect_malformed_expr_transaction(
        "verify_aggregate_assignment_preflight", &assignment) && ok;

    inline_body_callee.kind = AST_IDENT;
    inline_body_callee.type = TYPE_VOID;
    inline_body_callee.sval = "verify_inline_nested_call";
    inline_body_call.kind = AST_CALL;
    inline_body_call.type = TYPE_VOID;
    inline_body_call.a = &inline_body_callee;
    inline_function = add_global(
        "verify_inline_stmt_preflight", TYPE_VOID, SC_FUNC);
    inline_function->is_static = 1;
    inline_function->is_inline = 1;
    inline_function->has_proto = 1;
    inline_function->proto_nargs = 1;
    inline_function->proto_types[0] = TYPE_INT;
    inline_function->inline_stmt_expr = &inline_body_call;
    inline_callee.kind = AST_IDENT;
    inline_callee.type = TYPE_VOID;
    inline_callee.sval = inline_function->name;
    inline_callee.sym = inline_function;
    inline_call.kind = AST_CALL;
    inline_call.type = TYPE_VOID;
    inline_call.a = &inline_callee;
    inline_call.list_len = 1;
    inline_call.list_cap = 1;
    inline_call.list = NULL;
    return_stmt.kind = AST_EXPR_STMT;
    return_stmt.a = &inline_call;

    mir_begin_function(
        "verify_inline_list_preflight", "_verify_inline_list_preflight",
        EMIT_SINK_FINAL, 0, 0, 0);
    instruction = mir.count;
    value = mir.next_value;
    label = mir.next_label;
    call_id = mir.next_call_id;
    inline_temp_id = mir.next_inline_temp_id;
    aggregate_temp_bytes = mir.aggregate_temp_bytes;
    mir_capture_stmt(&return_stmt);
    ok = check_opaque_call_transaction(
        "inline corrupt list", instruction, value, label, call_id,
        inline_temp_id, aggregate_temp_bytes, AST_CALL, 0) && ok;

    inline_argument.kind = AST_UNARY;
    inline_argument.type = TYPE_INT;
    inline_argument.op = '-';
    inline_argument.a = &inline_argument;
    inline_arguments[0] = &inline_argument;
    inline_call.list = inline_arguments;
    mir_begin_function(
        "verify_inline_cycle_preflight", "_verify_inline_cycle_preflight",
        EMIT_SINK_FINAL, 0, 0, 0);
    instruction = mir.count;
    value = mir.next_value;
    label = mir.next_label;
    call_id = mir.next_call_id;
    inline_temp_id = mir.next_inline_temp_id;
    aggregate_temp_bytes = mir.aggregate_temp_bytes;
    mir_capture_stmt(&return_stmt);
    ok = check_opaque_call_transaction(
        "inline cyclic argument", instruction, value, label, call_id,
        inline_temp_id, aggregate_temp_bytes, AST_CALL, 0) && ok;

    if (!ok)
        ++failures;
}

static void verify_expression_lowering_preflight(void)
{
    struct AstNode argument;
    struct AstNode address;
    struct AstNode assign;
    struct AstNode base;
    struct AstNode binary;
    struct AstNode call;
    struct AstNode cast;
    struct AstNode callee;
    struct AstNode compound_literal;
    struct AstNode conditional;
    struct AstNode index;
    struct AstNode indirect_cycle[2];
    struct AstNode logical;
    struct AstNode member;
    struct AstNode operand;
    struct AstNode postfix;
    struct AstNode rhs;
    struct AstNode sizeof_expr;
    struct AstNode unary;
    struct AstNode *arguments[1];
    struct Sym *symbol;
    struct Sym *register_symbol;
    int errors_before;
    int ok = 1;

    memset(&argument, 0, sizeof(argument));
    memset(&address, 0, sizeof(address));
    memset(&assign, 0, sizeof(assign));
    memset(&base, 0, sizeof(base));
    memset(&binary, 0, sizeof(binary));
    memset(&call, 0, sizeof(call));
    memset(&cast, 0, sizeof(cast));
    memset(&callee, 0, sizeof(callee));
    memset(&compound_literal, 0, sizeof(compound_literal));
    memset(&conditional, 0, sizeof(conditional));
    memset(&index, 0, sizeof(index));
    memset(indirect_cycle, 0, sizeof(indirect_cycle));
    memset(&logical, 0, sizeof(logical));
    memset(&member, 0, sizeof(member));
    memset(&operand, 0, sizeof(operand));
    memset(&postfix, 0, sizeof(postfix));
    memset(&rhs, 0, sizeof(rhs));
    memset(&sizeof_expr, 0, sizeof(sizeof_expr));
    memset(&unary, 0, sizeof(unary));

    symbol = add_global("verify_expr_operand", TYPE_INT, SC_GLOBAL);
    operand.kind = AST_IDENT;
    operand.type = TYPE_INT;
    operand.sval = symbol->name;
    operand.sym = symbol;
    rhs.kind = AST_INT_LIT;
    rhs.type = TYPE_INT;
    rhs.ival = 1;

    unary.kind = AST_UNARY;
    unary.type = TYPE_INT;
    unary.op = '-';
    ok = expect_malformed_expr_transaction(
        "verify_missing_unary_operand", &unary) && ok;
    unary.a = &unary;
    ok = expect_malformed_expr_transaction(
        "verify_cyclic_unary_operand", &unary) && ok;
    unary.a = NULL;

    binary.kind = AST_BINARY;
    binary.type = TYPE_INT;
    binary.op = '?';
    binary.a = &operand;
    binary.b = &rhs;
    ok = expect_malformed_expr_transaction(
        "verify_invalid_binary_operator", &binary) && ok;

    postfix.kind = AST_POSTFIX;
    postfix.type = TYPE_INT;
    postfix.op = '?';
    postfix.a = &operand;
    ok = expect_malformed_expr_transaction(
        "verify_invalid_postfix_operator", &postfix) && ok;

    base = operand;
    member.kind = AST_MEMBER;
    member.type = TYPE_INT;
    member.op = '?';
    member.a = &base;
    member.sval = "missing";
    ok = expect_malformed_expr_transaction(
        "verify_invalid_member_operator", &member) && ok;

    assign.kind = AST_ASSIGN;
    assign.type = TYPE_INT;
    assign.op = '?';
    assign.a = &member;
    assign.b = &rhs;
    ok = expect_malformed_expr_transaction(
        "verify_invalid_assignment_operator", &assign) && ok;

    index.kind = AST_INDEX;
    index.type = TYPE_INT;
    index.a = &operand;
    ok = expect_malformed_expr_transaction(
        "verify_missing_index_operand", &index) && ok;

    logical.kind = AST_LOGAND;
    logical.type = TYPE_INT;
    logical.a = &operand;
    ok = expect_malformed_expr_transaction(
        "verify_missing_logical_operand", &logical) && ok;

    conditional.kind = AST_COND;
    conditional.type = TYPE_INT;
    conditional.a = &operand;
    conditional.b = &rhs;
    ok = expect_malformed_expr_transaction(
        "verify_missing_conditional_operand", &conditional) && ok;

    cast.kind = AST_CAST;
    cast.type = TYPE_INT;
    ok = expect_malformed_expr_transaction(
        "verify_missing_cast_operand", &cast) && ok;

    compound_literal.kind = AST_COMPOUND_LITERAL;
    compound_literal.type = TYPE_INT;
    ok = expect_malformed_expr_transaction(
        "verify_missing_compound_object", &compound_literal) && ok;

    callee.kind = AST_IDENT;
    callee.type = TYPE_INT;
    callee.sval = "verify_malformed_argument";
    argument.kind = AST_UNARY;
    argument.type = TYPE_INT;
    argument.op = '-';
    arguments[0] = &argument;
    call.kind = AST_CALL;
    call.type = TYPE_INT;
    call.a = &callee;
    call.list = arguments;
    call.list_len = 1;
    call.list_cap = 1;
    ok = expect_malformed_expr_transaction(
        "verify_malformed_call_argument", &call) && ok;

    register_symbol = add_global(
        "verify_register_argument", TYPE_INT, SC_GLOBAL);
    register_symbol->is_register = 1;
    operand.sval = register_symbol->name;
    operand.sym = register_symbol;
    address.kind = AST_UNARY;
    address.type = type_add_ptr(TYPE_INT);
    address.op = '&';
    address.a = &operand;
    arguments[0] = &address;
    errors_before = g_diag_error_count;
    ok = expect_malformed_expr_transaction(
        "verify_register_address_argument", &call) && ok;
    if (g_diag_error_count != errors_before + 1) {
        fprintf(stderr, "FAIL register address diagnostic count\n");
        ok = 0;
    }
    g_diag_error_count = errors_before;

    call.list = NULL;
    sizeof_expr.kind = AST_SIZEOF_EXPR;
    sizeof_expr.type = TYPE_INT;
    sizeof_expr.a = &call;
    ok = expect_malformed_expr_transaction(
        "verify_malformed_sizeof_operand", &sizeof_expr) && ok;

    indirect_cycle[0].kind = AST_UNARY;
    indirect_cycle[0].type = type_add_ptr(TYPE_INT);
    indirect_cycle[0].op = '*';
    indirect_cycle[0].a = &indirect_cycle[1];
    indirect_cycle[1].kind = AST_UNARY;
    indirect_cycle[1].type = type_add_ptr(TYPE_INT);
    indirect_cycle[1].op = '*';
    indirect_cycle[1].a = &indirect_cycle[0];
    call.a = &indirect_cycle[0];
    call.list_len = 0;
    call.list_cap = 0;
    call.list = NULL;
    ok = expect_malformed_expr_transaction(
        "verify_cyclic_indirect_callee", &call) && ok;

    rhs.list_len = 1;
    rhs.list_cap = 1;
    rhs.list = NULL;
    ok = expect_malformed_expr_transaction(
        "verify_noncall_null_list", &rhs) && ok;
    rhs.list_len = 0;
    ok = expect_malformed_expr_transaction(
        "verify_noncall_capacity_without_list", &rhs) && ok;
    rhs.list_cap = 0;
    rhs.list = arguments;
    ok = expect_malformed_expr_transaction(
        "verify_noncall_list_without_capacity", &rhs) && ok;
    rhs.list = NULL;

    call.a = &callee;
    call.list_cap = 1;
    ok = expect_malformed_expr_transaction(
        "verify_call_capacity_without_list", &call) && ok;
    call.list_cap = 0;
    call.list = arguments;
    ok = expect_malformed_expr_transaction(
        "verify_call_list_without_capacity", &call) && ok;

    if (!ok)
        ++failures;
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

static int mir_stream_seek_rejected_without_moving(
    MirStream *stream, long offset, int whence)
{
    long position = mir_stream_tell(stream);
    int result = mir_stream_seek(stream, offset, whence);
    long after = mir_stream_tell(stream);

    if (after != position)
        mir_stream_seek(stream, position, SEEK_SET);
    return result != 0 && after == position;
}

static void verify_mir_stream_seek_transaction(void)
{
    MirStream *control = mir_stream_open();
    MirStream *retry = mir_stream_open();
    char control_text[4];
    char retry_text[4];
    size_t control_bytes;
    size_t retry_bytes;
    int ok = 1;

    if (control == NULL || retry == NULL) {
        fprintf(stderr, "FAIL MIR stream seek transaction allocation\n");
        ++failures;
        mir_stream_close(control);
        mir_stream_close(retry);
        return;
    }
    mir_stream_puts("abc", control);
    mir_stream_puts("abc", retry);
    ok = ok && mir_stream_seek(control, 1, SEEK_SET) == 0;
    ok = ok && mir_stream_seek(retry, 1, SEEK_SET) == 0;
    ok = ok && mir_stream_seek_rejected_without_moving(
        retry, 4, SEEK_SET);
    ok = ok && mir_stream_seek_rejected_without_moving(
        retry, 3, SEEK_CUR);
    ok = ok && mir_stream_seek_rejected_without_moving(
        retry, 1, SEEK_END);
    ok = ok && mir_stream_seek_rejected_without_moving(
        retry, LONG_MAX, SEEK_CUR);
    ok = ok && mir_stream_seek_rejected_without_moving(
        retry, LONG_MIN, SEEK_END);
    ok = ok && mir_stream_seek(control, 1, SEEK_CUR) == 0;
    ok = ok && mir_stream_seek(retry, 1, SEEK_CUR) == 0;
    ok = ok && mir_stream_putc('Z', control) == 'Z';
    ok = ok && mir_stream_putc('Z', retry) == 'Z';
    ok = ok && mir_stream_tell(control) == 3;
    ok = ok && mir_stream_tell(retry) == 3;
    mir_stream_rewind(control);
    mir_stream_rewind(retry);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    retry_bytes = mir_stream_read(
        retry_text, 1, sizeof(retry_text), retry);
    ok = ok && control_bytes == 3 && retry_bytes == 3;
    ok = ok && memcmp(control_text, "abZ", 3) == 0;
    ok = ok && memcmp(control_text, retry_text, 3) == 0;
    if (!ok) {
        fprintf(stderr, "FAIL MIR stream rejected seek transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    mir_stream_close(retry);
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

static void setup_deferred_function_pointer_call(int malformed)
{
    struct Sym callback;
    struct Sym later;

    setup(9, 2, 1);
    mir.next_call_id = 1;
    memset(&callback, 0, sizeof(callback));
    strcpy(callback.name, "verify_deferred_callback");
    callback.type = TYPE_INT | TYPE_PTR;
    callback.storage = SC_LOCAL;
    callback.offset = -2;
    callback.is_funcptr = 1;
    callback.has_proto = 1;
    callback.proto_nargs = 1;
    callback.proto_types[0] = TYPE_LONG;
    callback.funcptr_return_type = TYPE_INT;
    mir_note_declared_symbol(&callback);
    memset(&later, 0, sizeof(later));
    strcpy(later.name, "verify_later_declaration");
    later.type = TYPE_INT;
    later.storage = SC_LOCAL;
    later.offset = -4;
    mir_note_declared_symbol(&later);

    mir.declaration_count = 2;
    mir.declaration_placeholders[0] = 2;
    mir.declaration_scope_ends[0] = 8;
    mir.declaration_scope_labels[0] = -1;
    mir.declaration_placeholders[1] = 6;
    mir.declaration_scope_ends[1] = 8;
    mir.declaration_scope_labels[1] = -1;
    mir.insns[2].opcode = MIR_DECL_PLACEHOLDER;
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    mir.insns[3].type = TYPE_INT;
    mir.insns[3].secondary_offset = 0;
    mir.insns[5].opcode = MIR_CALL;
    mir.insns[5].dst = 1;
    mir.insns[5].type = TYPE_INT;
    mir.insns[5].secondary_offset = 0;
    if (malformed)
        strcpy(mir.insns[5].name, "<indirect>");
    else
        strcpy(mir.insns[5].name, callback.name);
    mir.insns[6].opcode = MIR_DECL_PLACEHOLDER;
    mir.insns[8].src1 = 1;

    mir.debug_events = (struct MirDebugEvent *)calloc(
        1, sizeof(*mir.debug_events));
    if (mir.debug_events == NULL)
        fatal("cannot allocate deferred metadata debug event");
    mir.debug_events[0].text = (char *)malloc(20);
    if (mir.debug_events[0].text == NULL)
        fatal("cannot allocate deferred metadata debug text");
    strcpy(mir.debug_events[0].text, ";@dcc-line \"x\" 1\n");
    mir.debug_events[0].point = 5;
    mir.debug_event_count = 1;
    mir.debug_event_capacity = 1;
}

static void add_deferred_debug_event(int point, const char *text)
{
    struct MirDebugEvent *events;
    size_t length = strlen(text) + 1;

    events = (struct MirDebugEvent *)realloc(
        mir.debug_events,
        (size_t)(mir.debug_event_count + 1) * sizeof(*mir.debug_events));
    if (events == NULL)
        fatal("cannot grow deferred metadata debug events");
    mir.debug_events = events;
    mir.debug_event_capacity = mir.debug_event_count + 1;
    mir.debug_events[mir.debug_event_count].text = (char *)malloc(length);
    if (mir.debug_events[mir.debug_event_count].text == NULL)
        fatal("cannot allocate deferred metadata debug text");
    memcpy(mir.debug_events[mir.debug_event_count].text, text, length);
    mir.debug_events[mir.debug_event_count].point = point;
    ++mir.debug_event_count;
}

static void check_deferred_function_pointer_result(const char *name)
{
    int ok = 1;
    int verified;

    ok = ok && mir.count == 11 && mir.next_value == 4;
    ok = ok && mir.declaration_placeholders[0] == 2;
    ok = ok && mir.declaration_placeholders[1] == 8;
    ok = ok && mir.declaration_scope_ends[0] == 10;
    ok = ok && mir.declaration_scope_ends[1] == 10;
    ok = ok && mir.debug_event_count == 1;
    ok = ok && mir.debug_events[0].point == 7;
    ok = ok && mir.insns[3].opcode == MIR_CONST;
    ok = ok && mir.insns[3].src1 == -1 && mir.insns[3].type == TYPE_LONG;
    ok = ok && mir.insns[4].opcode == MIR_ARG;
    ok = ok && mir.insns[4].src1 == mir.insns[3].dst;
    ok = ok && mir.insns[6].opcode == MIR_LOAD;
    ok = ok && mir.insns[6].type == (TYPE_INT | TYPE_PTR);
    ok = ok && !strcmp(mir.insns[6].name, "verify_deferred_callback");
    ok = ok && mir.insns[7].opcode == MIR_CALL;
    ok = ok && mir.insns[7].src1 == mir.insns[6].dst;
    ok = ok && mir.insns[7].type == TYPE_INT;
    ok = ok && !strcmp(mir.insns[7].name, "<indirect>");
    verified = mir_verify_and_dump();
    ok = ok && verified;
    if (!ok) {
        fprintf(stderr, "FAIL %s\n", name);
        ++failures;
    }
    clear_liveness();
}

static void verify_deferred_function_pointer_metadata(void)
{
    setup_deferred_function_pointer_call(0);
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL deferred function-pointer verification control\n");
        ++failures;
    }
    clear_liveness();
    mir_resolve_deferred_metadata();
    check_deferred_function_pointer_result(
        "deferred function-pointer metadata insertion");
    mir_resolve_deferred_metadata();
    check_deferred_function_pointer_result(
        "deferred function-pointer metadata repeat");

    setup_deferred_function_pointer_call(1);
    expect_verification("malformed deferred function-pointer call", 0);
    mir_resolve_deferred_metadata();
    if (mir.count != 9 || mir.next_value != 2 ||
        mir.declaration_placeholders[0] != 2 ||
        mir.declaration_placeholders[1] != 6 ||
        mir.declaration_scope_ends[0] != 8 ||
        mir.declaration_scope_ends[1] != 8 ||
        mir.debug_events[0].point != 5) {
        fprintf(stderr, "FAIL malformed deferred metadata changed MIR state\n");
        ++failures;
    }
    strcpy(mir.insns[5].name, "verify_deferred_callback");
    mir_resolve_deferred_metadata();
    check_deferred_function_pointer_result(
        "deferred metadata retry after malformed call");
}

static void setup_deferred_direct_call_conversion(struct Sym *callee)
{
    struct Sym local;

    setup(6, 2, 1);
    mir.next_call_id = 1;
    mir.local_bytes = 4;
    memset(&local, 0, sizeof(local));
    strcpy(local.name, "verify_deferred_wide");
    local.type = TYPE_LONG;
    local.storage = SC_LOCAL;
    local.offset = -4;
    local.size = 4;
    mir_note_declared_symbol(&local);
    mir.object_count = 1;
    strcpy(mir.objects[0].name, local.name);
    mir.objects[0].type = local.type;
    mir.objects[0].storage = local.storage;
    mir.objects[0].offset = local.offset;

    mir.insns[1].opcode = MIR_LOAD;
    mir.insns[1].dst = 0;
    mir.insns[1].type = TYPE_INT;
    mir.insns[1].object = 0;
    strcpy(mir.insns[1].name, local.name);
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_INT;
    mir.insns[3].opcode = MIR_CALL;
    mir.insns[3].dst = 1;
    mir.insns[3].type = TYPE_INT;
    strcpy(mir.insns[3].name, callee->name);
    mir.insns[5].src1 = 1;
    add_deferred_debug_event(3, ";@dcc-line \"x\" 1\n");
}

static void verify_deferred_direct_call_conversion(void)
{
    struct MirInsn argument;
    struct MirInsn call;
    struct Sym *callee;
    int saved_debug = opt_debug;
    int ok = 1;

    opt_debug = 1;
    callee = add_global("verify_deferred_direct_call", TYPE_INT, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 1;
    callee->proto_types[0] = TYPE_INT;
    setup_deferred_direct_call_conversion(callee);

    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[1].opcode == MIR_LOAD;
    ok = ok && mir.insns[1].type == TYPE_LONG;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[2].src1 == 0;
    ok = ok && mir.insns[2].dst == 2;
    ok = ok && mir.insns[2].type == TYPE_INT;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[3].src1 == 2;
    ok = ok && mir.insns[3].type == TYPE_INT;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    ok = ok && mir.insns[4].secondary_offset == 0;
    ok = ok && mir.insns[6].opcode == MIR_RETURN;
    ok = ok && mir.insns[6].src1 == 1;
    ok = ok && mir.debug_events[0].point == 4;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir_verify_and_dump();
    if (!ok) {
        fprintf(stderr, "FAIL deferred direct-call conversion repair\n");
        ++failures;
    }
    clear_liveness();

    setup_deferred_direct_call_conversion(callee);
    argument = mir.insns[2];
    mir.insns[2].opcode = MIR_NOP;
    mir.insns[2].src1 = -1;
    mir.insns[4] = argument;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[3].opcode == MIR_CALL;
    ok = ok && mir.insns[4].opcode == MIR_ARG;
    ok = ok && mir.debug_events[0].point == 3;
    mir.insns[2] = argument;
    mir.insns[4].opcode = MIR_NOP;
    mir.insns[4].src1 = -1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 4;
    if (!ok) {
        fprintf(stderr,
                "FAIL malformed deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    mir.insns[4] = mir.insns[3];
    mir.insns[3] = mir.insns[2];
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 3;
    mir.insns[3].opcode = MIR_NOP;
    mir.insns[3].src1 = -1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[5].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 4;
    if (!ok) {
        fprintf(stderr,
                "FAIL duplicate deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    mir.insns[2].immediate = 1;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.debug_events[0].point == 3;
    mir.insns[2].immediate = 0;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 4;
    if (!ok) {
        fprintf(stderr,
                "FAIL sparse deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    mir.insns[4] = mir.insns[3];
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[3].opcode == MIR_CALL;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 3;
    mir.insns[4].opcode = MIR_NOP;
    mir.insns[4].dst = -1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 4;
    if (!ok) {
        fprintf(stderr,
                "FAIL repeated-ID deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    mir.insns[2].immediate = -1;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.debug_events[0].point == 3;
    mir.insns[2].immediate = 0;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    if (!ok) {
        fprintf(stderr,
                "FAIL negative-position deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    callee->proto_nargs = 2;
    callee->proto_types[1] = TYPE_LONG;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.debug_events[0].point == 3;
    callee->proto_nargs = 1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    if (!ok) {
        fprintf(stderr,
                "FAIL short fixed deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    argument = mir.insns[2];
    call = mir.insns[3];
    mir.insns[3] = argument;
    mir.insns[3].immediate = 1;
    mir.insns[4] = call;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 3;
    callee->proto_variadic = 1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[3].type == TYPE_INT;
    ok = ok && mir.insns[4].opcode == MIR_ARG;
    ok = ok && mir.insns[4].type == TYPE_LONG;
    ok = ok && mir.insns[5].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 4;
    if (!ok) {
        fprintf(stderr,
                "FAIL fixed/variadic deferred direct-call transaction\n");
        ++failures;
    }
    callee->proto_variadic = 0;

    setup_deferred_direct_call_conversion(callee);
    mir.insns[3].src1 = 0;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    mir.insns[3].src1 = -1;
    mir.insns[3].src2 = 0;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    mir.insns[3].src2 = -1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    if (!ok) {
        fprintf(stderr,
                "FAIL sourced deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    callee->storage = SC_GLOBAL;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    callee->storage = SC_FUNC;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    if (!ok) {
        fprintf(stderr,
                "FAIL non-function deferred direct-call transaction\n");
        ++failures;
    }

    opt_debug = 0;
    setup_deferred_direct_call_conversion(callee);
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[1].type == TYPE_LONG;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.insns[2].src1 == 0;
    ok = ok && mir.insns[2].type == TYPE_INT;
    ok = ok && mir.insns[3].opcode == MIR_CALL;
    ok = ok && mir.debug_events[0].point == 3;
    if (!ok) {
        fprintf(stderr, "FAIL release deferred direct-call gating\n");
        ++failures;
    }

    opt_debug = 1;
    setup_deferred_direct_call_conversion(callee);
    mir.insns[2].secondary_offset = -1;
    mir.insns[3].secondary_offset = -1;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.debug_events[0].point == 3;
    mir.insns[2].secondary_offset = 0;
    mir.insns[3].secondary_offset = 0;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    if (!ok) {
        fprintf(stderr,
                "FAIL negative-ID deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    callee->proto_nargs = -1;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    callee->proto_nargs = MAX_PROTO_PARAMS + 1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    callee->proto_nargs = 1;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].opcode == MIR_ARG;
    ok = ok && mir.insns[4].opcode == MIR_CALL;
    if (!ok) {
        fprintf(stderr,
                "FAIL prototype-bound deferred direct-call transaction\n");
        ++failures;
    }

    setup_deferred_direct_call_conversion(callee);
    callee->has_proto = 0;
    callee->proto_nargs = 0;
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 2;
    ok = ok && mir.insns[1].type == TYPE_LONG;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.insns[2].src1 == 0;
    ok = ok && mir.insns[2].type == TYPE_LONG;
    ok = ok && mir.insns[3].opcode == MIR_CALL;
    if (!ok) {
        fprintf(stderr,
                "FAIL unprototyped deferred direct-call preservation\n");
        ++failures;
    }
    callee->has_proto = 1;
    callee->proto_nargs = 1;

    setup_deferred_direct_call_conversion(callee);
    argument = mir.insns[2];
    call = mir.insns[3];
    mir.insns[2].immediate = 1;
    mir.insns[3] = argument;
    mir.insns[4] = call;
    callee->proto_nargs = 2;
    callee->proto_types[1] = TYPE_LONG;
    mir_resolve_deferred_metadata();
    ok = mir.count == 7 && mir.next_value == 3;
    ok = ok && mir.insns[2].opcode == MIR_ARG;
    ok = ok && mir.insns[2].immediate == 1;
    ok = ok && mir.insns[2].type == TYPE_LONG;
    ok = ok && mir.insns[3].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].src1 == 0;
    ok = ok && mir.insns[3].type == TYPE_INT;
    ok = ok && mir.insns[4].opcode == MIR_ARG;
    ok = ok && mir.insns[4].immediate == 0;
    ok = ok && mir.insns[4].src1 == mir.insns[3].dst;
    ok = ok && mir.insns[4].type == TYPE_INT;
    ok = ok && mir.insns[5].opcode == MIR_CALL;
    ok = ok && mir_verify_and_dump();
    if (!ok) {
        fprintf(stderr,
                "FAIL reverse-order deferred direct-call repair\n");
        ++failures;
    }
    clear_liveness();
    callee->proto_nargs = 1;
    opt_debug = saved_debug;
}

static void setup_deferred_binary_conversion(struct Sym *local,
                                             int narrow_on_right)
{
    setup(5, 3, 1);
    memset(local, 0, sizeof(*local));
    strcpy(local->name, "verify_deferred_narrow");
    local->type = TYPE_INT;
    local->storage = SC_LOCAL;
    local->offset = -2;
    local->size = 2;
    mir_note_declared_symbol(local);
    mir.insns[1].opcode = narrow_on_right ? MIR_CONST : MIR_LOAD;
    mir.insns[1].dst = 0;
    mir.insns[1].type = TYPE_LONG;
    mir.insns[1].immediate = 32;
    if (!narrow_on_right)
        strcpy(mir.insns[1].name, local->name);
    mir.insns[2].opcode = narrow_on_right ? MIR_LOAD : MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].type = TYPE_LONG;
    mir.insns[2].immediate = 32;
    if (narrow_on_right)
        strcpy(mir.insns[2].name, local->name);
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].type = TYPE_INT;
    mir.insns[3].secondary_offset = TYPE_LONG;
    mir.insns[3].immediate = '<';
    mir.insns[4].src1 = 2;
}

static void verify_deferred_binary_conversion(void)
{
    struct Sym local;
    int saved_debug = opt_debug;
    int ok = 1;

    opt_debug = 1;
    setup_deferred_binary_conversion(&local, 0);

    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 6 && mir.next_value == 4;
    ok = ok && mir.insns[1].opcode == MIR_LOAD;
    ok = ok && mir.insns[1].type == TYPE_INT;
    ok = ok && mir.insns[3].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].src1 == 0;
    ok = ok && mir.insns[3].dst == 3;
    ok = ok && mir.insns[3].type == TYPE_LONG;
    ok = ok && mir.insns[4].opcode == MIR_BINARY;
    ok = ok && mir.insns[4].src1 == 3;
    ok = ok && mir.insns[4].src2 == 1;
    ok = ok && mir.insns[5].opcode == MIR_RETURN;
    ok = ok && mir.insns[5].src1 == 2;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 6 && mir.next_value == 4;
    ok = ok && mir_verify_and_dump();
    if (!ok) {
        fprintf(stderr, "FAIL deferred binary conversion repair\n");
        ++failures;
    }
    clear_liveness();

    setup_deferred_binary_conversion(&local, 1);
    mir_resolve_deferred_metadata();
    ok = mir.count == 6 && mir.next_value == 4;
    ok = ok && mir.insns[1].opcode == MIR_CONST;
    ok = ok && mir.insns[1].type == TYPE_LONG;
    ok = ok && mir.insns[2].opcode == MIR_LOAD;
    ok = ok && mir.insns[2].type == TYPE_INT;
    ok = ok && mir.insns[3].opcode == MIR_UNARY;
    ok = ok && mir.insns[3].src1 == 1;
    ok = ok && mir.insns[3].dst == 3;
    ok = ok && mir.insns[3].type == TYPE_LONG;
    ok = ok && mir.insns[4].opcode == MIR_BINARY;
    ok = ok && mir.insns[4].src1 == 0;
    ok = ok && mir.insns[4].src2 == 3;
    ok = ok && mir.insns[5].opcode == MIR_RETURN;
    ok = ok && mir.insns[5].src1 == 2;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 6 && mir.next_value == 4;
    ok = ok && mir_verify_and_dump();
    if (!ok) {
        fprintf(stderr, "FAIL deferred right binary conversion repair\n");
        ++failures;
    }
    clear_liveness();

    opt_debug = 0;
    setup_deferred_binary_conversion(&local, 0);
    mir_resolve_deferred_metadata();
    ok = mir.count == 5 && mir.next_value == 3;
    ok = ok && mir.insns[1].opcode == MIR_LOAD;
    ok = ok && mir.insns[1].type == TYPE_INT;
    ok = ok && mir.insns[3].opcode == MIR_BINARY;
    ok = ok && mir.insns[3].src1 == 0;
    ok = ok && mir.insns[3].src2 == 1;
    if (!ok) {
        fprintf(stderr, "FAIL release deferred binary conversion gating\n");
        ++failures;
    }
    opt_debug = saved_debug;
}

static void verify_deferred_metadata_coordinates(void)
{
    int ok = 1;

    setup_deferred_function_pointer_call(0);
    mir.debug_events[0].point = 3;
    add_deferred_debug_event(5, ";@dcc-line \"x\" 2\n");
    add_deferred_debug_event(9, ";@dcc-line \"x\" 3\n");
    add_deferred_debug_event(INT_MAX, ";@dcc-line \"x\" 4\n");
    mir.declaration_count = 4;
    mir.declaration_placeholders[2] = 4;
    mir.declaration_scope_ends[2] = 5;
    mir.declaration_scope_labels[2] = -1;
    mir.declaration_placeholders[3] = INT_MAX;
    mir.declaration_scope_ends[3] = INT_MAX;
    mir.declaration_scope_labels[3] = -1;
    mir.insns[4].opcode = MIR_DECL_PLACEHOLDER;

    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 11 && mir.next_value == 4;
    ok = ok && mir.declaration_placeholders[2] == 5;
    ok = ok && mir.declaration_scope_ends[2] == 6;
    ok = ok && mir.declaration_placeholders[3] == INT_MAX;
    ok = ok && mir.declaration_scope_ends[3] == INT_MAX;
    ok = ok && mir.debug_events[0].point == 4;
    ok = ok && mir.debug_events[1].point == 7;
    ok = ok && mir.debug_events[2].point == 11;
    ok = ok && mir.debug_events[3].point == INT_MAX;
    ok = ok && mir.insns[5].opcode == MIR_DECL_PLACEHOLDER;
    ok = ok && mir.insns[6].opcode == MIR_LOAD;
    if (!ok) {
        fprintf(stderr, "FAIL deferred metadata coordinate updates\n");
        ++failures;
    }
}

static void verify_deferred_metadata_alias_bounds(void)
{
    int ok = 1;

    setup(6, 2, 1);
    mir.insns[1].opcode = MIR_LOAD;
    mir.insns[1].dst = 0;
    strcpy(mir.insns[1].name, "outer");
    mir.insns[2].opcode = MIR_DECL_PLACEHOLDER;
    mir.insns[3].opcode = MIR_LOAD;
    mir.insns[3].dst = 1;
    strcpy(mir.insns[3].name, "outer");
    mir.insns[4].opcode = MIR_LOAD;
    strcpy(mir.insns[4].name, "outer");
    mir.declaration_count = 1;
    mir.declaration_placeholders[0] = 2;
    mir.declaration_scope_ends[0] = 4;
    mir.declaration_scope_labels[0] = -1;
    mir.alias_count = 1;
    strcpy(mir.alias_source_names[0], "outer");
    strcpy(mir.alias_internal_names[0], "outer#b0");
    mir.alias_declaration_indices[0] = 0;
    mir_resolve_deferred_metadata();
    ok = ok && !strcmp(mir.insns[1].name, "outer");
    ok = ok && !strcmp(mir.insns[3].name, "outer#b0");
    ok = ok && !strcmp(mir.insns[4].name, "outer");

    setup(6, 2, 1);
    mir.insns[1].opcode = MIR_LOAD;
    mir.insns[1].dst = 0;
    strcpy(mir.insns[1].name, "outer");
    mir.insns[3].opcode = MIR_LOAD;
    mir.insns[3].dst = 1;
    strcpy(mir.insns[3].name, "outer");
    mir.declaration_count = 1;
    mir.declaration_placeholders[0] = -2;
    mir.declaration_scope_ends[0] = 4;
    mir.declaration_scope_labels[0] = -1;
    mir.alias_count = 1;
    strcpy(mir.alias_source_names[0], "outer");
    strcpy(mir.alias_internal_names[0], "outer#b0");
    mir.alias_declaration_indices[0] = 0;
    mir_resolve_deferred_metadata();
    ok = ok && !strcmp(mir.insns[1].name, "outer");
    ok = ok && !strcmp(mir.insns[3].name, "outer");

    mir.alias_declaration_indices[0] = mir.declaration_count;
    mir_resolve_deferred_metadata();
    ok = ok && !strcmp(mir.insns[1].name, "outer");
    ok = ok && !strcmp(mir.insns[3].name, "outer");
    if (!ok) {
        fprintf(stderr, "FAIL deferred metadata alias bounds\n");
        ++failures;
    }
}

static void verify_deferred_metadata_call_ordering(void)
{
    struct MirInsn argument;
    int ok = 1;

    setup_deferred_function_pointer_call(0);
    argument = mir.insns[3];
    mir.insns[3].opcode = MIR_NOP;
    mir.insns[7] = argument;
    expect_verification("late deferred function-pointer argument", 0);
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 9 && mir.next_value == 2;
    ok = ok && mir.insns[5].opcode == MIR_CALL;
    ok = ok && !strcmp(mir.insns[5].name, "verify_deferred_callback");
    ok = ok && mir.insns[7].opcode == MIR_ARG;
    ok = ok && mir.debug_events[0].point == 5;

    setup_deferred_function_pointer_call(0);
    mir.insns[3].secondary_offset = mir.next_call_id;
    mir.insns[5].secondary_offset = mir.next_call_id;
    expect_verification("invalid deferred function-pointer call ID", 0);
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 9 && mir.next_value == 2;
    ok = ok && mir.insns[5].opcode == MIR_CALL;
    ok = ok && !strcmp(mir.insns[5].name, "verify_deferred_callback");
    ok = ok && mir.debug_events[0].point == 5;

    setup_deferred_function_pointer_call(0);
    mir.insns[5].src1 = 0;
    mir_resolve_deferred_metadata();
    ok = ok && mir.count == 9 && mir.next_value == 2;
    ok = ok && mir.insns[5].opcode == MIR_CALL;
    ok = ok && !strcmp(mir.insns[5].name, "verify_deferred_callback");
    ok = ok && mir.insns[5].src1 == 0;
    ok = ok && mir.debug_events[0].point == 5;
    if (!ok) {
        fprintf(stderr, "FAIL malformed deferred metadata call ordering\n");
        ++failures;
    }
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
    prepare_test_cfg_metadata();
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

static void expect_spilled_candidate_transaction_rejection(
    const char *name,
    int expected_label)
{
    MirStream *stream;
    int result;

    mir_invalidate_use_cache();
    prepare_test_cfg_metadata();
    stream = mir_stream_open();
    if (stream == NULL) {
        fprintf(stderr, "FAIL spilled preflight %s stream allocation\n", name);
        ++failures;
        clear_liveness();
        return;
    }
    label_id = expected_label;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(stream);
    if (result != 0 ||
        mir_stream_tell(stream) != 0 ||
        mir_stream_size(stream) != 0 ||
        label_id != expected_label ||
        mir_spilled_cfg_emitted_frame_bytes() != 0) {
        fprintf(stderr, "FAIL spilled preflight %s transaction\n", name);
        ++failures;
    }
    mir_stream_close(stream);
    clear_liveness();
}

static void verify_homed_parameter_preflight_transaction(void)
{
    MirStream *control;
    MirStream *retry;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int saved_color;
    int saved_spill;
    int saved_spill_count;
    int sid;
    int result;
    int mutation;
    int ok = 1;

    setup(5, 1, 2);
    mir.return_type = TYPE_INT;
    mir.object_count = 1;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    mir.objects[0].storage = SC_PARAM;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = 4;
    strcpy(mir.objects[0].name, "verify_homed_parameter");
    mir.insns[1].opcode = MIR_PARAM;
    mir.insns[1].object = 0;
    strcpy(mir.insns[1].name, mir.objects[0].name);
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].dst = -1;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].dst = -1;
    mir.insns[3].label = 1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed parameter preflight verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed parameter preflight stream allocation\n");
        ++failures;
        mir_stream_close(control);
        clear_liveness();
        return;
    }
    first_label = label_id;
    saved_color = mir.allocation_colors[0];
    saved_spill = mir.allocation_spills[0];
    saved_spill_count = mir.allocation_spill_count;
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    sid = add_struct_def("verify_homed_invalid_parameter");
    struct_defs[sid - 1].size = 6;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0; mutation < 3; ++mutation) {
        retry = mir_stream_open();
        if (retry == NULL) {
            ok = 0;
            break;
        }
        label_id = first_label;
        mir.insns[1].object = 0;
        mir.objects[0].storage = SC_PARAM;
        mir.objects[0].type = TYPE_INT;
        if (mutation == 0)
            mir.objects[0].type = make_struct_type(sid);
        else if (mutation == 1)
            mir.insns[1].object = mir.object_count;
        else
            mir.objects[0].storage = SC_LOCAL;

        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        ok = ok && result == 0;
        ok = ok && mir_stream_tell(retry) == 0 &&
             mir_stream_size(retry) == 0;
        ok = ok && label_id == first_label;
        ok = ok && mir.allocation_colors[0] == saved_color;
        ok = ok && mir.allocation_spills[0] == saved_spill;
        ok = ok && mir.allocation_spill_count == saved_spill_count;

        mir.insns[1].object = 0;
        mir.objects[0].storage = SC_PARAM;
        mir.objects[0].type = TYPE_INT;
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        ok = ok && result == 1;
        mir_stream_rewind(retry);
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        ok = ok && retry_bytes < sizeof(retry_text);
        ok = ok && control_bytes == retry_bytes;
        ok = ok && memcmp(control_text, retry_text, control_bytes) == 0;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed parameter preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_value_operand_preflight_transaction(void)
{
    struct OperandMutation {
        int opcode;
        int field;
        int invalid_value;
    };
    static const struct OperandMutation mutations[] = {
        { MIR_PARAM, 0, -1 },
        { MIR_CONST, 0, -1 },
        { MIR_FLOAT_CONST, 0, -1 },
        { MIR_STRING_ADDRESS, 0, -1 },
        { MIR_ADDRESS, 0, -1 },
        { MIR_INDEX_ADDRESS, 0, -1 },
        { MIR_INDEX_ADDRESS, 1, -1 },
        { MIR_INDEX_ADDRESS, 2, -1 },
        { MIR_MEMBER_ADDRESS, 0, -1 },
        { MIR_MEMBER_ADDRESS, 1, -1 },
        { MIR_LOAD, 0, -1 },
        { MIR_LOAD_INDIRECT, 0, -1 },
        { MIR_LOAD_INDIRECT, 1, -1 },
        { MIR_STORE, 1, -1 },
        { MIR_STORE_INDIRECT, 1, -1 },
        { MIR_STORE_INDIRECT, 2, -1 },
        { MIR_COPY_AGGREGATE, 1, -1 },
        { MIR_COPY_AGGREGATE, 2, -1 },
        { MIR_UNARY, 0, -1 },
        { MIR_UNARY, 1, -1 },
        { MIR_BINARY, 0, -1 },
        { MIR_BINARY, 1, -1 },
        { MIR_BINARY, 2, -1 },
        { MIR_ARG, 1, -1 },
        { MIR_CALL, 0, -1 },
        { MIR_BRANCH_FALSE, 1, -1 },
        { MIR_PHI, 0, -1 },
        { MIR_PHI, 1, -1 },
        { MIR_PHI, 2, -1 },
        { MIR_RETURN, 1, -1 },
        { MIR_BINARY, 0, 3 },
        { MIR_BINARY, 1, 3 },
        { MIR_BINARY, 2, 3 },
        { MIR_BINARY, 1, -2 }
    };
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    size_t mutation;
    int result;
    int ok = 1;

    setup(6, 3, 1);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 3;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].opcode = MIR_NOP;
    mir.insns[5].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed value operand verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed value operand stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        int *operand =
            mutations[mutation].field == 0 ? &mir.insns[3].dst :
            mutations[mutation].field == 1 ? &mir.insns[3].src1 :
                                             &mir.insns[3].src2;
        int original_opcode = mir.insns[3].opcode;
        int original = *operand;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        label_id = first_label;
        mir.insns[3].opcode = mutations[mutation].opcode;
        *operand = mutations[mutation].invalid_value;
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        ok = ok && result == 0;
        ok = ok && mir_stream_tell(retry) == 0 &&
             mir_stream_size(retry) == 0;
        ok = ok && label_id == first_label;

        mir.insns[3].opcode = original_opcode;
        *operand = original;
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        ok = ok && result == 1;
        mir_stream_rewind(retry);
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        ok = ok && retry_bytes < sizeof(retry_text);
        ok = ok && control_bytes == retry_bytes;
        ok = ok && memcmp(control_text, retry_text, control_bytes) == 0;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed value operand preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_branch_target_preflight_transaction(void)
{
    struct TargetMutation {
        int opcode;
        int label;
        int duplicate_definition;
    };
    static const struct TargetMutation mutations[] = {
        { MIR_JUMP, -1, 0 },
        { MIR_JUMP, 2, 0 },
        { MIR_JUMP, 3, 0 },
        { MIR_BRANCH_FALSE, -1, 0 },
        { MIR_BRANCH_FALSE, 2, 0 },
        { MIR_BRANCH_FALSE, 3, 0 },
        { MIR_JUMP, 1, 1 },
        { MIR_BRANCH_FALSE, 1, 1 }
    };
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int saved_color;
    int saved_spill;
    int saved_spill_count;
    size_t mutation;
    int result;
    int ok = 1;

    setup(6, 1, 3);
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed branch target verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed branch target stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    saved_color = mir.allocation_colors[0];
    saved_spill = mir.allocation_spills[0];
    saved_spill_count = mir.allocation_spill_count;
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        label_id = first_label;
        mir.insns[2].opcode = mutations[mutation].opcode;
        mir.insns[2].src1 =
            mutations[mutation].opcode == MIR_BRANCH_FALSE ? 0 : -1;
        mir.insns[2].label = mutations[mutation].label;
        if (mutations[mutation].duplicate_definition) {
            mir.insns[4].opcode = MIR_LABEL;
            mir.insns[4].label = 1;
        }
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok && mir_stream_tell(retry) == 0 &&
                      mir_stream_size(retry) == 0;
        mutation_ok = mutation_ok && label_id == first_label;
        mutation_ok =
            mutation_ok && mir.allocation_colors[0] == saved_color;
        mutation_ok =
            mutation_ok && mir.allocation_spills[0] == saved_spill;
        mutation_ok =
            mutation_ok && mir.allocation_spill_count == saved_spill_count;

        mir.insns[2].opcode = MIR_JUMP;
        mir.insns[2].src1 = -1;
        mir.insns[2].label = 1;
        mir.insns[4].opcode = MIR_NOP;
        mir.insns[4].label = -1;
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mir_stream_rewind(retry);
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && control_bytes == retry_bytes;
        mutation_ok = mutation_ok &&
            memcmp(control_text, retry_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr,
                    "FAIL homed branch target opcode=%s label=%d duplicate=%d\n",
                    mir_opcode_name(mutations[mutation].opcode),
                    mutations[mutation].label,
                    mutations[mutation].duplicate_definition);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed branch target preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_noncall_preflight_transaction(void)
{
    static const char *mutations[] = {
        "duplicate definition",
        "use before definition",
        "invalid unused label",
        "invalid object",
        "invalid home color",
        "invalid spill slot",
        "invalid value type",
        "binary operand width",
        "truncated allocation table",
        "invalid type flags"
    };
    struct Sym *global;
    struct MirInsn control_insns[8];
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int saved_colors[4];
    int saved_spills[4];
    int saved_allocation_capacity;
    int saved_spill_count;
    int first_label;
    size_t mutation;
    int result;
    int ok = 1;

    global = add_global("verify_homed_noncall_object", TYPE_INT, SC_GLOBAL);
    setup(8, 4, 1);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 5;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].opcode = MIR_LOAD;
    mir.insns[4].dst = 3;
    strcpy(mir.insns[4].name, global->name);
    mir.insns[7].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed noncall preflight verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed noncall preflight stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    memcpy(control_insns, mir.insns, sizeof(control_insns));
    memcpy(saved_colors, mir.allocation_colors, sizeof(saved_colors));
    memcpy(saved_spills, mir.allocation_spills, sizeof(saved_spills));
    saved_allocation_capacity = mir.allocation_capacity;
    saved_spill_count = mir.allocation_spill_count;
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        long prefix_end;
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        mir_stream_puts("; preserved prefix\n", retry);
        prefix_end = mir_stream_tell(retry);
        label_id = first_label;
        memcpy(mir.insns, control_insns, sizeof(control_insns));
        memcpy(mir.allocation_colors, saved_colors, sizeof(saved_colors));
        memcpy(mir.allocation_spills, saved_spills, sizeof(saved_spills));
        mir.allocation_capacity = saved_allocation_capacity;
        mir.allocation_spill_count = saved_spill_count;
        if (mutation == 0) {
            mir.insns[6].opcode = MIR_CONST;
            mir.insns[6].dst = 2;
            mir.insns[6].immediate = 99;
        } else if (mutation == 1) {
            mir.insns[2].opcode = MIR_NOP;
            mir.insns[2].dst = -1;
            mir.insns[6] = control_insns[2];
        } else if (mutation == 2) {
            mir.insns[6].opcode = MIR_LABEL;
            mir.insns[6].label = mir.next_label;
        } else if (mutation == 3) {
            mir.insns[4].object = mir.object_count;
        } else if (mutation == 4) {
            mir.allocation_colors[0] = MIR_COLOR_COUNT;
        } else if (mutation == 5) {
            mir.allocation_colors[0] = -1;
            mir.allocation_spills[0] = 1;
            mir.allocation_spill_count = 1;
        } else if (mutation == 6) {
            mir.insns[1].type = 7;
        } else if (mutation == 7) {
            mir.insns[3].immediate = TOK_EQ;
            mir.insns[3].secondary_offset = TYPE_LONG;
        } else if (mutation == 8) {
            mir.allocation_capacity = mir.next_value - 1;
        } else {
            mir.insns[1].type = TYPE_INT | 0x4000;
        }

        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok &&
            mir_stream_tell(retry) == prefix_end &&
            mir_stream_size(retry) == prefix_end;
        mutation_ok = mutation_ok && label_id == first_label;

        memcpy(mir.insns, control_insns, sizeof(control_insns));
        memcpy(mir.allocation_colors, saved_colors, sizeof(saved_colors));
        memcpy(mir.allocation_spills, saved_spills, sizeof(saved_spills));
        mir.allocation_capacity = saved_allocation_capacity;
        mir.allocation_spill_count = saved_spill_count;
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mutation_ok = mutation_ok &&
            mir_stream_seek(retry, prefix_end, SEEK_SET) == 0;
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && retry_bytes == control_bytes;
        mutation_ok = mutation_ok &&
            memcmp(retry_text, control_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr, "FAIL homed noncall %s transaction\n",
                    mutations[mutation]);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed noncall preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_dimension_preflight_transaction(void)
{
    static const char *dimensions[] = {
        "next label",
        "next value"
    };
    int invalid_dimensions[2];
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int saved_color;
    int saved_spill;
    int saved_spill_count;
    int saved_next_label;
    int saved_next_value;
    int first_label;
    int dimension;
    int mutation;
    int result;
    int ok = 1;

    setup(3, 1, 1);
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed dimension verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed dimension stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    saved_color = mir.allocation_colors[0];
    saved_spill = mir.allocation_spills[0];
    saved_spill_count = mir.allocation_spill_count;
    saved_next_label = mir.next_label;
    saved_next_value = mir.next_value;
    invalid_dimensions[0] = mir.capacity + 1;
    invalid_dimensions[1] = INT_MAX;
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (dimension = 0; dimension < 2; ++dimension) {
        for (mutation = 0; mutation < 2; ++mutation) {
            MirStream *retry = mir_stream_open();
            long prefix_end;
            int mutation_ok = 1;

            if (retry == NULL) {
                ok = 0;
                break;
            }
            mir_stream_puts("; preserved prefix\n", retry);
            prefix_end = mir_stream_tell(retry);
            label_id = first_label;
            if (dimension == 0)
                mir.next_label = invalid_dimensions[mutation];
            else
                mir.next_value = invalid_dimensions[mutation];
            mir_extrn_begin_attempt();
            result = mir_try_emit_homed_scalar_cfg(retry);
            mutation_ok = mutation_ok && result == 0;
            mutation_ok = mutation_ok &&
                mir_stream_tell(retry) == prefix_end &&
                mir_stream_size(retry) == prefix_end;
            mutation_ok = mutation_ok && label_id == first_label;
            mutation_ok = mutation_ok &&
                (dimension == 0
                    ? mir.next_label == invalid_dimensions[mutation] &&
                      mir.next_value == saved_next_value
                    : mir.next_label == saved_next_label &&
                      mir.next_value == invalid_dimensions[mutation]);
            mutation_ok = mutation_ok &&
                mir.allocation_colors[0] == saved_color &&
                mir.allocation_spills[0] == saved_spill &&
                mir.allocation_spill_count == saved_spill_count;

            mir.next_label = saved_next_label;
            mir.next_value = saved_next_value;
            mir_extrn_begin_attempt();
            result = mir_try_emit_homed_scalar_cfg(retry);
            mutation_ok = mutation_ok && result == 1;
            mutation_ok = mutation_ok &&
                mir.next_label == saved_next_label &&
                mir.next_value == saved_next_value;
            mutation_ok = mutation_ok &&
                mir_stream_seek(retry, prefix_end, SEEK_SET) == 0;
            retry_bytes = mir_stream_read(
                retry_text, 1, sizeof(retry_text), retry);
            mutation_ok = mutation_ok &&
                retry_bytes < sizeof(retry_text) &&
                retry_bytes == control_bytes;
            mutation_ok = mutation_ok &&
                memcmp(retry_text, control_text, control_bytes) == 0;
            if (!mutation_ok)
                fprintf(stderr,
                        "FAIL homed dimension %s %s transaction\n",
                        dimensions[dimension],
                        mutation == 0 ? "capacity+1" : "INT_MAX");
            ok = ok && mutation_ok;
            mir_stream_close(retry);
        }
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed dimension preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_noncall_dominance_transaction(void)
{
    struct MirInsn control_insns[11];
    MirStream *control;
    MirStream *retry;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    long prefix_end;
    int result;
    int ok = 1;

    setup(11, 3, 3);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 9;
    mir.insns[3].opcode = MIR_BRANCH_FALSE;
    mir.insns[3].src1 = 0;
    mir.insns[3].label = 1;
    mir.insns[5].opcode = MIR_JUMP;
    mir.insns[5].label = 2;
    mir.insns[6].opcode = MIR_LABEL;
    mir.insns[6].label = 1;
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 2;
    mir.insns[9].opcode = MIR_BINARY;
    mir.insns[9].dst = 2;
    mir.insns[9].src1 = 0;
    mir.insns[9].src2 = 1;
    mir.insns[9].immediate = '+';
    mir.insns[9].secondary_offset = TYPE_INT;
    mir.insns[10].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed dominance verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    retry = mir_stream_open();
    if (control == NULL || retry == NULL) {
        fprintf(stderr, "FAIL homed dominance stream allocation\n");
        ++failures;
        mir_stream_close(retry);
        mir_stream_close(control);
        clear_liveness();
        return;
    }
    first_label = label_id;
    memcpy(control_insns, mir.insns, sizeof(control_insns));
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    mir_stream_puts("; preserved prefix\n", retry);
    prefix_end = mir_stream_tell(retry);
    label_id = first_label;
    mir.insns[2].opcode = MIR_NOP;
    mir.insns[2].dst = -1;
    mir.insns[4] = control_insns[2];
    mir_invalidate_use_cache();
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(retry);
    ok = ok && result == 0;
    ok = ok && mir_stream_tell(retry) == prefix_end &&
         mir_stream_size(retry) == prefix_end;
    ok = ok && label_id == first_label;

    memcpy(mir.insns, control_insns, sizeof(control_insns));
    mir_invalidate_use_cache();
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(retry);
    ok = ok && result == 1;
    ok = ok && mir_stream_seek(retry, prefix_end, SEEK_SET) == 0;
    retry_bytes = mir_stream_read(
        retry_text, 1, sizeof(retry_text), retry);
    ok = ok && retry_bytes < sizeof(retry_text);
    ok = ok && retry_bytes == control_bytes;
    ok = ok && memcmp(retry_text, control_text, control_bytes) == 0;
    if (!ok) {
        fprintf(stderr, "FAIL homed noncall dominance transaction\n");
        ++failures;
    }
    mir_stream_close(retry);
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_memory_preflight_transaction(void)
{
    static const char *rejections[] = {
        "bitfield extent",
        "bitfield mask",
        "wide address source",
        "wide narrow-store source"
    };
    static const char *compatible[] = {
        "integer address representation",
        "byte value representation",
        "implicit integer representation"
    };
    struct MirInsn control_insns[5];
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int saved_colors[2];
    int saved_spills[2];
    int saved_spill_count;
    int first_label;
    size_t mutation;
    int result;
    int ok = 1;

    setup(5, 2, 1);
    mir.insns[1].type = TYPE_VOID | TYPE_PTR;
    mir.insns[1].immediate = 4096;
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 3;
    mir.insns[3].opcode = MIR_STORE_INDIRECT;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].memory_size = 2;
    mir.insns[3].bit_width = 3;
    mir.insns[3].bit_shift = 4;
    mir.insns[3].bit_mask = 0x70;
    mir.insns[4].src1 = 1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed memory preflight verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed memory preflight stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    memcpy(control_insns, mir.insns, sizeof(control_insns));
    memcpy(saved_colors, mir.allocation_colors, sizeof(saved_colors));
    memcpy(saved_spills, mir.allocation_spills, sizeof(saved_spills));
    saved_spill_count = mir.allocation_spill_count;
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(rejections) / sizeof(rejections[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        long prefix_end;
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        mir_stream_puts("; preserved prefix\n", retry);
        prefix_end = mir_stream_tell(retry);
        label_id = first_label;
        memcpy(mir.insns, control_insns, sizeof(control_insns));
        memcpy(mir.allocation_colors, saved_colors, sizeof(saved_colors));
        memcpy(mir.allocation_spills, saved_spills, sizeof(saved_spills));
        mir.allocation_spill_count = saved_spill_count;
        if (mutation == 0) {
            mir.insns[3].bit_shift = 15;
            mir.insns[3].bit_mask = 0x8000;
        } else if (mutation == 1) {
            mir.insns[3].bit_mask = 0x60;
        } else if (mutation == 2) {
            mir.insns[1].type = TYPE_LONG;
        } else {
            mir.insns[2].type = TYPE_LONG;
        }
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok &&
            mir_stream_tell(retry) == prefix_end &&
            mir_stream_size(retry) == prefix_end;
        mutation_ok = mutation_ok && label_id == first_label;

        memcpy(mir.insns, control_insns, sizeof(control_insns));
        memcpy(mir.allocation_colors, saved_colors, sizeof(saved_colors));
        memcpy(mir.allocation_spills, saved_spills, sizeof(saved_spills));
        mir.allocation_spill_count = saved_spill_count;
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mutation_ok = mutation_ok &&
            mir_stream_seek(retry, prefix_end, SEEK_SET) == 0;
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && retry_bytes == control_bytes;
        mutation_ok = mutation_ok &&
            memcmp(retry_text, control_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr, "FAIL homed memory %s transaction\n",
                    rejections[mutation]);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }

    for (mutation = 0;
         mutation < sizeof(compatible) / sizeof(compatible[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        label_id = first_label;
        memcpy(mir.insns, control_insns, sizeof(control_insns));
        memcpy(mir.allocation_colors, saved_colors, sizeof(saved_colors));
        memcpy(mir.allocation_spills, saved_spills, sizeof(saved_spills));
        mir.allocation_spill_count = saved_spill_count;
        if (mutation == 0)
            mir.insns[1].type = TYPE_INT | TYPE_UNSIGNED;
        else if (mutation == 1)
            mir.insns[2].type = TYPE_CHAR | TYPE_UNSIGNED;
        else
            mir.insns[2].type = 0;
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mir_stream_rewind(retry);
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && retry_bytes == control_bytes;
        mutation_ok = mutation_ok &&
            memcmp(retry_text, control_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr, "FAIL homed compatible %s control\n",
                    compatible[mutation]);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed memory preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_phi_preflight_transaction(void)
{
    static const char *mutations[] = {
        "unknown predecessor",
        "non-dominating source"
    };
    struct MirInsn control_insns[11];
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    size_t mutation;
    int result;
    int ok = 1;

    setup(11, 4, 4);
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 2;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    mir.insns[4].opcode = MIR_CONST;
    mir.insns[4].dst = 1;
    mir.insns[4].immediate = 10;
    mir.insns[5].opcode = MIR_JUMP;
    mir.insns[5].label = 3;
    mir.insns[6].opcode = MIR_LABEL;
    mir.insns[6].label = 2;
    mir.insns[7].opcode = MIR_CONST;
    mir.insns[7].dst = 2;
    mir.insns[7].immediate = 20;
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 3;
    mir.insns[9].opcode = MIR_PHI;
    mir.insns[9].dst = 3;
    mir.insns[9].src1 = 1;
    mir.insns[9].phi_pred1 = 1;
    mir.insns[9].src2 = 2;
    mir.insns[9].phi_pred2 = 2;
    mir.insns[10].src1 = 3;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed PHI preflight verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed PHI preflight stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    memcpy(control_insns, mir.insns, sizeof(control_insns));
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        long prefix_end;
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        mir_stream_puts("; preserved prefix\n", retry);
        prefix_end = mir_stream_tell(retry);
        label_id = first_label;
        memcpy(mir.insns, control_insns, sizeof(control_insns));
        if (mutation == 0)
            mir.insns[9].phi_pred1 = 0;
        else
            mir.insns[9].src1 = 2;
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok &&
            mir_stream_tell(retry) == prefix_end &&
            mir_stream_size(retry) == prefix_end;
        mutation_ok = mutation_ok && label_id == first_label;

        memcpy(mir.insns, control_insns, sizeof(control_insns));
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mutation_ok = mutation_ok &&
            mir_stream_seek(retry, prefix_end, SEEK_SET) == 0;
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && retry_bytes == control_bytes;
        mutation_ok = mutation_ok &&
            memcmp(retry_text, control_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr, "FAIL homed PHI %s transaction\n",
                    mutations[mutation]);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed PHI preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_call_preflight_transaction(void)
{
    static const char *mutations[] = {
        "call return type",
        "callee storage",
        "argument ABI type",
        "argument value type",
        "argument position",
        "call ID",
        "missing argument",
        "duplicate argument",
        "late argument",
        "orphan argument",
        "direct call source",
        "direct call second source",
        "undefined argument value",
        "later argument definition",
        "reversed arguments",
        "missing call result",
        "call result collision",
        "later call result redefinition"
    };
    struct Sym *callee;
    struct MirInsn control_insns[9];
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int saved_colors[4];
    int saved_spills[4];
    int saved_spill_count;
    size_t mutation;
    int result;
    int ok = 1;

    callee = add_global("verify_homed_call_target", TYPE_INT, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 2;
    callee->proto_types[0] = TYPE_INT | TYPE_UNSIGNED;
    callee->proto_types[1] = TYPE_VOID | TYPE_PTR;
    setup(9, 4, 1);
    mir.next_call_id = 1;
    mir.insns[1].type = TYPE_INT;
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].type = TYPE_CHAR | TYPE_UNSIGNED | TYPE_PTR;
    mir.insns[2].immediate = 1;
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    mir.insns[3].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[3].immediate = 0;
    mir.insns[3].secondary_offset = 0;
    mir.insns[4].opcode = MIR_ARG;
    mir.insns[4].src1 = 1;
    mir.insns[4].type = TYPE_VOID | TYPE_PTR;
    mir.insns[4].immediate = 1;
    mir.insns[4].secondary_offset = 0;
    mir.insns[6].opcode = MIR_CALL;
    mir.insns[6].dst = 2;
    mir.insns[6].type = TYPE_INT;
    mir.insns[6].secondary_offset = 0;
    strcpy(mir.insns[6].name, callee->name);
    mir.insns[8].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed call preflight verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed call preflight stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    memcpy(saved_colors, mir.allocation_colors, sizeof(saved_colors));
    memcpy(saved_spills, mir.allocation_spills, sizeof(saved_spills));
    saved_spill_count = mir.allocation_spill_count;
    memcpy(control_insns, mir.insns, sizeof(control_insns));
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        long prefix_end;
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        mir_stream_puts("; preserved prefix\n", retry);
        prefix_end = mir_stream_tell(retry);
        label_id = first_label;
        memcpy(mir.insns, control_insns, sizeof(control_insns));
        mir.next_call_id = 1;
        callee->storage = SC_FUNC;
        if (mutation == 0)
            mir.insns[6].type = TYPE_CHAR;
        else if (mutation == 1)
            callee->storage = SC_GLOBAL;
        else if (mutation == 2) {
            mir.insns[1].type = TYPE_CHAR;
            mir.insns[3].type = TYPE_CHAR;
        } else if (mutation == 3)
            mir.insns[1].type = TYPE_CHAR;
        else if (mutation == 4)
            mir.insns[4].immediate = 2;
        else if (mutation == 5)
            mir.insns[6].secondary_offset = 1;
        else if (mutation == 6)
            mir.insns[4].opcode = MIR_NOP;
        else if (mutation == 7)
            mir.insns[4].immediate = 0;
        else if (mutation == 8) {
            mir.insns[4].opcode = MIR_NOP;
            mir.insns[7] = control_insns[4];
        } else if (mutation == 9) {
            mir.insns[4].secondary_offset = 1;
            mir.next_call_id = 2;
        } else if (mutation == 10) {
            mir.insns[6].src1 = 0;
        } else if (mutation == 11) {
            mir.insns[6].src2 = 0;
        } else if (mutation == 12) {
            mir.insns[4].src1 = 3;
        } else if (mutation == 13) {
            mir.insns[4].src1 = 3;
            mir.insns[5].opcode = MIR_CONST;
            mir.insns[5].dst = 3;
        } else if (mutation == 14) {
            mir.insns[3].immediate = 1;
            mir.insns[4].immediate = 0;
        } else if (mutation == 15) {
            mir.insns[6].dst = -1;
        } else if (mutation == 16) {
            mir.insns[6].dst = 1;
        } else {
            mir.insns[7].opcode = MIR_CONST;
            mir.insns[7].dst = 2;
            mir.insns[7].immediate = 99;
        }

        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok &&
            mir_stream_tell(retry) == prefix_end &&
            mir_stream_size(retry) == prefix_end;
        mutation_ok = mutation_ok && label_id == first_label;
        mutation_ok = mutation_ok &&
            memcmp(mir.allocation_colors, saved_colors,
                   sizeof(saved_colors)) == 0;
        mutation_ok = mutation_ok &&
            memcmp(mir.allocation_spills, saved_spills,
                   sizeof(saved_spills)) == 0;
        mutation_ok =
            mutation_ok && mir.allocation_spill_count == saved_spill_count;

        memcpy(mir.insns, control_insns, sizeof(control_insns));
        mir.next_call_id = 1;
        callee->storage = SC_FUNC;
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mutation_ok = mutation_ok &&
            mir_stream_seek(retry, prefix_end, SEEK_SET) == 0;
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && control_bytes == retry_bytes;
        mutation_ok = mutation_ok &&
            memcmp(control_text, retry_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr, "FAIL homed call preflight %s transaction\n",
                    mutations[mutation]);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed call preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_call_dominance_preflight_transaction(void)
{
    static const char *mutations[] = {
        "argument bypass",
        "argument source bypass"
    };
    struct Sym *callee;
    struct MirInsn control_insns[10];
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int saved_colors[3];
    int saved_spills[3];
    int saved_spill_count;
    int first_label;
    size_t mutation;
    int result;
    int ok = 1;

    callee = add_global("verify_homed_dominance_target", TYPE_INT, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 1;
    callee->proto_types[0] = TYPE_INT;
    setup(10, 3, 3);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 7;
    mir.insns[3].opcode = MIR_BRANCH_FALSE;
    mir.insns[3].src1 = 0;
    mir.insns[3].label = 1;
    mir.insns[5].opcode = MIR_LABEL;
    mir.insns[5].label = 1;
    mir.insns[6].opcode = MIR_ARG;
    mir.insns[6].src1 = 1;
    mir.insns[6].immediate = 0;
    mir.insns[6].secondary_offset = 0;
    mir.insns[7].opcode = MIR_LABEL;
    mir.insns[7].label = 2;
    mir.insns[8].opcode = MIR_CALL;
    mir.insns[8].dst = 2;
    mir.insns[8].secondary_offset = 0;
    strcpy(mir.insns[8].name, callee->name);
    mir.insns[9].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed call dominance verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL homed call dominance stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    memcpy(saved_colors, mir.allocation_colors, sizeof(saved_colors));
    memcpy(saved_spills, mir.allocation_spills, sizeof(saved_spills));
    saved_spill_count = mir.allocation_spill_count;
    memcpy(control_insns, mir.insns, sizeof(control_insns));
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        long prefix_end;
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        mir_stream_puts("; preserved prefix\n", retry);
        prefix_end = mir_stream_tell(retry);
        label_id = first_label;
        memcpy(mir.insns, control_insns, sizeof(control_insns));
        if (mutation == 0) {
            mir.insns[3].label = 2;
        } else {
            mir.insns[2].opcode = MIR_BRANCH_FALSE;
            mir.insns[2].dst = -1;
            mir.insns[2].src1 = 0;
            mir.insns[2].label = 1;
            mir.insns[3].opcode = MIR_CONST;
            mir.insns[3].dst = 1;
            mir.insns[3].src1 = -1;
            mir.insns[3].label = -1;
            mir.insns[3].immediate = 7;
        }
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok &&
            mir_stream_tell(retry) == prefix_end &&
            mir_stream_size(retry) == prefix_end;
        mutation_ok = mutation_ok && label_id == first_label;
        mutation_ok = mutation_ok &&
            memcmp(mir.allocation_colors, saved_colors,
                   sizeof(saved_colors)) == 0;
        mutation_ok = mutation_ok &&
            memcmp(mir.allocation_spills, saved_spills,
                   sizeof(saved_spills)) == 0;
        mutation_ok =
            mutation_ok && mir.allocation_spill_count == saved_spill_count;

        memcpy(mir.insns, control_insns, sizeof(control_insns));
        mir_invalidate_use_cache();
        mir_extrn_begin_attempt();
        result = mir_try_emit_homed_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mutation_ok = mutation_ok &&
            mir_stream_seek(retry, prefix_end, SEEK_SET) == 0;
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && retry_bytes == control_bytes;
        mutation_ok = mutation_ok &&
            memcmp(retry_text, control_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr,
                    "FAIL homed call dominance %s transaction\n",
                    mutations[mutation]);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL homed call dominance transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_homed_unused_value_operands(void)
{
    struct Sym *callee;
    MirStream *stream;
    int result;
    int ok = 1;

    callee = add_global("verify_homed_void_call", TYPE_VOID, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 0;
    setup(3, 1, 1);
    mir.return_type = TYPE_VOID;
    mir.next_call_id = 1;
    mir.insns[1].opcode = MIR_CALL;
    mir.insns[1].dst = 0;
    mir.insns[1].type = TYPE_VOID;
    mir.insns[1].secondary_offset = 0;
    strcpy(mir.insns[1].name, callee->name);
    mir.insns[2].src1 = -1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL homed unused operand verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    stream = mir_stream_open();
    if (stream == NULL) {
        fprintf(stderr, "FAIL homed unused operand stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    mir_extrn_begin_attempt();
    result = mir_try_emit_homed_scalar_cfg(stream);
    ok = ok && result == 1 && mir_stream_size(stream) > 0;
    if (!ok) {
        fprintf(stderr, "FAIL homed legitimate unused value operands\n");
        ++failures;
    }
    mir_stream_close(stream);
    clear_liveness();
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

    setup(3, 1, 1);
    mir.aggregate_temp_bytes = INT_MAX;
    expect_spilled_candidate("overflowing aggregate frame rejection", 0);

    setup(3, 1, 1);
    mir.dead_local_suffix_bytes = INT_MIN;
    expect_spilled_candidate("overflowing effective frame rejection", 0);

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

    sid = add_struct_def("verify_spilled_indirect_aggregate");
    struct_defs[sid - 1].size = 2;
    setup(4, 2, 1);
    mir.local_bytes = 2;
    mir.next_call_id = 1;
    mir.insns[1].type = type_add_ptr(make_struct_type(sid));
    mir.insns[2].opcode = MIR_CALL_AGGREGATE;
    mir.insns[2].dst = 1;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = type_add_ptr(make_struct_type(sid));
    mir.insns[2].immediate = -2;
    mir.insns[2].memory_size = 2;
    mir.insns[2].secondary_offset = 0;
    strcpy(mir.insns[2].name, "<indirect>");
    mir.insns[3].src1 = 1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL indirect aggregate call verification control\n");
        ++failures;
    }
    expect_spilled_candidate("indirect aggregate call rejection", 0);

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

static void verify_spilled_value_operand_preflight_transaction(void)
{
    struct OperandMutation {
        int opcode;
        int field;
        int invalid_value;
        int indirect_call;
        int aggregate_value_destination;
    };
    static const struct OperandMutation mutations[] = {
        { MIR_PARAM, 0, -1, 0, 0 },
        { MIR_CONST, 0, -1, 0, 0 },
        { MIR_FLOAT_CONST, 0, -1, 0, 0 },
        { MIR_STRING_ADDRESS, 0, -1, 0, 0 },
        { MIR_ADDRESS, 0, -1, 0, 0 },
        { MIR_COMPOUND_ADDRESS, 0, -1, 0, 0 },
        { MIR_INDEX_ADDRESS, 0, -1, 0, 0 },
        { MIR_INDEX_ADDRESS, 1, -1, 0, 0 },
        { MIR_INDEX_ADDRESS, 2, -1, 0, 0 },
        { MIR_MEMBER_ADDRESS, 0, -1, 0, 0 },
        { MIR_MEMBER_ADDRESS, 1, -1, 0, 0 },
        { MIR_VLA_SIZE, 0, -1, 0, 0 },
        { MIR_LOAD, 0, -1, 0, 0 },
        { MIR_LOAD_INDIRECT, 0, -1, 0, 0 },
        { MIR_LOAD_INDIRECT, 1, -1, 0, 0 },
        { MIR_STORE, 1, -1, 0, 0 },
        { MIR_STORE_INDIRECT, 1, -1, 0, 0 },
        { MIR_STORE_INDIRECT, 2, -1, 0, 0 },
        { MIR_COPY_AGGREGATE, 1, -1, 0, 0 },
        { MIR_COPY_AGGREGATE, 2, -1, 0, 0 },
        { MIR_VLA_ALLOC, 1, -1, 0, 0 },
        { MIR_UNARY, 0, -1, 0, 0 },
        { MIR_UNARY, 1, -1, 0, 0 },
        { MIR_BINARY, 0, -1, 0, 0 },
        { MIR_BINARY, 1, -1, 0, 0 },
        { MIR_BINARY, 2, -1, 0, 0 },
        { MIR_ARG, 1, -1, 0, 0 },
        { MIR_CALL, 0, -1, 0, 0 },
        { MIR_CALL, 1, -1, 1, 0 },
        { MIR_CALL_AGGREGATE, 0, -1, 0, 0 },
        { MIR_CALL_AGGREGATE, 1, -1, 0, 1 },
        { MIR_VA_START, 0, -1, 0, 0 },
        { MIR_VA_END, 0, -1, 0, 0 },
        { MIR_VA_ARG, 0, -1, 0, 0 },
        { MIR_BRANCH_FALSE, 1, -1, 0, 0 },
        { MIR_PHI, 0, -1, 0, 0 },
        { MIR_PHI, 1, -1, 0, 0 },
        { MIR_PHI, 2, -1, 0, 0 },
        { MIR_RETURN, 1, -1, 0, 0 },
        { MIR_BINARY, 0, 3, 0, 0 },
        { MIR_BINARY, 1, 3, 0, 0 },
        { MIR_BINARY, 2, 3, 0, 0 },
        { MIR_BINARY, 1, -2, 0, 0 }
    };
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    size_t mutation;
    int result;
    int ok = 1;

    setup(6, 3, 1);
    mir.local_bytes = 2;
    mir.object_count = 1;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    mir.objects[0].storage = SC_LOCAL;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = -2;
    strcpy(mir.objects[0].name, "verify_spilled_operand");
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].opcode = MIR_NOP;
    mir.insns[5].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL spilled value operand verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL spilled value operand stream allocation\n");
        ++failures;
        mir_stream_close(control);
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        int *operand =
            mutations[mutation].field == 0 ? &mir.insns[3].dst :
            mutations[mutation].field == 1 ? &mir.insns[3].src1 :
                                             &mir.insns[3].src2;
        struct MirInsn original = mir.insns[3];
        int original_next_call_id = mir.next_call_id;
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        label_id = first_label;
        mir.insns[3].opcode = mutations[mutation].opcode;
        mir.insns[3].object = 0;
        mir.insns[3].memory_size = 2;
        strcpy(mir.insns[3].name, mir.objects[0].name);
        if (mutations[mutation].opcode == MIR_BRANCH_FALSE)
            mir.insns[3].label = 0;
        if (mutations[mutation].opcode == MIR_CALL ||
            mutations[mutation].opcode == MIR_CALL_AGGREGATE) {
            mir.next_call_id = 1;
            mir.insns[3].secondary_offset = 0;
            strcpy(mir.insns[3].name, "verify_spilled_operand_call");
        }
        if (mutations[mutation].opcode == MIR_CALL_AGGREGATE) {
            mir.insns[3].type = type_add_ptr(TYPE_INT);
            mir.insns[3].immediate = 0;
        }
        if (mutations[mutation].opcode == MIR_VA_ARG) {
            mir.insns[3].immediate = -2;
            mir.insns[3].secondary_offset = 2;
        }
        if (mutations[mutation].indirect_call)
            strcpy(mir.insns[3].name, "<indirect>");
        if (mutations[mutation].aggregate_value_destination)
            mir.insns[3].immediate = MIR_AGGREGATE_VALUE_DEST_OFFSET;
        *operand = mutations[mutation].invalid_value;
        mir_extrn_begin_attempt();
        result = mir_try_emit_spilled_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok && mir_stream_tell(retry) == 0 &&
                      mir_stream_size(retry) == 0;
        mutation_ok = mutation_ok && label_id == first_label;
        mutation_ok =
            mutation_ok && mir_spilled_cfg_emitted_frame_bytes() == 0;
        mutation_ok = mutation_ok &&
            !mir_spilled_cfg_depends_on_promoted_local_slot_reuse();

        mir.insns[3] = original;
        mir.next_call_id = original_next_call_id;
        mir_extrn_begin_attempt();
        result = mir_try_emit_spilled_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mir_stream_rewind(retry);
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && control_bytes == retry_bytes;
        mutation_ok = mutation_ok &&
            memcmp(control_text, retry_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr,
                    "FAIL spilled operand opcode=%s field=%d value=%d\n",
                    mir_opcode_name(mutations[mutation].opcode),
                    mutations[mutation].field,
                    mutations[mutation].invalid_value);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL spilled value operand preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_spilled_unused_value_operands(void)
{
    struct Sym *callee;
    MirStream *stream;
    int result;
    int ok = 1;

    callee = add_global("verify_spilled_void_call", TYPE_VOID, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 0;
    setup(6, 0, 2);
    mir.return_type = TYPE_VOID;
    mir.next_call_id = 1;
    mir.insns[1].opcode = MIR_CALL;
    mir.insns[1].dst = -1;
    mir.insns[1].type = TYPE_VOID;
    mir.insns[1].secondary_offset = 0;
    strcpy(mir.insns[1].name, callee->name);
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_NOP;
    mir.insns[4].opcode = MIR_LABEL;
    mir.insns[4].label = 1;
    mir.insns[5].src1 = -1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL spilled unused operand verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    stream = mir_stream_open();
    if (stream == NULL) {
        fprintf(stderr, "FAIL spilled unused operand stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(stream);
    ok = ok && result == 1 && mir_stream_size(stream) > 0;
    if (!ok) {
        fprintf(stderr, "FAIL spilled legitimate unused value operands\n");
        ++failures;
    }
    mir_stream_close(stream);
    clear_liveness();
}

static void verify_va_arg_offset_preflight(void)
{
    static const long invalid_offsets[] = { LONG_MIN, -129, 127, LONG_MAX };
    static const long valid_offsets[] = { -128, 126 };
    size_t invalid;
    size_t valid;

    for (valid = 0; valid < sizeof(valid_offsets) / sizeof(valid_offsets[0]);
         ++valid) {
        for (invalid = 0;
             invalid < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]);
             ++invalid) {
            MirStream *control;
            MirStream *retry;
            char control_text[2048];
            char retry_text[2048];
            size_t control_bytes;
            size_t retry_bytes;
            int first_label;
            int result;

            setup(4, 2, 1);
            mir.local_bytes = 128;
            mir.insns[2].opcode = MIR_VA_ARG;
            mir.insns[2].dst = 1;
            mir.insns[2].immediate = valid_offsets[valid];
            mir.insns[2].secondary_offset = 2;
            mir.insns[3].src1 = 1;
            if (!mir_verify_and_dump()) {
                fprintf(stderr, "FAIL va_arg offset verification control\n");
                ++failures;
                clear_liveness();
                return;
            }
            control = mir_stream_open();
            retry = mir_stream_open();
            if (control == NULL || retry == NULL) {
                fprintf(stderr, "FAIL va_arg offset stream allocation\n");
                ++failures;
                mir_stream_close(control);
                mir_stream_close(retry);
                clear_liveness();
                return;
            }
            first_label = label_id;
            result = mir_try_emit_spilled_scalar_cfg(control);
            if (result != 1 || mir_stream_size(control) <= 0) {
                fprintf(stderr, "FAIL va_arg valid offset %ld\n",
                        valid_offsets[valid]);
                ++failures;
            }
            label_id = first_label;
            mir.insns[2].immediate = invalid_offsets[invalid];
            result = mir_try_emit_spilled_scalar_cfg(retry);
            if (result != 0) {
                fprintf(stderr, "FAIL va_arg invalid offset %ld accepted\n",
                        invalid_offsets[invalid]);
                ++failures;
            }
            if (mir_stream_tell(retry) != 0 || mir_stream_size(retry) != 0) {
                fprintf(stderr, "FAIL va_arg rejected offset %ld emitted text\n",
                        invalid_offsets[invalid]);
                ++failures;
            }
            if (label_id != first_label) {
                fprintf(stderr, "FAIL va_arg rejected offset %ld consumed labels\n",
                        invalid_offsets[invalid]);
                ++failures;
            }
            /* Retry without resetting the function, stream, or analysis caches. */
            mir.insns[2].immediate = valid_offsets[valid];
            result = mir_try_emit_spilled_scalar_cfg(retry);
            if (result != 1) {
                fprintf(stderr, "FAIL va_arg offset %ld retry after %ld\n",
                        valid_offsets[valid], invalid_offsets[invalid]);
                ++failures;
            }
            mir_stream_rewind(control);
            mir_stream_rewind(retry);
            control_bytes = mir_stream_read(
                control_text, 1, sizeof(control_text), control);
            retry_bytes = mir_stream_read(
                retry_text, 1, sizeof(retry_text), retry);
            if (control_bytes == sizeof(control_text) ||
                retry_bytes == sizeof(retry_text) ||
                control_bytes != retry_bytes ||
                memcmp(control_text, retry_text, control_bytes) != 0) {
                fprintf(stderr, "FAIL va_arg retry changed valid offset %ld text\n",
                        valid_offsets[valid]);
                ++failures;
            }
            mir_stream_close(control);
            mir_stream_close(retry);
            clear_liveness();
        }
    }
}

static void verify_direct_call_name_preflight(void)
{
    MirStream *control;
    MirStream *retry;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int result;

    setup(3, 1, 1);
    mir.next_call_id = 1;
    mir.insns[1].opcode = MIR_CALL;
    mir.insns[1].secondary_offset = 0;
    strcpy(mir.insns[1].name, "verify_named_call");
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL direct call name verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    retry = mir_stream_open();
    if (control == NULL || retry == NULL) {
        fprintf(stderr, "FAIL direct call name stream allocation\n");
        ++failures;
        mir_stream_close(control);
        mir_stream_close(retry);
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    if (result != 1 || mir_stream_size(control) <= 0) {
        fprintf(stderr, "FAIL valid direct call name\n");
        ++failures;
    }
    label_id = first_label;
    mir.insns[1].name[0] = '\0';
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    if (result != 0) {
        fprintf(stderr, "FAIL empty direct call name accepted\n");
        ++failures;
    }
    if (mir_stream_tell(retry) != 0 || mir_stream_size(retry) != 0) {
        fprintf(stderr, "FAIL empty direct call name emitted text\n");
        ++failures;
    }
    if (label_id != first_label) {
        fprintf(stderr, "FAIL empty direct call name consumed labels\n");
        ++failures;
    }
    strcpy(mir.insns[1].name, "verify_named_call");
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    if (result != 1) {
        fprintf(stderr, "FAIL direct call retry after empty name\n");
        ++failures;
    }
    mir_stream_rewind(control);
    mir_stream_rewind(retry);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    retry_bytes = mir_stream_read(
        retry_text, 1, sizeof(retry_text), retry);
    if (control_bytes == sizeof(control_text) ||
        retry_bytes == sizeof(retry_text) ||
        control_bytes != retry_bytes ||
        memcmp(control_text, retry_text, control_bytes) != 0) {
        fprintf(stderr, "FAIL direct call retry changed valid output\n");
        ++failures;
    }
    mir_stream_close(control);
    mir_stream_close(retry);
    clear_liveness();
}

static void verify_aggregate_call_name_preflight(void)
{
    MirStream *control;
    MirStream *retry;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int result;
    int sid;

    sid = add_struct_def("verify_named_aggregate_result");
    struct_defs[sid - 1].size = 2;
    setup(3, 1, 1);
    mir.local_bytes = 2;
    mir.next_call_id = 1;
    mir.insns[1].opcode = MIR_CALL_AGGREGATE;
    mir.insns[1].type = type_add_ptr(make_struct_type(sid));
    mir.insns[1].immediate = -2;
    mir.insns[1].memory_size = 2;
    mir.insns[1].secondary_offset = 0;
    strcpy(mir.insns[1].name, "verify_named_aggregate_call");
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL aggregate call name verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    retry = mir_stream_open();
    if (control == NULL || retry == NULL) {
        fprintf(stderr, "FAIL aggregate call name stream allocation\n");
        ++failures;
        mir_stream_close(control);
        mir_stream_close(retry);
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    if (result != 1 || mir_stream_size(control) <= 0) {
        fprintf(stderr, "FAIL valid direct aggregate call name\n");
        ++failures;
    }
    label_id = first_label;
    mir.insns[1].name[0] = '\0';
    clear_liveness();
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL empty aggregate call name was not verifier-valid\n");
        ++failures;
    }
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    if (result != 0) {
        fprintf(stderr, "FAIL empty aggregate call name accepted\n");
        ++failures;
    }
    if (mir_stream_tell(retry) != 0 || mir_stream_size(retry) != 0) {
        fprintf(stderr, "FAIL empty aggregate call name emitted text\n");
        ++failures;
    }
    if (label_id != first_label) {
        fprintf(stderr, "FAIL empty aggregate call name consumed labels\n");
        ++failures;
    }
    strcpy(mir.insns[1].name, "verify_named_aggregate_call");
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    if (result != 1) {
        fprintf(stderr, "FAIL aggregate call retry after empty name\n");
        ++failures;
    }
    mir_stream_rewind(control);
    mir_stream_rewind(retry);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    retry_bytes = mir_stream_read(
        retry_text, 1, sizeof(retry_text), retry);
    if (control_bytes == sizeof(control_text) ||
        retry_bytes == sizeof(retry_text) ||
        control_bytes != retry_bytes ||
        memcmp(control_text, retry_text, control_bytes) != 0) {
        fprintf(stderr, "FAIL aggregate call retry changed valid output\n");
        ++failures;
    }
    mir_stream_close(control);
    mir_stream_close(retry);
    clear_liveness();
}

static void verify_spilled_branch_target_preflight_transaction(void)
{
    struct TargetMutation {
        int opcode;
        int label;
    };
    static const struct TargetMutation mutations[] = {
        { MIR_JUMP, -1 },
        { MIR_JUMP, 2 },
        { MIR_JUMP, 3 },
        { MIR_BRANCH_FALSE, -1 },
        { MIR_BRANCH_FALSE, 2 },
        { MIR_BRANCH_FALSE, 3 }
    };
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    size_t mutation;
    int result;
    int ok = 1;

    setup(5, 1, 3);
    mir.local_bytes = 2;
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL spilled branch target verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL spilled branch target stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]);
         ++mutation) {
        MirStream *retry = mir_stream_open();
        int mutation_ok = 1;

        if (retry == NULL) {
            ok = 0;
            break;
        }
        label_id = first_label;
        mir.insns[2].opcode = mutations[mutation].opcode;
        mir.insns[2].src1 =
            mutations[mutation].opcode == MIR_BRANCH_FALSE ? 0 : -1;
        mir.insns[2].label = mutations[mutation].label;
        mir_extrn_begin_attempt();
        result = mir_try_emit_spilled_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0;
        mutation_ok = mutation_ok && mir_stream_tell(retry) == 0 &&
                      mir_stream_size(retry) == 0;
        mutation_ok = mutation_ok && label_id == first_label;
        mutation_ok =
            mutation_ok && mir_spilled_cfg_emitted_frame_bytes() == 0;

        mir.insns[2].opcode = MIR_JUMP;
        mir.insns[2].src1 = -1;
        mir.insns[2].label = 1;
        mir_extrn_begin_attempt();
        result = mir_try_emit_spilled_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mir_stream_rewind(retry);
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text);
        mutation_ok = mutation_ok && control_bytes == retry_bytes;
        mutation_ok = mutation_ok &&
            memcmp(control_text, retry_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr,
                    "FAIL spilled branch target opcode=%s label=%d\n",
                    mir_opcode_name(mutations[mutation].opcode),
                    mutations[mutation].label);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }
    if (!ok) {
        fprintf(stderr, "FAIL spilled branch target preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_spilled_cfg_metadata_preflight_transaction(void)
{
    struct CfgMutation {
        const char *name;
        int successor_count;
        int successor0;
        int successor1;
    };
    static const struct CfgMutation mutations[] = {
        { "wrong branch target", 2, 4, 3 },
        { "missing branch fallthrough", 1, 5, 3 },
        { "wrong branch fallthrough predecessor", 2, 5, 4 },
        { "excess branch successor count", 3, 5, 3 }
    };
    MirStream *control;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int result;
    size_t mutation;
    int ok = 1;

    setup(8, 3, 2);
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_CONST;
    mir.insns[3].dst = 1;
    mir.insns[3].immediate = 11;
    mir.insns[4].opcode = MIR_RETURN;
    mir.insns[4].src1 = 1;
    mir.insns[5].opcode = MIR_LABEL;
    mir.insns[5].label = 1;
    mir.insns[6].opcode = MIR_CONST;
    mir.insns[6].dst = 2;
    mir.insns[6].immediate = 22;
    mir.insns[7].src1 = 2;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL spilled CFG metadata verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    if (control == NULL) {
        fprintf(stderr, "FAIL spilled CFG metadata stream allocation\n");
        ++failures;
        mir_stream_close(control);
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0 &&
         !mir_spilled_cfg_uses_exact_semantic_kernel();
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    for (mutation = 0;
         mutation < sizeof(mutations) / sizeof(mutations[0]); ++mutation) {
        MirStream *retry = mir_stream_open();
        int mutation_ok = retry != NULL;

        if (!mutation_ok)
            break;
        label_id = first_label;
        mir.insns[2].successor_count = mutations[mutation].successor_count;
        mir.insns[2].successors[0] = mutations[mutation].successor0;
        mir.insns[2].successors[1] = mutations[mutation].successor1;
        mir_extrn_begin_attempt();
        result = mir_try_emit_spilled_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 0 &&
            mir_stream_tell(retry) == 0 && mir_stream_size(retry) == 0 &&
            label_id == first_label &&
            mir_spilled_cfg_emitted_frame_bytes() == 0;

        mir.insns[2].successor_count = 2;
        mir.insns[2].successors[0] = 5;
        mir.insns[2].successors[1] = 3;
        mir_extrn_begin_attempt();
        result = mir_try_emit_spilled_scalar_cfg(retry);
        mutation_ok = mutation_ok && result == 1;
        mir_stream_rewind(retry);
        retry_bytes = mir_stream_read(
            retry_text, 1, sizeof(retry_text), retry);
        mutation_ok = mutation_ok && retry_bytes < sizeof(retry_text) &&
            retry_bytes == control_bytes &&
            memcmp(retry_text, control_text, control_bytes) == 0;
        if (!mutation_ok)
            fprintf(stderr, "FAIL spilled CFG metadata %s\n",
                    mutations[mutation].name);
        ok = ok && mutation_ok;
        mir_stream_close(retry);
    }

    setup(7, 2, 2);
    mir.insns[2].opcode = MIR_JUMP;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_CONST;
    mir.insns[3].dst = 1;
    mir.insns[3].immediate = 33;
    mir.insns[4].opcode = MIR_RETURN;
    mir.insns[4].src1 = 1;
    mir.insns[5].opcode = MIR_LABEL;
    mir.insns[5].label = 1;
    mir.insns[6].src1 = 0;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL spilled jump metadata verification control\n");
        ++failures;
        mir_stream_close(control);
        clear_liveness();
        return;
    }
    {
        MirStream *jump_control = mir_stream_open();
        MirStream *retry = mir_stream_open();
        char jump_control_text[2048];
        size_t jump_control_bytes = 0;
        int mutation_ok = jump_control != NULL && retry != NULL;

        if (mutation_ok) {
            label_id = first_label;
            result = mir_try_emit_spilled_scalar_cfg(jump_control);
            mutation_ok = result == 1;
            mir_stream_rewind(jump_control);
            jump_control_bytes = mir_stream_read(
                jump_control_text, 1, sizeof(jump_control_text),
                jump_control);
            mutation_ok = mutation_ok &&
                jump_control_bytes < sizeof(jump_control_text);

            label_id = first_label;
            mir.insns[2].successors[0] = 3;
            mir_extrn_begin_attempt();
            result = mir_try_emit_spilled_scalar_cfg(retry);
            mutation_ok = mutation_ok && result == 0 &&
                mir_stream_tell(retry) == 0 &&
                mir_stream_size(retry) == 0 &&
                label_id == first_label;
            mir.insns[2].successors[0] = 5;
            result = mir_try_emit_spilled_scalar_cfg(retry);
            mutation_ok = mutation_ok && result == 1;
            mir_stream_rewind(retry);
            retry_bytes = mir_stream_read(
                retry_text, 1, sizeof(retry_text), retry);
            mutation_ok = mutation_ok &&
                retry_bytes == jump_control_bytes &&
                memcmp(retry_text, jump_control_text,
                       jump_control_bytes) == 0;
        }
        mir_stream_close(jump_control);
        mir_stream_close(retry);
        if (!mutation_ok)
            fprintf(stderr, "FAIL spilled CFG metadata wrong jump target\n");
        ok = ok && mutation_ok;
    }
    if (!ok) {
        fprintf(stderr,
                "FAIL spilled CFG metadata preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();
}

static void verify_spilled_dimension_preflight_transaction(void)
{
    int invalid_dimensions[2];
    MirStream *stream;
    int first_label;
    int saved_next_label;
    int saved_next_value;
    int result;
    int mutation;
    int ok = 1;

    setup(3, 1, 1);
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL spilled dimension verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    stream = mir_stream_open();
    if (stream == NULL) {
        fprintf(stderr, "FAIL spilled dimension stream allocation\n");
        ++failures;
        clear_liveness();
        return;
    }
    first_label = label_id;
    saved_next_label = mir.next_label;
    saved_next_value = mir.next_value;
    invalid_dimensions[0] = mir.capacity + 1;
    invalid_dimensions[1] = INT_MAX;

    for (mutation = 0; mutation < 2; ++mutation) {
        mir.next_label = invalid_dimensions[mutation];
        result = mir_try_emit_spilled_scalar_cfg(stream);
        ok = ok && result == 0 &&
             mir_stream_tell(stream) == 0 && mir_stream_size(stream) == 0 &&
             label_id == first_label;
        mir.next_label = saved_next_label;

        mir.next_value = invalid_dimensions[mutation];
        result = mir_try_emit_spilled_scalar_cfg(stream);
        ok = ok && result == 0 &&
             mir_stream_tell(stream) == 0 && mir_stream_size(stream) == 0 &&
             label_id == first_label;
        mir.next_value = saved_next_value;
    }

    result = mir_try_emit_spilled_scalar_cfg(stream);
    ok = ok && result == 1 && mir_stream_size(stream) > 0;
    if (!ok) {
        fprintf(stderr, "FAIL spilled dimension preflight transaction\n");
        ++failures;
    }
    mir_stream_close(stream);
    clear_liveness();
}

static void verify_spilled_structural_preflight(void)
{
    struct Sym *callee;
    MirStream *control;
    MirStream *retry;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int saved_capacity;
    int result;
    int ok = 1;

    setup(6, 3, 1);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[5].src1 = 2;
    expect_spilled_candidate("structural valid control", 1);

    mir.insns[2].dst = 0;
    expect_spilled_candidate("duplicate definition rejection", 0);

    mir.insns[2].dst = 1;
    mir.insns[2].opcode = MIR_NOP;
    mir.insns[2].dst = -1;
    expect_spilled_candidate("undefined value rejection", 0);

    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[3].src1 = 2;
    expect_spilled_candidate("self use rejection", 0);

    setup(3, 1, 1);
    mir.next_call_id = -1;
    expect_spilled_candidate("negative call dimension rejection", 0);

    mir.next_call_id = mir.capacity + 1;
    expect_spilled_candidate("oversized call dimension rejection", 0);

    setup(3, 1, 1);
    saved_capacity = mir.capacity;
    mir.capacity = INT_MAX;
    mir.next_value = INT_MAX;
    expect_spilled_candidate("capacity-independent value bound", 0);
    mir.next_value = 1;
    mir.next_label = INT_MAX;
    expect_spilled_candidate("capacity-independent label bound", 0);
    mir.next_label = 1;
    mir.next_call_id = INT_MAX;
    expect_spilled_candidate("capacity-independent call bound", 0);
    mir.capacity = saved_capacity;

    setup(5, 2, 1);
    mir.insns[2].opcode = MIR_CALL;
    mir.insns[2].dst = 1;
    mir.insns[2].secondary_offset = 0;
    strcpy(mir.insns[2].name, "verify_spilled_call");
    mir.insns[4].src1 = 1;
    mir.next_call_id = 1;
    expect_spilled_candidate("call metadata valid control", 1);

    mir.insns[2].secondary_offset = 1;
    expect_spilled_candidate("out of range call ID rejection", 0);

    setup(5, 3, 1);
    mir.insns[1].opcode = MIR_CALL;
    mir.insns[1].dst = 0;
    mir.insns[1].secondary_offset = 0;
    strcpy(mir.insns[1].name, "verify_spilled_call_a");
    mir.insns[2].opcode = MIR_CALL;
    mir.insns[2].dst = 1;
    mir.insns[2].secondary_offset = 1;
    strcpy(mir.insns[2].name, "verify_spilled_call_b");
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].src1 = 2;
    mir.next_call_id = 2;
    expect_spilled_candidate("unique call IDs valid control", 1);

    mir.insns[2].secondary_offset = 0;
    expect_spilled_candidate("duplicate call ID rejection", 0);

    callee = add_global("verify_spilled_arg", TYPE_INT, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 1;
    callee->proto_types[0] = TYPE_INT;
    setup(5, 2, 1);
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].immediate = 0;
    mir.insns[2].secondary_offset = 0;
    mir.insns[3].opcode = MIR_CALL;
    mir.insns[3].dst = 1;
    mir.insns[3].secondary_offset = 0;
    strcpy(mir.insns[3].name, "verify_spilled_arg");
    mir.insns[4].src1 = 1;
    mir.next_call_id = 1;
    expect_spilled_candidate("argument metadata valid control", 1);

    mir.insns[3].src1 = 0;
    expect_spilled_candidate("direct call source rejection", 0);

    mir.insns[3].src1 = -1;
    mir.insns[2].type = TYPE_LONG;
    expect_spilled_candidate("argument ABI type rejection", 0);

    mir.insns[2].type = TYPE_INT;
    mir.insns[1].type = TYPE_LONG;
    expect_spilled_candidate("argument source type rejection", 0);

    mir.insns[1].type = TYPE_INT;
    mir.insns[2].opcode = MIR_NOP;
    mir.insns[2].src1 = -1;
    expect_spilled_candidate("missing prototype argument rejection", 0);

    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    callee->storage = SC_GLOBAL;
    expect_spilled_candidate("non-function callee rejection", 0);

    callee->storage = SC_FUNC;
    mir.insns[3].type = TYPE_CHAR;
    expect_spilled_candidate("call return ABI rejection", 0);

    mir.insns[3].type = TYPE_INT;
    mir.insns[2].secondary_offset = 1;
    expect_spilled_candidate("out of range argument ID rejection", 0);

    mir.insns[2].secondary_offset = 0;
    mir.insns[2].immediate = 1;
    expect_spilled_candidate("noncontiguous argument rejection", 0);

    mir.insns[2].immediate = 0;
    memset(mir.insns[3].name, 'x', sizeof(mir.insns[3].name));
    expect_spilled_candidate("unterminated call name rejection", 0);

    setup(6, 2, 1);
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].immediate = 0;
    mir.insns[2].secondary_offset = 0;
    mir.insns[3].opcode = MIR_ARG;
    mir.insns[3].src1 = 0;
    mir.insns[3].immediate = 0;
    mir.insns[3].secondary_offset = 0;
    mir.insns[4].opcode = MIR_CALL;
    mir.insns[4].dst = 1;
    mir.insns[4].secondary_offset = 0;
    strcpy(mir.insns[4].name, "verify_spilled_duplicate_arg");
    mir.insns[5].src1 = 1;
    mir.next_call_id = 1;
    expect_spilled_candidate("duplicate argument rejection", 0);

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
    expect_spilled_candidate("PHI metadata valid control", 1);

    mir.insns[9].phi_pred1 = 0;
    expect_spilled_candidate("non-predecessor PHI label rejection", 0);

    mir.insns[9].phi_pred1 = 1;
    mir.insns[9].src1 = 2;
    mir.insns[9].src2 = 1;
    expect_spilled_candidate("non-dominating PHI source rejection", 0);

    setup(3, 1, 1);
    mir.local_bytes = 2;
    mir.object_count = 1;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    mir.objects[0].storage = SC_LOCAL;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = INT_MAX;
    strcpy(mir.objects[0].name, "verify_spilled_offset");
    mir.insns[1].opcode = MIR_LOAD;
    mir.insns[1].object = 0;
    mir.insns[1].immediate = 1;
    strcpy(mir.insns[1].name, mir.objects[0].name);
    expect_spilled_candidate("overflowing object offset rejection", 0);

    setup(6, 3, 1);
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[5].src1 = 2;
    prepare_test_cfg_metadata();
    control = mir_stream_open();
    retry = mir_stream_open();
    if (control == NULL || retry == NULL) {
        fprintf(stderr, "FAIL spilled structural stream allocation\n");
        ++failures;
        mir_stream_close(control);
        mir_stream_close(retry);
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    mir.insns[2].opcode = MIR_NOP;
    mir.insns[2].dst = -1;
    label_id = first_label;
    mir_invalidate_use_cache();
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    ok = ok && result == 0 &&
         mir_stream_tell(retry) == 0 && mir_stream_size(retry) == 0 &&
         label_id == first_label &&
         mir_spilled_cfg_emitted_frame_bytes() == 0;

    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir_invalidate_use_cache();
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    ok = ok && result == 1;
    mir_stream_rewind(retry);
    retry_bytes = mir_stream_read(
        retry_text, 1, sizeof(retry_text), retry);
    ok = ok && retry_bytes < sizeof(retry_text) &&
         retry_bytes == control_bytes &&
         memcmp(retry_text, control_text, control_bytes) == 0;
    if (!ok) {
        fprintf(stderr, "FAIL spilled structural preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    mir_stream_close(retry);
    clear_liveness();
}

static void verify_spilled_declared_metadata_preflight(void)
{
    int control_label;

    setup(3, 1, 1);
    expect_spilled_candidate("declared metadata control", 1);
    control_label = label_id;

    mir.declared_count = -1;
    expect_spilled_candidate_transaction_rejection(
        "negative declared count",
        control_label);
    mir.declared_count = MAX_LOCALS + 1;
    expect_spilled_candidate_transaction_rejection(
        "oversized declared count",
        control_label);
    mir.declared_count = INT_MAX;
    expect_spilled_candidate_transaction_rejection(
        "extreme declared count",
        control_label);
    mir.declared_count = 1;
    memset(mir.declared_names[0], 'n', sizeof(mir.declared_names[0]));
    expect_spilled_candidate_transaction_rejection(
        "unterminated declared name",
        control_label);
    memset(mir.declared_names[0], 0, sizeof(mir.declared_names[0]));
    memset(mir.declared_link_names[0],
           'l',
           sizeof(mir.declared_link_names[0]));
    expect_spilled_candidate_transaction_rejection(
        "unterminated declared link name",
        control_label);
    memset(mir.declared_link_names[0],
           0,
           sizeof(mir.declared_link_names[0]));
    memset(mir.declared_runtime_stride_names[0],
           'r',
           sizeof(mir.declared_runtime_stride_names[0]));
    expect_spilled_candidate_transaction_rejection(
        "unterminated declared runtime stride name",
        control_label);

    setup(3, 1, 1);
    expect_spilled_candidate("declared metadata valid retry", 1);
}

static void verify_spilled_widened_call_argument_preflight(void)
{
    struct Sym *callee;
    MirStream *control;
    MirStream *retry;
    char control_text[4096];
    char retry_text[4096];
    size_t control_bytes;
    size_t retry_bytes;
    int control_label;
    int result;

    callee = add_global("verify_spilled_widened_arg", TYPE_INT, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 1;
    callee->proto_types[0] = TYPE_LONG;
    setup(5, 2, 1);
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_LONG;
    mir.insns[3].opcode = MIR_CALL;
    mir.insns[3].dst = 1;
    strcpy(mir.insns[3].name, callee->name);
    mir.insns[4].src1 = 1;
    mir.next_call_id = 1;
    expect_spilled_candidate("narrow source widened call argument control", 1);
    control_label = label_id;

    mir.insns[1].type = TYPE_FLOAT;
    expect_spilled_candidate_transaction_rejection(
        "float source cannot supply integer wide argument",
        control_label);
    mir.insns[1].type = TYPE_LONG;
    mir.insns[2].type = TYPE_INT;
    callee->proto_types[0] = TYPE_INT;
    expect_spilled_candidate_transaction_rejection(
        "wide source cannot supply narrow argument",
        control_label);
    mir.insns[1].type = TYPE_INT;
    mir.insns[2].type = TYPE_LONG;
    callee->proto_types[0] = TYPE_LONG;
    expect_spilled_candidate("narrow source widened call argument retry", 1);

    callee->proto_types[0] = TYPE_BOOL;
    setup(5, 2, 1);
    mir.insns[1].type = 0;
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_BOOL;
    mir.insns[3].opcode = MIR_CALL;
    mir.insns[3].dst = 1;
    strcpy(mir.insns[3].name, callee->name);
    mir.insns[4].src1 = 1;
    mir.next_call_id = 1;
    prepare_test_cfg_metadata();
    control = mir_stream_open();
    retry = mir_stream_open();
    if (control == NULL || retry == NULL) {
        fprintf(stderr,
                "FAIL spilled implicit-word argument stream allocation\n");
        ++failures;
        mir_stream_close(control);
        mir_stream_close(retry);
        clear_liveness();
        return;
    }
    control_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    if (result != 1 || mir_stream_size(control) == 0) {
        fprintf(stderr,
                "FAIL spilled implicit-word boolean argument control\n");
        ++failures;
    }
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);

    mir.insns[1].type = TYPE_LONG;
    label_id = control_label;
    mir_invalidate_use_cache();
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    if (result != 0 ||
        mir_stream_tell(retry) != 0 ||
        mir_stream_size(retry) != 0 ||
        label_id != control_label ||
        mir_spilled_cfg_emitted_frame_bytes() != 0) {
        fprintf(stderr,
                "FAIL spilled wide boolean argument transaction\n");
        ++failures;
    }

    mir.insns[1].type = 0;
    mir_invalidate_use_cache();
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    mir_stream_rewind(retry);
    retry_bytes = mir_stream_read(
        retry_text, 1, sizeof(retry_text), retry);
    if (result != 1 ||
        control_bytes >= sizeof(control_text) ||
        retry_bytes >= sizeof(retry_text) ||
        retry_bytes != control_bytes ||
        memcmp(retry_text, control_text, control_bytes) != 0) {
        fprintf(stderr,
                "FAIL spilled implicit-word boolean argument retry\n");
        ++failures;
    }
    mir_stream_close(control);
    mir_stream_close(retry);
    clear_liveness();
}

static void verify_spilled_aggregate_call_preflight(void)
{
    struct Sym *callee;
    int control_label;
    int struct_id;
    int struct_type;

    struct_id = add_struct_def("verify_spilled_aggregate_argument");
    struct_defs[struct_id - 1].size = 4;
    struct_type = make_struct_type(struct_id);
    callee = add_global("spilled_aggregate_target", struct_type, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 2;
    callee->proto_types[0] = struct_type;
    callee->proto_types[1] = TYPE_INT;

    setup(7, 3, 1);
    mir.return_type = TYPE_VOID;
    mir.local_bytes = 4;
    mir.aggregate_temp_bytes = 4;
    mir.object_count = 1;
    mir.objects[0].type = struct_type;
    mir.objects[0].storage = SC_LOCAL;
    mir.objects[0].offset = -4;
    strcpy(mir.objects[0].name, "aggregate_argument");
    mir.insns[1].opcode = MIR_ADDRESS;
    mir.insns[1].dst = 0;
    mir.insns[1].type = type_add_ptr(struct_type);
    mir.insns[1].object = 0;
    strcpy(mir.insns[1].name, mir.objects[0].name);
    mir.insns[2].opcode = MIR_ARG;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = struct_type;
    mir.insns[3].opcode = MIR_CONST;
    mir.insns[3].dst = 1;
    mir.insns[3].type = TYPE_INT;
    mir.insns[3].immediate = 7;
    mir.insns[4].opcode = MIR_ARG;
    mir.insns[4].src1 = 1;
    mir.insns[4].type = TYPE_INT;
    mir.insns[4].immediate = 1;
    mir.insns[5].opcode = MIR_CALL_AGGREGATE;
    mir.insns[5].dst = 2;
    mir.insns[5].type = type_add_ptr(struct_type);
    mir.insns[5].immediate = -8;
    mir.insns[5].memory_size = 4;
    strcpy(mir.insns[5].name, callee->name);
    mir.insns[6].src1 = -1;
    mir.next_call_id = 1;
    expect_spilled_candidate("aggregate direct call control", 1);
    control_label = label_id;

    mir.insns[4].opcode = MIR_NOP;
    mir.insns[4].src1 = -1;
    expect_spilled_candidate_transaction_rejection(
        "aggregate call missing argument",
        control_label);
    mir.insns[4].opcode = MIR_ARG;
    mir.insns[4].src1 = 1;
    mir.insns[3].type = TYPE_LONG;
    mir.insns[4].type = TYPE_LONG;
    expect_spilled_candidate_transaction_rejection(
        "aggregate fixed parameter type mismatch",
        control_label);
    mir.insns[4].type = TYPE_INT;
    expect_spilled_candidate_transaction_rejection(
        "aggregate int argument rejects long source",
        control_label);
    mir.insns[3].type = TYPE_FLOAT;
    expect_spilled_candidate_transaction_rejection(
        "aggregate int argument rejects float source",
        control_label);
    mir.insns[3].type = TYPE_INT;
    expect_spilled_candidate("aggregate direct call valid retry", 1);
}

static void verify_spilled_vla_size_preflight_transaction(void)
{
    MirStream *control;
    MirStream *retry;
    char control_text[2048];
    char retry_text[2048];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int result;
    int ok = 1;

    setup(4, 2, 1);
    mir.insns[2].opcode = MIR_VLA_SIZE;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = -2;
    mir.insns[3].src1 = 1;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL spilled VLA-size verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    retry = mir_stream_open();
    if (control == NULL || retry == NULL) {
        fprintf(stderr, "FAIL spilled VLA-size stream allocation\n");
        ++failures;
        mir_stream_close(control);
        mir_stream_close(retry);
        clear_liveness();
        return;
    }
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0 &&
         !mir_spilled_cfg_uses_exact_semantic_kernel();
    mir_stream_rewind(control);
    control_bytes = mir_stream_read(
        control_text, 1, sizeof(control_text), control);
    ok = ok && control_bytes < sizeof(control_text);

    label_id = first_label;
    mir.insns[2].immediate = 127;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    ok = ok && result == 0 &&
         mir_stream_tell(retry) == 0 && mir_stream_size(retry) == 0 &&
         label_id == first_label &&
         mir_spilled_cfg_emitted_frame_bytes() == 0;

    mir.insns[2].immediate = -2;
    mir_extrn_begin_attempt();
    result = mir_try_emit_spilled_scalar_cfg(retry);
    ok = ok && result == 1;
    mir_stream_rewind(retry);
    retry_bytes = mir_stream_read(
        retry_text, 1, sizeof(retry_text), retry);
    ok = ok && retry_bytes < sizeof(retry_text) &&
         retry_bytes == control_bytes &&
         memcmp(retry_text, control_text, control_bytes) == 0;
    if (!ok) {
        fprintf(stderr, "FAIL spilled VLA-size preflight transaction\n");
        ++failures;
    }
    mir_stream_close(control);
    mir_stream_close(retry);
    clear_liveness();
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

static void verify_scalar_dag_preflight_transaction(void)
{
    MirStream *control;
    MirStream *retry;
    char control_output[512];
    char retry_output[512];
    size_t control_bytes;
    size_t retry_bytes;
    int first_label;
    int result;
    int ok = 1;
    int saved_stack_check = opt_stack_check;

    setup(9, 7, 1);
    mir.insns[1].immediate = 2;
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 3;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '*';
    mir.insns[4].opcode = MIR_UNARY;
    mir.insns[4].dst = 3;
    mir.insns[4].src1 = 2;
    mir.insns[4].immediate = '!';
    mir.insns[5].opcode = MIR_CONST;
    mir.insns[5].dst = 4;
    mir.insns[5].immediate = 1;
    mir.insns[6].opcode = MIR_UNARY;
    mir.insns[6].dst = 5;
    mir.insns[6].src1 = 4;
    mir.insns[6].immediate = '+';
    mir.insns[7].opcode = MIR_BINARY;
    mir.insns[7].dst = 6;
    mir.insns[7].src1 = 3;
    mir.insns[7].src2 = 5;
    mir.insns[7].immediate = '+';
    mir.insns[8].src1 = 6;
    if (!mir_verify_and_dump()) {
        fprintf(stderr, "FAIL scalar DAG verification control\n");
        ++failures;
        clear_liveness();
        return;
    }
    control = mir_stream_open();
    retry = mir_stream_open();
    if (control == NULL || retry == NULL) {
        fprintf(stderr, "FAIL scalar DAG stream allocation\n");
        ++failures;
        mir_stream_close(control);
        mir_stream_close(retry);
        clear_liveness();
        return;
    }
    opt_stack_check = 0;
    first_label = label_id;
    mir_extrn_begin_attempt();
    result = mir_try_emit_scalar_dag(control);
    ok = ok && result == 1 && mir_stream_size(control) > 0;
    mir_stream_rewind(control);
    memset(control_output, 0, sizeof(control_output));
    control_bytes = mir_stream_read(
        control_output, 1, sizeof(control_output) - 1, control);
    ok = ok && control_bytes < sizeof(control_output);
    ok = ok && strstr(control_output, "\textrn __mulu\n\tcall __mulu\n") != NULL;

    label_id = first_label;
    mir.insns[6].immediate = '?';
    mir_extrn_begin_attempt();
    result = mir_try_emit_scalar_dag(retry);
    ok = ok && result == 0;
    ok = ok && mir_stream_tell(retry) == 0 && mir_stream_size(retry) == 0;
    ok = ok && label_id == first_label;

    mir.insns[6].immediate = '+';
    result = mir_try_emit_scalar_dag(retry);
    ok = ok && result == 1;
    mir_stream_rewind(retry);
    memset(retry_output, 0, sizeof(retry_output));
    retry_bytes = mir_stream_read(
        retry_output, 1, sizeof(retry_output) - 1, retry);
    ok = ok && retry_bytes < sizeof(retry_output);
    ok = ok && control_bytes == retry_bytes;
    ok = ok && memcmp(control_output, retry_output, control_bytes) == 0;
    if (!ok) {
        fprintf(stderr, "FAIL scalar DAG preflight transaction\n");
        ++failures;
    }
    opt_stack_check = saved_stack_check;
    mir_stream_close(control);
    mir_stream_close(retry);
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

static void verify_conditional_callable_prototypes(void)
{
    struct Sym left;
    struct Sym right;
    struct AstNode left_node;
    struct AstNode right_node;
    struct AstNode condition;
    struct AstNode callee;
    struct AstNode call;
    struct AstNode null_pointer;
    struct AstNode null_pointer_cast;
    struct AstNode left_address;
    struct AstNode right_address;
    struct AstNode nested_call;
    struct AstNode argument;
    struct AstNode *arguments[1];
    struct Sym left_result;
    struct Sym right_result;

    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    memset(&left_node, 0, sizeof(left_node));
    memset(&right_node, 0, sizeof(right_node));
    memset(&condition, 0, sizeof(condition));
    memset(&callee, 0, sizeof(callee));
    memset(&call, 0, sizeof(call));
    memset(&null_pointer, 0, sizeof(null_pointer));
    memset(&null_pointer_cast, 0, sizeof(null_pointer_cast));
    memset(&left_address, 0, sizeof(left_address));
    memset(&right_address, 0, sizeof(right_address));
    memset(&nested_call, 0, sizeof(nested_call));
    memset(&argument, 0, sizeof(argument));
    memset(&left_result, 0, sizeof(left_result));
    memset(&right_result, 0, sizeof(right_result));
    left.storage = SC_FUNC;
    strcpy(left.name, "conditional_left");
    left.type = TYPE_LONG;
    left.has_proto = 1;
    left.proto_nargs = 1;
    left.proto_types[0] = TYPE_LONG;
    right = left;
    strcpy(right.name, "conditional_right");
    left_node.kind = AST_IDENT;
    left_node.type = type_add_ptr(TYPE_LONG);
    left_node.sym = &left;
    left_node.sval = left.name;
    right_node.kind = AST_IDENT;
    right_node.type = type_add_ptr(TYPE_LONG);
    right_node.sym = &right;
    right_node.sval = right.name;
    callee.kind = AST_COND;
    condition.kind = AST_INT_LIT;
    condition.type = TYPE_INT;
    condition.ival = 1;
    callee.a = &condition;
    callee.b = &left_node;
    callee.c = &right_node;
    callee.type = type_add_ptr(TYPE_LONG);
    call.kind = AST_CALL;
    call.type = TYPE_LONG;
    call.a = &callee;
    argument.kind = AST_INT_LIT;
    argument.type = TYPE_LONG;
    argument.ival = 7;
    arguments[0] = &argument;
    call.list = arguments;
    call.list_len = 1;
    call.list_cap = 1;

    if (ast_indirect_call_proto_sym(&call) != &left) {
        fprintf(stderr, "FAIL matching conditional callback prototype\n");
        ++failures;
    }
    mir_begin_function(
        "conditional_call_snapshot", "_conditional_call_snapshot",
        EMIT_SINK_FINAL, 0, 0, 0);
    mir_capture_discarded_expr(&call);
    if (mir.next_call_id != 1 || mir.call_signature_capacity <= 0 ||
        !mir.call_signatures[0].present ||
        !mir.call_signatures[0].has_proto ||
        mir.call_signatures[0].parameter_count != 1 ||
        mir.call_signatures[0].parameter_types[0] != TYPE_LONG ||
        mir.call_signatures[0].return_type != TYPE_LONG) {
        fprintf(stderr, "FAIL conditional call signature snapshot\n");
        ++failures;
    }
    right.proto_types[0] = TYPE_INT;
    if (ast_indirect_call_proto_sym(&call) != NULL) {
        fprintf(stderr, "FAIL conflicting conditional callback argument type\n");
        ++failures;
    }
    right.proto_types[0] = TYPE_LONG;
    right.type = TYPE_INT;
    if (ast_indirect_call_proto_sym(&call) != NULL) {
        fprintf(stderr, "FAIL conflicting conditional callback return type\n");
        ++failures;
    }
    right.type = TYPE_LONG;
    right.proto_variadic = 1;
    if (ast_indirect_call_proto_sym(&call) != NULL) {
        fprintf(stderr, "FAIL conflicting conditional callback variadic type\n");
        ++failures;
    }
    right.proto_variadic = 0;
    right.has_proto = 0;
    if (ast_indirect_call_proto_sym(&call) != &left) {
        fprintf(stderr, "FAIL partly unprototyped conditional callback\n");
        ++failures;
    }
    left.has_proto = 0;
    if (ast_indirect_call_proto_sym(&call) != &right) {
        fprintf(stderr, "FAIL unprototyped conditional callback preservation\n");
        ++failures;
    }
    right.has_proto = 1;
    if (ast_indirect_call_proto_sym(&call) != &right) {
        fprintf(stderr, "FAIL compatible mixed conditional callback prototype\n");
        ++failures;
    }
    right.proto_types[0] = TYPE_CHAR;
    if (ast_indirect_call_proto_sym(&call) != NULL) {
        fprintf(stderr, "FAIL promoted mixed conditional callback conflict\n");
        ++failures;
    }
    left.has_proto = 1;
    left.proto_types[0] = TYPE_LONG;
    right.proto_types[0] = TYPE_LONG;
    null_pointer.kind = AST_INT_LIT;
    null_pointer.type = TYPE_INT;
    null_pointer.ival = 0;
    callee.c = &null_pointer;
    if (ast_indirect_call_proto_sym(&call) != &left) {
        fprintf(stderr, "FAIL null-arm conditional callback prototype\n");
        ++failures;
    }
    null_pointer_cast.kind = AST_CAST;
    null_pointer_cast.type = type_add_ptr(TYPE_VOID);
    null_pointer_cast.a = &null_pointer;
    callee.c = &null_pointer_cast;
    if (ast_indirect_call_proto_sym(&call) != &left) {
        fprintf(stderr, "FAIL cast-null-arm conditional callback prototype\n");
        ++failures;
    }
    callee.c = &right_node;
    left_result.storage = SC_LOCAL;
    left_result.type = type_add_ptr(TYPE_LONG);
    left_result.funcptr_return_type = TYPE_LONG;
    left_result.has_proto = 0;
    left_result.proto_nargs = 1;
    left_result.proto_types[0] = TYPE_LONG;
    right_result = left_result;
    right_result.has_proto = 1;
    left.type = type_add_ptr(TYPE_LONG);
    right.type = type_add_ptr(TYPE_LONG);
    left.funcptr_result_prototype = &left_result;
    right.funcptr_result_prototype = &right_result;
    if (ast_indirect_call_proto_sym(&call) != &right) {
        fprintf(stderr, "FAIL structural returned-callback prototype\n");
        ++failures;
    }
    nested_call.kind = AST_CALL;
    nested_call.a = &call;
    if (ast_indirect_call_proto_sym(&nested_call) != &right_result) {
        fprintf(stderr, "FAIL nested composite callback prototype\n");
        ++failures;
    }
    left.funcptr_result_prototype = NULL;
    right.funcptr_result_prototype = NULL;
    left.type = TYPE_LONG;
    right.type = TYPE_LONG;
    left.proto_nargs = 1;
    right.proto_nargs = 1;
    left.is_fastcall = 1;
    right.is_fastcall = 1;
    left.is_fastcall = 0;
    right.is_fastcall = 0;
    left.proto_nargs = 1;
    right.proto_nargs = 1;
    left_address.kind = AST_UNARY;
    left_address.op = '&';
    left_address.a = &left_node;
    right_address.kind = AST_UNARY;
    right_address.op = '&';
    right_address.a = &right_node;
    callee.b = &left_address;
    callee.c = &right_address;
    if (ast_indirect_call_proto_sym(&call) != &left) {
        fprintf(stderr, "FAIL addressed conditional callback prototype\n");
        ++failures;
    }
    {
        struct Sym *support_left =
            add_global("conditional_support_left", TYPE_LONG, SC_FUNC);
        struct Sym *support_right =
            add_global("conditional_support_right", TYPE_LONG, SC_FUNC);
        struct AstNode support_left_node;
        struct AstNode support_right_node;
        struct AstNode support_condition;
        struct AstNode support_callee;
        struct AstNode support_argument;
        struct AstNode support_call;
        struct AstNode *support_arguments[1];

        support_left->has_proto = 1;
        support_left->proto_nargs = 1;
        support_left->proto_types[0] = TYPE_LONG;
        support_right->has_proto = 1;
        support_right->proto_nargs = 1;
        support_right->proto_types[0] = TYPE_LONG;
        memset(&support_left_node, 0, sizeof(support_left_node));
        memset(&support_right_node, 0, sizeof(support_right_node));
        memset(&support_condition, 0, sizeof(support_condition));
        memset(&support_callee, 0, sizeof(support_callee));
        memset(&support_argument, 0, sizeof(support_argument));
        memset(&support_call, 0, sizeof(support_call));
        support_left_node.kind = AST_IDENT;
        support_left_node.type = type_add_ptr(TYPE_LONG);
        support_left_node.sym = support_left;
        support_left_node.sval = support_left->name;
        support_right_node.kind = AST_IDENT;
        support_right_node.type = type_add_ptr(TYPE_LONG);
        support_right_node.sym = support_right;
        support_right_node.sval = support_right->name;
        support_condition.kind = AST_INT_LIT;
        support_condition.type = TYPE_INT;
        support_condition.ival = 1;
        support_callee.kind = AST_COND;
        support_callee.type = type_add_ptr(TYPE_LONG);
        support_callee.a = &support_condition;
        support_callee.b = &support_left_node;
        support_callee.c = &support_right_node;
        support_argument.kind = AST_INT_LIT;
        support_argument.type = TYPE_LONG;
        support_argument.ival = 7;
        support_arguments[0] = &support_argument;
        support_call.kind = AST_CALL;
        support_call.type = TYPE_LONG;
        support_call.a = &support_callee;
        support_call.list = support_arguments;
        support_call.list_len = 1;
        support_call.list_cap = 1;
        if (!ast_call_indirect_supported(&support_call)) {
            fprintf(stderr, "FAIL matching conditional callback support\n");
            ++failures;
        }
        support_right->proto_types[0] = TYPE_INT;
        if (ast_call_indirect_supported(&support_call)) {
            fprintf(stderr, "FAIL incompatible conditional callback support\n");
            ++failures;
        }
        support_right->proto_types[0] = TYPE_LONG;
        support_right->is_fastcall = 1;
        if (ast_call_indirect_supported(&support_call)) {
            fprintf(stderr, "FAIL mixed fastcall conditional callback support\n");
            ++failures;
        }
        support_left->is_fastcall = 1;
        if (ast_call_indirect_supported(&support_call)) {
            fprintf(stderr, "FAIL conditional fastcall callback support\n");
            ++failures;
        }
    }
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
    verify_ast_binary_folds();
    verify_conditional_callable_prototypes();
    verify_ast_assignment_support();
    verify_call_lowering_preflight();
    verify_expression_lowering_preflight();
    verify_large_expression_preflight();
    verify_call_fast_path_preflight();
    verify_diamond_edge_liveness();
    verify_immediate_phi_consumer_forwarding();
    verify_call_argument_liveness();
    verify_mir_stream_io();
    verify_mir_stream_seek_transaction();
    verify_ast_kind_names();
    verify_simple_mir_feature_queries();
    verify_parameter_emitters();
    verify_member_metadata_and_address();
    verify_deferred_function_pointer_metadata();
    verify_deferred_direct_call_conversion();
    verify_deferred_binary_conversion();
    verify_deferred_metadata_coordinates();
    verify_deferred_metadata_alias_bounds();
    verify_deferred_metadata_call_ordering();
    verify_five_call_arguments();
    verify_spilled_feature_defaults();
    verify_homed_parameter_preflight_transaction();
    verify_homed_value_operand_preflight_transaction();
    verify_homed_branch_target_preflight_transaction();
    verify_homed_dimension_preflight_transaction();
    verify_homed_noncall_preflight_transaction();
    verify_homed_noncall_dominance_transaction();
    verify_homed_memory_preflight_transaction();
    verify_homed_phi_preflight_transaction();
    verify_homed_call_preflight_transaction();
    verify_homed_call_dominance_preflight_transaction();
    verify_homed_unused_value_operands();
    verify_spilled_preflight_rejection();
    verify_spilled_value_operand_preflight_transaction();
    verify_spilled_unused_value_operands();
    verify_va_arg_offset_preflight();
    verify_direct_call_name_preflight();
    verify_aggregate_call_name_preflight();
    verify_spilled_branch_target_preflight_transaction();
    verify_spilled_cfg_metadata_preflight_transaction();
    verify_spilled_dimension_preflight_transaction();
    verify_spilled_structural_preflight();
    verify_spilled_declared_metadata_preflight();
    verify_spilled_widened_call_argument_preflight();
    verify_spilled_aggregate_call_preflight();
    verify_spilled_vla_size_preflight_transaction();
    verify_immediate_phi_return_forwarding();
    verify_common_expression_elimination();
    verify_scalar_dag_emission();
    verify_scalar_dag_preflight_transaction();
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
    setup(6, 2, 2);
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 1;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    mir.insns[4].opcode = MIR_PHI;
    mir.insns[4].dst = 1;
    mir.insns[4].src1 = 0;
    mir.insns[4].src2 = 0;
    mir.insns[4].phi_pred1 = 0;
    mir.insns[4].phi_pred2 = 1;
    mir.insns[5].src1 = 1;
    expect_verification("duplicate CFG arcs are one PHI predecessor", 0);
    setup(7, 2, 3);
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 2;
    mir.insns[3].opcode = MIR_LABEL;
    mir.insns[3].label = 1;
    mir.insns[4].opcode = MIR_LABEL;
    mir.insns[4].label = 2;
    mir.insns[5].opcode = MIR_PHI;
    mir.insns[5].dst = 1;
    mir.insns[5].src1 = 0;
    mir.insns[5].src2 = 0;
    mir.insns[5].phi_pred1 = 1;
    mir.insns[5].phi_pred2 = 2;
    mir.insns[6].src1 = 1;
    expect_verification("duplicate CFG arcs cannot fill both PHI slots", 0);
    setup(11, 2, 5);
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    mir.insns[2].src1 = 0;
    mir.insns[2].label = 1;
    mir.insns[4].opcode = MIR_LABEL;
    mir.insns[4].label = 1;
    mir.insns[5].opcode = MIR_LABEL;
    mir.insns[5].label = 2;
    mir.insns[6].opcode = MIR_LABEL;
    mir.insns[6].label = 4;
    mir.insns[7].opcode = MIR_PHI;
    mir.insns[7].dst = 1;
    mir.insns[7].src1 = 0;
    mir.insns[7].src2 = 0;
    mir.insns[7].phi_pred1 = 1;
    mir.insns[7].phi_pred2 = 3;
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 3;
    mir.insns[9].opcode = MIR_JUMP;
    mir.insns[9].label = 2;
    expect_verification("shared PHI entry alias accepts multiple arcs", 1);
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
    mir.insns[9].phi_pred2 = 0;
    expect_verification("unreachable PHI predecessor must still be real", 0);
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
    callee = add_global("cross_call_home_target", TYPE_INT, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 0;
    setup(5, 3, 1);
    mir.next_call_id = 1;
    mir.insns[2].opcode = MIR_CALL;
    mir.insns[2].dst = 1;
    mir.insns[2].secondary_offset = 0;
    strcpy(mir.insns[2].name, callee->name);
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 1;
    mir.insns[3].src2 = 0;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].src1 = 2;
    expect_verification("call-crossing allocation control", 1);
    if (mir.allocation_colors[0] != MIR_COLOR_IY &&
        mir.allocation_spills[0] < 0) {
        fprintf(stderr, "FAIL caller-saved home across call\n");
        ++failures;
    }
    callee = add_global("guarded_call_target", TYPE_VOID, SC_FUNC);
    callee->has_proto = 1;
    callee->proto_nargs = 0;
    {
        MirStream *output;
        char text[2048];
        size_t bytes;
        const char *push;
        const char *call;
        const char *pop;
        int emitted;

        diamond();
        mir.count = 12;
        mir.next_call_id = 1;
        mir.insns[11] = mir.insns[10];
        mir.insns[10] = mir.insns[9];
        memset(&mir.insns[9], 0, sizeof(mir.insns[9]));
        mir.insns[9].opcode = MIR_CALL;
        mir.insns[9].src1 = -1;
        mir.insns[9].src2 = -1;
        mir.insns[9].dst = -1;
        mir.insns[9].object = -1;
        mir.insns[9].label = -1;
        mir.insns[9].phi_pred1 = -1;
        mir.insns[9].phi_pred2 = -1;
        mir.insns[9].type = TYPE_VOID;
        mir.insns[9].secondary_offset = 0;
        strcpy(mir.insns[9].name, callee->name);
        if (!mir_verify_and_dump()) {
            fprintf(stderr, "FAIL guarded call allocation control\n");
            ++failures;
        } else if (mir.allocation_colors[3] != MIR_COLOR_DE) {
            fprintf(stderr, "FAIL late PHI guarded call allocation\n");
            ++failures;
        } else {
            output = mir_stream_open();
            if (output == NULL)
                fatal("cannot open guarded call output");
            mir_extrn_begin_attempt();
            emitted = mir_try_emit_homed_scalar_cfg(output);
            mir_stream_rewind(output);
            bytes = mir_stream_read(text, 1, sizeof(text) - 1, output);
            text[bytes] = '\0';
            push = strstr(text, "\tpush de\n");
            call = strstr(text, "\tcall _guarded_call_target\n");
            pop = call != NULL ? strstr(call, "\tpop de\n") : NULL;
            if (!emitted || bytes >= sizeof(text) - 1 ||
                push == NULL || call == NULL || pop == NULL ||
                push >= call || call >= pop) {
                fprintf(stderr, "FAIL guarded call DE preservation\n");
                ++failures;
            }
            mir_stream_close(output);
        }
        clear_liveness();
    }
    {
        unsigned char rematerializable[4] = {0, 1, 1, 0};
        MirStream *output;
        char text[4096];
        size_t bytes;
        const char *push;
        const char *call;
        const char *pop;
        int emitted;

        diamond();
        mir.count = 12;
        mir.return_type = TYPE_LONG;
        mir.next_call_id = 1;
        mir.insns[4].type = TYPE_LONG;
        mir.insns[7].type = TYPE_LONG;
        mir.insns[11] = mir.insns[10];
        mir.insns[11].type = TYPE_LONG;
        mir.insns[10] = mir.insns[9];
        mir.insns[10].type = TYPE_LONG;
        memset(&mir.insns[9], 0, sizeof(mir.insns[9]));
        mir.insns[9].opcode = MIR_CALL;
        mir.insns[9].src1 = -1;
        mir.insns[9].src2 = -1;
        mir.insns[9].dst = -1;
        mir.insns[9].object = -1;
        mir.insns[9].label = -1;
        mir.insns[9].phi_pred1 = -1;
        mir.insns[9].phi_pred2 = -1;
        mir.insns[9].type = TYPE_VOID;
        mir.insns[9].secondary_offset = 0;
        strcpy(mir.insns[9].name, callee->name);
        if (!mir_verify_and_dump()) {
            fprintf(stderr, "FAIL wide guarded call allocation control\n");
            ++failures;
        } else if (!mir_probe_wide_colors_for_homed(
                       rematerializable, 1)) {
            fprintf(stderr, "FAIL wide guarded call allocation probe\n");
            ++failures;
        } else if (mir.allocation_colors[3] != MIR_COLOR_BC_IY) {
            fprintf(stderr, "FAIL late wide PHI guarded call allocation\n");
            ++failures;
        } else {
            output = mir_stream_open();
            if (output == NULL)
                fatal("cannot open wide guarded call output");
            mir_extrn_begin_attempt();
            emitted = mir_try_emit_compacted_regional_homed_cfg(output);
            mir_stream_rewind(output);
            bytes = mir_stream_read(text, 1, sizeof(text) - 1, output);
            text[bytes] = '\0';
            push = strstr(text, "\tpush iy\n\tpush bc\n");
            call = push != NULL
                ? strstr(push, "\tcall _guarded_call_target\n")
                : NULL;
            pop = call != NULL
                ? strstr(call, "\tpop bc\n\tpop iy\n")
                : NULL;
            if (!emitted || bytes >= sizeof(text) - 1 ||
                push == NULL || call == NULL || pop == NULL ||
                push >= call || call >= pop) {
                fprintf(stderr, "FAIL guarded call BC:IY preservation\n");
                ++failures;
            }
            mir_stream_close(output);
        }
        clear_liveness();
    }
    {
        unsigned char rematerializable[4] = {1, 0, 0, 0};

        setup(6, 4, 1);
        mir.return_type = TYPE_LONG;
        mir.next_call_id = 1;
        mir.insns[1].type = TYPE_LONG;
        mir.insns[2].opcode = MIR_BINARY;
        mir.insns[2].dst = 1;
        mir.insns[2].src1 = 0;
        mir.insns[2].src2 = 0;
        mir.insns[2].type = TYPE_LONG;
        mir.insns[2].immediate = '+';
        mir.insns[2].secondary_offset = TYPE_LONG;
        mir.insns[3].opcode = MIR_CALL;
        mir.insns[3].dst = 2;
        mir.insns[3].secondary_offset = 0;
        strcpy(mir.insns[3].name, callee->name);
        mir.insns[4].opcode = MIR_BINARY;
        mir.insns[4].dst = 3;
        mir.insns[4].src1 = 1;
        mir.insns[4].src2 = 0;
        mir.insns[4].type = TYPE_LONG;
        mir.insns[4].immediate = '+';
        mir.insns[4].secondary_offset = TYPE_LONG;
        mir.insns[5].src1 = 3;
        mir.insns[5].type = TYPE_LONG;
        if (!mir_verify_and_dump()) {
            fprintf(stderr, "FAIL wide call-crossing allocation control\n");
            ++failures;
        } else if (!mir_probe_wide_colors_for_homed(
                       rematerializable, 1)) {
            fprintf(stderr, "FAIL wide call-crossing allocation probe\n");
            ++failures;
        } else if (mir.allocation_spills[1] < 0) {
            fprintf(stderr,
                    "FAIL wide value retained caller-clobbered home across call\n");
            ++failures;
        }
        clear_liveness();
    }
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
    setup_phi_indirect_call(1, 0, 1, 0, TYPE_LONG);
    expect_verification("PHI callback rejects excess argument", 0);
    setup_phi_indirect_call(1, 1, 1, 1, TYPE_LONG);
    expect_verification("PHI callback accepts matching prototype", 1);
    mir.insns[10].type = TYPE_INT;
    mir.insns[11].type = TYPE_INT;
    expect_verification("PHI callback rejects incorrect argument type", 0);
    setup_phi_indirect_call(1, 1, 1, 2, TYPE_LONG);
    expect_verification("conflicting PHI callback prototypes remain unknown", 1);
    setup_phi_indirect_call(1, 1, 0, 0, TYPE_LONG);
    expect_verification("partly unprototyped PHI callback remains unknown", 1);
    setup_phi_indirect_call(0, 0, 1, 1, TYPE_LONG);
    expect_verification("reverse unprototyped PHI callback remains unknown", 1);
    setup_phi_indirect_call(1, 1, 1, 1, TYPE_LONG);
    mir.declared_proto_variadic[1] = 1;
    expect_verification("variadic PHI callback conflict remains unknown", 1);
    mir.declared_proto_variadic[1] = 0;
    mir.declared_funcptr_return_types[1] = TYPE_LONG;
    expect_verification("return-type PHI callback conflict remains unknown", 1);
    mir.declared_funcptr_return_types[1] = TYPE_INT;
    mir.declared_proto_types[1][0] = TYPE_INT;
    expect_verification("parameter-type PHI callback conflict remains unknown", 1);
    {
        struct Sym *left_function =
            add_global("phi_direct_left", TYPE_INT, SC_FUNC);
        struct Sym *right_function =
            add_global("phi_direct_right", TYPE_INT, SC_FUNC);

        left_function->has_proto = 1;
        left_function->proto_nargs = 0;
        right_function->has_proto = 1;
        right_function->proto_nargs = 0;
        setup_phi_indirect_call(0, 0, 0, 0, TYPE_LONG);
        mir.insns[4].opcode = MIR_ADDRESS;
        strcpy(mir.insns[4].name, left_function->name);
        mir.insns[7].opcode = MIR_ADDRESS;
        strcpy(mir.insns[7].name, right_function->name);
        expect_verification("PHI function designators reject excess argument", 0);
    }
    {
        struct Sym recorded_callback;
        struct Sym *fallback_callback;

        memset(&recorded_callback, 0, sizeof(recorded_callback));
        recorded_callback.type = TYPE_INT | TYPE_PTR;
        recorded_callback.storage = SC_LOCAL;
        recorded_callback.is_funcptr = 1;
        recorded_callback.funcptr_return_type = TYPE_INT;
        recorded_callback.has_proto = 1;
        recorded_callback.proto_nargs = 1;
        recorded_callback.proto_types[0] = TYPE_LONG;
        setup_recorded_indirect_call(&recorded_callback, TYPE_INT);
        expect_verification("recorded indirect call argument ABI", 0);
        mir.insns[3].type = TYPE_LONG;
        expect_verification("recorded indirect call matching ABI", 1);
        mir.insns[3].opcode = MIR_NOP;
        mir.insns[3].src1 = -1;
        expect_verification("recorded indirect call arity", 0);
        fallback_callback = add_global(
            "recorded_fallback_callback", TYPE_INT | TYPE_PTR, SC_GLOBAL);
        fallback_callback->is_funcptr = 1;
        fallback_callback->funcptr_return_type = TYPE_INT;
        fallback_callback->has_proto = 1;
        fallback_callback->proto_nargs = 1;
        fallback_callback->proto_types[0] = TYPE_LONG;
        recorded_callback.has_proto = 0;
        setup_recorded_indirect_call(&recorded_callback, TYPE_INT);
        mir.insns[2].opcode = MIR_LOAD;
        strcpy(mir.insns[2].name, fallback_callback->name);
        expect_verification(
            "recorded unprototyped call overrides inferred prototype", 1);
    }
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