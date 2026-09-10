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

static void setup_scalar_dag(void)
{
    int instruction;

    mir_begin_function(
        "scalar_value_test", "_scalar_value_test", EMIT_SINK_FINAL, 0, 0, 0);
    mir.count = 10;
    mir.next_value = 8;
    mir.next_label = 1;
    mir.object_count = 1;
    mir.return_type = TYPE_INT;
    memset(&mir.objects[0], 0, sizeof(mir.objects[0]));
    mir.objects[0].storage = SC_PARAM;
    mir.objects[0].type = TYPE_CHAR;
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
    mir.insns[1].type = TYPE_CHAR;
    strcpy(mir.insns[1].name, "value");
    mir.insns[2].opcode = MIR_UNARY;
    mir.insns[2].dst = 1;
    mir.insns[2].src1 = 0;
    mir.insns[2].type = TYPE_CHAR | TYPE_UNSIGNED;
    mir.insns[2].immediate = 0;
    mir.insns[3].opcode = MIR_UNARY;
    mir.insns[3].dst = 2;
    mir.insns[3].src1 = 1;
    mir.insns[3].immediate = '-';
    mir.insns[4].opcode = MIR_CONST;
    mir.insns[4].dst = 3;
    mir.insns[4].immediate = 3;
    mir.insns[5].opcode = MIR_BINARY;
    mir.insns[5].dst = 4;
    mir.insns[5].src1 = 2;
    mir.insns[5].src2 = 3;
    mir.insns[5].immediate = '*';
    mir.insns[5].secondary_offset = TYPE_INT;
    mir.insns[6].opcode = MIR_UNARY;
    mir.insns[6].dst = 5;
    mir.insns[6].src1 = 4;
    mir.insns[6].immediate = '!';
    mir.insns[7].opcode = MIR_CONST;
    mir.insns[7].dst = 6;
    mir.insns[7].immediate = 1;
    mir.insns[8].opcode = MIR_BINARY;
    mir.insns[8].dst = 7;
    mir.insns[8].src1 = 5;
    mir.insns[8].src2 = 6;
    mir.insns[8].immediate = '+';
    mir.insns[8].secondary_offset = TYPE_INT;
    mir.insns[9].opcode = MIR_RETURN;
    mir.insns[9].src1 = 7;
    if (!mir_verify_and_dump())
        fatal("scalar value host fixture did not verify");
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

static void expect_scalar_output(const char *name, const char *needle)
{
    MirStream *stream = mir_stream_open();
    char text[4096];
    size_t bytes;
    int ok;

    if (stream == NULL)
        fatal("cannot create scalar value stream");
    mir_extrn_begin_attempt();
    ok = mir_try_emit_scalar_dag(stream) == 1;
    bytes = read_stream(stream, text, sizeof(text));
    ok = ok && bytes > 0 &&
         (needle == NULL || strstr(text, needle) != NULL);
    if (!ok) {
        fprintf(stderr, "FAIL scalar value %s output\n", name);
        ++failures;
    }
    mir_stream_close(stream);
    clear_liveness();
}

static void expect_scalar_declined_retry(
    const char *name, const char *control_text, size_t control_bytes,
    int first_label)
{
    MirStream *stream = mir_stream_open();
    char retry_text[4096];
    size_t retry_bytes;
    int accepted;
    int ok;

    if (stream == NULL)
        fatal("cannot create scalar value retry stream");
    mir_extrn_begin_attempt();
    accepted = mir_try_emit_scalar_dag(stream);
    ok = accepted == 0 &&
         mir_stream_tell(stream) == 0 &&
         mir_stream_size(stream) == 0 &&
         label_id == first_label;
    clear_liveness();
    setup_scalar_dag();
    accepted = mir_try_emit_scalar_dag(stream);
    retry_bytes = read_stream(stream, retry_text, sizeof(retry_text));
    ok = ok && accepted == 1 &&
         retry_bytes == control_bytes &&
         memcmp(retry_text, control_text, control_bytes) == 0;
    if (!ok) {
        fprintf(stderr, "FAIL scalar value %s transaction\n", name);
        ++failures;
    }
    mir_stream_close(stream);
    clear_liveness();
}

static void verify_scalar_value_paths(void)
{
    static const int unary_operations[] = {0, '+', '-', '~', '!'};
    static const int binary_operations[] = {
        '+', '-', '&', '|', '^', '*', '/', '%',
        TOK_EQ, TOK_NE, '<', '>', TOK_LE, TOK_GE, TOK_SHL, TOK_SHR
    };
    static const int parameter_types[] = {
        TYPE_CHAR, TYPE_CHAR | TYPE_UNSIGNED, TYPE_BOOL, TYPE_INT
    };
    static const char *parameter_output[] = {
        "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n",
        "\tld h,0\n",
        "\tld a,l\n\tor a\n\tld hl,0\n",
        "\tld h,(ix+5)\n"
    };
    char name[64];
    size_t index;
    int saved_stack_check = opt_stack_check;

    opt_stack_check = 0;
    for (index = 0;
         index < sizeof(parameter_types) / sizeof(parameter_types[0]);
         ++index) {
        setup_scalar_dag();
        mir.insns[1].type = parameter_types[index];
        mir.objects[0].type = parameter_types[index];
        snprintf(name, sizeof(name), "parameter type %d", parameter_types[index]);
        expect_scalar_output(name, parameter_output[index]);
    }
    for (index = 0;
         index < sizeof(unary_operations) / sizeof(unary_operations[0]);
         ++index) {
        setup_scalar_dag();
        mir.insns[3].immediate = unary_operations[index];
        snprintf(name, sizeof(name), "unary operator %d", unary_operations[index]);
        expect_scalar_output(name, NULL);
    }
    for (index = 0;
         index < sizeof(binary_operations) / sizeof(binary_operations[0]);
         ++index) {
        setup_scalar_dag();
        mir.insns[5].immediate = binary_operations[index];
        snprintf(name, sizeof(name), "binary operator %d", binary_operations[index]);
        expect_scalar_output(name, NULL);
    }
    setup_scalar_dag();
    mir.insns[5].immediate = '/';
    expect_scalar_output("signed division", "\textrn __divs\n\tcall __divs\n");

    setup_scalar_dag();
    mir.insns[5].immediate = '/';
    mir.insns[5].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[5].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
    expect_scalar_output(
        "unsigned division", "\textrn __divu\n\tcall __divu\n");

    setup_scalar_dag();
    mir.insns[4].immediate = 4;
    mir.insns[5].immediate = '/';
    mir.insns[5].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[5].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
    expect_scalar_output("unsigned power-of-two division", "\tsrl h\n");

    setup_scalar_dag();
    mir.insns[5].immediate = '%';
    expect_scalar_output("signed remainder", "\textrn __mods\n\tcall __mods\n");

    setup_scalar_dag();
    mir.insns[5].immediate = '%';
    mir.insns[5].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[5].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
    expect_scalar_output(
        "unsigned remainder", "\textrn __modu\n\tcall __modu\n");

    setup_scalar_dag();
    mir.insns[5].immediate = TOK_SHR;
    expect_scalar_output("signed right shift", "\tsra h\n");

    setup_scalar_dag();
    mir.insns[5].immediate = TOK_SHR;
    mir.insns[5].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
    expect_scalar_output("unsigned right shift", "\tsrl h\n");
    opt_stack_check = saved_stack_check;
}

static void verify_scalar_value_preflight(void)
{
    MirStream *control;
    char control_text[4096];
    size_t control_bytes;
    int first_label;
    int saved_stack_check = opt_stack_check;

    opt_stack_check = 1;
    setup_scalar_dag();
    control = mir_stream_open();
    if (control == NULL)
        fatal("cannot create scalar value control stream");
    first_label = label_id;
    mir_extrn_begin_attempt();
    if (!mir_try_emit_scalar_dag(control))
        fatal("scalar value control was declined");
    control_bytes = read_stream(control, control_text, sizeof(control_text));
    if (strstr(control_text, "\textrn __stchk\n\tcall __stchk\n") == NULL ||
        strstr(control_text, "\textrn __mulu\n\tcall __mulu\n") == NULL ||
        strstr(control_text,
               "\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n\tld h,0\n") ==
            NULL ||
        label_id == first_label) {
        fprintf(stderr, "FAIL scalar value control output\n");
        ++failures;
    }
    mir_stream_close(control);
    clear_liveness();

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[3].immediate = '?';
    expect_scalar_declined_retry(
        "unary operator", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[5].immediate = '?';
    expect_scalar_declined_retry(
        "binary operator", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[1].object = mir.object_count;
    expect_scalar_declined_retry(
        "parameter object", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.objects[0].storage = SC_LOCAL;
    expect_scalar_declined_retry(
        "parameter storage", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[1].type = TYPE_CHAR | TYPE_UNSIGNED;
    expect_scalar_declined_retry(
        "parameter type mismatch", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[1].type = TYPE_INT;
    mir.objects[0].type = TYPE_INT;
    mir.objects[0].offset = 127;
    expect_scalar_declined_retry(
        "parameter offset", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[1].type = type_add_ptr(TYPE_INT);
    mir.objects[0].type = mir.insns[1].type;
    expect_scalar_declined_retry(
        "parameter pointer type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[5].secondary_offset = type_add_ptr(TYPE_INT);
    expect_scalar_declined_retry(
        "binary operand type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[2].type = TYPE_FLOAT;
    expect_scalar_declined_retry(
        "value type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.return_type = type_add_ptr(TYPE_INT);
    expect_scalar_declined_retry(
        "return type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_scalar_dag();
    mir.insns[3].src1 = mir.insns[3].dst;
    expect_scalar_declined_retry(
        "recursive depth", control_text, control_bytes, first_label);

    opt_stack_check = saved_stack_check;
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
    verify_scalar_value_paths();
    verify_scalar_value_preflight();
    verify_homed_scalar_dag_preflight();
    if (failures != 0) {
        fprintf(stderr, "%d scalar DAG host test(s) failed\n", failures);
        return 1;
    }
    puts("scalar DAG host tests passed");
    return 0;
}
