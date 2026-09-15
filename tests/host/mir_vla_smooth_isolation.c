#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"
#include "dcc_mir_machine_internal.h"

struct VlaInsnFixture {
    int instruction;
    int dst;
    int src1;
    int src2;
    int type;
    long immediate;
    int label;
    int phi_pred1;
    int phi_pred2;
    int object;
    int memory_size;
    int memory_flags;
    int secondary_offset;
    const char *name;
};

static const int vla_opcodes[141] = {
    MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_NOP,
    MIR_CONST, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
    MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
    MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE,
    MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
    MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_STORE, MIR_NOP,
    MIR_CONST, MIR_STORE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
    MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
    MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
    MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
    MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
    MIR_LOAD, MIR_NOP, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
    MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
    MIR_STORE, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LOAD,
    MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
    MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_LOAD, MIR_BINARY,
    MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
    MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
    MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LABEL,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
    MIR_LOAD, MIR_RETURN
};

static const struct VlaInsnFixture vla_semantic_insns[] = {
    {0, -1, -1, -1, 0, 0, 0, -1, -1, -1, 0, 0, 0, ""},
    {1, 0, -1, -1, TYPE_INT, 0, -1, -1, -1, 0, 0, 0, 0, "n"},
    {2, 1, -1, -1, TYPE_INT, 0, -1, -1, -1, 1, 0, 0, 0, "w"},
    {3, 2, -1, -1, TYPE_INT | TYPE_PTR, 0, -1, -1, -1, 2, 0, 0, 0, "src"},
    {4, 3, -1, -1, TYPE_INT | TYPE_PTR, 0, -1, -1, -1, 3, 0, 0, 0, "dst"},
    {6, 5, -1, -1, TYPE_LONG, 0, -1, -1, -1, -1, 0, 0, 0, ""},
    {7, -1, 5, -1, TYPE_LONG, 0, 5, -1, -1, 4, 4, 128, 0, "changed"},
    {9, 7, -1, -1, TYPE_INT, 2, -1, -1, -1, -1, 0, 0, 0, ""},
    {10, 8, 1, 7, TYPE_INT, '/', -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {11, -1, 8, -1, TYPE_INT, 0, 8, -1, -1, 5, 2, 128, 0, "half"},
    {24, 88, -1, -1, TYPE_INT, 0, -1, -1, -1, -1, 0, 0, 0, ""},
    {25, -1, 88, -1, TYPE_INT, 0, -1, -1, -1, 6, 2, 0, 0, "i#0#0"},
    {26, -1, -1, -1, 0, 0, 1, -1, -1, -1, 0, 0, 0, ""},
    {33, 22, 88, 87, TYPE_INT, 0, -1, 0, 3, 6, 0, 0, 0, "i#0#0"},
    {39, 28, 22, 0, TYPE_INT, '<', -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {40, -1, 28, -1, 0, 0, 2, -1, -1, -1, 0, 0, 0, ""},
    {43, 90, -1, -1, TYPE_LONG, 0, -1, -1, -1, -1, 0, 0, 0, ""},
    {44, -1, 90, -1, TYPE_LONG, 0, -1, -1, -1, 7, 4, 0, 0, "sum#b1#0"},
    {46, 91, -1, -1, TYPE_INT, 0, -1, -1, -1, -1, 0, 0, 0, ""},
    {47, -1, 91, -1, TYPE_INT, 0, -1, -1, -1, 8, 2, 0, 0, "count#b1#1"},
    {51, 94, 22, 8, TYPE_INT, '-', -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {52, -1, 94, -1, TYPE_INT, 0, -1, -1, -1, 9, 2, 0, 0, "j#1#0"},
    {53, -1, -1, -1, 0, 0, 4, -1, -1, -1, 0, 0, 0, ""},
    {64, 39, -1, -1, TYPE_INT, 0, -1, -1, -1, 9, 0, 0, 0, "j#1#0"},
    {67, 42, 22, 8, TYPE_INT, '+', -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {68, 43, 39, 42, TYPE_INT, TOK_LE, -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {69, -1, 43, -1, 0, 0, 5, -1, -1, -1, 0, 0, 0, ""},
    {70, 44, -1, -1, TYPE_INT, 0, -1, -1, -1, 9, 0, 0, 0, "j#1#0"},
    {71, 45, -1, -1, TYPE_INT, 0, -1, -1, -1, -1, 0, 0, 0, ""},
    {72, 46, 44, 45, TYPE_INT, TOK_GE, -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {73, -1, 46, -1, 0, 0, 9, -1, -1, -1, 0, 0, 0, ""},
    {74, 47, -1, -1, TYPE_INT, 0, -1, -1, -1, 9, 0, 0, 0, "j#1#0"},
    {76, 49, 47, 0, TYPE_INT, '<', -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {77, -1, 49, -1, 0, 0, 9, -1, -1, -1, 0, 0, 0, ""},
    {78, -1, -1, -1, 0, 0, 11, -1, -1, -1, 0, 0, 0, ""},
    {79, 50, -1, -1, 0, 1, -1, -1, -1, -1, 0, 0, 0, ""},
    {80, -1, -1, -1, 0, 0, 10, -1, -1, -1, 0, 0, 0, ""},
    {81, -1, -1, -1, 0, 0, 9, -1, -1, -1, 0, 0, 0, ""},
    {82, 51, -1, -1, 0, 0, -1, -1, -1, -1, 0, 0, 0, ""},
    {83, -1, -1, -1, 0, 0, 10, -1, -1, -1, 0, 0, 0, ""},
    {84, 52, 50, 51, 0, 0, -1, 11, 9, -1, 0, 0, 0, ""},
    {85, -1, 52, -1, 0, 0, 7, -1, -1, -1, 0, 0, 0, ""},
    {86, 53, -1, -1, TYPE_LONG, 0, -1, -1, -1, 7, 0, 0, 0, "sum#b1#0"},
    {88, 55, -1, -1, TYPE_INT, 0, -1, -1, -1, 9, 0, 0, 0, "j#1#0"},
    {89, 56, 2, 55, TYPE_INT | TYPE_PTR, 2, -1, -1, -1, -1, 2, 0, 0, ""},
    {90, 57, 56, -1, TYPE_INT, 0, -1, -1, -1, -1, 2, 0, 0, ""},
    {91, 58, 53, 57, TYPE_LONG, '+', -1, -1, -1, -1, 0, 0, TYPE_LONG, ""},
    {93, -1, 58, -1, TYPE_LONG, 0, -1, -1, -1, 7, 4, 0, 0, "sum#b1#0"},
    {94, 60, -1, -1, TYPE_INT, 0, -1, -1, -1, 8, 0, 0, 0, "count#b1#1"},
    {95, 61, -1, -1, 0, 1, -1, -1, -1, -1, 0, 0, 0, ""},
    {96, 62, 60, 61, 0, '+', -1, -1, -1, -1, 0, 0, 0, ""},
    {97, -1, 62, -1, TYPE_INT, 0, -1, -1, -1, 8, 2, 0, 0, "count#b1#1"},
    {99, -1, -1, -1, 0, 0, 7, -1, -1, -1, 0, 0, 0, ""},
    {101, -1, -1, -1, 0, 0, 6, -1, -1, -1, 0, 0, 0, ""},
    {102, 63, -1, -1, TYPE_INT, 0, -1, -1, -1, 9, 0, 0, 0, "j#1#0"},
    {103, 64, -1, -1, 0, 1, -1, -1, -1, -1, 0, 0, 0, ""},
    {104, 65, 63, 64, 0, '+', -1, -1, -1, -1, 0, 0, 0, ""},
    {105, -1, 65, -1, TYPE_INT, 0, -1, -1, -1, 9, 2, 0, 0, "j#1#0"},
    {106, -1, -1, -1, 0, 0, 4, -1, -1, -1, 0, 0, 0, ""},
    {107, -1, -1, -1, 0, 0, 5, -1, -1, -1, 0, 0, 0, ""},
    {110, 68, 3, 22, TYPE_INT | TYPE_PTR, 2, -1, -1, -1, -1, 2, 0, 0, ""},
    {111, 69, -1, -1, TYPE_LONG, 0, -1, -1, -1, 7, 0, 0, 0, "sum#b1#0"},
    {112, 70, -1, -1, TYPE_INT, 0, -1, -1, -1, 8, 0, 0, 0, "count#b1#1"},
    {113, 71, 69, 70, TYPE_LONG, '/', -1, -1, -1, -1, 0, 0, TYPE_LONG, ""},
    {114, 72, 71, -1, TYPE_INT, 0, -1, -1, -1, -1, 0, 0, 0, ""},
    {115, -1, 68, 72, TYPE_INT, 0, -1, -1, -1, -1, 2, 0, 0, ""},
    {118, 75, 3, 22, TYPE_INT | TYPE_PTR, 2, -1, -1, -1, -1, 2, 0, 0, ""},
    {119, 76, 75, -1, TYPE_INT, 0, -1, -1, -1, -1, 2, 0, 0, ""},
    {122, 79, 2, 22, TYPE_INT | TYPE_PTR, 2, -1, -1, -1, -1, 2, 0, 0, ""},
    {123, 80, 79, -1, TYPE_INT, 0, -1, -1, -1, -1, 2, 0, 0, ""},
    {124, 81, 76, 80, TYPE_INT, TOK_NE, -1, -1, -1, -1, 0, 0, TYPE_INT, ""},
    {125, -1, 81, -1, 0, 0, 14, -1, -1, -1, 0, 0, 0, ""},
    {126, 82, -1, -1, TYPE_LONG, 0, -1, -1, -1, 4, 0, 0, 0, "changed"},
    {127, 83, -1, -1, TYPE_LONG, 1, -1, -1, -1, -1, 0, 0, 0, ""},
    {128, 84, 82, 83, TYPE_LONG, '+', -1, -1, -1, -1, 0, 0, TYPE_LONG, ""},
    {129, -1, 84, -1, TYPE_LONG, 0, -1, -1, -1, 4, 4, 0, 0, "changed"},
    {130, -1, -1, -1, 0, 0, 14, -1, -1, -1, 0, 0, 0, ""},
    {132, -1, -1, -1, 0, 0, 3, -1, -1, -1, 0, 0, 0, ""},
    {134, 86, -1, -1, 0, 1, -1, -1, -1, -1, 0, 0, 0, ""},
    {135, 87, 22, 86, 0, '+', -1, -1, -1, -1, 0, 0, 0, ""},
    {136, -1, 87, -1, TYPE_INT, 0, -1, -1, -1, 6, 2, 0, 0, "i#0#0"},
    {137, -1, -1, -1, 0, 0, 1, -1, -1, -1, 0, 0, 0, ""},
    {138, -1, -1, -1, 0, 0, 2, -1, -1, -1, 0, 0, 0, ""},
    {139, 95, -1, -1, TYPE_LONG, 0, -1, -1, -1, 4, 0, 0, 0, "changed"},
    {140, -1, 95, -1, 0, 0, -1, -1, -1, -1, 0, 0, 0, ""}
};

static const char *const vla_object_names[10] = {
    "n", "w", "src", "dst", "changed", "half", "i#0#0",
    "sum#b1#0", "count#b1#1", "j#1#0"
};

static const int vla_object_types[10] = {
    TYPE_INT, TYPE_INT, TYPE_INT | TYPE_PTR, TYPE_INT | TYPE_PTR,
    TYPE_LONG, TYPE_INT, TYPE_INT, TYPE_LONG, TYPE_INT, TYPE_INT
};

static const int vla_object_storage[10] = {
    SC_PARAM, SC_PARAM, SC_PARAM, SC_PARAM,
    SC_LOCAL, SC_LOCAL, SC_LOCAL, SC_LOCAL, SC_LOCAL, SC_LOCAL
};

static const int vla_object_offsets[10] = {
    4, 6, 8, 10, -4, -6, -8, -12, -14, -16
};

static const int vla_object_sizes[10] = {
    2, 2, 2, 2, 4, 2, 2, 4, 2, 2
};

static const int vla_parameter_object_uses[][2] = {
    {27, 0}, {38, 0}, {54, 0}, {75, 0},
    {28, 1}, {55, 1},
    {29, 2}, {56, 2}, {87, 2}, {120, 2},
    {30, 3}, {57, 3}, {108, 3}, {116, 3}
};

static const int vla_local_object_uses[][2] = {
    {31, 4}, {58, 4},
    {20, 5}, {32, 5}, {50, 5}, {59, 5}, {66, 5},
    {13, 6}, {19, 6}, {33, 6}, {37, 6}, {49, 6}, {60, 6},
    {65, 6}, {109, 6}, {117, 6}, {121, 6}, {133, 6}, {136, 6},
    {16, 7}, {34, 7}, {61, 7},
    {18, 8}, {35, 8}, {62, 8},
    {22, 9}, {36, 9}, {63, 9}
};

static void initialize_vla_instruction(struct MirInsn *insn)
{
    memset(insn, 0, sizeof(*insn));
    insn->src1 = -1;
    insn->src2 = -1;
    insn->dst = -1;
    insn->object = -1;
    insn->label = -1;
    insn->phi_pred1 = -1;
    insn->phi_pred2 = -1;
}

static void setup_vla_smooth_fixture(void)
{
    int instruction;
    int item;

    mir_begin_function(
        "vla_smooth_isolation", "_vla_smooth_isolation",
        EMIT_SINK_FINAL, 0, 0, 0);
    if (mir.capacity < 141) {
        struct MirInsn *insns = (struct MirInsn *)realloc(
            mir.insns, 141 * sizeof(*mir.insns));

        if (insns == NULL)
            fatal("cannot allocate VLA smooth fixture");
        mir.insns = insns;
        mir.capacity = 141;
    }
    mir.count = 141;
    mir.next_value = 96;
    mir.next_label = 17;
    mir.return_type = TYPE_LONG;
    mir.local_bytes = 16;
    mir.aggregate_temp_bytes = 0;
    mir.has_vla = 0;
    mir.object_count = 10;
    mir.declared_count = 10;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        initialize_vla_instruction(&mir.insns[instruction]);
        mir.insns[instruction].opcode = vla_opcodes[instruction];
    }
    for (item = 0;
         item < (int)(sizeof(vla_semantic_insns) /
                      sizeof(vla_semantic_insns[0]));
         ++item) {
        const struct VlaInsnFixture *fixture = &vla_semantic_insns[item];
        struct MirInsn *insn = &mir.insns[fixture->instruction];

        insn->dst = fixture->dst;
        insn->src1 = fixture->src1;
        insn->src2 = fixture->src2;
        insn->type = fixture->type;
        insn->immediate = fixture->immediate;
        insn->label = fixture->label;
        insn->phi_pred1 = fixture->phi_pred1;
        insn->phi_pred2 = fixture->phi_pred2;
        insn->object = fixture->object;
        insn->memory_size = fixture->memory_size;
        insn->memory_flags = fixture->memory_flags;
        insn->secondary_offset = fixture->secondary_offset;
        strcpy(insn->name, fixture->name);
    }
    for (item = 0;
         item < (int)(sizeof(vla_parameter_object_uses) /
                      sizeof(vla_parameter_object_uses[0]));
         ++item)
        mir.insns[vla_parameter_object_uses[item][0]].object =
            vla_parameter_object_uses[item][1];
    for (item = 0;
         item < (int)(sizeof(vla_local_object_uses) /
                      sizeof(vla_local_object_uses[0]));
         ++item)
        mir.insns[vla_local_object_uses[item][0]].object =
            vla_local_object_uses[item][1];
    for (item = 0; item < 10; ++item) {
        struct MirObject *object = &mir.objects[item];

        memset(object, 0, sizeof(*object));
        strcpy(object->name, vla_object_names[item]);
        object->storage = vla_object_storage[item];
        object->type = vla_object_types[item];
        object->offset = vla_object_offsets[item];
        object->entry_value = item < 4 ? item : -1;
        strcpy(mir.declared_names[item], vla_object_names[item]);
        mir.declared_types[item] = vla_object_types[item];
        mir.declared_storage[item] = vla_object_storage[item];
        mir.declared_offsets[item] = vla_object_offsets[item];
        mir.declared_sizes[item] = vla_object_sizes[item];
    }
}

enum VlaMutation {
    VLA_CONST_TYPE,
    VLA_BOOL_PHI_TYPE,
    VLA_UNTYPED_INCREMENT,
    VLA_INDIRECT_STORE_TYPE,
    VLA_DIRECT_STORE_TYPE,
    VLA_RETURN_INSN_TYPE,
    VLA_PARAMETER_MEMORY_SIZE,
    VLA_DIRECT_STORE_MEMORY_SIZE,
    VLA_DIRECT_LOAD_MEMORY_SIZE,
    VLA_PARAMETER_VOLATILE_FLAG,
    VLA_DIRECT_STORE_BITFIELD,
    VLA_DIRECT_LOAD_BITFIELD,
    VLA_INDIRECT_LOAD_BITFIELD,
    VLA_INDIRECT_STORE_BITFIELD,
    VLA_INDIRECT_VOLATILE,
    VLA_INDEX_WIDTH,
    VLA_COMPARE_SIGNEDNESS,
    VLA_ACCUMULATE_SIGNEDNESS,
    VLA_DIVIDE_SIGNEDNESS,
    VLA_INDEX_POINTER_QUALIFIER,
    VLA_INDEX_POINTEE_VOLATILE,
    VLA_INDEX_STRIDE,
    VLA_DECLARED_PARAMETER_TYPE,
    VLA_DECLARED_PARAMETER_OFFSET,
    VLA_DECLARED_PARAMETER_VOLATILE,
    VLA_DECLARED_POINTEE_MASK,
    VLA_DECLARED_DYNAMIC_STRIDE,
    VLA_DECLARED_VLA,
    VLA_RUNTIME_STRIDE_STATE,
    VLA_VARIADIC_STATE,
    VLA_DEAD_FRAME_SUFFIX,
    VLA_IMPLICIT_RETURN,
    VLA_EXTRA_OBJECT,
    VLA_EXTRA_DECLARATION,
    VLA_OBJECT_REGISTER,
    VLA_OBJECT_ENTRY_VALUE,
    VLA_INSN_OBJECT_NAME,
    VLA_PARAMETER_OBJECT_TYPE,
    VLA_PARAMETER_STORAGE,
    VLA_LOCAL_OUT_OF_FRAME,
    VLA_LOCAL_OVERLAP,
    VLA_LOCAL_VOLATILE,
    VLA_LOCAL_FRAME_SIZE,
    VLA_AGGREGATE_FRAME,
    VLA_OPERATOR,
    VLA_CALL_OPCODE,
    VLA_BRANCH_VALUE,
    VLA_BRANCH_TARGET,
    VLA_PHI_PREDECESSOR,
    VLA_LABEL_DUPLICATE,
    VLA_FUNCTION_RETURN_TYPE,
    VLA_RETURN_VALUE,
    VLA_ALIAS_OBJECT,
    VLA_VLA_STATE,
    VLA_MUTATION_COUNT
};

static const char *const vla_mutation_names[VLA_MUTATION_COUNT] = {
    "constant type", "boolean PHI type", "untyped increment",
    "indirect store type", "direct store type", "return instruction type",
    "parameter memory size", "direct store memory size",
    "direct load memory size", "parameter volatile flag",
    "direct store bit-field", "direct load bit-field",
    "indirect load bit-field", "indirect store bit-field",
    "indirect volatility", "index width",
    "comparison signedness", "accumulation signedness",
    "division signedness", "index pointer qualifier",
    "index pointee volatility", "index stride", "declared parameter type",
    "declared parameter offset", "declared parameter volatility",
    "declared pointee mask", "declared dynamic stride",
    "declared VLA", "runtime stride state", "variadic state",
    "dead frame suffix", "implicit return", "extra object",
    "extra declaration", "register object", "object entry value",
    "instruction object name", "parameter object type",
    "parameter storage", "local outside frame", "local overlap",
    "local volatility", "local frame size", "aggregate frame",
    "operator", "call opcode", "branch value", "branch target",
    "PHI predecessor", "duplicate label", "function return type",
    "return value", "aliased objects", "VLA state"
};

static void apply_vla_mutation(enum VlaMutation mutation)
{
    switch (mutation) {
    case VLA_CONST_TYPE: mir.insns[24].type = TYPE_CHAR; break;
    case VLA_BOOL_PHI_TYPE: mir.insns[84].type = TYPE_LONG; break;
    case VLA_UNTYPED_INCREMENT: mir.insns[96].type = TYPE_LONG; break;
    case VLA_INDIRECT_STORE_TYPE: mir.insns[115].type = TYPE_CHAR; break;
    case VLA_DIRECT_STORE_TYPE: mir.insns[136].type = TYPE_CHAR; break;
    case VLA_RETURN_INSN_TYPE: mir.insns[140].type = TYPE_LONG; break;
    case VLA_PARAMETER_MEMORY_SIZE: mir.insns[1].memory_size = 1; break;
    case VLA_DIRECT_STORE_MEMORY_SIZE: mir.insns[93].memory_size = 1; break;
    case VLA_DIRECT_LOAD_MEMORY_SIZE: mir.insns[64].memory_size = 1; break;
    case VLA_PARAMETER_VOLATILE_FLAG:
        mir.insns[1].memory_flags |= MIR_MEMORY_FLAG_VOLATILE;
        break;
    case VLA_DIRECT_STORE_BITFIELD:
        mir.insns[93].bit_width = 8;
        mir.insns[93].bit_mask = 255;
        break;
    case VLA_DIRECT_LOAD_BITFIELD:
        mir.insns[64].bit_width = 8;
        mir.insns[64].bit_mask = 255;
        break;
    case VLA_INDIRECT_LOAD_BITFIELD:
        mir.insns[90].bit_width = 8;
        mir.insns[90].bit_mask = 255;
        break;
    case VLA_INDIRECT_STORE_BITFIELD:
        mir.insns[115].bit_width = 8;
        mir.insns[115].bit_mask = 255;
        break;
    case VLA_INDIRECT_VOLATILE:
        mir.insns[90].memory_flags = MIR_MEMORY_FLAG_VOLATILE;
        break;
    case VLA_INDEX_WIDTH: mir.insns[89].memory_size = 1; break;
    case VLA_COMPARE_SIGNEDNESS:
        mir.insns[39].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
        break;
    case VLA_ACCUMULATE_SIGNEDNESS:
        mir.insns[91].secondary_offset = TYPE_LONG | TYPE_UNSIGNED;
        break;
    case VLA_DIVIDE_SIGNEDNESS:
        mir.insns[113].secondary_offset = TYPE_LONG | TYPE_UNSIGNED;
        break;
    case VLA_INDEX_POINTER_QUALIFIER:
        mir.insns[89].has_pointer_qualifiers = 1;
        break;
    case VLA_INDEX_POINTEE_VOLATILE:
        mir.insns[89].pointee_volatile_mask = 1;
        break;
    case VLA_INDEX_STRIDE: mir.insns[89].immediate = 1; break;
    case VLA_DECLARED_PARAMETER_TYPE:
        mir.declared_types[0] = TYPE_INT | TYPE_UNSIGNED;
        break;
    case VLA_DECLARED_PARAMETER_OFFSET:
        mir.declared_offsets[0] = 6;
        break;
    case VLA_DECLARED_PARAMETER_VOLATILE:
        mir.declared_is_volatile[0] = 1;
        break;
    case VLA_DECLARED_POINTEE_MASK:
        mir.declared_pointee_volatile_masks[2] = 1;
        break;
    case VLA_DECLARED_DYNAMIC_STRIDE:
        mir.declared_dynamic_strides[2] = 4;
        strcpy(mir.declared_runtime_stride_names[2], "n");
        break;
    case VLA_DECLARED_VLA: mir.declared_is_vla[2] = 1; break;
    case VLA_RUNTIME_STRIDE_STATE:
        mir.has_runtime_stride_param = 1;
        break;
    case VLA_VARIADIC_STATE:
        mir.is_variadic_function = 1;
        break;
    case VLA_DEAD_FRAME_SUFFIX: mir.dead_local_suffix_bytes = 2; break;
    case VLA_IMPLICIT_RETURN: mir.implicit_zero_return = 1; break;
    case VLA_EXTRA_OBJECT: mir.object_count = 11; break;
    case VLA_EXTRA_DECLARATION: mir.declared_count = 11; break;
    case VLA_OBJECT_REGISTER: mir.objects[7].is_register = 1; break;
    case VLA_OBJECT_ENTRY_VALUE: mir.objects[7].entry_value = 53; break;
    case VLA_INSN_OBJECT_NAME: strcpy(mir.insns[93].name, "changed"); break;
    case VLA_PARAMETER_OBJECT_TYPE:
        mir.objects[0].type = TYPE_INT | TYPE_UNSIGNED;
        break;
    case VLA_PARAMETER_STORAGE: mir.objects[0].storage = SC_LOCAL; break;
    case VLA_LOCAL_OUT_OF_FRAME:
        mir.objects[9].offset = -18;
        mir.declared_offsets[9] = -18;
        break;
    case VLA_LOCAL_OVERLAP:
        mir.objects[9].offset = -14;
        mir.declared_offsets[9] = -14;
        break;
    case VLA_LOCAL_VOLATILE:
        mir.declared_is_volatile[7] = 1;
        break;
    case VLA_LOCAL_FRAME_SIZE: mir.local_bytes = 18; break;
    case VLA_AGGREGATE_FRAME: mir.aggregate_temp_bytes = 2; break;
    case VLA_OPERATOR: mir.insns[68].immediate = '<'; break;
    case VLA_CALL_OPCODE:
        mir.insns[5].opcode = MIR_CALL;
        strcpy(mir.insns[5].name, "unexpected_call");
        break;
    case VLA_BRANCH_VALUE: mir.insns[69].src1 = 46; break;
    case VLA_BRANCH_TARGET: mir.insns[69].label = 9; break;
    case VLA_PHI_PREDECESSOR: mir.insns[33].phi_pred2 = 14; break;
    case VLA_LABEL_DUPLICATE: mir.insns[130].label = 3; break;
    case VLA_FUNCTION_RETURN_TYPE:
        mir.return_type = TYPE_LONG | TYPE_UNSIGNED;
        break;
    case VLA_RETURN_VALUE: mir.insns[140].src1 = 82; break;
    case VLA_ALIAS_OBJECT: mir.insns[4].object = 2; break;
    case VLA_VLA_STATE: mir.has_vla = 1; break;
    case VLA_MUTATION_COUNT: break;
    }
}

enum VlaBranchMutationKind {
    VLA_BRANCH_MUTATE_INSN_FIELD,
    VLA_BRANCH_MUTATE_INSN_NAME,
    VLA_BRANCH_MUTATE_OBJECT_DECL_OFFSET,
    VLA_BRANCH_MUTATE_OBJECT_DECL_TYPE
};

enum VlaBranchInsnField {
    VLA_BRANCH_FIELD_DST,
    VLA_BRANCH_FIELD_SRC1,
    VLA_BRANCH_FIELD_SRC2,
    VLA_BRANCH_FIELD_IMMEDIATE,
    VLA_BRANCH_FIELD_LABEL,
    VLA_BRANCH_FIELD_PHI_PRED1,
    VLA_BRANCH_FIELD_PHI_PRED2,
    VLA_BRANCH_FIELD_OBJECT
};

enum VlaBranchValueKind {
    VLA_BRANCH_VALUE_LITERAL,
    VLA_BRANCH_VALUE_INSN_DST,
    VLA_BRANCH_VALUE_INSN_LABEL,
    VLA_BRANCH_VALUE_INSN_OBJECT
};

struct VlaBranchMutationCase {
    const char *name;
    int kind;
    int index;
    int field;
    int value_kind;
    long value;
    const char *text;
};

static const struct VlaBranchMutationCase vla_branch_mutations[] = {
    {"w parameter value", VLA_BRANCH_MUTATE_INSN_FIELD, 2,
     VLA_BRANCH_FIELD_DST, VLA_BRANCH_VALUE_LITERAL, 88, NULL},
    {"src parameter offset", VLA_BRANCH_MUTATE_OBJECT_DECL_OFFSET, 2,
     0, VLA_BRANCH_VALUE_LITERAL, 12, NULL},
    {"dst parameter object type", VLA_BRANCH_MUTATE_OBJECT_DECL_TYPE, 3,
     0, VLA_BRANCH_VALUE_LITERAL, TYPE_LONG | TYPE_PTR, NULL},
    {"parameter use object", VLA_BRANCH_MUTATE_INSN_FIELD, 87,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 1, NULL},
    {"changed anchor name", VLA_BRANCH_MUTATE_INSN_NAME, 7,
     0, VLA_BRANCH_VALUE_LITERAL, 0, "changed_x"},
    {"half anchor name", VLA_BRANCH_MUTATE_INSN_NAME, 11,
     0, VLA_BRANCH_VALUE_LITERAL, 0, "half_x"},
    {"index anchor name", VLA_BRANCH_MUTATE_INSN_NAME, 25,
     0, VLA_BRANCH_VALUE_LITERAL, 0, "i#0#0x"},
    {"sum anchor name", VLA_BRANCH_MUTATE_INSN_NAME, 44,
     0, VLA_BRANCH_VALUE_LITERAL, 0, "sum#b1#0x"},
    {"count anchor name", VLA_BRANCH_MUTATE_INSN_NAME, 47,
     0, VLA_BRANCH_VALUE_LITERAL, 0, "count#b1#1x"},
    {"loop index anchor name", VLA_BRANCH_MUTATE_INSN_NAME, 52,
     0, VLA_BRANCH_VALUE_LITERAL, 0, "j#1#0x"},
    {"local upper bound", VLA_BRANCH_MUTATE_OBJECT_DECL_OFFSET, 9,
     0, VLA_BRANCH_VALUE_LITERAL, -1, NULL},
    {"local use object", VLA_BRANCH_MUTATE_INSN_FIELD, 109,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 52, NULL},
    {"changed alias store local", VLA_BRANCH_MUTATE_INSN_FIELD, 126,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 44, NULL},
    {"changed accumulation store local", VLA_BRANCH_MUTATE_INSN_FIELD, 129,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 44, NULL},
    {"changed return load local", VLA_BRANCH_MUTATE_INSN_FIELD, 139,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 44, NULL},
    {"sum loop load local", VLA_BRANCH_MUTATE_INSN_FIELD, 86,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 7, NULL},
    {"sum accumulation store local", VLA_BRANCH_MUTATE_INSN_FIELD, 93,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 7, NULL},
    {"sum division load local", VLA_BRANCH_MUTATE_INSN_FIELD, 111,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 7, NULL},
    {"count loop load local", VLA_BRANCH_MUTATE_INSN_FIELD, 94,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"count increment store local", VLA_BRANCH_MUTATE_INSN_FIELD, 97,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"count division load local", VLA_BRANCH_MUTATE_INSN_FIELD, 112,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"j loop bound load local", VLA_BRANCH_MUTATE_INSN_FIELD, 64,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"j lower bound load local", VLA_BRANCH_MUTATE_INSN_FIELD, 70,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"j upper bound load local", VLA_BRANCH_MUTATE_INSN_FIELD, 74,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"j source index local", VLA_BRANCH_MUTATE_INSN_FIELD, 88,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"j increment load local", VLA_BRANCH_MUTATE_INSN_FIELD, 102,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"j increment store local", VLA_BRANCH_MUTATE_INSN_FIELD, 105,
     VLA_BRANCH_FIELD_OBJECT, VLA_BRANCH_VALUE_INSN_OBJECT, 25, NULL},
    {"constant value", VLA_BRANCH_MUTATE_INSN_FIELD, 103,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, 2, NULL},
    {"initial changed source", VLA_BRANCH_MUTATE_INSN_FIELD, 7,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 9, NULL},
    {"initial divide operator", VLA_BRANCH_MUTATE_INSN_FIELD, 10,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '+', NULL},
    {"initial divide lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 10,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 1, NULL},
    {"initial divide rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 10,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 6, NULL},
    {"initial half store source", VLA_BRANCH_MUTATE_INSN_FIELD, 11,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 9, NULL},
    {"initial i store source", VLA_BRANCH_MUTATE_INSN_FIELD, 25,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 9, NULL},
    {"outer phi initial source", VLA_BRANCH_MUTATE_INSN_FIELD, 33,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 9, NULL},
    {"outer phi backedge source", VLA_BRANCH_MUTATE_INSN_FIELD, 33,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 134, NULL},
    {"outer phi entry predecessor", VLA_BRANCH_MUTATE_INSN_FIELD, 33,
     VLA_BRANCH_FIELD_PHI_PRED1, VLA_BRANCH_VALUE_INSN_LABEL, 26, NULL},
    {"outer phi backedge predecessor", VLA_BRANCH_MUTATE_INSN_FIELD, 33,
     VLA_BRANCH_FIELD_PHI_PRED2, VLA_BRANCH_VALUE_INSN_LABEL, 130, NULL},
    {"outer compare operator", VLA_BRANCH_MUTATE_INSN_FIELD, 39,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, TOK_LE, NULL},
    {"outer compare lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 39,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 51, NULL},
    {"outer compare rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 39,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 2, NULL},
    {"outer branch value", VLA_BRANCH_MUTATE_INSN_FIELD, 40,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 33, NULL},
    {"outer branch label", VLA_BRANCH_MUTATE_INSN_FIELD, 40,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 107, NULL},
    {"inner sum source", VLA_BRANCH_MUTATE_INSN_FIELD, 44,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 6, NULL},
    {"inner count source", VLA_BRANCH_MUTATE_INSN_FIELD, 47,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 24, NULL},
    {"inner subtract operator", VLA_BRANCH_MUTATE_INSN_FIELD, 51,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '+', NULL},
    {"inner subtract lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 51,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 64, NULL},
    {"inner subtract rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 51,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 9, NULL},
    {"inner j source", VLA_BRANCH_MUTATE_INSN_FIELD, 52,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 10, NULL},
    {"inner add operator", VLA_BRANCH_MUTATE_INSN_FIELD, 67,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '-', NULL},
    {"inner add lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 67,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 64, NULL},
    {"inner add rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 67,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 9, NULL},
    {"inner bound lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 68,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 33, NULL},
    {"inner bound rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 68,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 10, NULL},
    {"valid ge operator", VLA_BRANCH_MUTATE_INSN_FIELD, 72,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '>', NULL},
    {"valid ge lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 72,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 64, NULL},
    {"valid ge rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 72,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 10, NULL},
    {"valid lower branch value", VLA_BRANCH_MUTATE_INSN_FIELD, 73,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 68, NULL},
    {"valid lower branch label", VLA_BRANCH_MUTATE_INSN_FIELD, 73,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 83, NULL},
    {"valid lt operator", VLA_BRANCH_MUTATE_INSN_FIELD, 76,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, TOK_LE, NULL},
    {"valid lt lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 76,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 70, NULL},
    {"valid lt rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 76,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 2, NULL},
    {"valid upper branch value", VLA_BRANCH_MUTATE_INSN_FIELD, 77,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 68, NULL},
    {"valid upper branch label", VLA_BRANCH_MUTATE_INSN_FIELD, 77,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 83, NULL},
    {"valid index jump label", VLA_BRANCH_MUTATE_INSN_FIELD, 80,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 99, NULL},
    {"valid phi first source", VLA_BRANCH_MUTATE_INSN_FIELD, 84,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 82, NULL},
    {"valid phi second source", VLA_BRANCH_MUTATE_INSN_FIELD, 84,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 79, NULL},
    {"valid phi first predecessor", VLA_BRANCH_MUTATE_INSN_FIELD, 84,
     VLA_BRANCH_FIELD_PHI_PRED1, VLA_BRANCH_VALUE_INSN_LABEL, 81, NULL},
    {"valid phi second predecessor", VLA_BRANCH_MUTATE_INSN_FIELD, 84,
     VLA_BRANCH_FIELD_PHI_PRED2, VLA_BRANCH_VALUE_INSN_LABEL, 78, NULL},
    {"valid index branch value", VLA_BRANCH_MUTATE_INSN_FIELD, 85,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 82, NULL},
    {"valid index branch label", VLA_BRANCH_MUTATE_INSN_FIELD, 85,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 107, NULL},
    {"accumulation load source", VLA_BRANCH_MUTATE_INSN_FIELD, 90,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 110, NULL},
    {"accumulation add operator", VLA_BRANCH_MUTATE_INSN_FIELD, 91,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '-', NULL},
    {"accumulation add lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 91,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 111, NULL},
    {"accumulation add rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 91,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 119, NULL},
    {"accumulation store source", VLA_BRANCH_MUTATE_INSN_FIELD, 93,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 86, NULL},
    {"count add operator", VLA_BRANCH_MUTATE_INSN_FIELD, 96,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '-', NULL},
    {"count add lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 96,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 95, NULL},
    {"count add rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 96,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 94, NULL},
    {"count store source", VLA_BRANCH_MUTATE_INSN_FIELD, 97,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 95, NULL},
    {"increment operator", VLA_BRANCH_MUTATE_INSN_FIELD, 104,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '-', NULL},
    {"increment lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 104,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 64, NULL},
    {"increment rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 104,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 95, NULL},
    {"increment store source", VLA_BRANCH_MUTATE_INSN_FIELD, 105,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 102, NULL},
    {"increment jump label", VLA_BRANCH_MUTATE_INSN_FIELD, 106,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 81, NULL},
    {"average store index", VLA_BRANCH_MUTATE_INSN_FIELD, 110,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 52, NULL},
    {"average divide operator", VLA_BRANCH_MUTATE_INSN_FIELD, 113,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '+', NULL},
    {"average divide lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 113,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 86, NULL},
    {"average divide rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 113,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 94, NULL},
    {"average cast immediate", VLA_BRANCH_MUTATE_INSN_FIELD, 114,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, 1, NULL},
    {"average cast source", VLA_BRANCH_MUTATE_INSN_FIELD, 114,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 111, NULL},
    {"average store pointer", VLA_BRANCH_MUTATE_INSN_FIELD, 115,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 118, NULL},
    {"average store value", VLA_BRANCH_MUTATE_INSN_FIELD, 115,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 113, NULL},
    {"alias dst index", VLA_BRANCH_MUTATE_INSN_FIELD, 118,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 52, NULL},
    {"alias dst load source", VLA_BRANCH_MUTATE_INSN_FIELD, 119,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 122, NULL},
    {"alias src index", VLA_BRANCH_MUTATE_INSN_FIELD, 122,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 52, NULL},
    {"alias src load source", VLA_BRANCH_MUTATE_INSN_FIELD, 123,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 118, NULL},
    {"alias compare operator", VLA_BRANCH_MUTATE_INSN_FIELD, 124,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, TOK_EQ, NULL},
    {"alias compare lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 124,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 123, NULL},
    {"alias compare rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 124,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 119, NULL},
    {"alias branch value", VLA_BRANCH_MUTATE_INSN_FIELD, 125,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 81, NULL},
    {"alias branch label", VLA_BRANCH_MUTATE_INSN_FIELD, 125,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 107, NULL},
    {"changed add operator", VLA_BRANCH_MUTATE_INSN_FIELD, 128,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '-', NULL},
    {"changed add lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 128,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 127, NULL},
    {"changed add rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 128,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 126, NULL},
    {"changed store source", VLA_BRANCH_MUTATE_INSN_FIELD, 129,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 126, NULL},
    {"return add operator", VLA_BRANCH_MUTATE_INSN_FIELD, 135,
     VLA_BRANCH_FIELD_IMMEDIATE, VLA_BRANCH_VALUE_LITERAL, '-', NULL},
    {"return add lhs", VLA_BRANCH_MUTATE_INSN_FIELD, 135,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 134, NULL},
    {"return add rhs", VLA_BRANCH_MUTATE_INSN_FIELD, 135,
     VLA_BRANCH_FIELD_SRC2, VLA_BRANCH_VALUE_INSN_DST, 95, NULL},
    {"return store source", VLA_BRANCH_MUTATE_INSN_FIELD, 136,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 134, NULL},
    {"return jump label", VLA_BRANCH_MUTATE_INSN_FIELD, 137,
     VLA_BRANCH_FIELD_LABEL, VLA_BRANCH_VALUE_INSN_LABEL, 53, NULL},
    {"return value source", VLA_BRANCH_MUTATE_INSN_FIELD, 140,
     VLA_BRANCH_FIELD_SRC1, VLA_BRANCH_VALUE_INSN_DST, 126, NULL}
};

static long vla_branch_mutation_value(
    const struct VlaBranchMutationCase *mutation)
{
    if (mutation->value_kind == VLA_BRANCH_VALUE_INSN_DST)
        return mir.insns[mutation->value].dst;
    if (mutation->value_kind == VLA_BRANCH_VALUE_INSN_LABEL)
        return mir.insns[mutation->value].label;
    if (mutation->value_kind == VLA_BRANCH_VALUE_INSN_OBJECT)
        return mir.insns[mutation->value].object;
    return mutation->value;
}

static void apply_vla_branch_mutation(
    const struct VlaBranchMutationCase *mutation)
{
    long value = vla_branch_mutation_value(mutation);

    switch (mutation->kind) {
    case VLA_BRANCH_MUTATE_INSN_FIELD:
        switch (mutation->field) {
        case VLA_BRANCH_FIELD_DST:
            mir.insns[mutation->index].dst = (int)value;
            break;
        case VLA_BRANCH_FIELD_SRC1:
            mir.insns[mutation->index].src1 = (int)value;
            break;
        case VLA_BRANCH_FIELD_SRC2:
            mir.insns[mutation->index].src2 = (int)value;
            break;
        case VLA_BRANCH_FIELD_IMMEDIATE:
            mir.insns[mutation->index].immediate = value;
            break;
        case VLA_BRANCH_FIELD_LABEL:
            mir.insns[mutation->index].label = (int)value;
            break;
        case VLA_BRANCH_FIELD_PHI_PRED1:
            mir.insns[mutation->index].phi_pred1 = (int)value;
            break;
        case VLA_BRANCH_FIELD_PHI_PRED2:
            mir.insns[mutation->index].phi_pred2 = (int)value;
            break;
        case VLA_BRANCH_FIELD_OBJECT:
            mir.insns[mutation->index].object = (int)value;
            break;
        }
        break;
    case VLA_BRANCH_MUTATE_INSN_NAME:
        strcpy(mir.insns[mutation->index].name, mutation->text);
        break;
    case VLA_BRANCH_MUTATE_OBJECT_DECL_OFFSET:
        mir.objects[mutation->index].offset = (int)value;
        mir.declared_offsets[mutation->index] = (int)value;
        break;
    case VLA_BRANCH_MUTATE_OBJECT_DECL_TYPE:
        mir.objects[mutation->index].type = (int)value;
        mir.declared_types[mutation->index] = (int)value;
        break;
    }
}

static void apply_vla_touching_layout(void)
{
    mir.objects[4].offset = -6;
    mir.declared_offsets[4] = -6;
    mir.objects[5].offset = -8;
    mir.declared_offsets[5] = -8;
    mir.objects[6].offset = -10;
    mir.declared_offsets[6] = -10;
    mir.objects[7].offset = -14;
    mir.declared_offsets[7] = -14;
    mir.objects[8].offset = -2;
    mir.declared_offsets[8] = -2;
}

static int emit_vla_smooth_candidate(MirStream *out)
{
    return mir_try_emit_vla_smooth_isolation(out);
}

static size_t read_vla_stream(
    MirStream *stream, char *text, size_t capacity)
{
    size_t bytes;

    mir_stream_rewind(stream);
    memset(text, 0, capacity);
    bytes = mir_stream_read(text, 1, capacity - 1, stream);
    if (bytes >= capacity - 1)
        fatal("VLA smooth isolation stream overflow");
    return bytes;
}

static int verify_vla_control_variant(
    int stack_check, void (*mutate_fixture)(void))
{
    MirStream *stream = mir_stream_open();
    char text[32768];
    size_t bytes;
    int saved_stack_check = opt_stack_check;
    int accepted;
    int ok;

    if (stream == NULL)
        fatal("cannot create VLA smooth control stream");
    setup_vla_smooth_fixture();
    if (mutate_fixture != NULL)
        mutate_fixture();
    opt_stack_check = stack_check;
    label_id = 100;
    accepted = mir_try_selector(stream, emit_vla_smooth_candidate);
    bytes = read_vla_stream(stream, text, sizeof(text));
    ok = accepted == 1 && bytes > 0 &&
         strstr(text, MIR_EXACT_KERNEL_MARKER) != NULL &&
         strstr(text, "\tld hl,-14\n\tadd hl,sp\n\tld sp,hl\n") != NULL &&
         (stack_check
              ? strstr(text, "\tcall __stchk\n") != NULL
              : strstr(text, "\tcall __stchk\n") == NULL);
    opt_stack_check = saved_stack_check;
    mir_stream_close(stream);
    return ok;
}

static int verify_vla_control(int stack_check)
{
    return verify_vla_control_variant(stack_check, NULL);
}

static int verify_vla_touching_layout_control(int stack_check)
{
    return verify_vla_control_variant(
        stack_check, apply_vla_touching_layout);
}

static int verify_vla_mutations(void)
{
    int survivors = 0;
    int mutation;

    for (mutation = 0; mutation < VLA_MUTATION_COUNT; ++mutation) {
        MirStream *stream = mir_stream_open();
        char text[32];
        size_t bytes;
        int accepted;

        if (stream == NULL)
            fatal("cannot create VLA smooth mutation stream");
        setup_vla_smooth_fixture();
        apply_vla_mutation((enum VlaMutation)mutation);
        label_id = 211;
        mir_stream_puts("prefix\n", stream);
        accepted = mir_try_selector(stream, emit_vla_smooth_candidate);
        if (accepted != 0) {
            fprintf(stderr, "VLA mutation survived: %s\n",
                    vla_mutation_names[mutation]);
            ++survivors;
        } else {
            bytes = read_vla_stream(stream, text, sizeof(text));
            if (bytes != 7 || memcmp(text, "prefix\n", 7) != 0 ||
                label_id != 211) {
                fprintf(stderr,
                        "VLA mutation contaminated candidate: %s\n",
                        vla_mutation_names[mutation]);
                ++survivors;
            }
        }
        mir_stream_close(stream);
    }
    printf("VLA smooth mutation survivors=%d/%d\n",
           survivors, VLA_MUTATION_COUNT);
    return survivors == 0;
}

static int verify_vla_branch_mutations(void)
{
    int survivors = 0;
    int mutation;

    for (mutation = 0;
         mutation < (int)(sizeof(vla_branch_mutations) /
                          sizeof(vla_branch_mutations[0]));
         ++mutation) {
        MirStream *stream = mir_stream_open();
        char text[32];
        size_t bytes;
        int accepted;

        if (stream == NULL)
            fatal("cannot create VLA smooth branch mutation stream");
        setup_vla_smooth_fixture();
        apply_vla_branch_mutation(&vla_branch_mutations[mutation]);
        label_id = 317;
        mir_stream_puts("prefix\n", stream);
        accepted = mir_try_selector(stream, emit_vla_smooth_candidate);
        if (accepted != 0) {
            fprintf(stderr, "VLA branch mutation survived: %s\n",
                    vla_branch_mutations[mutation].name);
            ++survivors;
        } else {
            bytes = read_vla_stream(stream, text, sizeof(text));
            if (bytes != 7 || memcmp(text, "prefix\n", 7) != 0 ||
                label_id != 317) {
                fprintf(stderr,
                        "VLA branch mutation contaminated candidate: %s\n",
                        vla_branch_mutations[mutation].name);
                ++survivors;
            }
        }
        mir_stream_close(stream);
    }
    printf("VLA smooth branch mutation survivors=%d/%d\n",
           survivors,
           (int)(sizeof(vla_branch_mutations) /
                 sizeof(vla_branch_mutations[0])));
    return survivors == 0;
}

int main(void)
{
    int ok = 1;

    if (!verify_vla_control(0) ||
        !verify_vla_control(1) ||
        !verify_vla_touching_layout_control(0) ||
        !verify_vla_touching_layout_control(1)) {
        fputs("VLA smooth valid control failed\n", stderr);
        ok = 0;
    }
    if (!verify_vla_mutations())
        ok = 0;
    if (!verify_vla_branch_mutations())
        ok = 0;
    if (!ok)
        return 1;
    puts("VLA smooth isolation host tests passed");
    return 0;
}
