#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"
#include <limits.h>

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
         label_id == first_label &&
         mir_extrn_should_emit_name("__stchk");
    mir_extrn_begin_attempt();
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

static void expect_homed_output(const char *name, const char *needle)
{
    MirStream *stream = mir_stream_open();
    char text[2048];
    size_t bytes;
    int ok;

    if (stream == NULL)
        fatal("cannot create homed scalar DAG stream");
    mir_extrn_begin_attempt();
    ok = mir_try_emit_homed_scalar_dag(stream) == 1;
    bytes = read_stream(stream, text, sizeof(text));
    ok = ok && bytes > 0 &&
         (needle == NULL || strstr(text, needle) != NULL);
    if (!ok) {
        fprintf(stderr, "FAIL homed scalar DAG %s output\n", name);
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

    setup_homed_scalar_dag();
    mir.objects[0].type = TYPE_CHAR;
    mir.insns[1].type = TYPE_CHAR;
    expect_homed_output(
        "signed byte parameter", "\trlca\n\tsbc a,a\n\tld h,a\n");

    setup_homed_scalar_dag();
    mir.objects[0].type = TYPE_CHAR | TYPE_UNSIGNED;
    mir.insns[1].type = TYPE_CHAR | TYPE_UNSIGNED;
    expect_homed_output("unsigned byte parameter", "\tld h,0\n");

    setup_homed_scalar_dag();
    mir.objects[0].type = TYPE_BOOL;
    mir.insns[1].type = TYPE_BOOL;
    expect_homed_output("boolean parameter", "\tld a,l\n\tor a\n\tld hl,0\n");

    setup_homed_scalar_dag();
    mir.objects[0].is_register = 1;
    expect_homed_output("register parameter", "\tret\n");

    setup_homed_scalar_dag();
    mir.allocation_colors[0] = MIR_COLOR_IY;
    expect_homed_output("IY home", "\tpush iy\n");

    setup_homed_scalar_dag();
    mir.local_bytes = 2;
    expect_homed_output("local frame", "\tld hl,-2\n\tadd hl,sp\n");

    setup_homed_scalar_dag();
    mir.insns[4].immediate = '-';
    expect_homed_output("unary negation", "\tsub l\n");

    setup_homed_scalar_dag();
    mir.insns[4].immediate = '~';
    expect_homed_output("unary complement", "\tcpl\n");

    setup_homed_scalar_dag();
    mir.insns[4].immediate = 0;
    mir.insns[4].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[6].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[6].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
    expect_homed_output("word signedness conversion", "\tadd hl,de\n");

    setup_homed_scalar_dag();
    mir.insns[2].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[3].type = TYPE_INT | TYPE_UNSIGNED;
    mir.insns[3].secondary_offset = TYPE_INT | TYPE_UNSIGNED;
    expect_homed_output("unsigned binary common type", "\tadd hl,de\n");

    setup_homed_scalar_dag();
    mir.insns[3].immediate = '-';
    expect_homed_output("binary subtraction", "\tsbc hl,de\n");

    setup_homed_scalar_dag();
    mir.insns[3].immediate = '&';
    expect_homed_output("binary and", "\tand d\n");

    setup_homed_scalar_dag();
    mir.insns[3].immediate = '|';
    expect_homed_output("binary or", "\tor d\n");

    setup_homed_scalar_dag();
    mir.insns[3].immediate = '^';
    expect_homed_output("binary xor", "\txor d\n");

    setup_homed_scalar_dag();
    mir.allocation_colors[0] = MIR_COLOR_HL;
    expect_homed_output("HL parameter home", "\tld a,(hl)\n\tinc hl\n");

    setup_homed_scalar_dag();
    mir.allocation_colors[0] = MIR_COLOR_DE;
    mir.allocation_colors[1] = MIR_COLOR_BC;
    expect_homed_output(
        "DE parameter home", "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n");

    setup_homed_scalar_dag();
    mir.allocation_colors[1] = MIR_COLOR_IY;
    expect_homed_output("IY constant home", "\tpop iy\n");

    setup_homed_scalar_dag();
    mir.insns[3].src2 = mir.insns[3].src1;
    expect_homed_output("shared BC operand", "\tadd hl,de\n");

    setup_homed_scalar_dag();
    mir.insns[3].src2 = mir.insns[3].src1;
    mir.allocation_colors[0] = MIR_COLOR_DE;
    mir.allocation_colors[1] = MIR_COLOR_BC;
    mir.allocation_colors[2] = MIR_COLOR_DE;
    expect_homed_output(
        "shared DE operand",
        "\tpush de\n\tpop hl\n\tadd hl,de\n\tex de,hl\n");

    setup_homed_scalar_dag();
    mir.insns[5].opcode = MIR_UNARY;
    mir.insns[5].src1 = 0;
    mir.insns[5].immediate = '+';
    mir.allocation_colors[0] = MIR_COLOR_DE;
    mir.allocation_colors[1] = MIR_COLOR_BC;
    expect_homed_output(
        "live DE binary left",
        "\tpush de\n\tpush de\n\tpop hl\n\tld d,b\n\tld e,c\n"
        "\tadd hl,de\n\tpop de\n");

    setup_homed_scalar_dag();
    mir.insns[5].opcode = MIR_UNARY;
    mir.insns[5].src1 = 2;
    mir.insns[5].immediate = '+';
    mir.insns[5].type = TYPE_INT;
    mir.allocation_colors[2] = MIR_COLOR_DE;
    expect_homed_output(
        "live DE unary source",
        "\tpush hl\n\tpush de\n\tpop hl\n\tld a,h\n");

    opt_stack_check = 0;
    setup_homed_scalar_dag();
    expect_homed_output("no stack check", "\tret\n");
    opt_stack_check = 1;

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

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[1].type = TYPE_CHAR;
    expect_declined_retry(
        "parameter instruction type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    strcpy(mir.insns[1].name, "other");
    expect_declined_retry(
        "parameter name", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[1].name[0] = 0;
    mir.objects[0].name[0] = 0;
    expect_declined_retry(
        "empty parameter name", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    memset(mir.insns[1].name, 'x', sizeof(mir.insns[1].name));
    expect_declined_retry(
        "unterminated parameter name",
        control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.objects[0].entry_value = 1;
    expect_declined_retry(
        "parameter entry value", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.declared_count = 1;
    strcpy(mir.declared_names[0], "value");
    mir.declared_is_volatile[0] = 1;
    expect_declined_retry(
        "volatile parameter", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.declared_count = 1;
    strcpy(mir.declared_names[0], "value");
    mir.declared_storage[0] = SC_PARAM;
    mir.declared_types[0] = TYPE_CHAR;
    mir.declared_is_volatile[0] = 0;
    expect_declined_retry(
        "declared parameter type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.declared_count = 2;
    strcpy(mir.declared_names[0], "value");
    strcpy(mir.declared_names[1], "value");
    mir.declared_storage[0] = SC_PARAM;
    mir.declared_storage[1] = SC_PARAM;
    mir.declared_types[0] = TYPE_INT;
    mir.declared_types[1] = TYPE_INT;
    mir.declared_is_volatile[0] = 0;
    mir.declared_is_volatile[1] = 1;
    expect_declined_retry(
        "duplicate volatile declaration",
        control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.declared_count = 1;
    memset(mir.declared_names[0], 'x', sizeof(mir.declared_names[0]));
    expect_declined_retry(
        "unterminated declaration name",
        control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[3].src2 = 4;
    expect_declined_retry(
        "forward reference", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.allocation_colors[1] = MIR_COLOR_HL;
    expect_declined_retry(
        "HL right operand", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[3].src1 = mir.next_value;
    expect_declined_retry(
        "source value bound", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[2].opcode = MIR_NOP;
    expect_declined_retry(
        "missing definition", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[5].dst = 1;
    expect_declined_retry(
        "duplicate definition", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[4].src1 = mir.insns[4].dst;
    expect_declined_retry(
        "self reference", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[5].opcode = MIR_UNARY;
    mir.insns[5].src1 = 2;
    mir.insns[5].immediate = '+';
    mir.insns[5].type = TYPE_INT;
    expect_declined_retry(
        "destructive shared source", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[5].opcode = MIR_UNARY;
    mir.insns[5].src1 = 0;
    mir.insns[5].immediate = '+';
    mir.allocation_colors[0] = MIR_COLOR_HL;
    mir.allocation_colors[1] = MIR_COLOR_BC;
    mir.allocation_colors[2] = MIR_COLOR_IY;
    expect_declined_retry(
        "live HL binary left",
        control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[4].immediate = 0;
    mir.insns[4].type = TYPE_CHAR;
    expect_declined_retry(
        "narrowing cast", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[3].secondary_offset = TYPE_CHAR;
    expect_declined_retry(
        "binary common type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[3].type = TYPE_CHAR;
    expect_declined_retry(
        "binary result type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[4].immediate = '-';
    mir.insns[4].type = TYPE_CHAR;
    expect_declined_retry(
        "unary result type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[4].type = TYPE_INT | TYPE_UNSIGNED;
    expect_declined_retry(
        "logical result type", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[4].immediate = '*';
    expect_declined_retry(
        "unary operator", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[3].immediate = '*';
    expect_declined_retry(
        "binary operator", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[5].type = TYPE_CHAR | TYPE_UNSIGNED;
    mir.insns[5].immediate = 0x101;
    expect_declined_retry(
        "unnormalized constant", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.allocation_colors[0] = MIR_COLOR_DE;
    mir.allocation_colors[1] = MIR_COLOR_DE;
    expect_declined_retry(
        "overlapping homes", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    {
        struct MirInsn temporary = mir.insns[6];
        mir.insns[6] = mir.insns[7];
        mir.insns[7] = temporary;
        mir.insns[6].successor_count = 0;
        mir.insns[7].successor_count = 1;
        mir.insns[7].successors[0] = 8;
    }
    expect_declined_retry(
        "instruction after return", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[0].label = 1;
    expect_declined_retry(
        "entry label", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[0].opcode = MIR_NOP;
    mir.insns[0].dst = 0;
    expect_declined_retry(
        "NOP destination", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.next_label = 2;
    expect_declined_retry(
        "label dimension", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[7].successor_count = 1;
    mir.insns[7].successors[0] = 0;
    expect_declined_retry(
        "successor metadata", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[6].opcode = MIR_RETURN;
    mir.insns[6].src1 = 3;
    expect_declined_retry(
        "multiple returns", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.count = 3;
    mir.next_value = 1;
    mir.object_count = 0;
    mir.insns[1].opcode = MIR_CONST;
    mir.insns[1].dst = 0;
    mir.insns[1].object = -1;
    mir.insns[1].immediate = 1;
    mir.insns[2].opcode = MIR_RETURN;
    mir.insns[2].dst = -1;
    mir.insns[2].src1 = 0;
    mir.insns[2].successor_count = 0;
    expect_declined_retry(
        "constant-only graph", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[7].type = TYPE_INT | TYPE_UNSIGNED;
    expect_homed_output("unused return instruction type", "\tret\n");

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.count = mir.capacity + 1;
    expect_declined_retry(
        "instruction capacity", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.count = INT_MAX;
    expect_declined_retry(
        "extreme instruction count", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.next_value = mir.allocation_capacity + 1;
    expect_declined_retry(
        "allocation capacity", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.next_value = INT_MAX;
    expect_declined_retry(
        "extreme value count", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.next_label = INT_MAX;
    expect_declined_retry(
        "extreme label count", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.objects[0].offset = INT_MAX;
    expect_declined_retry(
        "extreme parameter offset",
        control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.object_count = INT_MAX;
    expect_declined_retry(
        "extreme object count", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.local_bytes = INT_MAX;
    mir.dead_local_suffix_bytes = INT_MIN;
    expect_declined_retry(
        "extreme frame dimensions", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[2].opcode = MIR_CALL;
    expect_declined_retry(
        "call near match", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[2].opcode = MIR_STORE;
    expect_declined_retry(
        "store near match", control_text, control_bytes, first_label);

    label_id = first_label;
    setup_homed_scalar_dag();
    mir.insns[2].opcode = MIR_BRANCH_FALSE;
    expect_declined_retry(
        "branch near match", control_text, control_bytes, first_label);

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
