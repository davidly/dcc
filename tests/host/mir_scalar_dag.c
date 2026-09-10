#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"

static int failures;

static void clear_liveness(void)
{
    free(mir.live_in);
    free(mir.live_out);
    mir.live_in = NULL;
    mir.live_out = NULL;
}

static void setup_homed_scalar_dag(void)
{
    int instruction;

    mir_begin_function(
        "scalar_dag_test", "_scalar_dag_test", EMIT_SINK_FINAL, 0, 0, 0);
    mir.count = 8;
    mir.next_value = 6;
    mir.next_label = 1;
    mir.object_count = 1;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    mir.objects[0].storage = SC_PARAM;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = 4;
    strcpy(mir.objects[0].name, "value");
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
    strcpy(mir.insns[1].name, "value");
    mir.insns[2].opcode = MIR_CONST;
    mir.insns[2].dst = 1;
    mir.insns[2].immediate = 2;
    mir.insns[3].opcode = MIR_BINARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 0;
    mir.insns[3].src2 = 1;
    mir.insns[3].immediate = '+';
    mir.insns[3].secondary_offset = TYPE_INT;
    mir.insns[4].opcode = MIR_UNARY;
    mir.insns[4].dst = 3;
    mir.insns[4].src1 = 2;
    mir.insns[4].immediate = '!';
    mir.insns[5].opcode = MIR_CONST;
    mir.insns[5].dst = 4;
    mir.insns[5].immediate = 1;
    mir.insns[6].opcode = MIR_BINARY;
    mir.insns[6].dst = 5;
    mir.insns[6].src1 = 3;
    mir.insns[6].src2 = 4;
    mir.insns[6].immediate = '+';
    mir.insns[6].secondary_offset = TYPE_INT;
    mir.insns[7].opcode = MIR_RETURN;
    mir.insns[7].src1 = 5;
    if (!mir_verify_and_dump())
        fatal("scalar DAG host fixture did not verify");
    mir.allocation_spill_count = 0;
    mir.allocation_colors[0] = MIR_COLOR_BC;
    mir.allocation_colors[1] = MIR_COLOR_DE;
    mir.allocation_colors[2] = MIR_COLOR_HL;
    mir.allocation_colors[3] = MIR_COLOR_BC;
    mir.allocation_colors[4] = MIR_COLOR_DE;
    mir.allocation_colors[5] = MIR_COLOR_HL;
    for (instruction = 0; instruction < mir.next_value; ++instruction)
        mir.allocation_spills[instruction] = -1;
}

static size_t read_stream(MirStream *stream, char *text, size_t capacity)
{
    size_t bytes;

    mir_stream_rewind(stream);
    memset(text, 0, capacity);
    bytes = mir_stream_read(text, 1, capacity - 1, stream);
    if (bytes >= capacity - 1)
        fatal("scalar DAG host fixture output overflow");
    return bytes;
}

static void expect_declined_retry(
    const char *name, const char *control_text, size_t control_bytes,
    int first_label)
{
    MirStream *stream = mir_stream_open();
    char retry_text[2048];
    size_t retry_bytes;
    int accepted;
    int ok;

    if (stream == NULL)
        fatal("cannot create scalar DAG retry stream");
    mir_extrn_begin_attempt();
    accepted = mir_try_emit_homed_scalar_dag(stream);
    ok = accepted == 0 &&
         mir_stream_tell(stream) == 0 &&
         mir_stream_size(stream) == 0 &&
         label_id == first_label;
    clear_liveness();
    setup_homed_scalar_dag();
    accepted = mir_try_emit_homed_scalar_dag(stream);
    retry_bytes = read_stream(stream, retry_text, sizeof(retry_text));
    ok = ok && accepted == 1 &&
         retry_bytes == control_bytes &&
         memcmp(retry_text, control_text, control_bytes) == 0;
    if (!ok) {
        fprintf(stderr, "FAIL homed scalar DAG %s transaction\n", name);
        ++failures;
    }
    mir_stream_close(stream);
    clear_liveness();
}

static void verify_homed_scalar_dag_preflight(void)
{
    MirStream *control;
    char control_text[2048];
    size_t control_bytes;
    int first_label;
    int saved_stack_check = opt_stack_check;

    opt_stack_check = 1;
    setup_homed_scalar_dag();
    control = mir_stream_open();
    if (control == NULL)
        fatal("cannot create scalar DAG control stream");
    first_label = label_id;
    mir_extrn_begin_attempt();
    if (!mir_try_emit_homed_scalar_dag(control))
        fatal("homed scalar DAG control was declined");
    control_bytes = read_stream(control, control_text, sizeof(control_text));
    if (strstr(control_text, "\textrn __stchk\n\tcall __stchk\n") == NULL ||
        label_id == first_label) {
        fprintf(stderr, "FAIL homed scalar DAG control output\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.allocation_colors[4] = MIR_COLOR_HL_DE;
    expect_declined_retry(
        "wide home", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[1].object = mir.object_count;
    expect_declined_retry(
        "parameter object", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.objects[0].storage = SC_LOCAL;
    expect_declined_retry(
        "parameter storage", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.objects[0].type = TYPE_FLOAT;
    expect_declined_retry(
        "parameter type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.allocation_colors[0] = MIR_COLOR_IY;
    mir.objects[0].offset = 127;
    expect_declined_retry(
        "parameter offset", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[5].type = type_add_ptr(TYPE_INT);
    expect_declined_retry(
        "value type", control_text, control_bytes, first_label);

    opt_stack_check = saved_stack_check;
}

int main(void)
{
    verify_homed_scalar_dag_preflight();
    if (failures != 0) {
        fprintf(stderr, "%d scalar DAG host test(s) failed\n", failures);
        return 1;
    }
    puts("scalar DAG host tests passed");
    return 0;
}
