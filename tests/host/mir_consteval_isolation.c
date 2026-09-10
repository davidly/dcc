#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"
#include "dcc_mir_machine_internal.h"

enum ConstevalMemoryCase {
    CONSTEVAL_DIRECT_STORE,
    CONSTEVAL_INDIRECT_LOAD,
    CONSTEVAL_INDIRECT_STORE
};

static void initialize_instruction(struct MirInsn *insn)
{
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

static void setup_constant_memory_case(
    const char *name, enum ConstevalMemoryCase memory_case,
    int malformed)
{
    struct Sym *function;
    int instruction;

    function = add_global(name, TYPE_INT, SC_FUNC);
    function->is_defined = 1;
    mir_begin_function(name, name, EMIT_SINK_FINAL, 0, 0, 0);
    mir.return_type = TYPE_INT;
    mir.count = memory_case == CONSTEVAL_INDIRECT_LOAD ? 19 : 20;
    mir.next_value = memory_case == CONSTEVAL_INDIRECT_LOAD ? 9 : 10;
    mir.next_label = 3;
    mir.object_count = 2;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    memset(&mir.objects[1], 0, sizeof(mir.objects[1]));
    strcpy(mir.objects[0].name, "value");
    mir.objects[0].storage = SC_LOCAL;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = -2;
    strcpy(mir.objects[1].name, "counter");
    mir.objects[1].storage = SC_LOCAL;
    mir.objects[1].type = TYPE_INT;
    mir.objects[1].offset = -4;
    for (instruction = 0; instruction < mir.count; ++instruction)
        initialize_instruction(&mir.insns[instruction]);

    mir.insns[0].opcode = MIR_LABEL;
    mir.insns[0].label = 0;
    mir.insns[1].opcode = MIR_CONST;
    mir.insns[1].dst = 0;
    mir.insns[1].immediate = 0;
    mir.insns[2].opcode = MIR_STORE;
    mir.insns[2].src1 = 0;
    mir.insns[2].object = 1;
    mir.insns[2].memory_size = 2;
    mir.insns[3].opcode = MIR_CONST;
    mir.insns[3].dst = 1;
    mir.insns[3].immediate =
        memory_case == CONSTEVAL_INDIRECT_STORE ? 0x1200 : 0x1234;
    mir.insns[4].opcode = MIR_STORE;
    mir.insns[4].src1 = 1;
    mir.insns[4].object = 0;
    mir.insns[4].memory_size = 2;
    mir.insns[5].opcode = MIR_ADDRESS;
    mir.insns[5].dst = 2;
    mir.insns[5].object = 0;
    mir.insns[5].type = type_add_ptr(TYPE_INT);
    strcpy(mir.insns[5].name, "value");
    mir.insns[6].opcode = MIR_LABEL;
    mir.insns[6].label = 1;
    mir.insns[7].opcode = MIR_LOAD;
    mir.insns[7].dst = 3;
    mir.insns[7].object = 1;
    mir.insns[8].opcode = MIR_CONST;
    mir.insns[8].dst = 4;
    mir.insns[8].immediate = 1;
    mir.insns[9].opcode = MIR_BINARY;
    mir.insns[9].dst = 5;
    mir.insns[9].src1 = 3;
    mir.insns[9].src2 = 4;
    mir.insns[9].immediate = '<';
    mir.insns[9].secondary_offset = TYPE_INT;
    mir.insns[10].opcode = MIR_BRANCH_FALSE;
    mir.insns[10].src1 = 5;
    mir.insns[10].label = 2;

    if (memory_case == CONSTEVAL_DIRECT_STORE) {
        mir.insns[11].opcode = MIR_CONST;
        mir.insns[11].dst = 6;
        mir.insns[11].immediate = 0x1234;
        mir.insns[12].opcode = MIR_STORE;
        mir.insns[12].src1 = 6;
        mir.insns[12].object = 0;
        mir.insns[12].memory_size = 2;
        if (malformed)
            mir.insns[12].type = TYPE_CHAR;
        mir.insns[13].opcode = MIR_CONST;
        mir.insns[13].dst = 7;
        mir.insns[13].immediate = 1;
        mir.insns[14].opcode = MIR_BINARY;
        mir.insns[14].dst = 8;
        mir.insns[14].src1 = 3;
        mir.insns[14].src2 = 7;
        mir.insns[14].immediate = '+';
        mir.insns[14].secondary_offset = TYPE_INT;
        mir.insns[15].opcode = MIR_STORE;
        mir.insns[15].src1 = 8;
        mir.insns[15].object = 1;
        mir.insns[15].memory_size = 2;
        mir.insns[16].opcode = MIR_JUMP;
        mir.insns[16].label = 1;
        mir.insns[17].opcode = MIR_LABEL;
        mir.insns[17].label = 2;
        mir.insns[18].opcode = MIR_LOAD;
        mir.insns[18].dst = 9;
        mir.insns[18].object = 0;
        mir.insns[19].opcode = MIR_RETURN;
        mir.insns[19].src1 = 9;
    } else {
        if (memory_case == CONSTEVAL_INDIRECT_LOAD) {
            mir.insns[11].opcode = MIR_LOAD_INDIRECT;
            mir.insns[11].dst = 6;
            mir.insns[11].src1 = 2;
            mir.insns[11].memory_size = 2;
        } else {
            mir.insns[11].opcode = MIR_CONST;
            mir.insns[11].dst = 6;
            mir.insns[11].immediate = 0x3456;
            mir.insns[12].opcode = MIR_STORE_INDIRECT;
            mir.insns[12].src1 = 2;
            mir.insns[12].src2 = 6;
            mir.insns[12].memory_size = 2;
        }
        if (malformed)
            mir.insns[memory_case == CONSTEVAL_INDIRECT_LOAD ? 11 : 12]
                .bit_width = 8;
        instruction =
            memory_case == CONSTEVAL_INDIRECT_LOAD ? 12 : 13;
        mir.insns[instruction].opcode = MIR_CONST;
        mir.insns[instruction].dst = 7;
        mir.insns[instruction].immediate = 1;
        ++instruction;
        mir.insns[instruction].opcode = MIR_BINARY;
        mir.insns[instruction].dst = 8;
        mir.insns[instruction].src1 = 3;
        mir.insns[instruction].src2 = 7;
        mir.insns[instruction].immediate = '+';
        mir.insns[instruction].secondary_offset = TYPE_INT;
        ++instruction;
        mir.insns[instruction].opcode = MIR_STORE;
        mir.insns[instruction].src1 = 8;
        mir.insns[instruction].object = 1;
        mir.insns[instruction].memory_size = 2;
        ++instruction;
        mir.insns[instruction].opcode = MIR_JUMP;
        mir.insns[instruction].label = 1;
        ++instruction;
        mir.insns[instruction].opcode = MIR_LABEL;
        mir.insns[instruction].label = 2;
        ++instruction;
        if (memory_case == CONSTEVAL_INDIRECT_STORE) {
            mir.insns[instruction].opcode = MIR_LOAD;
            mir.insns[instruction].dst = 9;
            mir.insns[instruction].object = 0;
        }
        ++instruction;
        mir.insns[instruction].opcode = MIR_RETURN;
        mir.insns[instruction].src1 =
            memory_case == CONSTEVAL_INDIRECT_LOAD ? 6 : 9;
    }
}

static int run_constant_memory_case(
    const char *name, enum ConstevalMemoryCase memory_case,
    int malformed, int expected_result)
{
    MirStream *stream = mir_stream_open();
    char output[128];
    size_t bytes;
    int accepted;
    int ok;

    if (stream == NULL)
        fatal("cannot create constant evaluator isolation stream");
    setup_constant_memory_case(name, memory_case, malformed);
    accepted = mir_try_emit_constant_folding_kernels(stream);
    mir_stream_rewind(stream);
    memset(output, 0, sizeof(output));
    bytes = mir_stream_read(output, 1, sizeof(output) - 1, stream);
    ok = malformed
        ? accepted == 0 && bytes == 0
        : accepted == 1 &&
          strstr(output, "\tld hl,") != NULL &&
          strstr(output, expected_result == 0x1234
              ? "\tld hl,4660\n" : "\tld hl,13398\n") != NULL;
    mir_stream_close(stream);
    return ok;
}

int main(void)
{
    int ok = 1;

    if (!run_constant_memory_case(
            "consteval_store_control",
            CONSTEVAL_DIRECT_STORE, 0, 0x1234)) {
        fprintf(stderr, "direct store control failed\n");
        ok = 0;
    }
    if (!run_constant_memory_case(
            "consteval_store_type",
            CONSTEVAL_DIRECT_STORE, 1, 0)) {
        fprintf(stderr, "store type mutation was accepted\n");
        ok = 0;
    }
    if (!run_constant_memory_case(
            "consteval_load_control",
            CONSTEVAL_INDIRECT_LOAD, 0, 0x1234)) {
        fprintf(stderr, "indirect load control failed\n");
        ok = 0;
    }
    if (!run_constant_memory_case(
            "consteval_load_bitfield",
            CONSTEVAL_INDIRECT_LOAD, 1, 0)) {
        fprintf(stderr, "indirect load bit-field mutation was accepted\n");
        ok = 0;
    }
    if (!run_constant_memory_case(
            "consteval_store_indirect_control",
            CONSTEVAL_INDIRECT_STORE, 0, 0x3456)) {
        fprintf(stderr, "indirect store control failed\n");
        ok = 0;
    }
    if (!run_constant_memory_case(
            "consteval_store_indirect_bitfield",
            CONSTEVAL_INDIRECT_STORE, 1, 0)) {
        fprintf(stderr, "indirect store bit-field mutation was accepted\n");
        ok = 0;
    }
    return ok ? 0 : 1;
}
