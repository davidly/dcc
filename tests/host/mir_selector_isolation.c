#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"

static int rejecting_candidate(MirStream *out)
{
    int label = new_label();

    mir_emit_runtime_call(out, "__mulu");
    mir_stream_printf(out, "L%d:\n\tdiscard\n", label);
    return 0;
}

static int accepting_candidate(MirStream *out)
{
    int label = new_label();

    mir_emit_runtime_call(out, "__mulu");
    mir_stream_printf(out, "L%d:\n\tret\n", label);
    return 1;
}

static size_t read_stream(MirStream *stream, char *text, size_t capacity)
{
    size_t bytes;

    mir_stream_rewind(stream);
    memset(text, 0, capacity);
    bytes = mir_stream_read(text, 1, capacity - 1, stream);
    if (bytes >= capacity - 1)
        fatal("selector isolation host fixture output overflow");
    return bytes;
}

static void setup_affine_return(void)
{
    int instruction;

    mir_begin_function(
        "selector_affine", "_selector_affine", EMIT_SINK_FINAL, 0, 0, 0);
    mir.count = 5;
    mir.next_value = 3;
    mir.next_label = 1;
    mir.object_count = 2;
    if (mir.allocation_capacity < mir.next_value) {
        int *colors = (int *)realloc(
            mir.allocation_colors,
            (size_t)mir.next_value * sizeof(*mir.allocation_colors));
        int *spills = (int *)realloc(
            mir.allocation_spills,
            (size_t)mir.next_value * sizeof(*mir.allocation_spills));

        if (colors == NULL || spills == NULL)
            fatal("cannot allocate affine selector homes");
        mir.allocation_colors = colors;
        mir.allocation_spills = spills;
        mir.allocation_capacity = mir.next_value;
    }
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    memset(&mir.objects[1], 0, sizeof(mir.objects[1]));
    mir.objects[0].storage = SC_PARAM;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = 4;
    mir.objects[1].storage = SC_PARAM;
    mir.objects[1].type = TYPE_INT;
    mir.objects[1].offset = 6;
    for (instruction = 0; instruction < mir.count; ++instruction) {
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
    mir.insns[1].opcode = MIR_PARAM;
    mir.insns[1].dst = 0;
    mir.insns[1].object = 0;
    mir.insns[2].opcode = MIR_PARAM;
    mir.insns[2].dst = 1;
    mir.insns[2].object = 1;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '-';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].opcode = MIR_RETURN;
    mir.insns[4].src1 = 2;
    for (instruction = 0; instruction < mir.next_value; ++instruction) {
        mir.allocation_colors[instruction] = MIR_COLOR_HL;
        mir.allocation_spills[instruction] = -1;
    }
}

static void clear_selector_liveness(void)
{
    free(mir.live_in);
    free(mir.live_out);
    mir.live_in = NULL;
    mir.live_out = NULL;
}

static int verify_affine_return_isolation(void)
{
    MirStream *stream = mir_stream_open();
    char text[512];
    size_t bytes;
    int accepted;
    int saved_stack_check = opt_stack_check;
    int ok = stream != NULL;

    if (!ok)
        fatal("cannot create affine selector isolation stream");
    opt_stack_check = 1;
    label_id = 73;
    mir_stream_puts("prefix\n", stream);
    setup_affine_return();
    if (!mir_verify_and_dump())
        fatal("valid affine selector fixture did not verify");
    mir.insns[2].object = 2;
    accepted = mir_try_emit_z80(stream);
    ok = ok && accepted == 0;
    ok = ok && mir_stream_size(stream) == 7 && label_id == 73;
    clear_selector_liveness();

    setup_affine_return();
    if (!mir_verify_and_dump())
        fatal("valid affine selector retry fixture did not verify");
    accepted = mir_try_selector(stream, mir_try_emit_affine_return);
    ok = ok && accepted == 1;
    bytes = read_stream(stream, text, sizeof(text));
    ok = ok && bytes > 7 && !memcmp(text, "prefix\n", 7);
    ok = ok &&
         strstr(text, "\textrn __stchk\n\tcall __stchk\n") != NULL &&
         strstr(text, "\tld l,(ix+4)\n\tld h,(ix+5)\n") != NULL &&
         strstr(text, "\tld e,(ix+6)\n\tld d,(ix+7)\n") != NULL &&
         strstr(text, "\tor a\n\tsbc hl,de\n") != NULL &&
         strstr(text, "\tld sp,ix\n\tpop ix\n\tret\n") != NULL;
    clear_selector_liveness();
    opt_stack_check = saved_stack_check;
    mir_stream_close(stream);
    return ok;
}

