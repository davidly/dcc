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

    mir_stream_close(retry);
    mir_stream_close(control);
    if (!ok) {
        fputs("FAIL selector candidate isolation\n", stderr);
        return 1;
    }
    puts("selector isolation host tests passed");
    return 0;
}
