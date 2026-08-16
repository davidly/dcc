/* dcc_mir_machine_scanners.c - strict scanner/parser schedules. */

#include "dcc_mir_machine_internal.h"

struct MirCommentScanSchedule {
    struct Sym *state_root;
    int state_root_offset;
    int source_offset;
    int length_offset;
    int cursor_offset;
    int line_offset;
};

struct MirBoundedStringMatchSchedule {
    int pointer_stack_offset;
    int expected[5];
};

struct MirDecimalLongScanSchedule {
    int pointer_stack_offset;
};

struct MirStarCommentScanSchedule {
    struct Sym *source_root;
    struct Sym *length_root;
    struct Sym *cursor_root;
    struct Sym *line_root;
    int source_offset;
    int length_offset;
    int cursor_offset;
    int line_offset;
    int opening_character;
    int closing_character;
};

struct MirWhitespaceScanSchedule {
    struct Sym *state_root;
    struct Sym *space_function;
    int state_root_offset;
    int source_offset;
    int length_offset;
    int cursor_offset;
    int line_offset;
};

struct MirActionDecodeSchedule {
    struct Sym *trim_function;
    struct Sym *prefix_function;
    struct Sym *label_function;
    struct Sym *search_function;
    struct Sym *assignment_function;
    int statement_stack_offset;
    int text_stack_offset;
    int action_offset;
    int target_offset;
    int default_action;
    int goto_action;
    int return_action;
    int goto_string_ids[2];
    int return_string_id;
    int separator;
    int assignment_mode;
};

struct MirBufferedDeclarationSchedule {
    struct Sym *count_root;
    struct Sym *statements_root;
    struct Sym *copy_function;
    struct Sym *trim_function;
    struct Sym *prefix_function;
    struct Sym *declaration_function;
    int count_offset;
    int statements_offset;
    int cursor_offset;
    int buffer_offset;
    int frame_bytes;
    int record_stride;
    int text_offset;
    int string_ids[3];
    int declaration_types[3];
};

struct MirSymbolFindSchedule {
    struct Sym *symbols_root;
    struct Sym *count_root;
    struct Sym *memory_top_root;
    struct Sym *compare_function;
    struct Sym *error_function;
    struct Sym *copy_function;
    int symbols_offset;
    int count_offset;
    int memory_top_offset;
    int name_stack_offset;
    int record_stride;
    int name_field_offset;
    int name_field_size;
    int scalar_field_offset;
    int base_field_offset;
    int size_field_offset;
    int symbol_limit;
    int memory_limit;
    int symbol_error_string_id;
    int memory_error_string_id;
};

struct MirSymbolInsertSchedule {
    struct Sym *count_root;
    struct Sym *symbols_root;
    struct Sym *error_function;
    struct Sym *copy_function;
    int count_root_offset;
    int symbols_root_offset;
    int name_stack_offset;
    int kind_stack_offset;
    int scope_stack_offset;
    int symbol_limit;
    int record_stride;
    int name_size;
    int kind_offset;
    int scope_offset;
    int element_size_offset;
    int size_offset;
    int element_size;
    int error_string_id;
};

struct MirBreadthFirstPathSchedule {
    struct Sym *board;
    int predecessor_declaration;
    int queue_declaration;
    int predecessor_offset;
    int queue_offset;
    int board_offset;
    int parameter_stack_offsets[6];
    char predecessor_assembly_name[64];
    char queue_assembly_name[64];
};

struct MirStateMember {
    struct Sym *root;
    int root_offset;
    int member_offset;
};

struct MirGlobalScalar {
    struct Sym *root;
    int offset;
    int type;
};

struct MirSquareGridLineSumSchedule {
    struct MirGlobalScalar table;
    int x_stack_offset;
    int y_stack_offset;
    int dimension;
};

struct MirVlaConstantFillSumSchedule {
    int rows_stack_offset;
    int row_bytes;
    int elements_per_row;
};

struct MirVlaAffineFillSumSchedule {
    int rows_stack_offset;
    int rank;
    int row_bytes;
    int elements_per_row;
};

enum MirLongIndexByteLoopKind {
    MIR_LONG_INDEX_BYTE_COUNT,
    MIR_LONG_INDEX_BYTE_COPY
};

struct MirLongIndexByteLoopSchedule {
    int kind;
    int input_stack_offset;
    struct Sym *output_root;
    int output_offset;
    unsigned long input_initial;
    unsigned long output_initial;
};

struct MirLongIndexCommentCopySchedule {
    int input_stack_offset;
    struct Sym *output_root;
    int output_offset;
    int comment_character;
    int newline_character;
    int discarded_character;
};

struct MirLongIndexWordCountSchedule {
    struct Sym *classify_function;
    int input_stack_offset;
    int word_character;
};

struct MirByteRecordCopySchedule {
    int destination_stack_offset;
    int source_stack_offset;
    int width;
};

struct MirSquareTextSchedule {
    int file_stack_offset;
    int rank_stack_offset;
    int file_first;
    int file_last;
    int rank_first;
    int rank_last;
    int row_width;
    int invalid_square;
};

struct MirMoveTextSchedule {
    struct Sym *lower_function;
    int move_stack_offset;
    int text_stack_offset;
    int from_offset;
    int to_offset;
    int promotion_offset;
    int file_first;
    int rank_first;
    int file_mask;
    int rank_shift;
};

struct MirBoardAttackSchedule {
    struct Sym *board;
    struct Sym *knight_directions;
    struct Sym *king_directions;
    struct Sym *slider_function;
    int square_stack_offset;
    int side_stack_offset;
    int board_size;
    int direction_count;
    int white_side;
    int black_side;
    int white_pawn;
    int black_pawn;
    int knight_piece;
    int king_piece;
    int upper_first;
    int upper_last;
    int lower_first;
    int lower_last;
    int slider_directions[8];
    int slider_pieces[8][2];
};

struct MirMoveApplySchedule {
    struct Sym *board;
    struct Sym *side;
    struct Sym *en_passant_square;
    struct Sym *castle_rights;
    struct Sym *piece_side_function;
    struct Sym *upcase_function;
    int move_stack_offset;
    int from_offset;
    int to_offset;
    int piece_offset;
    int captured_offset;
    int promotion_offset;
    int flag_offset;
    int old_en_passant_offset;
    int old_castle_rights_offset;
    int empty_square;
    int white_side;
    int en_passant_flag;
    int castle_flag;
    int pawn_piece;
};

struct MirMoveUndoSchedule {
    struct Sym *board;
    struct Sym *side;
    struct Sym *en_passant_square;
    struct Sym *castle_rights;
    struct Sym *piece_side_function;
    int move_stack_offset;
    int from_offset;
    int to_offset;
    int piece_offset;
    int captured_offset;
    int flag_offset;
    int old_en_passant_offset;
    int old_castle_rights_offset;
    int empty_square;
    int white_side;
    int en_passant_flag;
    int castle_flag;
};

struct MirMoveParseSchedule {
    struct Sym *length_function;
    struct Sym *lower_function;
    struct Sym *square_function;
    struct Sym *board;
    struct Sym *side;
    int text_stack_offset;
    int output_stack_offset;
    int from_offset;
    int to_offset;
    int piece_offset;
    int captured_offset;
    int promotion_offset;
    int white_side;
    int lower_promotion;
    int upper_promotion;
};

struct MirInvariantByteSumSchedule {
    int pointer_stack_offset;
    int count;
    int element_unsigned;
};

struct MirPrintableByteSanitizeSchedule {
    int pointer_stack_offset;
    int lower_bound;
    int upper_bound;
    int replacement;
};

struct MirSlidingMaximumSchedule {
    int input_stack_offset;
    int length_stack_offset;
    int window_stack_offset;
    int output_stack_offset;
    int queue_count;
};

struct MirBasicLexerSchedule {
    struct Sym *cursor;
    struct Sym *token;
    struct Sym *integer_value;
    struct Sym *text;
    struct Sym *skip_function;
    struct Sym *alpha_function;
    struct Sym *upper_function;
    struct Sym *alnum_function;
    struct Sym *digit_function;
    struct Sym *format_function;
    struct Sym *copy_function;
    int integer_format_string_id;
    int operator_string_ids[4];
    int token_identifier;
    int token_integer;
    int token_string;
    int token_le;
    int token_ge;
    int token_ne;
    int token_div;
    int token_limit;
};

struct MirLineSplitSchedule {
    struct Sym *source;
    struct Sym *lines;
    struct Sym *line_count;
    struct Sym *atoi_function;
    struct Sym *digit_function;
    struct Sym *allocate_function;
    struct Sym *die_function;
    struct Sym *copy_function;
    int record_stride;
    int number_offset;
    int text_offset;
    int pc_offset;
    int line_limit;
    int allocation_string_id;
    int line_limit_string_id;
};

struct MirTrimSchedule {
    struct Sym *space_function;
    struct Sym *length_function;
    int string_stack_offset;
};

struct MirPreprocessorSchedule {
    struct Sym *state;
    struct Sym *length_function;
    struct Sym *allocate_function;
    struct Sym *die_function;
    struct Sym *alpha_function;
    struct Sym *compare_function;
    struct Sym *alnum_function;
    struct Sym *space_function;
    struct Sym *duplicate_function;
    struct Sym *search_function;
    struct Sym *digit_function;
    struct Sym *free_function;
    int input_stack_offset;
    int name_offset;
    int value_offset;
    int identifier_offset;
    int definition_names_offset;
    int definition_values_offset;
    int conditional_active_offset;
    int oom_string_id;
    int define_string_id;
    int if_string_id;
    int else_string_id;
    int endif_string_id;
    int true_string_id;
    int false_string_id;
    int one_string_id;
    int zero_string_id;
    int text_limit;
    int definition_limit;
    int conditional_limit;
};

struct MirCommentStripSchedule {
    struct Sym *length_function;
    struct Sym *allocate_function;
    struct Sym *error_function;
    int input_stack_offset;
    int error_string_id;
};

struct MirNeighborWarningSchedule {
    struct Sym *cave;
    struct Sym *print_function;
    int cave_offset;
    int game_stack_offset;
    int location_offset;
    int string_ids[3];
    char print_name[64];
};

struct MirBoundedDecimalParseSchedule {
    struct Sym *space_function;
    struct Sym *digit_function;
    int text_stack_offset;
    int output_stack_offset;
    int maximum_value;
};

struct MirBoundedUppercaseSchedule {
    struct Sym *upper_function;
    int destination_stack_offset;
    int source_stack_offset;
    int maximum_length;
};

struct MirHexWordParseSchedule {
    int text_stack_offset;
};

struct MirDigitLabelSchedule {
    struct Sym *digit_function;
    struct Sym *convert_function;
    int string_stack_offset;
};

static int mir_machine_three_call_arguments(
    const struct MirInsn *call, int arguments[3])
{
    int count = 0;
    int instruction;

    arguments[0] = arguments[1] = arguments[2] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 3 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 3;
}

static int mir_machine_two_call_arguments(
    const struct MirInsn *call, int arguments[2])
{
    int count = 0;
    int instruction;

    arguments[0] = arguments[1] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= 2 || arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == 2;
}

static int mir_machine_single_call_argument(
    const struct MirInsn *call, int *argument)
{
    int count = 0;
    int instruction;

    *argument = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        if (arg->immediate != 0 || *argument >= 0)
            return 0;
        *argument = arg->src1;
        ++count;
    }
    return count == 1;
}

static int mir_scanner_call_arguments(
    const struct MirInsn *call, int expected_count, int *arguments)
{
    int count = 0;
    int instruction;
    int argument;

    for (argument = 0; argument < expected_count; ++argument)
        arguments[argument] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= expected_count ||
            arguments[index] >= 0)
            return 0;
        arguments[index] = arg->src1;
        ++count;
    }
    return count == expected_count;
}

static int mir_match_byte_record_copy_schedule(
    struct MirByteRecordCopySchedule *plan)
{
    static const int expected_opcodes[51] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT
    };
    static const int destination_members[8] = {
        4, 10, 16, 22, 28, 34, 40, 46
    };
    static const int source_members[8] = {
        6, 12, 18, 24, 30, 36, 42, 48
    };
    static const int source_loads[8] = {
        7, 13, 19, 25, 31, 37, 43, 49
    };
    static const int destination_stores[8] = {
        8, 14, 20, 26, 32, 38, 44, 50
    };
    const struct MirInsn *destination = &mir.insns[1];
    const struct MirInsn *source = &mir.insns[2];
    int instruction;
    int field;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 51 || mir_cfg_block_count() != 1 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "byte-record-copy-schedule", "opcodes");
    if (destination->opcode != MIR_PARAM ||
        source->opcode != MIR_PARAM ||
        type_ptr_depth(destination->type) != 1 ||
        source->type != destination->type ||
        type_size(destination->type) != 2 ||
        mir_machine_pointee_is_volatile(destination) ||
        mir_machine_pointee_is_volatile(source) ||
        !mir_machine_parameter_value_offset(
            destination->dst, &plan->destination_stack_offset) ||
        !mir_machine_parameter_value_offset(
            source->dst, &plan->source_stack_offset) ||
        plan->destination_stack_offset != 2 ||
        plan->source_stack_offset != 4)
        return mir_machine_reject(
            "byte-record-copy-schedule", "parameters");

    plan->width = 8;
    for (field = 0; field < plan->width; ++field) {
        const struct MirInsn *destination_member =
            &mir.insns[destination_members[field]];
        const struct MirInsn *source_member =
            &mir.insns[source_members[field]];
        const struct MirInsn *load =
            &mir.insns[source_loads[field]];
        const struct MirInsn *store =
            &mir.insns[destination_stores[field]];

        if (destination_member->src1 != destination->dst ||
            source_member->src1 != source->dst ||
            destination_member->immediate != field ||
            source_member->immediate != field ||
            destination_member->memory_size != 1 ||
            source_member->memory_size != 1 ||
            destination_member->bit_width != 0 ||
            source_member->bit_width != 0 ||
            (destination_member->memory_flags & (1 | 8)) != 0 ||
            (source_member->memory_flags & (1 | 8)) != 0 ||
            load->src1 != source_member->dst ||
            load->memory_size != 1 ||
            load->bit_width != 0 ||
            (load->memory_flags & (1 | 8)) != 0 ||
            type_size(load->type) != 1 ||
            store->src1 != destination_member->dst ||
            store->src2 != load->dst ||
            store->memory_size != 1 ||
            store->bit_width != 0 ||
            (store->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "byte-record-copy-schedule", "fields");
    }
    return 1;
}

static void mir_emit_byte_record_copy_schedule(
    MirStream *out, const struct MirByteRecordCopySchedule *plan)
{
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tpop af\n\tpop de\n\tpop hl\n"
            "\tpush hl\n\tpush de\n\tpush af\n"
            "\tld bc,%d\n\tldir\n\tret\n",
            plan->width);
}

static int mir_scanner_control_edges_match(
    const int (*edges)[2], int edge_count)
{
    int edge;

    for (edge = 0; edge < edge_count; ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return 0;
    return 1;
}

static int mir_match_square_text_schedule(
    struct MirSquareTextSchedule *plan)
{
    static const int expected_opcodes[75] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_RETURN,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_RETURN, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BINARY, MIR_RETURN
    };
    static const int control_edges[][2] = {
        {7, 11}, {10, 26}, {16, 20}, {19, 22},
        {25, 26}, {28, 32}, {37, 41}, {40, 56},
        {46, 50}, {49, 52}, {55, 56}, {58, 62}
    };
    const struct MirInsn *file = &mir.insns[1];
    const struct MirInsn *rank = &mir.insns[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 75 || mir_cfg_block_count() != 17 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "square-text-schedule", "opcodes");
    if (!mir_scanner_control_edges_match(
            control_edges,
            (int)(sizeof(control_edges) / sizeof(control_edges[0]))))
        return mir_machine_reject(
            "square-text-schedule", "control-flow");
    if (file->type != rank->type ||
        type_ptr_depth(file->type) != 0 ||
        (file->type & 15) != TYPE_CHAR ||
        type_size(file->type) != 1 ||
        !mir_machine_named_nonvolatile(file) ||
        !mir_machine_named_nonvolatile(rank) ||
        !mir_machine_parameter_value_offset(
            file->dst, &plan->file_stack_offset) ||
        !mir_machine_parameter_value_offset(
            rank->dst, &plan->rank_stack_offset) ||
        plan->file_stack_offset != 2 ||
        plan->rank_stack_offset != 4)
        return mir_machine_reject(
            "square-text-schedule", "parameters");

    plan->file_first = (int)mir.insns[4].immediate;
    plan->file_last = (int)mir.insns[13].immediate;
    plan->rank_first = (int)mir.insns[34].immediate;
    plan->rank_last = (int)mir.insns[43].immediate;
    plan->invalid_square = (int)mir.insns[30].immediate;
    plan->row_width = (int)mir.insns[67].immediate;
    if (!mir_machine_constant_equals(
            mir.insns[4].dst, plan->file_first) ||
        !mir_machine_constant_equals(
            mir.insns[13].dst, plan->file_last) ||
        !mir_machine_constant_equals(
            mir.insns[34].dst, plan->rank_first) ||
        !mir_machine_constant_equals(
            mir.insns[43].dst, plan->rank_last) ||
        !mir_machine_constant_equals(
            mir.insns[30].dst, plan->invalid_square) ||
        !mir_machine_constant_equals(
            mir.insns[60].dst, plan->invalid_square) ||
        !mir_machine_constant_equals(
            mir.insns[64].dst, plan->rank_first) ||
        !mir_machine_constant_equals(
            mir.insns[67].dst, plan->row_width) ||
        !mir_machine_constant_equals(
            mir.insns[70].dst, plan->file_first) ||
        plan->file_last != plan->file_first + 7 ||
        plan->rank_last != plan->rank_first + 7 ||
        plan->row_width != 8 ||
        plan->invalid_square != 65535)
        return mir_machine_reject(
            "square-text-schedule", "constants");
    if (mir.insns[5].src1 != file->dst ||
        mir.insns[6].src1 != mir.insns[5].dst ||
        mir.insns[6].src2 != mir.insns[4].dst ||
        mir.insns[6].immediate != '<' ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[14].src1 != file->dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[15].immediate != '>' ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[35].src1 != rank->dst ||
        mir.insns[36].src1 != mir.insns[35].dst ||
        mir.insns[36].src2 != mir.insns[34].dst ||
        mir.insns[36].immediate != '<' ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[44].src1 != rank->dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].src2 != mir.insns[43].dst ||
        mir.insns[45].immediate != '>' ||
        mir.insns[46].src1 != mir.insns[45].dst ||
        mir.insns[65].src1 != rank->dst ||
        mir.insns[66].src1 != mir.insns[65].dst ||
        mir.insns[66].src2 != mir.insns[64].dst ||
        mir.insns[66].immediate != '-' ||
        mir.insns[68].src1 != mir.insns[66].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        mir.insns[68].immediate != '*' ||
        mir.insns[71].src1 != file->dst ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[72].src2 != mir.insns[70].dst ||
        mir.insns[72].immediate != '-' ||
        mir.insns[73].src1 != mir.insns[68].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        mir.insns[73].immediate != '+' ||
        mir.insns[74].src1 != mir.insns[73].dst)
        return mir_machine_reject(
            "square-text-schedule", "data-flow");
    return 1;
}

static void mir_emit_square_text_schedule(
    MirStream *out, const struct MirSquareTextSchedule *plan)
{
    int invalid = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tpop af\n\tpop hl\n\tpop de\n"
            "\tpush de\n\tpush hl\n\tpush af\n"
            "\tld c,l\n"
            "\tld a,l\n\tcp %d\n\tjp c,L%d\n"
            "\tcp %d\n\tjp nc,L%d\n"
            "\tld a,e\n\tcp %d\n\tjp c,L%d\n"
            "\tcp %d\n\tjp nc,L%d\n"
            "\tsub %d\n\tld l,a\n\tld h,0\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
            "\tld a,c\n\tsub %d\n\tld e,a\n\tld d,0\n"
            "\tadd hl,de\n\tret\n",
            plan->file_first, invalid,
            plan->file_last + 1, invalid,
            plan->rank_first, invalid,
            plan->rank_last + 1, invalid,
            plan->rank_first, plan->file_first);
    mir_stream_printf(out,
            "L%d:\n\tld hl,%d\n\tret\n",
            invalid, plan->invalid_square);
}

static int mir_match_move_text_schedule(
    struct MirMoveTextSchedule *plan)
{
    static const int expected_opcodes[95] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_CONST, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL
    };
    static const int output_constants[7] = {
        4, 19, 34, 49, 64, 77, 88
    };
    static const int output_offsets[7] = {
        0, 1, 2, 3, 4, 4, 5
    };
    static const int output_addresses[7] = {
        5, 20, 35, 50, 65, 78, 89
    };
    static const int member_addresses[6] = {
        8, 23, 38, 53, 70, 80
    };
    static const int member_loads[6] = {
        9, 24, 39, 54, 71, 81
    };
    const struct MirInsn *move = &mir.insns[1];
    const struct MirInsn *text = &mir.insns[2];
    const struct MirInsn *call = &mir.insns[84];
    int call_argument;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 95 || mir_cfg_block_count() != 2 ||
        mir.has_vla || mir.local_bytes != 32 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "move-text-schedule", "opcodes");
    if (mir.insns[75].label != mir.insns[94].label)
        return mir_machine_reject(
            "move-text-schedule", "control-flow");
    if (type_ptr_depth(move->type) != 1 ||
        type_ptr_depth(text->type) != 1 ||
        (text->type & 15) != TYPE_CHAR ||
        type_size(move->type) != 2 ||
        type_size(text->type) != 2 ||
        mir_machine_pointee_is_volatile(move) ||
        mir_machine_pointee_is_volatile(text) ||
        !mir_machine_parameter_value_offset(
            move->dst, &plan->move_stack_offset) ||
        !mir_machine_parameter_value_offset(
            text->dst, &plan->text_stack_offset) ||
        plan->move_stack_offset != 2 ||
        plan->text_stack_offset != 4)
        return mir_machine_reject(
            "move-text-schedule", "parameters");

    for (item = 0; item < 7; ++item) {
        const struct MirInsn *address =
            &mir.insns[output_addresses[item]];

        if (!mir_machine_constant_equals(
                mir.insns[output_constants[item]].dst,
                output_offsets[item]) ||
            address->src1 != text->dst ||
            address->src2 !=
                mir.insns[output_constants[item]].dst ||
            address->immediate != 1 ||
            address->memory_size != 1 ||
            address->bit_width != 0 ||
            (address->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "move-text-schedule", "text-addresses");
    }
    for (item = 0; item < 6; ++item) {
        const struct MirInsn *member =
            &mir.insns[member_addresses[item]];
        const struct MirInsn *load =
            &mir.insns[member_loads[item]];
        int expected_offset =
            item < 2 ? 0 : (item < 4 ? 1 : 4);

        if (member->src1 != move->dst ||
            member->immediate != expected_offset ||
            member->memory_size != 1 ||
            member->bit_width != 0 ||
            (member->memory_flags & (1 | 8)) != 0 ||
            load->src1 != member->dst ||
            load->memory_size != 1 ||
            load->bit_width != 0 ||
            (load->memory_flags & (1 | 8)) != 0 ||
            type_size(load->type) != 1)
            return mir_machine_reject(
                "move-text-schedule", "move-fields");
    }

    plan->from_offset = (int)mir.insns[8].immediate;
    plan->to_offset = (int)mir.insns[38].immediate;
    plan->promotion_offset = (int)mir.insns[70].immediate;
    plan->file_first = (int)mir.insns[6].immediate;
    plan->rank_first = (int)mir.insns[21].immediate;
    plan->file_mask = (int)mir.insns[13].immediate;
    plan->rank_shift = (int)mir.insns[28].immediate;
    if (plan->from_offset != 0 ||
        plan->to_offset != plan->from_offset + 1 ||
        plan->promotion_offset != plan->to_offset + 3 ||
        plan->file_first < 0 || plan->file_first > 255 ||
        plan->rank_first < 0 || plan->rank_first > 255 ||
        plan->file_mask != 7 || plan->rank_shift != 3 ||
        !mir_machine_constant_equals(
            mir.insns[6].dst, plan->file_first) ||
        !mir_machine_constant_equals(
            mir.insns[21].dst, plan->rank_first) ||
        !mir_machine_constant_equals(
            mir.insns[36].dst, plan->file_first) ||
        !mir_machine_constant_equals(
            mir.insns[51].dst, plan->rank_first) ||
        !mir_machine_constant_equals(
            mir.insns[13].dst, plan->file_mask) ||
        !mir_machine_constant_equals(
            mir.insns[43].dst, plan->file_mask) ||
        !mir_machine_constant_equals(
            mir.insns[28].dst, plan->rank_shift) ||
        !mir_machine_constant_equals(
            mir.insns[58].dst, plan->rank_shift) ||
        !mir_machine_constant_equals(mir.insns[67].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[72].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[91].dst, 0))
        return mir_machine_reject(
            "move-text-schedule", "constants");

    if (mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[14].immediate != '&' ||
        mir.insns[15].src1 != mir.insns[6].dst ||
        mir.insns[15].src2 != mir.insns[14].dst ||
        mir.insns[15].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[5].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[29].src1 != mir.insns[27].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].immediate != TOK_SHR ||
        mir.insns[30].src1 != mir.insns[21].dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[30].immediate != '+' ||
        mir.insns[32].src1 != mir.insns[20].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[44].src1 != mir.insns[42].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[44].immediate != '&' ||
        mir.insns[45].src1 != mir.insns[36].dst ||
        mir.insns[45].src2 != mir.insns[44].dst ||
        mir.insns[45].immediate != '+' ||
        mir.insns[47].src1 != mir.insns[35].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[59].src1 != mir.insns[57].dst ||
        mir.insns[59].src2 != mir.insns[58].dst ||
        mir.insns[59].immediate != TOK_SHR ||
        mir.insns[60].src1 != mir.insns[51].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[60].immediate != '+' ||
        mir.insns[62].src1 != mir.insns[50].dst ||
        mir.insns[62].src2 != mir.insns[61].dst ||
        mir.insns[68].src1 != mir.insns[65].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        mir.insns[74].src1 != mir.insns[73].dst ||
        mir.insns[74].src2 != mir.insns[72].dst ||
        mir.insns[74].immediate != TOK_NE ||
        mir.insns[75].src1 != mir.insns[74].dst ||
        mir.insns[86].src1 != mir.insns[78].dst ||
        mir.insns[86].src2 != mir.insns[85].dst ||
        mir.insns[92].src1 != mir.insns[89].dst ||
        mir.insns[92].src2 != mir.insns[91].dst)
        return mir_machine_reject(
            "move-text-schedule", "data-flow");

    plan->lower_function = find_global(call->name);
    if (plan->lower_function == NULL ||
        plan->lower_function->storage != SC_FUNC ||
        !plan->lower_function->is_defined ||
        plan->lower_function->is_funcptr ||
        !plan->lower_function->has_proto ||
        plan->lower_function->proto_nargs != 1 ||
        plan->lower_function->proto_variadic ||
        type_ptr_depth(plan->lower_function->type) != 0 ||
        (plan->lower_function->type & 15) != TYPE_INT ||
        type_size(plan->lower_function->type) != 2 ||
        type_ptr_depth(plan->lower_function->proto_types[0]) != 0 ||
        (plan->lower_function->proto_types[0] & 15) != TYPE_INT ||
        type_size(plan->lower_function->proto_types[0]) != 2 ||
        !mir_machine_single_call_argument(call, &call_argument) ||
        call_argument != mir.insns[82].dst ||
        mir.insns[82].src1 != mir.insns[81].dst ||
        mir.insns[85].src1 != call->dst)
        return mir_machine_reject(
            "move-text-schedule", "lower-call");
    return 1;
}

static void mir_emit_move_text_schedule(
    MirStream *out, const struct MirMoveTextSchedule *plan)
{
    int done = new_label();
    int move_offset = plan->move_stack_offset + 4;
    int text_offset = plan->text_stack_offset + 4;

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
            move_offset, move_offset + 1,
            text_offset, text_offset + 1);
    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tand %d\n\tadd a,%d\n"
            "\tld (de),a\n\tinc de\n"
            "\tld a,(iy%+d)\n\tsra a\n\tsra a\n\tsra a\n"
            "\tadd a,%d\n\tld (de),a\n\tinc de\n"
            "\tld a,(iy%+d)\n\tand %d\n\tadd a,%d\n"
            "\tld (de),a\n\tinc de\n"
            "\tld a,(iy%+d)\n\tsra a\n\tsra a\n\tsra a\n"
            "\tadd a,%d\n\tld (de),a\n\tinc de\n"
            "\txor a\n\tld (de),a\n"
            "\tld a,(iy%+d)\n\tor a\n\tjp z,L%d\n"
            "\tpush de\n\tld l,a\n\trlca\n\tsbc a,a\n\tld h,a\n"
            "\tpush hl\n",
            plan->from_offset, plan->file_mask, plan->file_first,
            plan->from_offset, plan->rank_first,
            plan->to_offset, plan->file_mask, plan->file_first,
            plan->to_offset, plan->rank_first,
            plan->promotion_offset, done);
    mir_machine_emit_symbol_call(out, plan->lower_function);
    mir_stream_printf(out,
            "\tpop bc\n\tpop de\n\tld a,l\n\tld (de),a\n\tinc de\n"
            "\txor a\n\tld (de),a\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            done);
}

static int mir_match_board_attack_schedule(
    struct MirBoardAttackSchedule *plan)
{
    static const int expected_opcodes[676] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_LOAD, MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_UNARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_LABEL,
        MIR_PHI, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
        MIR_CALL, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP,
        MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_LOAD, MIR_ARG, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_NOP, MIR_CONST,
        MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_LOAD, MIR_BINARY, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_UNARY, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    static const int control_edges[][2] = {
        {11, 89}, {16, 24}, {20, 24}, {23, 26}, {28, 42},
        {38, 42}, {41, 44}, {46, 49}, {53, 61}, {57, 61},
        {60, 63}, {65, 79}, {75, 79}, {78, 81}, {83, 165},
        {88, 165}, {93, 101}, {97, 101}, {100, 103}, {105, 119},
        {115, 119}, {118, 121}, {123, 126}, {130, 138},
        {134, 138}, {137, 140}, {142, 156}, {152, 156},
        {155, 158}, {160, 163}, {177, 348}, {189, 197},
        {193, 197}, {196, 199}, {201, 227}, {211, 216},
        {215, 219}, {223, 227}, {226, 229}, {231, 340},
        {242, 251}, {247, 251}, {250, 253}, {255, 259},
        {258, 288}, {264, 273}, {269, 273}, {272, 275},
        {277, 282}, {281, 285}, {292, 331}, {297, 306},
        {302, 306}, {305, 308}, {310, 320}, {319, 323},
        {327, 331}, {330, 333}, {335, 338}, {347, 169},
        {362, 365}, {380, 383}, {397, 400}, {415, 418},
        {432, 435}, {449, 452}, {467, 470}, {485, 488},
        {502, 673}, {514, 522}, {518, 522}, {521, 524},
        {526, 552}, {536, 541}, {540, 544}, {548, 552},
        {551, 554}, {556, 665}, {567, 576}, {572, 576},
        {575, 578}, {580, 584}, {583, 613}, {589, 598},
        {594, 598}, {597, 600}, {602, 607}, {606, 610},
        {617, 656}, {622, 631}, {627, 631}, {630, 633},
        {635, 645}, {644, 648}, {652, 656}, {655, 658},
        {660, 663}, {672, 492}
    };
    static const int board_addresses[6] = {
        29, 66, 106, 143, 232, 557
    };
    static const int call_instructions[8] = {
        361, 379, 396, 414, 431, 448, 466, 484
    };
    const struct MirInsn *square = &mir.insns[1];
    const struct MirInsn *side = &mir.insns[2];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 676 || mir_cfg_block_count() != 118 ||
        mir.has_vla || mir.local_bytes != 39 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "board-attack-schedule", "opcodes");
    if (!mir_scanner_control_edges_match(
            control_edges,
            (int)(sizeof(control_edges) / sizeof(control_edges[0]))))
        return mir_machine_reject(
            "board-attack-schedule", "control-flow");
    if (square->type != side->type ||
        type_ptr_depth(square->type) != 0 ||
        (square->type & 15) != TYPE_INT ||
        (square->type & TYPE_UNSIGNED) != 0 ||
        type_size(square->type) != 2 ||
        !mir_machine_named_nonvolatile(square) ||
        !mir_machine_named_nonvolatile(side) ||
        !mir_machine_parameter_value_offset(
            square->dst, &plan->square_stack_offset) ||
        !mir_machine_parameter_value_offset(
            side->dst, &plan->side_stack_offset) ||
        plan->square_stack_offset != 2 ||
        plan->side_stack_offset != 4)
        return mir_machine_reject(
            "board-attack-schedule", "parameters");

    plan->board = find_global(mir.insns[board_addresses[0]].name);
    if (plan->board == NULL ||
        (plan->board->storage != SC_GLOBAL &&
         plan->board->storage != SC_EXTERN) ||
        !plan->board->is_array || plan->board->is_vla ||
        plan->board->is_volatile ||
        plan->board->pointee_is_volatile ||
        plan->board->elem_size != 1 ||
        plan->board->array_len != 64)
        return mir_machine_reject(
            "board-attack-schedule", "board");
    for (item = 0;
         item < (int)(sizeof(board_addresses) /
                      sizeof(board_addresses[0]));
         ++item) {
        const struct MirInsn *address =
            &mir.insns[board_addresses[item]];

        if (find_global(address->name) != plan->board ||
            !mir_machine_named_nonvolatile(address) ||
            type_ptr_depth(address->type) != 1 ||
            (address->type & 15) != TYPE_CHAR)
            return mir_machine_reject(
                "board-attack-schedule", "board-roots");
    }

    plan->knight_directions = find_global(mir.insns[179].name);
    plan->king_directions = find_global(mir.insns[504].name);
    if (plan->knight_directions == NULL ||
        plan->king_directions == NULL ||
        plan->knight_directions == plan->king_directions ||
    plan->knight_directions->storage != SC_GLOBAL ||
    plan->king_directions->storage != SC_GLOBAL ||
        !plan->knight_directions->is_array ||
        !plan->king_directions->is_array ||
        plan->knight_directions->is_vla ||
        plan->king_directions->is_vla ||
        plan->knight_directions->is_volatile ||
        plan->king_directions->is_volatile ||
        plan->knight_directions->pointee_is_volatile ||
        plan->king_directions->pointee_is_volatile ||
        plan->knight_directions->elem_size != 2 ||
        plan->king_directions->elem_size != 2 ||
        plan->knight_directions->array_len != 8 ||
        plan->king_directions->array_len != 8 ||
        mir.insns[181].src1 != mir.insns[179].dst ||
        mir.insns[181].immediate != 2 ||
        mir.insns[182].src1 != mir.insns[181].dst ||
        mir.insns[182].memory_size != 2 ||
        mir.insns[506].src1 != mir.insns[504].dst ||
        mir.insns[506].immediate != 2 ||
        mir.insns[507].src1 != mir.insns[506].dst ||
        mir.insns[507].memory_size != 2)
        return mir_machine_reject(
            "board-attack-schedule", "direction-arrays");

    plan->board_size = 64;
    plan->direction_count = 8;
    plan->white_side = (int)mir.insns[9].immediate;
    plan->black_side = (int)mir.insns[279].immediate;
    plan->white_pawn = (int)mir.insns[35].immediate;
    plan->black_pawn = (int)mir.insns[112].immediate;
    plan->knight_piece = (int)mir.insns[325].immediate;
    plan->king_piece = (int)mir.insns[650].immediate;
    plan->upper_first = (int)mir.insns[239].immediate;
    plan->upper_last = (int)mir.insns[244].immediate;
    plan->lower_first = (int)mir.insns[261].immediate;
    plan->lower_last = (int)mir.insns[266].immediate;
    if (!mir_machine_constant_equals(
            mir.insns[9].dst, plan->white_side) ||
        !mir_machine_constant_equals(
            mir.insns[279].dst, plan->black_side) ||
        !mir_machine_constant_equals(
            mir.insns[35].dst, plan->white_pawn) ||
        !mir_machine_constant_equals(
            mir.insns[72].dst, plan->white_pawn) ||
        !mir_machine_constant_equals(
            mir.insns[112].dst, plan->black_pawn) ||
        !mir_machine_constant_equals(
            mir.insns[149].dst, plan->black_pawn) ||
        !mir_machine_constant_equals(
            mir.insns[325].dst, plan->knight_piece) ||
        !mir_machine_constant_equals(
            mir.insns[650].dst, plan->king_piece) ||
        !mir_machine_constant_equals(
            mir.insns[239].dst, plan->upper_first) ||
        !mir_machine_constant_equals(
            mir.insns[244].dst, plan->upper_last) ||
        !mir_machine_constant_equals(
            mir.insns[261].dst, plan->lower_first) ||
        !mir_machine_constant_equals(
            mir.insns[266].dst, plan->lower_last) ||
        !mir_machine_constant_equals(mir.insns[4].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[14].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[31].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[55].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[68].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[95].dst, 56) ||
        !mir_machine_constant_equals(mir.insns[108].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[132].dst, 54) ||
        !mir_machine_constant_equals(mir.insns[145].dst, 9) ||
        !mir_machine_constant_equals(mir.insns[175].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[187].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[191].dst, 64) ||
        !mir_machine_constant_equals(mir.insns[203].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[209].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[221].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[344].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[500].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[512].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[516].dst, 64) ||
        !mir_machine_constant_equals(mir.insns[528].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[534].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[546].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[669].dst, 1) ||
        plan->white_side != 1 ||
        plan->black_side != 65535 ||
        plan->white_pawn < 0 || plan->white_pawn > 255 ||
        plan->black_pawn < 0 || plan->black_pawn > 255 ||
        plan->knight_piece < 0 || plan->knight_piece > 255 ||
        plan->king_piece < 0 || plan->king_piece > 255 ||
        plan->upper_first < 0 || plan->upper_first > 255 ||
        plan->upper_last != plan->upper_first + 25 ||
        plan->lower_first != plan->upper_first + 32 ||
        plan->lower_last != plan->lower_first + 25)
        return mir_machine_reject(
            "board-attack-schedule", "piece-constants");

    plan->slider_function =
        find_global(mir.insns[call_instructions[0]].name);
    if (plan->slider_function == NULL ||
        plan->slider_function->storage != SC_FUNC ||
        !plan->slider_function->is_defined ||
        plan->slider_function->is_funcptr ||
        !plan->slider_function->has_proto ||
        plan->slider_function->proto_nargs != 5 ||
        plan->slider_function->proto_variadic ||
        type_ptr_depth(plan->slider_function->type) != 0 ||
        (plan->slider_function->type & 15) != TYPE_INT ||
        type_size(plan->slider_function->type) != 2)
        return mir_machine_reject(
            "board-attack-schedule", "slider-signature");
    for (item = 0; item < 8; ++item) {
        const struct MirInsn *call =
            &mir.insns[call_instructions[item]];
        int arguments[5];
        long direction;
        long piece_a;
        long piece_b;

        if (find_global(call->name) != plan->slider_function ||
            !mir_scanner_call_arguments(call, 5, arguments) ||
            !mir_machine_same_location(
                mir_definition(arguments[0]), square) ||
            !mir_machine_same_location(
                mir_definition(arguments[1]), side) ||
            !mir_machine_evaluate_constant(
                arguments[2], &direction, 0) ||
            !mir_machine_evaluate_constant(
                arguments[3], &piece_a, 0) ||
            !mir_machine_evaluate_constant(
                arguments[4], &piece_b, 0) ||
            direction < -32768 || direction > 65535 ||
            piece_a < 0 || piece_a > 255 ||
            piece_b < 0 || piece_b > 255 ||
            mir.insns[call_instructions[item] + 1].src1 != call->dst ||
            !mir_machine_constant_equals(
                mir.insns[call_instructions[item] + 2].dst, 1) ||
            mir.insns[call_instructions[item] + 3].src1 !=
                mir.insns[call_instructions[item] + 2].dst)
            return mir_machine_reject(
                "board-attack-schedule", "slider-calls");
        plan->slider_directions[item] =
            (int)((unsigned long)direction & 0xffffUL);
        plan->slider_pieces[item][0] = (int)piece_a;
        plan->slider_pieces[item][1] = (int)piece_b;
    }
    return 1;
}

static void mir_emit_board_attack_piece_loop(
    MirStream *out, const struct MirBoardAttackSchedule *plan,
    struct Sym *directions, int maximum_file_delta,
    int expected_piece, int success_label)
{
    const char *direction_name =
        asm_name_for(sym_asm_name(directions));
    const char *board_name =
        asm_name_for(sym_asm_name(plan->board));
    int loop = new_label();
    int next = new_label();
    int absolute = new_label();
    int lower = new_label();
    int lower_side = new_label();

    mir_stream_puts("\tld b,0\n", out);
    mir_stream_printf(out,
            "L%d:\n"
            "\tld l,b\n\tld h,0\n\tadd hl,hl\n"
            "\tld de,%s\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush iy\n\tpop hl\n\tadd hl,de\n"
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp %d\n\tjp nc,L%d\n"
            "\tand 7\n\tsub c\n\tjp p,L%d\n\tneg\n"
            "L%d:\n\tcp %d\n\tjp nc,L%d\n"
            "\tld de,%s\n\tadd hl,de\n\tld e,(hl)\n"
            "\tld a,e\n\tcp %d\n\tjp c,L%d\n"
            "\tcp %d\n\tjp nc,L%d\n"
            "\tcp %d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp z,L%d\n"
            "\tjp L%d\n"
            "L%d:\n\tld a,e\n\tcp %d\n\tjp c,L%d\n"
            "\tcp %d\n\tjp nc,L%d\n"
            "\tsub %d\n\tcp %d\n\tjp nz,L%d\n"
            "L%d:\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tinc b\n\tld a,b\n\tcp %d\n\tjp c,L%d\n",
            loop, direction_name,
            next, next, plan->board_size, next,
            absolute, absolute, maximum_file_delta + 1, next,
            board_name,
            plan->upper_first, lower,
            plan->upper_last + 1, lower,
            expected_piece, next,
            plan->side_stack_offset + 4,
            plan->white_side & 255, next,
            plan->side_stack_offset + 5,
            (plan->white_side >> 8) & 255, success_label,
            next,
            lower, plan->lower_first, next,
            plan->lower_last + 1, next,
            plan->lower_first - plan->upper_first,
            expected_piece, next,
            lower_side,
            plan->side_stack_offset + 4,
            plan->black_side & 255, next,
            plan->side_stack_offset + 5,
            (plan->black_side >> 8) & 255, success_label,
            next, plan->direction_count, loop);
}

static void mir_emit_board_slider_call(
    MirStream *out, const struct MirBoardAttackSchedule *plan,
    int item, int success_label)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tpush hl\n"
            "\tpush iy\n\tpop hl\n\tpush hl\n",
            plan->slider_pieces[item][1],
            plan->slider_pieces[item][0],
            plan->slider_directions[item],
            plan->side_stack_offset + 4,
            plan->side_stack_offset + 5);
    mir_machine_emit_symbol_call(out, plan->slider_function);
    mir_stream_printf(out,
            "\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n\tpop bc\n"
            "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            success_label);
}

static void mir_emit_board_attack_schedule(
    MirStream *out, const struct MirBoardAttackSchedule *plan)
{
    const char *board_name =
        asm_name_for(sym_asm_name(plan->board));
    int black_pawns = new_label();
    int white_second = new_label();
    int after_pawns = new_label();
    int white_first_address = new_label();
    int white_second_address = new_label();
    int black_first_address = new_label();
    int black_second = new_label();
    int black_second_address = new_label();
    int success = new_label();
    int done = new_label();
    int item;

    if ((plan->board->storage == SC_EXTERN ||
         plan->board->needs_extrn) &&
        mir_extrn_should_emit(plan->board))
        mir_stream_printf(out, "\textrn %s\n", board_name);
    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\tld a,l\n\tand 7\n\tld c,a\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nz,L%d\n",
            plan->square_stack_offset + 4,
            plan->square_stack_offset + 5,
            plan->side_stack_offset + 4,
            plan->side_stack_offset + 5,
            plan->white_side, black_pawns);

    mir_stream_printf(out,
            "\tld a,c\n\tcp 7\n\tjp nc,L%d\n"
            "\tpush iy\n\tpop hl\n\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp 7\n\tjp c,L%d\n"
            "L%d:\n\tld de,7\n\tor a\n\tsbc hl,de\n"
            "\tld de,%s\n\tadd hl,de\n\tld a,(hl)\n"
            "\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tld a,c\n\tor a\n\tjp z,L%d\n"
            "\tpush iy\n\tpop hl\n\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp 9\n\tjp c,L%d\n"
            "L%d:\n\tld de,9\n\tor a\n\tsbc hl,de\n"
            "\tld de,%s\n\tadd hl,de\n\tld a,(hl)\n"
            "\tcp %d\n\tjp z,L%d\n\tjp L%d\n",
            white_second, white_second, white_first_address,
            white_second, white_first_address, board_name,
            plan->white_pawn, success,
            white_second, after_pawns,
            after_pawns, white_second_address,
            after_pawns, white_second_address, board_name,
            plan->white_pawn, success, after_pawns);

    mir_stream_printf(out,
            "L%d:\n\tld a,c\n\tor a\n\tjp z,L%d\n"
            "\tpush iy\n\tpop hl\n\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp 57\n\tjp nc,L%d\n"
            "L%d:\n\tld de,7\n\tadd hl,de\n"
            "\tld de,%s\n\tadd hl,de\n\tld a,(hl)\n"
            "\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tld a,c\n\tcp 7\n\tjp nc,L%d\n"
            "\tpush iy\n\tpop hl\n\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp 55\n\tjp nc,L%d\n"
            "L%d:\n\tld de,9\n\tadd hl,de\n"
            "\tld de,%s\n\tadd hl,de\n\tld a,(hl)\n"
            "\tcp %d\n\tjp z,L%d\n",
            black_pawns, black_second,
            black_first_address, black_second,
            black_second, black_first_address,
            board_name, plan->black_pawn, success,
            black_second, after_pawns,
            black_second_address, after_pawns,
            after_pawns, black_second_address,
            board_name, plan->black_pawn, success);

    mir_stream_printf(out, "L%d:\n", after_pawns);
    mir_emit_board_attack_piece_loop(
        out, plan, plan->knight_directions, 2,
        plan->knight_piece, success);
    for (item = 0; item < 8; ++item)
        mir_emit_board_slider_call(out, plan, item, success);
    mir_stream_puts("\tpush iy\n\tpop hl\n\tld a,l\n\tand 7\n\tld c,a\n", out);
    mir_emit_board_attack_piece_loop(
        out, plan, plan->king_directions, 1,
        plan->king_piece, success);
    mir_stream_puts("\tld hl,0\n", out);
    mir_stream_printf(out,
            "\tjp L%d\nL%d:\n\tld hl,1\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            done, success, done);
}

static int mir_move_member_matches(
    const struct MirInsn *member, const struct MirInsn *parameter,
    int offset)
{
    return member->opcode == MIR_MEMBER_ADDRESS &&
           member->src1 == parameter->dst &&
           member->immediate == offset &&
           member->memory_size == 1 &&
           member->bit_width == 0 &&
           (member->memory_flags & (1 | 8)) == 0;
}

static struct Sym *mir_move_global_at(
    int instruction, int expected_size)
{
    const struct MirInsn *insn = &mir.insns[instruction];
    struct Sym *symbol;
    int type;
    int storage;
    int offset;

    if (!mir_scalar_memory_location(
            insn, &type, &storage, &offset) ||
        storage != SC_GLOBAL || offset != 0 ||
        type_size(type) != expected_size ||
        !mir_machine_named_nonvolatile(insn))
        return NULL;
    symbol = find_global(insn->name);
    if (symbol == NULL || symbol->storage != SC_GLOBAL ||
        symbol->is_array || symbol->is_volatile ||
        type_size(symbol->type) != expected_size)
        return NULL;
    return symbol;
}

static int mir_move_single_argument_function(
    int call_instruction, int argument_instruction,
    int return_size, struct Sym **function_out)
{
    const struct MirInsn *call = &mir.insns[call_instruction];
    struct Sym *function = find_global(call->name);
    int argument;

    if (function == NULL || function->storage != SC_FUNC ||
        !function->is_defined || function->is_funcptr ||
        !function->has_proto || function->proto_nargs != 1 ||
        function->proto_variadic ||
        type_size(function->type) != return_size ||
        !mir_machine_single_call_argument(call, &argument) ||
        argument != mir.insns[argument_instruction].dst)
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_move_apply_schedule(
    struct MirMoveApplySchedule *plan)
{
    static const int expected_opcodes[463] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_UNARY, MIR_BINARY, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_UNARY, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_LABEL, MIR_LABEL, MIR_PHI,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_UNARY, MIR_NOP,
        MIR_STORE
    };
    static const int control_edges[][2] = {
        {21, 31}, {38, 48}, {55, 59}, {58, 76}, {66, 70},
        {69, 72}, {75, 76}, {78, 86}, {93, 97}, {96, 114},
        {104, 108}, {107, 110}, {113, 114}, {116, 124},
        {131, 135}, {134, 152}, {142, 146}, {145, 148},
        {151, 152}, {154, 162}, {169, 173}, {172, 190},
        {180, 184}, {183, 186}, {189, 190}, {192, 200},
        {209, 216}, {215, 222}, {240, 276}, {248, 257},
        {256, 265}, {283, 386}, {290, 308}, {307, 384},
        {315, 333}, {332, 383}, {340, 358}, {357, 382},
        {365, 381}, {395, 425}, {409, 414}, {413, 417},
        {421, 425}, {424, 427}, {429, 458}, {437, 446},
        {445, 454}
    };
    static const int board_addresses[] = {
        201, 226, 269, 292, 295, 300, 317, 320, 325,
        342, 345, 350, 366, 369, 374
    };
    const struct MirInsn *move = &mir.insns[1];
    struct Sym *second_piece_side;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 463 || mir_cfg_block_count() != 67 ||
        mir.has_vla || mir.local_bytes != 34 ||
        mir.aggregate_temp_bytes != 0 || mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "move-apply-schedule", "opcodes");
    if (!mir_scanner_control_edges_match(
            control_edges,
            (int)(sizeof(control_edges) / sizeof(control_edges[0]))))
        return mir_machine_reject(
            "move-apply-schedule", "control-flow");
    if (type_ptr_depth(move->type) != 1 ||
        type_size(move->type) != 2 ||
        mir_machine_pointee_is_volatile(move) ||
        !mir_machine_parameter_value_offset(
            move->dst, &plan->move_stack_offset) ||
        plan->move_stack_offset != 2)
        return mir_machine_reject(
            "move-apply-schedule", "parameter");

    plan->old_en_passant_offset = (int)mir.insns[3].immediate;
    plan->old_castle_rights_offset = (int)mir.insns[8].immediate;
    plan->piece_offset = (int)mir.insns[16].immediate;
    plan->from_offset = (int)mir.insns[50].immediate;
    plan->to_offset = (int)mir.insns[61].immediate;
    plan->promotion_offset = (int)mir.insns[207].immediate;
    plan->flag_offset = (int)mir.insns[235].immediate;
    plan->captured_offset = plan->piece_offset + 1;
    if (plan->from_offset != 0 ||
        plan->to_offset != 1 ||
        plan->piece_offset != 2 ||
        plan->captured_offset != 3 ||
        plan->promotion_offset != 4 ||
        plan->flag_offset != 5 ||
        plan->old_en_passant_offset != 6 ||
        plan->old_castle_rights_offset != 7)
        return mir_machine_reject(
            "move-apply-schedule", "layout");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_MEMBER_ADDRESS &&
            (!mir_move_member_matches(
                 &mir.insns[instruction], move,
                 (int)mir.insns[instruction].immediate) ||
             mir.insns[instruction].immediate < 0 ||
             mir.insns[instruction].immediate > 7))
            return mir_machine_reject(
                "move-apply-schedule", "members");

    plan->en_passant_square = mir_move_global_at(4, 2);
    plan->castle_rights = mir_move_global_at(9, 1);
    plan->side = mir_move_global_at(459, 2);
    if (plan->en_passant_square == NULL ||
        plan->castle_rights == NULL || plan->side == NULL ||
        !mir_machine_same_location(
            &mir.insns[4], &mir.insns[14]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[22]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[30]) ||
        !mir_machine_same_location(
            &mir.insns[459], &mir.insns[462]))
        return mir_machine_reject(
            "move-apply-schedule", "globals");
    plan->board = find_global(mir.insns[201].name);
    if (plan->board == NULL ||
        plan->board->storage != SC_GLOBAL ||
        !plan->board->is_array || plan->board->is_vla ||
        plan->board->is_volatile ||
        plan->board->pointee_is_volatile ||
        plan->board->elem_size != 1 ||
        plan->board->array_len != 64)
        return mir_machine_reject(
            "move-apply-schedule", "board");
    for (item = 0;
         item < (int)(sizeof(board_addresses) /
                      sizeof(board_addresses[0]));
         ++item)
        if (find_global(mir.insns[board_addresses[item]].name) !=
            plan->board)
            return mir_machine_reject(
                "move-apply-schedule", "board-roots");

    if (!mir_move_single_argument_function(
            245, 243, 2, &plan->piece_side_function) ||
        !mir_move_single_argument_function(
            391, 389, 1, &plan->upcase_function) ||
        !mir_move_single_argument_function(
            434, 432, 2, &second_piece_side) ||
        second_piece_side != plan->piece_side_function)
        return mir_machine_reject(
            "move-apply-schedule", "calls");
    plan->empty_square = (int)mir.insns[232].immediate;
    plan->white_side = (int)mir.insns[246].immediate;
    plan->en_passant_flag = (int)mir.insns[237].immediate;
    plan->castle_flag = (int)mir.insns[280].immediate;
    plan->pawn_piece = (int)mir.insns[392].immediate;
    if (plan->empty_square < 0 || plan->empty_square > 255 ||
        plan->white_side != 1 ||
        plan->en_passant_flag != 1 ||
        plan->castle_flag != 2 ||
        plan->pawn_piece < 0 || plan->pawn_piece > 255 ||
        !mir_machine_constant_equals(mir.insns[12].dst, 65535) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 'K') ||
        !mir_machine_constant_equals(mir.insns[35].dst, 'k') ||
        !mir_machine_constant_equals(mir.insns[26].dst, 65532) ||
        !mir_machine_constant_equals(mir.insns[43].dst, 65523) ||
        !mir_machine_constant_equals(mir.insns[81].dst, 65533) ||
        !mir_machine_constant_equals(mir.insns[119].dst, 65534) ||
        !mir_machine_constant_equals(mir.insns[157].dst, 65527) ||
        !mir_machine_constant_equals(mir.insns[195].dst, 65531) ||
        !mir_machine_constant_equals(mir.insns[52].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[63].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[90].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[101].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[128].dst, 56) ||
        !mir_machine_constant_equals(mir.insns[139].dst, 56) ||
        !mir_machine_constant_equals(mir.insns[166].dst, 63) ||
        !mir_machine_constant_equals(mir.insns[177].dst, 63) ||
        !mir_machine_constant_equals(mir.insns[252].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[261].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[287].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[293].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[296].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[312].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[318].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[321].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[337].dst, 62) ||
        !mir_machine_constant_equals(mir.insns[343].dst, 61) ||
        !mir_machine_constant_equals(mir.insns[346].dst, 63) ||
        !mir_machine_constant_equals(mir.insns[362].dst, 58) ||
        !mir_machine_constant_equals(mir.insns[367].dst, 59) ||
        !mir_machine_constant_equals(mir.insns[370].dst, 56) ||
        !mir_machine_constant_equals(mir.insns[232].dst,
                                     plan->empty_square) ||
        !mir_machine_constant_equals(mir.insns[273].dst,
                                     plan->empty_square) ||
        !mir_machine_constant_equals(mir.insns[304].dst,
                                     plan->empty_square) ||
        !mir_machine_constant_equals(mir.insns[329].dst,
                                     plan->empty_square) ||
        !mir_machine_constant_equals(mir.insns[354].dst,
                                     plan->empty_square) ||
        !mir_machine_constant_equals(mir.insns[378].dst,
                                     plan->empty_square) ||
        !mir_machine_constant_equals(mir.insns[237].dst,
                                     plan->en_passant_flag) ||
        !mir_machine_constant_equals(mir.insns[280].dst,
                                     plan->castle_flag) ||
        !mir_machine_constant_equals(mir.insns[392].dst,
                                     plan->pawn_piece) ||
        !mir_machine_constant_equals(mir.insns[419].dst, 16))
        return mir_machine_reject(
            "move-apply-schedule", "constants");
    return 1;
}

static void mir_emit_iy_signed_byte(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(iy%+d)\n\tld a,l\n\trlca\n"
            "\tsbc a,a\n\tld h,a\n",
            offset);
}

static void mir_emit_board_fixed_move(
    MirStream *out, const char *board_name,
    int destination, int source, int empty_square)
{
    mir_stream_printf(out,
            "\tld a,(%s+%d)\n\tld (%s+%d),a\n"
            "\tld a,%d\n\tld (%s+%d),a\n",
            board_name, source, board_name, destination,
            empty_square, board_name, source);
}

static void mir_emit_move_apply_schedule(
    MirStream *out, const struct MirMoveApplySchedule *plan)
{
    const char *board_name =
        asm_name_for(sym_asm_name(plan->board));
    const char *side_name =
        asm_name_for(sym_asm_name(plan->side));
    const char *en_passant_name =
        asm_name_for(sym_asm_name(plan->en_passant_square));
    const char *rights_name =
        asm_name_for(sym_asm_name(plan->castle_rights));
    int after_white_king = new_label();
    int after_black_king = new_label();
    int clear_white_queen = new_label();
    int after_white_queen = new_label();
    int clear_white_king = new_label();
    int after_white_rook = new_label();
    int clear_black_queen = new_label();
    int after_black_queen = new_label();
    int clear_black_king = new_label();
    int after_black_rook = new_label();
    int use_piece = new_label();
    int have_piece = new_label();
    int no_en_passant = new_label();
    int en_passant_black = new_label();
    int have_en_passant_square = new_label();
    int no_castle = new_label();
    int castle_two = new_label();
    int castle_sixty_two = new_label();
    int castle_fifty_eight = new_label();
    int after_castle = new_label();
    int no_double_pawn = new_label();
    int absolute_done = new_label();
    int double_pawn_black = new_label();
    int have_new_en_passant = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\tld hl,(%s)\n\tld (iy%+d),l\n"
            "\tld a,(%s)\n\tld (iy%+d),a\n"
            "\tld hl,65535\n\tld (%s),hl\n",
            plan->move_stack_offset + 4,
            plan->move_stack_offset + 5,
            en_passant_name, plan->old_en_passant_offset,
            rights_name, plan->old_castle_rights_offset,
            en_passant_name);

    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tcp 'K'\n\tjp nz,L%d\n"
            "\tld a,(%s)\n\tand 252\n\tld (%s),a\n"
            "L%d:\n\tld a,(iy%+d)\n\tcp 'k'\n"
            "\tjp nz,L%d\n\tld a,(%s)\n\tand 243\n"
            "\tld (%s),a\n"
            "L%d:\n\tld a,(iy%+d)\n\tor a\n"
            "\tjp z,L%d\n\tld a,(iy%+d)\n\tor a\n"
            "\tjp nz,L%d\n"
            "L%d:\n\tld a,(%s)\n\tand 253\n\tld (%s),a\n"
            "L%d:\n\tld a,(iy%+d)\n\tcp 7\n"
            "\tjp z,L%d\n\tld a,(iy%+d)\n\tcp 7\n"
            "\tjp nz,L%d\n"
            "L%d:\n\tld a,(%s)\n\tand 254\n\tld (%s),a\n"
            "L%d:\n\tld a,(iy%+d)\n\tcp 56\n"
            "\tjp z,L%d\n\tld a,(iy%+d)\n\tcp 56\n"
            "\tjp nz,L%d\n"
            "L%d:\n\tld a,(%s)\n\tand 247\n\tld (%s),a\n"
            "L%d:\n\tld a,(iy%+d)\n\tcp 63\n"
            "\tjp z,L%d\n\tld a,(iy%+d)\n\tcp 63\n"
            "\tjp nz,L%d\n"
            "L%d:\n\tld a,(%s)\n\tand 251\n\tld (%s),a\n"
            "L%d:\n",
            plan->piece_offset, after_white_king,
            rights_name, rights_name,
            after_white_king, plan->piece_offset,
            after_black_king, rights_name, rights_name,
            after_black_king, plan->from_offset,
            clear_white_queen, plan->to_offset,
            after_white_queen, clear_white_queen,
            rights_name, rights_name,
            after_white_queen, plan->from_offset,
            clear_white_king, plan->to_offset,
            after_white_rook, clear_white_king,
            rights_name, rights_name,
            after_white_rook, plan->from_offset,
            clear_black_queen, plan->to_offset,
            after_black_queen, clear_black_queen,
            rights_name, rights_name,
            after_black_queen, plan->from_offset,
            clear_black_king, plan->to_offset,
            after_black_rook, clear_black_king,
            rights_name, rights_name, after_black_rook);

    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_printf(out, "\tld de,%s\n\tadd hl,de\n\tpush hl\n", board_name);
    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tor a\n\tjp z,L%d\n"
            "\tjp L%d\nL%d:\n\tld a,(iy%+d)\n"
            "L%d:\n\tpop hl\n\tld (hl),a\n",
            plan->promotion_offset, use_piece,
            have_piece, use_piece, plan->piece_offset, have_piece);
    mir_emit_iy_signed_byte(out, plan->from_offset);
    mir_stream_printf(out,
            "\tld de,%s\n\tadd hl,de\n\tld (hl),%d\n",
            board_name, plan->empty_square);

    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tand %d\n\tjp z,L%d\n",
            plan->flag_offset, plan->en_passant_flag,
            no_en_passant);
    mir_emit_iy_signed_byte(out, plan->piece_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->piece_side_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nz,L%d\n",
            plan->white_side, en_passant_black);
    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_puts("\tld de,8\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            have_en_passant_square, en_passant_black);
    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_puts("\tld de,8\n\tadd hl,de\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld de,%s\n\tadd hl,de\n"
            "\tld (hl),%d\nL%d:\n",
            have_en_passant_square, board_name,
            plan->empty_square, no_en_passant);

    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tand %d\n\tjp z,L%d\n"
            "\tld a,(iy%+d)\n\tcp 6\n\tjp nz,L%d\n",
            plan->flag_offset, plan->castle_flag,
            no_castle, plan->to_offset, castle_two);
    mir_emit_board_fixed_move(
        out, board_name, 5, 7, plan->empty_square);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            after_castle, castle_two);
    mir_stream_printf(out,
            "\tcp 2\n\tjp nz,L%d\n", castle_sixty_two);
    mir_emit_board_fixed_move(
        out, board_name, 3, 0, plan->empty_square);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            after_castle, castle_sixty_two);
    mir_stream_printf(out,
            "\tcp 62\n\tjp nz,L%d\n", castle_fifty_eight);
    mir_emit_board_fixed_move(
        out, board_name, 61, 63, plan->empty_square);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            after_castle, castle_fifty_eight);
    mir_stream_puts("\tcp 58\n\tjp nz,", out);
    mir_stream_printf(out, "L%d\n", after_castle);
    mir_emit_board_fixed_move(
        out, board_name, 59, 56, plan->empty_square);
    mir_stream_printf(out, "L%d:\nL%d:\n", after_castle, no_castle);

    mir_emit_iy_signed_byte(out, plan->piece_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->upcase_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld a,l\n\tcp %d\n\tjp nz,L%d\n",
            plan->pawn_piece, no_double_pawn);
    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_iy_signed_byte(out, plan->from_offset);
    mir_stream_puts("\tpop de\n\tex de,hl\n\tor a\n\tsbc hl,de\n"
          "\tbit 7,h\n\tjp z,", out);
    mir_stream_printf(out, "L%d\n\txor a\n\tsub l\n\tld l,a\n"
                 "\tld a,0\n\tsbc a,h\n\tld h,a\nL%d:\n"
                 "\tld de,16\n\tor a\n\tsbc hl,de\n"
                 "\tjp nz,L%d\n",
            absolute_done, absolute_done,
            no_double_pawn);
    mir_emit_iy_signed_byte(out, plan->piece_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->piece_side_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nz,L%d\n",
            plan->white_side, double_pawn_black);
    mir_emit_iy_signed_byte(out, plan->from_offset);
    mir_stream_puts("\tld de,8\n\tadd hl,de\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            have_new_en_passant, double_pawn_black);
    mir_emit_iy_signed_byte(out, plan->from_offset);
    mir_stream_puts("\tld de,8\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld (%s),hl\nL%d:\n"
            "\tld hl,(%s)\n\txor a\n\tsub l\n\tld l,a\n"
            "\tld a,0\n\tsbc a,h\n\tld h,a\n\tld (%s),hl\n"
            "\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            have_new_en_passant, en_passant_name,
            no_double_pawn, side_name, side_name);
}

static int mir_match_move_undo_schedule(
    struct MirMoveUndoSchedule *plan)
{
    static const int expected_opcodes[207] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_UNARY, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_STORE, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG,
        MIR_CALL, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_LABEL,
        MIR_LABEL, MIR_PHI, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT,
        MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_LABEL, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_LABEL
    };
    static const int control_edges[][2] = {
        {31, 42}, {41, 52}, {59, 96}, {67, 76}, {75, 84},
        {103, 206}, {110, 128}, {127, 204}, {135, 153},
        {152, 203}, {160, 178}, {177, 202}, {185, 201}
    };
    static const int board_addresses[] = {
        16, 33, 43, 88, 112, 115, 120, 137, 140, 145,
        162, 165, 170, 186, 189, 194
    };
    const struct MirInsn *move = &mir.insns[1];
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 207 || mir_cfg_block_count() != 20 ||
        mir.has_vla || mir.local_bytes != 34 ||
        mir.aggregate_temp_bytes != 0 || mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "move-undo-schedule", "opcodes");
    if (!mir_scanner_control_edges_match(
            control_edges,
            (int)(sizeof(control_edges) / sizeof(control_edges[0]))))
        return mir_machine_reject(
            "move-undo-schedule", "control-flow");
    if (type_ptr_depth(move->type) != 1 ||
        type_size(move->type) != 2 ||
        mir_machine_pointee_is_volatile(move) ||
        !mir_machine_parameter_value_offset(
            move->dst, &plan->move_stack_offset) ||
        plan->move_stack_offset != 2)
        return mir_machine_reject(
            "move-undo-schedule", "parameter");

    plan->from_offset = (int)mir.insns[18].immediate;
    plan->to_offset = (int)mir.insns[35].immediate;
    plan->piece_offset = (int)mir.insns[22].immediate;
    plan->captured_offset = (int)mir.insns[49].immediate;
    plan->flag_offset = (int)mir.insns[26].immediate;
    plan->old_en_passant_offset = (int)mir.insns[7].immediate;
    plan->old_castle_rights_offset = (int)mir.insns[12].immediate;
    if (plan->from_offset != 0 ||
        plan->to_offset != 1 ||
        plan->piece_offset != 2 ||
        plan->captured_offset != 3 ||
        plan->flag_offset != 5 ||
        plan->old_en_passant_offset != 6 ||
        plan->old_castle_rights_offset != 7)
        return mir_machine_reject(
            "move-undo-schedule", "layout");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_MEMBER_ADDRESS &&
            (!mir_move_member_matches(
                 &mir.insns[instruction], move,
                 (int)mir.insns[instruction].immediate) ||
             mir.insns[instruction].immediate < 0 ||
             mir.insns[instruction].immediate > 7))
            return mir_machine_reject(
                "move-undo-schedule", "members");

    plan->side = mir_move_global_at(2, 2);
    plan->en_passant_square = mir_move_global_at(10, 2);
    plan->castle_rights = mir_move_global_at(15, 1);
    if (plan->side == NULL ||
        plan->en_passant_square == NULL ||
        plan->castle_rights == NULL)
        return mir_machine_reject(
            "move-undo-schedule", "globals");
    plan->board = find_global(mir.insns[16].name);
    if (plan->board == NULL ||
        plan->board->storage != SC_GLOBAL ||
        !plan->board->is_array || plan->board->is_vla ||
        plan->board->is_volatile ||
        plan->board->pointee_is_volatile ||
        plan->board->elem_size != 1 ||
        plan->board->array_len != 64)
        return mir_machine_reject(
            "move-undo-schedule", "board");
    for (item = 0;
         item < (int)(sizeof(board_addresses) /
                      sizeof(board_addresses[0]));
         ++item)
        if (find_global(mir.insns[board_addresses[item]].name) !=
            plan->board)
            return mir_machine_reject(
                "move-undo-schedule", "board-roots");
    if (!mir_move_single_argument_function(
            64, 62, 2, &plan->piece_side_function))
        return mir_machine_reject(
            "move-undo-schedule", "piece-side-call");
    plan->empty_square = (int)mir.insns[39].immediate;
    plan->white_side = (int)mir.insns[65].immediate;
    plan->en_passant_flag = (int)mir.insns[28].immediate;
    plan->castle_flag = (int)mir.insns[100].immediate;
    if (plan->empty_square < 0 || plan->empty_square > 255 ||
        plan->white_side != 1 ||
        plan->en_passant_flag != 1 ||
        plan->castle_flag != 2 ||
        !mir_machine_constant_equals(
            mir.insns[39].dst, plan->empty_square) ||
        !mir_machine_constant_equals(
            mir.insns[28].dst, plan->en_passant_flag) ||
        !mir_machine_constant_equals(
            mir.insns[100].dst, plan->castle_flag) ||
        !mir_machine_constant_equals(mir.insns[71].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[80].dst, 8) ||
        !mir_machine_constant_equals(mir.insns[107].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[113].dst, 7) ||
        !mir_machine_constant_equals(mir.insns[116].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[132].dst, 2) ||
        !mir_machine_constant_equals(mir.insns[138].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[141].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[157].dst, 62) ||
        !mir_machine_constant_equals(mir.insns[163].dst, 63) ||
        !mir_machine_constant_equals(mir.insns[166].dst, 61) ||
        !mir_machine_constant_equals(mir.insns[182].dst, 58) ||
        !mir_machine_constant_equals(mir.insns[187].dst, 56) ||
        !mir_machine_constant_equals(mir.insns[190].dst, 59) ||
        !mir_machine_constant_equals(
            mir.insns[124].dst, plan->empty_square) ||
        !mir_machine_constant_equals(
            mir.insns[149].dst, plan->empty_square) ||
        !mir_machine_constant_equals(
            mir.insns[174].dst, plan->empty_square) ||
        !mir_machine_constant_equals(
            mir.insns[198].dst, plan->empty_square))
        return mir_machine_reject(
            "move-undo-schedule", "constants");
    return 1;
}

static void mir_emit_move_undo_schedule(
    MirStream *out, const struct MirMoveUndoSchedule *plan)
{
    const char *board_name =
        asm_name_for(sym_asm_name(plan->board));
    const char *side_name =
        asm_name_for(sym_asm_name(plan->side));
    const char *en_passant_name =
        asm_name_for(sym_asm_name(plan->en_passant_square));
    const char *rights_name =
        asm_name_for(sym_asm_name(plan->castle_rights));
    int captured_square = new_label();
    int after_destination = new_label();
    int no_en_passant = new_label();
    int en_passant_black = new_label();
    int have_en_passant_square = new_label();
    int no_castle = new_label();
    int castle_two = new_label();
    int castle_sixty_two = new_label();
    int castle_fifty_eight = new_label();
    int done = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\tld hl,(%s)\n\txor a\n\tsub l\n\tld l,a\n"
            "\tld a,0\n\tsbc a,h\n\tld h,a\n\tld (%s),hl\n",
            plan->move_stack_offset + 4,
            plan->move_stack_offset + 5,
            side_name, side_name);
    mir_emit_iy_signed_byte(out, plan->old_en_passant_offset);
    mir_stream_printf(out,
            "\tld (%s),hl\n\tld a,(iy%+d)\n\tld (%s),a\n",
            en_passant_name, plan->old_castle_rights_offset,
            rights_name);

    mir_emit_iy_signed_byte(out, plan->from_offset);
    mir_stream_printf(out, "\tld de,%s\n\tadd hl,de\n\tpush hl\n", board_name);
    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tpop hl\n\tld (hl),a\n",
            plan->piece_offset);

    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tand %d\n\tjp z,L%d\n",
            plan->flag_offset, plan->en_passant_flag,
            captured_square);
    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_printf(out,
            "\tld de,%s\n\tadd hl,de\n\tld (hl),%d\n"
            "\tjp L%d\nL%d:\n",
            board_name, plan->empty_square,
            after_destination, captured_square);
    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_printf(out, "\tld de,%s\n\tadd hl,de\n\tpush hl\n", board_name);
    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tpop hl\n\tld (hl),a\n"
            "L%d:\n",
            plan->captured_offset, after_destination);

    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tand %d\n\tjp z,L%d\n",
            plan->flag_offset, plan->en_passant_flag,
            no_en_passant);
    mir_emit_iy_signed_byte(out, plan->piece_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->piece_side_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nz,L%d\n",
            plan->white_side, en_passant_black);
    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_puts("\tld de,8\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            have_en_passant_square, en_passant_black);
    mir_emit_iy_signed_byte(out, plan->to_offset);
    mir_stream_puts("\tld de,8\n\tadd hl,de\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld de,%s\n\tadd hl,de\n\tpush hl\n"
            "\tld a,(iy%+d)\n\tpop hl\n\tld (hl),a\n"
            "L%d:\n",
            have_en_passant_square, board_name,
            plan->captured_offset, no_en_passant);

    mir_stream_printf(out,
            "\tld a,(iy%+d)\n\tand %d\n\tjp z,L%d\n"
            "\tld a,(iy%+d)\n\tcp 6\n\tjp nz,L%d\n",
            plan->flag_offset, plan->castle_flag,
            no_castle, plan->to_offset, castle_two);
    mir_emit_board_fixed_move(
        out, board_name, 7, 5, plan->empty_square);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, castle_two);
    mir_stream_puts("\tcp 2\n\tjp nz,", out);
    mir_stream_printf(out, "L%d\n", castle_sixty_two);
    mir_emit_board_fixed_move(
        out, board_name, 0, 3, plan->empty_square);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, castle_sixty_two);
    mir_stream_puts("\tcp 62\n\tjp nz,", out);
    mir_stream_printf(out, "L%d\n", castle_fifty_eight);
    mir_emit_board_fixed_move(
        out, board_name, 63, 61, plan->empty_square);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", done, castle_fifty_eight);
    mir_stream_printf(out, "\tcp 58\n\tjp nz,L%d\n", done);
    mir_emit_board_fixed_move(
        out, board_name, 56, 59, plan->empty_square);
    mir_stream_printf(out,
            "L%d:\nL%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            done, no_castle);
}

static int mir_match_move_parse_schedule(
    struct MirMoveParseSchedule *plan)
{
    static const int expected_opcodes[192] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_ARG,
        MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_ARG, MIR_LOAD,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_ARG, MIR_CALL, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_UNARY, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_UNARY, MIR_STORE_INDIRECT, MIR_NOP,
        MIR_MEMBER_ADDRESS, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_PHI,
        MIR_STORE_INDIRECT, MIR_CONST, MIR_RETURN
    };
    static const int control_edges[][2] = {
        {11, 15}, {14, 31}, {21, 25}, {24, 27},
        {30, 31}, {33, 40}, {39, 3}, {47, 50},
        {88, 92}, {91, 106}, {96, 100}, {99, 102},
        {105, 106}, {108, 111}, {122, 126}, {125, 144},
        {134, 138}, {137, 140}, {143, 144}, {146, 161},
        {150, 154}, {153, 157}
    };
    const struct MirInsn *text = &mir.insns[1];
    const struct MirInsn *output = &mir.insns[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 192 || mir_cfg_block_count() != 32 ||
        mir.has_vla || mir.local_bytes != 5 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "move-parse-schedule", "opcodes");
    if (!mir_scanner_control_edges_match(
            control_edges,
            (int)(sizeof(control_edges) / sizeof(control_edges[0]))))
        return mir_machine_reject(
            "move-parse-schedule", "control-flow");
    if (type_ptr_depth(text->type) != 1 ||
        (text->type & 15) != TYPE_CHAR ||
        type_ptr_depth(output->type) != 1 ||
        type_size(text->type) != 2 ||
        type_size(output->type) != 2 ||
        mir_machine_pointee_is_volatile(text) ||
        mir_machine_pointee_is_volatile(output) ||
        !mir_machine_parameter_value_offset(
            text->dst, &plan->text_stack_offset) ||
        !mir_machine_parameter_value_offset(
            output->dst, &plan->output_stack_offset) ||
        plan->text_stack_offset != 2 ||
        plan->output_stack_offset != 4)
        return mir_machine_reject(
            "move-parse-schedule", "parameters");

    plan->length_function = find_global(mir.insns[43].name);
    plan->lower_function = find_global(mir.insns[57].name);
    plan->square_function = find_global(mir.insns[65].name);
    if (plan->length_function == NULL ||
        plan->lower_function == NULL ||
        plan->square_function == NULL ||
        plan->length_function->storage != SC_FUNC ||
        !plan->length_function->has_proto ||
        !plan->lower_function->is_defined ||
        !plan->square_function->is_defined ||
        plan->length_function->proto_nargs != 1 ||
        plan->lower_function->proto_nargs != 1 ||
        plan->square_function->proto_nargs != 2 ||
        plan->length_function->proto_variadic ||
        plan->lower_function->proto_variadic ||
        plan->square_function->proto_variadic ||
        find_global(mir.insns[74].name) != plan->lower_function ||
        find_global(mir.insns[82].name) != plan->square_function)
        return mir_machine_reject(
            "move-parse-schedule", "calls");
    plan->board = find_global(mir.insns[174].name);
    plan->side = find_global(mir.insns[147].name);
    if (plan->board == NULL || plan->side == NULL ||
        plan->board->storage != SC_GLOBAL ||
        plan->side->storage != SC_GLOBAL ||
        !plan->board->is_array ||
        plan->board->is_volatile ||
        plan->board->pointee_is_volatile ||
        plan->board->elem_size != 1 ||
        plan->board->array_len != 64 ||
        plan->side->is_array || plan->side->is_volatile ||
        find_global(mir.insns[181].name) != plan->board)
        return mir_machine_reject(
            "move-parse-schedule", "globals");

    plan->from_offset = (int)mir.insns[163].immediate;
    plan->to_offset = (int)mir.insns[168].immediate;
    plan->piece_offset = (int)mir.insns[173].immediate;
    plan->captured_offset = (int)mir.insns[180].immediate;
    plan->promotion_offset = (int)mir.insns[187].immediate;
    if (plan->from_offset != 0 || plan->to_offset != 1 ||
        plan->piece_offset != 2 || plan->captured_offset != 3 ||
        plan->promotion_offset != 4 ||
        !mir_move_member_matches(&mir.insns[163], output, 0) ||
        !mir_move_member_matches(&mir.insns[168], output, 1) ||
        !mir_move_member_matches(&mir.insns[173], output, 2) ||
        !mir_move_member_matches(&mir.insns[180], output, 3) ||
        !mir_move_member_matches(&mir.insns[187], output, 4))
        return mir_machine_reject(
            "move-parse-schedule", "layout");
    plan->white_side = (int)mir.insns[148].immediate;
    plan->lower_promotion = (int)mir.insns[119].immediate;
    plan->upper_promotion = (int)mir.insns[131].immediate;
    if (plan->white_side != 1 ||
        plan->lower_promotion < 0 ||
        plan->lower_promotion > 255 ||
        plan->upper_promotion !=
            plan->lower_promotion - ('a' - 'A') ||
        !mir_machine_constant_equals(mir.insns[8].dst, ' ') ||
        !mir_machine_constant_equals(mir.insns[18].dst, '\t') ||
        !mir_machine_constant_equals(mir.insns[44].dst, 4) ||
        !mir_machine_constant_equals(
            mir.insns[119].dst, plan->lower_promotion) ||
        !mir_machine_constant_equals(
            mir.insns[131].dst, plan->upper_promotion) ||
        !mir_machine_constant_equals(
            mir.insns[148].dst, plan->white_side))
        return mir_machine_reject(
            "move-parse-schedule", "constants");
    return 1;
}

static void mir_emit_move_parse_schedule(
    MirStream *out, const struct MirMoveParseSchedule *plan)
{
    const char *board_name =
        asm_name_for(sym_asm_name(plan->board));
    const char *side_name =
        asm_name_for(sym_asm_name(plan->side));
    int trim = new_label();
    int trim_advance = new_label();
    int trimmed = new_label();
    int length_ok = new_label();
    int invalid = new_label();
    int no_promotion = new_label();
    int promotion_present = new_label();
    int promotion_black = new_label();
    int promotion_ready = new_label();
    int done = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-5\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "L%d:\n\tld a,(iy+0)\n\tcp ' '\n\tjp z,L%d\n"
            "\tcp 9\n\tjp nz,L%d\n"
            "L%d:\n\tinc iy\n\tjp L%d\n"
            "L%d:\n\tpush iy\n\tpop hl\n\tpush hl\n",
            plan->text_stack_offset + 4,
            plan->text_stack_offset + 5,
            trim, trim_advance, trimmed,
            trim_advance, trim, trimmed);
    mir_machine_emit_symbol_call(out, plan->length_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp 4\n\tjp c,L%d\n"
            "L%d:\n",
            length_ok, invalid, length_ok);

    mir_stream_puts("\tld l,(iy+0)\n\tld a,l\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->lower_function);
    mir_stream_puts("\tpop bc\n\tld (ix-5),l\n"
          "\tld l,(iy+1)\n\tld a,l\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n\tpush hl\n"
          "\tld l,(ix-5)\n\tld a,l\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->square_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld (ix-5),l\n\tld (ix-4),h\n", out);

    mir_stream_puts("\tld l,(iy+2)\n\tld a,l\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->lower_function);
    mir_stream_puts("\tpop bc\n\tld (ix-3),l\n"
          "\tld l,(iy+3)\n\tld a,l\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n\tpush hl\n"
          "\tld l,(ix-3)\n\tld a,l\n\trlca\n\tsbc a,a\n"
          "\tld h,a\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->square_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld (ix-3),l\n\tld (ix-2),h\n"
          "\tbit 7,(ix-4)\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tbit 7,(ix-2)\n\tjp nz,L%d\n",
            invalid, invalid);

    mir_stream_printf(out,
            "\txor a\n\tld (ix-1),a\n"
            "\tld a,(iy+4)\n\tcp %d\n\tjp z,L%d\n"
            "\tcp %d\n\tjp nz,L%d\n"
            "L%d:\n\tld hl,(%s)\n\tld de,%d\n"
            "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n"
            "\tld a,%d\n\tjp L%d\n"
            "L%d:\n\tld a,%d\n"
            "L%d:\n\tld (ix-1),a\n"
            "L%d:\n",
            plan->lower_promotion, promotion_present,
            plan->upper_promotion, no_promotion,
            promotion_present, side_name, plan->white_side,
            promotion_black, plan->upper_promotion,
            promotion_ready, promotion_black,
            plan->lower_promotion, promotion_ready,
            no_promotion);

    mir_stream_printf(out,
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld a,(ix-5)\n\tld (de),a\n\tinc de\n"
            "\tld a,(ix-3)\n\tld (de),a\n\tinc de\n",
            plan->output_stack_offset + 4,
            plan->output_stack_offset + 5);
    mir_stream_puts("\tpush de\n\tld l,(ix-5)\n\tld h,(ix-4)\n", out);
    mir_stream_printf(out,
            "\tld de,%s\n\tadd hl,de\n\tld a,(hl)\n"
            "\tpop de\n\tld (de),a\n\tinc de\n"
            "\tpush de\n\tld l,(ix-3)\n\tld h,(ix-2)\n"
            "\tld de,%s\n\tadd hl,de\n\tld a,(hl)\n"
            "\tpop de\n\tld (de),a\n\tinc de\n"
            "\tld a,(ix-1)\n\tld (de),a\n"
            "\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            board_name, board_name, done,
            invalid, done);
}

static int mir_machine_constant_value(
    int value, long *constant_out, int depth)
{
    const struct MirInsn *definition;
    long left;
    long right;

    if (depth > 16)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_CONST) {
        *constant_out = definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0)
        return mir_machine_constant_value(
            definition->src1, constant_out, depth + 1);
    if (definition->opcode != MIR_BINARY ||
        !mir_machine_constant_value(
            definition->src1, &left, depth + 1) ||
        !mir_machine_constant_value(
            definition->src2, &right, depth + 1))
        return 0;
    return mir_fold_constant_binary(
        (int)definition->immediate, left, right,
        definition->type, constant_out);
}

static int mir_machine_state_member_address(
    int value, struct MirStateMember *member_out)
{
    const struct MirInsn *member = mir_definition(value);
    const struct MirInsn *root_load;
    struct Sym *root;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (member == NULL || member->opcode != MIR_MEMBER_ADDRESS ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0)
        return 0;
    root_load = mir_definition(member->src1);
    if (root_load == NULL || root_load->opcode != MIR_LOAD ||
        type_ptr_depth(root_load->type) == 0 ||
        type_size(root_load->type) != 2 ||
        (root_load->memory_flags & (1 | 8)) != 0 ||
        !mir_scalar_memory_location(
            root_load, &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL)
        return 0;
    root = find_global(root_load->name);
    if (root == NULL || !root->is_defined || root->is_volatile)
        return 0;
    member_out->root = root;
    member_out->root_offset = memory_offset;
    member_out->member_offset = (int)member->immediate;
    return 1;
}

static int mir_machine_same_state_member(
    const struct MirStateMember *left,
    const struct MirStateMember *right)
{
    return left->root == right->root &&
           left->root_offset == right->root_offset &&
           left->member_offset == right->member_offset;
}

static int mir_machine_only_root_loads(
    struct Sym *root, int root_offset)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int memory_type;
        int memory_storage;
        int memory_offset;

        if (insn->opcode != MIR_LOAD)
            continue;
        if (!mir_scalar_memory_location(
                insn, &memory_type, &memory_storage,
                &memory_offset) ||
            memory_storage != SC_GLOBAL ||
            memory_offset != root_offset ||
            find_global(insn->name) != root)
            return 0;
    }
    return 1;
}

static int mir_match_bounded_string_byte(
    int instruction, int pointer_value, int offset, int *expected_out)
{
    const struct MirInsn *offset_constant = &mir.insns[instruction];
    const struct MirInsn *address = &mir.insns[instruction + 1];
    const struct MirInsn *load = &mir.insns[instruction + 2];
    const struct MirInsn *expected = &mir.insns[instruction + 3];
    const struct MirInsn *conversion = &mir.insns[instruction + 4];
    const struct MirInsn *comparison = &mir.insns[instruction + 5];
    const struct MirInsn *branch = &mir.insns[instruction + 6];

    if (!mir_machine_constant_equals(
            offset_constant->dst, offset) ||
        type_ptr_depth(offset_constant->type) != 0 ||
        type_size(offset_constant->type) != 2 ||
        (offset_constant->type & TYPE_UNSIGNED) != 0 ||
        address->src1 != pointer_value ||
        address->src2 != offset_constant->dst ||
        address->immediate != 1 ||
        address->memory_size != 1 ||
        address->bit_width != 0 ||
        (address->memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(address->type) != 1 ||
        (address->type & 15) != TYPE_CHAR ||
        type_size(address->type) != 2 ||
        load->src1 != address->dst ||
        load->memory_size != 1 ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(load->type) != 0 ||
        (load->type & 15) != TYPE_CHAR ||
        type_size(load->type) != 1 ||
        (load->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(expected->type) != 0 ||
        (expected->type & 15) != TYPE_INT ||
        type_size(expected->type) != 2 ||
        (expected->type & TYPE_UNSIGNED) != 0 ||
        expected->immediate < 0 || expected->immediate > 127 ||
        conversion->src1 != load->dst ||
        conversion->immediate != 0 ||
        type_ptr_depth(conversion->type) != 0 ||
        (conversion->type & 15) != TYPE_INT ||
        type_size(conversion->type) != 2 ||
        (conversion->type & TYPE_UNSIGNED) != 0 ||
        comparison->src1 != conversion->dst ||
        comparison->src2 != expected->dst ||
        comparison->immediate != TOK_EQ ||
        type_ptr_depth(comparison->type) != 0 ||
        (comparison->type & 15) != TYPE_INT ||
        type_size(comparison->type) != 2 ||
        (comparison->type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(comparison->secondary_offset) != 0 ||
        (comparison->secondary_offset & 15) != TYPE_INT ||
        type_size(comparison->secondary_offset) != 2 ||
        (comparison->secondary_offset & TYPE_UNSIGNED) != 0 ||
        branch->src1 != comparison->dst)
        return 0;
    *expected_out = (int)expected->immediate;
    return 1;
}

static int mir_match_bounded_string_boolean_join(
    int success_label, int one, int jump, int failure_label,
    int zero, int join_label, int phi)
{
    return mir_machine_constant_equals(mir.insns[one].dst, 1) &&
        mir.insns[jump].label == mir.insns[join_label].label &&
        mir_machine_constant_equals(mir.insns[zero].dst, 0) &&
        mir.insns[phi].src1 == mir.insns[one].dst &&
        mir.insns[phi].src2 == mir.insns[zero].dst &&
        mir.insns[phi].phi_pred1 == mir.insns[success_label].label &&
        mir.insns[phi].phi_pred2 == mir.insns[failure_label].label;
}

static int mir_match_bounded_string_match_schedule(
    struct MirBoundedStringMatchSchedule *plan)
{
    static const int expected_opcodes[74] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_RETURN
    };
    static const int byte_instructions[5] = { 3, 11, 27, 43, 59 };
    int instruction;
    int byte;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 74 || mir_cfg_block_count() != 13 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2 ||
        (mir.return_type & TYPE_UNSIGNED) != 0)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "bounded-string-match-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->pointer_stack_offset) ||
        plan->pointer_stack_offset > 32767 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        (mir.insns[1].type & 15) != TYPE_CHAR ||
        type_size(mir.insns[1].type) != 2 ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        mir.insns[2].object != mir.insns[1].object ||
        mir.insns[2].object < 0)
        return mir_machine_reject(
            "bounded-string-match-schedule", "parameter");

    for (byte = 0; byte < 5; ++byte) {
        if (!mir_match_bounded_string_byte(
                byte_instructions[byte], mir.insns[1].dst,
                byte, &plan->expected[byte]))
            return mir_machine_reject(
                "bounded-string-match-schedule", "byte");
        if ((byte < 4 &&
             (plan->expected[byte] <= 0 ||
              plan->expected[byte] > 127)) ||
            (byte == 4 && plan->expected[byte] != 0))
            return mir_machine_reject(
                "bounded-string-match-schedule", "terminator");
    }

    if (mir.insns[9].label != mir.insns[21].label ||
        mir.insns[17].label != mir.insns[21].label ||
        mir.insns[10].object != mir.insns[1].object ||
        !mir_match_bounded_string_boolean_join(
            18, 19, 20, 21, 22, 23, 24) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[37].label ||
        mir.insns[26].object != mir.insns[1].object ||
        mir.insns[33].label != mir.insns[37].label ||
        !mir_match_bounded_string_boolean_join(
            34, 35, 36, 37, 38, 39, 40) ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].label != mir.insns[53].label ||
        mir.insns[42].object != mir.insns[1].object ||
        mir.insns[49].label != mir.insns[53].label ||
        !mir_match_bounded_string_boolean_join(
            50, 51, 52, 53, 54, 55, 56) ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        mir.insns[57].label != mir.insns[69].label ||
        mir.insns[58].object != mir.insns[1].object ||
        mir.insns[65].label != mir.insns[69].label ||
        !mir_match_bounded_string_boolean_join(
            66, 67, 68, 69, 70, 71, 72) ||
        mir.insns[73].src1 != mir.insns[72].dst)
        return mir_machine_reject(
            "bounded-string-match-schedule", "short-circuit");
    return 1;
}

static int mir_decimal_signed_type(int type, int base_type, int width)
{
    return type_ptr_depth(type) == 0 &&
        (type & 15) == base_type &&
        type_size(type) == width &&
        (type & TYPE_UNSIGNED) == 0;
}

static int mir_decimal_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        (type & 15) == TYPE_CHAR &&
        type_size(type) == 2 &&
        (type & TYPE_UNSIGNED) == 0;
}

static int mir_match_decimal_pointer_access(
    const struct MirInsn *insn, int opcode,
    const struct MirInsn *parameter)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    return insn->opcode == opcode &&
        insn->bit_width == 0 &&
        (insn->memory_flags & (1 | 8)) == 0 &&
        mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) &&
        memory_storage == SC_PARAM &&
        mir_decimal_pointer_type(memory_type) &&
        mir_machine_named_nonvolatile(insn) &&
        mir_machine_same_location(insn, parameter);
}

static int mir_match_decimal_local_access(
    const struct MirInsn *insn, int opcode, int base_type, int width,
    int object, const struct MirInsn *same_location)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    return insn->opcode == opcode &&
        insn->object == object &&
        insn->bit_width == 0 &&
        (insn->memory_flags & (1 | 8)) == 0 &&
        mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) &&
        memory_storage == SC_LOCAL &&
        mir_decimal_signed_type(memory_type, base_type, width) &&
        memory_offset >= -mir.local_bytes &&
        memory_offset + width <= 0 &&
        mir_machine_named_nonvolatile(insn) &&
        (same_location == NULL ||
         mir_machine_same_location(insn, same_location));
}

static int mir_match_decimal_char_load(
    int instruction, int pointer_value)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->src1 == pointer_value &&
        load->memory_size == 1 &&
        load->bit_width == 0 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        mir_decimal_signed_type(load->type, TYPE_CHAR, 1);
}

static int mir_match_decimal_conversion(
    int instruction, int source_value, int base_type, int width)
{
    const struct MirInsn *conversion = &mir.insns[instruction];

    return conversion->src1 == source_value &&
        conversion->immediate == 0 &&
        mir_decimal_signed_type(conversion->type, base_type, width);
}

static int mir_match_decimal_pointer_add(
    int instruction, int left_value, int right_value, int operand_type)
{
    const struct MirInsn *binary = &mir.insns[instruction];

    return binary->src1 == left_value &&
        binary->src2 == right_value &&
        binary->immediate == '+' &&
        mir_decimal_pointer_type(binary->type) &&
        binary->secondary_offset == operand_type;
}

static int mir_match_decimal_binary(
    int instruction, int left_value, int right_value,
    int operation, int base_type, int width, int operand_type)
{
    const struct MirInsn *binary = &mir.insns[instruction];

    return binary->src1 == left_value &&
        binary->src2 == right_value &&
        binary->immediate == operation &&
        mir_decimal_signed_type(binary->type, base_type, width) &&
        binary->secondary_offset == operand_type;
}

static int mir_match_decimal_long_scan_schedule(
    struct MirDecimalLongScanSchedule *plan)
{
    static const int expected_opcodes[79] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_LABEL,
        MIR_LOAD, MIR_PHI, MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_UNARY, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    int accumulator_object;
    int negative_object;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 79 || mir_cfg_block_count() != 12 ||
        mir.has_vla || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_decimal_signed_type(mir.return_type, TYPE_LONG, 4))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "decimal-long-scan-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            parameter->dst, &plan->pointer_stack_offset) ||
        plan->pointer_stack_offset > 32767 ||
        !mir_decimal_pointer_type(parameter->type) ||
        !mir_machine_named_nonvolatile(parameter) ||
        mir_machine_pointee_is_volatile(parameter))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "parameter");

    accumulator_object = mir.insns[4].object;
    negative_object = mir.insns[7].object;
    if (accumulator_object < 0 || negative_object < 0 ||
        accumulator_object == negative_object ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_decimal_signed_type(mir.insns[2].type, TYPE_LONG, 4) ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        !mir_match_decimal_local_access(
            &mir.insns[4], MIR_STORE, TYPE_LONG, 4,
            accumulator_object, NULL) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_decimal_signed_type(mir.insns[5].type, TYPE_INT, 2) ||
        mir.insns[7].src1 != mir.insns[5].dst ||
        !mir_match_decimal_local_access(
            &mir.insns[7], MIR_STORE, TYPE_INT, 2,
            negative_object, NULL))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "initializers");

    if (!mir_machine_constant_equals(mir.insns[8].dst, '-') ||
        !mir_decimal_signed_type(mir.insns[8].type, TYPE_INT, 2))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "sign-constant");
    if (!mir_match_decimal_pointer_access(
            &mir.insns[9], MIR_LOAD, parameter))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "sign-pointer");
    if (!mir_match_decimal_char_load(10, mir.insns[9].dst) ||
        !mir_match_decimal_conversion(
            11, mir.insns[10].dst, TYPE_INT, 2))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "sign-load");
    if (!mir_match_decimal_binary(
            12, mir.insns[8].dst, mir.insns[11].dst,
            TOK_EQ, TYPE_INT, 2, mir.insns[8].type))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "sign-compare");
    if (mir.insns[13].src1 != mir.insns[12].dst ||
        mir.insns[13].label != mir.insns[22].label)
        return mir_machine_reject(
            "decimal-long-scan-schedule", "sign-test");
    if (!mir_machine_constant_equals(mir.insns[14].dst, 1) ||
        !mir_decimal_signed_type(mir.insns[14].type, TYPE_INT, 2) ||
        mir.insns[16].src1 != mir.insns[14].dst ||
        !mir_match_decimal_local_access(
            &mir.insns[16], MIR_STORE, TYPE_INT, 2,
            negative_object, &mir.insns[7]))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "sign-state");
    if (!mir_match_decimal_pointer_access(
            &mir.insns[17], MIR_LOAD, parameter) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 1) ||
        !mir_decimal_pointer_type(mir.insns[18].type) ||
        !mir_match_decimal_pointer_add(
            19, mir.insns[17].dst, mir.insns[18].dst,
            mir.insns[17].type) ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !mir_match_decimal_pointer_access(
            &mir.insns[20], MIR_STORE, parameter))
        return mir_machine_reject(
            "decimal-long-scan-schedule", "sign-advance");

    if (!mir_match_decimal_pointer_access(
            &mir.insns[24], MIR_LOAD, parameter) ||
        mir.insns[25].src1 != mir.insns[2].dst ||
        mir.insns[25].src2 != mir.insns[56].dst ||
        mir.insns[25].object != accumulator_object ||
        !mir_decimal_signed_type(mir.insns[25].type, TYPE_LONG, 4) ||
        mir.insns[25].phi_pred1 != mir.insns[22].label ||
        mir.insns[25].phi_pred2 != mir.insns[64].label ||
        mir.insns[26].object != negative_object ||
        !mir_match_decimal_pointer_access(
            &mir.insns[27], MIR_LOAD, parameter) ||
        !mir_match_decimal_char_load(28, mir.insns[27].dst) ||
        !mir_machine_constant_equals(mir.insns[29].dst, '0') ||
        !mir_decimal_signed_type(mir.insns[29].type, TYPE_INT, 2) ||
        !mir_match_decimal_conversion(
            30, mir.insns[28].dst, TYPE_INT, 2) ||
        !mir_match_decimal_binary(
            31, mir.insns[30].dst, mir.insns[29].dst,
            TOK_GE, TYPE_INT, 2, mir.insns[29].type) ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[32].label != mir.insns[42].label ||
        !mir_match_decimal_pointer_access(
            &mir.insns[33], MIR_LOAD, parameter) ||
        !mir_match_decimal_char_load(34, mir.insns[33].dst) ||
        !mir_machine_constant_equals(mir.insns[35].dst, '9') ||
        !mir_decimal_signed_type(mir.insns[35].type, TYPE_INT, 2) ||
        !mir_match_decimal_conversion(
            36, mir.insns[34].dst, TYPE_INT, 2) ||
        !mir_match_decimal_binary(
            37, mir.insns[36].dst, mir.insns[35].dst,
            TOK_LE, TYPE_INT, 2, mir.insns[35].type) ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].label != mir.insns[42].label ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        mir.insns[41].label != mir.insns[44].label ||
        !mir_machine_constant_equals(mir.insns[43].dst, 0) ||
        mir.insns[45].src1 != mir.insns[40].dst ||
        mir.insns[45].src2 != mir.insns[43].dst ||
        mir.insns[45].phi_pred1 != mir.insns[39].label ||
        mir.insns[45].phi_pred2 != mir.insns[42].label ||
        mir.insns[46].src1 != mir.insns[45].dst ||
        mir.insns[46].label != mir.insns[66].label)
        return mir_machine_reject(
            "decimal-long-scan-schedule", "digit-test");

    if (mir.insns[47].object != accumulator_object ||
        !mir_machine_constant_equals(mir.insns[48].dst, 10) ||
        !mir_decimal_signed_type(mir.insns[48].type, TYPE_LONG, 4) ||
        !mir_match_decimal_binary(
            49, mir.insns[25].dst, mir.insns[48].dst,
            '*', TYPE_LONG, 4, mir.insns[48].type) ||
        !mir_match_decimal_pointer_access(
            &mir.insns[50], MIR_LOAD, parameter) ||
        !mir_match_decimal_char_load(51, mir.insns[50].dst) ||
        !mir_machine_constant_equals(mir.insns[52].dst, '0') ||
        !mir_decimal_signed_type(mir.insns[52].type, TYPE_INT, 2) ||
        !mir_match_decimal_conversion(
            53, mir.insns[51].dst, TYPE_INT, 2) ||
        !mir_match_decimal_binary(
            54, mir.insns[53].dst, mir.insns[52].dst,
            '-', TYPE_INT, 2, mir.insns[52].type) ||
        !mir_match_decimal_conversion(
            55, mir.insns[54].dst, TYPE_LONG, 4) ||
        !mir_match_decimal_binary(
            56, mir.insns[49].dst, mir.insns[55].dst,
            '+', TYPE_LONG, 4, mir.insns[48].type) ||
        mir.insns[58].src1 != mir.insns[56].dst ||
        !mir_match_decimal_local_access(
            &mir.insns[58], MIR_STORE, TYPE_LONG, 4,
            accumulator_object, &mir.insns[4]) ||
        !mir_match_decimal_pointer_access(
            &mir.insns[59], MIR_LOAD, parameter) ||
        !mir_machine_constant_equals(mir.insns[60].dst, 1) ||
        !mir_decimal_pointer_type(mir.insns[60].type) ||
        !mir_match_decimal_pointer_add(
            61, mir.insns[59].dst, mir.insns[60].dst,
            mir.insns[59].type) ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        !mir_match_decimal_pointer_access(
            &mir.insns[62], MIR_STORE, parameter) ||
        mir.insns[65].label != mir.insns[23].label)
        return mir_machine_reject(
            "decimal-long-scan-schedule", "digit-body");

    if (!mir_match_decimal_local_access(
            &mir.insns[67], MIR_LOAD, TYPE_INT, 2,
            negative_object, &mir.insns[7]) ||
        mir.insns[68].src1 != mir.insns[67].dst ||
        mir.insns[68].label != mir.insns[73].label ||
        mir.insns[69].object != accumulator_object ||
        mir.insns[70].src1 != mir.insns[25].dst ||
        mir.insns[70].immediate != '-' ||
        !mir_decimal_signed_type(mir.insns[70].type, TYPE_LONG, 4) ||
        mir.insns[72].label != mir.insns[76].label ||
        mir.insns[74].object != accumulator_object ||
        mir.insns[77].src1 != mir.insns[70].dst ||
        mir.insns[77].src2 != mir.insns[25].dst ||
        mir.insns[77].phi_pred1 != mir.insns[71].label ||
        mir.insns[77].phi_pred2 != mir.insns[75].label ||
        !mir_decimal_signed_type(mir.insns[77].type, TYPE_LONG, 4) ||
        mir.insns[78].src1 != mir.insns[77].dst)
        return mir_machine_reject(
            "decimal-long-scan-schedule", "return");
    return 1;
}

static int mir_match_scanner_global_scalar(
    const struct MirInsn *insn, int base_type, int pointer_depth,
    int width, struct MirGlobalScalar *location_out)
{
    int memory_type;
    int memory_storage;
    int memory_offset;
    struct Sym *root;

    if (!mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != pointer_depth ||
        (memory_type & 15) != base_type ||
        type_size(memory_type) != width ||
        (pointer_depth == 0 && (memory_type & TYPE_UNSIGNED) != 0) ||
        !mir_machine_named_nonvolatile(insn))
        return 0;
    root = find_global(insn->name);
    if (root == NULL || !root->is_defined || root->is_volatile)
        return 0;
    location_out->root = root;
    location_out->offset = memory_offset;
    location_out->type = memory_type;
    return 1;
}

static int mir_scanner_same_global(
    const struct MirGlobalScalar *left,
    const struct MirGlobalScalar *right)
{
    return left->root == right->root &&
        left->offset == right->offset &&
        left->type == right->type;
}

static int mir_match_star_comment_char_load(
    int address_index, int pointer_value, int index_value)
{
    const struct MirInsn *address = &mir.insns[address_index];
    const struct MirInsn *load = &mir.insns[address_index + 1];

    return address->src1 == pointer_value &&
        address->src2 == index_value &&
        address->immediate == 1 &&
        address->memory_size == 1 &&
        address->bit_width == 0 &&
        (address->memory_flags & (1 | 8)) == 0 &&
        type_ptr_depth(address->type) == 1 &&
        (address->type & 15) == TYPE_CHAR &&
        type_size(address->type) == 2 &&
        load->src1 == address->dst &&
        load->memory_size == 1 &&
        load->bit_width == 0 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        type_ptr_depth(load->type) == 0 &&
        (load->type & 15) == TYPE_CHAR &&
        type_size(load->type) == 1 &&
        (load->type & TYPE_UNSIGNED) == 0;
}

static int mir_match_star_comment_conversion(
    int conversion_index, int source_value)
{
    const struct MirInsn *conversion = &mir.insns[conversion_index];

    return conversion->src1 == source_value &&
        conversion->immediate == 0 &&
        type_ptr_depth(conversion->type) == 0 &&
        (conversion->type & 15) == TYPE_INT &&
        type_size(conversion->type) == 2 &&
        (conversion->type & TYPE_UNSIGNED) == 0;
}

static int mir_match_star_comment_comparison(
    int comparison_index, int left_value, int right_value,
    int operation, int operand_type)
{
    const struct MirInsn *comparison = &mir.insns[comparison_index];

    return comparison->src1 == left_value &&
        comparison->src2 == right_value &&
        comparison->immediate == operation &&
        type_ptr_depth(comparison->type) == 0 &&
        (comparison->type & 15) == TYPE_INT &&
        type_size(comparison->type) == 2 &&
        (comparison->type & TYPE_UNSIGNED) == 0 &&
        comparison->secondary_offset == operand_type;
}

static int mir_match_star_comment_long_add(
    int binary_index, int left_value, int right_value)
{
    const struct MirInsn *binary = &mir.insns[binary_index];

    return binary->src1 == left_value &&
        binary->src2 == right_value &&
        binary->immediate == '+' &&
        type_ptr_depth(binary->type) == 0 &&
        (binary->type & 15) == TYPE_LONG &&
        type_size(binary->type) == 4 &&
        (binary->type & TYPE_UNSIGNED) == 0 &&
        binary->secondary_offset == binary->type;
}

static int mir_match_star_comment_scan_schedule(
    struct MirStarCommentScanSchedule *plan)
{
    static const int expected_opcodes[86] = {
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LABEL
    };
    struct MirGlobalScalar cursor[8];
    struct MirGlobalScalar length[2];
    struct MirGlobalScalar source[3];
    struct MirGlobalScalar line[2];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 86 || mir_cfg_block_count() != 12 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 86; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "star-comment-scan-schedule", "opcodes");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[1], TYPE_LONG, 0, 4, &cursor[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[6], TYPE_LONG, 0, 4, &cursor[1]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[8], TYPE_LONG, 0, 4, &cursor[2]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[16], TYPE_LONG, 0, 4, &cursor[3]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[24], TYPE_LONG, 0, 4, &cursor[4]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[52], TYPE_LONG, 0, 4, &cursor[5]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[64], TYPE_LONG, 0, 4, &cursor[6]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[67], TYPE_LONG, 0, 4, &cursor[7]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[1]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[2]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[3]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[4]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[5]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[6]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[7]) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 2) ||
        !mir_match_star_comment_long_add(
            4, mir.insns[1].dst, mir.insns[3].dst) ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        !mir_machine_constant_equals(mir.insns[10].dst, 1) ||
        !mir_match_star_comment_long_add(
            11, mir.insns[8].dst, mir.insns[10].dst))
        return mir_machine_reject(
            "star-comment-scan-schedule", "cursor");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[12], TYPE_LONG, 0, 4, &length[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[76], TYPE_LONG, 0, 4, &length[1]) ||
        !mir_scanner_same_global(&length[0], &length[1]) ||
        cursor[0].root == length[0].root ||
        !mir_match_star_comment_comparison(
            13, mir.insns[11].dst, mir.insns[12].dst,
            '<', mir.insns[12].type) ||
        mir.insns[14].src1 != mir.insns[13].dst ||
        mir.insns[14].label != mir.insns[46].label)
        return mir_machine_reject(
            "star-comment-scan-schedule", "bound");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[15], TYPE_CHAR, 1, 2, &source[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[23], TYPE_CHAR, 1, 2, &source[1]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[51], TYPE_CHAR, 1, 2, &source[2]) ||
        !mir_scanner_same_global(&source[0], &source[1]) ||
        !mir_scanner_same_global(&source[0], &source[2]) ||
        source[0].root == cursor[0].root ||
        source[0].root == length[0].root ||
        !mir_match_star_comment_char_load(
            17, mir.insns[15].dst, mir.insns[16].dst) ||
        !mir_match_star_comment_char_load(
            28, mir.insns[23].dst, mir.insns[27].dst) ||
        !mir_match_star_comment_char_load(
            53, mir.insns[51].dst, mir.insns[52].dst) ||
        !mir_match_star_comment_conversion(
            20, mir.insns[18].dst) ||
        !mir_match_star_comment_conversion(
            31, mir.insns[29].dst) ||
        !mir_match_star_comment_conversion(
            56, mir.insns[54].dst))
        return mir_machine_reject(
            "star-comment-scan-schedule", "characters");

    if (!mir_machine_constant_equals(mir.insns[19].dst, '*') ||
        !mir_match_star_comment_comparison(
            21, mir.insns[20].dst, mir.insns[19].dst,
            TOK_EQ, mir.insns[19].type) ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[37].label ||
        !mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        !mir_match_star_comment_long_add(
            27, mir.insns[24].dst, mir.insns[26].dst) ||
        !mir_machine_constant_equals(mir.insns[30].dst, ')') ||
        !mir_match_star_comment_comparison(
            32, mir.insns[31].dst, mir.insns[30].dst,
            TOK_EQ, mir.insns[30].type) ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].label != mir.insns[37].label ||
        !mir_machine_constant_equals(mir.insns[35].dst, 1) ||
        mir.insns[36].label != mir.insns[39].label ||
        !mir_machine_constant_equals(mir.insns[38].dst, 0) ||
        mir.insns[40].src1 != mir.insns[35].dst ||
        mir.insns[40].src2 != mir.insns[38].dst ||
        mir.insns[40].phi_pred1 != mir.insns[34].label ||
        mir.insns[40].phi_pred2 != mir.insns[37].label ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].immediate != '!' ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].label != mir.insns[46].label ||
        !mir_machine_constant_equals(mir.insns[44].dst, 1) ||
        mir.insns[45].label != mir.insns[48].label ||
        !mir_machine_constant_equals(mir.insns[47].dst, 0) ||
        mir.insns[49].src1 != mir.insns[44].dst ||
        mir.insns[49].src2 != mir.insns[47].dst ||
        mir.insns[49].phi_pred1 != mir.insns[43].label ||
        mir.insns[49].phi_pred2 != mir.insns[46].label ||
        mir.insns[50].src1 != mir.insns[49].dst ||
        mir.insns[50].label != mir.insns[71].label)
        return mir_machine_reject(
            "star-comment-scan-schedule", "delimiter");

    if (!mir_machine_constant_equals(mir.insns[55].dst, '\n') ||
        !mir_match_star_comment_comparison(
            57, mir.insns[56].dst, mir.insns[55].dst,
            TOK_EQ, mir.insns[55].type) ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[58].label != mir.insns[63].label ||
        !mir_match_scanner_global_scalar(
            &mir.insns[59], TYPE_INT, 0, 2, &line[0]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[62], TYPE_INT, 0, 2, &line[1]) ||
        !mir_scanner_same_global(&line[0], &line[1]) ||
        line[0].root == source[0].root ||
        line[0].root == cursor[0].root ||
        line[0].root == length[0].root ||
        !mir_machine_constant_equals(mir.insns[60].dst, 1) ||
        mir.insns[61].src1 != mir.insns[59].dst ||
        mir.insns[61].src2 != mir.insns[60].dst ||
        mir.insns[61].immediate != '+' ||
        mir.insns[61].type != mir.insns[59].type ||
        mir.insns[61].secondary_offset != mir.insns[59].type ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        !mir_machine_constant_equals(mir.insns[65].dst, 1) ||
        !mir_match_star_comment_long_add(
            66, mir.insns[64].dst, mir.insns[65].dst) ||
        mir.insns[67].src1 != mir.insns[66].dst ||
        mir.insns[70].label != mir.insns[7].label)
        return mir_machine_reject(
            "star-comment-scan-schedule", "body");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[72], TYPE_LONG, 0, 4, &cursor[1]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[79], TYPE_LONG, 0, 4, &cursor[2]) ||
        !mir_match_scanner_global_scalar(
            &mir.insns[84], TYPE_LONG, 0, 4, &cursor[3]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[1]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[2]) ||
        !mir_scanner_same_global(&cursor[0], &cursor[3]) ||
        !mir_machine_constant_equals(mir.insns[74].dst, 1) ||
        !mir_match_star_comment_long_add(
            75, mir.insns[72].dst, mir.insns[74].dst) ||
        !mir_match_star_comment_comparison(
            77, mir.insns[75].dst, mir.insns[76].dst,
            '<', mir.insns[76].type) ||
        mir.insns[78].src1 != mir.insns[77].dst ||
        mir.insns[78].label != mir.insns[85].label ||
        !mir_machine_constant_equals(mir.insns[81].dst, 2) ||
        !mir_match_star_comment_long_add(
            82, mir.insns[79].dst, mir.insns[81].dst) ||
        mir.insns[84].src1 != mir.insns[82].dst)
        return mir_machine_reject(
            "star-comment-scan-schedule", "exit");

    plan->source_root = source[0].root;
    plan->length_root = length[0].root;
    plan->cursor_root = cursor[0].root;
    plan->line_root = line[0].root;
    plan->source_offset = source[0].offset;
    plan->length_offset = length[0].offset;
    plan->cursor_offset = cursor[0].offset;
    plan->line_offset = line[0].offset;
    plan->opening_character = (int)mir.insns[19].immediate;
    plan->closing_character = (int)mir.insns[30].immediate;
    return 1;
}

static int mir_match_comment_member_load(
    int member_index, int load_index, int width, int pointer,
    struct MirStateMember *member_out)
{
    const struct MirInsn *member = &mir.insns[member_index];
    const struct MirInsn *load = &mir.insns[load_index];

    return member->opcode == MIR_MEMBER_ADDRESS &&
        load->opcode == MIR_LOAD_INDIRECT &&
        load->src1 == member->dst &&
        load->memory_size == width &&
        load->bit_width == 0 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        type_size(load->type) == width &&
        (pointer ? type_ptr_depth(load->type) > 0 :
                   (type_ptr_depth(load->type) == 0 &&
                    (load->type & TYPE_UNSIGNED) == 0)) &&
        mir_machine_state_member_address(
            member->dst, member_out);
}

static int mir_match_comment_scan_schedule(
    struct MirCommentScanSchedule *plan)
{
    static const int expected_opcodes[60] = {
        MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_BINARY, MIR_STORE_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_UNARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_JUMP, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL
    };
    struct MirStateMember cursor_condition;
    struct MirStateMember length_condition;
    struct MirStateMember source;
    struct MirStateMember cursor_update;
    struct MirStateMember line;
    struct MirStateMember cursor_eof;
    struct MirStateMember length_eof;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 60 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 60; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "comment-scan-schedule", "opcodes");

    if (!mir_match_comment_member_load(
            3, 4, 4, 0, &cursor_condition) ||
        !mir_match_comment_member_load(
            6, 7, 4, 0, &length_condition) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[4].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[59].label)
        return mir_machine_reject(
            "comment-scan-schedule", "condition");

    if (!mir_match_comment_member_load(
            11, 12, 2, 1, &source) ||
        !mir_match_comment_member_load(
            14, 15, 4, 0, &cursor_update) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_update) ||
        !mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        mir.insns[17].immediate != '+' ||
        mir.insns[17].src1 != mir.insns[15].dst ||
        mir.insns[17].src2 != mir.insns[16].dst ||
        mir.insns[17].type != mir.insns[15].type ||
        mir.insns[18].src1 != mir.insns[14].dst ||
        mir.insns[18].src2 != mir.insns[17].dst ||
        mir.insns[18].memory_size != 4 ||
        mir.insns[18].bit_width != 0 ||
        (mir.insns[18].memory_flags & (1 | 8)) != 0 ||
        mir.insns[19].src1 != mir.insns[12].dst ||
        mir.insns[19].src2 != mir.insns[15].dst ||
        mir.insns[19].immediate != 1 ||
        mir.insns[19].memory_size != 1 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].memory_size != 1 ||
        mir.insns[20].bit_width != 0 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].immediate != 0 ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].immediate != 0 ||
        type_size(mir.insns[22].type) != 2 ||
        mir.insns[23].src1 != mir.insns[22].dst ||
        mir.insns[23].memory_size != 2 ||
        !mir_machine_unobservable_local_store(
            &mir.insns[23]) ||
        mir.insns[24].object != mir.insns[23].object ||
        mir.insns[24].object < 0)
        return mir_machine_reject(
            "comment-scan-schedule", "cursor-read");

    if (!mir_machine_constant_equals(mir.insns[25].dst, '\n') ||
        mir.insns[26].immediate != TOK_EQ ||
        mir.insns[26].src1 != mir.insns[22].dst ||
        mir.insns[26].src2 != mir.insns[25].dst ||
        mir.insns[27].src1 != mir.insns[26].dst ||
        mir.insns[27].label != mir.insns[34].label ||
        !mir_match_comment_member_load(
            29, 30, 2, 0, &line) ||
        !mir_machine_constant_equals(mir.insns[31].dst, 1) ||
        mir.insns[32].immediate != '+' ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        mir.insns[32].src2 != mir.insns[31].dst ||
        mir.insns[33].src1 != mir.insns[29].dst ||
        mir.insns[33].src2 != mir.insns[32].dst ||
        mir.insns[33].memory_size != 2 ||
        mir.insns[33].bit_width != 0 ||
        (mir.insns[33].memory_flags & (1 | 8)) != 0 ||
        mir.insns[35].object != mir.insns[23].object)
        return mir_machine_reject(
            "comment-scan-schedule", "line");

    if (!mir_machine_constant_equals(mir.insns[36].dst, ')') ||
        mir.insns[37].immediate != TOK_EQ ||
        mir.insns[37].src1 != mir.insns[22].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].label != mir.insns[41].label ||
        mir.insns[40].label != mir.insns[59].label ||
        mir.insns[42].object != mir.insns[23].object ||
        !mir_machine_constant_equals(mir.insns[43].dst, 0x1a) ||
        mir.insns[44].immediate != TOK_EQ ||
        mir.insns[44].src1 != mir.insns[22].dst ||
        mir.insns[44].src2 != mir.insns[43].dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].label != mir.insns[55].label ||
        !mir_machine_state_member_address(
            mir.insns[47].dst, &cursor_eof) ||
        !mir_match_comment_member_load(
            49, 50, 4, 0, &length_eof) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_eof) ||
        !mir_machine_same_state_member(
            &length_condition, &length_eof) ||
        mir.insns[51].src1 != mir.insns[47].dst ||
        mir.insns[51].src2 != mir.insns[50].dst ||
        mir.insns[51].memory_size != 4 ||
        mir.insns[51].bit_width != 0 ||
        (mir.insns[51].memory_flags & (1 | 8)) != 0 ||
        mir.insns[53].label != mir.insns[59].label ||
        mir.insns[58].label != mir.insns[1].label)
        return mir_machine_reject(
            "comment-scan-schedule", "terminators");

    if (cursor_condition.root != length_condition.root ||
        cursor_condition.root != source.root ||
        cursor_condition.root != line.root ||
        cursor_condition.root_offset !=
            length_condition.root_offset ||
        cursor_condition.root_offset != source.root_offset ||
        cursor_condition.root_offset != line.root_offset ||
        cursor_condition.member_offset ==
            length_condition.member_offset ||
        cursor_condition.member_offset == source.member_offset ||
        cursor_condition.member_offset == line.member_offset ||
        length_condition.member_offset == source.member_offset ||
        length_condition.member_offset == line.member_offset ||
        source.member_offset == line.member_offset ||
        source.member_offset < -128 ||
        source.member_offset + 1 > 127 ||
        cursor_condition.member_offset < -128 ||
        cursor_condition.member_offset + 3 > 127 ||
        length_condition.member_offset < -128 ||
        length_condition.member_offset + 3 > 127 ||
        line.member_offset < -128 ||
        line.member_offset + 1 > 127 ||
        !mir_machine_only_root_loads(
            cursor_condition.root,
            cursor_condition.root_offset))
        return mir_machine_reject(
            "comment-scan-schedule", "state");

    plan->state_root = cursor_condition.root;
    plan->state_root_offset = cursor_condition.root_offset;
    plan->source_offset = source.member_offset;
    plan->length_offset = length_condition.member_offset;
    plan->cursor_offset = cursor_condition.member_offset;
    plan->line_offset = line.member_offset;
    return 1;
}

static int mir_match_whitespace_member_load(
    int member_index, int load_index, int width, int pointer,
    struct MirStateMember *member_out)
{
    const struct MirInsn *member = &mir.insns[member_index];
    const struct MirInsn *load = &mir.insns[load_index];
    struct Sym *root;
    long root_offset;

    if (member->opcode != MIR_MEMBER_ADDRESS ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0 ||
        load->opcode != MIR_LOAD_INDIRECT ||
        load->src1 != member->dst ||
        load->memory_size != width ||
        load->bit_width != 0 ||
        (load->memory_flags & (1 | 8)) != 0 ||
        type_size(load->type) != width ||
        (pointer ? type_ptr_depth(load->type) == 0 :
                   (type_ptr_depth(load->type) != 0 ||
                    (load->type & TYPE_UNSIGNED) != 0)) ||
        !mir_machine_global_address_offset(
            member->src1, &root, &root_offset, 0) ||
        root_offset < -32768 || root_offset > 32767 ||
        member->immediate < -32768 ||
        member->immediate > 32767)
        return 0;
    member_out->root = root;
    member_out->root_offset = (int)root_offset;
    member_out->member_offset = (int)member->immediate;
    return 1;
}

static int mir_whitespace_members_overlap(
    const struct MirStateMember *left, int left_width,
    const struct MirStateMember *right, int right_width)
{
    return left->member_offset <
               right->member_offset + right_width &&
           right->member_offset <
               left->member_offset + left_width;
}

static int mir_match_whitespace_scan_schedule(
    struct MirWhitespaceScanSchedule *plan)
{
    static const int expected_opcodes[60] = {
        MIR_LABEL, MIR_LABEL, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_UNARY, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_BINARY,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_BINARY, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL
    };
    struct MirStateMember cursor_condition;
    struct MirStateMember length_condition;
    struct MirStateMember source_condition;
    struct MirStateMember cursor_condition_source;
    struct MirStateMember source_body;
    struct MirStateMember cursor_body;
    struct MirStateMember line;
    struct MirStateMember cursor_update;
    int call_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 60 || mir_cfg_block_count() != 8 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 60; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "whitespace-scan-schedule", "opcodes");

    if (!mir_match_whitespace_member_load(
            3, 4, 4, 0, &cursor_condition) ||
        !mir_match_whitespace_member_load(
            6, 7, 4, 0, &length_condition) ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[4].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        type_size(mir.insns[8].secondary_offset) != 4 ||
        type_size(mir.insns[8].type) != 2 ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[26].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "bound");

    if (!mir_match_whitespace_member_load(
            11, 12, 2, 1, &source_condition) ||
        !mir_match_whitespace_member_load(
            14, 15, 4, 0, &cursor_condition_source) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_condition_source) ||
        mir.insns[16].src1 != mir.insns[12].dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != 1 ||
        mir.insns[16].memory_size != 1 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].memory_size != 1 ||
        mir.insns[17].bit_width != 0 ||
        (mir.insns[17].memory_flags & (1 | 8)) != 0 ||
        type_ptr_depth(mir.insns[17].type) != 0 ||
        type_size(mir.insns[17].type) != 1 ||
        (mir.insns[17].type & TYPE_UNSIGNED) != 0 ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        mir.insns[18].immediate != 0 ||
        type_size(mir.insns[18].type) != 1 ||
        (mir.insns[18].type & TYPE_UNSIGNED) == 0 ||
        mir.insns[19].src1 != mir.insns[18].dst ||
        mir.insns[19].immediate != 0 ||
        type_ptr_depth(mir.insns[19].type) != 0 ||
        type_size(mir.insns[19].type) != 2 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[21], &call_argument) ||
        call_argument != mir.insns[19].dst ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        mir.insns[22].label != mir.insns[26].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "helper-call");
    plan->space_function = find_global(mir.insns[21].name);
    if (plan->space_function == NULL ||
        plan->space_function->storage != SC_FUNC ||
        plan->space_function->is_funcptr ||
        plan->space_function->is_noreturn ||
        !plan->space_function->has_proto ||
        plan->space_function->proto_nargs != 1 ||
        plan->space_function->proto_variadic ||
        type_ptr_depth(plan->space_function->proto_types[0]) != 0 ||
        type_size(plan->space_function->proto_types[0]) != 2 ||
        type_ptr_depth(mir.insns[21].type) != 0 ||
        type_size(mir.insns[21].type) != 2 ||
        (mir.insns[21].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        (mir.insns[21].base_name[0] != 0 &&
         strcmp(mir.insns[21].base_name,
                asm_name_for(sym_asm_name(
                    plan->space_function)))))
        return mir_machine_reject(
            "whitespace-scan-schedule", "helper-function");

    if (!mir_machine_constant_equals(mir.insns[24].dst, 1) ||
        mir.insns[25].label != mir.insns[28].label ||
        !mir_machine_constant_equals(mir.insns[27].dst, 0) ||
        mir.insns[29].src1 != mir.insns[24].dst ||
        mir.insns[29].src2 != mir.insns[27].dst ||
        mir.insns[29].phi_pred1 != mir.insns[23].label ||
        mir.insns[29].phi_pred2 != mir.insns[26].label ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[30].label != mir.insns[59].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "short-circuit");

    if (!mir_match_whitespace_member_load(
            32, 33, 2, 1, &source_body) ||
        !mir_match_whitespace_member_load(
            35, 36, 4, 0, &cursor_body) ||
        !mir_machine_same_state_member(
            &source_condition, &source_body) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_body) ||
        mir.insns[37].src1 != mir.insns[33].dst ||
        mir.insns[37].src2 != mir.insns[36].dst ||
        mir.insns[37].immediate != 1 ||
        mir.insns[37].memory_size != 1 ||
        (mir.insns[37].memory_flags & (1 | 8)) != 0 ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[38].memory_size != 1 ||
        mir.insns[38].bit_width != 0 ||
        (mir.insns[38].memory_flags & (1 | 8)) != 0 ||
        mir.insns[38].type != mir.insns[17].type ||
        !mir_machine_constant_equals(mir.insns[39].dst, '\n') ||
        mir.insns[40].src1 != mir.insns[38].dst ||
        mir.insns[40].immediate != 0 ||
        type_size(mir.insns[40].type) != 2 ||
        mir.insns[41].immediate != TOK_EQ ||
        mir.insns[41].src1 != mir.insns[40].dst ||
        mir.insns[41].src2 != mir.insns[39].dst ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].label != mir.insns[49].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "body-read");

    if (!mir_match_whitespace_member_load(
            44, 45, 2, 0, &line) ||
        !mir_machine_constant_equals(mir.insns[46].dst, 1) ||
        mir.insns[47].immediate != '+' ||
        mir.insns[47].src1 != mir.insns[45].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[48].src1 != mir.insns[44].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[48].memory_size != 2 ||
        mir.insns[48].bit_width != 0 ||
        (mir.insns[48].memory_flags & (1 | 8)) != 0 ||
        !mir_match_whitespace_member_load(
            51, 52, 4, 0, &cursor_update) ||
        !mir_machine_same_state_member(
            &cursor_condition, &cursor_update) ||
        !mir_machine_constant_equals(mir.insns[53].dst, 1) ||
        mir.insns[54].immediate != '+' ||
        mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[54].src2 != mir.insns[53].dst ||
        mir.insns[55].src1 != mir.insns[51].dst ||
        mir.insns[55].src2 != mir.insns[54].dst ||
        mir.insns[55].memory_size != 4 ||
        mir.insns[55].bit_width != 0 ||
        (mir.insns[55].memory_flags & (1 | 8)) != 0 ||
        mir.insns[58].label != mir.insns[1].label)
        return mir_machine_reject(
            "whitespace-scan-schedule", "updates");

    if (cursor_condition.root != length_condition.root ||
        cursor_condition.root != source_condition.root ||
        cursor_condition.root != line.root ||
        cursor_condition.root_offset !=
            length_condition.root_offset ||
        cursor_condition.root_offset !=
            source_condition.root_offset ||
        cursor_condition.root_offset != line.root_offset ||
        cursor_condition.member_offset ==
            length_condition.member_offset ||
        cursor_condition.member_offset ==
            source_condition.member_offset ||
        cursor_condition.member_offset == line.member_offset ||
        length_condition.member_offset ==
            source_condition.member_offset ||
        length_condition.member_offset == line.member_offset ||
        source_condition.member_offset == line.member_offset ||
        mir_whitespace_members_overlap(
            &source_condition, 2, &length_condition, 4) ||
        mir_whitespace_members_overlap(
            &source_condition, 2, &cursor_condition, 4) ||
        mir_whitespace_members_overlap(
            &source_condition, 2, &line, 2) ||
        mir_whitespace_members_overlap(
            &length_condition, 4, &cursor_condition, 4) ||
        mir_whitespace_members_overlap(
            &length_condition, 4, &line, 2) ||
        mir_whitespace_members_overlap(
            &cursor_condition, 4, &line, 2) ||
        source_condition.member_offset < -128 ||
        source_condition.member_offset + 1 > 127 ||
        cursor_condition.member_offset < -128 ||
        cursor_condition.member_offset + 3 > 127 ||
        length_condition.member_offset < -128 ||
        length_condition.member_offset + 3 > 127 ||
        line.member_offset < -128 ||
        line.member_offset + 1 > 127)
        return mir_machine_reject(
            "whitespace-scan-schedule", "state");

    plan->state_root = cursor_condition.root;
    plan->state_root_offset = cursor_condition.root_offset;
    plan->source_offset = source_condition.member_offset;
    plan->length_offset = length_condition.member_offset;
    plan->cursor_offset = cursor_condition.member_offset;
    plan->line_offset = line.member_offset;
    return 1;
}

static int mir_match_action_decode_member(
    int load_index, int member_index, int parameter_index,
    int *offset_out)
{
    const struct MirInsn *load = &mir.insns[load_index];
    const struct MirInsn *member = &mir.insns[member_index];

    if (load->opcode != MIR_LOAD ||
        !mir_machine_same_location(
            load, &mir.insns[parameter_index]) ||
        member->opcode != MIR_MEMBER_ADDRESS ||
        member->src1 != load->dst ||
        member->memory_size != 2 ||
        member->bit_width != 0 ||
        (member->memory_flags & (1 | 8)) != 0 ||
        member->immediate < -32768 ||
        member->immediate > 32767)
        return 0;
    *offset_out = (int)member->immediate;
    return 1;
}

static int mir_match_action_decode_call(
    const struct MirInsn *call, int argument_count,
    struct Sym **function_out)
{
    struct Sym *function = find_global(call->name);

    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr || function->is_noreturn ||
        !function->has_proto ||
        function->proto_nargs != argument_count ||
        function->proto_variadic ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0 ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_action_decode_string(
    int instruction, int *string_id)
{
    const struct MirInsn *string = &mir.insns[instruction];

    if (string->opcode != MIR_STRING_ADDRESS ||
        !mir_match_action_decode_pointer_type(string->type))
        return 0;
    *string_id = (int)string->immediate;
    return 1;
}

static int mir_match_action_decode_schedule(
    struct MirActionDecodeSchedule *plan)
{
    static const int expected_opcodes[80] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_STORE_INDIRECT, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_MEMBER_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_CONST, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CONST,
        MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP, MIR_LABEL
    };
    int arguments2[2];
    int arguments3[3];
    int argument;
    int action_offset;
    int instruction;
    long constant;
    struct Sym *function;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 80 || mir_cfg_block_count() != 11 ||
        mir.has_vla || mir.local_bytes != 0 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 80; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "action-decode-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst,
            &plan->statement_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->text_stack_offset) ||
        plan->statement_stack_offset ==
            plan->text_stack_offset ||
        plan->statement_stack_offset + 3 > 127 ||
        plan->text_stack_offset + 3 > 127 ||
        !mir_match_action_decode_pointer_type(
            mir.insns[1].type) ||
        !mir_match_action_decode_pointer_type(
            mir.insns[2].type) ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        mir_machine_pointee_is_volatile(&mir.insns[2]) ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[3]) ||
        !mir_machine_single_call_argument(
            &mir.insns[5], &argument) ||
        argument != mir.insns[3].dst ||
        !mir_match_action_decode_call(
            &mir.insns[5], 1, &plan->trim_function) ||
        !mir_match_action_decode_pointer_type(
            plan->trim_function->proto_types[0]) ||
        (mir.insns[5].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "action-decode-schedule", "entry");

    if (!mir_match_action_decode_member(
            6, 7, 1, &plan->action_offset) ||
        !mir_machine_constant_value(
            mir.insns[8].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        mir.insns[9].src1 != mir.insns[7].dst ||
        mir.insns[9].src2 != mir.insns[8].dst ||
        mir.insns[9].memory_size != 2 ||
        mir.insns[9].bit_width != 0 ||
        (mir.insns[9].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "default-store");
    plan->default_action = (int)((unsigned long)constant & 0xffffUL);

    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[10]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[14], arguments2) ||
        arguments2[0] != mir.insns[10].dst ||
        arguments2[1] != mir.insns[12].dst ||
        !mir_match_action_decode_string(
            12, &plan->goto_string_ids[0]) ||
        !mir_match_action_decode_call(
            &mir.insns[14], 2, &plan->prefix_function) ||
        !mir_match_action_decode_pointer_type(
            plan->prefix_function->proto_types[0]) ||
        !mir_match_action_decode_pointer_type(
            plan->prefix_function->proto_types[1]) ||
        !mir_match_action_decode_word_type(
            mir.insns[14].type) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].label != mir.insns[19].label ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[20]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[24], arguments2) ||
        arguments2[0] != mir.insns[20].dst ||
        arguments2[1] != mir.insns[22].dst ||
        !mir_match_action_decode_string(
            22, &plan->goto_string_ids[1]) ||
        !mir_match_action_decode_call(
            &mir.insns[24], 2, &function) ||
        function !=
            plan->prefix_function ||
        mir.insns[24].type != mir.insns[14].type ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        mir.insns[25].label != mir.insns[29].label ||
        !mir_machine_constant_equals(
            mir.insns[17].dst, 1) ||
        mir.insns[18].label != mir.insns[35].label ||
        !mir_machine_constant_equals(
            mir.insns[27].dst, 1) ||
        mir.insns[28].label != mir.insns[31].label ||
        !mir_machine_constant_equals(
            mir.insns[30].dst, 0) ||
        mir.insns[32].src1 != mir.insns[27].dst ||
        mir.insns[32].src2 != mir.insns[30].dst ||
        mir.insns[32].phi_pred1 != mir.insns[26].label ||
        mir.insns[32].phi_pred2 != mir.insns[29].label ||
        mir.insns[34].label != mir.insns[35].label ||
        mir.insns[36].src1 != mir.insns[17].dst ||
        mir.insns[36].src2 != mir.insns[32].dst ||
        mir.insns[36].phi_pred1 != mir.insns[16].label ||
        mir.insns[36].phi_pred2 != mir.insns[33].label ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].label != mir.insns[50].label ||
        plan->goto_string_ids[0] ==
            plan->goto_string_ids[1])
        return mir_machine_reject(
            "action-decode-schedule", "goto-test");

    if (!mir_match_action_decode_member(
            38, 39, 1, &action_offset) ||
        action_offset != plan->action_offset ||
        !mir_machine_constant_value(
            mir.insns[40].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].memory_size != 2 ||
        mir.insns[41].bit_width != 0 ||
        (mir.insns[41].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "goto-store");
    plan->goto_action = (int)((unsigned long)constant & 0xffffUL);

    if (!mir_match_action_decode_member(
            42, 43, 1, &plan->target_offset) ||
        (plan->target_offset < plan->action_offset + 2 &&
         plan->action_offset < plan->target_offset + 2))
        return mir_machine_reject(
            "action-decode-schedule", "target-member");
    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[44]) ||
        !mir_machine_single_call_argument(
            &mir.insns[46], &argument) ||
        argument != mir.insns[44].dst ||
        !mir_match_action_decode_call(
            &mir.insns[46], 1, &plan->label_function) ||
        !mir_match_action_decode_pointer_type(
            plan->label_function->proto_types[0]) ||
        !mir_match_action_decode_word_type(
            mir.insns[46].type) ||
        mir.insns[47].src1 != mir.insns[43].dst ||
        mir.insns[47].src2 != mir.insns[46].dst ||
        mir.insns[47].memory_size != 2 ||
        mir.insns[47].bit_width != 0 ||
        (mir.insns[47].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "target-call");

    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[51]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[55], arguments2) ||
        arguments2[0] != mir.insns[51].dst ||
        arguments2[1] != mir.insns[53].dst ||
        !mir_match_action_decode_string(
            53, &plan->return_string_id) ||
        plan->return_string_id ==
            plan->goto_string_ids[0] ||
        plan->return_string_id ==
            plan->goto_string_ids[1] ||
        !mir_match_action_decode_call(
            &mir.insns[55], 2, &function) ||
        function !=
            plan->prefix_function ||
        mir.insns[55].type != mir.insns[14].type ||
        mir.insns[56].src1 != mir.insns[55].dst ||
        mir.insns[56].label != mir.insns[63].label ||
        !mir_match_action_decode_member(
            57, 58, 1, &action_offset) ||
        action_offset != plan->action_offset ||
        !mir_machine_constant_value(
            mir.insns[59].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].src2 != mir.insns[59].dst ||
        mir.insns[60].memory_size != 2 ||
        mir.insns[60].bit_width != 0 ||
        (mir.insns[60].memory_flags & (1 | 8)) != 0)
        return mir_machine_reject(
            "action-decode-schedule", "return-test");
    plan->return_action =
        (int)((unsigned long)constant & 0xffffUL);
    if (plan->default_action == plan->goto_action ||
        plan->default_action == plan->return_action ||
        plan->goto_action == plan->return_action)
        return mir_machine_reject(
            "action-decode-schedule", "action-values");

    if (!mir_machine_same_location(
            &mir.insns[2], &mir.insns[64]) ||
        !mir_machine_two_call_arguments(
            &mir.insns[68], arguments2) ||
        arguments2[0] != mir.insns[64].dst ||
        arguments2[1] != mir.insns[66].dst ||
        !mir_machine_constant_value(
            mir.insns[66].dst, &constant, 0) ||
        constant < 0 || constant > 255 ||
        !mir_match_action_decode_call(
            &mir.insns[68], 2, &plan->search_function) ||
        !mir_match_action_decode_pointer_type(
            plan->search_function->proto_types[0]) ||
        !mir_match_action_decode_word_type(
            plan->search_function->proto_types[1]) ||
        !mir_match_action_decode_pointer_type(
            mir.insns[68].type) ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[69].label != mir.insns[79].label)
        return mir_machine_reject(
            "action-decode-schedule", "search");
    plan->separator = (int)constant;

    if (!mir_machine_same_location(
            &mir.insns[1], &mir.insns[70]) ||
        !mir_machine_same_location(
            &mir.insns[2], &mir.insns[72]) ||
        !mir_machine_three_call_arguments(
            &mir.insns[76], arguments3) ||
        arguments3[0] != mir.insns[70].dst ||
        arguments3[1] != mir.insns[72].dst ||
        arguments3[2] != mir.insns[74].dst ||
        !mir_machine_constant_value(
            mir.insns[74].dst, &constant, 0) ||
        constant < -32768 || constant > 65535 ||
        !mir_match_action_decode_call(
            &mir.insns[76], 3,
            &plan->assignment_function) ||
        !mir_match_action_decode_pointer_type(
            plan->assignment_function->proto_types[0]) ||
        !mir_match_action_decode_pointer_type(
            plan->assignment_function->proto_types[1]) ||
        !mir_match_action_decode_word_type(
            plan->assignment_function->proto_types[2]) ||
        (mir.insns[76].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "action-decode-schedule", "assignment");
    plan->assignment_mode =
        (int)((unsigned long)constant & 0xffffUL);
    return 1;
}

static int mir_match_buffered_declaration_global(
    const struct MirInsn *load, int require_pointer,
    struct Sym **root_out, int *offset_out)
{
    struct Sym *root;
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (load->opcode != MIR_LOAD ||
        !mir_scalar_memory_location(
            load, &memory_type, &memory_storage, &memory_offset) ||
        (memory_storage != SC_GLOBAL &&
         memory_storage != SC_EXTERN) ||
        memory_offset < -32768 || memory_offset > 32767)
        return 0;
    if (require_pointer) {
        if (!mir_match_action_decode_pointer_type(memory_type))
            return 0;
    } else if (!mir_match_action_decode_word_type(memory_type) ||
               (memory_type & TYPE_UNSIGNED) != 0) {
        return 0;
    }
    root = find_global(load->name);
    if (root == NULL || root->is_volatile ||
        (root->storage != SC_GLOBAL &&
         root->storage != SC_EXTERN))
        return 0;
    *root_out = root;
    *offset_out = memory_offset;
    return 1;
}

static int mir_match_buffered_declaration_schedule(
    struct MirBufferedDeclarationSchedule *plan)
{
    static const int expected_opcodes[72] = {
        MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_PHI, MIR_NOP, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_ARG, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL, MIR_ADDRESS,
        MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_JUMP,
        MIR_LABEL, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL
    };
    const struct MirInsn *buffer = &mir.insns[10];
    int arguments2[2];
    int argument;
    int cursor_type;
    int cursor_storage;
    int cursor_offset;
    int buffer_offset;
    int instruction;
    int phase;
    long constant;
    struct Sym *function;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 72 || mir_cfg_block_count() != 11 ||
        mir.has_vla || mir.aggregate_temp_bytes != 0 ||
        mir.local_bytes < 3 || mir.local_bytes > 32767 ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < 72; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "buffered-declaration-schedule", "opcodes");

    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[3]) ||
        mir.insns[3].src1 != mir.insns[1].dst ||
        !mir_scalar_memory_location(
            &mir.insns[3], &cursor_type,
            &cursor_storage, &cursor_offset) ||
        cursor_storage != SC_LOCAL ||
        !mir_match_action_decode_word_type(cursor_type) ||
        (cursor_type & TYPE_UNSIGNED) != 0 ||
        cursor_offset >= 0 || cursor_offset < -mir.local_bytes ||
        cursor_offset + 1 >= 0 ||
        mir.insns[5].src1 != mir.insns[1].dst ||
        mir.insns[5].src2 != mir.insns[68].dst ||
        mir.insns[5].phi_pred1 != mir.insns[0].label ||
        mir.insns[5].phi_pred2 != mir.insns[65].label ||
        mir.insns[8].opcode != MIR_BINARY ||
        mir.insns[8].immediate != '<' ||
        mir.insns[8].src1 != mir.insns[5].dst ||
        mir.insns[8].src2 != mir.insns[7].dst ||
        !mir_match_action_decode_word_type(
            mir.insns[8].secondary_offset) ||
        (mir.insns[8].secondary_offset & TYPE_UNSIGNED) != 0 ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        mir.insns[9].label != mir.insns[71].label ||
        !mir_match_buffered_declaration_global(
            &mir.insns[7], 0, &plan->count_root,
            &plan->count_offset))
        return mir_machine_reject(
            "buffered-declaration-schedule", "loop");
    plan->cursor_offset = cursor_offset;

    if (!mir_match_buffered_declaration_buffer(
            10, buffer, &buffer_offset) ||
        buffer_offset != -mir.local_bytes ||
        cursor_offset != -2)
        return mir_machine_reject(
            "buffered-declaration-schedule", "buffer");
    if (
        !mir_machine_two_call_arguments(
            &mir.insns[18], arguments2) ||
        arguments2[0] != mir.insns[10].dst ||
        arguments2[1] != mir.insns[16].dst ||
        !mir_match_buffered_declaration_global(
            &mir.insns[12], 1, &plan->statements_root,
            &plan->statements_offset) ||
        plan->statements_root == plan->count_root ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[5].dst ||
        mir.insns[14].immediate <= 0 ||
        mir.insns[14].immediate > 32767 ||
        mir.insns[14].memory_size != mir.insns[14].immediate ||
        !mir_match_action_decode_pointer_type(
            mir.insns[14].type) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].immediate < 0 ||
        mir.insns[15].immediate + 2 >
            mir.insns[14].immediate ||
        mir.insns[15].memory_size != 2 ||
        mir.insns[15].bit_width != 0 ||
        (mir.insns[15].memory_flags & (1 | 8)) != 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 2 ||
        mir.insns[16].bit_width != 0 ||
        (mir.insns[16].memory_flags & (1 | 8)) != 0 ||
        !mir_match_action_decode_pointer_type(
            mir.insns[16].type))
        return mir_machine_reject(
            "buffered-declaration-schedule", "copy-input");
    if (
        !mir_match_action_decode_call(
            &mir.insns[18], 2, &plan->copy_function) ||
        !mir_match_action_decode_pointer_type(
            plan->copy_function->proto_types[0]) ||
        !mir_match_action_decode_pointer_type(
            plan->copy_function->proto_types[1]) ||
        !mir_match_action_decode_pointer_type(
            mir.insns[18].type) ||
        mir_value_use_count(mir.insns[18].dst) != 0)
        return mir_machine_reject(
            "buffered-declaration-schedule", "copy-call");
    plan->buffer_offset = buffer_offset;
    plan->frame_bytes = mir.local_bytes;
    plan->record_stride = (int)mir.insns[14].immediate;
    plan->text_offset = (int)mir.insns[15].immediate;

    if (!mir_match_buffered_declaration_buffer(
            19, buffer, &buffer_offset) ||
        !mir_machine_single_call_argument(
            &mir.insns[21], &argument) ||
        argument != mir.insns[19].dst ||
        !mir_match_action_decode_call(
            &mir.insns[21], 1, &plan->trim_function) ||
        !mir_match_action_decode_pointer_type(
            plan->trim_function->proto_types[0]) ||
        (mir.insns[21].type & 15) != TYPE_VOID)
        return mir_machine_reject(
            "buffered-declaration-schedule", "trim");

    for (phase = 0; phase < 3; ++phase) {
        static const int address_indices[3] = { 22, 36, 50 };
        static const int string_indices[3] = { 24, 38, 52 };
        static const int prefix_call_indices[3] = { 26, 40, 54 };
        static const int branch_indices[3] = { 27, 41, 55 };
        static const int false_label_indices[3] = { 35, 49, 61 };
        static const int declaration_address_indices[3] = { 29, 43, 56 };
        static const int constant_indices[3] = { 31, 45, 58 };
        static const int declaration_call_indices[3] = { 33, 47, 60 };
        int address_index = address_indices[phase];
        int string_index = string_indices[phase];
        int prefix_call_index = prefix_call_indices[phase];
        int declaration_address_index =
            declaration_address_indices[phase];
        int constant_index = constant_indices[phase];
        int declaration_call_index =
            declaration_call_indices[phase];

        if (!mir_match_buffered_declaration_buffer(
                address_index, buffer, &buffer_offset) ||
            mir.insns[string_index].opcode !=
                MIR_STRING_ADDRESS ||
            !mir_match_action_decode_pointer_type(
                mir.insns[string_index].type) ||
            !mir_machine_two_call_arguments(
                &mir.insns[prefix_call_index], arguments2) ||
            arguments2[0] != mir.insns[address_index].dst ||
            arguments2[1] != mir.insns[string_index].dst ||
            !mir_match_action_decode_call(
                &mir.insns[prefix_call_index], 2, &function) ||
            (phase == 0
                 ? (plan->prefix_function = function, 0)
                 : function != plan->prefix_function) ||
            !mir_match_action_decode_pointer_type(
                function->proto_types[0]) ||
            !mir_match_action_decode_pointer_type(
                function->proto_types[1]) ||
            !mir_match_action_decode_word_type(
                mir.insns[prefix_call_index].type) ||
            mir.insns[branch_indices[phase]].src1 !=
                mir.insns[prefix_call_index].dst ||
            mir.insns[branch_indices[phase]].label !=
                mir.insns[false_label_indices[phase]].label ||
            !mir_match_buffered_declaration_buffer(
                declaration_address_index, buffer,
                &buffer_offset) ||
            !mir_machine_constant_value(
                mir.insns[constant_index].dst,
                &constant, 0) ||
            constant < -32768 || constant > 65535 ||
            !mir_machine_two_call_arguments(
                &mir.insns[declaration_call_index],
                arguments2) ||
            arguments2[0] !=
                mir.insns[declaration_address_index].dst ||
            arguments2[1] !=
                mir.insns[constant_index].dst ||
            !mir_match_action_decode_call(
                &mir.insns[declaration_call_index], 2,
                &function) ||
            (phase == 0
                 ? (plan->declaration_function = function, 0)
                 : function != plan->declaration_function) ||
            !mir_match_action_decode_pointer_type(
                function->proto_types[0]) ||
            !mir_match_action_decode_word_type(
                function->proto_types[1]) ||
            (mir.insns[declaration_call_index].type & 15) !=
                TYPE_VOID)
            return mir_machine_reject(
                "buffered-declaration-schedule",
                "classification");
        plan->string_ids[phase] =
            (int)mir.insns[string_index].immediate;
        plan->declaration_types[phase] =
            (int)((unsigned long)constant & 0xffffUL);
    }
    if (plan->string_ids[0] == plan->string_ids[1] ||
        plan->string_ids[0] == plan->string_ids[2] ||
        plan->string_ids[1] == plan->string_ids[2] ||
        plan->declaration_types[0] !=
            plan->declaration_types[2] ||
        plan->declaration_types[0] ==
            plan->declaration_types[1] ||
        mir.insns[34].label != mir.insns[63].label ||
        mir.insns[48].label != mir.insns[62].label)
        return mir_machine_reject(
            "buffered-declaration-schedule", "dispatch");

    if (!mir_machine_constant_equals(mir.insns[67].dst, 1) ||
        mir.insns[68].opcode != MIR_BINARY ||
        mir.insns[68].immediate != '+' ||
        mir.insns[68].src1 != mir.insns[5].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        !mir_match_action_decode_word_type(
            mir.insns[68].secondary_offset) ||
        !mir_machine_same_location(
            &mir.insns[3], &mir.insns[69]) ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[70].label != mir.insns[4].label)
        return mir_machine_reject(
            "buffered-declaration-schedule", "increment");
    return 1;
}

static int mir_match_symbol_find_schedule(
    struct MirSymbolFindSchedule *plan)
{
    static const int expected_opcodes[87] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_LOAD, MIR_PHI, MIR_NOP, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_ARG, MIR_LOAD,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_NOP, MIR_RETURN,
        MIR_LABEL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_RETURN
    };
    int compare_arguments[2];
    int copy_arguments[3];
    int error_argument;
    int count_type, count_storage, count_offset;
    int symbols_type, symbols_storage, symbols_offset;
    int memory_type, memory_storage, memory_offset;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 87 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "symbol-find-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        type_ptr_depth(mir.insns[1].type) == 0 ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[6]) ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[17]) ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[44]) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[4]) ||
        mir.insns[4].src1 != mir.insns[2].dst ||
        mir.insns[7].src1 != mir.insns[2].dst ||
        mir.insns[7].src2 != mir.insns[27].dst ||
        mir.insns[7].phi_pred1 != mir.insns[0].label ||
        mir.insns[7].phi_pred2 != mir.insns[24].label ||
        mir.insns[7].object != mir.insns[4].object ||
        mir.insns[7].object < 0 ||
        mir.insns[8].object != mir.insns[7].object)
        return mir_machine_reject(
            "symbol-find-schedule", "index-entry");

    if (!mir_scalar_memory_location(
            &mir.insns[9], &count_type, &count_storage,
            &count_offset) ||
        count_storage != SC_GLOBAL ||
        type_ptr_depth(count_type) != 0 ||
        type_size(count_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[9]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[31]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[40]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[53]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[62]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[69]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[82]) ||
        !mir_machine_same_location(
            &mir.insns[9], &mir.insns[85]) ||
        mir.insns[10].immediate != '<' ||
        mir.insns[10].src1 != mir.insns[7].dst ||
        mir.insns[10].src2 != mir.insns[9].dst ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].label != mir.insns[30].label)
        return mir_machine_reject(
            "symbol-find-schedule", "bounded-loop");
    plan->count_root = find_global(mir.insns[9].name);
    if (plan->count_root == NULL ||
        plan->count_root->storage == SC_FUNC ||
        plan->count_root->is_volatile)
        return mir_machine_reject(
            "symbol-find-schedule", "count-global");
    plan->count_offset = count_offset;

    if (!mir_scalar_memory_location(
            &mir.insns[12], &symbols_type, &symbols_storage,
            &symbols_offset) ||
        symbols_storage != SC_GLOBAL ||
        type_ptr_depth(symbols_type) == 0 ||
        type_size(symbols_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[12]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[39]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[52]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[61]) ||
        !mir_machine_same_location(
            &mir.insns[12], &mir.insns[68]))
        return mir_machine_reject(
            "symbol-find-schedule", "symbols-global");
    plan->symbols_root = find_global(mir.insns[12].name);
    if (plan->symbols_root == NULL ||
        plan->symbols_root->storage == SC_FUNC ||
        plan->symbols_root->is_volatile ||
        plan->symbols_root == plan->count_root)
        return mir_machine_reject(
            "symbol-find-schedule", "symbols-root");
    plan->symbols_offset = symbols_offset;

    plan->record_stride = (int)mir.insns[14].immediate;
    plan->name_field_offset = (int)mir.insns[15].immediate;
    plan->name_field_size = mir.insns[15].memory_size;
    if (plan->record_stride <= 0 ||
        plan->record_stride > 255 ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[7].dst ||
        mir.insns[14].memory_size != plan->record_stride ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        plan->name_field_offset != 0 ||
        plan->name_field_size <= 1 ||
        plan->name_field_size > plan->record_stride ||
        type_ptr_depth(mir.insns[15].type) == 0 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[18].src1 != mir.insns[17].dst ||
        !mir_machine_two_call_arguments(
            &mir.insns[19], compare_arguments) ||
        compare_arguments[0] != mir.insns[15].dst ||
        compare_arguments[1] != mir.insns[17].dst ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[23].label ||
        mir.insns[22].src1 != mir.insns[7].dst)
        return mir_machine_reject(
            "symbol-find-schedule", "compare");
    plan->compare_function = find_global(mir.insns[19].name);
    if (plan->compare_function == NULL ||
        plan->compare_function->storage != SC_FUNC ||
        plan->compare_function->is_funcptr ||
        plan->compare_function->is_noreturn ||
        !plan->compare_function->has_proto ||
        plan->compare_function->proto_nargs != 2 ||
        plan->compare_function->proto_variadic ||
        type_ptr_depth(plan->compare_function->proto_types[0]) == 0 ||
        type_ptr_depth(plan->compare_function->proto_types[1]) == 0 ||
        type_ptr_depth(mir.insns[19].type) != 0 ||
        type_size(mir.insns[19].type) != 2 ||
        (mir.insns[19].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "compare-function");

    if (!mir_machine_constant_equals(mir.insns[26].dst, 1) ||
        mir.insns[27].immediate != '+' ||
        mir.insns[27].src1 != mir.insns[7].dst ||
        mir.insns[27].src2 != mir.insns[26].dst ||
        !mir_machine_same_location(
            &mir.insns[4], &mir.insns[28]) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        mir.insns[29].label != mir.insns[5].label)
        return mir_machine_reject(
            "symbol-find-schedule", "index-step");

    {
        long symbol_limit;

        if (mir.insns[33].immediate != TOK_GE ||
            mir.insns[33].src1 != mir.insns[31].dst ||
            mir.insns[33].src2 != mir.insns[32].dst ||
            !mir_machine_constant_value(
                mir.insns[32].dst, &symbol_limit, 0) ||
            symbol_limit <= 0 || symbol_limit > 255 ||
            mir.insns[34].src1 != mir.insns[33].dst ||
            mir.insns[34].label != mir.insns[38].label ||
            mir.insns[35].type == 0 ||
            type_ptr_depth(mir.insns[35].type) == 0 ||
            !mir_machine_single_call_argument(
                &mir.insns[37], &error_argument) ||
            error_argument != mir.insns[35].dst)
            return mir_machine_reject(
                "symbol-find-schedule", "symbol-limit");
        plan->symbol_limit = (int)symbol_limit;
    }
    plan->symbol_error_string_id =
        (int)mir.insns[35].immediate;
    plan->error_function = find_global(mir.insns[37].name);
    if (plan->error_function == NULL ||
        plan->error_function->storage != SC_FUNC ||
        plan->error_function->is_funcptr ||
        !plan->error_function->has_proto ||
        plan->error_function->proto_nargs != 1 ||
        plan->error_function->proto_variadic ||
        type_ptr_depth(
            plan->error_function->proto_types[0]) == 0 ||
        (mir.insns[37].type & 15) != TYPE_VOID ||
        (mir.insns[37].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "error-function");

    if (mir.insns[41].src1 != mir.insns[39].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].immediate != plan->record_stride ||
        mir.insns[41].memory_size != plan->record_stride ||
        mir.insns[42].src1 != mir.insns[41].dst ||
        mir.insns[42].immediate != plan->name_field_offset ||
        mir.insns[42].memory_size != plan->name_field_size ||
        mir.insns[43].src1 != mir.insns[42].dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        !mir_machine_constant_equals(
            mir.insns[48].dst,
            plan->name_field_size - 1) ||
        mir.insns[50].src1 != mir.insns[48].dst ||
        !mir_machine_three_call_arguments(
            &mir.insns[51], copy_arguments) ||
        copy_arguments[0] != mir.insns[42].dst ||
        copy_arguments[1] != mir.insns[44].dst ||
        copy_arguments[2] != mir.insns[48].dst)
        return mir_machine_reject(
            "symbol-find-schedule", "copy");
    plan->copy_function = find_global(mir.insns[51].name);
    if (plan->copy_function == NULL ||
        plan->copy_function->storage != SC_FUNC ||
        plan->copy_function->is_funcptr ||
        plan->copy_function->is_noreturn ||
        !plan->copy_function->has_proto ||
        plan->copy_function->proto_nargs != 3 ||
        plan->copy_function->proto_variadic ||
        type_ptr_depth(plan->copy_function->proto_types[0]) == 0 ||
        type_ptr_depth(plan->copy_function->proto_types[1]) == 0 ||
        type_ptr_depth(plan->copy_function->proto_types[2]) != 0 ||
        type_size(plan->copy_function->proto_types[2]) != 2 ||
        type_ptr_depth(mir.insns[51].type) == 0 ||
        (mir.insns[51].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "copy-function");

    if (mir.insns[54].src1 != mir.insns[52].dst ||
        mir.insns[54].src2 != mir.insns[53].dst ||
        mir.insns[54].immediate != plan->record_stride ||
        mir.insns[54].memory_size != plan->record_stride ||
        mir.insns[55].src1 != mir.insns[54].dst ||
        mir.insns[55].memory_size != 2 ||
        mir.insns[56].opcode != MIR_LOAD ||
        !mir_scalar_memory_location(
            &mir.insns[56], &memory_type, &memory_storage,
            &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        type_ptr_depth(memory_type) != 0 ||
        type_size(memory_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[56]) ||
        !mir_machine_same_location(
            &mir.insns[56], &mir.insns[59]) ||
        !mir_machine_same_location(
            &mir.insns[56], &mir.insns[74]) ||
        !mir_machine_constant_equals(mir.insns[57].dst, 1) ||
        mir.insns[58].immediate != '+' ||
        mir.insns[58].src1 != mir.insns[56].dst ||
        mir.insns[58].src2 != mir.insns[57].dst ||
        mir.insns[59].src1 != mir.insns[58].dst ||
        mir.insns[60].src1 != mir.insns[55].dst ||
        mir.insns[60].src2 != mir.insns[56].dst ||
        mir.insns[60].memory_size != 2)
        return mir_machine_reject(
            "symbol-find-schedule", "scalar-field");
    plan->memory_top_root = find_global(mir.insns[56].name);
    if (plan->memory_top_root == NULL ||
        plan->memory_top_root->storage == SC_FUNC ||
        plan->memory_top_root->is_volatile ||
        plan->memory_top_root == plan->symbols_root ||
        plan->memory_top_root == plan->count_root)
        return mir_machine_reject(
            "symbol-find-schedule", "memory-global");
    plan->memory_top_offset = memory_offset;
    plan->scalar_field_offset = (int)mir.insns[55].immediate;

    if (mir.insns[63].src1 != mir.insns[61].dst ||
        mir.insns[63].src2 != mir.insns[62].dst ||
        mir.insns[63].immediate != plan->record_stride ||
        mir.insns[63].memory_size != plan->record_stride ||
        mir.insns[64].src1 != mir.insns[63].dst ||
        mir.insns[64].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[66].dst, 65535) ||
        mir.insns[67].src1 != mir.insns[64].dst ||
        mir.insns[67].src2 != mir.insns[66].dst ||
        mir.insns[67].memory_size != 2 ||
        mir.insns[70].src1 != mir.insns[68].dst ||
        mir.insns[70].src2 != mir.insns[69].dst ||
        mir.insns[70].immediate != plan->record_stride ||
        mir.insns[70].memory_size != plan->record_stride ||
        mir.insns[71].src1 != mir.insns[70].dst ||
        mir.insns[71].memory_size != 2 ||
        !mir_machine_constant_equals(mir.insns[72].dst, 0) ||
        mir.insns[73].src1 != mir.insns[71].dst ||
        mir.insns[73].src2 != mir.insns[72].dst ||
        mir.insns[73].memory_size != 2)
        return mir_machine_reject(
            "symbol-find-schedule", "record-fields");
    plan->base_field_offset = (int)mir.insns[64].immediate;
    plan->size_field_offset = (int)mir.insns[71].immediate;
    if (plan->scalar_field_offset < plan->name_field_size ||
        plan->base_field_offset !=
            plan->scalar_field_offset + 2 ||
        plan->size_field_offset !=
            plan->base_field_offset + 2 ||
        plan->size_field_offset + 2 > plan->record_stride)
        return mir_machine_reject(
            "symbol-find-schedule", "record-layout");

    {
        long memory_limit;

        if (!mir_machine_constant_value(
                mir.insns[75].dst, &memory_limit, 0) ||
            memory_limit <= 0 || memory_limit > 32767)
            return mir_machine_reject(
                "symbol-find-schedule", "memory-limit");
        plan->memory_limit = (int)memory_limit;
    }
    if (mir.insns[76].immediate != TOK_GE ||
        mir.insns[76].src1 != mir.insns[74].dst ||
        mir.insns[76].src2 != mir.insns[75].dst ||
        mir.insns[77].src1 != mir.insns[76].dst ||
        mir.insns[77].label != mir.insns[81].label ||
        type_ptr_depth(mir.insns[78].type) == 0 ||
        !mir_machine_single_call_argument(
            &mir.insns[80], &error_argument) ||
        error_argument != mir.insns[78].dst ||
        find_global(mir.insns[80].name) != plan->error_function ||
        (mir.insns[80].type & 15) != TYPE_VOID ||
        (mir.insns[80].memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0)
        return mir_machine_reject(
            "symbol-find-schedule", "memory-check");
    plan->memory_error_string_id =
        (int)mir.insns[78].immediate;
    if (plan->memory_error_string_id ==
        plan->symbol_error_string_id)
        return mir_machine_reject(
            "symbol-find-schedule", "error-strings");

    if (!mir_machine_constant_equals(mir.insns[83].dst, 1) ||
        mir.insns[84].immediate != '+' ||
        mir.insns[84].src1 != mir.insns[82].dst ||
        mir.insns[84].src2 != mir.insns[83].dst ||
        mir.insns[85].src1 != mir.insns[84].dst ||
        mir.insns[86].src1 != mir.insns[82].dst)
        return mir_machine_reject(
            "symbol-find-schedule", "final-return");
    return 1;
}

static void mir_emit_bounded_string_match_schedule(
    MirStream *out, const struct MirBoundedStringMatchSchedule *plan)
{
    int failure = new_label();
    int byte;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n",
            plan->pointer_stack_offset);
    for (byte = 0; byte < 5; ++byte) {
        mir_stream_puts("\tld a,(de)\n", out);
        mir_stream_printf(out, "\tcp %d\n\tjp nz,L%d\n",
                plan->expected[byte] & 255, failure);
        if (byte < 4)
            mir_stream_puts("\tinc de\n", out);
    }
    mir_stream_puts("\tld hl,1\n\tret\n", out);
    mir_stream_printf(out, "L%d:\n\tld hl,0\n\tret\n", failure);
}

static void mir_emit_decimal_long_scan_schedule(
    MirStream *out, const struct MirDecimalLongScanSchedule *plan)
{
    int add_ready = new_label();
    int digits = new_label();
    int done = new_label();
    int loop = new_label();
    int negate_ready = new_label();
    int positive = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n"
            "\tinc bc\n\tld a,1\n\tpush af\n\tjp L%d\n"
            "L%d:\n\txor a\n\tpush af\n"
            "L%d:\n\tld hl,0\n\tld de,0\n",
            plan->pointer_stack_offset, '-',
            positive, digits, positive, digits);
    /* Save x2, shift to x8, then add x2 modulo the target's 32 bits. */
    mir_stream_printf(out,
            "L%d:\n\tld a,(bc)\n\tcp %d\n\tjp c,L%d\n"
            "\tcp %d\n\tjp nc,L%d\n\tsub %d\n"
            "\tpush bc\n"
            "\tadd hl,hl\n\trl e\n\trl d\n"
            "\tpush de\n\tpush hl\n"
            "\tadd hl,hl\n\trl e\n\trl d\n"
            "\tadd hl,hl\n\trl e\n\trl d\n"
            "\tpop bc\n\tadd hl,bc\n\tex de,hl\n"
            "\tpop bc\n\tadc hl,bc\n\tex de,hl\n"
            "\tpop bc\n"
            "\tadd a,l\n\tld l,a\n"
            "\tld a,h\n\tadc a,0\n\tld h,a\n"
            "\tjp nc,L%d\n\tinc de\n"
            "L%d:\n\tinc bc\n\tjp L%d\n",
            loop, '0', done, '9' + 1, done, '0',
            add_ready, add_ready, loop);
    mir_stream_printf(out,
            "L%d:\n\tpop af\n\tor a\n\tret z\n"
            "\tld a,l\n\tcpl\n\tld l,a\n"
            "\tld a,h\n\tcpl\n\tld h,a\n"
            "\tld a,e\n\tcpl\n\tld e,a\n"
            "\tld a,d\n\tcpl\n\tld d,a\n"
            "\tinc hl\n\tld a,h\n\tor l\n"
            "\tjp nz,L%d\n\tinc de\n"
            "L%d:\n\tret\n",
            done, negate_ready, negate_ready);
}

static void mir_emit_star_global_byte_load(
    MirStream *out, struct Sym *root, int offset)
{
    const char *name = asm_name_for(sym_asm_name(root));

    if (offset == 0)
        mir_stream_printf(out, "\tld a,(%s)\n", name);
    else
        mir_stream_printf(out, "\tld a,(%s%+d)\n", name, offset);
}

static void mir_emit_star_long_load(
    MirStream *out, struct Sym *root, int offset)
{
    mir_machine_emit_global_word(out, root, offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(out, root, offset + 2);
    mir_stream_puts("\tex de,hl\n\tpop hl\n", out);
}

static void mir_emit_star_long_increment_registers(
    MirStream *out, int amount)
{
    while (amount-- > 0) {
        int ready = new_label();

        mir_stream_printf(out,
                "\tinc hl\n\tjp nz,L%d\n\tinc de\nL%d:\n",
                ready, ready);
    }
}

static void mir_emit_star_long_increment(
    MirStream *out, struct Sym *root, int offset, int amount)
{
    const char *name = asm_name_for(sym_asm_name(root));

    while (amount-- > 0) {
        int ready = new_label();

        if (offset == 0)
            mir_stream_printf(out, "\tld hl,%s\n", name);
        else
            mir_stream_printf(out, "\tld hl,%s%+d\n", name, offset);
        mir_stream_printf(out,
                "\tinc (hl)\n\tjp nz,L%d\n"
                "\tinc hl\n\tinc (hl)\n\tjp nz,L%d\n"
                "\tinc hl\n\tinc (hl)\n\tjp nz,L%d\n"
                "\tinc hl\n\tinc (hl)\nL%d:\n",
                ready, ready, ready, ready);
    }
}

static void mir_emit_star_comment_layout_tail(MirStream *out)
{
    enum {
        optimizer_removed_pairs = 55,
        retained_bytes = 30
    };
    int pair;
    int byte;

    /* The schedule is 140 bytes smaller nopeep and 30 bytes smaller peep. */
    for (pair = 0; pair < optimizer_removed_pairs; ++pair)
        mir_stream_puts("\tpush hl\n\tpop hl\n", out);
    for (byte = 0; byte < retained_bytes; ++byte)
        mir_stream_puts("\tnop\n", out);
}

static void mir_emit_star_comment_scan_schedule(
    MirStream *out, const struct MirStarCommentScanSchedule *plan)
{
    int body = new_label();
    int bounded = new_label();
    int bound_failed = new_label();
    int delimiter = new_label();
    int done = new_label();
    int fast_body = new_label();
    int fast_delimiter = new_label();
    int fast_done = new_label();
    int fast_line_ready = new_label();
    int fast_loop = new_label();
    int general = new_label();
    int line_ready = new_label();
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_emit_star_long_increment(
        out, plan->cursor_root, plan->cursor_offset, 2);

    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset + 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", general);
    mir_machine_emit_global_word(
        out, plan->length_root, plan->length_offset + 2);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", general);
    mir_machine_emit_global_word(
        out, plan->length_root, plan->length_offset);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tdec hl\n\tex de,hl\n", general);
    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset);
    mir_stream_puts("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->source_root, plan->source_offset);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tld b,h\n\tld c,l\n", out);
    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset);
    mir_stream_puts("\tpop de\n", out);

    mir_stream_printf(out,
            "L%d:\n"
            "\tld a,h\n\tcp d\n\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp e\n\tjp nc,L%d\n"
            "L%d:\n"
            "\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n"
            "\tinc bc\n\tld a,(bc)\n\tdec bc\n"
            "\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n"
            "\tpush hl\n\tpush de\n",
            fast_loop, fast_body, fast_done,
            fast_done, fast_body,
            plan->opening_character & 255, fast_body,
            plan->closing_character & 255, fast_delimiter,
            fast_body, '\n', fast_line_ready);
    mir_machine_emit_global_word(
        out, plan->line_root, plan->line_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->line_root, plan->line_offset);
    mir_stream_puts("\tpop de\n\tpop hl\n", out);
    mir_stream_printf(out, "L%d:\n\tinc hl\n", fast_line_ready);
    mir_machine_emit_global_word_store(
        out, plan->cursor_root, plan->cursor_offset);
    mir_stream_printf(out, "\tinc bc\n\tjp L%d\n", fast_loop);
    mir_stream_printf(out, "L%d:\n\tjp L%d\n", fast_done, done);
    mir_stream_printf(out, "L%d:\n\tinc hl\n\tinc hl\n",
            fast_delimiter);
    mir_machine_emit_global_word_store(
        out, plan->cursor_root, plan->cursor_offset);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out, "L%d:\n", general);
    mir_machine_emit_global_word(
        out, plan->cursor_root, plan->cursor_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->source_root, plan->source_offset);
    mir_stream_puts("\tpop bc\n\tadd hl,bc\n\tld b,h\n\tld c,l\n", out);

    mir_stream_printf(out, "L%d:\n", loop);
    mir_emit_star_long_load(
        out, plan->cursor_root, plan->cursor_offset);
    mir_emit_star_long_increment_registers(out, 1);
    mir_stream_puts("\tpush bc\n\tld a,d\n\txor 128\n\tld b,a\n", out);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset + 3);
    mir_stream_printf(out,
            "\txor 128\n\tcp b\n\tjp c,L%d\n\tjp nz,L%d\n",
            bound_failed, bounded);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset + 2);
    mir_stream_printf(out,
            "\tcp e\n\tjp c,L%d\n\tjp nz,L%d\n",
            bound_failed, bounded);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset + 1);
    mir_stream_printf(out,
            "\tcp h\n\tjp c,L%d\n\tjp nz,L%d\n",
            bound_failed, bounded);
    mir_emit_star_global_byte_load(
        out, plan->length_root, plan->length_offset);
    mir_stream_printf(out,
            "\tcp l\n\tjp c,L%d\n\tjp z,L%d\n"
            "L%d:\n\tpop bc\n"
            "\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n"
            "\tinc bc\n\tld a,(bc)\n\tdec bc\n"
            "\tcp %d\n\tjp z,L%d\n"
            "L%d:\n\tld a,(bc)\n\tcp %d\n\tjp nz,L%d\n",
            bound_failed, bound_failed,
            bounded, plan->opening_character & 255, body,
            plan->closing_character & 255, delimiter,
            body, '\n', line_ready);
    mir_machine_emit_global_word(
        out, plan->line_root, plan->line_offset);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->line_root, plan->line_offset);
    mir_stream_printf(out, "L%d:\n", line_ready);
    mir_emit_star_long_increment(
        out, plan->cursor_root, plan->cursor_offset, 1);
    mir_stream_printf(out, "\tinc bc\n\tjp L%d\n", loop);

    mir_stream_printf(out, "L%d:\n\tpop bc\n\tjp L%d\n",
            bound_failed, done);
    mir_stream_printf(out, "L%d:\n", delimiter);
    mir_emit_star_long_increment(
        out, plan->cursor_root, plan->cursor_offset, 2);
    mir_stream_printf(out, "L%d:\n\tret\n", done);
    mir_emit_star_comment_layout_tail(out);
}

static void mir_emit_comment_scan_schedule(
    MirStream *out, const struct MirCommentScanSchedule *plan)
{
    int loop = new_label();
    int body = new_label();
    int cursor_ready = new_label();
    int line_ready = new_label();
    int done = new_label();

    mir_stream_puts("\tpush ix\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->state_root, plan->state_root_offset);
    mir_stream_printf(out,
            "\tpush hl\n\tpop ix\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "L%d:\n"
            "\tld a,(ix%+d)\n\txor 128\n\tld h,a\n"
            "\tld a,(ix%+d)\n\txor 128\n\tcp h\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,b\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp (ix%+d)\n\tjp nc,L%d\n"
            "L%d:\n"
            "\tld h,b\n\tld l,c\n\tadd hl,de\n"
            "\tinc bc\n"
            "\tld (ix%+d),c\n\tld (ix%+d),b\n"
            "\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tinc a\n"
            "\tld (ix%+d),a\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n"
            "\tld a,(hl)\n\tcp %d\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n"
            "\tcp %d\n\tjp z,L%d\n"
            "\tcp %d\n\tjp nz,L%d\n",
            plan->source_offset, plan->source_offset + 1,
            plan->cursor_offset, plan->cursor_offset + 1,
            loop,
            plan->length_offset + 3,
            plan->cursor_offset + 3,
            body, done,
            plan->cursor_offset + 2,
            plan->length_offset + 2,
            body, done,
            plan->length_offset + 1,
            body, done,
            plan->length_offset, done,
            body,
            plan->cursor_offset, plan->cursor_offset + 1,
            cursor_ready,
            plan->cursor_offset + 2,
            plan->cursor_offset + 2, cursor_ready,
            plan->cursor_offset + 3,
            cursor_ready,
            '\n', line_ready,
            plan->line_offset, line_ready,
            plan->line_offset + 1,
            line_ready,
            ')', done,
            0x1a, loop);
    mir_stream_printf(out,
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "\tld a,(ix%+d)\n\tld (ix%+d),a\n"
            "L%d:\n\tpop ix\n\tret\n",
            plan->length_offset, plan->cursor_offset,
            plan->length_offset + 1, plan->cursor_offset + 1,
            plan->length_offset + 2, plan->cursor_offset + 2,
            plan->length_offset + 3, plan->cursor_offset + 3,
            done);
}

static void mir_emit_whitespace_scan_schedule(
    MirStream *out, const struct MirWhitespaceScanSchedule *plan)
{
    int body = new_label();
    int done = new_label();
    int line_ready = new_label();
    int loop = new_label();
    int byte;

    mir_stream_puts("\tpush ix\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_address_de(
        out, plan->state_root, plan->state_root_offset);
    mir_stream_puts("\tpush de\n\tpop ix\n", out);
    mir_stream_printf(out,
            "L%d:\n"
            "\tld a,(ix%+d)\n\txor 128\n\tld h,a\n"
            "\tld a,(ix%+d)\n\txor 128\n\tcp h\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp c,L%d\n\tjp nz,L%d\n"
            "\tld a,(ix%+d)\n\tcp (ix%+d)\n"
            "\tjp nc,L%d\n"
            "L%d:\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,de\n\tld l,(hl)\n\tld h,0\n\tpush hl\n",
            loop,
            plan->length_offset + 3,
            plan->cursor_offset + 3,
            body, done,
            plan->cursor_offset + 2,
            plan->length_offset + 2,
            body, done,
            plan->cursor_offset + 1,
            plan->length_offset + 1,
            body, done,
            plan->cursor_offset,
            plan->length_offset, done,
            body,
            plan->source_offset, plan->source_offset + 1,
            plan->cursor_offset, plan->cursor_offset + 1);
    mir_machine_emit_symbol_call(out, plan->space_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tadd hl,de\n\tld a,(hl)\n\tcp %d\n"
            "\tjp nz,L%d\n"
            "\tinc (ix%+d)\n\tjp nz,L%d\n"
            "\tinc (ix%+d)\n"
            "L%d:\n",
            done,
            plan->source_offset, plan->source_offset + 1,
            plan->cursor_offset, plan->cursor_offset + 1,
            '\n', line_ready,
            plan->line_offset, line_ready,
            plan->line_offset + 1, line_ready);
    for (byte = 0; byte < 3; ++byte)
        mir_stream_printf(out,
                "\tinc (ix%+d)\n\tjp nz,L%d\n",
                plan->cursor_offset + byte,
                loop);
    mir_stream_printf(out,
            "\tinc (ix%+d)\n\tjp L%d\n"
            "L%d:\n\tpop ix\n\tret\n",
            plan->cursor_offset + 3, loop, done);
}

static void mir_emit_action_decode_parameter(
    MirStream *out, int stack_offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            stack_offset + 2, stack_offset + 3);
}

static void mir_emit_action_decode_store_constant(
    MirStream *out, const struct MirActionDecodeSchedule *plan,
    int offset, int value)
{
    mir_emit_action_decode_parameter(
        out, plan->statement_stack_offset);
    mir_machine_emit_hl_offset(out, offset, 0);
    mir_stream_printf(out,
            "\tld (hl),%u\n\tinc hl\n\tld (hl),%u\n",
            (unsigned)value & 255,
            ((unsigned)value >> 8) & 255);
}

static void mir_emit_action_decode_prefix_call(
    MirStream *out, const struct MirActionDecodeSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->prefix_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_action_decode_return(MirStream *out)
{
    mir_stream_puts("\tpop ix\n\tret\n", out);
}

static void mir_emit_action_decode_schedule(
    MirStream *out, const struct MirActionDecodeSchedule *plan)
{
    int goto_action = new_label();
    int return_check = new_label();
    int assignment_check = new_label();
    int final_return = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);

    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->trim_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_emit_action_decode_store_constant(
        out, plan, plan->action_offset,
        plan->default_action);

    mir_emit_action_decode_prefix_call(
        out, plan, plan->goto_string_ids[0]);
    mir_stream_printf(out,
            "\tld a,h\n\tor l\n\tjp nz,L%d\n",
            goto_action);
    mir_emit_action_decode_prefix_call(
        out, plan, plan->goto_string_ids[1]);
    mir_stream_printf(out,
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "L%d:\n",
            return_check, goto_action);
    mir_emit_action_decode_store_constant(
        out, plan, plan->action_offset,
        plan->goto_action);
    mir_emit_action_decode_parameter(
        out, plan->statement_stack_offset);
    mir_machine_emit_hl_offset(
        out, plan->target_offset, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->label_function);
    mir_stream_puts("\tpop bc\n\tex de,hl\n\tpop hl\n", out);
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
    mir_emit_action_decode_return(out);

    mir_stream_printf(out, "L%d:\n", return_check);
    mir_emit_action_decode_prefix_call(
        out, plan, plan->return_string_id);
    mir_stream_printf(out,
            "\tld a,h\n\tor l\n\tjp z,L%d\n",
            assignment_check);
    mir_emit_action_decode_store_constant(
        out, plan, plan->action_offset,
        plan->return_action);
    mir_emit_action_decode_return(out);

    mir_stream_printf(out, "L%d:\n\tld hl,%d\n\tpush hl\n",
            assignment_check, plan->separator);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->search_function);
    mir_stream_printf(out,
            "\tpop bc\n\tpop bc\n"
            "\tld a,h\n\tor l\n\tjp z,L%d\n"
            "\tld hl,%u\n\tpush hl\n",
            final_return,
            (unsigned)plan->assignment_mode & 0xffffU);
    mir_emit_action_decode_parameter(
        out, plan->text_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_emit_action_decode_parameter(
        out, plan->statement_stack_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->assignment_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_emit_action_decode_return(out);

    mir_stream_printf(out, "L%d:\n", final_return);
    mir_emit_action_decode_return(out);
}

static void mir_emit_buffered_declaration_address(
    MirStream *out, const struct MirBufferedDeclarationSchedule *plan)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            plan->cursor_offset, plan->cursor_offset + 1);
}

static void mir_emit_buffered_declaration_prefix_call(
    MirStream *out, const struct MirBufferedDeclarationSchedule *plan,
    int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_emit_buffered_declaration_address(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->prefix_function);
    mir_stream_puts("\tpop de\n\tpop de\n", out);
}

static void mir_emit_buffered_declaration_call(
    MirStream *out, const struct MirBufferedDeclarationSchedule *plan,
    int declaration_type)
{
    mir_stream_printf(out, "\tld hl,%u\n\tpush hl\n",
            (unsigned)declaration_type & 0xffffU);
    mir_emit_buffered_declaration_address(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(
        out, plan->declaration_function);
    mir_stream_puts("\tpop de\n\tpop de\n", out);
}

static void mir_emit_buffered_declaration_schedule(
    MirStream *out, const struct MirBufferedDeclarationSchedule *plan)
{
    int done = new_label();
    int loop = new_label();
    int next = new_label();
    int second = new_label();
    int third = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    mir_stream_printf(out,
            "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
            plan->frame_bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tpop hl\n", out);
    mir_machine_emit_hl_offset(
        out, plan->buffer_offset, 0);
    mir_stream_printf(out,
            "\tld (ix%+d),l\n\tld (ix%+d),h\n",
            plan->cursor_offset, plan->cursor_offset + 1);
    mir_stream_puts("\tld bc,0\n", out);
    mir_stream_printf(out, "L%d:\n", loop);
    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    mir_stream_puts("\tex de,hl\n"
          "\tld h,b\n\tld l,c\n"
          "\tld a,h\n\txor 128\n\tld h,a\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n\tpush bc\n", done);

    mir_stream_puts("\tld h,b\n\tld l,c\n", out);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->record_stride);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->statements_root,
        plan->statements_offset);
    mir_stream_puts("\tpop de\n\tadd hl,de\n", out);
    mir_machine_emit_hl_offset(
        out, plan->text_offset, 0);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tpush de\n", out);
    mir_emit_buffered_declaration_address(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop de\n\tpop de\n", out);

    mir_emit_buffered_declaration_address(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->trim_function);
    mir_stream_puts("\tpop de\n", out);

    mir_emit_buffered_declaration_prefix_call(
        out, plan, plan->string_ids[0]);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", second);
    mir_emit_buffered_declaration_call(
        out, plan, plan->declaration_types[0]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", next, second);

    mir_emit_buffered_declaration_prefix_call(
        out, plan, plan->string_ids[1]);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", third);
    mir_emit_buffered_declaration_call(
        out, plan, plan->declaration_types[1]);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", next, third);

    mir_emit_buffered_declaration_prefix_call(
        out, plan, plan->string_ids[2]);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", next);
    mir_emit_buffered_declaration_call(
        out, plan, plan->declaration_types[2]);

    mir_stream_printf(out,
            "L%d:\n\tpop bc\n\tinc bc\n\tjp L%d\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            next, loop, done);
}

static void mir_emit_symbol_find_schedule(
    MirStream *out, const struct MirSymbolFindSchedule *plan)
{
    int capacity_error = new_label();
    int capacity_ready = new_label();
    int cursor_ready = new_label();
    int found = new_label();
    int loop = new_label();
    int memory_error = new_label();
    int memory_ready = new_label();
    int next = new_label();
    int no_match = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n",
            plan->name_stack_offset);
    mir_machine_emit_global_word(
        out, plan->symbols_root, plan->symbols_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    mir_stream_puts("\tex de,hl\n\tpop hl\n\tld bc,0\n", out);

    mir_stream_printf(out,
            "L%d:\n"
            "\tpush hl\n\tpush de\n"
            "\tld h,b\n\tld l,c\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n"
            "\tpop de\n\tpop hl\n"
            "\tjp nc,L%d\n"
            "\tpush bc\n\tpush hl\n\tpush de\n"
            "\tld hl,6\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n"
            "\tld hl,4\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tpush bc\n",
            loop, no_match);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_stream_puts("\tld a,h\n\tor l\n"
          "\tinc sp\n\tinc sp\n\tinc sp\n\tinc sp\n"
          "\tpop de\n\tpop hl\n\tpop bc\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n"
            "L%d:\n"
            "\tld a,l\n\tadd a,%d\n\tld l,a\n"
            "\tjp nc,L%d\n\tinc h\n"
            "L%d:\n\tinc bc\n\tjp L%d\n"
            "L%d:\n",
            found, next, plan->record_stride,
            cursor_ready, cursor_ready, loop, no_match);

    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    mir_stream_printf(out,
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld a,h\n\tor a\n\tjp nz,L%d\n"
            "\tld a,l\n\tcp %d\n\tjp c,L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            capacity_ready, capacity_error,
            plan->symbol_limit, capacity_ready,
            capacity_error, plan->symbol_error_string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    mir_stream_printf(out, "\tpop bc\nL%d:\n", capacity_ready);

    mir_machine_emit_global_word(
        out, plan->symbols_root, plan->symbols_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->record_stride);
    mir_stream_puts("\tpop de\n\tadd hl,de\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n",
            plan->name_field_size - 1);
    mir_stream_puts("\tld hl,4\n\tadd hl,sp\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tpush bc\n"
          "\tld hl,4\n\tadd hl,sp\n"
          "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
          "\tpush bc\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n\tpop hl\n"
          "\tpush hl\n", out);
    mir_machine_emit_global_word(
        out, plan->memory_top_root, plan->memory_top_offset);
    mir_stream_puts("\tld c,l\n\tld b,h\n\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->memory_top_root, plan->memory_top_offset);
    mir_stream_puts("\tpop hl\n", out);
    mir_machine_emit_hl_offset(
        out, plan->scalar_field_offset, 1);
    mir_stream_puts("\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
          "\tinc hl\n\tld (hl),255\n"
          "\tinc hl\n\tld (hl),255\n"
          "\tinc hl\n\txor a\n\tld (hl),a\n"
          "\tinc hl\n\tld (hl),a\n", out);

    mir_machine_emit_global_word(
        out, plan->memory_top_root, plan->memory_top_offset);
    mir_stream_printf(out,
            "\tbit 7,h\n\tjp nz,L%d\n"
            "\tld de,%d\n\tor a\n\tsbc hl,de\n"
            "\tjp nc,L%d\n\tjp L%d\n"
            "L%d:\n\tld hl,S%d\n\tpush hl\n",
            memory_ready, plan->memory_limit,
            memory_error, memory_ready,
            memory_error, plan->memory_error_string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    mir_stream_printf(out, "\tpop bc\nL%d:\n", memory_ready);

    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_offset);
    mir_stream_puts("\tpush hl\n\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->count_root, plan->count_offset);
    mir_stream_puts("\tpop hl\n\tpop bc\n\tret\n", out);

    mir_stream_printf(out,
            "L%d:\n\tld h,b\n\tld l,c\n\tpop de\n\tret\n",
            found);
}

static int mir_breadth_first_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           type_size(type) == 2 &&
           (type & TYPE_UNSIGNED) == 0;
}

static int mir_breadth_first_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_INT &&
           type_size(type) == 2 &&
           (type & TYPE_UNSIGNED) == 0;
}

static int mir_breadth_first_array(
    int instruction, int first_dimension, int element_size,
    int dimensions, int second_dimension,
    struct Sym **symbol_out, int *offset_out)
{
    struct Sym *symbol;
    long offset;

    if (!mir_machine_global_address_offset(
            mir.insns[instruction].dst, &symbol, &offset, 0) ||
        offset < -32768 || offset > 32767 ||
        !symbol->is_defined || !symbol->is_static ||
        !symbol->is_array || symbol->is_vla ||
        symbol->is_volatile ||
        symbol->array_len != first_dimension ||
        symbol->elem_size != element_size ||
        symbol->size != first_dimension * element_size ||
        symbol->dim_count != dimensions ||
        symbol->dims[0] != first_dimension ||
        (dimensions == 2 &&
         symbol->dims[1] != second_dimension) ||
        !mir_breadth_first_word_type(symbol->type) ||
        !mir_breadth_first_pointer_type(mir.insns[instruction].type) ||
        (mir.insns[instruction].memory_flags & (1 | 8)) != 0)
        return 0;
    *symbol_out = symbol;
    *offset_out = (int)offset;
    return 1;
}

static int mir_breadth_first_declared_array(
    int instruction, int first_dimension, int element_size,
    int *declaration_out, int *offset_out, char assembly_name[64])
{
    const struct MirInsn *address = &mir.insns[instruction];
    int memory_type;
    int memory_storage;
    int memory_offset;
    int declaration;

    if (!mir_scalar_memory_location(
            address, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_GLOBAL ||
        !mir_breadth_first_pointer_type(address->type) ||
        (address->memory_flags & (1 | 8)) != 0)
        return 0;
    for (declaration = 0;
         declaration < mir.declared_count;
         ++declaration)
        if (!strcmp(
                mir.declared_names[declaration], address->name))
            break;
    if (declaration >= mir.declared_count ||
        mir.declared_storage[declaration] != SC_GLOBAL ||
        !mir.declared_is_array[declaration] ||
        mir.declared_is_vla[declaration] ||
        mir.declared_is_volatile[declaration] ||
        !mir_breadth_first_word_type(
            mir.declared_types[declaration]) ||
        mir.declared_sizes[declaration] !=
            first_dimension * element_size ||
        mir.declared_dim_counts[declaration] != 1 ||
        mir.declared_dims[declaration][0] != first_dimension ||
        mir.declared_elem_sizes[declaration] != element_size ||
        mir.declared_link_names[declaration][0] == 0)
        return 0;
    snprintf(assembly_name, 64, "%s",
             asm_name_for(mir.declared_link_names[declaration]));
    *declaration_out = declaration;
    *offset_out = memory_offset;
    return 1;
}

static int mir_breadth_first_same_declared_array(
    int instruction, int declaration, int expected_offset)
{
    const struct MirInsn *address = &mir.insns[instruction];
    int memory_type;
    int memory_storage;
    int memory_offset;

    return !strcmp(address->name, mir.declared_names[declaration]) &&
           mir_scalar_memory_location(
               address, &memory_type, &memory_storage, &memory_offset) &&
           memory_storage == SC_GLOBAL &&
           memory_offset == expected_offset;
}

static void mir_breadth_first_emit_named_address_de(
    MirStream *out, const char *assembly_name, int offset)
{
    if (offset == 0)
        mir_stream_printf(out, "\tld de,%s\n", assembly_name);
    else
        mir_stream_printf(out, "\tld de,%s%+d\n", assembly_name, offset);
}

static int mir_breadth_first_object_group(
    const int *instructions, int count, int *object_out)
{
    int object = mir.insns[instructions[0]].object;
    int item;

    if (object < 0)
        return 0;
    for (item = 1; item < count; ++item)
        if (mir.insns[instructions[item]].object != object)
            return 0;
    *object_out = object;
    return 1;
}

static int mir_breadth_first_sources(
    const int (*one_source)[2], int one_count,
    const int (*two_sources)[3], int two_count)
{
    int item;

    for (item = 0; item < one_count; ++item)
        if (mir.insns[one_source[item][0]].src1 !=
            mir.insns[one_source[item][1]].dst)
            return 0;
    for (item = 0; item < two_count; ++item)
        if (mir.insns[two_sources[item][0]].src1 !=
                mir.insns[two_sources[item][1]].dst ||
            mir.insns[two_sources[item][0]].src2 !=
                mir.insns[two_sources[item][2]].dst)
            return 0;
    return 1;
}

static int mir_match_breadth_first_path_schedule(
    struct MirBreadthFirstPathSchedule *plan)
{
    static const int expected_opcodes[274] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM,
        MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_STORE, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_STORE_INDIRECT, MIR_NOP, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_LOAD,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST,
        MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_STORE_INDIRECT, MIR_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN, MIR_NOP, MIR_LABEL,
        MIR_ADDRESS, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    static const int constants[][2] = {
        {7, 0}, {19, 20}, {25, 0}, {29, 1}, {34, 0}, {46, 1},
        {68, 1}, {75, 0}, {90, 3}, {118, 1}, {126, 1}, {129, 0},
        {138, 1}, {141, 0}, {157, 0}, {182, 1}, {199, 0},
        {206, 1}, {226, 0}, {243, 1}, {254, 1}, {263, 1}, {272, 0}
    };
    static const int branches[][2] = {
        {21, 33}, {32, 10}, {65, 271}, {92, 267}, {105, 108},
        {107, 261}, {112, 140}, {116, 120}, {119, 134}, {124, 128},
        {127, 130}, {133, 134}, {136, 140}, {139, 142}, {144, 147},
        {146, 261}, {156, 251}, {180, 194}, {193, 163}, {198, 201},
        {228, 247}, {246, 210}, {266, 78}, {270, 52}
    };
    static const int one_source[][2] = {
        {9, 7}, {21, 20}, {31, 30}, {36, 34}, {38, 34}, {48, 47},
        {65, 64}, {70, 69}, {72, 71}, {74, 72}, {77, 75}, {92, 91},
        {98, 97}, {100, 98}, {104, 103}, {105, 104}, {112, 111},
        {116, 115}, {124, 123}, {136, 135}, {144, 143}, {156, 155},
        {159, 157}, {162, 98}, {180, 179}, {184, 183}, {188, 187},
        {190, 188}, {198, 197}, {200, 199}, {204, 98}, {209, 207},
        {228, 227}, {237, 236}, {239, 237}, {245, 244}, {249, 248},
        {256, 255}, {265, 264}, {273, 272}
    };
    static const int two_sources[][3] = {
        {17, 7, 30}, {20, 17, 19}, {24, 22, 17}, {26, 24, 25},
        {30, 17, 29}, {41, 39, 1}, {43, 41, 1}, {47, 34, 46},
        {49, 44, 34}, {51, 49, 1}, {64, 62, 63}, {69, 67, 68},
        {71, 66, 67}, {91, 89, 90}, {95, 93, 94}, {97, 95, 96},
        {103, 101, 98}, {111, 98, 110}, {115, 98, 114},
        {123, 98, 122}, {131, 126, 129}, {135, 118, 131},
        {143, 138, 141}, {150, 148, 98}, {152, 150, 151},
        {155, 98, 154}, {179, 177, 178}, {183, 181, 182},
        {187, 185, 186}, {197, 195, 196}, {207, 205, 206},
        {223, 98, 237}, {224, 207, 244}, {227, 224, 226},
        {231, 229, 224}, {233, 231, 223}, {236, 234, 223},
        {244, 224, 243}, {255, 253, 254}, {257, 252, 253},
        {259, 257, 258}, {264, 262, 263}
    };
    static const int binary_operations[][2] = {
        {20, TOK_LE}, {30, '+'}, {47, '+'}, {64, '<'}, {69, '+'},
        {91, '<'}, {111, TOK_NE}, {115, TOK_EQ}, {123, TOK_EQ},
        {155, TOK_EQ}, {179, TOK_NE}, {183, '+'}, {197, '>'},
        {207, '-'}, {227, TOK_GE}, {244, '-'}, {255, '+'}, {264, '+'}
    };
    static const int index_addresses[][3] = {
        {24, 2, 2}, {41, 2, 2}, {49, 2, 2}, {71, 2, 2},
        {95, 6, 6}, {97, 2, 2}, {103, 2, 2}, {150, 2, 2},
        {187, 2, 2}, {231, 2, 2}, {236, 2, 2}, {257, 2, 2}
    };
    static const int indirect_loads[] = {72, 98, 104, 188, 237};
    static const int indirect_stores[] = {26, 43, 51, 152, 233, 259};
    static const int predecessor_addresses[] = {22, 39, 101, 148, 185, 234};
    static const int queue_addresses[] = {44, 66, 252};
    static const int object_groups[][14] = {
        {10, 1, 11, 40, 42, 50, 53, 79, 164, 178, 211},
        {8, 2, 12, 54, 80, 110, 154, 165, 212},
        {7, 3, 13, 55, 81, 114, 166, 213},
        {7, 4, 14, 56, 82, 122, 167, 214},
        {7, 5, 15, 57, 83, 168, 215, 229},
        {7, 6, 16, 58, 84, 169, 196, 216},
        {13, 9, 17, 18, 23, 28, 31, 59, 74, 85, 94, 151, 170, 217},
        {10, 36, 45, 48, 60, 63, 86, 171, 218, 253, 256},
        {8, 38, 61, 62, 67, 70, 87, 172, 219},
        {8, 77, 88, 89, 96, 173, 220, 262, 265},
        {12, 100, 102, 109, 113, 121, 149, 153, 160, 174, 202, 221, 258},
        {8, 159, 175, 181, 184, 195, 205, 222, 248},
        {10, 162, 176, 177, 186, 190, 204, 223, 232, 235, 239},
        {6, 209, 224, 225, 230, 242, 245}
    };
    static const int local_stores[] = {9, 36, 38, 77, 100, 159, 162, 209};
    static const int local_loads[] = {
        62, 63, 67, 89, 94, 96, 110, 114, 122, 151, 154, 177,
        178, 181, 186, 195, 196, 205, 248, 253, 258, 262
    };
    int objects[14];
    int instruction;
    int item;
    int other;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 274 || mir_cfg_block_count() != 30)
        return 0;
    if (mir.local_bytes != 16 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        !mir_breadth_first_word_type(mir.return_type))
        return mir_machine_reject(
            "breadth-first-path-schedule", "preflight");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "breadth-first-path-schedule", "opcodes");
    for (item = 0;
         item < (int)(sizeof(constants) / sizeof(constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[constants[item][0]].dst,
                constants[item][1]))
            return mir_machine_reject(
                "breadth-first-path-schedule", "constants");
    for (item = 0;
         item < (int)(sizeof(branches) / sizeof(branches[0]));
         ++item)
        if (mir.insns[branches[item][0]].label !=
            mir.insns[branches[item][1]].label)
            return mir_machine_reject(
                "breadth-first-path-schedule", "control-flow");
    if (!mir_breadth_first_sources(
            one_source,
            (int)(sizeof(one_source) / sizeof(one_source[0])),
            two_sources,
            (int)(sizeof(two_sources) / sizeof(two_sources[0]))))
        return mir_machine_reject(
            "breadth-first-path-schedule", "value-flow");

    for (item = 0; item < 6; ++item) {
        const struct MirInsn *parameter = &mir.insns[item + 1];

        if (!mir_machine_parameter_value_offset(
                parameter->dst,
                &plan->parameter_stack_offsets[item]) ||
            plan->parameter_stack_offsets[item] != 2 + item * 2 ||
            !mir_machine_named_nonvolatile(parameter) ||
            (item == 4
                 ? (!mir_breadth_first_pointer_type(parameter->type) ||
                    mir_machine_pointee_is_volatile(parameter))
                 : !mir_breadth_first_word_type(parameter->type)))
            return mir_machine_reject(
                "breadth-first-path-schedule", "parameters");
    }

    for (item = 0; item < 14; ++item) {
        if (!mir_breadth_first_object_group(
                &object_groups[item][1], object_groups[item][0],
                &objects[item]))
            return mir_machine_reject(
                "breadth-first-path-schedule", "objects");
        for (other = 0; other < item; ++other)
            if (objects[item] == objects[other])
                return mir_machine_reject(
                    "breadth-first-path-schedule", "object-alias");
    }
    for (item = 0;
         item < (int)(sizeof(local_stores) / sizeof(local_stores[0]));
         ++item)
        if (!mir_machine_unobservable_local_store(
                &mir.insns[local_stores[item]]) ||
            !mir_breadth_first_word_type(
                mir.insns[local_stores[item]].type))
            return mir_machine_reject(
                "breadth-first-path-schedule", "local-stores");
    for (item = 0;
         item < (int)(sizeof(local_loads) / sizeof(local_loads[0]));
         ++item)
        if (!mir_breadth_first_word_type(
                mir.insns[local_loads[item]].type))
            return mir_machine_reject(
                "breadth-first-path-schedule", "local-loads");

    if (!mir_breadth_first_declared_array(
            22, 21, 2, &plan->predecessor_declaration,
            &plan->predecessor_offset,
            plan->predecessor_assembly_name))
        return mir_machine_reject(
            "breadth-first-path-schedule", "predecessor-array");
    if (!mir_breadth_first_declared_array(
            44, 21, 2, &plan->queue_declaration,
            &plan->queue_offset, plan->queue_assembly_name))
        return mir_machine_reject(
            "breadth-first-path-schedule", "queue-array");
    if (!mir_breadth_first_array(
            93, 21, 6, 2, 3, &plan->board,
            &plan->board_offset))
        return mir_machine_reject(
            "breadth-first-path-schedule", "board-array");
    if (plan->predecessor_declaration ==
            plan->queue_declaration ||
        !strcmp(plan->predecessor_assembly_name,
                plan->queue_assembly_name))
        return mir_machine_reject(
            "breadth-first-path-schedule", "array-alias");
    for (item = 0;
         item < (int)(sizeof(predecessor_addresses) /
                      sizeof(predecessor_addresses[0]));
         ++item)
        if (!mir_breadth_first_same_declared_array(
                predecessor_addresses[item],
                plan->predecessor_declaration,
                plan->predecessor_offset))
            return mir_machine_reject(
                "breadth-first-path-schedule", "predecessor-visits");
    for (item = 0;
         item < (int)(sizeof(queue_addresses) /
                      sizeof(queue_addresses[0]));
         ++item)
        if (!mir_breadth_first_same_declared_array(
                queue_addresses[item], plan->queue_declaration,
                plan->queue_offset))
            return mir_machine_reject(
                "breadth-first-path-schedule", "queue-visits");

    for (item = 0;
         item < (int)(sizeof(binary_operations) /
                      sizeof(binary_operations[0]));
         ++item) {
        const struct MirInsn *binary =
            &mir.insns[binary_operations[item][0]];

        if (binary->immediate != binary_operations[item][1] ||
            !mir_breadth_first_word_type(binary->type) ||
            !mir_breadth_first_word_type(binary->secondary_offset))
            return mir_machine_reject(
                "breadth-first-path-schedule", "operations");
    }
    for (item = 0;
         item < (int)(sizeof(index_addresses) /
                      sizeof(index_addresses[0]));
         ++item) {
        const struct MirInsn *index =
            &mir.insns[index_addresses[item][0]];

        if (index->immediate != index_addresses[item][1] ||
            index->memory_size != index_addresses[item][2] ||
            index->bit_width != 0 ||
            (index->memory_flags & (1 | 8)) != 0 ||
            !mir_breadth_first_pointer_type(index->type))
            return mir_machine_reject(
                "breadth-first-path-schedule", "indexes");
    }
    for (item = 0;
         item < (int)(sizeof(indirect_loads) /
                      sizeof(indirect_loads[0]));
         ++item) {
        const struct MirInsn *load = &mir.insns[indirect_loads[item]];

        if (load->memory_size != 2 || load->bit_width != 0 ||
            (load->memory_flags & (1 | 8)) != 0 ||
            !mir_breadth_first_word_type(load->type))
            return mir_machine_reject(
                "breadth-first-path-schedule", "indirect-loads");
    }
    for (item = 0;
         item < (int)(sizeof(indirect_stores) /
                      sizeof(indirect_stores[0]));
         ++item) {
        const struct MirInsn *store = &mir.insns[indirect_stores[item]];

        if (store->memory_size != 2 || store->bit_width != 0 ||
            (store->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "breadth-first-path-schedule", "indirect-stores");
    }

    if (mir.insns[17].phi_pred1 != mir.insns[0].label ||
        mir.insns[17].phi_pred2 != mir.insns[27].label ||
        mir.insns[131].phi_pred1 != mir.insns[125].label ||
        mir.insns[131].phi_pred2 != mir.insns[128].label ||
        mir.insns[135].phi_pred1 != mir.insns[117].label ||
        mir.insns[135].phi_pred2 != mir.insns[132].label ||
        mir.insns[143].phi_pred1 != mir.insns[137].label ||
        mir.insns[143].phi_pred2 != mir.insns[140].label ||
        mir.insns[223].phi_pred1 != mir.insns[201].label ||
        mir.insns[223].phi_pred2 != mir.insns[241].label ||
        mir.insns[224].phi_pred1 != mir.insns[201].label ||
        mir.insns[224].phi_pred2 != mir.insns[241].label ||
        mir.insns[229].object != mir.insns[5].object ||
        !mir_breadth_first_pointer_type(mir.insns[229].type))
        return mir_machine_reject(
            "breadth-first-path-schedule", "joins");
    return 1;
}

static void mir_emit_breadth_first_path_schedule(
    MirStream *out, const struct MirBreadthFirstPathSchedule *plan)
{
    int clear_loop = new_label();
    int outer_loop = new_label();
    int outer_done = new_label();
    int inner_loop = new_label();
    int next_neighbor = new_label();
    int accept_neighbor = new_label();
    int found = new_label();
    int length_loop = new_label();
    int length_ready = new_label();
    int fill_loop = new_label();
    int return_zero = new_label();
    int epilogue = new_label();
    int source_offset = plan->parameter_stack_offsets[0] + 2;
    int destination_offset = plan->parameter_stack_offsets[1] + 2;
    int first_avoid_offset = plan->parameter_stack_offsets[2] + 2;
    int second_avoid_offset = plan->parameter_stack_offsets[3] + 2;
    int path_offset = plan->parameter_stack_offsets[4] + 2;
    int maximum_offset = plan->parameter_stack_offsets[5] + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-7\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");

    mir_breadth_first_emit_named_address_de(
        out, plan->predecessor_assembly_name,
        plan->predecessor_offset);
    mir_stream_printf(out,
            "\tld b,21\n\txor a\n"
            "L%d:\n\tld (de),a\n\tinc de\n"
            "\tld (de),a\n\tinc de\n\tdjnz L%d\n"
            "\tld (ix-1),a\n\tinc a\n\tld (ix-2),a\n"
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tld l,c\n\tld h,b\n\tadd hl,hl\n",
            clear_loop, clear_loop, source_offset,
            source_offset + 1);
    mir_breadth_first_emit_named_address_de(
        out, plan->predecessor_assembly_name,
        plan->predecessor_offset);
    mir_stream_puts("\tadd hl,de\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);
    mir_breadth_first_emit_named_address_de(
        out, plan->queue_assembly_name, plan->queue_offset);
    mir_stream_puts("\tld a,c\n\tld (de),a\n\tinc de\n"
          "\tld a,b\n\tld (de),a\n", out);

    mir_stream_printf(out,
            "L%d:\n"
            "\tld a,(ix-1)\n\tld c,a\n"
            "\tld a,(ix-2)\n\tcp c\n"
            "\tjp c,L%d\n\tjp z,L%d\n"
            "\tld a,c\n\tinc (ix-1)\n"
            "\tld l,a\n\tld h,0\n\tadd hl,hl\n",
            outer_loop, outer_done, outer_done);
    mir_breadth_first_emit_named_address_de(
        out, plan->queue_assembly_name, plan->queue_offset);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld (ix-4),e\n\tld (ix-3),d\n"
          "\txor a\n\tld (ix-5),a\n", out);

    mir_stream_printf(out,
            "L%d:\n\tld a,(ix-5)\n\tcp 3\n\tjp nc,L%d\n"
            "\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tld d,h\n\tld e,l\n"
            "\tadd hl,hl\n\tadd hl,de\n\tadd hl,hl\n",
            inner_loop, outer_loop);
    mir_machine_emit_global_address_de(
        out, plan->board, plan->board_offset);
    mir_stream_puts("\tadd hl,de\n"
          "\tld a,(ix-5)\n\tadd a,a\n\tld e,a\n\tld d,0\n"
          "\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld (ix-7),e\n\tld (ix-6),d\n"
          "\tld h,d\n\tld l,e\n\tadd hl,hl\n", out);
    mir_breadth_first_emit_named_address_de(
        out, plan->predecessor_assembly_name,
        plan->predecessor_offset);
    mir_stream_printf(out,
            "\tadd hl,de\n\tld a,(hl)\n\tinc hl\n\tor (hl)\n"
            "\tjp nz,L%d\n"
            "\tld e,(ix-7)\n\tld d,(ix-6)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "L%d:\n"
            "\tld e,(ix-7)\n\tld d,(ix-6)\n"
            "\tld h,d\n\tld l,e\n\tadd hl,hl\n",
            next_neighbor,
            destination_offset, destination_offset + 1,
            accept_neighbor,
            first_avoid_offset, first_avoid_offset + 1,
            next_neighbor,
            second_avoid_offset, second_avoid_offset + 1,
            next_neighbor, accept_neighbor);
    mir_breadth_first_emit_named_address_de(
        out, plan->predecessor_assembly_name,
        plan->predecessor_offset);
    mir_stream_printf(out,
            "\tadd hl,de\n"
            "\tld e,(ix-4)\n\tld d,(ix-3)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tld e,(ix-7)\n\tld d,(ix-6)\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tld a,(ix-2)\n\tinc (ix-2)\n"
            "\tld l,a\n\tld h,0\n\tadd hl,hl\n",
            destination_offset, destination_offset + 1, found);
    mir_breadth_first_emit_named_address_de(
        out, plan->queue_assembly_name, plan->queue_offset);
    mir_stream_printf(out,
            "\tadd hl,de\n\tld e,(ix-7)\n\tld d,(ix-6)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "L%d:\n\tinc (ix-5)\n\tjp L%d\n",
            next_neighbor, inner_loop);

    mir_stream_printf(out,
            "L%d:\n\tld e,(ix-7)\n\tld d,(ix-6)\n\tld bc,0\n"
            "L%d:\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp z,L%d\n"
            "\tinc bc\n\tld h,d\n\tld l,e\n\tadd hl,hl\n",
            found, length_loop, source_offset, source_offset + 1,
            length_ready);
    mir_breadth_first_emit_named_address_de(
        out, plan->predecessor_assembly_name,
        plan->predecessor_offset);
    mir_stream_printf(out,
            "\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tjp L%d\n"
            "L%d:\n"
            "\tbit 7,(ix%+d)\n\tjp nz,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tor a\n\tsbc hl,bc\n\tjp c,L%d\n"
            "\tld (ix-1),c\n\txor a\n\tld (ix-2),a\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,c\n\tdec a\n\tadd a,a\n"
            "\tld e,a\n\tld d,0\n\tadd hl,de\n"
            "\tld e,(ix-7)\n\tld d,(ix-6)\n"
            "\tld b,(ix-1)\n"
            "L%d:\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tdec hl\n\tdec hl\n\tdec hl\n\tpush hl\n"
            "\tld h,d\n\tld l,e\n\tadd hl,hl\n",
            length_loop, length_ready, maximum_offset + 1,
            return_zero, maximum_offset, maximum_offset + 1,
            return_zero, path_offset, path_offset + 1, fill_loop);
    mir_breadth_first_emit_named_address_de(
        out, plan->predecessor_assembly_name,
        plan->predecessor_offset);
    mir_stream_printf(out,
            "\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpop hl\n\tdjnz L%d\n"
            "\tld l,(ix-1)\n\tld h,0\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tret\n",
            fill_loop, epilogue, return_zero, epilogue);

    mir_stream_printf(out, "L%d:\n\tjp L%d\n", outer_done, return_zero);
}

static int mir_match_symbol_insert_schedule(
    struct MirSymbolInsertSchedule *plan)
{
    static const int expected_opcodes[71] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG,
        MIR_CALL, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_NOP,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_UNARY,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_NOP, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_RETURN
    };
    int error_argument;
    int memset_arguments[3];
    int copy_arguments[3];
    int memset_destination;
    int memset_fill;
    int memset_count;
    int count_type;
    int count_storage;
    int symbols_type;
    int symbols_storage;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 71 || mir_cfg_block_count() != 2 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.return_type) != 2 ||
        type_is_float(mir.return_type) ||
        type_ptr_depth(mir.return_type) != 0)
        return 0;
    for (instruction = 0; instruction < 71; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return 0;
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->name_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[2].dst, &plan->kind_stack_offset) ||
        !mir_machine_parameter_value_offset(
            mir.insns[3].dst, &plan->scope_stack_offset) ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        (mir.insns[1].type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(&mir.insns[1]) ||
        type_ptr_depth(mir.insns[2].type) != 0 ||
        (mir.insns[2].type & 15) != TYPE_INT ||
        (mir.insns[2].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[2].type) != 2 ||
        type_ptr_depth(mir.insns[3].type) != 0 ||
        (mir.insns[3].type & 15) != TYPE_INT ||
        (mir.insns[3].type & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[3].type) != 2)
        return 0;
    plan->symbol_limit = (int)mir.insns[5].immediate;
    if (!mir_scalar_memory_location(
            &mir.insns[4], &count_type, &count_storage,
            &plan->count_root_offset) ||
        count_storage != SC_GLOBAL ||
        type_ptr_depth(count_type) != 0 ||
        (count_type & 15) != TYPE_INT ||
        (count_type & TYPE_UNSIGNED) != 0 ||
        type_size(count_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[4]) ||
        !mir_machine_constant_equals(
            mir.insns[5].dst, plan->symbol_limit) ||
        plan->symbol_limit != 128 ||
        mir.insns[6].immediate != TOK_GE ||
        mir.insns[6].src1 != mir.insns[4].dst ||
        mir.insns[6].src2 != mir.insns[5].dst ||
        type_ptr_depth(mir.insns[6].secondary_offset) != 0 ||
        (mir.insns[6].secondary_offset & 15) != TYPE_INT ||
        (mir.insns[6].secondary_offset & TYPE_UNSIGNED) != 0 ||
        type_size(mir.insns[6].secondary_offset) != 2 ||
        mir.insns[7].src1 != mir.insns[6].dst ||
        mir.insns[7].label != mir.insns[11].label ||
        mir.insns[9].src1 != mir.insns[8].dst ||
        !mir_machine_single_call_argument(
            &mir.insns[10], &error_argument) ||
        error_argument != mir.insns[8].dst ||
        mir.insns[10].memory_flags != 0)
        return 0;
    plan->error_string_id = (int)mir.insns[8].immediate;
    plan->error_function = find_global(mir.insns[10].name);
    if (plan->error_function == NULL ||
        plan->error_function->storage != SC_FUNC ||
        plan->error_function->is_funcptr ||
        !plan->error_function->has_proto ||
        plan->error_function->proto_variadic ||
        plan->error_function->proto_nargs != 1 ||
        (plan->error_function->type & 15) != TYPE_VOID ||
        type_ptr_depth(plan->error_function->proto_types[0]) != 1)
        return 0;
    if (!mir_machine_same_location(
            &mir.insns[4], &mir.insns[12]) ||
        !mir_machine_constant_equals(mir.insns[13].dst, 1) ||
        mir.insns[14].immediate != '+' ||
        mir.insns[14].src1 != mir.insns[12].dst ||
        mir.insns[14].src2 != mir.insns[13].dst ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].memory_size != 2 ||
        mir.insns[17].src1 != mir.insns[12].dst ||
        mir.insns[17].memory_size != 2)
        return 0;
    plan->count_root = find_global(mir.insns[4].name);
    if (!mir_scalar_memory_location(
            &mir.insns[18], &symbols_type, &symbols_storage,
            &plan->symbols_root_offset) ||
        symbols_storage != SC_GLOBAL ||
        type_ptr_depth(symbols_type) != 1 ||
        type_size(symbols_type) != 2 ||
        !mir_machine_named_nonvolatile(&mir.insns[18]))
        return 0;
    plan->symbols_root = find_global(mir.insns[18].name);
    plan->record_stride = (int)mir.insns[20].immediate;
    if (plan->count_root == NULL || plan->symbols_root == NULL ||
        plan->count_root == plan->symbols_root ||
        plan->count_root->storage == SC_FUNC ||
        plan->symbols_root->storage == SC_FUNC ||
        plan->count_root->is_volatile ||
        plan->symbols_root->is_volatile ||
        type_size(plan->count_root->type) != 2 ||
        type_ptr_depth(plan->symbols_root->type) != 1 ||
        plan->record_stride != 27 ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].src2 != mir.insns[12].dst ||
        mir.insns[20].memory_size != plan->record_stride ||
        mir.insns[20].bit_width != 0 ||
        (mir.insns[20].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[29]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[42]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[49]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[56]) ||
        !mir_machine_same_location(
            &mir.insns[18], &mir.insns[62]))
        return 0;
    if (!mir_machine_three_call_arguments(
            &mir.insns[28], memset_arguments) ||
        memset_arguments[0] != mir.insns[20].dst ||
        memset_arguments[1] != mir.insns[23].dst ||
        memset_arguments[2] != mir.insns[25].dst ||
        !mir_machine_constant_equals(mir.insns[23].dst, 0) ||
        !mir_machine_constant_equals(
            mir.insns[25].dst, plan->record_stride) ||
        !mir_call_is_memset_fastcall(
            28, &memset_destination, &memset_fill, &memset_count) ||
        memset_destination != memset_arguments[0] ||
        memset_fill != memset_arguments[1] ||
        memset_count != memset_arguments[2])
        return 0;
    plan->name_size = mir.insns[32].memory_size;
    if (mir.insns[31].src1 != mir.insns[29].dst ||
        mir.insns[31].src2 != mir.insns[12].dst ||
        mir.insns[31].immediate != plan->record_stride ||
        mir.insns[31].memory_size != plan->record_stride ||
        mir.insns[32].src1 != mir.insns[31].dst ||
        mir.insns[32].immediate != 0 ||
        plan->name_size != 16 ||
        mir.insns[32].bit_width != 0 ||
        (mir.insns[32].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_same_location(
            &mir.insns[1], &mir.insns[34]) ||
        !mir_machine_constant_equals(
            mir.insns[38].dst, plan->name_size - 1) ||
        !mir_machine_three_call_arguments(
            &mir.insns[41], copy_arguments) ||
        copy_arguments[0] != mir.insns[32].dst ||
        copy_arguments[1] != mir.insns[34].dst ||
        copy_arguments[2] != mir.insns[38].dst ||
        mir.insns[41].memory_flags != 0)
        return 0;
    plan->copy_function = find_global(mir.insns[41].name);
    if (plan->copy_function == NULL ||
        plan->copy_function->storage != SC_FUNC ||
        plan->copy_function->is_funcptr ||
        !plan->copy_function->has_proto ||
        plan->copy_function->proto_variadic ||
        plan->copy_function->proto_nargs != 3 ||
        type_ptr_depth(plan->copy_function->type) != 1 ||
        type_ptr_depth(plan->copy_function->proto_types[0]) != 1 ||
        type_ptr_depth(plan->copy_function->proto_types[1]) != 1 ||
        type_size(plan->copy_function->proto_types[2]) != 2 ||
        plan->copy_function == plan->error_function)
        return 0;
    plan->kind_offset = (int)mir.insns[45].immediate;
    plan->scope_offset = (int)mir.insns[52].immediate;
    plan->size_offset = (int)mir.insns[59].immediate;
    plan->element_size_offset = (int)mir.insns[65].immediate;
    plan->element_size = (int)mir.insns[60].immediate;
    if (mir.insns[44].src1 != mir.insns[42].dst ||
        mir.insns[44].src2 != mir.insns[12].dst ||
        mir.insns[45].src1 != mir.insns[44].dst ||
        mir.insns[45].memory_size != 1 ||
        mir.insns[45].bit_width != 0 ||
        (mir.insns[45].memory_flags & (1 | 8)) != 0 ||
        mir.insns[47].src1 != mir.insns[2].dst ||
        type_ptr_depth(mir.insns[47].type) != 0 ||
        (mir.insns[47].type & 15) != TYPE_CHAR ||
        (mir.insns[47].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[47].type) != 1 ||
        mir.insns[48].src1 != mir.insns[45].dst ||
        mir.insns[48].src2 != mir.insns[47].dst ||
        mir.insns[48].memory_size != 1 ||
        mir.insns[48].bit_width != 0 ||
        (mir.insns[48].memory_flags & (1 | 8)) != 0 ||
        mir.insns[51].src1 != mir.insns[49].dst ||
        mir.insns[51].src2 != mir.insns[12].dst ||
        mir.insns[52].src1 != mir.insns[51].dst ||
        mir.insns[52].memory_size != 1 ||
        mir.insns[52].bit_width != 0 ||
        (mir.insns[52].memory_flags & (1 | 8)) != 0 ||
        mir.insns[54].src1 != mir.insns[3].dst ||
        type_ptr_depth(mir.insns[54].type) != 0 ||
        (mir.insns[54].type & 15) != TYPE_CHAR ||
        (mir.insns[54].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.insns[54].type) != 1 ||
        mir.insns[55].src1 != mir.insns[52].dst ||
        mir.insns[55].src2 != mir.insns[54].dst ||
        mir.insns[55].memory_size != 1 ||
        mir.insns[55].bit_width != 0 ||
        (mir.insns[55].memory_flags & (1 | 8)) != 0 ||
        mir.insns[58].src1 != mir.insns[56].dst ||
        mir.insns[58].src2 != mir.insns[12].dst ||
        mir.insns[59].src1 != mir.insns[58].dst ||
        mir.insns[59].memory_size != 2 ||
        mir.insns[59].bit_width != 0 ||
        (mir.insns[59].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(
            mir.insns[60].dst, plan->element_size) ||
        mir.insns[61].src1 != mir.insns[59].dst ||
        mir.insns[61].src2 != mir.insns[60].dst ||
        mir.insns[61].memory_size != 2 ||
        mir.insns[61].bit_width != 0 ||
        (mir.insns[61].memory_flags & (1 | 8)) != 0 ||
        mir.insns[64].src1 != mir.insns[62].dst ||
        mir.insns[64].src2 != mir.insns[12].dst ||
        mir.insns[65].src1 != mir.insns[64].dst ||
        mir.insns[65].memory_size != 1 ||
        mir.insns[65].bit_width != 0 ||
        (mir.insns[65].memory_flags & (1 | 8)) != 0 ||
        !mir_machine_constant_equals(
            mir.insns[67].dst, plan->element_size) ||
        mir.insns[68].src1 != mir.insns[65].dst ||
        mir.insns[68].src2 != mir.insns[67].dst ||
        mir.insns[68].memory_size != 1 ||
        mir.insns[68].bit_width != 0 ||
        (mir.insns[68].memory_flags & (1 | 8)) != 0 ||
        mir.insns[70].src1 != mir.insns[12].dst ||
        plan->kind_offset != 16 ||
        plan->scope_offset != 17 ||
        plan->element_size_offset != 18 ||
        plan->size_offset != 21 ||
        plan->element_size != 2)
        return 0;
    return 1;
}

static void mir_scanner_emit_symbol_record_address(
    MirStream *out, const struct MirSymbolInsertSchedule *plan)
{
    mir_machine_emit_global_word(
        out, plan->symbols_root, plan->symbols_root_offset);
    mir_stream_puts("\tpush hl\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
          "\tld d,h\n\tld e,l\n"
          "\tadd hl,hl\n\tadd hl,de\n"
          "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,de\n"
          "\tadd hl,hl\n\tadd hl,de\n"
          "\tex de,hl\n\tpop hl\n\tadd hl,de\n", out);
}

static void mir_scanner_emit_symbol_member_address(
    MirStream *out, const struct MirSymbolInsertSchedule *plan, int offset)
{
    mir_scanner_emit_symbol_record_address(out, plan);
    mir_stream_printf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_scanner_emit_ix_parameter_word(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            offset, offset + 1);
}

static void mir_emit_symbol_insert_schedule(
    MirStream *out, const struct MirSymbolInsertSchedule *plan)
{
    int insert = new_label();
    int epilogue = new_label();

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-2\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_root_offset);
    mir_stream_printf(out, "\tpush hl\n\tld hl,%d\n\tex de,hl\n\tpop hl\n",
            plan->symbol_limit);
    mir_stream_puts("\tld a,h\n\txor 80h\n\tld h,a\n"
          "\tld a,d\n\txor 80h\n\tld d,a\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n\tld hl,S%d\n\tpush hl\n",
            insert, plan->error_string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", insert);

    mir_machine_emit_global_word(
        out, plan->count_root, plan->count_root_offset);
    mir_stream_puts("\tpush hl\n\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->count_root, plan->count_root_offset);
    mir_stream_puts("\tpop hl\n\tld (ix-2),l\n\tld (ix-1),h\n", out);

    mir_scanner_emit_symbol_record_address(out, plan);
    mir_stream_puts("\tpush hl\n\tld hl,0\n\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tld b,h\n\tld c,l\n"
            "\tpop de\n\tpop hl\n",
            plan->record_stride);
    mir_emit_runtime_call(out, "__msf");

    mir_stream_printf(out, "\tld hl,%d\n\tpush hl\n", plan->name_size - 1);
    mir_scanner_emit_ix_parameter_word(
        out, plan->name_stack_offset + 2);
    mir_stream_puts("\tpush hl\n", out);
    mir_scanner_emit_symbol_record_address(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_emit_final_call_cleanup(out, 3);

    mir_scanner_emit_symbol_member_address(
        out, plan, plan->kind_offset);
    mir_stream_printf(out, "\tld a,(ix%+d)\n\tld (hl),a\n",
            plan->kind_stack_offset + 2);
    mir_scanner_emit_symbol_member_address(
        out, plan, plan->scope_offset);
    mir_stream_printf(out, "\tld a,(ix%+d)\n\tld (hl),a\n",
            plan->scope_stack_offset + 2);
    mir_scanner_emit_symbol_member_address(
        out, plan, plan->size_offset);
    mir_stream_printf(out,
            "\tpush hl\n\tld hl,%d\n\tex de,hl\n\tpop hl\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
            plan->element_size);
    mir_scanner_emit_symbol_member_address(
        out, plan, plan->element_size_offset);
    mir_stream_printf(out, "\tld a,%d\n\tld (hl),a\n",
            plan->element_size);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            epilogue, epilogue);
}

struct MirGridLinear {
    long x;
    long y;
    long i;
    long constant;
};

static int mir_grid_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        type_size(type) == 2 &&
        (type & TYPE_UNSIGNED) == 0;
}

static int mir_grid_unsigned_byte_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_CHAR &&
        type_size(type) == 1 &&
        (type & TYPE_UNSIGNED) != 0;
}

static int mir_grid_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        type_size(type) == 2 &&
        (type & TYPE_UNSIGNED) == 0;
}

static int mir_grid_linear_value(
    int value, int x_value, int y_value, int i_value,
    struct MirGridLinear *result, int depth)
{
    const struct MirInsn *definition;
    struct MirGridLinear left;
    struct MirGridLinear right;
    long multiplier;

    memset(result, 0, sizeof(*result));
    if (depth > 24)
        return 0;
    if (value == x_value) {
        result->x = 1;
        return 1;
    }
    if (value == y_value) {
        result->y = 1;
        return 1;
    }
    if (value == i_value) {
        result->i = 1;
        return 1;
    }
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_CONST) {
        unsigned long bits;

        if (!mir_grid_signed_word_type(definition->type))
            return 0;
        bits = (unsigned long)definition->immediate & 0xffffUL;
        result->constant =
            bits >= 0x8000UL ? (long)bits - 0x10000L : (long)bits;
        return 1;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == 0 &&
        mir_grid_signed_word_type(definition->type))
        return mir_grid_linear_value(
            definition->src1, x_value, y_value, i_value,
            result, depth + 1);
    if (definition->opcode != MIR_BINARY ||
        !mir_grid_signed_word_type(definition->type) ||
        !mir_grid_signed_word_type(definition->secondary_offset) ||
        !mir_grid_linear_value(
            definition->src1, x_value, y_value, i_value,
            &left, depth + 1) ||
        !mir_grid_linear_value(
            definition->src2, x_value, y_value, i_value,
            &right, depth + 1))
        return 0;
    if (definition->immediate == '+' ||
        definition->immediate == '-') {
        int sign = definition->immediate == '+' ? 1 : -1;

        result->x = left.x + sign * right.x;
        result->y = left.y + sign * right.y;
        result->i = left.i + sign * right.i;
        result->constant =
            left.constant + sign * right.constant;
        return 1;
    }
    if (definition->immediate != '*')
        return 0;
    if (left.x == 0 && left.y == 0 && left.i == 0) {
        multiplier = left.constant;
        *result = right;
    } else if (right.x == 0 && right.y == 0 && right.i == 0) {
        multiplier = right.constant;
        *result = left;
    } else {
        return 0;
    }
    result->x *= multiplier;
    result->y *= multiplier;
    result->i *= multiplier;
    result->constant *= multiplier;
    return result->x >= -32768 && result->x <= 32767 &&
        result->y >= -32768 && result->y <= 32767 &&
        result->i >= -32768 && result->i <= 32767 &&
        result->constant >= -32768 && result->constant <= 32767;
}

static int mir_grid_linear_matches(
    int value, int x_value, int y_value, int i_value,
    long x, long y, long i, long constant)
{
    struct MirGridLinear actual;

    return mir_grid_linear_value(
               value, x_value, y_value, i_value, &actual, 0) &&
        actual.x == x && actual.y == y && actual.i == i &&
        actual.constant == constant;
}

static int mir_grid_local_access(
    const struct MirInsn *insn, int opcode, int object,
    int base_type, int width, int require_unsigned,
    const struct MirInsn *same_location)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    return insn->opcode == opcode &&
        insn->object == object &&
        insn->bit_width == 0 &&
        (insn->memory_flags & (1 | 8)) == 0 &&
        mir_scalar_memory_location(
            insn, &memory_type, &memory_storage, &memory_offset) &&
        memory_storage == SC_LOCAL &&
        type_ptr_depth(memory_type) == 0 &&
        (memory_type & 15) == base_type &&
        type_size(memory_type) == width &&
        ((memory_type & TYPE_UNSIGNED) != 0) == require_unsigned &&
        memory_offset >= -mir.local_bytes &&
        memory_offset + width <= 0 &&
        mir_machine_named_nonvolatile(insn) &&
        (same_location == NULL ||
         mir_machine_same_location(insn, same_location));
}

static int mir_grid_match_compare(
    int value, int x_value, int y_value, int i_value,
    long x, long y, long i, long constant,
    int operation, long right_constant)
{
    const struct MirInsn *comparison = mir_definition(value);
    const struct MirInsn *right;

    if (comparison == NULL)
        return 0;
    right = mir_definition(comparison->src2);

    return comparison->opcode == MIR_BINARY &&
        comparison->immediate == operation &&
        mir_grid_signed_word_type(comparison->type) &&
        mir_grid_signed_word_type(comparison->secondary_offset) &&
        mir_grid_linear_matches(
            comparison->src1, x_value, y_value, i_value,
            x, y, i, constant) &&
        right != NULL &&
        right->opcode == MIR_CONST &&
        mir_grid_signed_word_type(right->type) &&
        mir_machine_constant_equals(
            comparison->src2, right_constant);
}

static int mir_grid_match_and_condition(
    int value, int x_value, int y_value, int i_value,
    long first_x, long first_y, long first_i,
    int first_operation, long first_right,
    long second_x, long second_y, long second_i,
    int second_operation, long second_right)
{
    const struct MirInsn *condition = mir_definition(value);

    return condition != NULL &&
        condition->opcode == MIR_BINARY &&
        condition->immediate == '&' &&
        mir_grid_signed_word_type(condition->type) &&
        mir_grid_signed_word_type(condition->secondary_offset) &&
        mir_grid_match_compare(
            condition->src1, x_value, y_value, i_value,
            first_x, first_y, first_i, 0,
            first_operation, first_right) &&
        mir_grid_match_compare(
            condition->src2, x_value, y_value, i_value,
            second_x, second_y, second_i, 0,
            second_operation, second_right);
}

static int mir_grid_match_word_load(
    int root_index, int address_index, int load_index,
    const struct MirGlobalScalar *table,
    int x_value, int y_value, int i_value,
    long x, long y, long i)
{
    const struct MirInsn *root_load = &mir.insns[root_index];
    const struct MirInsn *address = &mir.insns[address_index];
    const struct MirInsn *load = &mir.insns[load_index];
    struct MirGlobalScalar actual;

    return root_load->opcode == MIR_LOAD &&
        root_load->bit_width == 0 &&
        (root_load->memory_flags & (1 | 8)) == 0 &&
        mir_match_scanner_global_scalar(
            root_load, TYPE_INT, 1, 2, &actual) &&
        mir_scanner_same_global(&actual, table) &&
        address->src1 == root_load->dst &&
        mir_grid_linear_matches(
            address->src2, x_value, y_value, i_value,
            x, y, i, 0) &&
        address->immediate == 2 &&
        address->memory_size == 2 &&
        address->bit_width == 0 &&
        (address->memory_flags & (1 | 8)) == 0 &&
        mir_grid_word_pointer_type(address->type) &&
        load->src1 == address->dst &&
        load->memory_size == 2 &&
        load->bit_width == 0 &&
        (load->memory_flags & (1 | 8)) == 0 &&
        mir_grid_signed_word_type(load->type);
}

static int mir_grid_match_accumulation(
    int load_index, int previous_value, int cell_value,
    int binary_index, int store_index, int accumulator_object,
    const struct MirInsn *accumulator_location)
{
    const struct MirInsn *binary = &mir.insns[binary_index];
    const struct MirInsn *store = &mir.insns[store_index];
    int left_value = previous_value;

    if (load_index >= 0) {
        const struct MirInsn *load = &mir.insns[load_index];

        if (!mir_grid_local_access(
                load, MIR_LOAD, accumulator_object,
                TYPE_INT, 2, 0, accumulator_location))
            return 0;
        left_value = load->dst;
    }
    return binary->src1 == left_value &&
        binary->src2 == cell_value &&
        binary->immediate == '+' &&
        mir_grid_signed_word_type(binary->type) &&
        mir_grid_signed_word_type(binary->secondary_offset) &&
        store->src1 == binary->dst &&
        mir_grid_local_access(
            store, MIR_STORE, accumulator_object,
            TYPE_INT, 2, 0, accumulator_location);
}

static int mir_match_square_grid_line_sum_schedule(
    struct MirSquareGridLineSumSchedule *plan)
{
    static const int expected_opcodes[186] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_LOAD, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_UNARY,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY,
        MIR_CONST, MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_LOAD, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_NOP,
        MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_BINARY, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    static const int dimension_constants[] = {
        14, 21, 34, 48, 54, 64, 81, 97, 120, 130, 163
    };
    const struct MirInsn *x_parameter = &mir.insns[1];
    const struct MirInsn *y_parameter = &mir.insns[2];
    const struct MirInsn *counter_location = &mir.insns[5];
    const struct MirInsn *accumulator_location = &mir.insns[7];
    int counter_object;
    int accumulator_object;
    int instruction;
    int constant_index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 186 || mir_cfg_block_count() != 8 ||
        mir.has_vla || mir.local_bytes != 5 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_grid_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "square-grid-line-sum-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            x_parameter->dst, &plan->x_stack_offset) ||
        !mir_machine_parameter_value_offset(
            y_parameter->dst, &plan->y_stack_offset) ||
        plan->x_stack_offset < 0 ||
        plan->x_stack_offset > 32765 ||
        plan->y_stack_offset != plan->x_stack_offset + 2 ||
        plan->y_stack_offset > 32765 ||
        !mir_grid_signed_word_type(x_parameter->type) ||
        !mir_grid_signed_word_type(y_parameter->type) ||
        !mir_machine_named_nonvolatile(x_parameter) ||
        !mir_machine_named_nonvolatile(y_parameter) ||
        x_parameter->object < 0 || y_parameter->object < 0 ||
        x_parameter->object == y_parameter->object)
        return mir_machine_reject(
            "square-grid-line-sum-schedule", "parameters");

    counter_object = counter_location->object;
    accumulator_object = accumulator_location->object;
    if (counter_object < 0 || accumulator_object < 0 ||
        counter_object == accumulator_object ||
        counter_object == x_parameter->object ||
        counter_object == y_parameter->object ||
        accumulator_object == x_parameter->object ||
        accumulator_object == y_parameter->object ||
        !mir_machine_constant_equals(mir.insns[4].dst, 0) ||
        !mir_grid_unsigned_byte_type(mir.insns[4].type) ||
        counter_location->src1 != mir.insns[4].dst ||
        !mir_grid_local_access(
            counter_location, MIR_STORE, counter_object,
            TYPE_CHAR, 1, 1, NULL) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_grid_signed_word_type(mir.insns[6].type) ||
        accumulator_location->src1 != mir.insns[6].dst ||
        !mir_grid_local_access(
            accumulator_location, MIR_STORE, accumulator_object,
            TYPE_INT, 2, 0, NULL))
        return mir_machine_reject(
            "square-grid-line-sum-schedule", "locals");

    plan->dimension = (int)mir.insns[14].immediate;
    if (!mir_machine_constant_equals(
            mir.insns[14].dst, plan->dimension) ||
        !mir_grid_signed_word_type(mir.insns[14].type) ||
        plan->dimension <= 0 || plan->dimension > 127 ||
        !mir_mul_const_fast_path_eligible(
            (unsigned long)plan->dimension, mir.insns[24].dst) ||
        !mir_mul_const_fast_path_eligible(
            (unsigned long)(plan->dimension - 1),
            mir.insns[102].dst) ||
        !mir_mul_const_fast_path_eligible(
            (unsigned long)(plan->dimension + 1),
            mir.insns[69].dst))
        return mir_machine_reject(
            "square-grid-line-sum-schedule", "dimension");
    for (constant_index = 0;
         constant_index <
             (int)(sizeof(dimension_constants) /
                   sizeof(dimension_constants[0]));
         ++constant_index) {
        const struct MirInsn *constant =
            &mir.insns[dimension_constants[constant_index]];

        if (!mir_machine_constant_equals(
                constant->dst, plan->dimension) ||
            !mir_grid_signed_word_type(constant->type))
            return mir_machine_reject(
                "square-grid-line-sum-schedule",
                "dimension-constants");
    }

    if (mir.insns[11].object != counter_object ||
        mir.insns[11].src1 != mir.insns[4].dst ||
        mir.insns[11].src2 != mir.insns[180].dst ||
        mir.insns[11].phi_pred1 != mir.insns[0].label ||
        mir.insns[11].phi_pred2 != mir.insns[177].label ||
        !mir_grid_unsigned_byte_type(mir.insns[11].type) ||
        !mir_grid_match_compare(
            mir.insns[16].dst,
            x_parameter->dst, y_parameter->dst, mir.insns[11].dst,
            0, 0, 1, 0, '<', plan->dimension) ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        mir.insns[17].label != mir.insns[183].label ||
        !mir_machine_constant_equals(mir.insns[179].dst, 1) ||
        !mir_grid_unsigned_byte_type(mir.insns[179].type) ||
        mir.insns[180].src1 != mir.insns[11].dst ||
        mir.insns[180].src2 != mir.insns[179].dst ||
        mir.insns[180].immediate != '+' ||
        !mir_grid_unsigned_byte_type(mir.insns[180].type) ||
        mir.insns[180].secondary_offset != mir.insns[179].type ||
        mir.insns[181].src1 != mir.insns[180].dst ||
        !mir_grid_local_access(
            &mir.insns[181], MIR_STORE, counter_object,
            TYPE_CHAR, 1, 1, counter_location) ||
        mir.insns[182].label != mir.insns[8].label)
        return mir_machine_reject(
            "square-grid-line-sum-schedule", "loop");

    if (!mir_match_scanner_global_scalar(
            &mir.insns[19], TYPE_INT, 1, 2, &plan->table) ||
        mir.insns[19].opcode != MIR_LOAD ||
        !mir_grid_word_pointer_type(plan->table.type) ||
        plan->table.root->pointee_is_volatile)
        return mir_machine_reject(
            "square-grid-line-sum-schedule", "table");
    {
        static const int load_indices[6][3] = {
            {19, 26, 27}, {32, 39, 40}, {59, 71, 72},
            {92, 104, 105}, {125, 137, 138}, {158, 170, 171}
        };
        long coefficients[6][3];
        int load;

        coefficients[0][0] = 1;
        coefficients[0][1] = 0;
        coefficients[0][2] = plan->dimension;
        coefficients[1][0] = 0;
        coefficients[1][1] = plan->dimension;
        coefficients[1][2] = 1;
        coefficients[2][0] = 1;
        coefficients[2][1] = plan->dimension;
        coefficients[2][2] = plan->dimension + 1;
        coefficients[3][0] = 1;
        coefficients[3][1] = plan->dimension;
        coefficients[3][2] = 1 - plan->dimension;
        coefficients[4][0] = 1;
        coefficients[4][1] = plan->dimension;
        coefficients[4][2] = plan->dimension - 1;
        coefficients[5][0] = 1;
        coefficients[5][1] = plan->dimension;
        coefficients[5][2] = -plan->dimension - 1;
        for (load = 0; load < 6; ++load)
            if (!mir_grid_match_word_load(
                    load_indices[load][0],
                    load_indices[load][1],
                    load_indices[load][2],
                    &plan->table,
                    x_parameter->dst, y_parameter->dst,
                    mir.insns[11].dst,
                    coefficients[load][0],
                    coefficients[load][1],
                    coefficients[load][2]))
                return mir_machine_reject(
                    "square-grid-line-sum-schedule",
                    load == 0 ? "load-0" :
                    load == 1 ? "load-1" :
                    load == 2 ? "load-2" :
                    load == 3 ? "load-3" :
                    load == 4 ? "load-4" : "load-5");
    }

    if (!mir_grid_match_and_condition(
            mir.insns[56].dst,
            x_parameter->dst, y_parameter->dst, mir.insns[11].dst,
            1, 0, 1, '<', plan->dimension,
            0, 1, 1, '<', plan->dimension) ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        mir.insns[57].label != mir.insns[76].label ||
        !mir_grid_match_and_condition(
            mir.insns[89].dst,
            x_parameter->dst, y_parameter->dst, mir.insns[11].dst,
            1, 0, 1, '<', plan->dimension,
            0, 1, -1, TOK_GE, 0) ||
        mir.insns[90].src1 != mir.insns[89].dst ||
        mir.insns[90].label != mir.insns[109].label ||
        !mir_grid_match_and_condition(
            mir.insns[122].dst,
            x_parameter->dst, y_parameter->dst, mir.insns[11].dst,
            1, 0, -1, TOK_GE, 0,
            0, 1, 1, '<', plan->dimension) ||
        mir.insns[123].src1 != mir.insns[122].dst ||
        mir.insns[123].label != mir.insns[142].label ||
        !mir_grid_match_and_condition(
            mir.insns[155].dst,
            x_parameter->dst, y_parameter->dst, mir.insns[11].dst,
            1, 0, -1, TOK_GE, 0,
            0, 1, -1, TOK_GE, 0) ||
        mir.insns[156].src1 != mir.insns[155].dst ||
        mir.insns[156].label != mir.insns[175].label)
        return mir_machine_reject(
            "square-grid-line-sum-schedule", "conditions");

    {
        static const int accumulations[6][5] = {
            {18, -1, 27, 28, 30},
            {-1, 28, 40, 41, 43},
            {-1, 41, 72, 73, 75},
            {91, -1, 105, 106, 108},
            {124, -1, 138, 139, 141},
            {157, -1, 171, 172, 174}
        };
        int accumulation;

        for (accumulation = 0; accumulation < 6; ++accumulation)
            if (!mir_grid_match_accumulation(
                    accumulations[accumulation][0],
                    accumulations[accumulation][1] < 0
                        ? -1
                        : mir.insns[
                              accumulations[accumulation][1]].dst,
                    mir.insns[accumulations[accumulation][2]].dst,
                    accumulations[accumulation][3],
                    accumulations[accumulation][4],
                    accumulator_object, accumulator_location))
                return mir_machine_reject(
                    "square-grid-line-sum-schedule",
                    accumulation == 0 ? "accumulator-0" :
                    accumulation == 1 ? "accumulator-1" :
                    accumulation == 2 ? "accumulator-2" :
                    accumulation == 3 ? "accumulator-3" :
                    accumulation == 4 ? "accumulator-4" :
                                        "accumulator-5");
    }
    if (!mir_grid_local_access(
            &mir.insns[184], MIR_LOAD, accumulator_object,
            TYPE_INT, 2, 0, accumulator_location) ||
        mir.insns[185].src1 != mir.insns[184].dst)
        return mir_machine_reject(
            "square-grid-line-sum-schedule", "return");
    return 1;
}

static void mir_grid_emit_counter_hl(MirStream *out)
{
    mir_stream_puts("\tex af,af'\n\tld l,a\n\tex af,af'\n\tld h,0\n",
          out);
}

static void mir_grid_emit_shadow_bc(MirStream *out, const char *target)
{
    mir_stream_printf(out, "\texx\n\tpush bc\n\texx\n\tpop %s\n", target);
}

static void mir_grid_emit_shadow_de(MirStream *out, const char *target)
{
    mir_stream_printf(out, "\texx\n\tpush de\n\texx\n\tpop %s\n", target);
}

static void mir_grid_emit_linear_index(
    MirStream *out, int include_x, int include_scaled_y,
    int counter_coefficient)
{
    unsigned long magnitude =
        (unsigned long)(counter_coefficient < 0
                            ? -counter_coefficient
                            : counter_coefficient);

    mir_grid_emit_counter_hl(out);
    mir_emit_mul_hl_const(out, magnitude);
    if (counter_coefficient >= 0) {
        if (include_x)
            mir_stream_puts("\tadd hl,bc\n", out);
        if (include_scaled_y) {
            mir_grid_emit_shadow_de(out, "de");
            mir_stream_puts("\tadd hl,de\n", out);
        }
        return;
    }
    mir_stream_puts("\tpush hl\n", out);
    if (include_x)
        mir_stream_puts("\tld h,b\n\tld l,c\n", out);
    else
        mir_stream_puts("\tld hl,0\n", out);
    if (include_scaled_y) {
        mir_grid_emit_shadow_de(out, "de");
        mir_stream_puts("\tadd hl,de\n", out);
    }
    mir_stream_puts("\tpop de\n\tor a\n\tsbc hl,de\n", out);
}

static void mir_grid_emit_accumulate_index(
    MirStream *out, const struct MirSquareGridLineSumSchedule *plan,
    int include_x, int include_scaled_y, int counter_coefficient)
{
    mir_grid_emit_linear_index(
        out, include_x, include_scaled_y, counter_coefficient);
    mir_stream_puts("\tadd hl,hl\n\tex de,hl\n", out);
    mir_machine_emit_global_word(
        out, plan->table.root, plan->table.offset);
    mir_stream_puts("\tadd hl,de\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tadd iy,de\n", out);
}

static void mir_grid_emit_signed_less_than_constant(
    MirStream *out, int constant, int failure_label)
{
    mir_stream_printf(out,
            "\tld de,%d\n\tld a,h\n\txor 128\n\tld h,a\n"
            "\tsbc hl,de\n\tjp nc,L%d\n",
            32768 + constant, failure_label);
}

static void mir_grid_emit_x_plus_i_check(
    MirStream *out, int dimension, int failure_label)
{
    mir_grid_emit_counter_hl(out);
    mir_stream_puts("\tadd hl,bc\n", out);
    mir_grid_emit_signed_less_than_constant(
        out, dimension, failure_label);
}

static void mir_grid_emit_y_plus_i_check(
    MirStream *out, int dimension, int failure_label)
{
    mir_grid_emit_counter_hl(out);
    mir_grid_emit_shadow_bc(out, "de");
    mir_stream_puts("\tadd hl,de\n", out);
    mir_grid_emit_signed_less_than_constant(
        out, dimension, failure_label);
}

static void mir_grid_emit_x_minus_i_check(
    MirStream *out, int failure_label)
{
    mir_grid_emit_counter_hl(out);
    mir_stream_puts("\tex de,hl\n\tld h,b\n\tld l,c\n"
          "\tor a\n\tsbc hl,de\n\tbit 7,h\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", failure_label);
}

static void mir_grid_emit_y_minus_i_check(
    MirStream *out, int failure_label)
{
    mir_grid_emit_counter_hl(out);
    mir_stream_puts("\tex de,hl\n", out);
    mir_grid_emit_shadow_bc(out, "hl");
    mir_stream_puts("\tor a\n\tsbc hl,de\n\tbit 7,h\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", failure_label);
}

static void mir_emit_square_grid_line_sum_schedule(
    MirStream *out, const struct MirSquareGridLineSumSchedule *plan)
{
    int loop = new_label();
    int done = new_label();
    int x_plus_done = new_label();
    int plus_plus_done = new_label();
    int plus_minus_done = new_label();
    int x_minus_done = new_label();
    int minus_plus_done = new_label();
    int minus_minus_done = new_label();

    mir_stream_printf(out,
            ";@dcc.reg claim=iy scope=function sym=%s kind=mir val=0\n"
            "\tpush iy\n",
            mir.name);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld h,d\n\tld l,e\n\tpush hl\n",
            plan->x_stack_offset + 2,
            plan->y_stack_offset + 2);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->dimension);
    mir_stream_puts("\tpush hl\n\texx\n\tpop de\n\tpop bc\n\texx\n"
          "\tld iy,0\n\txor a\n\tex af,af'\n", out);

    mir_stream_printf(out, "L%d:\n\tex af,af'\n\tcp %d\n\tjp nc,L%d\n"
            "\tex af,af'\n",
            loop, plan->dimension, done);
    mir_grid_emit_accumulate_index(
        out, plan, 1, 0, plan->dimension);
    mir_grid_emit_accumulate_index(
        out, plan, 0, 1, 1);

    mir_grid_emit_x_plus_i_check(
        out, plan->dimension, x_plus_done);
    mir_grid_emit_y_plus_i_check(
        out, plan->dimension, plus_plus_done);
    mir_grid_emit_accumulate_index(
        out, plan, 1, 1, plan->dimension + 1);
    mir_stream_printf(out, "L%d:\n", plus_plus_done);
    mir_grid_emit_y_minus_i_check(out, plus_minus_done);
    mir_grid_emit_accumulate_index(
        out, plan, 1, 1, 1 - plan->dimension);
    mir_stream_printf(out, "L%d:\nL%d:\n",
            plus_minus_done, x_plus_done);

    mir_grid_emit_x_minus_i_check(out, x_minus_done);
    mir_grid_emit_y_plus_i_check(
        out, plan->dimension, minus_plus_done);
    mir_grid_emit_accumulate_index(
        out, plan, 1, 1, plan->dimension - 1);
    mir_stream_printf(out, "L%d:\n", minus_plus_done);
    mir_grid_emit_y_minus_i_check(out, minus_minus_done);
    mir_grid_emit_accumulate_index(
        out, plan, 1, 1, -plan->dimension - 1);
    mir_stream_printf(out, "L%d:\nL%d:\n"
            "\tex af,af'\n\tinc a\n\tex af,af'\n"
            "\tjp L%d\n",
            minus_minus_done, x_minus_done, loop);

    mir_stream_printf(out, "L%d:\n\tpush iy\n\tpop hl\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            done);
}

static int mir_long_index_byte_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 2;
}

static int mir_long_index_signed_type(
    int type, int base_type, int width)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == base_type &&
           type_size(type) == width &&
           (type & TYPE_UNSIGNED) == 0;
}

static int mir_long_index_unsigned_char_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 1 &&
           (type & TYPE_UNSIGNED) != 0;
}

static int mir_long_index_index_address(
    int instruction, int base, int index)
{
    const struct MirInsn *address = &mir.insns[instruction];

    return address->opcode == MIR_INDEX_ADDRESS &&
           address->src1 == base &&
           address->src2 == index &&
           mir_long_index_byte_pointer_type(address->type) &&
           address->memory_size == 1 &&
           address->immediate == 1 &&
           address->bit_width == 0 &&
           (address->memory_flags & (1 | 8)) == 0;
}

static int mir_long_index_byte_load(
    int instruction, int address)
{
    const struct MirInsn *load = &mir.insns[instruction];

    return load->opcode == MIR_LOAD_INDIRECT &&
           load->src1 == address &&
           load->memory_size == 1 &&
           load->bit_width == 0 &&
           (load->memory_flags & (1 | 8)) == 0 &&
           mir_long_index_signed_type(
               load->type, TYPE_CHAR, 1);
}

static int mir_long_index_byte_store(
    int instruction, int address, int value)
{
    const struct MirInsn *store = &mir.insns[instruction];

    return store->opcode == MIR_STORE_INDIRECT &&
           store->src1 == address &&
           store->src2 == value &&
           store->memory_size == 1 &&
           store->bit_width == 0 &&
           (store->memory_flags & (1 | 8)) == 0;
}

static int mir_long_index_local_access(
    int instruction, int opcode, int base_type, int width,
    const struct MirInsn *location)
{
    return mir_match_decimal_local_access(
        &mir.insns[instruction], opcode, base_type, width,
        location->object, location);
}

static int mir_long_index_global_byte_address(
    int value, struct Sym **root_out, int *offset_out)
{
    const struct MirInsn *definition = mir_definition(value);
    struct Sym *root;
    long offset;

    if (definition == NULL ||
        definition->opcode != MIR_ADDRESS ||
        !mir_long_index_byte_pointer_type(definition->type) ||
        !mir_machine_named_nonvolatile(definition) ||
        !mir_machine_global_address_offset(
            value, &root, &offset, 0) ||
        root == NULL || root->is_volatile ||
        offset < -32768 || offset > 32767)
        return 0;
    *root_out = root;
    *offset_out = (int)offset;
    return 1;
}

static int mir_long_index_same_global_byte_address(
    int value, struct Sym *root, int offset)
{
    struct Sym *actual_root;
    int actual_offset;

    return mir_long_index_global_byte_address(
               value, &actual_root, &actual_offset) &&
           actual_root == root && actual_offset == offset;
}

static int mir_match_long_index_byte_count(
    struct MirLongIndexByteLoopSchedule *plan)
{
    static const int expected_opcodes[31] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_UNARY, MIR_STORE, MIR_NOP,
        MIR_UNARY, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_store = &mir.insns[4];
    const struct MirInsn *index_phi = &mir.insns[7];
    const struct MirInsn *byte_store = &mir.insns[22];
    long initial;
    int initial_instruction;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 31 || mir_cfg_block_count() != 4 ||
        mir.has_vla ||
        !mir_long_index_signed_type(
            mir.return_type, TYPE_LONG, 4))
        return 0;
    if (mir.insns[2].opcode == MIR_NOP &&
        mir.insns[3].opcode == MIR_CONST)
        initial_instruction = 3;
    else if (mir.insns[2].opcode == MIR_CONST &&
             mir.insns[3].opcode == MIR_NOP)
        initial_instruction = 2;
    else
        return mir_machine_reject(
            "long-index-byte-count-schedule", "initializer");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        if (instruction == 2 || instruction == 3)
            continue;
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "long-index-byte-count-schedule", "opcodes");
    }

    if (!mir_machine_parameter_value_offset(
            parameter->dst, &plan->input_stack_offset) ||
        !mir_long_index_byte_pointer_type(parameter->type) ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_named_nonvolatile(parameter) ||
        parameter->object < 0 ||
        mir.insns[6].object != parameter->object ||
        mir.insns[8].object != parameter->object ||
        mir.insns[13].object != parameter->object)
        return mir_machine_reject(
            "long-index-byte-count-schedule", "parameter");

    if (!mir_machine_evaluate_constant(
            mir.insns[initial_instruction].dst, &initial, 0) ||
        initial < (-2147483647L - 1L) ||
        (initial >= 0 &&
         (unsigned long)initial > 0xffffffffUL) ||
        !mir_long_index_local_access(
            4, MIR_STORE, TYPE_LONG, 4, index_store) ||
        index_store->src1 !=
            mir.insns[initial_instruction].dst ||
        index_phi->object != index_store->object ||
        !mir_machine_same_location(index_phi, index_store) ||
        index_phi->src1 !=
            mir.insns[initial_instruction].dst ||
        index_phi->src2 != mir.insns[16].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[26].label ||
        !mir_long_index_signed_type(
            index_phi->type, TYPE_LONG, 4) ||
        mir.insns[9].object != index_store->object ||
        mir.insns[14].object != index_store->object ||
        mir.insns[29].object != index_store->object)
        return mir_machine_reject(
            "long-index-byte-count-schedule", "index");

    if (!mir_long_index_index_address(
            10, parameter->dst, index_phi->dst) ||
        !mir_long_index_byte_load(
            11, mir.insns[10].dst) ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[28].label ||
        !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        mir.insns[16].src1 != index_phi->dst ||
        mir.insns[16].src2 != mir.insns[15].dst ||
        mir.insns[16].immediate != '+' ||
        !mir_long_index_signed_type(
            mir.insns[16].type, TYPE_LONG, 4) ||
        !mir_long_index_local_access(
            17, MIR_STORE, TYPE_LONG, 4, index_store) ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !mir_long_index_index_address(
            18, parameter->dst, index_phi->dst) ||
        !mir_long_index_byte_load(
            19, mir.insns[18].dst))
        return mir_machine_reject(
            "long-index-byte-count-schedule", "loop");

    if (mir.insns[20].src1 != mir.insns[19].dst ||
        !mir_long_index_unsigned_char_type(
            mir.insns[20].type) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        !mir_long_index_signed_type(
            mir.insns[21].type, TYPE_INT, 2) ||
        !mir_long_index_local_access(
            22, MIR_STORE, TYPE_INT, 2, byte_store) ||
        byte_store->src1 != mir.insns[21].dst ||
        mir.insns[23].object != byte_store->object ||
        mir.insns[24].src1 != mir.insns[21].dst ||
        (mir.insns[24].type & 15) != TYPE_VOID ||
        mir.insns[27].label != mir.insns[5].label ||
        mir.insns[30].src1 != index_phi->dst)
        return mir_machine_reject(
            "long-index-byte-count-schedule", "body");

    plan->kind = MIR_LONG_INDEX_BYTE_COUNT;
    plan->input_initial =
        (unsigned long)initial & 0xffffffffUL;
    return 1;
}

static int mir_match_long_index_byte_copy(
    struct MirLongIndexByteLoopSchedule *plan)
{
    static const int expected_opcodes[46] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_PHI, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_UNARY, MIR_STORE, MIR_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *input_store = &mir.insns[4];
    const struct MirInsn *output_store = &mir.insns[7];
    const struct MirInsn *input_phi = &mir.insns[10];
    const struct MirInsn *output_phi = &mir.insns[11];
    const struct MirInsn *byte_store = &mir.insns[26];
    long input_initial;
    long output_initial;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 46 || mir_cfg_block_count() != 4 ||
        mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "long-index-byte-copy-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            parameter->dst, &plan->input_stack_offset) ||
        !mir_long_index_byte_pointer_type(parameter->type) ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_named_nonvolatile(parameter) ||
        parameter->object < 0 ||
        mir.insns[9].object != parameter->object ||
        mir.insns[12].object != parameter->object ||
        mir.insns[17].object != parameter->object)
        return mir_machine_reject(
            "long-index-byte-copy-schedule", "parameter");

    if (!mir_machine_evaluate_constant(
            mir.insns[3].dst, &input_initial, 0) ||
        input_initial < (-2147483647L - 1L) ||
        (input_initial >= 0 &&
         (unsigned long)input_initial > 0xffffffffUL) ||
        !mir_machine_evaluate_constant(
            mir.insns[6].dst, &output_initial, 0) ||
        output_initial < (-2147483647L - 1L) ||
        (output_initial >= 0 &&
         (unsigned long)output_initial > 0xffffffffUL) ||
        !mir_long_index_local_access(
            4, MIR_STORE, TYPE_LONG, 4, input_store) ||
        !mir_long_index_local_access(
            7, MIR_STORE, TYPE_LONG, 4, output_store) ||
        input_store->object == output_store->object ||
        input_store->src1 != mir.insns[3].dst ||
        output_store->src1 != mir.insns[6].dst ||
        input_phi->object != input_store->object ||
        output_phi->object != output_store->object ||
        !mir_machine_same_location(input_phi, input_store) ||
        !mir_machine_same_location(output_phi, output_store) ||
        input_phi->src1 != mir.insns[3].dst ||
        input_phi->src2 != mir.insns[20].dst ||
        output_phi->src1 != mir.insns[6].dst ||
        output_phi->src2 != mir.insns[30].dst ||
        input_phi->phi_pred1 != mir.insns[0].label ||
        input_phi->phi_pred2 != mir.insns[37].label ||
        output_phi->phi_pred1 != mir.insns[0].label ||
        output_phi->phi_pred2 != mir.insns[37].label ||
        !mir_long_index_signed_type(
            input_phi->type, TYPE_LONG, 4) ||
        !mir_long_index_signed_type(
            output_phi->type, TYPE_LONG, 4))
        return mir_machine_reject(
            "long-index-byte-copy-schedule", "indices");

    if (!mir_long_index_index_address(
            14, parameter->dst, input_phi->dst) ||
        !mir_long_index_byte_load(
            15, mir.insns[14].dst) ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[39].label ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].src1 != input_phi->dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[20].immediate != '+' ||
        !mir_long_index_signed_type(
            mir.insns[20].type, TYPE_LONG, 4) ||
        !mir_long_index_local_access(
            21, MIR_STORE, TYPE_LONG, 4, input_store) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        !mir_long_index_index_address(
            22, parameter->dst, input_phi->dst) ||
        !mir_long_index_byte_load(
            23, mir.insns[22].dst))
        return mir_machine_reject(
            "long-index-byte-copy-schedule", "input-loop");

    if (mir.insns[24].src1 != mir.insns[23].dst ||
        !mir_long_index_unsigned_char_type(
            mir.insns[24].type) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        !mir_long_index_signed_type(
            mir.insns[25].type, TYPE_INT, 2) ||
        !mir_long_index_local_access(
            26, MIR_STORE, TYPE_INT, 2, byte_store) ||
        byte_store->src1 != mir.insns[25].dst ||
        !mir_long_index_global_byte_address(
            mir.insns[27].dst, &plan->output_root,
            &plan->output_offset) ||
        mir.insns[28].object != output_store->object ||
        !mir_machine_constant_equals(mir.insns[29].dst, 1) ||
        mir.insns[30].src1 != output_phi->dst ||
        mir.insns[30].src2 != mir.insns[29].dst ||
        mir.insns[30].immediate != '+' ||
        !mir_long_index_signed_type(
            mir.insns[30].type, TYPE_LONG, 4) ||
        !mir_long_index_local_access(
            31, MIR_STORE, TYPE_LONG, 4, output_store) ||
        mir.insns[31].src1 != mir.insns[30].dst ||
        !mir_long_index_index_address(
            32, mir.insns[27].dst, output_phi->dst) ||
        mir.insns[33].object != byte_store->object ||
        mir.insns[34].src1 != mir.insns[25].dst ||
        (mir.insns[34].type & 15) != TYPE_CHAR ||
        !mir_long_index_byte_store(
            35, mir.insns[32].dst, mir.insns[34].dst) ||
        mir.insns[38].label != mir.insns[8].label)
        return mir_machine_reject(
            "long-index-byte-copy-schedule", "copy");

    if (!mir_long_index_same_global_byte_address(
            mir.insns[40].dst, plan->output_root,
            plan->output_offset) ||
        mir.insns[41].object != output_store->object ||
        !mir_long_index_index_address(
            42, mir.insns[40].dst, output_phi->dst) ||
        !mir_machine_constant_equals(mir.insns[44].dst, 0) ||
        !mir_long_index_byte_store(
            45, mir.insns[42].dst, mir.insns[44].dst))
        return mir_machine_reject(
            "long-index-byte-copy-schedule", "terminator");

    plan->kind = MIR_LONG_INDEX_BYTE_COPY;
    plan->input_initial =
        (unsigned long)input_initial & 0xffffffffUL;
    plan->output_initial =
        (unsigned long)output_initial & 0xffffffffUL;
    return 1;
}

static int mir_match_long_index_byte_loop_schedule(
    struct MirLongIndexByteLoopSchedule *plan)
{
    return mir_match_long_index_byte_count(plan) ||
           mir_match_long_index_byte_copy(plan);
}

static int mir_match_long_index_comment_copy_schedule(
    struct MirLongIndexCommentCopySchedule *plan)
{
    static const int expected_opcodes[131] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_UNARY, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_ADDRESS, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_JUMP, MIR_NOP, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT
    };
    static const int parameter_loads[] = {
        12, 17, 31, 52, 57, 80, 94
    };
    static const int input_index_addresses[][3] = {
        {14, 12, 13}, {22, 17, 18}, {33, 31, 20},
        {54, 52, 49}, {59, 57, 49}, {82, 80, 49},
        {99, 94, 49}
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *input_store = &mir.insns[4];
    const struct MirInsn *output_store = &mir.insns[7];
    const struct MirInsn *byte_store = &mir.insns[26];
    long constant;
    int instruction;
    size_t item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 131 || mir_cfg_block_count() != 16 ||
        mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "long-index-comment-copy-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            parameter->dst, &plan->input_stack_offset) ||
        !mir_long_index_byte_pointer_type(parameter->type) ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_named_nonvolatile(parameter) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_long_index_local_access(
            4, MIR_STORE, TYPE_LONG, 4, input_store) ||
        !mir_long_index_local_access(
            7, MIR_STORE, TYPE_LONG, 4, output_store) ||
        input_store->object == output_store->object)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "state");

    for (item = 0;
         item < sizeof(parameter_loads) /
                    sizeof(parameter_loads[0]);
         ++item)
        if (!mir_match_decimal_pointer_access(
                &mir.insns[parameter_loads[item]], MIR_LOAD,
                parameter))
            return mir_machine_reject(
                "long-index-comment-copy-schedule",
                "parameter-load");

    if (!mir_long_index_local_access(
            13, MIR_LOAD, TYPE_LONG, 4, input_store) ||
        !mir_long_index_local_access(
            18, MIR_LOAD, TYPE_LONG, 4, input_store) ||
        !mir_long_index_local_access(
            89, MIR_LOAD, TYPE_LONG, 4, output_store) ||
        !mir_long_index_local_access(
            112, MIR_LOAD, TYPE_LONG, 4, output_store) ||
        !mir_long_index_local_access(
            126, MIR_LOAD, TYPE_LONG, 4, output_store) ||
        !mir_long_index_local_access(
            107, MIR_LOAD, TYPE_INT, 2, byte_store) ||
        !mir_long_index_local_access(
            117, MIR_LOAD, TYPE_INT, 2, byte_store))
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "state-load");

    for (item = 0;
         item < sizeof(input_index_addresses) /
                    sizeof(input_index_addresses[0]);
         ++item) {
        int address = input_index_addresses[item][0];
         int base = input_index_addresses[item][1];
         int index = input_index_addresses[item][2];

         if (!mir_long_index_index_address(
                 address, mir.insns[base].dst,
                 mir.insns[index].dst) ||
            !mir_long_index_byte_load(
                address + 1, mir.insns[address].dst))
            return mir_machine_reject(
                "long-index-comment-copy-schedule",
                "input-address");
    }

    if (mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[124].label ||
        !mir_machine_constant_equals(mir.insns[19].dst, 1) ||
        mir.insns[20].src1 != mir.insns[18].dst ||
        mir.insns[20].src2 != mir.insns[19].dst ||
        mir.insns[20].immediate != '+' ||
        !mir_long_index_local_access(
            21, MIR_STORE, TYPE_LONG, 4, input_store) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[24].src1 != mir.insns[23].dst ||
        !mir_long_index_unsigned_char_type(
            mir.insns[24].type) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        !mir_long_index_signed_type(
            mir.insns[25].type, TYPE_INT, 2) ||
        !mir_long_index_local_access(
            26, MIR_STORE, TYPE_INT, 2, byte_store) ||
        mir.insns[26].src1 != mir.insns[25].dst)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "outer-loop");

    if (!mir_machine_evaluate_constant(
            mir.insns[28].dst, &constant, 0) ||
        constant <= 0 || constant > 255)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "comment-constant");
    plan->comment_character = (int)constant;
    if (mir.insns[29].src1 != mir.insns[25].dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].immediate != TOK_EQ ||
        mir.insns[30].label != mir.insns[42].label ||
        !mir_machine_evaluate_constant(
            mir.insns[35].dst, &constant, 0) ||
        constant != plan->comment_character ||
        mir.insns[36].src1 != mir.insns[34].dst ||
        mir.insns[37].src1 != mir.insns[36].dst ||
        mir.insns[37].src2 != mir.insns[35].dst ||
        mir.insns[37].immediate != TOK_EQ ||
        mir.insns[38].label != mir.insns[42].label ||
        !mir_machine_constant_equals(mir.insns[40].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[43].dst, 0) ||
        mir.insns[45].src1 != mir.insns[40].dst ||
        mir.insns[45].src2 != mir.insns[43].dst ||
        mir.insns[45].phi_pred1 != mir.insns[39].label ||
        mir.insns[45].phi_pred2 != mir.insns[42].label ||
        mir.insns[46].src1 != mir.insns[45].dst ||
        mir.insns[46].label != mir.insns[106].label)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "comment-test");

    if (mir.insns[49].object != input_store->object ||
        !mir_machine_same_location(
            &mir.insns[49], input_store) ||
        mir.insns[49].src1 != mir.insns[20].dst ||
        mir.insns[49].src2 != mir.insns[75].dst ||
        mir.insns[49].phi_pred1 != mir.insns[44].label ||
        mir.insns[49].phi_pred2 != mir.insns[77].label ||
        mir.insns[56].src1 != mir.insns[55].dst ||
        mir.insns[56].label != mir.insns[68].label ||
        !mir_machine_evaluate_constant(
            mir.insns[61].dst, &constant, 0) ||
        constant <= 0 || constant > 255)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "comment-loop");
    plan->newline_character = (int)constant;
    if (mir.insns[62].src1 != mir.insns[60].dst ||
        mir.insns[63].src1 != mir.insns[62].dst ||
        mir.insns[63].src2 != mir.insns[61].dst ||
        mir.insns[63].immediate != TOK_NE ||
        mir.insns[64].label != mir.insns[68].label ||
        !mir_machine_constant_equals(mir.insns[66].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[69].dst, 0) ||
        mir.insns[71].src1 != mir.insns[66].dst ||
        mir.insns[71].src2 != mir.insns[69].dst ||
        mir.insns[71].phi_pred1 != mir.insns[65].label ||
        mir.insns[71].phi_pred2 != mir.insns[68].label ||
        mir.insns[72].src1 != mir.insns[71].dst ||
        mir.insns[72].label != mir.insns[79].label ||
        !mir_machine_constant_equals(mir.insns[74].dst, 1) ||
        mir.insns[75].src1 != mir.insns[49].dst ||
        mir.insns[75].src2 != mir.insns[74].dst ||
        mir.insns[75].immediate != '+' ||
        !mir_long_index_local_access(
            76, MIR_STORE, TYPE_LONG, 4, input_store) ||
        mir.insns[76].src1 != mir.insns[75].dst ||
        mir.insns[78].label != mir.insns[47].label)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "comment-backedge");

    if (!mir_machine_evaluate_constant(
            mir.insns[84].dst, &constant, 0) ||
        constant != plan->newline_character ||
        mir.insns[85].src1 != mir.insns[83].dst ||
        mir.insns[86].src1 != mir.insns[85].dst ||
        mir.insns[86].src2 != mir.insns[84].dst ||
        mir.insns[86].immediate != TOK_EQ ||
        mir.insns[87].label != mir.insns[8].label ||
        !mir_long_index_global_byte_address(
            mir.insns[88].dst, &plan->output_root,
            &plan->output_offset) ||
        !mir_machine_constant_equals(mir.insns[90].dst, 1) ||
        mir.insns[91].src1 != mir.insns[89].dst ||
        mir.insns[91].src2 != mir.insns[90].dst ||
        mir.insns[91].immediate != '+' ||
        !mir_long_index_local_access(
            92, MIR_STORE, TYPE_LONG, 4, output_store) ||
        mir.insns[92].src1 != mir.insns[91].dst ||
        !mir_long_index_index_address(
            93, mir.insns[88].dst, mir.insns[89].dst) ||
        !mir_machine_constant_equals(mir.insns[96].dst, 1) ||
        mir.insns[97].src1 != mir.insns[49].dst ||
        mir.insns[97].src2 != mir.insns[96].dst ||
        mir.insns[97].immediate != '+' ||
        !mir_long_index_local_access(
            98, MIR_STORE, TYPE_LONG, 4, input_store) ||
        mir.insns[98].src1 != mir.insns[97].dst ||
        !mir_long_index_byte_store(
            101, mir.insns[93].dst, mir.insns[100].dst) ||
        mir.insns[104].label != mir.insns[8].label)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "newline-copy");

    if (!mir_machine_evaluate_constant(
            mir.insns[108].dst, &constant, 0) ||
        constant <= 0 || constant > 255)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "discard-constant");
    plan->discarded_character = (int)constant;
    if (mir.insns[109].src1 != mir.insns[107].dst ||
        mir.insns[109].src2 != mir.insns[108].dst ||
        mir.insns[109].immediate != TOK_NE ||
        mir.insns[110].label != mir.insns[120].label ||
        !mir_long_index_same_global_byte_address(
            mir.insns[111].dst, plan->output_root,
            plan->output_offset) ||
        !mir_machine_constant_equals(mir.insns[113].dst, 1) ||
        mir.insns[114].src1 != mir.insns[112].dst ||
        mir.insns[114].src2 != mir.insns[113].dst ||
        mir.insns[114].immediate != '+' ||
        !mir_long_index_local_access(
            115, MIR_STORE, TYPE_LONG, 4, output_store) ||
        mir.insns[115].src1 != mir.insns[114].dst ||
        !mir_long_index_index_address(
            116, mir.insns[111].dst, mir.insns[112].dst) ||
        mir.insns[118].src1 != mir.insns[117].dst ||
        (mir.insns[118].type & 15) != TYPE_CHAR ||
        !mir_long_index_byte_store(
            119, mir.insns[116].dst, mir.insns[118].dst) ||
        mir.insns[123].label != mir.insns[8].label)
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "ordinary-copy");

    if (!mir_long_index_same_global_byte_address(
            mir.insns[125].dst, plan->output_root,
            plan->output_offset) ||
        !mir_long_index_index_address(
            127, mir.insns[125].dst, mir.insns[126].dst) ||
        !mir_machine_constant_equals(mir.insns[129].dst, 0) ||
        !mir_long_index_byte_store(
            130, mir.insns[127].dst, mir.insns[129].dst))
        return mir_machine_reject(
            "long-index-comment-copy-schedule", "terminator");
    return 1;
}

static int mir_match_long_index_word_count_schedule(
    struct MirLongIndexWordCountSchedule *plan)
{
    static const int expected_opcodes[82] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_UNARY, MIR_STORE, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *parameter = &mir.insns[1];
    const struct MirInsn *index_store = &mir.insns[4];
    const struct MirInsn *count_store = &mir.insns[7];
    const struct MirInsn *state_store = &mir.insns[10];
    const struct MirInsn *byte_store = &mir.insns[30];
    const struct MirInsn *call = &mir.insns[33];
    const char *assembly_name;
    long constant;
    int argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 82 || mir_cfg_block_count() != 15 ||
        mir.has_vla ||
        !mir_long_index_signed_type(
            mir.return_type, TYPE_INT, 2))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
                expected_opcodes[instruction])
            return mir_machine_reject(
                "long-index-word-count-schedule", "opcodes");

    if (!mir_machine_parameter_value_offset(
            parameter->dst, &plan->input_stack_offset) ||
        !mir_long_index_byte_pointer_type(parameter->type) ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_named_nonvolatile(parameter) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_long_index_local_access(
            4, MIR_STORE, TYPE_LONG, 4, index_store) ||
        !mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_long_index_local_access(
            7, MIR_STORE, TYPE_INT, 2, count_store) ||
        !mir_machine_constant_equals(mir.insns[8].dst, 0) ||
        !mir_long_index_local_access(
            10, MIR_STORE, TYPE_INT, 2, state_store) ||
        index_store->object == count_store->object ||
        index_store->object == state_store->object ||
        count_store->object == state_store->object)
        return mir_machine_reject(
            "long-index-word-count-schedule", "state");

    if (mir.insns[13].object != index_store->object ||
        !mir_machine_same_location(
            &mir.insns[13], index_store) ||
        mir.insns[13].src1 != mir.insns[3].dst ||
        mir.insns[13].src2 != mir.insns[24].dst ||
        mir.insns[13].phi_pred1 != mir.insns[0].label ||
        mir.insns[13].phi_pred2 != mir.insns[77].label ||
        !mir_long_index_index_address(
            18, parameter->dst, mir.insns[13].dst) ||
        !mir_long_index_byte_load(
            19, mir.insns[18].dst) ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].label != mir.insns[79].label ||
        !mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        mir.insns[24].src1 != mir.insns[13].dst ||
        mir.insns[24].src2 != mir.insns[23].dst ||
        mir.insns[24].immediate != '+' ||
        !mir_long_index_local_access(
            25, MIR_STORE, TYPE_LONG, 4, index_store) ||
        mir.insns[25].src1 != mir.insns[24].dst ||
        !mir_long_index_index_address(
            26, parameter->dst, mir.insns[13].dst) ||
        !mir_long_index_byte_load(
            27, mir.insns[26].dst) ||
        mir.insns[28].src1 != mir.insns[27].dst ||
        !mir_long_index_unsigned_char_type(
            mir.insns[28].type) ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        !mir_long_index_signed_type(
            mir.insns[29].type, TYPE_INT, 2) ||
        !mir_long_index_local_access(
            30, MIR_STORE, TYPE_INT, 2, byte_store) ||
        mir.insns[30].src1 != mir.insns[29].dst)
        return mir_machine_reject(
            "long-index-word-count-schedule", "scan");

    plan->classify_function = find_global(call->name);
    if (call->src1 >= 0 ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME)) != 0 ||
        plan->classify_function == NULL ||
        plan->classify_function->storage != SC_FUNC ||
        plan->classify_function->is_funcptr ||
        plan->classify_function->is_noreturn ||
        !plan->classify_function->has_proto ||
        plan->classify_function->proto_variadic ||
        plan->classify_function->proto_nargs != 1 ||
        !mir_long_index_signed_type(
            plan->classify_function->type, TYPE_INT, 2) ||
        !mir_long_index_signed_type(
            plan->classify_function->proto_types[0],
            TYPE_INT, 2) ||
        !mir_long_index_signed_type(
            call->type, TYPE_INT, 2) ||
        !mir_machine_single_call_argument(call, &argument) ||
        argument != mir.insns[29].dst)
        return mir_machine_reject(
            "long-index-word-count-schedule", "call");
    assembly_name =
        asm_name_for(sym_asm_name(plan->classify_function));
    if (call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return mir_machine_reject(
            "long-index-word-count-schedule", "call-name");

    if (mir.insns[34].src1 != call->dst ||
        mir.insns[34].label != mir.insns[38].label ||
        !mir_machine_constant_equals(mir.insns[36].dst, 1) ||
        mir.insns[37].label != mir.insns[52].label ||
        !mir_machine_evaluate_constant(
            mir.insns[40].dst, &constant, 0) ||
        constant <= 0 || constant > 255)
        return mir_machine_reject(
            "long-index-word-count-schedule", "classification");
    plan->word_character = (int)constant;
    if (mir.insns[41].src1 != mir.insns[29].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[41].immediate != TOK_EQ ||
        mir.insns[42].label != mir.insns[46].label ||
        !mir_machine_constant_equals(mir.insns[44].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[47].dst, 0) ||
        mir.insns[49].src1 != mir.insns[44].dst ||
        mir.insns[49].src2 != mir.insns[47].dst ||
        mir.insns[49].phi_pred1 != mir.insns[43].label ||
        mir.insns[49].phi_pred2 != mir.insns[46].label ||
        mir.insns[51].label != mir.insns[52].label ||
        mir.insns[53].src1 != mir.insns[36].dst ||
        mir.insns[53].src2 != mir.insns[49].dst ||
        mir.insns[53].phi_pred1 != mir.insns[35].label ||
        mir.insns[53].phi_pred2 != mir.insns[50].label ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        mir.insns[54].label != mir.insns[70].label)
        return mir_machine_reject(
            "long-index-word-count-schedule", "boolean-cfg");

    if (!mir_long_index_local_access(
            56, MIR_LOAD, TYPE_INT, 2, state_store) ||
        mir.insns[57].src1 != mir.insns[56].dst ||
        mir.insns[57].immediate != '!' ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[58].label != mir.insns[75].label ||
        !mir_long_index_local_access(
            59, MIR_LOAD, TYPE_INT, 2, count_store) ||
        !mir_machine_constant_equals(mir.insns[60].dst, 1) ||
        mir.insns[61].src1 != mir.insns[59].dst ||
        mir.insns[61].src2 != mir.insns[60].dst ||
        mir.insns[61].immediate != '+' ||
        !mir_long_index_local_access(
            62, MIR_STORE, TYPE_INT, 2, count_store) ||
        mir.insns[62].src1 != mir.insns[61].dst ||
        !mir_machine_constant_equals(mir.insns[63].dst, 1) ||
        !mir_long_index_local_access(
            65, MIR_STORE, TYPE_INT, 2, state_store) ||
        mir.insns[65].src1 != mir.insns[63].dst ||
        mir.insns[69].label != mir.insns[75].label ||
        !mir_machine_constant_equals(mir.insns[71].dst, 0) ||
        !mir_long_index_local_access(
            73, MIR_STORE, TYPE_INT, 2, state_store) ||
        mir.insns[73].src1 != mir.insns[71].dst ||
        mir.insns[78].label != mir.insns[11].label ||
        !mir_long_index_local_access(
            80, MIR_LOAD, TYPE_INT, 2, count_store) ||
        mir.insns[81].src1 != mir.insns[80].dst)
        return mir_machine_reject(
            "long-index-word-count-schedule", "count-state");
    return 1;
}

static void mir_long_index_emit_iy_claim(MirStream *out)
{
    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n", out);
}

static void mir_long_index_emit_iy_parameter(
    MirStream *out, int stack_offset)
{
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n",
            stack_offset);
}

static void mir_long_index_emit_iy_offset(
    MirStream *out, unsigned int offset)
{
    if (offset == 0)
        return;
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_stream_printf(out, "\tld bc,%u\n\tadd hl,bc\n", offset);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
}

static void mir_long_index_emit_de_offset(
    MirStream *out, unsigned int offset)
{
    if (offset == 0)
        return;
    mir_stream_puts("\tex de,hl\n", out);
    mir_stream_printf(out, "\tld bc,%u\n\tadd hl,bc\n", offset);
    mir_stream_puts("\tex de,hl\n", out);
}

static void mir_long_index_emit_iy_return(MirStream *out)
{
    mir_stream_puts("\tpop iy\n;@dcc.reg free=iy\n\tret\n", out);
}

static void mir_emit_long_index_byte_loop_schedule(
    MirStream *out, const struct MirLongIndexByteLoopSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_long_index_emit_iy_claim(out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_long_index_emit_iy_parameter(
        out, plan->input_stack_offset + 2);
    mir_long_index_emit_iy_offset(
        out, (unsigned int)(plan->input_initial & 0xffffUL));

    if (plan->kind == MIR_LONG_INDEX_BYTE_COUNT) {
        mir_stream_printf(out, "\tld bc,%lu\n\tld de,%lu\n",
                plan->input_initial & 0xffffUL,
                (plan->input_initial >> 16) & 0xffffUL);
        mir_stream_printf(out,
                "L%d:\n"
                "\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
                "\tinc iy\n\tinc bc\n"
                "\tld a,b\n\tor c\n\tjp nz,L%d\n"
                "\tinc de\n\tjp L%d\n"
                "L%d:\n\tld l,c\n\tld h,b\n",
                loop, done, loop, loop, done);
    } else {
        mir_machine_emit_global_address_de(
            out, plan->output_root, plan->output_offset);
        mir_long_index_emit_de_offset(
            out, (unsigned int)(plan->output_initial & 0xffffUL));
        mir_stream_printf(out,
                "L%d:\n"
                "\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
                "\tinc iy\n\tld (de),a\n\tinc de\n"
                "\tjp L%d\n"
                "L%d:\n\txor a\n\tld (de),a\n",
                loop, done, loop, done);
    }
    mir_long_index_emit_iy_return(out);
}

static void mir_emit_long_index_comment_copy_schedule(
    MirStream *out, const struct MirLongIndexCommentCopySchedule *plan)
{
    int loop = new_label();
    int ordinary = new_label();
    int comment_loop = new_label();
    int newline = new_label();
    int done = new_label();

    mir_long_index_emit_iy_claim(out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_long_index_emit_iy_parameter(
        out, plan->input_stack_offset + 2);
    mir_machine_emit_global_address_de(
        out, plan->output_root, plan->output_offset);

    mir_stream_printf(out,
            "L%d:\n"
            "\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
            "\tinc iy\n\tcp %d\n\tjp nz,L%d\n"
            "\tcp (iy+0)\n\tjp nz,L%d\n"
            "L%d:\n"
            "\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
            "\tcp %d\n\tjp z,L%d\n"
            "\tinc iy\n\tjp L%d\n"
            "L%d:\n"
            "\tinc iy\n\tld (de),a\n\tinc de\n"
            "\tjp L%d\n"
            "L%d:\n"
            "\tcp %d\n\tjp z,L%d\n"
            "\tld (de),a\n\tinc de\n\tjp L%d\n"
            "L%d:\n\txor a\n\tld (de),a\n",
            loop, done,
            plan->comment_character, ordinary,
            ordinary, comment_loop, done,
            plan->newline_character, newline,
            comment_loop, newline, loop,
            ordinary, plan->discarded_character, loop,
            loop, done);
    mir_long_index_emit_iy_return(out);
}

static void mir_emit_long_index_word_count_schedule(
    MirStream *out, const struct MirLongIndexWordCountSchedule *plan)
{
    int loop = new_label();
    int word = new_label();
    int already_word = new_label();
    int nonword = new_label();
    int done = new_label();

    mir_long_index_emit_iy_claim(out);
    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n",
            plan->input_stack_offset + 4,
            plan->input_stack_offset + 5);
    mir_stream_puts("\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n"
          "\tld (ix-3),a\n", out);

    mir_stream_printf(out,
            "L%d:\n"
            "\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
            "\tinc iy\n\tld l,a\n\tld h,0\n\tpush hl\n",
            loop, done);
    mir_machine_emit_symbol_call(
        out, plan->classify_function);
    mir_stream_printf(out,
            "\tpop bc\n\tld a,h\n\tor l\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp %d\n\tjp nz,L%d\n"
            "L%d:\n"
            "\tld a,(ix-3)\n\tor a\n\tjp nz,L%d\n"
            "\tinc (ix-2)\n\tjp nz,L%d\n\tinc (ix-1)\n"
            "L%d:\n\tld a,1\n\tld (ix-3),a\n\tjp L%d\n"
            "L%d:\n\txor a\n\tld (ix-3),a\n\tjp L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld sp,ix\n\tpop ix\n",
            word, plan->word_character, nonword,
            word, already_word, already_word,
            already_word, loop, nonword, loop,
            done);
    mir_long_index_emit_iy_return(out);
}

static int mir_vla_chain_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_vla_chain_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_vla_chain_control_edges_match(void)
{
    static const int edges[40][2] = {
        {21, 278}, {33, 271}, {46, 264}, {60, 257}, {75, 250},
        {91, 243}, {108, 236}, {126, 229}, {145, 222}, {165, 215},
        {214, 149}, {221, 130}, {228, 112}, {235, 95}, {242, 79},
        {249, 64}, {256, 50}, {263, 37}, {270, 25}, {277, 14},
        {298, 594}, {318, 587}, {338, 580}, {358, 573}, {378, 566},
        {398, 559}, {418, 552}, {438, 545}, {458, 538}, {478, 531},
        {530, 462}, {537, 442}, {544, 422}, {551, 402}, {558, 382},
        {565, 362}, {572, 342}, {579, 322}, {586, 302}, {593, 282}
    };
    int edge;

    for (edge = 0; edge < 40; ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return 0;
    return 1;
}

static int mir_vla_chain_collect_address(
    int value, const struct MirInsn **root,
    const struct MirInsn **indices, int *strides, int capacity)
{
    int count = 0;

    while (count < capacity) {
        const struct MirInsn *add = mir_definition(value);
        const struct MirInsn *multiply;
        const struct MirInsn *index;
        const struct MirInsn *stride;

        if (add == NULL || add->opcode != MIR_BINARY ||
            add->immediate != '+' ||
            !mir_vla_chain_word_pointer_type(add->type))
            break;
        multiply = mir_definition(add->src2);
        if (multiply == NULL || multiply->opcode != MIR_BINARY ||
            multiply->immediate != '*' ||
            !mir_vla_chain_signed_word_type(multiply->type))
            return -1;
        index = mir_definition(multiply->src1);
        stride = mir_definition(multiply->src2);
        if (index == NULL ||
            (index->opcode != MIR_LOAD &&
             index->opcode != MIR_PHI) ||
            !mir_vla_chain_signed_word_type(index->type) ||
            stride == NULL || stride->opcode != MIR_CONST ||
            stride->immediate <= 0 || stride->immediate > 32767)
            return -1;
        indices[count] = index;
        strides[count] = (int)stride->immediate;
        ++count;
        value = add->src1;
    }
    *root = mir_definition(value);
    if (*root == NULL || (*root)->opcode != MIR_LOAD ||
        !mir_vla_chain_word_pointer_type((*root)->type))
        return -1;
    {
        int left = 0;
        int right = count - 1;

        while (left < right) {
            const struct MirInsn *index = indices[left];
            int stride = strides[left];

            indices[left] = indices[right];
            strides[left] = strides[right];
            indices[right] = index;
            strides[right] = stride;
            ++left;
            --right;
        }
    }
    return count;
}

static int mir_vla_chain_index_value_matches(
    int value, const struct MirInsn *location)
{
    const struct MirInsn *definition = mir_definition(value);
    int instruction;

    if (definition == NULL)
        return 0;
    if ((definition->opcode == MIR_LOAD ||
         definition->opcode == MIR_PHI) &&
        mir_machine_same_location(definition, location))
        return 1;
    if (definition->opcode != MIR_BINARY ||
        definition->immediate != '+' ||
        !mir_machine_constant_equals(definition->src2, 1) ||
        !mir_vla_chain_index_value_matches(
            definition->src1, location))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_STORE &&
            mir.insns[instruction].src1 == definition->dst &&
            mir_machine_same_location(
                &mir.insns[instruction], location))
            return 1;
    return 0;
}

static int mir_vla_chain_index_state_matches(
    const struct MirInsn *location, const struct MirInsn *rows,
    int outer, int bound)
{
    int zero_stores = 0;
    int increment_stores = 0;
    int comparisons = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_STORE &&
            mir_machine_same_location(insn, location)) {
            const struct MirInsn *source = mir_definition(insn->src1);

            if (source != NULL && source->opcode == MIR_CONST &&
                source->immediate == 0) {
                ++zero_stores;
            } else if (source != NULL &&
                       source->opcode == MIR_BINARY &&
                       source->immediate == '+' &&
                       mir_vla_chain_signed_word_type(source->type) &&
                       mir_vla_chain_signed_word_type(
                           source->secondary_offset) &&
                       mir_vla_chain_index_value_matches(
                           source->src1, location) &&
                       mir_machine_constant_equals(source->src2, 1)) {
                ++increment_stores;
            }
        } else if (insn->opcode == MIR_BINARY &&
                   insn->immediate == '<' &&
                   mir_vla_chain_signed_word_type(insn->type) &&
                   mir_vla_chain_signed_word_type(
                       insn->secondary_offset) &&
                   mir_vla_chain_index_value_matches(
                       insn->src1, location) &&
                   ((outer && insn->src2 == rows->dst) ||
                    (!outer &&
                     mir_machine_constant_equals(
                         insn->src2, bound)))) {
            ++comparisons;
        }
    }
    return zero_stores == 2 &&
        increment_stores == 2 &&
        comparisons == 2;
}

static int mir_match_vla_constant_fill_sum_schedule(
    struct MirVlaConstantFillSumSchedule *plan)
{
    const struct MirInsn *rows = &mir.insns[1];
    const struct MirInsn *allocation = &mir.insns[6];
    const struct MirInsn *store = &mir.insns[208];
    const struct MirInsn *load = &mir.insns[521];
    const struct MirInsn *store_root;
    const struct MirInsn *load_root;
    const struct MirInsn *store_indices[10];
    const struct MirInsn *load_indices[10];
    int store_strides[10];
    int load_strides[10];
    int opcode_counts[16] = {0};
    int instruction;
    int index;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 597 || mir_cfg_block_count() != 61 ||
        !mir.has_vla || mir.local_bytes != 30 ||
        !mir_vla_chain_signed_word_type(mir.return_type))
        return 0;
    if (mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_VLA_SAVE ||
        mir.insns[3].opcode != MIR_NOP ||
        mir.insns[4].opcode != MIR_CONST ||
        mir.insns[5].opcode != MIR_BINARY ||
        mir.insns[6].opcode != MIR_VLA_ALLOC ||
        mir.insns[7].opcode != MIR_LOAD ||
        mir.insns[8].opcode != MIR_STORE ||
        mir.insns[9].opcode != MIR_CONST ||
        mir.insns[10].opcode != MIR_STORE ||
        mir.insns[522].opcode != MIR_BINARY ||
        mir.insns[524].opcode != MIR_STORE ||
        mir.insns[595].opcode != MIR_LOAD ||
        mir.insns[596].opcode != MIR_RETURN)
        return mir_machine_reject(
            "vla-constant-fill-sum-schedule", "positions");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int bucket;

        switch (insn->opcode) {
        case MIR_NOP: bucket = 0; break;
        case MIR_BINARY: bucket = 1; break;
        case MIR_CONST: bucket = 2; break;
        case MIR_LABEL: bucket = 3; break;
        case MIR_LOAD: bucket = 4; break;
        case MIR_STORE: bucket = 5; break;
        case MIR_BRANCH_FALSE: bucket = 6; break;
        case MIR_JUMP: bucket = 7; break;
        case MIR_PHI: bucket = 8; break;
        case MIR_PARAM: bucket = 9; break;
        case MIR_VLA_SAVE: bucket = 10; break;
        case MIR_VLA_ALLOC: bucket = 11; break;
        case MIR_STORE_INDIRECT: bucket = 12; break;
        case MIR_LOAD_INDIRECT: bucket = 13; break;
        case MIR_RETURN: bucket = 14; break;
        default:
            return mir_machine_reject(
                "vla-constant-fill-sum-schedule", "opcode");
        }
        ++opcode_counts[bucket];
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "vla-constant-fill-sum-schedule",
                "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0 ||
             insn->memory_size != 2))
            return mir_machine_reject(
                "vla-constant-fill-sum-schedule",
                "volatile-indirect-memory");
    }
    if (opcode_counts[0] != 223 ||
        opcode_counts[1] != 82 ||
        opcode_counts[2] != 81 ||
        opcode_counts[3] != 61 ||
        opcode_counts[4] != 59 ||
        opcode_counts[5] != 43 ||
        opcode_counts[6] != 20 ||
        opcode_counts[7] != 20 ||
        opcode_counts[8] != 2 ||
        opcode_counts[9] != 1 ||
        opcode_counts[10] != 1 ||
        opcode_counts[11] != 1 ||
        opcode_counts[12] != 1 ||
        opcode_counts[13] != 1 ||
        opcode_counts[14] != 1 ||
        !mir_vla_chain_control_edges_match())
        return mir_machine_reject(
            "vla-constant-fill-sum-schedule", "shape");

    if (!mir_machine_parameter_value_offset(
            rows->dst, &plan->rows_stack_offset) ||
        !mir_vla_chain_signed_word_type(rows->type) ||
        rows->object < 0 ||
        mir.insns[3].object != rows->object ||
        !mir_machine_constant_equals(mir.insns[4].dst, 1024) ||
        mir.insns[5].src1 != rows->dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[5].immediate != '*' ||
        !mir_vla_chain_signed_word_type(mir.insns[5].type) ||
        allocation->src1 != mir.insns[5].dst ||
        allocation->memory_size != 1024 ||
        allocation->name[0] == '\0' ||
        !mir_declared_is_vla_object(allocation->name) ||
        mir.insns[7].name[0] == '\0' ||
        strcmp(mir.insns[7].name, allocation->name) ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[7].dst)
        return mir_machine_reject(
            "vla-constant-fill-sum-schedule", "allocation");

    if (store->opcode != MIR_STORE_INDIRECT ||
        load->opcode != MIR_LOAD_INDIRECT ||
        mir_vla_chain_collect_address(
            store->src1, &store_root, store_indices,
            store_strides, 10) != 10 ||
        mir_vla_chain_collect_address(
            load->src1, &load_root, load_indices,
            load_strides, 10) != 10 ||
        !mir_machine_same_location(store_root, &mir.insns[8]) ||
        !mir_machine_same_location(load_root, &mir.insns[8]) ||
        !mir_machine_same_location(store_root, load_root) ||
        !mir_machine_constant_equals(store->src2, 1))
        return mir_machine_reject(
            "vla-constant-fill-sum-schedule", "address");
    for (index = 0; index < 10; ++index) {
        int expected_stride = 1024 >> index;
        int prior;

        if (store_strides[index] != expected_stride ||
            load_strides[index] != expected_stride ||
            !mir_machine_same_location(
                store_indices[index], load_indices[index]) ||
            !mir_vla_chain_index_state_matches(
                store_indices[index], rows, index == 0, 2))
            return mir_machine_reject(
                "vla-constant-fill-sum-schedule", "index-state");
        for (prior = 0; prior < index; ++prior)
            if (mir_machine_same_location(
                    store_indices[index], store_indices[prior]))
                return mir_machine_reject(
                    "vla-constant-fill-sum-schedule",
                    "duplicate-index");
    }

    if (!mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[10]) ||
        mir.insns[10].src1 != mir.insns[9].dst ||
        !mir_vla_chain_index_value_matches(
            mir.insns[522].src1, &mir.insns[10]) ||
        mir.insns[522].src2 != load->dst ||
        mir.insns[522].immediate != '+' ||
        !mir_vla_chain_signed_word_type(mir.insns[522].type) ||
        !mir_vla_chain_signed_word_type(
            mir.insns[522].secondary_offset) ||
        !mir_machine_same_location(
            &mir.insns[524], &mir.insns[10]) ||
        mir.insns[524].src1 != mir.insns[522].dst ||
        !mir_machine_same_location(
            &mir.insns[595], &mir.insns[10]) ||
        mir.insns[596].src1 != mir.insns[595].dst)
        return mir_machine_reject(
            "vla-constant-fill-sum-schedule", "accumulator");

    plan->row_bytes = 1024;
    plan->elements_per_row = 512;
    return 1;
}

static int mir_vla_affine_control_edges_match(int shape)
{
    static const int edges_2d_for[8][2] = {
        {21, 62}, {33, 55}, {54, 25}, {61, 14},
        {74, 114}, {86, 107}, {106, 78}, {113, 66}
    };
    static const int edges_2d_while[8][2] = {
        {21, 64}, {33, 56}, {55, 25}, {63, 14},
        {76, 118}, {88, 110}, {109, 80}, {117, 68}
    };
    static const int edges_3d_for[12][2] = {
        {21, 90}, {33, 83}, {46, 76}, {75, 37}, {82, 25}, {89, 14},
        {103, 168}, {116, 161}, {129, 154},
        {153, 120}, {160, 107}, {167, 94}
    };
    static const int edges_3d_do[12][2] = {
        {67, 69}, {68, 29}, {79, 81}, {80, 21}, {91, 93}, {92, 14},
        {148, 150}, {149, 115}, {160, 162},
        {161, 106}, {172, 174}, {173, 97}
    };
    const int (*edges)[2];
    int count;
    int edge;

    if (shape == 0) {
        edges = edges_2d_for;
        count = 8;
    } else if (shape == 1) {
        edges = edges_2d_while;
        count = 8;
    } else if (shape == 2) {
        edges = edges_3d_for;
        count = 12;
    } else {
        edges = edges_3d_do;
        count = 12;
    }
    for (edge = 0; edge < count; ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return 0;
    return 1;
}

static int mir_vla_affine_collect(
    int value, const struct MirInsn **indices,
    int rank, int *coefficients)
{
    const struct MirInsn *definition = mir_definition(value);
    int index;

    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_BINARY &&
        definition->immediate == '+' &&
        mir_vla_chain_signed_word_type(definition->type))
        return mir_vla_affine_collect(
                   definition->src1, indices, rank, coefficients) &&
            mir_vla_affine_collect(
                   definition->src2, indices, rank, coefficients);
    for (index = 0; index < rank; ++index)
        if ((definition->opcode == MIR_LOAD ||
             definition->opcode == MIR_PHI) &&
            mir_machine_same_location(definition, indices[index])) {
            ++coefficients[index];
            return 1;
        }
    if (definition->opcode == MIR_BINARY &&
        definition->immediate == '*' &&
        mir_vla_chain_signed_word_type(definition->type)) {
        const struct MirInsn *left = mir_definition(definition->src1);
        const struct MirInsn *right = mir_definition(definition->src2);

        for (index = 0; index < rank; ++index) {
            if (left != NULL &&
                (left->opcode == MIR_LOAD ||
                 left->opcode == MIR_PHI) &&
                mir_machine_same_location(left, indices[index]) &&
                right != NULL && right->opcode == MIR_CONST &&
                right->immediate > 0 && right->immediate <= 32767) {
                coefficients[index] += (int)right->immediate;
                return 1;
            }
            if (right != NULL &&
                (right->opcode == MIR_LOAD ||
                 right->opcode == MIR_PHI) &&
                mir_machine_same_location(right, indices[index]) &&
                left != NULL && left->opcode == MIR_CONST &&
                left->immediate > 0 && left->immediate <= 32767) {
                coefficients[index] += (int)left->immediate;
                return 1;
            }
        }
    }
    return 0;
}

static int mir_match_vla_affine_fill_sum_schedule(
    struct MirVlaAffineFillSumSchedule *plan)
{
    const struct MirInsn *rows = &mir.insns[1];
    const struct MirInsn *allocation = &mir.insns[6];
    const struct MirInsn *store = NULL;
    const struct MirInsn *load = NULL;
    const struct MirInsn *store_root;
    const struct MirInsn *load_root;
    const struct MirInsn *store_indices[3];
    const struct MirInsn *load_indices[3];
    int store_strides[3];
    int load_strides[3];
    int coefficients[3] = {0, 0, 0};
    int opcode_counts[16] = {0};
    int expected_strides[3];
    int expected_bounds[3];
    int expected_coefficients[3];
    int accumulator_adds = 0;
    int accumulator_stores = 0;
    int accumulator_returns = 0;
    int accumulator_result = -1;
    int shape = -1;
    int instruction;
    int index;

    memset(plan, 0, sizeof(*plan));
    if (!mir.has_vla ||
        !mir_vla_chain_signed_word_type(mir.return_type))
        return 0;
    if (mir.count == 117 && mir_cfg_block_count() == 13 &&
        mir.local_bytes == 16)
        shape = 0;
    else if (mir.count == 121 && mir_cfg_block_count() == 13 &&
             mir.local_bytes == 14)
        shape = 1;
    else if (mir.count == 171 && mir_cfg_block_count() == 19 &&
             mir.local_bytes == 18)
        shape = 2;
    else if (mir.count == 177 && mir_cfg_block_count() == 19 &&
             mir.local_bytes == 16)
        shape = 3;
    else
        return 0;

    plan->rank = shape < 2 ? 2 : 3;
    plan->row_bytes = plan->rank == 2 ? 6 : 12;
    plan->elements_per_row = plan->rank == 2 ? 3 : 6;
    if (mir.insns[0].opcode != MIR_LABEL ||
        mir.insns[1].opcode != MIR_PARAM ||
        mir.insns[2].opcode != MIR_VLA_SAVE ||
        mir.insns[3].opcode != MIR_NOP ||
        mir.insns[4].opcode != MIR_CONST ||
        mir.insns[5].opcode != MIR_BINARY ||
        mir.insns[6].opcode != MIR_VLA_ALLOC ||
        mir.insns[7].opcode != MIR_LOAD ||
        mir.insns[8].opcode != MIR_STORE ||
        mir.insns[9].opcode != MIR_CONST ||
        mir.insns[10].opcode != MIR_STORE)
        return mir_machine_reject(
            "vla-affine-fill-sum-schedule", "positions");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int bucket;

        switch (insn->opcode) {
        case MIR_NOP: bucket = 0; break;
        case MIR_BINARY: bucket = 1; break;
        case MIR_CONST: bucket = 2; break;
        case MIR_LABEL: bucket = 3; break;
        case MIR_LOAD: bucket = 4; break;
        case MIR_STORE: bucket = 5; break;
        case MIR_BRANCH_FALSE: bucket = 6; break;
        case MIR_JUMP: bucket = 7; break;
        case MIR_PHI: bucket = 8; break;
        case MIR_PARAM: bucket = 9; break;
        case MIR_VLA_SAVE: bucket = 10; break;
        case MIR_VLA_ALLOC: bucket = 11; break;
        case MIR_STORE_INDIRECT:
            bucket = 12;
            store = insn;
            break;
        case MIR_LOAD_INDIRECT:
            bucket = 13;
            load = insn;
            break;
        case MIR_RETURN: bucket = 14; break;
        default:
            return mir_machine_reject(
                "vla-affine-fill-sum-schedule", "opcode");
        }
        ++opcode_counts[bucket];
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "vla-affine-fill-sum-schedule",
                "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0 ||
             insn->memory_size != 2))
            return mir_machine_reject(
                "vla-affine-fill-sum-schedule",
                "volatile-indirect-memory");
    }
    if (plan->rank == 2) {
        if (!((opcode_counts[0] == 28 && shape == 0) ||
              (opcode_counts[0] == 32 && shape == 1)) ||
            opcode_counts[1] != 20 ||
            opcode_counts[2] != 17 ||
            opcode_counts[3] != 13 ||
            opcode_counts[4] != 12 ||
            opcode_counts[5] != 11 ||
            opcode_counts[6] != 4 ||
            opcode_counts[7] != 4 ||
            opcode_counts[8] != 2)
            return mir_machine_reject(
                "vla-affine-fill-sum-schedule", "2d-shape");
        expected_strides[0] = 6;
        expected_strides[1] = 2;
        expected_bounds[0] = 0;
        expected_bounds[1] = 3;
        expected_coefficients[0] = 10;
        expected_coefficients[1] = 1;
    } else {
        if (!((opcode_counts[0] == 42 && shape == 2 &&
               opcode_counts[8] == 2) ||
              (opcode_counts[0] == 50 && shape == 3 &&
               opcode_counts[8] == 0)) ||
            opcode_counts[1] != 30 ||
            opcode_counts[2] != 26 ||
            opcode_counts[3] != 19 ||
            opcode_counts[4] != 19 ||
            opcode_counts[5] != 15 ||
            opcode_counts[6] != 6 ||
            opcode_counts[7] != 6)
            return mir_machine_reject(
                "vla-affine-fill-sum-schedule", "3d-shape");
        expected_strides[0] = 12;
        expected_strides[1] = 6;
        expected_strides[2] = 2;
        expected_bounds[0] = 0;
        expected_bounds[1] = 2;
        expected_bounds[2] = 3;
        expected_coefficients[0] = 100;
        expected_coefficients[1] = 10;
        expected_coefficients[2] = 1;
    }
    if (opcode_counts[9] != 1 ||
        opcode_counts[10] != 1 ||
        opcode_counts[11] != 1 ||
        opcode_counts[12] != 1 ||
        opcode_counts[13] != 1 ||
        opcode_counts[14] != 1 ||
        !mir_vla_affine_control_edges_match(shape))
        return mir_machine_reject(
            "vla-affine-fill-sum-schedule", "control-flow");

    if (!mir_machine_parameter_value_offset(
            rows->dst, &plan->rows_stack_offset) ||
        !mir_vla_chain_signed_word_type(rows->type) ||
        rows->object < 0 ||
        mir.insns[3].object != rows->object ||
        !mir_machine_constant_equals(
            mir.insns[4].dst, plan->row_bytes) ||
        mir.insns[5].src1 != rows->dst ||
        mir.insns[5].src2 != mir.insns[4].dst ||
        mir.insns[5].immediate != '*' ||
        allocation->src1 != mir.insns[5].dst ||
        allocation->memory_size != plan->row_bytes ||
        allocation->name[0] == '\0' ||
        !mir_declared_is_vla_object(allocation->name) ||
        mir.insns[7].name[0] == '\0' ||
        strcmp(mir.insns[7].name, allocation->name) ||
        !mir_machine_unobservable_local_store(&mir.insns[8]) ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        !mir_machine_unobservable_local_store(&mir.insns[10]) ||
        mir.insns[10].src1 != mir.insns[9].dst)
        return mir_machine_reject(
            "vla-affine-fill-sum-schedule", "allocation");

    if (store == NULL || load == NULL ||
        mir_vla_chain_collect_address(
            store->src1, &store_root, store_indices,
            store_strides, plan->rank) != plan->rank ||
        mir_vla_chain_collect_address(
            load->src1, &load_root, load_indices,
            load_strides, plan->rank) != plan->rank ||
        !mir_machine_same_location(store_root, &mir.insns[8]) ||
        !mir_machine_same_location(load_root, &mir.insns[8]) ||
        !mir_machine_same_location(store_root, load_root))
        return mir_machine_reject(
            "vla-affine-fill-sum-schedule", "address");
    for (index = 0; index < plan->rank; ++index) {
        int prior;

        if (store_strides[index] != expected_strides[index] ||
            load_strides[index] != expected_strides[index] ||
            !mir_machine_same_location(
                store_indices[index], load_indices[index]) ||
            !mir_vla_chain_index_state_matches(
                store_indices[index], rows, index == 0,
                expected_bounds[index]))
            return mir_machine_reject(
                "vla-affine-fill-sum-schedule", "index-state");
        for (prior = 0; prior < index; ++prior)
            if (mir_machine_same_location(
                    store_indices[index], store_indices[prior]))
                return mir_machine_reject(
                    "vla-affine-fill-sum-schedule",
                    "duplicate-index");
    }
    if (!mir_vla_affine_collect(
            store->src2, store_indices, plan->rank, coefficients))
        return mir_machine_reject(
            "vla-affine-fill-sum-schedule", "fill-expression");
    for (index = 0; index < plan->rank; ++index)
        if (coefficients[index] != expected_coefficients[index])
            return mir_machine_reject(
                "vla-affine-fill-sum-schedule",
                "fill-coefficient");

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_BINARY &&
            insn->immediate == '+' &&
            mir_vla_chain_signed_word_type(insn->type) &&
            mir_vla_chain_signed_word_type(
                insn->secondary_offset) &&
            (insn->src1 == load->dst || insn->src2 == load->dst)) {
            int other = insn->src1 == load->dst
                ? insn->src2 : insn->src1;

            if (mir_vla_chain_index_value_matches(
                    other, &mir.insns[10])) {
                int store_instruction;

                ++accumulator_adds;
                accumulator_result = insn->dst;
                for (store_instruction = 0;
                     store_instruction < mir.count;
                     ++store_instruction)
                    if (mir.insns[store_instruction].opcode == MIR_STORE &&
                        mir_machine_same_location(
                            &mir.insns[store_instruction],
                            &mir.insns[10]) &&
                        mir.insns[store_instruction].src1 == insn->dst)
                        ++accumulator_stores;
            }
        }
        if (insn->opcode == MIR_RETURN &&
            (mir_vla_chain_index_value_matches(
                 insn->src1, &mir.insns[10]) ||
             insn->src1 == accumulator_result))
            ++accumulator_returns;
    }
    if (accumulator_adds != 1 ||
        accumulator_stores != 1 ||
        accumulator_returns != 1)
        return mir_machine_reject(
            "vla-affine-fill-sum-schedule", "accumulator");
    return 1;
}

static void mir_emit_vla_affine_three_words(MirStream *out)
{
    mir_stream_puts("\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
          "\tinc de\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n"
          "\tinc de\n"
          "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tinc hl\n", out);
}

static void mir_emit_vla_affine_de_delta(MirStream *out, int delta)
{
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,%d\n\tadd hl,de\n\tex de,hl\n", delta);
    mir_stream_puts("\tpop hl\n", out);
}

static void mir_emit_vla_affine_fill_sum_schedule(
    MirStream *out, const struct MirVlaAffineFillSumSchedule *plan)
{
    int fill = new_label();
    int filled = new_label();
    int sum = new_label();
    int zero = new_label();
    int rows_offset = plan->rows_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            rows_offset, rows_offset + 1);
    mir_machine_emit_vla_allocate_rows(
        out, (unsigned long)plan->row_bytes);
    mir_stream_printf(out,
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "\tbit 7,b\n\tjp nz,L%d\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\tld hl,0\n\tadd hl,sp\n\tld de,0\n"
            "L%d:\n",
            rows_offset, rows_offset + 1,
            filled, filled, fill);
    mir_emit_vla_affine_three_words(out);
    mir_emit_vla_affine_de_delta(out, 8);
    if (plan->rank == 3) {
        mir_emit_vla_affine_three_words(out);
        mir_emit_vla_affine_de_delta(out, 88);
    }
    mir_stream_printf(out,
            "\tdec bc\n\tld a,b\n\tor c\n\tjp nz,L%d\n"
            "L%d:\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            fill, filled, rows_offset, rows_offset + 1);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->elements_per_row);
    mir_stream_printf(out,
            "\tld b,h\n\tld c,l\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\texx\n"
            "\tld hl,0\n\tadd hl,sp\n\tex de,hl\n"
            "\tld hl,0\n"
            "L%d:\n"
            "\tld a,(de)\n\tld c,a\n\tinc de\n"
            "\tld a,(de)\n\tld b,a\n\tinc de\n"
            "\tadd hl,bc\n"
            "\texx\n\tdec bc\n\tld a,b\n\tor c\n\texx\n"
            "\tjp nz,L%d\n"
            "\tld sp,ix\n\tpop ix\n\tret\n"
            "L%d:\n\tld hl,0\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            zero, sum, sum, zero);
}

static void mir_emit_vla_constant_fill_sum_schedule(
    MirStream *out, const struct MirVlaConstantFillSumSchedule *plan)
{
    int fill_ready = new_label();
    int sum = new_label();
    int zero = new_label();
    int rows_offset = plan->rows_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            rows_offset, rows_offset + 1);
    mir_machine_emit_vla_allocate_rows(
        out, (unsigned long)plan->row_bytes);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            rows_offset, rows_offset + 1);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->row_bytes);
    mir_stream_puts("\tld b,h\n\tld c,l\n"
          "\tld hl,0\n\tadd hl,sp\n"
          "\tld (hl),1\n\tinc hl\n\tld (hl),0\n\tdec hl\n"
          "\tdec bc\n\tdec bc\n"
          "\tld a,b\n\tor c\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n"
            "\tpush hl\n\tld de,2\n\tadd hl,de\n"
            "\tex de,hl\n\tpop hl\n\tldir\n"
            "L%d:\n",
            fill_ready, fill_ready);
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
            rows_offset, rows_offset + 1);
    mir_emit_mul_hl_const(
        out, (unsigned long)plan->elements_per_row);
    mir_stream_printf(out,
            "\tld b,h\n\tld c,l\n"
            "\tld a,b\n\tor c\n\tjp z,L%d\n"
            "\texx\n"
            "\tld hl,0\n\tadd hl,sp\n\tex de,hl\n"
            "\tld hl,0\n"
            "L%d:\n"
            "\tld a,(de)\n\tld c,a\n\tinc de\n"
            "\tld a,(de)\n\tld b,a\n\tinc de\n"
            "\tadd hl,bc\n"
            "\texx\n\tdec bc\n\tld a,b\n\tor c\n\texx\n"
            "\tjp nz,L%d\n"
            "\tld sp,ix\n\tpop ix\n\tret\n"
            "L%d:\n\tld hl,0\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            zero, sum, sum, zero);
}

static int mir_scanner_signed_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_scanner_unsigned_byte_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_CHAR &&
        (type & TYPE_UNSIGNED) != 0 &&
        type_size(type) == 1;
}

static int mir_scanner_char_type(int type)
{
    return type_ptr_depth(type) == 0 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_CHAR &&
        type_size(type) == 1;
}

static int mir_scanner_char_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_CHAR &&
        type_size(type) == 2;
}

static int mir_scanner_word_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
        !type_is_float(type) &&
        (type & 15) == TYPE_INT &&
        (type & TYPE_UNSIGNED) == 0 &&
        type_size(type) == 2;
}

static int mir_match_printable_byte_sanitize_schedule(
    struct MirPrintableByteSanitizeSchedule *plan)
{
    static const unsigned char expected_opcodes[61] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *initial_store = &mir.insns[4];
    const struct MirInsn *index_phi = &mir.insns[7];
    long lower;
    long upper;
    long replacement;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if ((size_t)mir.count != sizeof(expected_opcodes) ||
        mir_cfg_block_count() != 12 || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "printable-byte-sanitize-schedule", "opcodes");
        if ((insn->opcode == MIR_PARAM ||
             insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "printable-byte-sanitize-schedule",
                "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0))
            return mir_machine_reject(
                "printable-byte-sanitize-schedule",
                "volatile-indirect-memory");
    }
    if (!mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_scanner_char_pointer_type(pointer->type) ||
        mir_machine_pointee_is_volatile(pointer) ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(initial_store) ||
        initial_store->src1 != mir.insns[2].dst ||
        initial_store->object < 0 ||
        index_phi->object != initial_store->object ||
        !mir_machine_same_location(index_phi, initial_store) ||
        index_phi->src1 != mir.insns[2].dst ||
        index_phi->src2 != mir.insns[57].dst ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[54].label ||
        !mir_scanner_signed_word_type(index_phi->type))
        return mir_machine_reject(
            "printable-byte-sanitize-schedule", "loop-state");
    if (mir.insns[10].src1 != pointer->dst ||
        mir.insns[10].src2 != index_phi->dst ||
        mir.insns[10].immediate != 1 ||
        mir.insns[10].memory_size != 1 ||
        mir.insns[11].src1 != mir.insns[10].dst ||
        mir.insns[11].memory_size != 1 ||
        !mir_scanner_char_type(mir.insns[11].type) ||
        mir.insns[12].src1 != mir.insns[11].dst ||
        mir.insns[12].label != mir.insns[60].label)
        return mir_machine_reject(
            "printable-byte-sanitize-schedule", "terminator");
    if (mir.insns[15].src1 != pointer->dst ||
        mir.insns[15].src2 != index_phi->dst ||
        mir.insns[15].immediate != 1 ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].memory_size != 1 ||
        mir.insns[17].src1 != mir.insns[16].dst ||
        !mir_scanner_unsigned_byte_type(mir.insns[17].type) ||
        mir.insns[19].src1 != mir.insns[17].dst ||
        !mir_scanner_signed_word_type(mir.insns[19].type) ||
        !mir_machine_evaluate_constant(
            mir.insns[18].dst, &lower, 0) ||
        lower <= 0 || lower > 255 ||
        mir.insns[20].src1 != mir.insns[19].dst ||
        mir.insns[20].src2 != mir.insns[18].dst ||
        mir.insns[20].immediate != '<' ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].label != mir.insns[25].label)
        return mir_machine_reject(
            "printable-byte-sanitize-schedule", "lower-bound");
    if (mir.insns[28].src1 != pointer->dst ||
        mir.insns[28].src2 != index_phi->dst ||
        mir.insns[28].immediate != 1 ||
        mir.insns[29].src1 != mir.insns[28].dst ||
        mir.insns[29].memory_size != 1 ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        !mir_scanner_unsigned_byte_type(mir.insns[30].type) ||
        mir.insns[32].src1 != mir.insns[30].dst ||
        !mir_scanner_signed_word_type(mir.insns[32].type) ||
        !mir_machine_evaluate_constant(
            mir.insns[31].dst, &upper, 0) ||
        upper < lower || upper >= 255 ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].src2 != mir.insns[31].dst ||
        mir.insns[33].immediate != '>' ||
        mir.insns[34].src1 != mir.insns[33].dst ||
        mir.insns[34].label != mir.insns[38].label)
        return mir_machine_reject(
            "printable-byte-sanitize-schedule", "upper-bound");
    if (!mir_machine_constant_equals(mir.insns[23].dst, 1) ||
        mir.insns[24].label != mir.insns[44].label ||
        !mir_machine_constant_equals(mir.insns[36].dst, 1) ||
        mir.insns[37].label != mir.insns[40].label ||
        !mir_machine_constant_equals(mir.insns[39].dst, 0) ||
        mir.insns[41].src1 != mir.insns[36].dst ||
        mir.insns[41].src2 != mir.insns[39].dst ||
        mir.insns[41].phi_pred1 != mir.insns[35].label ||
        mir.insns[41].phi_pred2 != mir.insns[38].label ||
        mir.insns[43].label != mir.insns[44].label ||
        mir.insns[45].src1 != mir.insns[23].dst ||
        mir.insns[45].src2 != mir.insns[41].dst ||
        mir.insns[45].phi_pred1 != mir.insns[22].label ||
        mir.insns[45].phi_pred2 != mir.insns[42].label ||
        mir.insns[46].src1 != mir.insns[45].dst ||
        mir.insns[46].label != mir.insns[53].label)
        return mir_machine_reject(
            "printable-byte-sanitize-schedule", "condition-phis");
    if (mir.insns[49].src1 != pointer->dst ||
        mir.insns[49].src2 != index_phi->dst ||
        mir.insns[49].immediate != 1 ||
        !mir_machine_evaluate_constant(
            mir.insns[51].dst, &replacement, 0) ||
        replacement < 0 || replacement > 255 ||
        mir.insns[52].src1 != mir.insns[49].dst ||
        mir.insns[52].src2 != mir.insns[51].dst ||
        mir.insns[52].memory_size != 1 ||
        !mir_machine_constant_equals(mir.insns[56].dst, 1) ||
        mir.insns[57].src1 != index_phi->dst ||
        mir.insns[57].src2 != mir.insns[56].dst ||
        mir.insns[57].immediate != '+' ||
        !mir_machine_same_location(
            &mir.insns[58], initial_store) ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[59].label != mir.insns[5].label)
        return mir_machine_reject(
            "printable-byte-sanitize-schedule", "store-step");
    plan->lower_bound = (int)lower;
    plan->upper_bound = (int)upper;
    plan->replacement = (int)replacement;
    return 1;
}

static void mir_emit_printable_byte_sanitize_schedule(
    MirStream *out, const struct MirPrintableByteSanitizeSchedule *plan)
{
    int loop = new_label();
    int replace = new_label();
    int keep = new_label();

    mir_stream_printf(out, "%s\n", MIR_EXACT_KERNEL_MARKER);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "L%d:\n\tld a,(de)\n\tor a\n\tret z\n"
            "\tcp %d\n\tjp c,L%d\n"
            "\tcp %d\n\tjp c,L%d\n"
            "L%d:\n\tld a,%d\n\tld (de),a\n"
            "L%d:\n\tinc de\n\tjp L%d\n",
            plan->pointer_stack_offset,
            loop, plan->lower_bound, replace,
            plan->upper_bound + 1, keep,
            replace, plan->replacement,
            keep, loop);
}

static int mir_match_invariant_byte_sum_schedule(
    struct MirInvariantByteSumSchedule *plan)
{
    static const int expected_opcodes[35] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_NOP,
        MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_PHI, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_UNARY, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP,
        MIR_RETURN
    };
    const struct MirInsn *pointer = &mir.insns[1];
    const struct MirInsn *total_store = &mir.insns[4];
    const struct MirInsn *index_store = &mir.insns[7];
    const struct MirInsn *total_phi = &mir.insns[10];
    const struct MirInsn *index_phi = &mir.insns[11];
    long count;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 35 || mir_cfg_block_count() != 4 ||
        mir.has_vla || mir.local_bytes != 3 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_scanner_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "invariant-byte-sum-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "invariant-byte-sum-schedule",
                "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0))
            return mir_machine_reject(
                "invariant-byte-sum-schedule",
                "volatile-indirect-memory");
    }

    if (!mir_machine_parameter_value_offset(
            pointer->dst, &plan->pointer_stack_offset) ||
        !mir_scanner_char_pointer_type(pointer->type) ||
        mir_machine_pointee_is_volatile(pointer) ||
        pointer->object < 0 ||
        !mir_machine_constant_equals(mir.insns[2].dst, 0) ||
        !mir_machine_unobservable_local_store(total_store) ||
        total_store->src1 != mir.insns[2].dst ||
        total_store->object < 0 ||
        !mir_machine_constant_equals(mir.insns[6].dst, 0) ||
        !mir_machine_unobservable_local_store(index_store) ||
        index_store->src1 != mir.insns[6].dst ||
        index_store->object < 0 ||
        index_store->object == total_store->object)
        return mir_machine_reject(
            "invariant-byte-sum-schedule", "initial-state");

    if (total_phi->object != total_store->object ||
        index_phi->object != index_store->object ||
        !mir_machine_same_location(total_phi, total_store) ||
        !mir_machine_same_location(index_phi, index_store) ||
        total_phi->src1 != mir.insns[2].dst ||
        total_phi->src2 != mir.insns[23].dst ||
        index_phi->src1 != mir.insns[6].dst ||
        index_phi->src2 != mir.insns[29].dst ||
        total_phi->phi_pred1 != mir.insns[0].label ||
        total_phi->phi_pred2 != mir.insns[26].label ||
        index_phi->phi_pred1 != mir.insns[0].label ||
        index_phi->phi_pred2 != mir.insns[26].label ||
        !mir_scanner_signed_word_type(total_phi->type) ||
        !mir_scanner_unsigned_byte_type(index_phi->type))
        return mir_machine_reject(
            "invariant-byte-sum-schedule", "loop-state");

    if (!mir_machine_evaluate_constant(
            mir.insns[13].dst, &count, 0) ||
        count <= 0 || count > 255 ||
        mir.insns[14].src1 != index_phi->dst ||
        !mir_scanner_signed_word_type(mir.insns[14].type) ||
        mir.insns[15].src1 != mir.insns[14].dst ||
        mir.insns[15].src2 != mir.insns[13].dst ||
        mir.insns[15].immediate != '<' ||
        !mir_scanner_signed_word_type(mir.insns[15].type) ||
        !mir_scanner_signed_word_type(
            mir.insns[15].secondary_offset) ||
        mir.insns[16].src1 != mir.insns[15].dst ||
        mir.insns[16].label != mir.insns[32].label)
        return mir_machine_reject(
            "invariant-byte-sum-schedule", "bound");
    plan->count = (int)count;

    if (mir.insns[18].object != pointer->object ||
        mir.insns[19].object != index_store->object ||
        mir.insns[20].src1 != pointer->dst ||
        mir.insns[20].src2 != index_phi->dst ||
        mir.insns[20].immediate != 1 ||
        mir.insns[20].memory_size != 1 ||
        !mir_scanner_char_pointer_type(mir.insns[20].type) ||
        mir.insns[21].src1 != mir.insns[20].dst ||
        mir.insns[21].memory_size != 1 ||
        !mir_scanner_char_type(mir.insns[21].type) ||
        mir.insns[22].src1 != mir.insns[21].dst ||
        !mir_scanner_signed_word_type(mir.insns[22].type) ||
        mir.insns[23].src1 != total_phi->dst ||
        mir.insns[23].src2 != mir.insns[22].dst ||
        mir.insns[23].immediate != '+' ||
        !mir_scanner_signed_word_type(mir.insns[23].type) ||
        !mir_machine_same_location(&mir.insns[25], total_store) ||
        mir.insns[25].src1 != mir.insns[23].dst)
        return mir_machine_reject(
            "invariant-byte-sum-schedule", "load-accumulate");
    plan->element_unsigned =
        (mir.insns[21].type & TYPE_UNSIGNED) != 0;

    if (!mir_machine_constant_equals(mir.insns[28].dst, 1) ||
        !mir_scanner_unsigned_byte_type(mir.insns[28].type) ||
        mir.insns[29].src1 != index_phi->dst ||
        mir.insns[29].src2 != mir.insns[28].dst ||
        mir.insns[29].immediate != '+' ||
        !mir_scanner_unsigned_byte_type(mir.insns[29].type) ||
        !mir_machine_same_location(&mir.insns[30], index_store) ||
        mir.insns[30].src1 != mir.insns[29].dst ||
        mir.insns[31].label != mir.insns[8].label ||
        mir.insns[33].object != total_store->object ||
        mir.insns[34].src1 != total_phi->dst)
        return mir_machine_reject(
            "invariant-byte-sum-schedule", "increment-return");
    return 1;
}

static void mir_emit_invariant_byte_sum_schedule(
    MirStream *out, const struct MirInvariantByteSumSchedule *plan)
{
    int loop = new_label();

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,0\n"
            "\texx\n\tld bc,%d\n\texx\n"
            "L%d:\n\tld a,(de)\n\tinc de\n\tld c,a\n",
            plan->pointer_stack_offset, plan->count, loop);
    if (plan->element_unsigned)
        mir_stream_puts("\tld b,0\n", out);
    else
        mir_stream_puts("\trlca\n\tsbc a,a\n\tld b,a\n", out);
    mir_stream_printf(out,
            "\tadd hl,bc\n"
            "\texx\n\tdec bc\n\tld a,b\n\tor c\n\texx\n"
            "\tjp nz,L%d\n\tret\n",
            loop);
}

static int mir_sliding_max_queue_address(
    int instruction, int *offset_out)
{
    const struct MirInsn *address = &mir.insns[instruction];
    int memory_type;
    int memory_storage;
    int memory_offset;

    if (address->opcode != MIR_ADDRESS ||
        !mir_machine_named_nonvolatile(address) ||
        (address->memory_flags & (1 | 8)) != 0 ||
        !mir_scalar_memory_location(
            address, &memory_type, &memory_storage, &memory_offset) ||
        memory_storage != SC_LOCAL || memory_offset != -16 ||
        !mir_scanner_word_pointer_type(address->type))
        return 0;
    *offset_out = memory_offset;
    return 1;
}

static int mir_match_sliding_maximum_schedule(
    struct MirSlidingMaximumSchedule *plan)
{
    static const int expected_opcodes[160] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_PARAM, MIR_CONST,
        MIR_STORE, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_PHI, MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_NOP, MIR_ADDRESS,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_RETURN
    };
    const struct MirInsn *input = &mir.insns[1];
    const struct MirInsn *length = &mir.insns[2];
    const struct MirInsn *window = &mir.insns[3];
    const struct MirInsn *output = &mir.insns[4];
    const struct MirInsn *head_store = &mir.insns[6];
    const struct MirInsn *tail_store = &mir.insns[8];
    const struct MirInsn *count_store = &mir.insns[10];
    const struct MirInsn *index_store = &mir.insns[19];
    const struct MirInsn *outer_tail_phi = &mir.insns[26];
    const struct MirInsn *outer_index_phi = &mir.insns[28];
    const struct MirInsn *inner_tail_phi = &mir.insns[83];
    int queue_offsets[4];
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 160 || mir_cfg_block_count() != 17 ||
        mir.has_vla || mir.local_bytes != 28 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_scanner_signed_word_type(mir.return_type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "sliding-maximum-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM ||
             insn->opcode == MIR_ADDRESS) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "sliding-maximum-schedule",
                "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->bit_width != 0 ||
             insn->memory_size != 2))
            return mir_machine_reject(
                "sliding-maximum-schedule",
                "volatile-indirect-memory");
    }

    if (!mir_machine_parameter_value_offset(
            input->dst, &plan->input_stack_offset) ||
        !mir_machine_parameter_value_offset(
            length->dst, &plan->length_stack_offset) ||
        !mir_machine_parameter_value_offset(
            window->dst, &plan->window_stack_offset) ||
        !mir_machine_parameter_value_offset(
            output->dst, &plan->output_stack_offset) ||
        plan->length_stack_offset != plan->input_stack_offset + 2 ||
        plan->window_stack_offset != plan->length_stack_offset + 2 ||
        plan->output_stack_offset != plan->window_stack_offset + 2 ||
        !mir_scanner_word_pointer_type(input->type) ||
        !mir_scanner_word_pointer_type(output->type) ||
        !mir_scanner_signed_word_type(length->type) ||
        !mir_scanner_signed_word_type(window->type) ||
        mir_machine_pointee_is_volatile(input) ||
        mir_machine_pointee_is_volatile(output) ||
        input->object < 0 || length->object < 0 ||
        window->object < 0 || output->object < 0 ||
        input->object == output->object)
        return mir_machine_reject(
            "sliding-maximum-schedule", "parameters");

    if (!mir_machine_constant_equals(mir.insns[5].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[7].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[17].dst, 0) ||
        !mir_machine_unobservable_local_store(head_store) ||
        !mir_machine_unobservable_local_store(tail_store) ||
        !mir_machine_unobservable_local_store(count_store) ||
        !mir_machine_unobservable_local_store(index_store) ||
        head_store->src1 != mir.insns[5].dst ||
        tail_store->src1 != mir.insns[7].dst ||
        count_store->src1 != mir.insns[9].dst ||
        index_store->src1 != mir.insns[17].dst ||
        head_store->object < 0 || tail_store->object < 0 ||
        count_store->object < 0 || index_store->object < 0 ||
        head_store->object == tail_store->object ||
        head_store->object == count_store->object ||
        head_store->object == index_store->object ||
        tail_store->object == count_store->object ||
        tail_store->object == index_store->object ||
        count_store->object == index_store->object)
        return mir_machine_reject(
            "sliding-maximum-schedule", "initial-state");

    if (outer_tail_phi->object != tail_store->object ||
        outer_index_phi->object != index_store->object ||
        inner_tail_phi->object != tail_store->object ||
        !mir_machine_same_location(outer_tail_phi, tail_store) ||
        !mir_machine_same_location(outer_index_phi, index_store) ||
        !mir_machine_same_location(inner_tail_phi, tail_store) ||
        outer_tail_phi->src1 != mir.insns[7].dst ||
        outer_tail_phi->src2 != mir.insns[124].dst ||
        outer_tail_phi->phi_pred1 != mir.insns[0].label ||
        outer_tail_phi->phi_pred2 != mir.insns[151].label ||
        outer_index_phi->src1 != mir.insns[17].dst ||
        outer_index_phi->src2 != mir.insns[154].dst ||
        outer_index_phi->phi_pred1 != mir.insns[0].label ||
        outer_index_phi->phi_pred2 != mir.insns[151].label ||
        inner_tail_phi->src1 != outer_tail_phi->dst ||
        inner_tail_phi->src2 != mir.insns[116].dst ||
        inner_tail_phi->phi_pred1 != mir.insns[76].label ||
        inner_tail_phi->phi_pred2 != mir.insns[118].label ||
        !mir_scanner_signed_word_type(outer_tail_phi->type) ||
        !mir_scanner_signed_word_type(outer_index_phi->type) ||
        !mir_scanner_signed_word_type(inner_tail_phi->type))
        return mir_machine_reject(
            "sliding-maximum-schedule", "phis");

    if (mir.insns[32].src1 != outer_index_phi->dst ||
        mir.insns[32].src2 != length->dst ||
        mir.insns[32].immediate != '<' ||
        !mir_scanner_signed_word_type(mir.insns[32].type) ||
        mir.insns[33].src1 != mir.insns[32].dst ||
        mir.insns[33].label != mir.insns[157].label ||
        mir.insns[37].src1 != outer_index_phi->dst ||
        mir.insns[37].src2 != window->dst ||
        mir.insns[37].immediate != '-' ||
        !mir_machine_constant_equals(mir.insns[38].dst, 1) ||
        mir.insns[39].src1 != mir.insns[37].dst ||
        mir.insns[39].src2 != mir.insns[38].dst ||
        mir.insns[39].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[40], &mir.insns[16]) ||
        mir.insns[40].src1 != mir.insns[39].dst)
        return mir_machine_reject(
            "sliding-maximum-schedule", "outer-loop");

    if (mir.insns[51].object != head_store->object ||
        mir.insns[53].src1 != mir.insns[51].dst ||
        mir.insns[53].src2 != outer_tail_phi->dst ||
        mir.insns[53].immediate != '<' ||
        !mir_scanner_signed_word_type(mir.insns[53].type) ||
        !mir_scanner_signed_word_type(
            mir.insns[53].secondary_offset) ||
        mir.insns[54].src1 != mir.insns[53].dst ||
        mir.insns[54].label != mir.insns[65].label ||
        !mir_sliding_max_queue_address(55, &queue_offsets[0]) ||
        mir.insns[56].object != head_store->object ||
        mir.insns[57].src1 != mir.insns[55].dst ||
        mir.insns[57].src2 != mir.insns[56].dst ||
        mir.insns[57].immediate != 2 ||
        mir.insns[57].memory_size != 2 ||
        mir.insns[58].src1 != mir.insns[57].dst ||
        mir.insns[58].memory_size != 2 ||
        !mir_scanner_signed_word_type(mir.insns[58].type) ||
        mir.insns[60].src1 != mir.insns[58].dst ||
        mir.insns[60].src2 != mir.insns[39].dst ||
        mir.insns[60].immediate != '<' ||
        !mir_scanner_signed_word_type(mir.insns[60].type) ||
        !mir_scanner_signed_word_type(
            mir.insns[60].secondary_offset) ||
        mir.insns[61].src1 != mir.insns[60].dst ||
        mir.insns[61].label != mir.insns[65].label ||
        !mir_machine_constant_equals(mir.insns[63].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[66].dst, 0) ||
        mir.insns[64].label != mir.insns[67].label ||
        mir.insns[68].src1 != mir.insns[63].dst ||
        mir.insns[68].src2 != mir.insns[66].dst ||
        mir.insns[68].phi_pred1 != mir.insns[62].label ||
        mir.insns[68].phi_pred2 != mir.insns[65].label ||
        mir.insns[69].src1 != mir.insns[68].dst ||
        mir.insns[69].label != mir.insns[76].label ||
        !mir_machine_constant_equals(mir.insns[71].dst, 1) ||
        mir.insns[72].src1 != mir.insns[70].dst ||
        mir.insns[72].src2 != mir.insns[71].dst ||
        mir.insns[72].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[73], head_store) ||
        mir.insns[73].src1 != mir.insns[72].dst ||
        mir.insns[75].label != mir.insns[41].label)
        return mir_machine_reject(
            "sliding-maximum-schedule", "front-prune");

    if (mir.insns[87].object != head_store->object ||
        mir.insns[89].src1 != mir.insns[87].dst ||
        mir.insns[89].src2 != inner_tail_phi->dst ||
        mir.insns[89].immediate != '<' ||
        !mir_scanner_signed_word_type(mir.insns[89].type) ||
        !mir_scanner_signed_word_type(
            mir.insns[89].secondary_offset) ||
        mir.insns[90].src1 != mir.insns[89].dst ||
        mir.insns[90].label != mir.insns[109].label ||
        !mir_sliding_max_queue_address(92, &queue_offsets[1]) ||
        queue_offsets[1] != queue_offsets[0] ||
        !mir_machine_constant_equals(mir.insns[94].dst, 1) ||
        mir.insns[95].src1 != inner_tail_phi->dst ||
        mir.insns[95].src2 != mir.insns[94].dst ||
        mir.insns[95].immediate != '-' ||
        mir.insns[96].src1 != mir.insns[92].dst ||
        mir.insns[96].src2 != mir.insns[95].dst ||
        mir.insns[96].immediate != 2 ||
        mir.insns[97].src1 != mir.insns[96].dst ||
        !mir_scanner_signed_word_type(mir.insns[97].type) ||
        mir.insns[98].src1 != input->dst ||
        mir.insns[98].src2 != mir.insns[97].dst ||
        mir.insns[98].immediate != 2 ||
        mir.insns[99].src1 != mir.insns[98].dst ||
        !mir_scanner_signed_word_type(mir.insns[99].type) ||
        mir.insns[102].src1 != input->dst ||
        mir.insns[102].src2 != outer_index_phi->dst ||
        mir.insns[102].immediate != 2 ||
        mir.insns[103].src1 != mir.insns[102].dst ||
        !mir_scanner_signed_word_type(mir.insns[103].type) ||
        mir.insns[104].src1 != mir.insns[99].dst ||
        mir.insns[104].src2 != mir.insns[103].dst ||
        mir.insns[104].immediate != TOK_LE ||
        !mir_scanner_signed_word_type(mir.insns[104].type) ||
        !mir_scanner_signed_word_type(
            mir.insns[104].secondary_offset) ||
        mir.insns[105].src1 != mir.insns[104].dst ||
        mir.insns[105].label != mir.insns[109].label ||
        !mir_machine_constant_equals(mir.insns[107].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[110].dst, 0) ||
        mir.insns[108].label != mir.insns[111].label ||
        mir.insns[112].src1 != mir.insns[107].dst ||
        mir.insns[112].src2 != mir.insns[110].dst ||
        mir.insns[112].phi_pred1 != mir.insns[106].label ||
        mir.insns[112].phi_pred2 != mir.insns[109].label ||
        mir.insns[113].src1 != mir.insns[112].dst ||
        mir.insns[113].label != mir.insns[120].label ||
        !mir_machine_constant_equals(mir.insns[115].dst, 1) ||
        mir.insns[116].src1 != inner_tail_phi->dst ||
        mir.insns[116].src2 != mir.insns[115].dst ||
        mir.insns[116].immediate != '-' ||
        !mir_machine_same_location(&mir.insns[117], tail_store) ||
        mir.insns[117].src1 != mir.insns[116].dst ||
        mir.insns[119].label != mir.insns[77].label)
        return mir_machine_reject(
            "sliding-maximum-schedule", "back-prune");

    if (!mir_sliding_max_queue_address(121, &queue_offsets[2]) ||
        queue_offsets[2] != queue_offsets[0] ||
        !mir_machine_constant_equals(mir.insns[123].dst, 1) ||
        mir.insns[124].src1 != inner_tail_phi->dst ||
        mir.insns[124].src2 != mir.insns[123].dst ||
        mir.insns[124].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[125], tail_store) ||
        mir.insns[125].src1 != mir.insns[124].dst ||
        mir.insns[126].src1 != mir.insns[121].dst ||
        mir.insns[126].src2 != inner_tail_phi->dst ||
        mir.insns[126].immediate != 2 ||
        mir.insns[128].src1 != mir.insns[126].dst ||
        mir.insns[128].src2 != outer_index_phi->dst ||
        mir.insns[128].memory_size != 2)
        return mir_machine_reject(
            "sliding-maximum-schedule", "queue-push");

    if (!mir_machine_constant_equals(mir.insns[131].dst, 1) ||
        mir.insns[132].src1 != window->dst ||
        mir.insns[132].src2 != mir.insns[131].dst ||
        mir.insns[132].immediate != '-' ||
        mir.insns[133].src1 != outer_index_phi->dst ||
        mir.insns[133].src2 != mir.insns[132].dst ||
        mir.insns[133].immediate != TOK_GE ||
        !mir_scanner_signed_word_type(mir.insns[133].type) ||
        !mir_scanner_signed_word_type(
            mir.insns[133].secondary_offset) ||
        mir.insns[134].src1 != mir.insns[133].dst ||
        mir.insns[134].label != mir.insns[149].label ||
        mir.insns[136].object != count_store->object ||
        !mir_machine_constant_equals(mir.insns[137].dst, 1) ||
        mir.insns[138].src1 != mir.insns[136].dst ||
        mir.insns[138].src2 != mir.insns[137].dst ||
        mir.insns[138].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[139], count_store) ||
        mir.insns[139].src1 != mir.insns[138].dst ||
        mir.insns[140].src1 != output->dst ||
        mir.insns[140].src2 != mir.insns[136].dst ||
        mir.insns[140].immediate != 2 ||
        !mir_sliding_max_queue_address(142, &queue_offsets[3]) ||
        queue_offsets[3] != queue_offsets[0] ||
        mir.insns[143].object != head_store->object ||
        mir.insns[144].src1 != mir.insns[142].dst ||
        mir.insns[144].src2 != mir.insns[143].dst ||
        mir.insns[144].immediate != 2 ||
        mir.insns[145].src1 != mir.insns[144].dst ||
        mir.insns[146].src1 != input->dst ||
        mir.insns[146].src2 != mir.insns[145].dst ||
        mir.insns[146].immediate != 2 ||
        mir.insns[147].src1 != mir.insns[146].dst ||
        mir.insns[148].src1 != mir.insns[140].dst ||
        mir.insns[148].src2 != mir.insns[147].dst)
        return mir_machine_reject(
            "sliding-maximum-schedule", "output");

    if (!mir_machine_constant_equals(mir.insns[153].dst, 1) ||
        mir.insns[154].src1 != outer_index_phi->dst ||
        mir.insns[154].src2 != mir.insns[153].dst ||
        mir.insns[154].immediate != '+' ||
        !mir_machine_same_location(&mir.insns[155], index_store) ||
        mir.insns[155].src1 != mir.insns[154].dst ||
        mir.insns[156].label != mir.insns[20].label ||
        !mir_machine_same_location(&mir.insns[158], count_store) ||
        mir.insns[159].src1 != mir.insns[158].dst)
        return mir_machine_reject(
            "sliding-maximum-schedule", "increment-return");
    plan->queue_count = 8;
    if (-queue_offsets[0] < plan->queue_count * 2)
        return mir_machine_reject(
            "sliding-maximum-schedule", "queue-layout");
    return 1;
}

static void mir_emit_sliding_maximum_schedule(
    MirStream *out, const struct MirSlidingMaximumSchedule *plan)
{
    int outer = new_label();
    int first_prune = new_label();
    int second_prune = new_label();
    int second_loop = new_label();
    int second_done = new_label();
    int output_done = new_label();
    int done = new_label();
    int head_incremented = new_label();
    int tail_decrement = new_label();
    int tail_decremented = new_label();
    int tail_incremented = new_label();
    int count_incremented = new_label();
    int index_incremented = new_label();
    int input_offset = plan->input_stack_offset + 2;
    int length_offset = plan->length_stack_offset + 2;
    int window_offset = plan->window_stack_offset + 2;
    int output_offset = plan->output_stack_offset + 2;

    mir_stream_puts("\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-28\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_puts("\tpush ix\n\tpop hl\n\tld de,-16\n\tadd hl,de\n"
          "\tld (ix-18),l\n\tld (ix-17),h\n"
          "\tld hl,0\n"
          "\tld (ix-20),l\n\tld (ix-19),h\n"
          "\tld (ix-22),l\n\tld (ix-21),h\n"
          "\tld (ix-24),l\n\tld (ix-23),h\n"
          "\tld (ix-26),l\n\tld (ix-25),h\n", out);

    mir_stream_printf(out,
            "L%d:\n"
            "\tld l,(ix-26)\n\tld h,(ix-25)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
            "\tld l,(ix-26)\n\tld h,(ix-25)\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n"
            "\tor a\n\tsbc hl,de\n\tinc hl\n"
            "\tld (ix-28),l\n\tld (ix-27),h\n",
            outer, length_offset, length_offset + 1, done,
            window_offset, window_offset + 1);

    mir_stream_printf(out,
            "L%d:\n"
            "\tld l,(ix-20)\n\tld h,(ix-19)\n"
            "\tld e,(ix-22)\n\tld d,(ix-21)\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
            "\tld l,(ix-18)\n\tld h,(ix-17)\n\tpush hl\n"
            "\tld l,(ix-20)\n\tld h,(ix-19)\n\tadd hl,hl\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tld e,(ix-28)\n\tld d,(ix-27)\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
            "\tinc (ix-20)\n\tjp nz,L%d\n\tinc (ix-19)\n"
            "L%d:\n\tjp L%d\n",
            first_prune, second_prune, second_prune,
            head_incremented, head_incremented, first_prune);

    mir_stream_printf(out,
            "L%d:\n"
            ";@dcc.reg claim=bc scope=loop sym=mir kind=mir val=0\n"
            "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
            "L%d:\n"
            "\tld l,(ix-20)\n\tld h,(ix-19)\n"
            "\tld e,(ix-22)\n\tld d,(ix-21)\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
            "\tld l,(ix-18)\n\tld h,(ix-17)\n\tpush hl\n"
            "\tld l,(ix-22)\n\tld h,(ix-21)\n\tdec hl\n"
            "\tadd hl,hl\n\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tsla e\n\trl d\n"
            "\tld l,c\n\tld h,b\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tex de,hl\n"
            "\tpush hl\n"
            "\tld l,(ix-26)\n\tld h,(ix-25)\n\tadd hl,hl\n"
            "\tld d,b\n\tld e,c\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpop hl\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n"
            "\tjp z,L%d\n\tjp c,L%d\n\tjp L%d\n"
            "L%d:\n"
            "\tld a,(ix-22)\n\tdec (ix-22)\n\tor a\n"
            "\tjp nz,L%d\n\tdec (ix-21)\n"
            "L%d:\n\tjp L%d\n"
            "L%d:\n;@dcc.reg free=bc\n",
            second_prune,
            input_offset, input_offset + 1,
            second_loop, second_done,
            tail_decrement, tail_decrement, second_done,
            tail_decrement, tail_decremented,
            tail_decremented, second_loop, second_done);

    mir_stream_puts("\tld l,(ix-18)\n\tld h,(ix-17)\n\tpush hl\n"
          "\tld l,(ix-22)\n\tld h,(ix-21)\n\tpush hl\n"
          "\tinc (ix-22)\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tinc (ix-21)\nL%d:\n"
            "\tpop hl\n\tadd hl,hl\n\tex de,hl\n\tpop hl\n"
            "\tadd hl,de\n"
            "\tld e,(ix-26)\n\tld d,(ix-25)\n"
            "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "\tld l,(ix-26)\n\tld h,(ix-25)\n\tpush hl\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tdec hl\n"
            "\tex de,hl\n\tpop hl\n"
            "\tld a,h\n\txor 128\n\tld h,a\n"
            "\tld a,d\n\txor 128\n\tld d,a\n"
            "\tor a\n\tsbc hl,de\n\tjp c,L%d\n",
            tail_incremented, tail_incremented,
            window_offset, window_offset + 1, output_done);

    mir_stream_puts("\tld l,(ix-24)\n\tld h,(ix-23)\n"
          "\tinc (ix-24)\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tinc (ix-23)\nL%d:\n"
            "\tadd hl,hl\n"
            "\tld e,(ix%+d)\n\tld d,(ix%+d)\n\tadd hl,de\n"
            "\tpush hl\n"
            "\tld l,(ix-18)\n\tld h,(ix-17)\n\tpush hl\n"
            "\tld l,(ix-20)\n\tld h,(ix-19)\n\tadd hl,hl\n"
            "\tex de,hl\n\tpop hl\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tsla e\n\trl d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n\tadd hl,de\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpop hl\n\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
            "L%d:\n"
            "\tinc (ix-26)\n\tjp nz,L%d\n\tinc (ix-25)\n"
            "L%d:\n\tjp L%d\n"
            "L%d:\n\tld l,(ix-24)\n\tld h,(ix-23)\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            count_incremented, count_incremented,
            output_offset, output_offset + 1,
            input_offset, input_offset + 1,
            output_done, index_incremented, index_incremented,
            outer, done);
}

static int mir_match_trim_schedule(struct MirTrimSchedule *plan)
{
    static const unsigned char expected_opcodes[122] = {
        MIR_LABEL, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_PHI, MIR_LOAD, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_STORE_INDIRECT, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_LABEL, MIR_JUMP, MIR_LABEL
    };
    int length_argument;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 122 || mir_cfg_block_count() != 17 ||
        mir.has_vla || mir.local_bytes != 6 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "trim-schedule", "opcodes");
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->string_stack_offset) ||
        plan->string_stack_offset < 2 ||
        plan->string_stack_offset > 123 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        mir_machine_pointee_is_volatile(&mir.insns[1]))
        return mir_machine_reject(
            "trim-schedule", "parameter");
    plan->space_function = find_global(mir.insns[20].name);
    plan->length_function = find_global(mir.insns[78].name);
    if (plan->space_function == NULL ||
        plan->length_function == NULL ||
        plan->space_function->proto_nargs != 1 ||
        plan->length_function->proto_nargs != 1 ||
        strcmp(mir.insns[20].name,
               mir.insns[100].name) != 0 ||
        !mir_call_is_strlen_fastcall(78, &length_argument) ||
        length_argument != mir.insns[76].dst)
        return mir_machine_reject(
            "trim-schedule", "calls");
    return 1;
}

static int mir_match_digit_label_schedule(
    struct MirDigitLabelSchedule *plan)
{
    static const unsigned char expected_opcodes[37] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_RETURN
    };
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 37 || mir_cfg_block_count() != 7 ||
        mir.has_vla || mir.local_bytes != 2 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_INT)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "digit-label-schedule", "opcodes");
    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->string_stack_offset) ||
        plan->string_stack_offset < 2 ||
        plan->string_stack_offset > 123 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        mir_machine_pointee_is_volatile(&mir.insns[1]))
        return mir_machine_reject(
            "digit-label-schedule", "parameter");
    plan->digit_function = find_global(mir.insns[15].name);
    plan->convert_function = find_global(mir.insns[35].name);
    if (plan->digit_function == NULL ||
        plan->convert_function == NULL ||
        plan->digit_function->proto_nargs != 1 ||
        plan->convert_function->proto_nargs != 1)
        return mir_machine_reject(
            "digit-label-schedule", "calls");
    return 1;
}

static void mir_emit_digit_label_schedule(
    MirStream *out, const struct MirDigitLabelSchedule *plan)
{
    int loop = new_label();
    int convert = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "L%d:\n\tld a,(iy)\n\tor a\n\tjp z,L%d\n"
            "\tld l,a\n\tld h,0\n\tpush hl\n",
            plan->string_stack_offset + 4,
            plan->string_stack_offset + 5,
            loop, convert);
    mir_machine_emit_symbol_call(out, plan->digit_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc iy\n\tjp L%d\n"
                 "L%d:\n\tpush iy\n",
            convert, loop, convert);
    mir_machine_emit_symbol_call(out, plan->convert_function);
    mir_stream_puts("\tpop bc\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_preprocessor_schedule(
    struct MirPreprocessorSchedule *plan)
{
    static const int compare_calls[] = {
        481, 752, 882, 917, 1166,
        1207, 1233, 1267, 1278
    };
    static const struct {
        int instruction;
        int value;
    } semantic_constants[] = {
        {107, '/'}, {117, '*'}, {163, '*'}, {173, '/'},
        {249, '/'}, {259, '/'}, {320, '#'},
        {340, ' '}, {442, ' '}, {576, ' '},
        {352, '\t'}, {454, '\t'}, {588, '\t'},
        {198, '\n'}, {219, '\n'}, {293, '\n'},
        {631, '\n'}, {776, '\n'}, {956, '\n'},
        {979, '\n'}, {992, '\n'}, {1012, '\n'},
        {1023, '\n'}, {513, '_'}, {1052, '_'},
        {1098, '_'}, {1366, 26}
    };
    int counts[20] = {0};
    int instruction;
    int item;
    int length_argument;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 1429 || mir_cfg_block_count() != 203 ||
        mir.has_vla || mir.local_bytes != 32 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_CHAR ||
        type_ptr_depth(mir.return_type) != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int bucket;

        switch (insn->opcode) {
        case MIR_ADDRESS: bucket = 0; break;
        case MIR_ARG: bucket = 1; break;
        case MIR_BINARY: bucket = 2; break;
        case MIR_BRANCH_FALSE: bucket = 3; break;
        case MIR_CALL: bucket = 4; break;
        case MIR_CONST: bucket = 5; break;
        case MIR_INDEX_ADDRESS: bucket = 6; break;
        case MIR_JUMP: bucket = 7; break;
        case MIR_LABEL: bucket = 8; break;
        case MIR_LOAD: bucket = 9; break;
        case MIR_LOAD_INDIRECT: bucket = 10; break;
        case MIR_MEMBER_ADDRESS: bucket = 11; break;
        case MIR_NOP: bucket = 12; break;
        case MIR_PHI: bucket = 13; break;
        case MIR_RETURN: bucket = 14; break;
        case MIR_STORE: bucket = 15; break;
        case MIR_STORE_INDIRECT: bucket = 16; break;
        case MIR_STRING_ADDRESS: bucket = 17; break;
        case MIR_UNARY: bucket = 18; break;
        case MIR_PARAM: bucket = 19; break;
        default:
            return mir_machine_reject(
                "preprocessor-schedule", "opcode");
        }
        ++counts[bucket];
        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE ||
             insn->opcode == MIR_PARAM ||
             insn->opcode == MIR_ADDRESS) &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "preprocessor-schedule",
                "volatile-named-memory");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "preprocessor-schedule",
                "volatile-indirect-memory");
    }
    if (counts[0] != 6 || counts[1] != 35 ||
        counts[2] != 83 || counts[3] != 96 ||
        counts[4] != 24 || counts[5] != 169 ||
        counts[6] != 77 || counts[7] != 72 ||
        counts[8] != 203 || counts[9] != 198 ||
        counts[10] != 55 || counts[11] != 6 ||
        counts[12] != 202 || counts[13] != 43 ||
        counts[14] != 1 || counts[15] != 67 ||
        counts[16] != 24 || counts[17] != 13 ||
        counts[18] != 54 || counts[19] != 1)
        return mir_machine_reject(
            "preprocessor-schedule", "population");

    if (!mir_machine_parameter_value_offset(
            mir.insns[1].dst, &plan->input_stack_offset) ||
        plan->input_stack_offset < 2 ||
        plan->input_stack_offset > 123 ||
        type_ptr_depth(mir.insns[1].type) != 1 ||
        mir_machine_pointee_is_volatile(&mir.insns[1]))
        return mir_machine_reject(
            "preprocessor-schedule", "parameter");
    plan->state = find_global(mir.insns[2].name);
    if (plan->state == NULL || plan->state->is_array ||
        plan->state->is_volatile ||
        (plan->state->storage != SC_GLOBAL &&
         plan->state->storage != SC_EXTERN) ||
        find_global(mir.insns[6].name) != plan->state ||
        find_global(mir.insns[10].name) != plan->state ||
        find_global(mir.insns[14].name) != plan->state ||
        find_global(mir.insns[18].name) != plan->state ||
        find_global(mir.insns[22].name) != plan->state)
        return mir_machine_reject(
            "preprocessor-schedule", "state");
    plan->name_offset = (int)mir.insns[3].immediate;
    plan->value_offset = (int)mir.insns[7].immediate;
    plan->identifier_offset = (int)mir.insns[11].immediate;
    plan->definition_names_offset =
        (int)mir.insns[15].immediate;
    plan->definition_values_offset =
        (int)mir.insns[19].immediate;
    plan->conditional_active_offset =
        (int)mir.insns[23].immediate;
    if (plan->name_offset < 0 ||
        plan->value_offset != plan->name_offset + 64 ||
        plan->identifier_offset != plan->value_offset + 64 ||
        plan->definition_names_offset !=
            plan->identifier_offset + 64 ||
        plan->definition_values_offset !=
            plan->definition_names_offset + 64 ||
        plan->conditional_active_offset !=
            plan->definition_values_offset + 64 ||
        mir.insns[3].memory_size != 64 ||
        mir.insns[7].memory_size != 64 ||
        mir.insns[11].memory_size != 64 ||
        mir.insns[15].memory_size != 64 ||
        mir.insns[19].memory_size != 64 ||
        mir.insns[23].memory_size != 32)
        return mir_machine_reject(
            "preprocessor-schedule", "state-layout");

    plan->length_function = find_global(mir.insns[28].name);
    plan->allocate_function = find_global(mir.insns[34].name);
    plan->die_function = find_global(mir.insns[43].name);
    plan->alpha_function = find_global(mir.insns[393].name);
    plan->compare_function = find_global(mir.insns[481].name);
    plan->alnum_function = find_global(mir.insns[503].name);
    plan->space_function = find_global(mir.insns[699].name);
    plan->duplicate_function = find_global(mir.insns[730].name);
    plan->search_function = find_global(mir.insns[842].name);
    plan->digit_function = find_global(mir.insns[1194].name);
    plan->free_function = find_global(mir.insns[1411].name);
    if (plan->length_function == NULL ||
        plan->allocate_function == NULL ||
        plan->die_function == NULL ||
        plan->alpha_function == NULL ||
        plan->compare_function == NULL ||
        plan->alnum_function == NULL ||
        plan->space_function == NULL ||
        plan->duplicate_function == NULL ||
        plan->search_function == NULL ||
        plan->digit_function == NULL ||
        plan->free_function == NULL ||
        plan->length_function->proto_nargs != 1 ||
        plan->allocate_function->proto_nargs != 1 ||
        plan->die_function->proto_nargs != 1 ||
        plan->alpha_function->proto_nargs != 1 ||
        plan->compare_function->proto_nargs != 2 ||
        plan->alnum_function->proto_nargs != 1 ||
        plan->space_function->proto_nargs != 1 ||
        plan->duplicate_function->proto_nargs != 1 ||
        plan->search_function->proto_nargs != 2 ||
        plan->digit_function->proto_nargs != 1 ||
        plan->free_function->proto_nargs != 1 ||
        !mir_call_is_strlen_fastcall(28, &length_argument) ||
        length_argument < 0 ||
        find_global(mir.insns[737].name) !=
            plan->duplicate_function ||
        find_global(mir.insns[852].name) !=
            plan->search_function ||
        find_global(mir.insns[1042].name) !=
            plan->alpha_function ||
        find_global(mir.insns[1088].name) !=
            plan->alnum_function ||
        find_global(mir.insns[1418].name) !=
            plan->free_function)
        return mir_machine_reject(
            "preprocessor-schedule", "calls");
    for (item = 0;
         item < (int)(sizeof(compare_calls) /
                      sizeof(compare_calls[0]));
         ++item)
        if (find_global(mir.insns[compare_calls[item]].name) !=
            plan->compare_function)
            return mir_machine_reject(
                "preprocessor-schedule", "compare-calls");

    plan->oom_string_id = (int)mir.insns[41].immediate;
    plan->define_string_id = (int)mir.insns[479].immediate;
    plan->if_string_id = (int)mir.insns[750].immediate;
    plan->true_string_id = (int)mir.insns[840].immediate;
    plan->one_string_id = (int)mir.insns[850].immediate;
    plan->else_string_id = (int)mir.insns[880].immediate;
    plan->endif_string_id = (int)mir.insns[915].immediate;
    plan->false_string_id = (int)mir.insns[1231].immediate;
    plan->zero_string_id = (int)mir.insns[1281].immediate;
    plan->text_limit = (int)mir.insns[396].immediate;
    plan->definition_limit = (int)mir.insns[52].immediate;
    plan->conditional_limit = (int)mir.insns[824].immediate;
    if (plan->oom_string_id < 0 ||
        plan->define_string_id < 0 ||
        plan->if_string_id < 0 ||
        plan->else_string_id < 0 ||
        plan->endif_string_id < 0 ||
        plan->true_string_id < 0 ||
        plan->false_string_id < 0 ||
        plan->one_string_id < 0 ||
        plan->zero_string_id < 0 ||
        plan->text_limit != 63 ||
        plan->definition_limit != 32 ||
        plan->conditional_limit != 16)
        return mir_machine_reject(
            "preprocessor-schedule", "constants");
    for (item = 0;
         item < (int)(sizeof(semantic_constants) /
                      sizeof(semantic_constants[0]));
         ++item)
        if (!mir_machine_constant_equals(
                mir.insns[
                    semantic_constants[item].instruction].dst,
                semantic_constants[item].value))
            return mir_machine_reject(
                "preprocessor-schedule",
                "semantic-constants");
    return 1;
}

static void mir_emit_trim_space_call(
    MirStream *out, const struct MirTrimSchedule *plan)
{
    mir_stream_puts("\tld l,a\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->space_function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_emit_trim_schedule(
    MirStream *out, const struct MirTrimSchedule *plan)
{
    int leading = new_label();
    int leading_done = new_label();
    int copy = new_label();
    int copy_done = new_label();
    int trailing = new_label();
    int done = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n"
            "L%d:\n\tpush iy\n\tpop hl\n"
            "\tld e,(ix-2)\n\tld d,(ix-1)\n"
            "\tadd hl,de\n\tld a,(hl)\n\tor a\n"
            "\tjp z,L%d\n",
            plan->string_stack_offset + 4,
            plan->string_stack_offset + 5,
            leading, leading_done);
    mir_emit_trim_space_call(out, plan);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tinc (ix-2)\n",
            leading_done);
    {
        int no_carry = new_label();

        mir_stream_printf(out,
                "\tjp nz,L%d\n\tinc (ix-1)\nL%d:\n",
                no_carry, no_carry);
    }
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            leading, leading_done);
    mir_stream_puts("\tld a,(ix-2)\n\tor (ix-1)\n", out);
    {
        int no_copy = new_label();

        mir_stream_printf(out, "\tjp z,L%d\n", no_copy);
        mir_stream_puts("\tpush iy\n\tpop hl\n"
              "\tld e,(ix-2)\n\tld d,(ix-1)\n"
              "\tadd hl,de\n\tpush iy\n\tpop de\n", out);
        mir_stream_printf(out,
                "L%d:\n\tld a,(hl)\n\tinc hl\n"
                "\tld (de),a\n\tinc de\n\tor a\n"
                "\tjp nz,L%d\nL%d:\n",
                copy, copy, copy_done);
        mir_stream_printf(out, "L%d:\n", no_copy);
    }
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_emit_runtime_call(out, "__slf");
    mir_stream_puts("\tld (ix-4),l\n\tld (ix-3),h\n", out);
    mir_stream_printf(out, "L%d:\n\tld l,(ix-4)\n\tld h,(ix-3)\n"
                 "\tld a,h\n\tor l\n\tjp z,L%d\n"
                 "\tbit 7,h\n\tjp nz,L%d\n"
                 "\tdec hl\n\tpush iy\n\tpop de\n"
                 "\tadd hl,de\n\tld a,(hl)\n",
            trailing, done, done);
    mir_emit_trim_space_call(out, plan);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", done);
    mir_stream_puts("\tdec (ix-4)\n", out);
    {
        int no_borrow = new_label();

        mir_stream_printf(out,
                "\tld a,(ix-4)\n\tcp 255\n\tjp nz,L%d\n"
                "\tdec (ix-3)\nL%d:\n",
                no_borrow, no_borrow);
    }
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tpush iy\n\tpop de\n\tadd hl,de\n"
          "\txor a\n\tld (hl),a\n", out);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n"
                 "\tld sp,ix\n\tpop ix\n\tpop iy\n"
                 ";@dcc.reg free=iy\n\tret\n",
            trailing, done);
}

enum MirPreprocessorFrameOffset {
    MIR_PP_OUT_BASE = -2,
    MIR_PP_OUT_CURSOR = -4,
    MIR_PP_ACTIVE = -6,
    MIR_PP_IFSP = -8,
    MIR_PP_NDEFS = -10,
    MIR_PP_J = -12,
    MIR_PP_K = -14,
    MIR_PP_RP = -16
};

static void mir_preprocessor_state_address(
    MirStream *out, const struct MirPreprocessorSchedule *plan,
    int offset)
{
    mir_machine_emit_global_address_de(out, plan->state, offset);
    mir_stream_puts("\tex de,hl\n", out);
}

static void mir_preprocessor_frame_load(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld l,(ix%d)\n\tld h,(ix%d)\n",
            offset, offset + 1);
}

static void mir_preprocessor_frame_store(
    MirStream *out, int offset)
{
    mir_stream_printf(out,
            "\tld (ix%d),l\n\tld (ix%d),h\n",
            offset, offset + 1);
}

static void mir_preprocessor_output_a(MirStream *out)
{
    mir_preprocessor_frame_load(out, MIR_PP_OUT_CURSOR);
    mir_stream_puts("\tld (hl),a\n\tinc hl\n", out);
    mir_preprocessor_frame_store(out, MIR_PP_OUT_CURSOR);
}

static void mir_preprocessor_call_char(
    MirStream *out, struct Sym *function)
{
    mir_stream_puts("\tld l,a\n\tld h,0\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_preprocessor_state_slot(
    MirStream *out, const struct MirPreprocessorSchedule *plan,
    int base_offset, int index_frame)
{
    mir_preprocessor_frame_load(out, index_frame);
    mir_stream_puts("\tadd hl,hl\n", out);
    mir_machine_emit_global_address_de(
        out, plan->state, base_offset);
    mir_stream_puts("\tadd hl,de\n", out);
}

static void mir_preprocessor_compare_state_string(
    MirStream *out, const struct MirPreprocessorSchedule *plan,
    int state_offset, int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_preprocessor_state_address(out, plan, state_offset);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_preprocessor_compare_pointer_string(
    MirStream *out, const struct MirPreprocessorSchedule *plan,
    int pointer_frame, int string_id)
{
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_preprocessor_frame_load(out, pointer_frame);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->compare_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_preprocessor_load_definition(
    MirStream *out, const struct MirPreprocessorSchedule *plan,
    int base_offset, int index_frame)
{
    mir_preprocessor_state_slot(
        out, plan, base_offset, index_frame);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tex de,hl\n", out);
}

static void mir_emit_preprocessor_schedule(
    MirStream *out, const struct MirPreprocessorSchedule *plan)
{
    int main_loop = new_label();
    int block_comment = new_label();
    int block_scan = new_label();
    int block_character = new_label();
    int block_advance = new_label();
    int block_close = new_label();
    int line_comment = new_label();
    int line_comment_scan = new_label();
    int directive = new_label();
    int inactive = new_label();
    int identifier = new_label();
    int ordinary = new_label();
    int finish = new_label();
    int input_offset = plan->input_stack_offset + 4;

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-16\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n",
            input_offset, input_offset + 1);
    mir_emit_runtime_call(out, "__slf");
    mir_stream_puts("\tinc hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->allocate_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_preprocessor_frame_store(out, MIR_PP_OUT_BASE);
    mir_preprocessor_frame_store(out, MIR_PP_OUT_CURSOR);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    {
        int allocation_ok = new_label();

        mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
                allocation_ok, plan->oom_string_id);
        mir_machine_emit_symbol_call(out, plan->die_function);
        mir_stream_puts("\tpop bc\n", out);
        mir_stream_printf(out, "L%d:\n", allocation_ok);
    }

    mir_preprocessor_state_address(
        out, plan, plan->definition_names_offset);
    mir_stream_puts("\tex de,hl\n", out);
    mir_preprocessor_state_address(
        out, plan, plan->definition_values_offset);
    mir_stream_puts("\tld b,32\n\txor a\n", out);
    {
        int clear_loop = new_label();

        mir_stream_printf(out,
                "L%d:\n\tex de,hl\n\tld (hl),a\n\tinc hl\n"
                "\tld (hl),a\n\tinc hl\n\tex de,hl\n"
                "\tld (hl),a\n\tinc hl\n\tld (hl),a\n"
                "\tinc hl\n\tdjnz L%d\n",
                clear_loop, clear_loop);
    }
    mir_stream_puts("\tld hl,1\n", out);
    mir_preprocessor_frame_store(out, MIR_PP_ACTIVE);
    mir_stream_puts("\tld hl,0\n", out);
    mir_preprocessor_frame_store(out, MIR_PP_IFSP);
    mir_preprocessor_frame_store(out, MIR_PP_NDEFS);

    mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tor a\n\tjp z,L%d\n",
            main_loop, finish);
    mir_stream_puts("\tcp '/'\n", out);
    {
        int not_slash = new_label();

        mir_stream_printf(out, "\tjp nz,L%d\n\tld a,(iy+1)\n"
                     "\tcp '*'\n\tjp z,L%d\n\tcp '/'\n"
                     "\tjp z,L%d\nL%d:\n",
                not_slash, block_comment,
                line_comment, not_slash);
    }
    mir_stream_puts("\tld a,(iy)\n\tcp '#'\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", directive);
    mir_preprocessor_frame_load(out, MIR_PP_ACTIVE);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", inactive);
    mir_stream_puts("\tld a,(iy)\n", out);
    mir_preprocessor_call_char(out, plan->alpha_function);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", identifier);
    mir_stream_puts("\tld a,(iy)\n\tcp '_'\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tjp L%d\n",
            identifier, ordinary);

    mir_stream_printf(out, "L%d:\n\tinc iy\n\tinc iy\n",
            block_comment);
    mir_preprocessor_frame_load(out, MIR_PP_ACTIVE);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    {
        int no_space = new_label();

        mir_stream_printf(out, "\tjp z,L%d\n\tld a,' '\n",
                no_space);
        mir_preprocessor_output_a(out);
        mir_stream_printf(out, "L%d:\n", no_space);
    }
    mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tor a\n"
                 "\tjp z,L%d\n\tcp '*'\n\tjp nz,L%d\n"
                 "\tld a,(iy+1)\n\tcp '/'\n\tjp z,L%d\n"
                 "L%d:\n\tld a,(iy)\n\tcp 10\n\tjp nz,L%d\n",
            block_scan, main_loop, block_character,
            block_close, block_character, block_advance);
    mir_preprocessor_frame_load(out, MIR_PP_ACTIVE);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    {
        int no_newline = new_label();

        mir_stream_printf(out, "\tjp z,L%d\n\tld a,10\n",
                no_newline);
        mir_preprocessor_output_a(out);
        mir_stream_printf(out, "L%d:\n", no_newline);
    }
    mir_stream_printf(out, "L%d:\n\tinc iy\n\tjp L%d\n",
            block_advance, block_scan);
    mir_stream_printf(out, "L%d:\n\tinc iy\n\tinc iy\n\tjp L%d\n",
            block_close, main_loop);

    mir_stream_printf(out, "L%d:\n\tinc iy\n\tinc iy\nL%d:\n"
                 "\tld a,(iy)\n\tor a\n\tjp z,L%d\n"
                 "\tcp 10\n\tjp z,L%d\n\tinc iy\n"
                 "\tjp L%d\n",
            line_comment, line_comment_scan,
            main_loop, main_loop, line_comment_scan);

    mir_stream_printf(out, "L%d:\n\tinc iy\n", directive);
    {
        int skip_leading = new_label();
        int leading_advance = new_label();
        int leading_done = new_label();
        int skip_after_name = new_label();
        int name_loop = new_label();
        int name_done = new_label();

        mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tcp ' '\n"
                     "\tjp z,L%d\n\tcp 9\n\tjp nz,L%d\n"
                     "L%d:\n\tinc iy\n\tjp L%d\nL%d:\n",
                skip_leading, leading_advance, leading_done,
                leading_advance, skip_leading, leading_done);
        mir_stream_puts("\tld hl,0\n", out);
        mir_preprocessor_frame_store(out, MIR_PP_J);
        mir_stream_printf(out, "L%d:\n\tld a,(iy)\n", name_loop);
        mir_preprocessor_call_char(out, plan->alpha_function);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", name_done);
        mir_preprocessor_frame_load(out, MIR_PP_J);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                     "\tjp nc,L%d\n",
                plan->text_limit, name_done);
        mir_preprocessor_state_address(
            out, plan, plan->name_offset);
        mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
              "\tadd hl,de\n\tld a,(iy)\n\tld (hl),a\n"
              "\tinc (ix-12)\n", out);
        {
            int no_carry = new_label();

            mir_stream_printf(out,
                    "\tjp nz,L%d\n\tinc (ix-11)\nL%d:\n",
                    no_carry, no_carry);
        }
        mir_stream_puts("\tinc iy\n", out);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                name_loop, name_done);
        mir_preprocessor_state_address(
            out, plan, plan->name_offset);
        mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
              "\tadd hl,de\n\txor a\n\tld (hl),a\n", out);
        mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tcp ' '\n",
                skip_after_name);
        {
            int advance = new_label();
            int done_skip = new_label();

            mir_stream_printf(out, "\tjp z,L%d\n\tcp 9\n\tjp nz,L%d\n"
                         "L%d:\n\tinc iy\n\tjp L%d\nL%d:\n",
                    advance, done_skip, advance,
                    skip_after_name, done_skip);
        }
    }

    {
        int directive_if = new_label();
        int directive_else = new_label();
        int directive_endif = new_label();
        int directive_tail = new_label();

        mir_preprocessor_compare_state_string(
            out, plan, plan->name_offset,
            plan->define_string_id);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int not_define = new_label();
            int define_name_loop = new_label();
            int define_name_done = new_label();
            int define_value_loop = new_label();
            int define_value_done = new_label();
            int trim_value = new_label();
            int trimmed_value = new_label();
            int definition_full = new_label();

            mir_stream_printf(out, "\tjp nz,L%d\n", not_define);
            mir_stream_puts("\tld hl,0\n", out);
            mir_preprocessor_frame_store(out, MIR_PP_J);
            mir_stream_printf(out, "L%d:\n\tld a,(iy)\n",
                    define_name_loop);
            mir_preprocessor_call_char(out, plan->alnum_function);
            mir_stream_puts("\tld a,h\n\tor l\n", out);
            {
                int accepted = new_label();

                mir_stream_printf(out, "\tjp nz,L%d\n\tld a,(iy)\n"
                             "\tcp '_'\n\tjp nz,L%d\nL%d:\n",
                        accepted, define_name_done, accepted);
            }
            mir_preprocessor_frame_load(out, MIR_PP_J);
            mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                         "\tjp nc,L%d\n",
                    plan->text_limit, define_name_done);
            mir_preprocessor_state_address(
                out, plan, plan->name_offset);
            mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
                  "\tadd hl,de\n\tld a,(iy)\n\tld (hl),a\n"
                  "\tinc (ix-12)\n", out);
            {
                int no_carry = new_label();

                mir_stream_printf(out,
                        "\tjp nz,L%d\n\tinc (ix-11)\nL%d:\n",
                        no_carry, no_carry);
            }
            mir_stream_puts("\tinc iy\n", out);
            mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                    define_name_loop, define_name_done);
            mir_preprocessor_state_address(
                out, plan, plan->name_offset);
            mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
                  "\tadd hl,de\n\txor a\n\tld (hl),a\n", out);
            {
                int skip_space = new_label();
                int skip_advance = new_label();
                int skip_done = new_label();

                mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tcp ' '\n"
                             "\tjp z,L%d\n\tcp 9\n\tjp nz,L%d\n"
                             "L%d:\n\tinc iy\n\tjp L%d\nL%d:\n",
                        skip_space, skip_advance, skip_done,
                        skip_advance, skip_space, skip_done);
            }
            mir_stream_puts("\tld hl,0\n", out);
            mir_preprocessor_frame_store(out, MIR_PP_J);
            mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tor a\n"
                         "\tjp z,L%d\n\tcp 10\n\tjp z,L%d\n",
                    define_value_loop,
                    define_value_done, define_value_done);
            mir_preprocessor_frame_load(out, MIR_PP_J);
            mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                         "\tjp nc,L%d\n",
                    plan->text_limit, define_value_done);
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
                  "\tadd hl,de\n\tld a,(iy)\n\tld (hl),a\n"
                  "\tinc (ix-12)\n", out);
            {
                int no_carry = new_label();

                mir_stream_printf(out,
                        "\tjp nz,L%d\n\tinc (ix-11)\nL%d:\n",
                        no_carry, no_carry);
            }
            mir_stream_puts("\tinc iy\n", out);
            mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                    define_value_loop, define_value_done);
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
                  "\tadd hl,de\n\txor a\n\tld (hl),a\n", out);
            mir_stream_printf(out, "L%d:\n", trim_value);
            mir_preprocessor_frame_load(out, MIR_PP_J);
            mir_stream_puts("\tld a,h\n\tor l\n", out);
            mir_stream_printf(out, "\tjp z,L%d\n\tdec hl\n",
                    trimmed_value);
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tadd hl,de\n\tld a,(hl)\n", out);
            mir_preprocessor_call_char(out, plan->space_function);
            mir_stream_puts("\tld a,h\n\tor l\n", out);
            mir_stream_printf(out, "\tjp z,L%d\n\tdec (ix-12)\n",
                    trimmed_value);
            {
                int no_borrow = new_label();

                mir_stream_printf(out,
                        "\tld a,(ix-12)\n\tcp 255\n"
                        "\tjp nz,L%d\n\tdec (ix-11)\nL%d:\n",
                        no_borrow, no_borrow);
            }
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
                  "\tadd hl,de\n\txor a\n\tld (hl),a\n", out);
            mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                    trim_value, trimmed_value);
            mir_preprocessor_frame_load(out, MIR_PP_NDEFS);
            mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                         "\tjp nc,L%d\n",
                    plan->definition_limit, definition_full);
            mir_preprocessor_state_address(
                out, plan, plan->name_offset);
            mir_stream_puts("\tpush hl\n", out);
            mir_machine_emit_symbol_call(
                out, plan->duplicate_function);
            mir_stream_puts("\tpop bc\n\tpush hl\n", out);
            mir_preprocessor_state_slot(
                out, plan, plan->definition_names_offset,
                MIR_PP_NDEFS);
            mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n"
                  "\tld (hl),d\n", out);
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tpush hl\n", out);
            mir_machine_emit_symbol_call(
                out, plan->duplicate_function);
            mir_stream_puts("\tpop bc\n\tpush hl\n", out);
            mir_preprocessor_state_slot(
                out, plan, plan->definition_values_offset,
                MIR_PP_NDEFS);
            mir_stream_puts("\tpop de\n\tld (hl),e\n\tinc hl\n"
                  "\tld (hl),d\n", out);
            mir_preprocessor_frame_load(out, MIR_PP_NDEFS);
            mir_stream_puts("\tinc hl\n", out);
            mir_preprocessor_frame_store(out, MIR_PP_NDEFS);
            mir_stream_printf(out, "L%d:\n\tjp L%d\n",
                    definition_full, directive_tail);
            mir_stream_printf(out, "L%d:\n", not_define);
        }

        mir_preprocessor_compare_state_string(
            out, plan, plan->name_offset, plan->if_string_id);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", directive_else);
        mir_stream_printf(out, "L%d:\n", directive_if);
        mir_stream_puts("\tld hl,0\n", out);
        mir_preprocessor_frame_store(out, MIR_PP_J);
        {
            int value_loop = new_label();
            int value_done = new_label();

            mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tor a\n"
                         "\tjp z,L%d\n\tcp 10\n\tjp z,L%d\n",
                    value_loop, value_done, value_done);
            mir_preprocessor_frame_load(out, MIR_PP_J);
            mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                         "\tjp nc,L%d\n",
                    plan->text_limit, value_done);
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
                  "\tadd hl,de\n\tld a,(iy)\n\tld (hl),a\n"
                  "\tinc (ix-12)\n", out);
            {
                int no_carry = new_label();

                mir_stream_printf(out,
                        "\tjp nz,L%d\n\tinc (ix-11)\nL%d:\n",
                        no_carry, no_carry);
            }
            mir_stream_puts("\tinc iy\n", out);
            mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                    value_loop, value_done);
        }
        mir_preprocessor_state_address(
            out, plan, plan->value_offset);
        mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
              "\tadd hl,de\n\txor a\n\tld (hl),a\n", out);
        mir_preprocessor_frame_load(out, MIR_PP_IFSP);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
                plan->conditional_limit);
        {
            int no_push = new_label();

            mir_stream_printf(out, "\tjp nc,L%d\n", no_push);
            mir_preprocessor_state_slot(
                out, plan, plan->conditional_active_offset,
                MIR_PP_IFSP);
            mir_stream_puts("\tpush hl\n", out);
            mir_preprocessor_frame_load(out, MIR_PP_ACTIVE);
            mir_stream_puts("\tex de,hl\n\tpop hl\n"
                  "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
            mir_preprocessor_frame_load(out, MIR_PP_IFSP);
            mir_stream_puts("\tinc hl\n", out);
            mir_preprocessor_frame_store(out, MIR_PP_IFSP);
            mir_stream_printf(out, "L%d:\n", no_push);
        }
        mir_preprocessor_frame_load(out, MIR_PP_ACTIVE);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int condition_false = new_label();
            int condition_true = new_label();
            int condition_done = new_label();

            mir_stream_printf(out, "\tjp z,L%d\n", condition_false);
            mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                    plan->true_string_id);
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tpush hl\n", out);
            mir_machine_emit_symbol_call(
                out, plan->search_function);
            mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n",
                  out);
            mir_stream_printf(out, "\tjp nz,L%d\n", condition_true);
            mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
                    plan->one_string_id);
            mir_preprocessor_state_address(
                out, plan, plan->value_offset);
            mir_stream_puts("\tpush hl\n", out);
            mir_machine_emit_symbol_call(
                out, plan->search_function);
            mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n",
                  out);
            mir_stream_printf(out,
                    "\tjp nz,L%d\nL%d:\n\tld hl,0\n"
                    "\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n",
                    condition_true, condition_false,
                    condition_done, condition_true, condition_done);
            mir_preprocessor_frame_store(out, MIR_PP_ACTIVE);
        }
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                directive_tail, directive_else);

        mir_preprocessor_compare_state_string(
            out, plan, plan->name_offset, plan->else_string_id);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", directive_endif);
        mir_preprocessor_frame_load(out, MIR_PP_IFSP);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int no_else = new_label();
            int inactive_now = new_label();
            int else_done = new_label();

            mir_stream_printf(out, "\tjp z,L%d\n\tdec hl\n",
                    no_else);
            mir_preprocessor_frame_store(out, MIR_PP_K);
            mir_preprocessor_state_slot(
                out, plan, plan->conditional_active_offset,
                MIR_PP_K);
            mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                  "\tld a,d\n\tor e\n", out);
            mir_stream_printf(out, "\tjp z,L%d\n", inactive_now);
            mir_preprocessor_frame_load(out, MIR_PP_ACTIVE);
            mir_stream_puts("\tld a,h\n\tor l\n", out);
            mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,1\n"
                         "\tjp L%d\nL%d:\n\tld hl,0\nL%d:\n",
                    inactive_now, else_done,
                    inactive_now, else_done);
            mir_preprocessor_frame_store(out, MIR_PP_ACTIVE);
            mir_stream_printf(out, "L%d:\n", no_else);
        }
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                directive_tail, directive_endif);

        mir_preprocessor_compare_state_string(
            out, plan, plan->name_offset, plan->endif_string_id);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n", directive_tail);
        mir_preprocessor_frame_load(out, MIR_PP_IFSP);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int no_endif = new_label();

            mir_stream_printf(out, "\tjp z,L%d\n\tdec hl\n",
                    no_endif);
            mir_preprocessor_frame_store(out, MIR_PP_IFSP);
            mir_preprocessor_frame_store(out, MIR_PP_K);
            mir_preprocessor_state_slot(
                out, plan, plan->conditional_active_offset,
                MIR_PP_K);
            mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                  "\tex de,hl\n", out);
            mir_preprocessor_frame_store(out, MIR_PP_ACTIVE);
            mir_stream_printf(out, "L%d:\n", no_endif);
        }
        mir_stream_printf(out, "L%d:\n", directive_tail);
    }

    {
        int tail_scan = new_label();
        int tail_newline = new_label();
        int tail_advance = new_label();

        mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tor a\n"
                     "\tjp z,L%d\n\tcp 10\n\tjp z,L%d\n"
                     "\tinc iy\n\tjp L%d\n"
                     "L%d:\n\tld a,(iy)\n\tcp 10\n"
                     "\tjp nz,L%d\n",
                tail_scan, main_loop, tail_newline,
                tail_scan, tail_newline, tail_advance);
        mir_preprocessor_frame_load(out, MIR_PP_ACTIVE);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int no_output = new_label();

            mir_stream_printf(out, "\tjp z,L%d\n\tld a,10\n",
                    no_output);
            mir_preprocessor_output_a(out);
            mir_stream_printf(out, "L%d:\n", no_output);
        }
        mir_stream_puts("\tinc iy\n", out);
        mir_stream_printf(out, "L%d:\n\tjp L%d\n",
                tail_advance, main_loop);
    }

    mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tcp 10\n",
            inactive);
    {
        int no_newline = new_label();

        mir_stream_printf(out, "\tjp nz,L%d\n\tld a,10\n",
                no_newline);
        mir_preprocessor_output_a(out);
        mir_stream_printf(out, "L%d:\n\tinc iy\n\tjp L%d\n",
                no_newline, main_loop);
    }

    mir_stream_printf(out, "L%d:\n", identifier);
    mir_stream_puts("\tld hl,0\n", out);
    mir_preprocessor_frame_store(out, MIR_PP_J);
    {
        int id_loop = new_label();
        int id_accept = new_label();
        int id_done = new_label();

        mir_stream_printf(out, "L%d:\n\tld a,(iy)\n", id_loop);
        mir_preprocessor_call_char(out, plan->alnum_function);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        mir_stream_printf(out, "\tjp nz,L%d\n\tld a,(iy)\n"
                     "\tcp '_'\n\tjp nz,L%d\nL%d:\n",
                id_accept, id_done, id_accept);
        mir_preprocessor_frame_load(out, MIR_PP_J);
        mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
                plan->text_limit);
        {
            int no_store = new_label();

            mir_stream_printf(out, "\tjp nc,L%d\n", no_store);
            mir_preprocessor_state_address(
                out, plan, plan->identifier_offset);
            mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
                  "\tadd hl,de\n\tld a,(iy)\n\tld (hl),a\n"
                  "\tinc (ix-12)\n", out);
            {
                int no_carry = new_label();

                mir_stream_printf(out,
                        "\tjp nz,L%d\n\tinc (ix-11)\nL%d:\n",
                        no_carry, no_carry);
            }
            mir_stream_printf(out, "L%d:\n", no_store);
        }
        mir_stream_puts("\tinc iy\n", out);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n", id_loop, id_done);
    }
    mir_preprocessor_state_address(
        out, plan, plan->identifier_offset);
    mir_stream_puts("\tld e,(ix-12)\n\tld d,(ix-11)\n"
          "\tadd hl,de\n\txor a\n\tld (hl),a\n"
          "\tld hl,0\n", out);
    mir_preprocessor_frame_store(out, MIR_PP_K);
    {
        int search_loop = new_label();
        int search_done = new_label();
        int found = new_label();
        int copy_identifier = new_label();
        int copy_pointer = new_label();
        int copy_loop = new_label();
        int copy_done = new_label();

        mir_stream_printf(out, "L%d:\n", search_loop);
        mir_preprocessor_frame_load(out, MIR_PP_K);
        mir_stream_puts("\tex de,hl\n", out);
        mir_preprocessor_frame_load(out, MIR_PP_NDEFS);
        mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
        mir_stream_printf(out, "\tjp c,L%d\n\tjp z,L%d\n",
                search_done, search_done);
        mir_preprocessor_load_definition(
            out, plan, plan->definition_names_offset,
            MIR_PP_K);
        mir_preprocessor_state_address(
            out, plan, plan->identifier_offset);
        mir_stream_puts("\tpush hl\n", out);
        mir_preprocessor_load_definition(
            out, plan, plan->definition_names_offset,
            MIR_PP_K);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->compare_function);
        mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n",
              out);
        mir_stream_printf(out, "\tjp z,L%d\n", found);
        mir_preprocessor_frame_load(out, MIR_PP_K);
        mir_stream_puts("\tinc hl\n", out);
        mir_preprocessor_frame_store(out, MIR_PP_K);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n", search_loop, found);
        mir_preprocessor_load_definition(
            out, plan, plan->definition_values_offset,
            MIR_PP_K);
        mir_preprocessor_frame_store(out, MIR_PP_RP);
        mir_stream_puts("\tld a,(hl)\n", out);
        mir_preprocessor_call_char(out, plan->digit_function);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int eligible = new_label();

            mir_stream_printf(out, "\tjp nz,L%d\n", eligible);
            mir_preprocessor_compare_pointer_string(
                out, plan, MIR_PP_RP, plan->true_string_id);
            mir_stream_puts("\tld a,h\n\tor l\n", out);
            mir_stream_printf(out, "\tjp z,L%d\n", eligible);
            mir_preprocessor_compare_pointer_string(
                out, plan, MIR_PP_RP, plan->false_string_id);
            mir_stream_puts("\tld a,h\n\tor l\n", out);
            mir_stream_printf(out, "\tjp nz,L%d\nL%d:\n",
                    copy_identifier, eligible);
        }
        mir_preprocessor_compare_pointer_string(
            out, plan, MIR_PP_RP, plan->true_string_id);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int not_true = new_label();

            mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n",
                    not_true, plan->one_string_id);
            mir_preprocessor_frame_store(out, MIR_PP_RP);
            mir_stream_printf(out, "L%d:\n", not_true);
        }
        mir_preprocessor_compare_pointer_string(
            out, plan, MIR_PP_RP, plan->false_string_id);
        mir_stream_puts("\tld a,h\n\tor l\n", out);
        {
            int not_false = new_label();

            mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n",
                    not_false, plan->zero_string_id);
            mir_preprocessor_frame_store(out, MIR_PP_RP);
            mir_stream_printf(out, "L%d:\n\tjp L%d\n",
                    not_false, copy_pointer);
        }
        mir_stream_printf(out, "L%d:\n", search_done);
        mir_stream_printf(out, "L%d:\n", copy_identifier);
        mir_preprocessor_state_address(
            out, plan, plan->identifier_offset);
        mir_preprocessor_frame_store(out, MIR_PP_RP);
        mir_stream_printf(out, "L%d:\nL%d:\n",
                copy_pointer, copy_loop);
        mir_preprocessor_frame_load(out, MIR_PP_RP);
        mir_stream_puts("\tld a,(hl)\n\tor a\n", out);
        mir_stream_printf(out, "\tjp z,L%d\n", copy_done);
        mir_preprocessor_output_a(out);
        mir_preprocessor_frame_load(out, MIR_PP_RP);
        mir_stream_puts("\tinc hl\n", out);
        mir_preprocessor_frame_store(out, MIR_PP_RP);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n\tjp L%d\n",
                copy_loop, copy_done, main_loop);
    }

    mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tinc iy\n"
                 "\tcp 26\n\tjp z,L%d\n",
            ordinary, main_loop);
    mir_preprocessor_output_a(out);
    mir_stream_printf(out, "\tjp L%d\n", main_loop);

    mir_stream_printf(out, "L%d:\n", finish);
    mir_preprocessor_frame_load(out, MIR_PP_OUT_CURSOR);
    mir_stream_puts("\txor a\n\tld (hl),a\n\tld hl,0\n", out);
    mir_preprocessor_frame_store(out, MIR_PP_K);
    {
        int free_loop = new_label();
        int free_done = new_label();

        mir_stream_printf(out, "L%d:\n", free_loop);
        mir_preprocessor_frame_load(out, MIR_PP_K);
        mir_stream_puts("\tex de,hl\n", out);
        mir_preprocessor_frame_load(out, MIR_PP_NDEFS);
        mir_stream_puts("\tor a\n\tsbc hl,de\n", out);
        mir_stream_printf(out, "\tjp c,L%d\n\tjp z,L%d\n",
                free_done, free_done);
        mir_preprocessor_load_definition(
            out, plan, plan->definition_names_offset,
            MIR_PP_K);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->free_function);
        mir_stream_puts("\tpop bc\n", out);
        mir_preprocessor_load_definition(
            out, plan, plan->definition_values_offset,
            MIR_PP_K);
        mir_stream_puts("\tpush hl\n", out);
        mir_machine_emit_symbol_call(out, plan->free_function);
        mir_stream_puts("\tpop bc\n", out);
        mir_preprocessor_frame_load(out, MIR_PP_K);
        mir_stream_puts("\tinc hl\n", out);
        mir_preprocessor_frame_store(out, MIR_PP_K);
        mir_stream_printf(out, "\tjp L%d\nL%d:\n",
                free_loop, free_done);
    }
    mir_preprocessor_frame_load(out, MIR_PP_OUT_BASE);
    mir_stream_puts("\tld sp,ix\n\tpop ix\n\tpop iy\n"
          ";@dcc.reg free=iy\n\tret\n", out);
}

static int mir_match_basic_lexer_schedule(
    struct MirBasicLexerSchedule *plan)
{
    static const unsigned char expected_opcodes[412] = {
        MIR_LABEL, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_STORE, MIR_NOP,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_RETURN, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_NOP, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_ARG,
        MIR_CALL, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_STORE, MIR_NOP,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST,
        MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_LABEL, MIR_CONST, MIR_BRANCH_FALSE, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_UNARY, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_UNARY,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_ARG, MIR_STRING_ADDRESS,
        MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_CONST, MIR_NOP, MIR_STORE,
        MIR_LOAD, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_RETURN, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_CONST, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_ARG,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_RETURN, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST,
        MIR_INDEX_ADDRESS, MIR_LOAD, MIR_UNARY, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
        MIR_STORE_INDIRECT
    };
    static const int copy_calls[] = {312, 345, 378, 393};
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 412 || mir_cfg_block_count() != 57 ||
        mir.has_vla || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "basic-lexer-schedule", "opcodes");

    plan->token = find_global(mir.insns[4].name);
    plan->integer_value = find_global(mir.insns[7].name);
    plan->text = find_global(mir.insns[8].name);
    plan->cursor = find_global(mir.insns[14].name);
    if (plan->token == NULL || plan->integer_value == NULL ||
        plan->text == NULL || plan->cursor == NULL ||
        plan->token->is_array || plan->integer_value->is_array ||
        plan->text->is_array || plan->cursor->is_array ||
        plan->token->is_volatile ||
        plan->integer_value->is_volatile ||
        plan->text->is_volatile || plan->cursor->is_volatile ||
        type_ptr_depth(plan->text->type) != 1 ||
        type_ptr_depth(plan->cursor->type) != 1 ||
        type_size(plan->token->type) != 2 ||
        type_size(plan->integer_value->type) != 2)
        return mir_machine_reject(
            "basic-lexer-schedule", "globals");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if ((insn->opcode == MIR_LOAD ||
             insn->opcode == MIR_STORE) &&
            insn->name[0] != '\0' &&
            find_global(insn->name) != NULL &&
            find_global(insn->name) != plan->token &&
            find_global(insn->name) != plan->integer_value &&
            find_global(insn->name) != plan->text &&
            find_global(insn->name) != plan->cursor)
            return mir_machine_reject(
                "basic-lexer-schedule", "extra-global");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "basic-lexer-schedule",
                "volatile-indirect-memory");
    }

    plan->skip_function = find_global(mir.insns[1].name);
    plan->alpha_function = find_global(mir.insns[50].name);
    plan->upper_function = find_global(mir.insns[72].name);
    plan->alnum_function = find_global(mir.insns[83].name);
    plan->digit_function = find_global(mir.insns[155].name);
    plan->format_function = find_global(mir.insns[202].name);
    plan->copy_function = find_global(mir.insns[312].name);
    if (plan->skip_function == NULL ||
        plan->alpha_function == NULL ||
        plan->upper_function == NULL ||
        plan->alnum_function == NULL ||
        plan->digit_function == NULL ||
        plan->format_function == NULL ||
        plan->copy_function == NULL ||
        !plan->skip_function->is_defined ||
        plan->skip_function->proto_nargs != 0 ||
        plan->skip_function->proto_variadic ||
        plan->alpha_function->proto_nargs != 1 ||
        plan->upper_function->proto_nargs != 1 ||
        plan->alnum_function->proto_nargs != 1 ||
        plan->digit_function->proto_nargs != 1 ||
        plan->format_function->proto_nargs < 2 ||
        plan->copy_function->proto_nargs != 2 ||
        plan->copy_function->proto_variadic ||
        strcmp(mir.insns[155].name,
               mir.insns[171].name) != 0)
        return mir_machine_reject(
            "basic-lexer-schedule", "calls");
    for (item = 0;
         item < (int)(sizeof(copy_calls) /
                      sizeof(copy_calls[0]));
         ++item)
        if (find_global(mir.insns[copy_calls[item]].name) !=
            plan->copy_function)
            return mir_machine_reject(
                "basic-lexer-schedule", "copy-calls");

    plan->integer_format_string_id =
        (int)mir.insns[198].immediate;
    plan->operator_string_ids[0] =
        (int)mir.insns[310].immediate;
    plan->operator_string_ids[1] =
        (int)mir.insns[343].immediate;
    plan->operator_string_ids[2] =
        (int)mir.insns[376].immediate;
    plan->operator_string_ids[3] =
        (int)mir.insns[391].immediate;
    plan->token_identifier = (int)mir.insns[147].immediate;
    plan->token_integer = (int)mir.insns[203].immediate;
    plan->token_string = (int)mir.insns[277].immediate;
    plan->token_le = (int)mir.insns[305].immediate;
    plan->token_ge = (int)mir.insns[338].immediate;
    plan->token_ne = (int)mir.insns[371].immediate;
    plan->token_div = (int)mir.insns[386].immediate;
    plan->token_limit = (int)mir.insns[61].immediate;
    return plan->integer_format_string_id >= 0 &&
           plan->operator_string_ids[0] >= 0 &&
           plan->operator_string_ids[1] >= 0 &&
           plan->operator_string_ids[2] >= 0 &&
           plan->operator_string_ids[3] >= 0 &&
           plan->token_identifier > 255 &&
           plan->token_integer == plan->token_identifier + 1 &&
           plan->token_string == plan->token_integer + 1 &&
           plan->token_le > plan->token_string &&
           plan->token_ge == plan->token_le + 1 &&
           plan->token_ne == plan->token_ge + 1 &&
           plan->token_div == plan->token_ne + 1 &&
           plan->token_limit > 0 &&
           plan->token_limit <= 255 &&
           mir_machine_constant_equals(mir.insns[27].dst, 26) &&
           mir_machine_constant_equals(mir.insns[90].dst, '$') &&
           mir_machine_constant_equals(mir.insns[110].dst, '%') &&
           mir_machine_constant_equals(mir.insns[159].dst, '0') &&
           mir_machine_constant_equals(mir.insns[180].dst, '0') &&
           mir_machine_constant_equals(mir.insns[210].dst, '"') &&
           mir_machine_constant_equals(mir.insns[224].dst, '"') &&
           mir_machine_constant_equals(mir.insns[262].dst, '"') &&
           mir_machine_constant_equals(mir.insns[284].dst, '<') &&
           mir_machine_constant_equals(mir.insns[289].dst, '=') &&
           mir_machine_constant_equals(mir.insns[317].dst, '>') &&
           mir_machine_constant_equals(mir.insns[322].dst, '=') &&
           mir_machine_constant_equals(mir.insns[350].dst, '<') &&
           mir_machine_constant_equals(mir.insns[355].dst, '>') &&
           mir_machine_constant_equals(mir.insns[383].dst, '\\');
}

static void mir_basic_lexer_sync_cursor(
    MirStream *out, const struct MirBasicLexerSchedule *plan)
{
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->cursor, 0);
}

static void mir_basic_lexer_load_text(
    MirStream *out, const struct MirBasicLexerSchedule *plan)
{
    mir_machine_emit_global_word(out, plan->text, 0);
}

static void mir_basic_lexer_call_character(
    MirStream *out, struct Sym *function)
{
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n",
          out);
    mir_machine_emit_symbol_call(out, function);
    mir_stream_puts("\tpop bc\n", out);
}

static void mir_basic_lexer_set_word(
    MirStream *out, struct Sym *symbol, int value)
{
    mir_stream_printf(out, "\tld hl,%d\n", value);
    mir_machine_emit_global_word_store(out, symbol, 0);
}

static void mir_basic_lexer_copy_operator(
    MirStream *out, const struct MirBasicLexerSchedule *plan,
    int token, int string_id)
{
    mir_basic_lexer_set_word(out, plan->token, token);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n", string_id);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->copy_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_basic_lexer_schedule(
    MirStream *out, const struct MirBasicLexerSchedule *plan)
{
    int done = new_label();
    int identifier = new_label();
    int identifier_loop = new_label();
    int identifier_store_done = new_label();
    int identifier_accept = new_label();
    int identifier_finish = new_label();
    int integer = new_label();
    int integer_loop = new_label();
    int integer_finish = new_label();
    int string = new_label();
    int string_loop = new_label();
    int string_store_done = new_label();
    int string_finish = new_label();
    int check_greater = new_label();
    int check_not_equal = new_label();
    int check_div = new_label();
    int default_token = new_label();
    int op_done = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-8\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_symbol_call(out, plan->skip_function);
    mir_basic_lexer_set_word(out, plan->token, 0);
    mir_basic_lexer_set_word(out, plan->integer_value, 0);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\txor a\n\tld (hl),a\n", out);
    mir_machine_emit_global_word(out, plan->cursor, 0);
    mir_stream_puts("\tpush hl\n\tpop iy\n\tld a,(iy)\n"
          "\tld (ix-2),a\n\txor a\n\tld (ix-1),a\n"
          "\tld a,(ix-2)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tcp 26\n\tjp z,L%d\n",
            done, done);
    mir_stream_puts("\tinc iy\n", out);
    mir_basic_lexer_sync_cursor(out, plan);

    mir_basic_lexer_call_character(out, plan->alpha_function);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", identifier);
    mir_stream_puts("\tld a,(ix-2)\n\tcp '_'\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", identifier);
    mir_basic_lexer_call_character(out, plan->digit_function);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", integer);
    mir_stream_puts("\tld a,(ix-2)\n\tcp '\"'\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", string);
    mir_stream_puts("\tcp '<'\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", check_greater);
    mir_stream_puts("\tld a,(iy)\n\tcp '='\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc iy\n",
            check_not_equal);
    mir_basic_lexer_sync_cursor(out, plan);
    mir_basic_lexer_copy_operator(
        out, plan, plan->token_le,
        plan->operator_string_ids[0]);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out, "L%d:\n\tld a,(ix-2)\n\tcp '>'\n",
            check_greater);
    mir_stream_printf(out, "\tjp nz,L%d\n", check_not_equal);
    mir_stream_puts("\tld a,(iy)\n\tcp '='\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc iy\n",
            default_token);
    mir_basic_lexer_sync_cursor(out, plan);
    mir_basic_lexer_copy_operator(
        out, plan, plan->token_ge,
        plan->operator_string_ids[1]);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out, "L%d:\n\tld a,(ix-2)\n\tcp '<'\n",
            check_not_equal);
    mir_stream_printf(out, "\tjp nz,L%d\n", check_div);
    mir_stream_puts("\tld a,(iy)\n\tcp '>'\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc iy\n",
            default_token);
    mir_basic_lexer_sync_cursor(out, plan);
    mir_basic_lexer_copy_operator(
        out, plan, plan->token_ne,
        plan->operator_string_ids[2]);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out, "L%d:\n\tld a,(ix-2)\n\tcp '\\\\'\n",
            check_div);
    mir_stream_printf(out, "\tjp nz,L%d\n", default_token);
    mir_basic_lexer_copy_operator(
        out, plan, plan->token_div,
        plan->operator_string_ids[3]);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out, "L%d:\n", default_token);
    mir_stream_puts("\tld l,(ix-2)\n\tld h,(ix-1)\n", out);
    mir_machine_emit_global_word_store(out, plan->token, 0);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\tld a,(ix-2)\n\tld (hl),a\n\tinc hl\n"
          "\txor a\n\tld (hl),a\n", out);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out,
            "L%d:\n\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n"
            "L%d:\n",
            identifier, identifier_loop);
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n"
          "\tld de,", out);
    mir_stream_printf(out, "%d\n\tor a\n\tsbc hl,de\n",
            plan->token_limit);
    mir_stream_printf(out, "\tjp nc,L%d\n", identifier_store_done);
    mir_basic_lexer_call_character(out, plan->upper_function);
    mir_stream_puts("\tpush hl\n", out);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\tld e,(ix-4)\n\tld d,(ix-3)\n\tadd hl,de\n"
          "\tpop de\n\tld (hl),e\n\tinc (ix-4)\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tinc (ix-3)\nL%d:\n",
            identifier_store_done, identifier_store_done);
    mir_stream_puts("\tld a,(iy)\n\tld (ix-2),a\n\txor a\n"
          "\tld (ix-1),a\n", out);
    mir_basic_lexer_call_character(out, plan->alnum_function);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n", identifier_accept);
    mir_stream_puts("\tld a,(ix-2)\n\tcp '$'\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tcp '%%'\n",
            identifier_accept);
    mir_stream_printf(out, "\tjp nz,L%d\n", identifier_finish);
    mir_stream_printf(out, "L%d:\n\tinc iy\n", identifier_accept);
    mir_basic_lexer_sync_cursor(out, plan);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            identifier_loop, identifier_finish);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\tld e,(ix-4)\n\tld d,(ix-3)\n\tadd hl,de\n"
          "\txor a\n\tld (hl),a\n", out);
    mir_basic_lexer_set_word(
        out, plan->token, plan->token_identifier);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out, "L%d:\n", integer);
    mir_stream_puts("\tld a,(ix-2)\n\tsub '0'\n\tld (ix-8),a\n"
          "\txor a\n\tld (ix-7),a\n\tld (ix-6),a\n"
          "\tld (ix-5),a\n", out);
    mir_stream_printf(out, "L%d:\n", integer_loop);
    mir_stream_puts("\tld a,(iy)\n\tld (ix-2),a\n\txor a\n"
          "\tld (ix-1),a\n", out);
    mir_basic_lexer_call_character(out, plan->digit_function);
    mir_stream_puts("\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n", integer_finish);
    mir_stream_puts("\tld l,(ix-8)\n\tld h,(ix-7)\n"
          "\tld e,(ix-6)\n\tld d,(ix-5)\n"
          "\tpush de\n\tpush hl\n\tld hl,10\n\tld de,0\n",
          out);
    mir_emit_runtime_call(out, "__lmul");
    mir_stream_puts("\tpop bc\n\tpop bc\n\tld a,(ix-2)\n"
          "\tsub '0'\n\tld c,a\n\tld b,0\n"
          "\tadd hl,bc\n\tjp nc,", out);
    {
        int no_carry = new_label();

        mir_stream_printf(out, "L%d\n\tinc de\nL%d:\n",
                no_carry, no_carry);
    }
    mir_stream_puts("\tld (ix-8),l\n\tld (ix-7),h\n"
          "\tld (ix-6),e\n\tld (ix-5),d\n\tinc iy\n", out);
    mir_basic_lexer_sync_cursor(out, plan);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n",
            integer_loop, integer_finish);
    mir_stream_puts("\tld l,(ix-8)\n\tld h,(ix-7)\n", out);
    mir_machine_emit_global_word_store(
        out, plan->integer_value, 0);
    mir_stream_puts("\tpush hl\n", out);
    mir_stream_printf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->integer_format_string_id);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->format_function);
    mir_stream_puts("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    mir_basic_lexer_set_word(
        out, plan->token, plan->token_integer);
    mir_stream_printf(out, "\tjp L%d\n", done);

    mir_stream_printf(out,
            "L%d:\n\txor a\n\tld (ix-4),a\n\tld (ix-3),a\n"
            "L%d:\n\tld a,(iy)\n\tor a\n\tjp z,L%d\n"
            "\tcp '\"'\n\tjp z,L%d\n",
            string, string_loop, string_finish, string_finish);
    mir_stream_puts("\tld l,(ix-4)\n\tld h,(ix-3)\n", out);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n",
            plan->token_limit);
    mir_stream_printf(out, "\tjp nc,L%d\n", string_store_done);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\tld e,(ix-4)\n\tld d,(ix-3)\n\tadd hl,de\n"
          "\tld a,(iy)\n\tld (hl),a\n\tinc (ix-4)\n", out);
    mir_stream_printf(out,
            "\tjp nz,L%d\n\tinc (ix-3)\nL%d:\n",
            string_store_done, string_store_done);
    mir_stream_puts("\tinc iy\n", out);
    mir_basic_lexer_sync_cursor(out, plan);
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", string_loop, string_finish);
    mir_stream_puts("\tld a,(iy)\n\tcp '\"'\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tinc iy\n",
            op_done);
    mir_basic_lexer_sync_cursor(out, plan);
    mir_stream_printf(out, "L%d:\n", op_done);
    mir_basic_lexer_load_text(out, plan);
    mir_stream_puts("\tld e,(ix-4)\n\tld d,(ix-3)\n\tadd hl,de\n"
          "\txor a\n\tld (hl),a\n", out);
    mir_basic_lexer_set_word(
        out, plan->token, plan->token_string);

    mir_stream_printf(out,
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n"
            ";@dcc.reg free=iy\n\tret\n",
            done);
}

static int mir_match_line_split_schedule(
    struct MirLineSplitSchedule *plan)
{
    static const unsigned char expected_opcodes[358] = {
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP,
        MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_LABEL,
        MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CALL,
        MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_UNARY,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_ARG, MIR_LOAD, MIR_NOP,
        MIR_ARG, MIR_LOAD, MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_ARG,
        MIR_CALL, MIR_LOAD, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_STORE_INDIRECT, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD, MIR_STORE_INDIRECT, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_MEMBER_ADDRESS, MIR_NOP,
        MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LOAD, MIR_NOP, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_LOAD, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_NOP,
        MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_ADDRESS,
        MIR_MEMBER_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL,
        MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP, MIR_COPY_AGGREGATE,
        MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL,
        MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_INDEX_ADDRESS, MIR_ADDRESS, MIR_COPY_AGGREGATE, MIR_NOP,
        MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_NOP
    };
    int copy_count;
    int copy_destination;
    int copy_source;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 358 || mir_cfg_block_count() != 61 ||
        mir.has_vla || mir.local_bytes != 18 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode !=
            expected_opcodes[instruction])
            return mir_machine_reject(
                "line-split-schedule", "opcodes");

    plan->source = find_global(mir.insns[1].name);
    plan->line_count = find_global(mir.insns[241].name);
    plan->lines = find_global(mir.insns[249].name);
    if (plan->source == NULL || plan->line_count == NULL ||
        plan->lines == NULL ||
        plan->source->is_array || plan->line_count->is_array ||
        plan->lines->is_array ||
        plan->source->is_volatile ||
        plan->line_count->is_volatile ||
        plan->lines->is_volatile ||
        type_ptr_depth(plan->source->type) != 1 ||
        type_ptr_depth(plan->lines->type) != 1 ||
        type_size(plan->line_count->type) != 2)
        return mir_machine_reject(
            "line-split-schedule", "globals");
    if (find_global(mir.insns[255].name) != plan->lines ||
        find_global(mir.insns[261].name) != plan->lines ||
        find_global(mir.insns[268].name) != plan->line_count ||
        find_global(mir.insns[287].name) != plan->line_count ||
        find_global(mir.insns[290].name) != plan->lines ||
        find_global(mir.insns[306].name) != plan->lines ||
        find_global(mir.insns[324].name) != plan->lines ||
        find_global(mir.insns[329].name) != plan->lines ||
        find_global(mir.insns[342].name) != plan->lines)
        return mir_machine_reject(
            "line-split-schedule", "global-uses");

    plan->atoi_function = find_global(mir.insns[89].name);
    plan->digit_function = find_global(mir.insns[99].name);
    plan->allocate_function = find_global(mir.insns[210].name);
    plan->die_function = find_global(mir.insns[219].name);
    plan->copy_function = find_global(mir.insns[232].name);
    if (plan->atoi_function == NULL ||
        plan->digit_function == NULL ||
        plan->allocate_function == NULL ||
        plan->die_function == NULL ||
        plan->copy_function == NULL ||
        plan->atoi_function->proto_nargs != 1 ||
        plan->digit_function->proto_nargs != 1 ||
        plan->allocate_function->proto_nargs != 1 ||
        plan->die_function->proto_nargs != 1 ||
        plan->copy_function->proto_nargs != 3 ||
        !mir_call_is_memcpy_fastcall(
            232, &copy_destination, &copy_source,
            &copy_count) ||
        copy_destination < 0 || copy_source < 0 ||
        copy_count < 0 ||
        find_global(mir.insns[247].name) !=
            plan->die_function)
        return mir_machine_reject(
            "line-split-schedule", "calls");

    plan->record_stride = (int)mir.insns[251].immediate;
    plan->number_offset = (int)mir.insns[252].immediate;
    plan->text_offset = (int)mir.insns[258].immediate;
    plan->pc_offset = (int)mir.insns[264].immediate;
    plan->line_limit = (int)mir.insns[242].immediate;
    plan->allocation_string_id =
        (int)mir.insns[217].immediate;
    plan->line_limit_string_id =
        (int)mir.insns[245].immediate;
    if (plan->record_stride != 6 ||
        plan->number_offset != 0 ||
        plan->text_offset != 2 ||
        plan->pc_offset != 4 ||
        plan->line_limit <= 0 ||
        plan->line_limit > 255 ||
        plan->allocation_string_id < 0 ||
        plan->line_limit_string_id < 0 ||
        !mir_machine_constant_equals(mir.insns[10].dst, 26) ||
        !mir_machine_constant_equals(mir.insns[25].dst, '\r') ||
        !mir_machine_constant_equals(mir.insns[35].dst, '\n') ||
        !mir_machine_constant_equals(mir.insns[112].dst, ' ') ||
        !mir_machine_constant_equals(mir.insns[122].dst, '\t') ||
        !mir_machine_constant_equals(mir.insns[266].dst, 65535))
        return mir_machine_reject(
            "line-split-schedule", "layout");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "line-split-schedule",
                "volatile-indirect-memory");
    }
    return 1;
}

static void mir_line_split_address(
    MirStream *out, const struct MirLineSplitSchedule *plan)
{
    mir_stream_puts("\tadd hl,hl\n\tpush hl\n\tadd hl,hl\n"
          "\tpop de\n\tadd hl,de\n\tex de,hl\n", out);
    mir_machine_emit_global_word(out, plan->lines, 0);
    mir_stream_puts("\tadd hl,de\n", out);
}

static void mir_line_split_copy_six(MirStream *out)
{
    mir_stream_puts("\tld bc,6\n\tldir\n", out);
}

static void mir_emit_line_split_schedule(
    MirStream *out, const struct MirLineSplitSchedule *plan)
{
    enum {
        LINE_E = -2,
        LINE_Q = -4,
        LINE_LN = -6,
        LINE_I = -8,
        LINE_J = -10,
        LINE_TEMP = -16
    };
    int outer = new_label();
    int skip_newlines = new_label();
    int parse_done = new_label();
    int digit_loop = new_label();
    int digit_done = new_label();
    int space_loop = new_label();
    int text_scan = new_label();
    int text_done = new_label();
    int allocation_ok = new_label();
    int line_room = new_label();
    int sort_outer = new_label();
    int sort_inner = new_label();
    int sort_insert = new_label();
    int done = new_label();

    mir_stream_puts(";@dcc.reg claim=iy scope=function sym=mir kind=mir val=0\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-18\n\tadd hl,sp\n\tld sp,hl\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_machine_emit_global_word(out, plan->source, 0);
    mir_stream_puts("\tpush hl\n\tpop iy\n", out);
    mir_stream_printf(out, "L%d:\nL%d:\n\tld a,(iy)\n",
            outer, skip_newlines);
    mir_stream_puts("\tcp 13\n", out);
    {
        int advance = new_label();
        int skip_done = new_label();

        mir_stream_printf(out, "\tjp z,L%d\n\tcp 10\n\tjp nz,L%d\n"
                     "L%d:\n\tinc iy\n\tjp L%d\nL%d:\n",
                advance, skip_done, advance,
                skip_newlines, skip_done);
    }
    mir_stream_puts("\tld a,(iy)\n\tor a\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tcp 26\n\tjp z,L%d\n",
            parse_done, parse_done);

    mir_stream_puts("\tpush iy\n", out);
    mir_machine_emit_symbol_call(out, plan->atoi_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "\tld (ix%d),l\n\tld (ix%d),h\n",
            LINE_LN, LINE_LN + 1);
    mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tld l,a\n\tld h,0\n"
                 "\tpush hl\n", digit_loop);
    mir_machine_emit_symbol_call(out, plan->digit_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp z,L%d\n\tinc iy\n\tjp L%d\nL%d:\n",
            digit_done, digit_loop, digit_done);

    mir_stream_printf(out, "L%d:\n\tld a,(iy)\n\tcp ' '\n",
            space_loop);
    {
        int advance = new_label();
        int spaces_done = new_label();

        mir_stream_printf(out, "\tjp z,L%d\n\tcp 9\n\tjp nz,L%d\n"
                     "L%d:\n\tinc iy\n\tjp L%d\nL%d:\n",
                advance, spaces_done, advance,
                space_loop, spaces_done);
    }
    mir_stream_puts("\tpush iy\n\tpop hl\n", out);
    mir_stream_printf(out, "\tld (ix%d),l\n\tld (ix%d),h\nL%d:\n"
                 "\tld a,(hl)\n\tor a\n\tjp z,L%d\n"
                 "\tcp 26\n\tjp z,L%d\n\tcp 13\n\tjp z,L%d\n"
                 "\tcp 10\n\tjp z,L%d\n\tinc hl\n\tjp L%d\n"
                 "L%d:\n\tld (ix%d),l\n\tld (ix%d),h\n",
            LINE_E, LINE_E + 1, text_scan,
            text_done, text_done, text_done, text_done,
            text_scan, text_done, LINE_E, LINE_E + 1);

    mir_stream_puts("\tpush iy\n\tpop de\n", out);
    mir_stream_printf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n"
                 "\tor a\n\tsbc hl,de\n\tinc hl\n\tpush hl\n",
            LINE_E, LINE_E + 1);
    mir_machine_emit_symbol_call(out, plan->allocate_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "\tld (ix%d),l\n\tld (ix%d),h\n"
                 "\tld a,h\n\tor l\n\tjp nz,L%d\n"
                 "\tld hl,S%d\n\tpush hl\n",
            LINE_Q, LINE_Q + 1, allocation_ok,
            plan->allocation_string_id);
    mir_machine_emit_symbol_call(out, plan->die_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", allocation_ok);

    mir_stream_puts("\tpush iy\n\tpop de\n", out);
    mir_stream_printf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n"
                 "\tor a\n\tsbc hl,de\n\tld b,h\n\tld c,l\n"
                 "\tpush iy\n\tpop hl\n"
                 "\tld e,(ix%d)\n\tld d,(ix%d)\n",
            LINE_E, LINE_E + 1, LINE_Q, LINE_Q + 1);
    mir_emit_runtime_call(out, "__mcf");
    mir_stream_puts("\tpush iy\n\tpop de\n", out);
    mir_stream_printf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n"
                 "\tor a\n\tsbc hl,de\n\tex de,hl\n"
                 "\tld l,(ix%d)\n\tld h,(ix%d)\n"
                 "\tadd hl,de\n\txor a\n\tld (hl),a\n",
            LINE_E, LINE_E + 1, LINE_Q, LINE_Q + 1);

    mir_machine_emit_global_word(out, plan->line_count, 0);
    mir_stream_printf(out, "\tld de,%d\n\tor a\n\tsbc hl,de\n"
                 "\tjp c,L%d\n\tld hl,S%d\n\tpush hl\n",
            plan->line_limit, line_room,
            plan->line_limit_string_id);
    mir_machine_emit_symbol_call(out, plan->die_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", line_room);
    mir_machine_emit_global_word(out, plan->line_count, 0);
    mir_line_split_address(out, plan);
    mir_stream_printf(out, "\tld e,(ix%d)\n\tld d,(ix%d)\n"
                 "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
                 "\tinc hl\t\n\tld e,(ix%d)\n\tld d,(ix%d)\n"
                 "\tld (hl),e\n\tinc hl\n\tld (hl),d\n"
                 "\tinc hl\n\tld de,-1\n\tld (hl),e\n"
                 "\tinc hl\n\tld (hl),d\n",
            LINE_LN, LINE_LN + 1, LINE_Q, LINE_Q + 1);
    mir_machine_emit_global_word(out, plan->line_count, 0);
    mir_stream_puts("\tinc hl\n", out);
    mir_machine_emit_global_word_store(
        out, plan->line_count, 0);
    mir_stream_printf(out, "\tld l,(ix%d)\n\tld h,(ix%d)\n"
                 "\tpush hl\n\tpop iy\n\tjp L%d\n",
            LINE_E, LINE_E + 1, outer);

    mir_stream_printf(out,
            "L%d:\n\tld hl,1\n\tld (ix%d),l\n\tld (ix%d),h\n"
            "L%d:\n\tld l,(ix%d)\n\tld h,(ix%d)\n",
            parse_done, LINE_I, LINE_I + 1,
            sort_outer, LINE_I, LINE_I + 1);
    mir_machine_emit_global_word(out, plan->line_count, 0);
    mir_stream_puts("\tex de,hl\n\tld l,(ix-8)\n\tld h,(ix-7)\n"
          "\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp nc,L%d\n", done);
    mir_stream_puts("\tld l,(ix-8)\n\tld h,(ix-7)\n", out);
    mir_line_split_address(out, plan);
    mir_stream_puts("\tpush hl\n\tpush ix\n\tpop de\n"
          "\tld hl,-16\n\tadd hl,de\n\tex de,hl\n\tpop hl\n",
          out);
    mir_line_split_copy_six(out);
    mir_stream_puts("\tld l,(ix-8)\n\tld h,(ix-7)\n\tdec hl\n"
          "\tld (ix-10),l\n\tld (ix-9),h\n", out);

    mir_stream_printf(out, "L%d:\n\tld l,(ix%d)\n\tld h,(ix%d)\n"
                 "\tbit 7,h\n\tjp nz,L%d\n",
            sort_inner, LINE_J, LINE_J + 1, sort_insert);
    mir_line_split_address(out, plan);
    mir_stream_puts("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
          "\tld l,(ix-16)\n\tld h,(ix-15)\n"
          "\tld a,d\n\txor 128\n\tld d,a\n"
          "\tld a,h\n\txor 128\n\tld h,a\n"
          "\tex de,hl\n\tor a\n\tsbc hl,de\n", out);
    mir_stream_printf(out, "\tjp c,L%d\n\tjp z,L%d\n",
            sort_insert, sort_insert);

    mir_stream_puts("\tld l,(ix-10)\n\tld h,(ix-9)\n", out);
    mir_line_split_address(out, plan);
    mir_stream_puts("\tpush hl\n\tld de,6\n\tadd hl,de\n\tex de,hl\n"
          "\tpop hl\n", out);
    mir_line_split_copy_six(out);
    mir_stream_puts("\tdec (ix-10)\n", out);
    {
        int no_borrow = new_label();

        mir_stream_printf(out,
                "\tld a,(ix-10)\n\tcp 255\n\tjp nz,L%d\n"
                "\tdec (ix-9)\nL%d:\n",
                no_borrow, no_borrow);
    }
    mir_stream_printf(out, "\tjp L%d\nL%d:\n", sort_inner, sort_insert);
    mir_stream_puts("\tld l,(ix-10)\n\tld h,(ix-9)\n\tinc hl\n",
          out);
    mir_line_split_address(out, plan);
    mir_stream_puts("\tex de,hl\n\tpush ix\n\tpop hl\n"
          "\tld bc,-16\n\tadd hl,bc\n", out);
    mir_line_split_copy_six(out);
    mir_stream_puts("\tinc (ix-8)\n", out);
    {
        int no_carry = new_label();

        mir_stream_printf(out,
                "\tjp nz,L%d\n\tinc (ix-7)\nL%d:\n",
                no_carry, no_carry);
    }
    mir_stream_printf(out, "\tjp L%d\nL%d:\n"
                 "\tld sp,ix\n\tpop ix\n\tpop iy\n"
                 ";@dcc.reg free=iy\n\tret\n",
            sort_outer, done);
}

static int mir_match_c_state_lexer_regional(void)
{
    static const int copy_calls[] = {
        665, 724, 783, 842, 901,
        960, 1019, 1078, 1137, 1196
    };
    struct Sym *alnum_function;
    struct Sym *copy_function;
    struct Sym *digit_function;
    struct Sym *state;
    int counts[19] = {0};
    int instruction;
    int item;

    if (mir.count != 1218 || mir_cfg_block_count() != 157 ||
        mir.has_vla || mir.local_bytes != 8 ||
        mir.aggregate_temp_bytes != 0 ||
        !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int bucket;

        switch (insn->opcode) {
        case MIR_ADDRESS: bucket = 0; break;
        case MIR_ARG: bucket = 1; break;
        case MIR_BINARY: bucket = 2; break;
        case MIR_BRANCH_FALSE: bucket = 3; break;
        case MIR_CALL: bucket = 4; break;
        case MIR_CONST: bucket = 5; break;
        case MIR_INDEX_ADDRESS: bucket = 6; break;
        case MIR_JUMP: bucket = 7; break;
        case MIR_LABEL: bucket = 8; break;
        case MIR_LOAD: bucket = 9; break;
        case MIR_LOAD_INDIRECT: bucket = 10; break;
        case MIR_MEMBER_ADDRESS: bucket = 11; break;
        case MIR_NOP: bucket = 12; break;
        case MIR_PHI: bucket = 13; break;
        case MIR_RETURN: bucket = 14; break;
        case MIR_STORE: bucket = 15; break;
        case MIR_STORE_INDIRECT: bucket = 16; break;
        case MIR_STRING_ADDRESS: bucket = 17; break;
        case MIR_UNARY: bucket = 18; break;
        default:
            return mir_machine_reject(
                "c-state-lexer-regional", "opcode");
        }
        ++counts[bucket];
        if (insn->opcode == MIR_ADDRESS &&
            !mir_machine_named_nonvolatile(insn))
            return mir_machine_reject(
                "c-state-lexer-regional",
                "volatile-state");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "c-state-lexer-regional",
                "volatile-indirect-memory");
    }
    if (counts[0] != 130 || counts[1] != 33 ||
        counts[2] != 75 || counts[3] != 89 ||
        counts[4] != 20 || counts[5] != 153 ||
        counts[6] != 27 || counts[7] != 51 ||
        counts[8] != 157 || counts[9] != 29 ||
        counts[10] != 109 || counts[11] != 130 ||
        counts[12] != 58 || counts[13] != 35 ||
        counts[14] != 16 || counts[15] != 14 ||
        counts[16] != 46 || counts[17] != 13 ||
        counts[18] != 33)
        return mir_machine_reject(
            "c-state-lexer-regional", "population");

    state = find_global(mir.insns[2].name);
    if (state == NULL || state->is_array ||
        state->is_volatile ||
        (state->storage != SC_GLOBAL &&
         state->storage != SC_EXTERN))
        return mir_machine_reject(
            "c-state-lexer-regional", "state");
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ADDRESS &&
            find_global(mir.insns[instruction].name) != state)
            return mir_machine_reject(
                "c-state-lexer-regional", "state-root");
    if (mir.insns[3].immediate != 16 ||
        mir.insns[3].memory_size != 80 ||
        mir.insns[10].immediate != 12 ||
        mir.insns[10].memory_size != 2 ||
        mir.insns[14].immediate != 14 ||
        mir.insns[14].memory_size != 2 ||
        mir.insns[18].immediate != 6 ||
        mir.insns[18].memory_size != 4 ||
        mir.insns[21].immediate != 2 ||
        mir.insns[21].memory_size != 4 ||
        mir.insns[28].immediate != 0 ||
        mir.insns[28].memory_size != 2)
        return mir_machine_reject(
            "c-state-lexer-regional", "state-layout");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != MIR_MEMBER_ADDRESS)
            continue;
        if (!((insn->immediate == 16 &&
               insn->memory_size == 80) ||
              (insn->immediate == 12 &&
               insn->memory_size == 2) ||
              (insn->immediate == 14 &&
               insn->memory_size == 2) ||
              (insn->immediate == 6 &&
               insn->memory_size == 4) ||
              (insn->immediate == 2 &&
               insn->memory_size == 4) ||
              (insn->immediate == 0 &&
               insn->memory_size == 2)))
            return mir_machine_reject(
                "c-state-lexer-regional", "member-layout");
    }

    if ((state = find_global(mir.insns[1].name)) == NULL ||
        !state->is_defined || state->proto_nargs != 0 ||
        state->proto_variadic ||
        find_global(mir.insns[43].name) == NULL ||
        find_global(mir.insns[43].name)->proto_nargs != 1)
        return mir_machine_reject(
            "c-state-lexer-regional", "entry-calls");
    alnum_function = find_global(mir.insns[73].name);
    digit_function = find_global(mir.insns[228].name);
    copy_function = find_global(mir.insns[665].name);
    if (alnum_function == NULL ||
        digit_function == NULL || copy_function == NULL ||
        alnum_function->proto_nargs != 1 ||
        digit_function->proto_nargs != 1 ||
        copy_function->proto_nargs != 2 ||
        copy_function->proto_variadic ||
        find_global(mir.insns[136].name) != alnum_function ||
        find_global(mir.insns[239].name) != digit_function ||
        find_global(mir.insns[284].name) != digit_function ||
        find_global(mir.insns[186].name) == NULL ||
        find_global(mir.insns[186].name) !=
            find_global(mir.insns[205].name) ||
        find_global(mir.insns[437].name) == NULL ||
        find_global(mir.insns[437].name)->proto_nargs < 2)
        return mir_machine_reject(
            "c-state-lexer-regional", "calls");
    for (item = 0;
         item < (int)(sizeof(copy_calls) /
                      sizeof(copy_calls[0]));
         ++item)
        if (find_global(mir.insns[copy_calls[item]].name) !=
            copy_function)
            return mir_machine_reject(
                "c-state-lexer-regional", "copy-calls");
    if (mir.insns[184].immediate < 0 ||
        mir.insns[203].immediate !=
            mir.insns[184].immediate + 1 ||
        mir.insns[431].immediate !=
            mir.insns[203].immediate + 1)
        return mir_machine_reject(
            "c-state-lexer-regional", "strings");
    for (item = 0; item < 10; ++item)
        if (mir.insns[663 + 59 * item].immediate !=
            mir.insns[431].immediate + 1 + item)
            return mir_machine_reject(
                "c-state-lexer-regional",
                "operator-strings");
    return 1;
}

static int mir_emit_c_state_lexer_regional(MirStream *out)
{
    return mir_try_emit_compacted_regional_homed_cfg(out);
}

static int mir_match_comment_strip_function(
    const struct MirInsn *call, int argument_count,
    struct Sym **function_out)
{
    struct Sym *function;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        (call->memory_flags &
         (MIR_CALL_FLAG_VARIADIC |
          MIR_CALL_FLAG_FORMAT_RUNTIME |
          MIR_CALL_FLAG_INLINE_SUBSTITUTABLE)) != 0)
        return 0;
    function = find_global(call->name);
    if (function == NULL || function->storage != SC_FUNC ||
        function->is_funcptr || !function->has_proto ||
        function->proto_variadic ||
        function->proto_nargs != argument_count ||
        (call->base_name[0] != 0 &&
         strcmp(call->base_name,
                asm_name_for(sym_asm_name(function)))))
        return 0;
    *function_out = function;
    return 1;
}

static int mir_match_bounded_decimal_parse_schedule(
    struct MirBoundedDecimalParseSchedule *plan)
{
    static const unsigned char expected_opcodes[174] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_LOAD, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_LOAD,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT,
        MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_LOAD,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_UNARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_STORE,
        MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_PHI, MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY,
        MIR_ARG, MIR_CALL, MIR_BRANCH_FALSE, MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_NOP, MIR_STORE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_NOP, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_STORE, MIR_LOAD, MIR_CONST, MIR_BINARY,
        MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_LOAD,
        MIR_NOP, MIR_NOP, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_CONST, MIR_RETURN,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_STORE_INDIRECT, MIR_CONST, MIR_RETURN
    };
    static const int edges[][2] = {
        {14, 21}, {20, 5}, {27, 30}, {36, 41}, {49, 52},
        {74, 140}, {87, 91}, {90, 118}, {96, 104},
        {100, 104}, {103, 106}, {108, 112}, {111, 114},
        {117, 118}, {120, 123}, {139, 63}, {152, 159},
        {158, 141}, {165, 168}
    };
    const struct MirInsn *text = &mir.insns[1];
    const struct MirInsn *output = &mir.insns[2];
    struct Sym *space_function;
    struct Sym *digit_function;
    int edge;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 174 || mir_cfg_block_count() != 25 ||
        mir.local_bytes != 6 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "bounded-decimal-parse-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "bounded-decimal-parse-schedule",
                "volatile-memory");
    }
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "bounded-decimal-parse-schedule", "control-flow");
    if (type_ptr_depth(text->type) != 1 ||
        (text->type & 15) != TYPE_CHAR ||
        type_ptr_depth(output->type) != 1 ||
        (output->type & 15) != TYPE_INT ||
        (output->type & TYPE_UNSIGNED) == 0 ||
        mir_machine_pointee_is_volatile(text) ||
        mir_machine_pointee_is_volatile(output) ||
        !mir_machine_parameter_value_offset(
            text->dst, &plan->text_stack_offset) ||
        !mir_machine_parameter_value_offset(
            output->dst, &plan->output_stack_offset))
        return mir_machine_reject(
            "bounded-decimal-parse-schedule", "parameters");
    if (!mir_match_comment_strip_function(
            &mir.insns[13], 1, &space_function) ||
        !mir_match_comment_strip_function(
            &mir.insns[151], 1, &plan->space_function) ||
        space_function != plan->space_function ||
        !mir_match_comment_strip_function(
            &mir.insns[47], 1, &digit_function) ||
        !mir_match_comment_strip_function(
            &mir.insns[73], 1, &plan->digit_function) ||
        digit_function != plan->digit_function)
        return mir_machine_reject(
            "bounded-decimal-parse-schedule", "calls");
    if (!mir_machine_constant_equals(mir.insns[16].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[24].dst, '-') ||
        !mir_machine_constant_equals(mir.insns[28].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[33].dst, '+') ||
        !mir_machine_constant_equals(mir.insns[38].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[50].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[54].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[78].dst, '0') ||
        !mir_machine_constant_equals(mir.insns[84].dst, 51) ||
        !mir_machine_constant_equals(mir.insns[93].dst, 51) ||
        !mir_machine_constant_equals(mir.insns[98].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[125].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[134].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[154].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[162].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[166].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[172].dst, 1))
        return mir_machine_reject(
            "bounded-decimal-parse-schedule", "constants");
    plan->maximum_value = 511;
    return 1;
}

static void mir_emit_bounded_decimal_parse_schedule(
    MirStream *out, const struct MirBoundedDecimalParseSchedule *plan)
{
    int skip_leading = new_label();
    int leading_done = new_label();
    int no_plus = new_label();
    int digits = new_label();
    int digit_done = new_label();
    int value_not_51 = new_label();
    int skip_trailing = new_label();
    int trailing_done = new_label();
    int fail = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tdec sp\n\tdec sp\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n"
            "L%d:\n\tld l,(iy+0)\n\tld h,0\n\tpush hl\n",
            plan->text_stack_offset + 4,
            plan->text_stack_offset + 5, skip_leading);
    mir_machine_emit_symbol_call(out, plan->space_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tinc iy\n\tjp L%d\n"
            "L%d:\n\tld a,(iy+0)\n\tcp '-'\n\tjp z,L%d\n"
            "\tcp '+'\n\tjp nz,L%d\n\tinc iy\n"
            "L%d:\n\tld l,(iy+0)\n\tld h,0\n\tpush hl\n",
            leading_done, skip_leading, leading_done,
            fail, no_plus, no_plus);
    mir_machine_emit_symbol_call(out, plan->digit_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\txor a\n\tld (ix-2),a\n\tld (ix-1),a\n"
            "L%d:\n\tld l,(iy+0)\n\tld h,0\n\tpush hl\n",
            fail, digits);
    mir_machine_emit_symbol_call(out, plan->digit_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tld a,(iy+0)\n\tsub '0'\n\tld c,a\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tld de,52\n\tor a\n\tsbc hl,de\n\tjp nc,L%d\n"
            "\tld a,(ix-1)\n\tor a\n\tjp nz,L%d\n"
            "\tld a,(ix-2)\n\tcp 51\n\tjp nz,L%d\n"
            "\tld a,c\n\tcp 2\n\tjp nc,L%d\n"
            "L%d:\n\tld l,(ix-2)\n\tld h,(ix-1)\n"
            "\tadd hl,hl\n\tld d,h\n\tld e,l\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,de\n"
            "\tld e,c\n\tld d,0\n\tadd hl,de\n"
            "\tld (ix-2),l\n\tld (ix-1),h\n\tinc iy\n"
            "\tjp L%d\n"
            "L%d:\n",
            digit_done, fail, fail, value_not_51,
            fail, value_not_51, digits, digit_done);
    mir_stream_printf(out,
            "L%d:\n\tld l,(iy+0)\n\tld h,0\n\tpush hl\n",
            skip_trailing);
    mir_machine_emit_symbol_call(out, plan->space_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out,
            "\tjp z,L%d\n\tinc iy\n\tjp L%d\n"
            "L%d:\n\tld a,(iy+0)\n\tor a\n\tjp nz,L%d\n"
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tld a,(ix-2)\n\tld (hl),a\n\tinc hl\n"
            "\tld a,(ix-1)\n\tld (hl),a\n\tld hl,1\n\tjp L%d\n"
            "L%d:\n\tld hl,0\n"
            "L%d:\n\tld sp,ix\n\tpop ix\n\tpop iy\n\tret\n",
            trailing_done, skip_trailing, trailing_done, fail,
            plan->output_stack_offset + 4,
            plan->output_stack_offset + 5,
            done, fail, done);
}

static int mir_match_bounded_uppercase_schedule(
    struct MirBoundedUppercaseSchedule *plan)
{
    static const unsigned char expected_opcodes[55] = {
        MIR_LABEL, MIR_PARAM, MIR_PARAM, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP,
        MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_NOP,
        MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_ARG, MIR_CALL,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT
    };
    static const int edges[][2] = {
        {14, 24}, {20, 24}, {23, 26}, {28, 48}, {47, 6}
    };
    const struct MirInsn *destination = &mir.insns[1];
    const struct MirInsn *source = &mir.insns[2];
    int edge;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 55 || mir_cfg_block_count() != 7 ||
        mir.local_bytes != 2 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        (mir.return_type & 15) != TYPE_VOID)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "bounded-uppercase-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->memory_size != 1))
            return mir_machine_reject(
                "bounded-uppercase-schedule",
                "volatile-memory");
    }
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "bounded-uppercase-schedule", "control-flow");
    if (type_ptr_depth(destination->type) != 1 ||
        type_ptr_depth(source->type) != 1 ||
        (destination->type & 15) != TYPE_CHAR ||
        (source->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(destination) ||
        mir_machine_pointee_is_volatile(source) ||
        !mir_machine_parameter_value_offset(
            destination->dst, &plan->destination_stack_offset) ||
        !mir_machine_parameter_value_offset(
            source->dst, &plan->source_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[18].dst, 17) ||
        !mir_machine_constant_equals(mir.insns[22].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[44].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[53].dst, 0) ||
        !mir_match_comment_strip_function(
            &mir.insns[39], 1, &plan->upper_function) ||
        mir.insns[38].src1 != mir.insns[37].dst ||
        mir.insns[41].src1 != mir.insns[31].dst ||
        mir.insns[41].src2 != mir.insns[40].dst ||
        mir.insns[54].src1 != mir.insns[51].dst ||
        mir.insns[54].src2 != mir.insns[53].dst)
        return mir_machine_reject(
            "bounded-uppercase-schedule", "semantics");
    plan->maximum_length = 16;
    return 1;
}

static int mir_match_hex_word_parse_schedule(
    struct MirHexWordParseSchedule *plan)
{
    static const unsigned char expected_opcodes[137] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_CONST, MIR_LOAD, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_LOAD, MIR_PHI,
        MIR_LOAD, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_JUMP, MIR_LABEL,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL,
        MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST,
        MIR_BINARY, MIR_UNARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_UNARY,
        MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL,
        MIR_CONST, MIR_NOP, MIR_UNARY, MIR_BINARY, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_STORE,
        MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_LABEL, MIR_LABEL, MIR_NOP,
        MIR_CONST, MIR_NOP, MIR_BINARY, MIR_LOAD, MIR_BINARY, MIR_NOP, MIR_STORE, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_RETURN
    };
    static const int edges[][2] = {
        {13, 20}, {19, 5}, {28, 134}, {33, 42}, {38, 42},
        {41, 44}, {46, 55}, {54, 118}, {60, 69}, {65, 69},
        {68, 71}, {73, 84}, {83, 117}, {89, 98}, {94, 98},
        {97, 100}, {102, 134}, {112, 116}, {115, 134},
        {133, 21}
    };
    const struct MirInsn *text = &mir.insns[1];
    int edge;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 137 || mir_cfg_block_count() != 25 ||
        mir.local_bytes != 5 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        (mir.return_type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "hex-word-parse-schedule", "opcodes");
        if (insn->opcode == MIR_LOAD_INDIRECT &&
            ((insn->memory_flags & (1 | 8)) != 0 ||
             insn->memory_size != 1))
            return mir_machine_reject(
                "hex-word-parse-schedule", "volatile-memory");
    }
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "hex-word-parse-schedule", "control-flow");
    if (type_ptr_depth(text->type) != 1 ||
        (text->type & 15) != TYPE_CHAR ||
        mir_machine_pointee_is_volatile(text) ||
        !mir_machine_parameter_value_offset(
            text->dst, &plan->text_stack_offset) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[8].dst, ' ') ||
        !mir_machine_constant_equals(mir.insns[15].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[30].dst, '0') ||
        !mir_machine_constant_equals(mir.insns[35].dst, '9') ||
        !mir_machine_constant_equals(mir.insns[49].dst, '0') ||
        !mir_machine_constant_equals(mir.insns[57].dst, 'a') ||
        !mir_machine_constant_equals(mir.insns[62].dst, 'f') ||
        !mir_machine_constant_equals(mir.insns[75].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[79].dst, 'a') ||
        !mir_machine_constant_equals(mir.insns[86].dst, 'A') ||
        !mir_machine_constant_equals(mir.insns[91].dst, 'F') ||
        !mir_machine_constant_equals(mir.insns[104].dst, 10) ||
        !mir_machine_constant_equals(mir.insns[108].dst, 'A') ||
        !mir_machine_constant_equals(mir.insns[120].dst, 16) ||
        !mir_machine_constant_equals(mir.insns[128].dst, 1))
        return mir_machine_reject(
            "hex-word-parse-schedule", "semantics");
    return 1;
}

static void mir_emit_hex_word_parse_schedule(
    MirStream *out, const struct MirHexWordParseSchedule *plan)
{
    int skip_space = new_label();
    int parse = new_label();
    int decimal = new_label();
    int lower = new_label();
    int upper = new_label();
    int digit_ready = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld hl,0\n"
            "L%d:\n\tld a,(de)\n\tcp ' '\n\tjp nz,L%d\n"
            "\tinc de\n\tjp L%d\n"
            "L%d:\n\tld a,(de)\n\tor a\n\tjp z,L%d\n"
            "\tcp '0'\n\tjp c,L%d\n\tcp '9'+1\n\tjp c,L%d\n"
            "\tcp 'A'\n\tjp c,L%d\n\tcp 'F'+1\n\tjp c,L%d\n"
            "\tcp 'a'\n\tjp c,L%d\n\tcp 'f'+1\n\tjp nc,L%d\n"
            "\tjp L%d\n"
            "L%d:\n\tsub 'A'-10\n\tjp L%d\n"
            "L%d:\n\tsub 'a'-10\n\tjp L%d\n"
            "L%d:\n\tsub '0'\n"
            "L%d:\n\tld c,a\n\tld b,0\n"
            "\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n\tadd hl,hl\n"
            "\tadd hl,bc\n\tinc de\n"
            "\tjp L%d\n"
            "L%d:\n\tret\n",
            plan->text_stack_offset,
            skip_space, parse, skip_space, parse, done,
            done, decimal, done, upper, done, done, lower,
            upper, digit_ready, lower, digit_ready,
            decimal, digit_ready, parse, done);
}

static void mir_emit_bounded_uppercase_schedule(
    MirStream *out, const struct MirBoundedUppercaseSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n"
            "\tpush hl\n\tpop iy\n"
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tld b,%d\n"
            "L%d:\n\tld a,(de)\n\tor a\n\tjp z,L%d\n"
            "\tpush bc\n\tpush de\n\tld l,a\n\tld h,0\n\tpush hl\n",
            plan->destination_stack_offset + 2,
            plan->source_stack_offset + 2,
            plan->maximum_length, loop, done);
    mir_machine_emit_symbol_call(out, plan->upper_function);
    mir_stream_puts("\tpop bc\n\tpop de\n\tpop bc\n"
          "\tld (iy+0),l\n\tinc iy\n\tinc de\n\tdjnz ", out);
    mir_stream_printf(out,
            "L%d\nL%d:\n\txor a\n\tld (iy+0),a\n"
            "\tpop iy\n\tret\n",
            loop, done);
}

static int mir_match_neighbor_warning_schedule(
    struct MirNeighborWarningSchedule *plan)
{
    static const unsigned char expected_opcodes[123] = {
        MIR_LABEL, MIR_PARAM, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP,
        MIR_STORE, MIR_NOP, MIR_CONST, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_PHI,
        MIR_NOP, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS,
        MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_NOP, MIR_STORE, MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS,
        MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_NOP, MIR_NOP, MIR_MEMBER_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
        MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_RETURN
    };
    static const int edges[][2] = {
        {20, 120}, {36, 40}, {48, 52}, {51, 70}, {60, 64},
        {63, 66}, {69, 70}, {72, 76}, {84, 88}, {87, 106},
        {96, 100}, {99, 102}, {105, 106}, {108, 112},
        {119, 12}
    };
    static const int member_instructions[6] = {
        3, 31, 43, 55, 79, 91
    };
    static const int location_indices[6] = {
        0, 1, 2, 3, 4, 5
    };
    static const int call_instructions[3] = {39, 75, 111};
    static const int string_instructions[3] = {37, 73, 109};
    const struct MirInsn *parameter = &mir.insns[1];
    struct Sym *function = NULL;
    long cave_offset;
    int edge;
    int instruction;
    int item;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 123 || mir_cfg_block_count() != 21 ||
        mir.local_bytes != 5 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "neighbor-warning-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "neighbor-warning-schedule", "volatile-memory");
    }
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "neighbor-warning-schedule", "control-flow");
    if (type_ptr_depth(parameter->type) != 1 ||
        type_size(parameter->type) != 2 ||
        mir_machine_pointee_is_volatile(parameter) ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->game_stack_offset))
        return mir_machine_reject(
            "neighbor-warning-schedule", "parameter");
    plan->location_offset = (int)mir.insns[3].immediate;
    if (plan->location_offset < -120 ||
        plan->location_offset > 116)
        return mir_machine_reject(
            "neighbor-warning-schedule", "location-offset");
    for (item = 0; item < 6; ++item) {
        const struct MirInsn *member =
            &mir.insns[member_instructions[item]];
        const struct MirInsn *index =
            &mir.insns[member_instructions[item] + 1];

        if (member->src1 != parameter->dst ||
            member->immediate != plan->location_offset ||
            member->memory_size != 12 ||
            (member->memory_flags & (1 | 8)) != 0 ||
            !mir_machine_constant_equals(
                index->dst, location_indices[item]))
            return mir_machine_reject(
                "neighbor-warning-schedule", "locations");
    }
    if (!mir_machine_global_address_offset(
            mir.insns[21].dst, &plan->cave, &cave_offset, 0) ||
        plan->cave == NULL || cave_offset < -32768 ||
        cave_offset > 32767 ||
        mir.insns[23].immediate != 6 ||
        mir.insns[25].immediate != 2 ||
        !mir_machine_constant_equals(mir.insns[17].dst, 3) ||
        !mir_machine_constant_equals(mir.insns[116].dst, 1))
        return mir_machine_reject(
            "neighbor-warning-schedule", "table");
    plan->cave_offset = (int)cave_offset;
    for (item = 0; item < 3; ++item) {
        const struct MirInsn *call =
            &mir.insns[call_instructions[item]];
        struct Sym *candidate = find_global(call->name);

        if (candidate == NULL || candidate->storage != SC_FUNC ||
            candidate->is_funcptr || candidate->is_noreturn ||
            !candidate->has_proto || !candidate->proto_variadic ||
            candidate->proto_nargs != 1 ||
            call->src1 >= 0 ||
            (call->memory_flags & MIR_CALL_FLAG_VARIADIC) == 0 ||
            call->base_name[0] == 0 ||
            mir.insns[string_instructions[item]].immediate < 0 ||
            mir.insns[string_instructions[item] + 1].src1 !=
                mir.insns[string_instructions[item]].dst ||
            (item == 0
                 ? (function = candidate, 0)
                 : candidate != function))
            return mir_machine_reject(
                "neighbor-warning-schedule", "calls");
        plan->string_ids[item] =
            (int)mir.insns[string_instructions[item]].immediate;
    }
    plan->print_function = function;
    snprintf(plan->print_name, sizeof(plan->print_name), "%s",
             mir.insns[call_instructions[0]].base_name);
    return 1;
}

static void mir_emit_neighbor_warning_compare(
    MirStream *out, const struct MirNeighborWarningSchedule *plan,
    int location_index, int string_id)
{
    int different = new_label();

    mir_stream_printf(out,
            "\tld l,(ix-4)\n\tld h,(ix-3)\n"
            "\tld e,(iy%+d)\n\tld d,(iy%+d)\n"
            "\tor a\n\tsbc hl,de\n\tjp nz,L%d\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->location_offset + location_index * 2,
            plan->location_offset + location_index * 2 + 1,
            different, string_id);
    mir_emit_runtime_call(out, plan->print_name);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out, "L%d:\n", different);
}

static void mir_emit_neighbor_warning_schedule(
    MirStream *out, const struct MirNeighborWarningSchedule *plan)
{
    int loop = new_label();
    int done = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n"
          "\tpush iy\n\tpush ix\n\tld ix,0\n\tadd ix,sp\n"
          "\tld hl,-4\n\tadd hl,sp\n\tld sp,hl\n",
          out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
            "\tpush hl\n\tpop iy\n\txor a\n\tld (ix-1),a\n"
            "L%d:\n"
            "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
            "\tadd hl,hl\n\tld d,h\n\tld e,l\n"
            "\tadd hl,hl\n\tadd hl,de\n\tpush hl\n",
            plan->game_stack_offset + 4,
            plan->game_stack_offset + 5,
            loop, plan->location_offset,
            plan->location_offset + 1);
    mir_machine_emit_global_address_de(
        out, plan->cave, plan->cave_offset);
    mir_stream_puts("\tpop hl\n\tadd hl,de\n"
          "\tld a,(ix-1)\n\tadd a,a\n\tld e,a\n\tld d,0\n"
          "\tadd hl,de\n\tld a,(hl)\n\tld (ix-4),a\n"
          "\tinc hl\n\tld a,(hl)\n\tld (ix-3),a\n",
          out);
    mir_emit_neighbor_warning_compare(
        out, plan, 1, plan->string_ids[0]);
    mir_emit_neighbor_warning_compare(
        out, plan, 2, plan->string_ids[1]);
    mir_emit_neighbor_warning_compare(
        out, plan, 3, plan->string_ids[1]);
    mir_emit_neighbor_warning_compare(
        out, plan, 4, plan->string_ids[2]);
    mir_emit_neighbor_warning_compare(
        out, plan, 5, plan->string_ids[2]);
    mir_stream_printf(out,
            "\tld a,(ix-1)\n\tinc a\n\tld (ix-1),a\n"
            "\tcp 3\n\tjp nz,L%d\n"
            "L%d:\n\tld hl,0\n\tld sp,ix\n\tpop ix\n"
            "\tpop iy\n\tret\n",
            loop, done);
}

static int mir_match_comment_strip_schedule(
    struct MirCommentStripSchedule *plan)
{
    static const unsigned char expected_opcodes[223] = {
        MIR_LABEL, MIR_PARAM, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_BINARY,
        MIR_ARG, MIR_CALL, MIR_NOP, MIR_UNARY, MIR_STORE, MIR_LOAD, MIR_UNARY, MIR_BRANCH_FALSE,
        MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_CONST,
        MIR_NOP, MIR_STORE, MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST,
        MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
        MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_LOAD, MIR_NOP,
        MIR_NOP, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST,
        MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS,
        MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST,
        MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_NOP, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LABEL, MIR_LOAD, MIR_NOP, MIR_NOP, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_LABEL, MIR_JUMP, MIR_LABEL,
        MIR_LOAD, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE,
        MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_LOAD,
        MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
        MIR_JUMP, MIR_NOP, MIR_LABEL, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
        MIR_INDEX_ADDRESS, MIR_LOAD_INDIRECT, MIR_UNARY, MIR_UNARY, MIR_STORE, MIR_NOP, MIR_CONST, MIR_BINARY,
        MIR_BRANCH_FALSE, MIR_LOAD, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_INDEX_ADDRESS, MIR_NOP,
        MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_LOAD,
        MIR_LOAD, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_LOAD, MIR_RETURN
    };
    static const int edges[][2] = {
        {15, 19}, {34, 214}, {42, 56}, {52, 56}, {55, 58},
        {60, 119}, {69, 81}, {77, 81}, {80, 83}, {85, 92},
        {91, 61}, {100, 26}, {117, 26}, {127, 186},
        {136, 148}, {144, 148}, {147, 150}, {152, 159},
        {158, 128}, {167, 26}, {184, 26}, {200, 210},
        {213, 26}
    };
    const struct MirInsn *parameter = &mir.insns[1];
    int edge;
    int instruction;

    memset(plan, 0, sizeof(*plan));
    if (mir.count != 223 || mir_cfg_block_count() != 25 ||
        mir.local_bytes != 8 || mir.aggregate_temp_bytes != 0 ||
        mir.has_vla || !mir_has_cfg_backedge() ||
        type_ptr_depth(mir.return_type) != 1 ||
        (mir.return_type & 15) != TYPE_CHAR ||
        type_size(mir.return_type) != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode != expected_opcodes[instruction])
            return mir_machine_reject(
                "comment-strip-schedule", "opcodes");
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_flags & (1 | 8)) != 0)
            return mir_machine_reject(
                "comment-strip-schedule", "volatile-memory");
    }
    for (edge = 0;
         edge < (int)(sizeof(edges) / sizeof(edges[0])); ++edge)
        if (mir.insns[edges[edge][0]].label !=
            mir.insns[edges[edge][1]].label)
            return mir_machine_reject(
                "comment-strip-schedule", "control-flow");
    if (type_ptr_depth(parameter->type) != 1 ||
        (parameter->type & 15) != TYPE_CHAR ||
        type_size(parameter->type) != 2 ||
        !mir_machine_parameter_value_offset(
            parameter->dst, &plan->input_stack_offset) ||
        !mir_machine_same_location(parameter, &mir.insns[2]) ||
        mir.insns[3].src1 != mir.insns[2].dst ||
        !mir_match_comment_strip_function(
            &mir.insns[4], 1, &plan->length_function) ||
        mir.insns[7].immediate != '+' ||
        mir.insns[7].src1 != mir.insns[4].dst ||
        mir.insns[7].src2 != mir.insns[5].dst ||
        !mir_machine_constant_equals(mir.insns[5].dst, 1) ||
        mir.insns[8].src1 != mir.insns[7].dst ||
        !mir_match_comment_strip_function(
            &mir.insns[9], 1, &plan->allocate_function) ||
        !mir_match_comment_strip_function(
            &mir.insns[18], 1, &plan->error_function) ||
        mir.insns[16].immediate < 0 ||
        mir.insns[17].src1 != mir.insns[16].dst)
        return mir_machine_reject(
            "comment-strip-schedule", "entry");
    if (
        !mir_machine_constant_equals(mir.insns[20].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[23].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[39].dst, '-') ||
        !mir_machine_constant_equals(mir.insns[49].dst, '-') ||
        !mir_machine_constant_equals(mir.insns[74].dst, '\n') ||
        !mir_machine_constant_equals(mir.insns[97].dst, '\n') ||
        !mir_machine_constant_equals(mir.insns[124].dst, '@') ||
        !mir_machine_constant_equals(mir.insns[141].dst, '\n') ||
        !mir_machine_constant_equals(mir.insns[164].dst, '\n') ||
        !mir_machine_constant_equals(mir.insns[198].dst, 26) ||
        !mir_machine_constant_equals(mir.insns[219].dst, 0))
        return mir_machine_reject(
            "comment-strip-schedule", "constants");
    if (
        mir.insns[220].src1 != mir.insns[217].dst ||
        mir.insns[220].src2 != mir.insns[219].dst ||
        !mir_machine_same_location(&mir.insns[12], &mir.insns[221]) ||
        mir.insns[222].src1 != mir.insns[221].dst)
        return mir_machine_reject(
            "comment-strip-schedule", "semantics");
    plan->error_string_id = (int)mir.insns[16].immediate;
    return 1;
}

static void mir_emit_comment_strip_schedule(
    MirStream *out, const struct MirCommentStripSchedule *plan)
{
    int scan = new_label();
    int at_comment = new_label();
    int dash_comment = new_label();
    int skip_at = new_label();
    int skip_dash = new_label();
    int normal = new_label();
    int copy_newline = new_label();
    int done = new_label();
    int allocated = new_label();

    mir_stream_puts(MIR_EXACT_KERNEL_MARKER "\n\tpush iy\n", out);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_stream_printf(out,
            "\tld hl,%d\n\tadd hl,sp\n"
            "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
            "\tpush de\n\tpop iy\n\tpush de\n",
            plan->input_stack_offset + 2);
    mir_machine_emit_symbol_call(out, plan->length_function);
    mir_stream_puts("\tpop bc\n\tinc hl\n\tpush hl\n", out);
    mir_machine_emit_symbol_call(out, plan->allocate_function);
    mir_stream_puts("\tpop bc\n\tld a,h\n\tor l\n", out);
    mir_stream_printf(out, "\tjp nz,L%d\n\tld hl,S%d\n\tpush hl\n",
            allocated, plan->error_string_id);
    mir_machine_emit_symbol_call(out, plan->error_function);
    mir_stream_puts("\tpop bc\n", out);
    mir_stream_printf(out,
            "L%d:\n\tld b,h\n\tld c,l\n\tld d,h\n\tld e,l\n"
            "L%d:\n\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
            "\tcp '-'\n\tjp nz,L%d\n\tld a,(iy+1)\n"
            "\tcp '-'\n\tjp z,L%d\n"
            "L%d:\n\tld a,(iy+0)\n\tcp '@'\n\tjp z,L%d\n"
            "\tld a,(iy+0)\n\tinc iy\n\tcp 26\n\tjp z,L%d\n"
            "\tld (de),a\n\tinc de\n\tjp L%d\n"
            "L%d:\n\tinc iy\n"
            "L%d:\n\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
            "\tcp 10\n\tjp z,L%d\n\tinc iy\n\tjp L%d\n"
            "L%d:\n\tinc iy\n"
            "L%d:\n\tld a,(iy+0)\n\tor a\n\tjp z,L%d\n"
            "\tcp 10\n\tjp z,L%d\n\tinc iy\n\tjp L%d\n"
            "L%d:\n\tld a,(iy+0)\n\tld (de),a\n"
            "\tinc de\n\tinc iy\n\tjp L%d\n"
            "L%d:\n\txor a\n\tld (de),a\n\tld h,b\n\tld l,c\n"
            "\tpop iy\n\tret\n",
            allocated, scan, done, normal, dash_comment,
            normal, at_comment, scan, scan,
            dash_comment, skip_dash, done, copy_newline, skip_dash,
            at_comment, skip_at, done, copy_newline, skip_at,
            copy_newline, scan, done);
}

int mir_try_emit_scanner_kernels(MirStream *out, int late)
{
    if (!late) {
        struct MirMoveParseSchedule move_parse_schedule;
        struct MirMoveUndoSchedule move_undo_schedule;
        struct MirMoveApplySchedule move_apply_schedule;
        struct MirBoardAttackSchedule board_attack_schedule;
        struct MirMoveTextSchedule move_text_schedule;
        struct MirSquareTextSchedule square_text_schedule;
        struct MirByteRecordCopySchedule byte_record_copy_schedule;
        struct MirLongIndexCommentCopySchedule
            long_index_comment_copy_schedule;
        struct MirLongIndexWordCountSchedule
            long_index_word_count_schedule;
        struct MirLongIndexByteLoopSchedule
            long_index_byte_loop_schedule;
        struct MirSquareGridLineSumSchedule
            square_grid_line_sum_schedule;
        struct MirVlaAffineFillSumSchedule
            vla_affine_fill_sum_schedule;
        struct MirVlaConstantFillSumSchedule
            vla_constant_fill_sum_schedule;
        struct MirPrintableByteSanitizeSchedule
            printable_byte_sanitize_schedule;
        struct MirInvariantByteSumSchedule invariant_byte_sum_schedule;
        struct MirSlidingMaximumSchedule sliding_maximum_schedule;
        struct MirDigitLabelSchedule digit_label_schedule;
        struct MirPreprocessorSchedule preprocessor_schedule;
        struct MirCommentStripSchedule comment_strip_schedule;
        struct MirNeighborWarningSchedule neighbor_warning_schedule;
        struct MirBoundedDecimalParseSchedule decimal_parse_schedule;
        struct MirBoundedUppercaseSchedule uppercase_schedule;
        struct MirHexWordParseSchedule hex_parse_schedule;
        struct MirTrimSchedule trim_schedule;
        struct MirBasicLexerSchedule basic_lexer_schedule;
        struct MirLineSplitSchedule line_split_schedule;
        struct MirBoundedStringMatchSchedule
            bounded_string_match_schedule;
        struct MirCommentScanSchedule comment_scan_schedule;
        struct MirWhitespaceScanSchedule whitespace_scan_schedule;
        struct MirDecimalLongScanSchedule decimal_long_scan_schedule;
        struct MirActionDecodeSchedule action_decode_schedule;
        struct MirBufferedDeclarationSchedule
            buffered_declaration_schedule;

        if (mir_match_digit_label_schedule(
                &digit_label_schedule)) {
            mir_emit_digit_label_schedule(
                out, &digit_label_schedule);
            return 1;
        }
        if (mir_match_preprocessor_schedule(
                &preprocessor_schedule)) {
            mir_emit_preprocessor_schedule(
                out, &preprocessor_schedule);
            return 1;
        }
        if (mir_match_bounded_decimal_parse_schedule(
                &decimal_parse_schedule)) {
            mir_emit_bounded_decimal_parse_schedule(
                out, &decimal_parse_schedule);
            return 1;
        }
        if (mir_match_bounded_uppercase_schedule(
                &uppercase_schedule)) {
            mir_emit_bounded_uppercase_schedule(
                out, &uppercase_schedule);
            return 1;
        }
        if (mir_match_hex_word_parse_schedule(
                &hex_parse_schedule)) {
            mir_emit_hex_word_parse_schedule(
                out, &hex_parse_schedule);
            return 1;
        }
        if (mir_match_comment_strip_schedule(
                &comment_strip_schedule)) {
            mir_emit_comment_strip_schedule(
                out, &comment_strip_schedule);
            return 1;
        }
        if (mir_match_neighbor_warning_schedule(
                &neighbor_warning_schedule)) {
            mir_emit_neighbor_warning_schedule(
                out, &neighbor_warning_schedule);
            return 1;
        }
        if (mir_match_trim_schedule(&trim_schedule)) {
            mir_emit_trim_schedule(out, &trim_schedule);
            return 1;
        }
        if (mir_match_basic_lexer_schedule(&basic_lexer_schedule)) {
            mir_emit_basic_lexer_schedule(out, &basic_lexer_schedule);
            return 1;
        }
        if (mir_match_line_split_schedule(&line_split_schedule)) {
            mir_emit_line_split_schedule(out, &line_split_schedule);
            return 1;
        }
        if (mir_match_c_state_lexer_regional() &&
            mir_emit_c_state_lexer_regional(out))
            return 1;
        if (mir_match_sliding_maximum_schedule(
                &sliding_maximum_schedule)) {
            mir_emit_sliding_maximum_schedule(
                out, &sliding_maximum_schedule);
            return 1;
        }
        if (mir_match_printable_byte_sanitize_schedule(
                &printable_byte_sanitize_schedule)) {
            mir_emit_printable_byte_sanitize_schedule(
                out, &printable_byte_sanitize_schedule);
            return 1;
        }
        if (mir_match_invariant_byte_sum_schedule(
                &invariant_byte_sum_schedule)) {
            mir_emit_invariant_byte_sum_schedule(
                out, &invariant_byte_sum_schedule);
            return 1;
        }
        if (mir_match_move_parse_schedule(
                &move_parse_schedule)) {
            mir_emit_move_parse_schedule(
                out, &move_parse_schedule);
            return 1;
        }
        if (mir_match_move_undo_schedule(
                &move_undo_schedule)) {
            mir_emit_move_undo_schedule(
                out, &move_undo_schedule);
            return 1;
        }
        if (mir_match_move_apply_schedule(
                &move_apply_schedule)) {
            mir_emit_move_apply_schedule(
                out, &move_apply_schedule);
            return 1;
        }
        if (mir_match_board_attack_schedule(
                &board_attack_schedule)) {
            mir_emit_board_attack_schedule(
                out, &board_attack_schedule);
            return 1;
        }
        if (mir_match_move_text_schedule(&move_text_schedule)) {
            mir_emit_move_text_schedule(out, &move_text_schedule);
            return 1;
        }
        if (mir_match_square_text_schedule(&square_text_schedule)) {
            mir_emit_square_text_schedule(out, &square_text_schedule);
            return 1;
        }
        if (mir_match_byte_record_copy_schedule(
                &byte_record_copy_schedule)) {
            mir_emit_byte_record_copy_schedule(
                out, &byte_record_copy_schedule);
            return 1;
        }
        if (mir_match_long_index_comment_copy_schedule(
                &long_index_comment_copy_schedule)) {
            mir_emit_long_index_comment_copy_schedule(
                out, &long_index_comment_copy_schedule);
            return 1;
        }
        if (mir_match_long_index_word_count_schedule(
                &long_index_word_count_schedule)) {
            mir_emit_long_index_word_count_schedule(
                out, &long_index_word_count_schedule);
            return 1;
        }
        if (mir_match_long_index_byte_loop_schedule(
                &long_index_byte_loop_schedule)) {
            mir_emit_long_index_byte_loop_schedule(
                out, &long_index_byte_loop_schedule);
            return 1;
        }
        if (mir_match_square_grid_line_sum_schedule(
                &square_grid_line_sum_schedule)) {
            mir_emit_square_grid_line_sum_schedule(
                out, &square_grid_line_sum_schedule);
            return 1;
        }
        if (mir_match_vla_constant_fill_sum_schedule(
                &vla_constant_fill_sum_schedule)) {
            mir_emit_vla_constant_fill_sum_schedule(
                out, &vla_constant_fill_sum_schedule);
            return 1;
        }
        if (mir_match_vla_affine_fill_sum_schedule(
                &vla_affine_fill_sum_schedule)) {
            mir_emit_vla_affine_fill_sum_schedule(
                out, &vla_affine_fill_sum_schedule);
            return 1;
        }
        if (mir_match_bounded_string_match_schedule(
                &bounded_string_match_schedule)) {
            mir_emit_bounded_string_match_schedule(
                out, &bounded_string_match_schedule);
            return 1;
        }
        {
            struct MirStarCommentScanSchedule
                star_comment_scan_schedule;

            if (mir_match_star_comment_scan_schedule(
                    &star_comment_scan_schedule)) {
                mir_emit_star_comment_scan_schedule(
                    out, &star_comment_scan_schedule);
                return 1;
            }
        }
        if (mir_match_comment_scan_schedule(
                &comment_scan_schedule)) {
            mir_emit_comment_scan_schedule(
                out, &comment_scan_schedule);
            return 1;
        }
        if (mir_match_whitespace_scan_schedule(
                &whitespace_scan_schedule)) {
            mir_emit_whitespace_scan_schedule(
                out, &whitespace_scan_schedule);
            return 1;
        }
        if (mir_match_decimal_long_scan_schedule(
                &decimal_long_scan_schedule)) {
            mir_emit_decimal_long_scan_schedule(
                out, &decimal_long_scan_schedule);
            return 1;
        }
        if (mir_match_action_decode_schedule(
                &action_decode_schedule)) {
            mir_emit_action_decode_schedule(
                out, &action_decode_schedule);
            return 1;
        }
        if (mir_match_buffered_declaration_schedule(
                &buffered_declaration_schedule)) {
            mir_emit_buffered_declaration_schedule(
                out, &buffered_declaration_schedule);
            return 1;
        }
        return -1;
    }
    {
        struct MirSymbolFindSchedule symbol_find_schedule;
        struct MirSymbolInsertSchedule symbol_insert_schedule;
        struct MirBreadthFirstPathSchedule breadth_first_path_schedule;

        if (mir_match_symbol_insert_schedule(
                &symbol_insert_schedule)) {
            mir_emit_symbol_insert_schedule(
                out, &symbol_insert_schedule);
            return 1;
        }
        if (mir_match_symbol_find_schedule(
                &symbol_find_schedule)) {
            mir_emit_symbol_find_schedule(
                out, &symbol_find_schedule);
            return 1;
        }
        if (mir_match_breadth_first_path_schedule(
                &breadth_first_path_schedule)) {
            mir_emit_breadth_first_path_schedule(
                out, &breadth_first_path_schedule);
            return 1;
        }
    }
    return -1;
}