enum RepeatedAddMutation {
    REPEATED_FIRST_OPERATOR,
    REPEATED_SECOND_OPERATOR,
    REPEATED_INCREMENT_OPERATOR,
    REPEATED_COMPARE_OPERATOR,
    REPEATED_FACTOR_TYPE,
    REPEATED_TOTAL_TYPE,
    REPEATED_INDEX_POINTER_TYPE,
    REPEATED_FIRST_RESULT_TYPE,
    REPEATED_SECOND_RESULT_TYPE,
    REPEATED_INCREMENT_RESULT_TYPE,
    REPEATED_COMPARE_OPERAND_TYPE,
    REPEATED_COMPARE_SIGNEDNESS,
    REPEATED_INDEX_OBJECT_SIGNEDNESS,
    REPEATED_SIGNED_WORD_LIMIT,
    REPEATED_FACTOR_OFFSET,
    REPEATED_FACTOR_STORAGE,
    REPEATED_STORE_WIDTH,
    REPEATED_BRANCH_TARGET,
    REPEATED_BACKEDGE_TARGET,
    REPEATED_TOTAL_PHI_PREDECESSOR,
    REPEATED_INDEX_PHI_PREDECESSOR,
    REPEATED_EXTRA_BRANCH,
    REPEATED_EXTRA_RETURN,
    REPEATED_OBSERVABLE_STORE,
    REPEATED_TOTAL_STORAGE,
    REPEATED_INDEX_STORAGE,
    REPEATED_VOLATILE_PARAMETER,
    REPEATED_VLA_STATE,
    REPEATED_MUTATION_COUNT
};

static const char *repeated_add_mutation_name(int mutation)
{
    static const char *names[REPEATED_MUTATION_COUNT] = {
        "first operator", "second operator", "increment operator",
        "compare operator", "factor type", "total type",
        "index pointer type", "first result type", "second result type",
        "increment result type", "compare operand type",
        "compare signedness", "index object signedness", "signed word limit",
        "factor offset", "factor storage", "store width", "branch target",
        "backedge target",
        "total PHI predecessor", "index PHI predecessor", "extra branch",
        "extra return", "observable store", "total storage", "index storage",
        "volatile parameter", "VLA state"
    };

    return names[mutation];
}

