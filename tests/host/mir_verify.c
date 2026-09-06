#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"

static int failures;

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
    free(mir.live_in);
    free(mir.live_out);
    mir.live_in = NULL;
    mir.live_out = NULL;
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

int main(void)
{
    struct Sym *callee;
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
    printf("MIR verifier failures=%d\n", failures);
    return failures != 0;
}