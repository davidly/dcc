#define main dcc_driver_main
#include "../../src/dcc/dcc.c"
#undef main
#include "dcc_mir_internal.h"
#include "dcc_ast_gen_internal.h"
#include "dcc_mir_machine_internal.h"
#include <limits.h>

static int failures;

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
    if (!ok) {
        fprintf(stderr, "FAIL common expression elimination contracts\n");
        ++failures;
    }
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
    verify_diamond_edge_liveness();
    verify_call_argument_liveness();
    verify_mir_stream_io();
    verify_ast_kind_names();
    verify_simple_mir_feature_queries();
    verify_parameter_emitters();
    verify_member_metadata_and_address();
    verify_five_call_arguments();
    verify_spilled_feature_defaults();
    verify_immediate_phi_return_forwarding();
    verify_common_expression_elimination();
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