static void setup_repeated_invariant_add(void)
{
    int instruction;

    mir_begin_function(
        "selector_repeated", "_selector_repeated", EMIT_SINK_FINAL, 0, 0, 0);
    mir.count = 37;
    mir.next_value = 11;
    mir.next_label = 4;
    mir.object_count = 3;
    mir.return_type = TYPE_INT;
    mir.declared_count = 3;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    memset(&mir.objects[1], 0, sizeof(mir.objects[1]));
    memset(&mir.objects[2], 0, sizeof(mir.objects[2]));
    strcpy(mir.objects[0].name, "factor");
    mir.objects[0].storage = SC_PARAM;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = 4;
    strcpy(mir.objects[1].name, "total");
    mir.objects[1].storage = SC_LOCAL;
    mir.objects[1].type = TYPE_INT;
    mir.objects[1].offset = -2;
    strcpy(mir.objects[2].name, "index");
    mir.objects[2].storage = SC_LOCAL;
    mir.objects[2].type = TYPE_INT;
    mir.objects[2].offset = -4;
    strcpy(mir.declared_names[0], "factor");
    strcpy(mir.declared_names[1], "total");
    strcpy(mir.declared_names[2], "index");
    mir.declared_types[0] = TYPE_INT;
    mir.declared_types[1] = TYPE_INT;
    mir.declared_types[2] = TYPE_INT;
    for (instruction = 0; instruction < mir.count; ++instruction) {
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
    mir.insns[1].opcode = MIR_PARAM;
    mir.insns[1].dst = 0;
    mir.insns[1].object = 0;
    strcpy(mir.insns[1].name, "factor");
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 0;
    mir.insns[4].opcode = MIR_STORE;
    mir.insns[4].src1 = 1;
    mir.insns[4].object = 1;
    mir.insns[4].memory_size = 2;
    strcpy(mir.insns[4].name, "total");
    mir.insns[6].opcode = MIR_CONST;
    mir.insns[6].dst = 2;
    mir.insns[6].immediate = 0;
    mir.insns[7].opcode = MIR_STORE;
    mir.insns[7].src1 = 2;
    mir.insns[7].object = 2;
    mir.insns[7].memory_size = 2;
    strcpy(mir.insns[7].name, "index");
    mir.insns[8].opcode = MIR_LABEL;
    mir.insns[8].label = 1;
    mir.insns[10].opcode = MIR_PHI;
    mir.insns[10].dst = 3;
    mir.insns[10].src1 = 1;
    mir.insns[10].src2 = 8;
    mir.insns[10].phi_pred1 = 0;
    mir.insns[10].phi_pred2 = 2;
    mir.insns[10].object = 1;
    strcpy(mir.insns[10].name, "total");
    mir.insns[11].opcode = MIR_PHI;
    mir.insns[11].dst = 4;
    mir.insns[11].src1 = 2;
    mir.insns[11].src2 = 10;
    mir.insns[11].phi_pred1 = 0;
    mir.insns[11].phi_pred2 = 2;
    mir.insns[11].object = 2;
    strcpy(mir.insns[11].name, "index");
    mir.insns[13].opcode = MIR_CONST;
    mir.insns[13].dst = 5;
    mir.insns[13].immediate = 5;
    mir.insns[15].opcode = MIR_BINARY;
    mir.insns[15].dst = 6;
    mir.insns[15].src1 = 4;
    mir.insns[15].src2 = 5;
    mir.insns[15].immediate = '<';
    mir.insns[15].secondary_offset = TYPE_INT;
    mir.insns[16].opcode = MIR_BRANCH_FALSE;
    mir.insns[16].src1 = 6;
    mir.insns[16].label = 3;
    mir.insns[19].opcode = MIR_BINARY;
    mir.insns[19].dst = 7;
    mir.insns[19].src1 = 3;
    mir.insns[19].src2 = 0;
    mir.insns[19].immediate = '+';
    mir.insns[19].secondary_offset = TYPE_INT;
    mir.insns[21].opcode = MIR_STORE;
    mir.insns[21].src1 = 7;
    mir.insns[21].object = 1;
    mir.insns[21].memory_size = 2;
    strcpy(mir.insns[21].name, "total");
    mir.insns[24].opcode = MIR_BINARY;
    mir.insns[24].dst = 8;
    mir.insns[24].src1 = 7;
    mir.insns[24].src2 = 0;
    mir.insns[24].immediate = '+';
    mir.insns[24].secondary_offset = TYPE_INT;
    mir.insns[26].opcode = MIR_STORE;
    mir.insns[26].src1 = 8;
    mir.insns[26].object = 1;
    mir.insns[26].memory_size = 2;
    strcpy(mir.insns[26].name, "total");
    mir.insns[28].opcode = MIR_LABEL;
    mir.insns[28].label = 2;
    mir.insns[30].opcode = MIR_CONST;
    mir.insns[30].dst = 9;
    mir.insns[30].immediate = 1;
    mir.insns[31].opcode = MIR_BINARY;
    mir.insns[31].dst = 10;
    mir.insns[31].src1 = 4;
    mir.insns[31].src2 = 9;
    mir.insns[31].immediate = '+';
    mir.insns[31].secondary_offset = TYPE_INT;
    mir.insns[32].opcode = MIR_STORE;
    mir.insns[32].src1 = 10;
    mir.insns[32].object = 2;
    mir.insns[32].memory_size = 2;
    strcpy(mir.insns[32].name, "index");
    mir.insns[33].opcode = MIR_JUMP;
    mir.insns[33].label = 1;
    mir.insns[34].opcode = MIR_LABEL;
    mir.insns[34].label = 3;
    mir.insns[36].opcode = MIR_RETURN;
    mir.insns[36].src1 = 3;
}

static void mutate_repeated_invariant_add(int mutation)
{
    switch (mutation) {
    case REPEATED_FIRST_OPERATOR: mir.insns[19].immediate = '-'; break;
    case REPEATED_SECOND_OPERATOR: mir.insns[24].immediate = '-'; break;
    case REPEATED_INCREMENT_OPERATOR: mir.insns[31].immediate = '-'; break;
    case REPEATED_COMPARE_OPERATOR: mir.insns[15].immediate = TOK_LE; break;
    case REPEATED_FACTOR_TYPE: mir.objects[0].type = TYPE_LONG; break;
    case REPEATED_TOTAL_TYPE: mir.objects[1].type = TYPE_LONG; break;
    case REPEATED_INDEX_POINTER_TYPE:
        mir.objects[2].type = type_add_ptr(TYPE_INT);
        break;
    case REPEATED_FIRST_RESULT_TYPE: mir.insns[19].type = TYPE_CHAR; break;
    case REPEATED_SECOND_RESULT_TYPE: mir.insns[24].type = TYPE_CHAR; break;
    case REPEATED_INCREMENT_RESULT_TYPE: mir.insns[31].type = TYPE_LONG; break;
    case REPEATED_COMPARE_OPERAND_TYPE:
        mir.insns[15].secondary_offset = TYPE_LONG;
        break;
    case REPEATED_COMPARE_SIGNEDNESS:
        mir.insns[15].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
        break;
    case REPEATED_INDEX_OBJECT_SIGNEDNESS:
        mir.objects[2].type = TYPE_INT | TYPE_UNSIGNED;
        break;
    case REPEATED_SIGNED_WORD_LIMIT: mir.insns[13].immediate = 32768; break;
    case REPEATED_FACTOR_OFFSET: mir.objects[0].offset = 125; break;
    case REPEATED_FACTOR_STORAGE: mir.objects[0].storage = SC_LOCAL; break;
    case REPEATED_STORE_WIDTH: mir.insns[21].memory_size = 1; break;
    case REPEATED_BRANCH_TARGET: mir.insns[16].label = 2; break;
    case REPEATED_BACKEDGE_TARGET: mir.insns[33].label = 0; break;
    case REPEATED_TOTAL_PHI_PREDECESSOR:
        mir.insns[10].phi_pred2 = 1;
        break;
    case REPEATED_INDEX_PHI_PREDECESSOR:
        mir.insns[11].phi_pred2 = 1;
        break;
    case REPEATED_EXTRA_BRANCH:
        mir.insns[17].opcode = MIR_BRANCH_FALSE;
        mir.insns[17].src1 = 6;
        mir.insns[17].label = 3;
        break;
    case REPEATED_EXTRA_RETURN:
        mir.insns[35].opcode = MIR_RETURN;
        mir.insns[35].src1 = 3;
        break;
    case REPEATED_OBSERVABLE_STORE:
        mir.insns[27].opcode = MIR_STORE;
        mir.insns[27].src1 = 8;
        mir.insns[27].object = 0;
        mir.insns[27].memory_size = 2;
        strcpy(mir.insns[27].name, "factor");
        break;
    case REPEATED_TOTAL_STORAGE: mir.objects[1].storage = SC_GLOBAL; break;
    case REPEATED_INDEX_STORAGE: mir.objects[2].storage = SC_GLOBAL; break;
    case REPEATED_VOLATILE_PARAMETER: mir.insns[1].memory_flags = 1; break;
    case REPEATED_VLA_STATE: mir.has_vla = 1; break;
    }
}

static int verify_repeated_add_mutation_isolation(void)
{
    MirStream *control = mir_stream_open();
    char control_text[1024];
    size_t control_bytes;
    int control_label_after;
    int mutation;
    int survivors = 0;
    int ok = control != NULL;

    if (!ok)
        fatal("cannot create repeated-add selector control stream");
    label_id = 121;
    setup_repeated_invariant_add();
    if (!mir_verify_and_dump())
        fatal("valid repeated-add selector fixture did not verify");
    if (!mir_try_selector(control, mir_try_emit_repeated_invariant_add_loop))
        fatal("valid repeated-add selector fixture was rejected");
    control_label_after = label_id;
    control_bytes = read_stream(control, control_text, sizeof(control_text));
    ok = ok &&
         strstr(control_text, "\tld l,(ix+6)\n\tld h,(ix+7)\n") != NULL &&
         strstr(control_text, "\tld hl,-5\n\tadd hl,bc\n") != NULL &&
         strstr(control_text, "\tpush iy\n\tpop hl\n\tadd hl,de\n") != NULL;
    clear_selector_liveness();

    for (mutation = 0; mutation < REPEATED_MUTATION_COUNT; ++mutation) {
        MirStream *retry = mir_stream_open();
        char retry_text[1024];
        size_t retry_bytes;
        int accepted;

        if (retry == NULL)
            fatal("cannot create repeated-add mutation stream");
        label_id = 121;
        mir_stream_puts("prefix\n", retry);
        setup_repeated_invariant_add();
        if (!mir_verify_and_dump())
            fatal("repeated-add mutation control did not verify");
        mutate_repeated_invariant_add(mutation);
        accepted = mir_try_selector(
            retry, mir_try_emit_repeated_invariant_add_loop);
        clear_selector_liveness();
        if (accepted) {
            ++survivors;
            fprintf(stderr, "SURVIVED repeated-add %s\n",
                    repeated_add_mutation_name(mutation));
        } else {
            ok = ok && mir_stream_size(retry) == 7 && label_id == 121;
            setup_repeated_invariant_add();
            if (!mir_verify_and_dump())
                fatal("repeated-add retry fixture did not verify");
            accepted = mir_try_selector(
                retry, mir_try_emit_repeated_invariant_add_loop);
            retry_bytes = read_stream(retry, retry_text, sizeof(retry_text));
            ok = ok && accepted == 1 && label_id == control_label_after;
            ok = ok && retry_bytes == control_bytes + 7;
            ok = ok && !memcmp(retry_text, "prefix\n", 7);
            ok = ok &&
                 !memcmp(retry_text + 7, control_text, control_bytes);
            clear_selector_liveness();
        }
        mir_stream_close(retry);
    }
    fprintf(stderr, "repeated-add mutation survivors=%d/%d\n",
            survivors, REPEATED_MUTATION_COUNT);
    mir_stream_close(control);
    return ok && survivors == 0;
}

static void setup_comparison_branch(int right_offset)
{
    int instruction;

    mir_begin_function(
        "selector_compare", "_selector_compare", EMIT_SINK_FINAL, 0, 0, 0);
    mir.count = 10;
    mir.next_value = 5;
    mir.next_label = 2;
    mir.object_count = 2;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    memset(&mir.objects[1], 0, sizeof(mir.objects[1]));
    mir.objects[0].storage = SC_PARAM;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = -128;
    mir.objects[1].storage = SC_PARAM;
    mir.objects[1].type = TYPE_INT;
    mir.objects[1].offset = right_offset;
    for (instruction = 0; instruction < mir.count; ++instruction) {
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
    mir.insns[1].opcode = MIR_PARAM;
    mir.insns[1].dst = 0;
    mir.insns[1].object = 0;
    mir.insns[2].opcode = MIR_PARAM;
    mir.insns[2].dst = 1;
    mir.insns[2].object = 1;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = TOK_EQ;
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].opcode = MIR_BRANCH_FALSE;
    mir.insns[4].src1 = 2;
    mir.insns[4].label = 1;
    mir.insns[5].opcode = MIR_CONST;
    mir.insns[5].dst = 3;
    mir.insns[5].immediate = 11;
    mir.insns[6].opcode = MIR_RETURN;
    mir.insns[6].src1 = 3;
    mir.insns[7].opcode = MIR_LABEL;
    mir.insns[7].label = 1;
    mir.insns[8].opcode = MIR_CONST;
    mir.insns[8].dst = 4;
    mir.insns[8].immediate = 22;
    mir.insns[9].opcode = MIR_RETURN;
    mir.insns[9].src1 = 4;
}

static int verify_comparison_offset_isolation(void)
{
    MirStream *control = mir_stream_open();
    MirStream *fallback = mir_stream_open();
    MirStream *retry = mir_stream_open();
    char control_text[1024];
    char fallback_text[1024];
    char retry_text[1024];
    size_t control_bytes;
    size_t fallback_bytes;
    size_t retry_bytes;
    int fallback_label_after;
    int accepted;
    int ok = control != NULL && fallback != NULL && retry != NULL;

    if (!ok)
        fatal("cannot create comparison selector isolation streams");

    label_id = 91;
    setup_comparison_branch(126);
    if (!mir_verify_and_dump())
        fatal("valid comparison selector fixture did not verify");
    accepted = mir_try_emit_z80(control);
    ok = ok && accepted == 1;
    control_bytes = read_stream(
        control, control_text, sizeof(control_text));
    ok = ok &&
         strstr(control_text, "\tld l,(ix-128)\n\tld h,(ix-127)\n") != NULL &&
         strstr(control_text, "\tld e,(ix+126)\n\tld d,(ix+127)\n") != NULL &&
         strstr(control_text, "\tor a\n\tsbc hl,de\n") != NULL;
    clear_selector_liveness();

    label_id = 91;
    setup_comparison_branch(127);
    if (!mir_verify_and_dump())
        fatal("comparison offset near-match did not verify");
    accepted = mir_try_selector(fallback, mir_try_emit_homed_scalar_cfg);
    ok = ok && accepted == 1;
    fallback_label_after = label_id;
    fallback_bytes = read_stream(
        fallback, fallback_text, sizeof(fallback_text));
    ok = ok && fallback_bytes != control_bytes;
    ok = ok && strstr(fallback_text, "(ix+128)") == NULL;
    clear_selector_liveness();

    label_id = 91;
    mir_stream_puts("prefix\n", retry);
    setup_comparison_branch(127);
    if (!mir_verify_and_dump())
        fatal("comparison offset fallback fixture did not verify");
    accepted = mir_try_emit_z80(retry);
    ok = ok && accepted == 1;
    retry_bytes = read_stream(retry, retry_text, sizeof(retry_text));
    ok = ok && label_id == fallback_label_after;
    ok = ok && retry_bytes == fallback_bytes + 7;
    ok = ok && !memcmp(retry_text, "prefix\n", 7);
    ok = ok && !memcmp(retry_text + 7, fallback_text, fallback_bytes);
    clear_selector_liveness();

    mir_stream_close(retry);
    mir_stream_close(fallback);
    mir_stream_close(control);
    return ok;
}

int main(void)
{
    MirStream *control = mir_stream_open();
    MirStream *retry = mir_stream_open();
    char control_text[256];
    char retry_text[256];
    size_t control_bytes;
    size_t retry_bytes;
    int control_label_after;
    int accepted;
    int ok = control != NULL && retry != NULL;

    if (!ok)
        fatal("cannot create selector isolation host streams");

    label_id = 41;
    accepted = mir_try_selector(control, accepting_candidate);
    ok = ok && accepted == 1;
    control_label_after = label_id;
    control_bytes = read_stream(
        control, control_text, sizeof(control_text));
    ok = ok &&
         strstr(control_text, "\textrn __mulu\n\tcall __mulu\n") != NULL &&
         strstr(control_text, "L42:\n\tret\n") != NULL;

    label_id = 41;
    mir_stream_puts("prefix\n", retry);
    accepted = mir_try_selector(retry, rejecting_candidate);
    ok = ok && accepted == 0;
    ok = ok && mir_stream_size(retry) == 7 && label_id == 41;
    accepted = mir_try_selector(retry, accepting_candidate);
    ok = ok && accepted == 1;
    retry_bytes = read_stream(retry, retry_text, sizeof(retry_text));
    ok = ok && label_id == control_label_after;
    ok = ok && retry_bytes == control_bytes + 7;
    ok = ok && !memcmp(retry_text, "prefix\n", 7);
    ok = ok && !memcmp(
        retry_text + 7, control_text, control_bytes);
    ok = ok && verify_affine_return_isolation();
    ok = ok && verify_comparison_offset_isolation();
    ok = ok && verify_repeated_add_mutation_isolation();

    mir_stream_close(retry);
    mir_stream_close(control);
    if (!ok) {
        fputs("FAIL selector candidate isolation\n", stderr);
        return 1;
    }
    puts("selector isolation host tests passed");
    return 0;
}
