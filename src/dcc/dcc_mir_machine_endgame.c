/* dcc_mir_machine_endgame.c - strict late call-runner schedules. */

#include "dcc_mir_machine_internal.h"

#define MIR_ENDGAME_MAX_ARGS 16
#define MIR_ENDGAME_FLOAT_CHECKS 61
#define MIR_ENDGAME_WIDTH_CALLS 14

enum MirEndgameFloatOperandKind {
    MIR_ENDGAME_FLOAT_BITS,
    MIR_ENDGAME_FLOAT_GLOBAL
};

struct MirEndgameFloatOperand {
    int kind;
    unsigned long bits;
};

struct MirEndgameFloatCheck {
    struct MirEndgameFloatOperand left;
    struct MirEndgameFloatOperand right;
    int operation;
    int want;
    int string_id;
};

struct MirEndgameFloatRunner {
    struct Sym *check_function;
    struct Sym *nan_symbol;
    struct Sym *checks;
    struct Sym *failures;
    struct MirEndgameFloatCheck checks_plan[MIR_ENDGAME_FLOAT_CHECKS];
    struct MirEndgameFloatCheck final_check;
    unsigned long initial_bits;
    unsigned long bound_bits;
    unsigned long step_bits;
    unsigned long break_bits;
    int summary_string;
    int result_string;
    int pass_string;
    int fail_string;
    char summary_call[64];
    char result_call[64];
};

struct MirEndgameWidthRunner {
    struct Sym *print_function;
    int strings[8];
    char call_names[MIR_ENDGAME_WIDTH_CALLS][64];
    int outcomes[3];
    int passed;
};

struct MirEndgameWidthEdge {
    int instruction;
    int target;
};

struct MirEndgameWidthPhi {
    int instruction;
    int first_value;
    int second_value;
    int first_predecessor;
    int second_predecessor;
};

static const unsigned char mir_endgame_width_opcodes[] = {
    MIR_LABEL, MIR_CONST, MIR_STORE, MIR_CONST, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
    MIR_NOP, MIR_CONST, MIR_BRANCH_FALSE, MIR_NOP, MIR_CONST, MIR_NOP, MIR_CONST, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI,
    MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
    MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_NOP, MIR_NOP,
    MIR_NOP, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP,
    MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_NOP,
    MIR_CONST, MIR_UNARY, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BRANCH_FALSE,
    MIR_LABEL, MIR_CONST, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE,
    MIR_LABEL, MIR_CONST, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_UNARY, MIR_CONST, MIR_NOP,
    MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST,
    MIR_BINARY, MIR_STORE, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
    MIR_LABEL, MIR_NOP, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
    MIR_CONST, MIR_CONST, MIR_BINARY, MIR_NOP, MIR_CONST, MIR_CONST, MIR_BINARY, MIR_BINARY,
    MIR_BRANCH_FALSE, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_JUMP,
    MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_PHI, MIR_BRANCH_FALSE, MIR_LABEL, MIR_CONST, MIR_CONST,
    MIR_NOP, MIR_BINARY, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_LABEL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_NOP,
    MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_JUMP,
    MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
    MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE,
    MIR_CONST, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI,
    MIR_RETURN,
};

static const struct MirEndgameWidthEdge mir_endgame_width_edges[] = {
    {18, 28}, {24, 28}, {27, 30}, {32, 40}, {36, 40},
    {39, 42}, {44, 71}, {53, 64}, {63, 75}, {70, 75},
    {91, 99}, {95, 99}, {98, 101}, {103, 131}, {113, 124},
    {123, 135}, {130, 135}, {152, 160}, {156, 160},
    {159, 162}, {164, 192}, {174, 185}, {184, 196},
    {191, 196}, {207, 211}, {210, 214}
};

static const struct MirEndgameWidthPhi mir_endgame_width_phis[] = {
    {31, 26, 29, 25, 28},
    {43, 38, 41, 37, 40},
    {102, 97, 100, 96, 99},
    {163, 158, 161, 157, 160},
    {215, 208, 212, 209, 213}
};

static const unsigned char mir_endgame_file_opcodes[] = {
    MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_CONST,
    MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE,
    MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_STORE_INDIRECT,
    MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS,
    MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_NOP,
    MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI,
    MIR_LOAD, MIR_NOP, MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP,
    MIR_INDEX_ADDRESS, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP,
    MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS,
    MIR_NOP, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CALL,
    MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP,
    MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_PHI, MIR_LOAD, MIR_NOP, MIR_LOAD,
    MIR_NOP, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_NOP, MIR_INDEX_ADDRESS, MIR_LOAD,
    MIR_ARG, MIR_CALL, MIR_UNARY, MIR_STORE_INDIRECT, MIR_LABEL, MIR_NOP, MIR_CONST, MIR_BINARY,
    MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_NOP, MIR_CONST,
    MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
    MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_LOAD,
    MIR_ARG, MIR_ADDRESS, MIR_NOP, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_LABEL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CALL, MIR_LABEL, MIR_CONST, MIR_RETURN,
};

static const unsigned char mir_endgame_format_opcodes[] = {
    MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_NOP, MIR_CONST, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_FLOAT_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_FLOAT_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_FLOAT_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_FLOAT_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_FLOAT_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_FLOAT_CONST, MIR_UNARY, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_FLOAT_CONST, MIR_ARG,
    MIR_CALL, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_CONST, MIR_NOP, MIR_STORE,
    MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD,
    MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE,
    MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST,
    MIR_STORE_INDIRECT, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_CONST, MIR_BINARY,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ADDRESS,
    MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP,
    MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LOAD,
    MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
    MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS,
    MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG,
    MIR_CALL, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP, MIR_STORE, MIR_LABEL,
    MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS, MIR_LOAD, MIR_INDEX_ADDRESS,
    MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_STORE, MIR_JUMP,
    MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_ARG, MIR_CALL, MIR_CONST, MIR_NOP,
    MIR_STORE, MIR_LABEL, MIR_NOP, MIR_LOAD, MIR_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_ADDRESS,
    MIR_LOAD, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT, MIR_LABEL, MIR_LOAD, MIR_CONST, MIR_BINARY,
    MIR_STORE, MIR_JUMP, MIR_LABEL, MIR_ADDRESS, MIR_CONST, MIR_INDEX_ADDRESS, MIR_CONST, MIR_STORE_INDIRECT,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_ADDRESS, MIR_ARG, MIR_CALL, MIR_ADDRESS, MIR_ARG, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_LOAD, MIR_ARG, MIR_CALL, MIR_NOP, MIR_STRING_ADDRESS, MIR_ARG, MIR_CALL, MIR_CONST,
    MIR_RETURN,
};

static const unsigned char mir_endgame_float_opcodes[] = {
    MIR_LABEL, MIR_FLOAT_CONST, MIR_STORE, MIR_FLOAT_CONST, MIR_STORE, MIR_LOAD, MIR_STORE, MIR_FLOAT_CONST,
    MIR_STORE, MIR_FLOAT_CONST, MIR_STORE, MIR_FLOAT_CONST, MIR_STORE, MIR_FLOAT_CONST, MIR_STORE, MIR_FLOAT_CONST,
    MIR_STORE, MIR_FLOAT_CONST, MIR_STORE, MIR_FLOAT_CONST, MIR_STORE, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_FLOAT_CONST, MIR_UNARY, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_FLOAT_CONST, MIR_UNARY,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_FLOAT_CONST, MIR_UNARY, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP,
    MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS,
    MIR_ARG, MIR_FLOAT_CONST, MIR_NOP, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL,
    MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG,
    MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_ARG, MIR_CONST,
    MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_FLOAT_CONST, MIR_NOP, MIR_BINARY, MIR_ARG,
    MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP, MIR_BINARY,
    MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_NOP, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_LABEL, MIR_NOP, MIR_NOP,
    MIR_NOP, MIR_PHI, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP, MIR_NOP,
    MIR_NOP, MIR_NOP, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_NOP,
    MIR_STORE, MIR_NOP, MIR_FLOAT_CONST, MIR_BINARY, MIR_BRANCH_FALSE, MIR_NOP, MIR_JUMP, MIR_LABEL,
    MIR_NOP, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_NOP,
    MIR_BINARY, MIR_ARG, MIR_CONST, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD,
    MIR_ARG, MIR_LOAD, MIR_ARG, MIR_CALL, MIR_STRING_ADDRESS, MIR_ARG, MIR_LOAD, MIR_CONST,
    MIR_BINARY, MIR_BRANCH_FALSE, MIR_STRING_ADDRESS, MIR_LABEL, MIR_JUMP, MIR_LABEL, MIR_STRING_ADDRESS, MIR_LABEL,
    MIR_LABEL, MIR_PHI, MIR_ARG, MIR_CALL, MIR_LOAD, MIR_BRANCH_FALSE, MIR_CONST, MIR_LABEL,
    MIR_JUMP, MIR_LABEL, MIR_CONST, MIR_LABEL, MIR_LABEL, MIR_PHI, MIR_RETURN,
};

static int mir_endgame_word_type(int type)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == TYPE_INT &&
           type_size(type) == 2;
}

static int mir_endgame_char_pointer_type(int type)
{
    return type_ptr_depth(type) == 1 &&
           (type & 15) == TYPE_CHAR &&
           type_size(type) == 2;
}

static int mir_endgame_opcode_sequence(
    const unsigned char *expected, size_t count)
{
    size_t instruction;

    if ((size_t)mir.count != count)
        return 0;
    for (instruction = 0; instruction < count; ++instruction)
        if (mir.insns[instruction].opcode != expected[instruction])
            return 0;
    return 1;
}

static int mir_endgame_call_arguments(
    const struct MirInsn *call, int arguments[MIR_ENDGAME_MAX_ARGS])
{
    int count = 0;
    int instruction;
    int item;

    for (item = 0; item < MIR_ENDGAME_MAX_ARGS; ++item)
        arguments[item] = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *arg = &mir.insns[instruction];
        int index;

        if (arg->opcode != MIR_ARG ||
            arg->secondary_offset != call->secondary_offset)
            continue;
        index = (int)arg->immediate;
        if (index < 0 || index >= MIR_ENDGAME_MAX_ARGS ||
            arguments[index] >= 0)
            return -1;
        arguments[index] = arg->src1;
        if (index >= count)
            count = index + 1;
    }
    for (item = 0; item < count; ++item)
        if (arguments[item] < 0)
            return -1;
    return count;
}

static struct Sym *mir_endgame_call_function(
    const struct MirInsn *call, int variadic, int fixed_arguments)
{
    struct Sym *function;
    const char *assembly_name;

    if (call->opcode != MIR_CALL || call->src1 >= 0 ||
        ((call->memory_flags & MIR_CALL_FLAG_VARIADIC) != 0) != variadic ||
        (function = find_global(call->name)) == NULL ||
        function->storage != SC_FUNC || function->is_funcptr ||
        function->is_noreturn || !function->has_proto ||
        function->proto_variadic != variadic ||
        function->proto_nargs != fixed_arguments ||
        call->type != function->type)
        return NULL;
    assembly_name = asm_name_for(sym_asm_name(function));
    if (!variadic && call->base_name[0] != 0 &&
        strcmp(call->base_name, assembly_name))
        return NULL;
    if (variadic && call->base_name[0] == 0)
        return NULL;
    return function;
}

static int mir_endgame_constant_bits(
    int value, unsigned long *bits, int *width, int depth)
{
    const struct MirInsn *definition;

    if (depth > 8 || (definition = mir_definition(value)) == NULL)
        return 0;
    if (definition->opcode == MIR_CONST ||
        definition->opcode == MIR_FLOAT_CONST) {
        *width = type_size(definition->type);
        if (*width != 2 && *width != 4)
            return 0;
        *bits = (unsigned long)definition->immediate;
        if (*width == 2)
            *bits &= 0xffffUL;
        return 1;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == '-' &&
        mir_endgame_constant_bits(
            definition->src1, bits, width, depth + 1)) {
        if (*width == 4 && type_is_float(definition->type))
            *bits ^= 0x80000000UL;
        else
            *bits = (unsigned long)(0UL - *bits);
        return 1;
    }
    return 0;
}

static int mir_endgame_direct_argument(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    unsigned long bits;
    int width;

    return definition != NULL &&
        ((definition->opcode == MIR_STRING_ADDRESS &&
          definition->immediate >= 0 &&
          mir_endgame_char_pointer_type(definition->type)) ||
         mir_endgame_constant_bits(value, &bits, &width, 0));
}

static int mir_endgame_emit_direct_argument(FILE *out, int value)
{
    const struct MirInsn *definition = mir_definition(value);
    unsigned long bits;
    int width;

    if (definition != NULL &&
        definition->opcode == MIR_STRING_ADDRESS) {
        fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
                definition->immediate);
        return 1;
    }
    if (!mir_endgame_constant_bits(value, &bits, &width, 0))
        fatal("invalid endgame direct argument");
    if (width == 4) {
        mir_machine_emit_float_bits(out, bits);
        fputs("\tpush de\n\tpush hl\n", out);
        return 2;
    }
    fprintf(out, "\tld hl,%lu\n\tpush hl\n", bits & 0xffffUL);
    return 1;
}

static const char *mir_endgame_call_name(const struct MirInsn *call)
{
    struct Sym *function = find_global(call->name);

    if (call->base_name[0] != 0)
        return call->base_name;
    if (function == NULL)
        fatal("missing endgame call function");
    return asm_name_for(sym_asm_name(function));
}

static void mir_endgame_emit_direct_call(FILE *out, int instruction)
{
    const struct MirInsn *call = &mir.insns[instruction];
    int arguments[MIR_ENDGAME_MAX_ARGS];
    int argument_count = mir_endgame_call_arguments(call, arguments);
    int words = 0;
    int argument;

    if (argument_count < 0)
        fatal("invalid endgame direct call");
    for (argument = argument_count - 1; argument >= 0; --argument)
        words += mir_endgame_emit_direct_argument(
            out, arguments[argument]);
    if ((call->memory_flags & MIR_CALL_FLAG_FORMAT_HEX) != 0)
        mir_emit_runtime_call(out, "__pfehx");
    if ((call->memory_flags & MIR_CALL_FLAG_FORMAT_OCTAL) != 0)
        mir_emit_runtime_call(out, "__pfeoc");
    mir_emit_runtime_call(out, mir_endgame_call_name(call));
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static int mir_endgame_same_address(
    int first_instruction, int second_instruction)
{
    const struct MirInsn *first = &mir.insns[first_instruction];
    const struct MirInsn *second = &mir.insns[second_instruction];

    return first->opcode == MIR_ADDRESS &&
           second->opcode == MIR_ADDRESS &&
           !strcmp(first->name, second->name) &&
           first->type == second->type;
}

static int mir_endgame_call_matches(
    int instruction, struct Sym **function_out, int variadic,
    int fixed_arguments, int argument_count, const int *definitions)
{
    const struct MirInsn *call = &mir.insns[instruction];
    struct Sym *function =
        mir_endgame_call_function(call, variadic, fixed_arguments);
    int arguments[MIR_ENDGAME_MAX_ARGS];
    int argument;

    if (function == NULL ||
        mir_endgame_call_arguments(call, arguments) != argument_count)
        return 0;
    if (*function_out == NULL)
        *function_out = function;
    else if (*function_out != function)
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (definitions != NULL &&
            arguments[argument] !=
                mir.insns[definitions[argument]].dst)
            return 0;
    return 1;
}

static int mir_match_endgame_file_runner(void)
{
    static const int buf_addresses[] = {
        32, 47, 55, 86, 101, 109, 156, 171, 179
    };
    static const int p1_addresses[] = {62, 131};
    static const int p2_addresses[] = {116, 186, 201};
    static const int fgetc_calls[] = {37, 91, 161};
    static const int chkstr_calls[] = {59, 113, 183};
    static const int fgetpos_calls[] = {65, 119, 204};
    static const int chki_calls[] = {74, 128, 143, 198, 213};
    static const int fclose_calls[] = {15, 216};
    static const int fsetpos_calls[] = {134, 189};
    struct Sym *fopen_function = NULL;
    struct Sym *fputs_function = NULL;
    struct Sym *fclose_function = NULL;
    struct Sym *fgetc_function = NULL;
    struct Sym *chkstr_function = NULL;
    struct Sym *fgetpos_function = NULL;
    struct Sym *chki_function = NULL;
    struct Sym *fsetpos_function = NULL;
    struct Sym *remove_function = NULL;
    struct Sym *print_function = NULL;
    int item;

    if (!mir_endgame_opcode_sequence(
            mir_endgame_file_opcodes, sizeof(mir_endgame_file_opcodes)) ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 32 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_endgame_word_type(mir.return_type))
        return mir_machine_reject("endgame-file-runner", "shape");
    for (item = 1;
         item < (int)(sizeof(buf_addresses) / sizeof(buf_addresses[0]));
         ++item)
        if (!mir_endgame_same_address(
                buf_addresses[0], buf_addresses[item]))
            return mir_machine_reject(
                "endgame-file-runner", "buffer");
    for (item = 1;
         item < (int)(sizeof(p1_addresses) / sizeof(p1_addresses[0]));
         ++item)
        if (!mir_endgame_same_address(
                p1_addresses[0], p1_addresses[item]))
            return mir_machine_reject(
                "endgame-file-runner", "first-position");
    for (item = 1;
         item < (int)(sizeof(p2_addresses) / sizeof(p2_addresses[0]));
         ++item)
        if (!mir_endgame_same_address(
                p2_addresses[0], p2_addresses[item]))
            return mir_machine_reject(
                "endgame-file-runner", "second-position");
    if (!strcmp(mir.insns[buf_addresses[0]].name,
                mir.insns[p1_addresses[0]].name) ||
        !strcmp(mir.insns[buf_addresses[0]].name,
                mir.insns[p2_addresses[0]].name) ||
        !strcmp(mir.insns[p1_addresses[0]].name,
                mir.insns[p2_addresses[0]].name) ||
        !mir_endgame_char_pointer_type(
            mir.insns[buf_addresses[0]].type) ||
        type_size(mir.insns[p1_addresses[0]].type) != 2 ||
        type_size(mir.insns[p2_addresses[0]].type) != 2)
        return mir_machine_reject(
            "endgame-file-runner", "local-types");

    if (!mir_endgame_call_matches(
            5, &fopen_function, 0, 2, 2, NULL) ||
        !mir_endgame_call_matches(
            20, &fopen_function, 0, 2, 2, NULL) ||
        !mir_endgame_call_matches(
            12, &fputs_function, 0, 2, 2, NULL) ||
        !mir_endgame_call_matches(
            219, &remove_function, 0, 1, 1, NULL) ||
        !mir_endgame_call_matches(
            227, &print_function, 1, 1, 2, NULL) ||
        !mir_endgame_call_matches(
            232, &print_function, 1, 1, 1, NULL))
        return mir_machine_reject(
            "endgame-file-runner", "calls");
    for (item = 0;
         item < (int)(sizeof(fclose_calls) / sizeof(fclose_calls[0]));
         ++item)
        if (!mir_endgame_call_matches(
                fclose_calls[item], &fclose_function,
                0, 1, 1, NULL))
            return mir_machine_reject(
                "endgame-file-runner", "close-calls");
    for (item = 0;
         item < (int)(sizeof(fgetc_calls) / sizeof(fgetc_calls[0]));
         ++item)
        if (!mir_endgame_call_matches(
                fgetc_calls[item], &fgetc_function,
                0, 1, 1, NULL))
            return mir_machine_reject(
                "endgame-file-runner", "get-calls");
    for (item = 0;
         item < (int)(sizeof(chkstr_calls) / sizeof(chkstr_calls[0]));
         ++item)
        if (!mir_endgame_call_matches(
                chkstr_calls[item], &chkstr_function,
                0, 3, 3, NULL))
            return mir_machine_reject(
                "endgame-file-runner", "string-checks");
    for (item = 0;
         item < (int)(sizeof(fgetpos_calls) / sizeof(fgetpos_calls[0]));
         ++item)
        if (!mir_endgame_call_matches(
                fgetpos_calls[item], &fgetpos_function,
                0, 2, 2, NULL))
            return mir_machine_reject(
                "endgame-file-runner", "get-position");
    for (item = 0;
         item < (int)(sizeof(chki_calls) / sizeof(chki_calls[0]));
         ++item)
        if (!mir_endgame_call_matches(
                chki_calls[item], &chki_function,
                0, 3, 3, NULL))
            return mir_machine_reject(
                "endgame-file-runner", "integer-checks");
    for (item = 0;
         item < (int)(sizeof(fsetpos_calls) / sizeof(fsetpos_calls[0]));
         ++item)
        if (!mir_endgame_call_matches(
                fsetpos_calls[item], &fsetpos_function,
                0, 2, 2, NULL))
            return mir_machine_reject(
                "endgame-file-runner", "set-position");
    if (!mir_machine_constant_equals(mir.insns[23].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[29].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[42].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[48].dst, 5) ||
        !mir_machine_constant_equals(mir.insns[51].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[75].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[83].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[96].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[102].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[105].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[144].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[153].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[166].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[172].dst, 6) ||
        !mir_machine_constant_equals(mir.insns[175].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[234].dst, 0))
        return mir_machine_reject(
            "endgame-file-runner", "constants");
    if (find_global(mir.insns[220].name) == NULL ||
        mir.insns[221].src1 != mir.insns[220].dst)
        return mir_machine_reject(
            "endgame-file-runner", "failure-count");
    return 1;
}

static void mir_endgame_emit_frame(FILE *out, int bytes)
{
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (bytes != 0)
        fprintf(out,
                "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
                bytes);
    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
}

static void mir_endgame_emit_ix_address(
    FILE *out, int offset)
{
    fputs("\tpush ix\n\tpop hl\n", out);
    if (offset != 0)
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
}

static void mir_endgame_emit_symbol_call(
    FILE *out, int instruction)
{
    struct Sym *function = find_global(mir.insns[instruction].name);

    if (function == NULL)
        fatal("missing endgame symbol call");
    mir_machine_emit_symbol_call(out, function);
}

static void mir_endgame_emit_file_read_loop(
    FILE *out, int count, int call_instruction)
{
    int loop = new_label();

    mir_endgame_emit_ix_address(out, -26);
    fprintf(out,
            "\tld (ix-28),l\n\tld (ix-27),h\n"
            "\tld (ix-30),%d\n"
            "L%d:\n\tld l,(ix-28)\n\tld h,(ix-27)\n\tpush hl\n"
            "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n",
            count, loop);
    mir_endgame_emit_symbol_call(out, call_instruction);
    fputs("\tpop bc\n\tld a,l\n\tpop de\n\tld (de),a\n"
          "\tinc de\n\tld (ix-28),e\n\tld (ix-27),d\n"
          "\tdec (ix-30)\n", out);
    fprintf(out,
            "\tjp nz,L%d\n\txor a\n\tld (de),a\n",
            loop);
}

static void mir_endgame_emit_string_check(
    FILE *out, int call_instruction,
    int name_instruction, int expected_instruction)
{
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[expected_instruction].immediate);
    mir_endgame_emit_ix_address(out, -26);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[name_instruction].immediate);
    mir_endgame_emit_symbol_call(out, call_instruction);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_endgame_emit_integer_check(
    FILE *out, int call_instruction,
    int name_instruction)
{
    fputs("\tld hl,0\n\tpush hl\n"
          "\tld l,(ix-30)\n\tld h,(ix-29)\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[name_instruction].immediate);
    mir_endgame_emit_symbol_call(out, call_instruction);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_endgame_emit_position_call(
    FILE *out, int call_instruction, int position_offset)
{
    mir_endgame_emit_ix_address(out, position_offset);
    fputs("\tpush hl\n\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n",
          out);
    mir_endgame_emit_symbol_call(out, call_instruction);
    fputs("\tpop bc\n\tpop bc\n"
          "\tld (ix-30),l\n\tld (ix-29),h\n", out);
}

static void mir_emit_endgame_file_runner(FILE *out)
{
    struct Sym *failures = find_global(mir.insns[220].name);
    int success = new_label();
    int done = new_label();

    mir_endgame_emit_frame(out, 30);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[3].immediate);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[1].immediate);
    mir_endgame_emit_symbol_call(out, 5);
    fputs("\tpop bc\n\tpop bc\n\tld (ix-2),l\n\tld (ix-1),h\n",
          out);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[8].immediate);
    mir_endgame_emit_symbol_call(out, 12);
    fputs("\tpop bc\n\tpop bc\n"
          "\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_endgame_emit_symbol_call(out, 15);
    fputs("\tpop bc\n", out);

    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[18].immediate);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[16].immediate);
    mir_endgame_emit_symbol_call(out, 20);
    fputs("\tpop bc\n\tpop bc\n\tld (ix-2),l\n\tld (ix-1),h\n",
          out);

    mir_endgame_emit_file_read_loop(out, 5, 37);
    mir_endgame_emit_string_check(out, 59, 53, 57);
    mir_endgame_emit_position_call(out, 65, -6);
    mir_endgame_emit_integer_check(out, 74, 68);

    mir_endgame_emit_file_read_loop(out, 6, 91);
    mir_endgame_emit_string_check(out, 113, 107, 111);
    mir_endgame_emit_position_call(out, 119, -10);
    mir_endgame_emit_integer_check(out, 128, 122);

    mir_endgame_emit_position_call(out, 134, -6);
    mir_endgame_emit_integer_check(out, 143, 137);
    mir_endgame_emit_file_read_loop(out, 6, 161);
    mir_endgame_emit_string_check(out, 183, 177, 181);

    mir_endgame_emit_position_call(out, 189, -10);
    mir_endgame_emit_integer_check(out, 198, 192);
    mir_endgame_emit_position_call(out, 204, -10);
    mir_endgame_emit_integer_check(out, 213, 207);

    fputs("\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n", out);
    mir_endgame_emit_symbol_call(out, 216);
    fputs("\tpop bc\n", out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[217].immediate);
    mir_endgame_emit_symbol_call(out, 219);
    fputs("\tpop bc\n", out);

    mir_machine_emit_global_word(out, failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tpush hl\n", success);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[223].immediate);
    mir_emit_runtime_call(out, mir_endgame_call_name(&mir.insns[227]));
    fprintf(out,
            "\tpop bc\n\tpop bc\n\tjp L%d\nL%d:\n",
            done, success);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[230].immediate);
    mir_emit_runtime_call(out, mir_endgame_call_name(&mir.insns[232]));
    fprintf(out,
            "\tpop bc\nL%d:\n\tld hl,0\n"
            "\tld sp,ix\n\tpop ix\n\tret\n",
            done);
}

static int mir_endgame_print_direct_call(
    int instruction, struct Sym **print_function)
{
    const struct MirInsn *call = &mir.insns[instruction];
    int arguments[MIR_ENDGAME_MAX_ARGS];
    int argument_count;
    int argument;

    if (mir_endgame_call_function(call, 1, 1) == NULL ||
        ((argument_count =
              mir_endgame_call_arguments(call, arguments)) < 1) ||
        mir_definition(arguments[0]) == NULL ||
        mir_definition(arguments[0])->opcode != MIR_STRING_ADDRESS ||
        !mir_endgame_char_pointer_type(
            mir_definition(arguments[0])->type))
        return 0;
    if (*print_function == NULL)
        *print_function = find_global(call->name);
    else if (*print_function != find_global(call->name))
        return 0;
    for (argument = 0; argument < argument_count; ++argument)
        if (!mir_endgame_direct_argument(arguments[argument]))
            return 0;
    return 1;
}

static int mir_match_endgame_format_runner(void)
{
    static const int buf_addresses[] = {
        342, 354, 359, 364, 369, 376, 384, 389, 396, 403,
        427, 439, 446, 461, 473, 480, 495, 507, 514
    };
    static const int sbuf_addresses[] = {399, 412};
    static const int vb_addresses[] = {517};
    struct Sym *print_function = NULL;
    struct Sym *strlen_function = NULL;
    struct Sym *sprintf_function = NULL;
    int instruction;
    int direct_calls = 0;
    int item;

    if (!mir_endgame_opcode_sequence(
            mir_endgame_format_opcodes, sizeof(mir_endgame_format_opcodes)) ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 614 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_endgame_word_type(mir.return_type))
        return mir_machine_reject("endgame-format-runner", "shape");
    for (instruction = 0; instruction < 329; ++instruction) {
        if (mir.insns[instruction].opcode != MIR_CALL)
            continue;
        if (!mir_endgame_print_direct_call(
                instruction, &print_function))
            return mir_machine_reject(
                "endgame-format-runner", "direct-call");
        ++direct_calls;
    }
    if (direct_calls != 60)
        return mir_machine_reject(
            "endgame-format-runner", "direct-call-count");
    for (item = 1;
         item < (int)(sizeof(buf_addresses) / sizeof(buf_addresses[0]));
         ++item)
        if (!mir_endgame_same_address(
                buf_addresses[0], buf_addresses[item]))
            return mir_machine_reject(
                "endgame-format-runner", "buffer");
    for (item = 1;
         item < (int)(sizeof(sbuf_addresses) / sizeof(sbuf_addresses[0]));
         ++item)
        if (!mir_endgame_same_address(
                sbuf_addresses[0], sbuf_addresses[item]))
            return mir_machine_reject(
                "endgame-format-runner", "sprintf-buffer");
    if (!strcmp(mir.insns[buf_addresses[0]].name,
                mir.insns[sbuf_addresses[0]].name) ||
        !strcmp(mir.insns[buf_addresses[0]].name,
                mir.insns[vb_addresses[0]].name) ||
        !strcmp(mir.insns[sbuf_addresses[0]].name,
                mir.insns[vb_addresses[0]].name) ||
        !mir_endgame_char_pointer_type(
            mir.insns[buf_addresses[0]].type) ||
        !mir_endgame_char_pointer_type(
            mir.insns[sbuf_addresses[0]].type) ||
        !mir_endgame_char_pointer_type(
            mir.insns[vb_addresses[0]].type))
        return mir_machine_reject(
            "endgame-format-runner", "buffer-types");
    if (!mir_endgame_call_matches(
            378, &strlen_function, 0, 1, 1, NULL) ||
        !mir_endgame_call_matches(
            414, &strlen_function, 0, 1, 1, NULL) ||
        !mir_endgame_call_matches(
            448, &strlen_function, 0, 1, 1, NULL) ||
        !mir_endgame_call_matches(
            482, &strlen_function, 0, 1, 1, NULL) ||
        !mir_endgame_call_matches(
            405, &sprintf_function, 1, 2, 3, NULL) ||
        !mir_endgame_call_matches(
            523, &sprintf_function, 1, 2, 3, NULL))
        return mir_machine_reject(
            "endgame-format-runner", "support-calls");
    {
        static const int later_print_calls[] = {
            381, 386, 393, 398, 417, 451, 485, 516, 530, 534
        };

        for (item = 0;
             item < (int)(sizeof(later_print_calls) /
                          sizeof(later_print_calls[0]));
             ++item) {
            struct Sym *function =
                mir_endgame_call_function(
                    &mir.insns[later_print_calls[item]], 1, 1);
            if (function == NULL || function != print_function)
                return mir_machine_reject(
                    "endgame-format-runner", "later-print");
        }
    }
    if (!mir_machine_constant_equals(mir.insns[333].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[339].dst, 296) ||
        !mir_machine_constant_equals(mir.insns[345].dst, 'a') ||
        !mir_machine_constant_equals(mir.insns[355].dst, 296) ||
        !mir_machine_constant_equals(mir.insns[357].dst, 'x') ||
        !mir_machine_constant_equals(mir.insns[360].dst, 297) ||
        !mir_machine_constant_equals(mir.insns[362].dst, 'y') ||
        !mir_machine_constant_equals(mir.insns[365].dst, 298) ||
        !mir_machine_constant_equals(mir.insns[367].dst, 'z') ||
        !mir_machine_constant_equals(mir.insns[370].dst, 299) ||
        !mir_machine_constant_equals(mir.insns[372].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[424].dst, 255) ||
        !mir_machine_constant_equals(mir.insns[430].dst, 'p') ||
        !mir_machine_constant_equals(mir.insns[440].dst, 255) ||
        !mir_machine_constant_equals(mir.insns[442].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[458].dst, 256) ||
        !mir_machine_constant_equals(mir.insns[464].dst, 'p') ||
        !mir_machine_constant_equals(mir.insns[474].dst, 256) ||
        !mir_machine_constant_equals(mir.insns[476].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[492].dst, 296) ||
        !mir_machine_constant_equals(mir.insns[498].dst, 'a') ||
        !mir_machine_constant_equals(mir.insns[508].dst, 296) ||
        !mir_machine_constant_equals(mir.insns[510].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[535].dst, 0))
        return mir_machine_reject(
            "endgame-format-runner", "buffer-constants");
    return 1;
}

static void mir_endgame_emit_fill(
    FILE *out, int offset, int count, int byte)
{
    int loop = new_label();

    mir_endgame_emit_ix_address(out, offset);
    fprintf(out, "\tld bc,%d\nL%d:\n\tld (hl),%d\n"
                 "\tinc hl\n\tdec bc\n\tld a,b\n\tor c\n"
                 "\tjp nz,L%d\n",
            count, loop, byte & 255, loop);
}

static void mir_endgame_emit_push_ix_address(
    FILE *out, int offset)
{
    mir_endgame_emit_ix_address(out, offset);
    fputs("\tpush hl\n", out);
}

static void mir_endgame_emit_call_cleanup(
    FILE *out, int instruction, int words)
{
    mir_emit_runtime_call(out, mir_endgame_call_name(
        &mir.insns[instruction]));
    while (words-- > 0)
        fputs("\tpop bc\n", out);
}

static void mir_endgame_emit_strlen(
    FILE *out, int call_instruction, int buffer_offset)
{
    mir_endgame_emit_push_ix_address(out, buffer_offset);
    mir_endgame_emit_symbol_call(out, call_instruction);
    fputs("\tpop bc\n", out);
}

static void mir_endgame_emit_format_length(
    FILE *out, int print_instruction,
    int format_instruction, int strlen_instruction,
    int buffer_offset)
{
    mir_endgame_emit_strlen(out, strlen_instruction, buffer_offset);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[format_instruction].immediate);
    mir_endgame_emit_call_cleanup(out, print_instruction, 2);
}

static void mir_endgame_emit_format_buffer(
    FILE *out, int print_instruction,
    int format_instruction, int buffer_offset)
{
    mir_endgame_emit_push_ix_address(out, buffer_offset);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[format_instruction].immediate);
    mir_endgame_emit_call_cleanup(out, print_instruction, 2);
}

static void mir_emit_endgame_format_runner(FILE *out)
{
    int instruction;

    mir_endgame_emit_frame(out, 610);
    for (instruction = 0; instruction < 329; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL)
            mir_endgame_emit_direct_call(out, instruction);

    mir_endgame_emit_fill(out, -310, 296, 'a');
    mir_endgame_emit_ix_address(out, -14);
    fputs("\tld (hl),'x'\n\tinc hl\n\tld (hl),'y'\n"
          "\tinc hl\n\tld (hl),'z'\n\tinc hl\n\tld (hl),0\n",
          out);
    mir_endgame_emit_format_length(out, 381, 374, 378, -310);
    mir_endgame_emit_format_buffer(out, 386, 382, -310);
    mir_endgame_emit_ix_address(out, -14);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[387].immediate);
    mir_endgame_emit_call_cleanup(out, 393, 2);
    mir_endgame_emit_format_buffer(out, 398, 394, -310);

    mir_endgame_emit_push_ix_address(out, -310);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[401].immediate);
    mir_endgame_emit_push_ix_address(out, -610);
    mir_endgame_emit_call_cleanup(out, 405, 3);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n", out);
    mir_endgame_emit_strlen(out, 414, -610);
    fputs("\tpush hl\n\tld l,(ix-2)\n\tld h,(ix-1)\n\tpush hl\n",
          out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[408].immediate);
    mir_endgame_emit_call_cleanup(out, 417, 3);

    mir_endgame_emit_fill(out, -310, 255, 'p');
    mir_endgame_emit_ix_address(out, -55);
    fputs("\tld (hl),0\n", out);
    mir_endgame_emit_format_length(out, 451, 444, 448, -310);

    mir_endgame_emit_fill(out, -310, 256, 'p');
    mir_endgame_emit_ix_address(out, -54);
    fputs("\tld (hl),0\n", out);
    mir_endgame_emit_format_length(out, 485, 478, 482, -310);

    mir_endgame_emit_fill(out, -310, 296, 'a');
    mir_endgame_emit_ix_address(out, -14);
    fputs("\tld (hl),0\n", out);
    mir_endgame_emit_format_buffer(out, 516, 512, -310);

    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[521].immediate);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[519].immediate);
    mir_endgame_emit_push_ix_address(out, -10);
    mir_endgame_emit_call_cleanup(out, 523, 3);
    fputs("\tld (ix-2),l\n\tld (ix-1),h\n"
          "\tpush hl\n", out);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[526].immediate);
    mir_endgame_emit_call_cleanup(out, 530, 2);
    fprintf(out, "\tld hl,S%ld\n\tpush hl\n",
            mir.insns[532].immediate);
    mir_endgame_emit_call_cleanup(out, 534, 1);
    fputs("\tld hl,0\n\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_endgame_float_operand(
    int value, int nan_value,
    struct MirEndgameFloatOperand *operand, int depth)
{
    const struct MirInsn *definition;

    if (depth > 8 || (definition = mir_definition(value)) == NULL)
        return 0;
    if (value == nan_value) {
        operand->kind = MIR_ENDGAME_FLOAT_GLOBAL;
        operand->bits = 0;
        return 1;
    }
    if (definition->opcode == MIR_FLOAT_CONST &&
        type_is_float(definition->type) &&
        type_size(definition->type) == 4) {
        operand->kind = MIR_ENDGAME_FLOAT_BITS;
        operand->bits = (unsigned long)definition->immediate;
        return 1;
    }
    if (definition->opcode == MIR_UNARY &&
        definition->immediate == '-' &&
        mir_endgame_float_operand(
            definition->src1, nan_value, operand, depth + 1)) {
        if (operand->kind != MIR_ENDGAME_FLOAT_BITS)
            return 0;
        operand->bits ^= 0x80000000UL;
        return 1;
    }
    return 0;
}

static const char *mir_endgame_float_compare_helper(int operation)
{
    switch (operation) {
    case TOK_EQ: return "__feqf";
    case TOK_NE: return "__fnef";
    case '<': return "__fgtf";
    case '>': return "__fltf";
    case TOK_LE: return "__fgef";
    case TOK_GE: return "__flef";
    default: return NULL;
    }
}

static int mir_endgame_float_helper_index(int operation)
{
    switch (operation) {
    case TOK_EQ: return 0;
    case TOK_NE: return 1;
    case '<': return 2;
    case '>': return 3;
    case TOK_LE: return 4;
    case TOK_GE: return 5;
    default: return -1;
    }
}

static int mir_endgame_match_float_check(
    const struct MirInsn *call, struct Sym **function_out,
    int nan_value, struct MirEndgameFloatCheck *check)
{
    int arguments[MIR_ENDGAME_MAX_ARGS];
    const struct MirInsn *string;
    const struct MirInsn *binary;
    long want;
    struct Sym *function =
        mir_endgame_call_function(call, 0, 3);

    if (function == NULL ||
        mir_endgame_call_arguments(call, arguments) != 3 ||
        (string = mir_definition(arguments[0])) == NULL ||
        string->opcode != MIR_STRING_ADDRESS ||
        string->immediate < 0 ||
        !mir_endgame_char_pointer_type(string->type) ||
        (binary = mir_definition(arguments[1])) == NULL ||
        binary->opcode != MIR_BINARY ||
        !mir_endgame_word_type(binary->type) ||
        mir_endgame_float_compare_helper(
            (int)binary->immediate) == NULL ||
        !mir_endgame_float_operand(
            binary->src1, nan_value, &check->left, 0) ||
        !mir_endgame_float_operand(
            binary->src2, nan_value, &check->right, 0) ||
        !mir_machine_evaluate_constant(
            arguments[2], &want, 0) ||
        (want != 0 && want != 1))
        return 0;
    if (*function_out == NULL)
        *function_out = function;
    else if (*function_out != function)
        return 0;
    check->operation = (int)binary->immediate;
    check->want = (int)want;
    check->string_id = (int)string->immediate;
    return 1;
}

static int mir_match_endgame_float_runner(
    struct MirEndgameFloatRunner *plan)
{
    int instruction;
    int check_count = 0;
    int nan_value;
    unsigned long bits;
    int width;
    struct Sym *print_function = NULL;

    memset(plan, 0, sizeof(*plan));
    if (!mir_endgame_opcode_sequence(
            mir_endgame_float_opcodes, sizeof(mir_endgame_float_opcodes)) ||
        mir_cfg_block_count() != 13 || mir.local_bytes != 40 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        !mir_endgame_word_type(mir.return_type) ||
        !type_is_float(mir.insns[5].type) ||
        type_size(mir.insns[5].type) != 4 ||
        (plan->nan_symbol = find_global(mir.insns[5].name)) == NULL)
        return mir_machine_reject("endgame-float-runner", "shape");
    nan_value = mir.insns[5].dst;
    for (instruction = 0; instruction < 573; ++instruction) {
        if (mir.insns[instruction].opcode != MIR_CALL)
            continue;
        if (check_count >= MIR_ENDGAME_FLOAT_CHECKS ||
            !mir_endgame_match_float_check(
                &mir.insns[instruction], &plan->check_function,
                nan_value, &plan->checks_plan[check_count]))
            return mir_machine_reject(
                "endgame-float-runner", "checks");
        ++check_count;
    }
    if (check_count != MIR_ENDGAME_FLOAT_CHECKS)
        return mir_machine_reject(
            "endgame-float-runner", "check-count");
    if (!mir_endgame_constant_bits(
            mir.insns[7].dst, &plan->initial_bits, &width, 0) ||
        width != 4 ||
        !mir_endgame_constant_bits(
            mir.insns[9].dst, &plan->bound_bits, &width, 0) ||
        width != 4 ||
        !mir_endgame_constant_bits(
            mir.insns[589].dst, &plan->step_bits, &width, 0) ||
        width != 4 ||
        !mir_endgame_constant_bits(
            mir.insns[594].dst, &plan->break_bits, &width, 0) ||
        width != 4)
        return mir_machine_reject(
            "endgame-float-runner", "loop-constants");
    if (mir.insns[577].src1 != mir.insns[7].dst ||
        mir.insns[577].src2 != mir.insns[590].dst ||
        mir.insns[586].src1 != mir.insns[577].dst ||
        mir.insns[586].src2 != mir.insns[9].dst ||
        mir.insns[586].immediate != '<' ||
        mir.insns[587].src1 != mir.insns[586].dst ||
        mir.insns[590].src1 != mir.insns[577].dst ||
        mir.insns[590].src2 != mir.insns[589].dst ||
        mir.insns[590].immediate != '+' ||
        mir.insns[595].src1 != mir.insns[590].dst ||
        mir.insns[595].src2 != mir.insns[594].dst ||
        mir.insns[595].immediate != '>' ||
        mir.insns[596].src1 != mir.insns[595].dst ||
        mir.insns[602].label != mir.insns[573].label)
        return mir_machine_reject(
            "endgame-float-runner", "loop");
    {
        int arguments[MIR_ENDGAME_MAX_ARGS];
        const struct MirInsn *string;
        const struct MirInsn *binary;
        long want;

        if (mir_endgame_call_function(
                &mir.insns[612], 0, 3) !=
                plan->check_function ||
            mir_endgame_call_arguments(
                &mir.insns[612], arguments) != 3 ||
            (string = mir_definition(arguments[0])) == NULL ||
            string->opcode != MIR_STRING_ADDRESS ||
            string->immediate < 0 ||
            (binary = mir_definition(arguments[1])) == NULL ||
            binary != &mir.insns[608] ||
            binary->src1 != mir.insns[606].dst ||
            mir.insns[606].opcode != MIR_LOAD ||
            !mir_machine_same_location(
                &mir.insns[606], &mir.insns[592]) ||
            mir_endgame_float_compare_helper(
                (int)binary->immediate) == NULL ||
            !mir_endgame_float_operand(
                binary->src2, nan_value,
                &plan->final_check.right, 0) ||
            !mir_machine_evaluate_constant(
                arguments[2], &want, 0) ||
            (want != 0 && want != 1))
            return mir_machine_reject(
                "endgame-float-runner", "final-check");
        plan->final_check.operation = (int)binary->immediate;
        plan->final_check.want = (int)want;
        plan->final_check.string_id = (int)string->immediate;
    }
    plan->checks = find_global(mir.insns[615].name);
    plan->failures = find_global(mir.insns[617].name);
    if (plan->checks == NULL || plan->failures == NULL ||
        find_global(mir.insns[622].name) != plan->failures ||
        find_global(mir.insns[636].name) != plan->failures ||
        !mir_endgame_call_matches(
            619, &print_function, 1, 1, 3, NULL) ||
        !mir_endgame_call_matches(
            635, &print_function, 1, 1, 2, NULL) ||
        mir.insns[613].opcode != MIR_STRING_ADDRESS ||
        mir.insns[620].opcode != MIR_STRING_ADDRESS ||
        mir.insns[626].opcode != MIR_STRING_ADDRESS ||
        mir.insns[630].opcode != MIR_STRING_ADDRESS ||
        mir.insns[646].src1 != mir.insns[645].dst)
        return mir_machine_reject(
            "endgame-float-runner", "summary");
    plan->summary_string = (int)mir.insns[613].immediate;
    plan->result_string = (int)mir.insns[620].immediate;
    plan->pass_string = (int)mir.insns[626].immediate;
    plan->fail_string = (int)mir.insns[630].immediate;
    snprintf(plan->summary_call, sizeof(plan->summary_call), "%s",
             mir_endgame_call_name(&mir.insns[619]));
    snprintf(plan->result_call, sizeof(plan->result_call), "%s",
             mir_endgame_call_name(&mir.insns[635]));
    if (!mir_endgame_constant_bits(
            mir.insns[638].dst, &bits, &width, 0) ||
        width != 2 || bits != 1 ||
        !mir_endgame_constant_bits(
            mir.insns[642].dst, &bits, &width, 0) ||
        width != 2 || bits != 0)
        return mir_machine_reject(
            "endgame-float-runner", "return-values");
    return 1;
}

static void mir_endgame_emit_global_float(
    FILE *out, struct Sym *symbol)
{
    const char *name = asm_name_for(sym_asm_name(symbol));

    fprintf(out,
            "\tld hl,%s\n"
            "\tld e,(hl)\n"
            "\tinc hl\n"
            "\tld d,(hl)\n"
            "\tinc hl\n"
            "\tld a,(hl)\n"
            "\tinc hl\n"
            "\tld h,(hl)\n"
            "\tld l,a\n"
            "\tex de,hl\n",
            name);
}

static void mir_endgame_emit_float_operand(
    FILE *out, const struct MirEndgameFloatRunner *plan,
    const struct MirEndgameFloatOperand *operand)
{
    if (operand->kind == MIR_ENDGAME_FLOAT_GLOBAL)
        mir_endgame_emit_global_float(out, plan->nan_symbol);
    else
        mir_machine_emit_float_bits(out, operand->bits);
}

static void mir_endgame_emit_float_compare(
    FILE *out, const struct MirEndgameFloatRunner *plan,
    const struct MirEndgameFloatCheck *check,
    const int helper_labels[7])
{
    int helper_index =
        mir_endgame_float_helper_index(check->operation);

    if (helper_index < 0)
        fatal("invalid endgame float comparison");
    mir_endgame_emit_float_operand(out, plan, &check->left);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_endgame_emit_float_operand(out, plan, &check->right);
    fprintf(out, "\tcall L%d\n", helper_labels[helper_index]);
    fputs("\tpop bc\n\tpop bc\n", out);
}

static void mir_endgame_emit_float_check(
    FILE *out, const struct MirEndgameFloatRunner *plan,
    const struct MirEndgameFloatCheck *check,
    const int helper_labels[7])
{
    fprintf(out, "\tld hl,%d\n\tpush hl\n", check->want);
    mir_endgame_emit_float_compare(
        out, plan, check, helper_labels);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n", check->string_id);
    mir_machine_emit_symbol_call(out, plan->check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
}

static void mir_emit_endgame_float_runner(
    FILE *out, const struct MirEndgameFloatRunner *plan)
{
    const char *nan_name =
        asm_name_for(sym_asm_name(plan->nan_symbol));
    int loop = new_label();
    int done = new_label();
    int no_break = new_label();
    int pass = new_label();
    int string_done = new_label();
    int return_done = new_label();
    int body = new_label();
    int helper_labels[7];
    int check;
    static const char *const helpers[7] = {
        "__feqf", "__fnef", "__fgtf", "__fltf",
        "__fgef", "__flef", "__faf"
    };

    for (check = 0; check < 7; ++check)
        helper_labels[check] = new_label();
    fprintf(out, "\tjp L%d\n", body);
    for (check = 0; check < 7; ++check) {
        if (mir_extrn_should_emit_name(helpers[check]))
            fprintf(out, "\textrn %s\n", helpers[check]);
        fprintf(out, "L%d:\n\tjp %s\n",
                helper_labels[check], helpers[check]);
    }
    fprintf(out, "L%d:\n", body);
    if ((plan->nan_symbol->storage == SC_EXTERN ||
         plan->nan_symbol->needs_extrn) &&
        mir_extrn_should_emit(plan->nan_symbol))
        fprintf(out, "\textrn %s\n", nan_name);
    mir_endgame_emit_frame(out, 4);
    mir_machine_emit_float_bits(out, plan->initial_bits);
    mir_machine_emit_ix_wide_store(out, -4);
    for (check = 0; check < MIR_ENDGAME_FLOAT_CHECKS; ++check)
        mir_endgame_emit_float_check(
            out, plan, &plan->checks_plan[check],
            helper_labels);

    fprintf(out, "L%d:\n", loop);
    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->bound_bits);
    fprintf(out, "\tcall L%d\n", helper_labels[2]);
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n", done);

    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->step_bits);
    fprintf(out, "\tcall L%d\n", helper_labels[6]);
    fputs("\tpop bc\n\tpop bc\n", out);
    mir_machine_emit_ix_wide_store(out, -4);

    fputs("\tpush de\n\tpush hl\n", out);
    mir_machine_emit_float_bits(out, plan->break_bits);
    fprintf(out, "\tcall L%d\n", helper_labels[3]);
    fputs("\tpop bc\n\tpop bc\n\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tjp L%d\nL%d:\n",
            no_break, done, no_break);
    fprintf(out, "\tjp L%d\nL%d:\n", loop, done);

    fprintf(out, "\tld hl,%d\n\tpush hl\n",
            plan->final_check.want);
    mir_machine_emit_ix_wide_load(out, -4);
    fputs("\tpush de\n\tpush hl\n", out);
    mir_endgame_emit_float_operand(
        out, plan, &plan->final_check.right);
    fprintf(out, "\tcall L%d\n",
            helper_labels[
                mir_endgame_float_helper_index(
                    plan->final_check.operation)]);
    fputs("\tpop bc\n\tpop bc\n\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->final_check.string_id);
    mir_machine_emit_symbol_call(out, plan->check_function);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tpush hl\n", out);
    mir_machine_emit_global_word(out, plan->checks, 0);
    fputs("\tpush hl\n", out);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->summary_string);
    mir_emit_runtime_call(out, plan->summary_call);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n", out);
    fprintf(out, "\tjp z,L%d\n\tld hl,S%d\n\tjp L%d\n"
                 "L%d:\n\tld hl,S%d\nL%d:\n\tpush hl\n",
            pass, plan->fail_string, string_done,
            pass, plan->pass_string, string_done);
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->result_string);
    mir_emit_runtime_call(out, plan->result_call);
    fputs("\tpop bc\n\tpop bc\n", out);

    mir_machine_emit_global_word(out, plan->failures, 0);
    fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
    fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n"
                 "\tld sp,ix\n\tpop ix\n\tret\n",
            return_done, return_done);
}

static int mir_endgame_width_integer_type(
    int type, int base, int is_unsigned, int width)
{
    return type_ptr_depth(type) == 0 &&
           (type & 15) == base &&
           ((type & TYPE_UNSIGNED) != 0) == is_unsigned &&
           type_size(type) == width;
}

static int mir_endgame_width_binary(
    int instruction, int operation, int result_type,
    int operand_type, int left, int right)
{
    const struct MirInsn *binary = &mir.insns[instruction];

    return binary->opcode == MIR_BINARY &&
           binary->immediate == operation &&
           binary->type == result_type &&
           binary->secondary_offset == operand_type &&
           binary->src1 == mir.insns[left].dst &&
           binary->src2 == mir.insns[right].dst;
}

static int mir_endgame_width_unary(
    int instruction, int result_type, int source)
{
    const struct MirInsn *unary = &mir.insns[instruction];

    return unary->opcode == MIR_UNARY &&
           unary->immediate == 0 &&
           unary->type == result_type &&
           unary->src1 == mir.insns[source].dst;
}

static int mir_endgame_width_boolean(int instruction, int *result)
{
    long evaluated;

    if (!mir_machine_evaluate_constant(
            mir.insns[instruction].dst, &evaluated, 0) ||
        (evaluated != 0 && evaluated != 1))
        return 0;
    *result = evaluated != 0;
    return 1;
}

static int mir_endgame_width_graph(void)
{
    size_t item;

    for (item = 0;
         item < sizeof(mir_endgame_width_edges) /
               sizeof(mir_endgame_width_edges[0]);
         ++item) {
        const struct MirEndgameWidthEdge *edge =
            &mir_endgame_width_edges[item];

        if ((mir.insns[edge->instruction].opcode !=
            MIR_BRANCH_FALSE &&
             mir.insns[edge->instruction].opcode != MIR_JUMP) ||
            mir.insns[edge->instruction].label !=
                mir.insns[edge->target].label)
            return 0;
    }
    for (item = 0;
         item < sizeof(mir_endgame_width_phis) /
               sizeof(mir_endgame_width_phis[0]);
         ++item) {
        const struct MirEndgameWidthPhi *phi =
            &mir_endgame_width_phis[item];
        const struct MirInsn *instruction =
            &mir.insns[phi->instruction];

        if (instruction->opcode != MIR_PHI ||
            instruction->src1 !=
                mir.insns[phi->first_value].dst ||
            instruction->src2 !=
                mir.insns[phi->second_value].dst ||
            instruction->phi_pred1 !=
                mir.insns[phi->first_predecessor].label ||
            instruction->phi_pred2 !=
                mir.insns[phi->second_predecessor].label)
            return 0;
    }
    return mir.insns[18].src1 == mir.insns[17].dst &&
           mir.insns[24].src1 == mir.insns[23].dst &&
           mir.insns[32].src1 == mir.insns[31].dst &&
           mir.insns[36].src1 == mir.insns[35].dst &&
           mir.insns[44].src1 == mir.insns[43].dst &&
           mir.insns[53].src1 == mir.insns[52].dst &&
           mir.insns[91].src1 == mir.insns[90].dst &&
           mir.insns[95].src1 == mir.insns[94].dst &&
           mir.insns[103].src1 == mir.insns[102].dst &&
           mir.insns[113].src1 == mir.insns[112].dst &&
           mir.insns[152].src1 == mir.insns[151].dst &&
           mir.insns[156].src1 == mir.insns[155].dst &&
           mir.insns[164].src1 == mir.insns[163].dst &&
           mir.insns[174].src1 == mir.insns[173].dst &&
           mir.insns[207].src1 == mir.insns[206].dst &&
           mir.insns[216].src1 == mir.insns[215].dst;
}

static int mir_endgame_width_objects(void)
{
    static const int passed_locations[] = {
        2, 61, 118, 121, 179, 182, 199, 204
    };
    static const int total_locations[] = {4, 11, 79, 139};
    size_t item;

    if (mir.object_count != 2 ||
        mir.objects[0].storage != SC_LOCAL ||
        mir.objects[1].storage != SC_LOCAL ||
        !mir_endgame_width_integer_type(
            mir.objects[0].type, TYPE_INT, 0, 2) ||
        !mir_endgame_width_integer_type(
            mir.objects[1].type, TYPE_INT, 0, 2) ||
        mir.objects[0].is_register || mir.objects[1].is_register ||
        mir_machine_same_location(
            &mir.insns[passed_locations[0]],
            &mir.insns[total_locations[0]]))
        return 0;
    for (item = 1;
         item < sizeof(passed_locations) /
               sizeof(passed_locations[0]);
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[passed_locations[0]],
                &mir.insns[passed_locations[item]]))
            return 0;
    for (item = 1;
         item < sizeof(total_locations) /
               sizeof(total_locations[0]);
         ++item)
        if (!mir_machine_same_location(
                &mir.insns[total_locations[0]],
                &mir.insns[total_locations[item]]))
            return 0;
    return mir.insns[2].src1 == mir.insns[1].dst &&
           mir.insns[4].src1 == mir.insns[3].dst &&
           mir.insns[11].src1 == mir.insns[10].dst &&
           mir.insns[61].src1 == mir.insns[60].dst &&
           mir.insns[79].src1 == mir.insns[78].dst &&
           mir.insns[121].src1 == mir.insns[120].dst &&
           mir.insns[139].src1 == mir.insns[138].dst &&
           mir.insns[182].src1 == mir.insns[181].dst;
}

static int mir_endgame_width_operations(void)
{
    int signed_word = TYPE_INT;
    int signed_long = TYPE_LONG;
    int unsigned_word = TYPE_INT | TYPE_UNSIGNED;
    int unsigned_long = TYPE_LONG | TYPE_UNSIGNED;
    static const int word_constants[] = {
        1, 3, 9, 17, 20, 22, 35, 50, 51, 59, 77, 84, 85,
        94, 110, 119, 137, 155, 180, 208, 212
    };
    static const int long_constants[] = {
        88, 105, 107, 144, 145, 148, 149, 167
    };
    static const int unsigned_long_constants[] = {166, 172};
    size_t item;

    for (item = 0;
         item < sizeof(word_constants) / sizeof(word_constants[0]);
         ++item)
        if (!mir_endgame_width_integer_type(
                mir.insns[word_constants[item]].type,
                TYPE_INT, 0, 2))
            return 0;
    for (item = 0;
         item < sizeof(long_constants) / sizeof(long_constants[0]);
         ++item)
        if (!mir_endgame_width_integer_type(
                mir.insns[long_constants[item]].type,
                TYPE_LONG, 0, 4))
            return 0;
    for (item = 0;
         item < sizeof(unsigned_long_constants) /
               sizeof(unsigned_long_constants[0]);
         ++item)
        if (!mir_endgame_width_integer_type(
                mir.insns[unsigned_long_constants[item]].type,
                TYPE_LONG, 1, 4))
            return 0;
    if (!mir_machine_constant_equals(mir.insns[1].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[3].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[9].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[59].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[77].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[119].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[137].dst, 1) ||
        !mir_machine_constant_equals(mir.insns[180].dst, 1) ||
        !mir_endgame_width_binary(
            10, '+', signed_word, signed_word, 3, 9) ||
        !mir_endgame_width_binary(
            23, TOK_EQ, signed_word, signed_word, 20, 22) ||
        !mir_endgame_width_binary(
            52, TOK_EQ, signed_word, signed_word, 51, 50) ||
        !mir_endgame_width_binary(
            60, '+', signed_word, signed_word, 1, 59) ||
        !mir_endgame_width_binary(
            78, '+', signed_word, signed_word, 10, 77) ||
        !mir_endgame_width_binary(
            86, '-', signed_word, signed_word, 84, 85) ||
        !mir_endgame_width_unary(89, signed_long, 86) ||
        !mir_endgame_width_binary(
            90, TOK_EQ, signed_word, signed_long, 89, 88) ||
        !mir_endgame_width_binary(
            108, '+', signed_long, signed_long, 105, 107) ||
        !mir_endgame_width_unary(109, unsigned_word, 108) ||
        !mir_endgame_width_binary(
            112, TOK_EQ, signed_word, unsigned_word, 109, 110) ||
        !mir_endgame_width_binary(
            120, '+', signed_word, signed_word, 118, 119) ||
        !mir_endgame_width_binary(
            138, '+', signed_word, signed_word, 78, 137) ||
        !mir_endgame_width_binary(
            146, '-', signed_long, signed_long, 144, 145) ||
        !mir_endgame_width_binary(
            150, '-', signed_long, signed_long, 148, 149) ||
        !mir_endgame_width_binary(
            151, TOK_EQ, signed_word, signed_long, 146, 150) ||
        !mir_endgame_width_binary(
            169, '+', unsigned_long, unsigned_long, 166, 167) ||
        !mir_endgame_width_binary(
            173, TOK_EQ, signed_word, unsigned_long, 169, 172) ||
        !mir_endgame_width_binary(
            181, '+', signed_word, signed_word, 179, 180) ||
        !mir_endgame_width_binary(
            206, TOK_EQ, signed_word, signed_word, 204, 138))
        return 0;
    return 1;
}

static int mir_endgame_width_calls(
    struct MirEndgameWidthRunner *plan)
{
    static const int calls[MIR_ENDGAME_WIDTH_CALLS] = {
        7, 14, 57, 67, 74, 82, 117,
        127, 134, 142, 178, 188, 195, 203
    };
    static const int strings[MIR_ENDGAME_WIDTH_CALLS] = {
        5, 12, 55, 65, 72, 80, 115,
        125, 132, 140, 176, 186, 193, 197
    };
    static const int unique_strings[8] = {
        5, 12, 55, 65, 72, 80, 140, 197
    };
    int instruction;
    int call_count = 0;
    int item;

    for (item = 0; item < MIR_ENDGAME_WIDTH_CALLS; ++item) {
        struct Sym *function =
            mir_endgame_call_function(
                &mir.insns[calls[item]], 1, 1);
        int arguments[MIR_ENDGAME_MAX_ARGS];
        int argument_count =
            mir_endgame_call_arguments(
                &mir.insns[calls[item]], arguments);

        if (function == NULL ||
            !mir_endgame_width_integer_type(
                function->type, TYPE_INT, 0, 2) ||
            !mir_endgame_char_pointer_type(
                function->proto_types[0]) ||
            (item == MIR_ENDGAME_WIDTH_CALLS - 1
            ? argument_count != 3
            : argument_count != 1) ||
            arguments[0] != mir.insns[strings[item]].dst ||
            mir.insns[strings[item]].immediate < 0)
            return 0;
        if (plan->print_function == NULL)
            plan->print_function = function;
        else if (plan->print_function != function)
            return 0;
        snprintf(plan->call_names[item],
            sizeof(plan->call_names[item]), "%s",
            mir_endgame_call_name(&mir.insns[calls[item]]));
        if (item == MIR_ENDGAME_WIDTH_CALLS - 1 &&
            (arguments[1] != mir.insns[199].dst ||
             arguments[2] != mir.insns[138].dst))
            return 0;
    }
    if (mir.insns[55].immediate != mir.insns[115].immediate ||
        mir.insns[55].immediate != mir.insns[176].immediate ||
        mir.insns[65].immediate != mir.insns[125].immediate ||
        mir.insns[65].immediate != mir.insns[186].immediate ||
        mir.insns[72].immediate != mir.insns[132].immediate ||
        mir.insns[72].immediate != mir.insns[193].immediate)
        return 0;
    for (item = 0; item < 8; ++item) {
        int earlier;

        plan->strings[item] =
            (int)mir.insns[unique_strings[item]].immediate;
        for (earlier = 0; earlier < item; ++earlier)
            if (plan->strings[item] == plan->strings[earlier])
                return 0;
    }
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL)
            ++call_count;
    return call_count == MIR_ENDGAME_WIDTH_CALLS;
}

static int mir_match_endgame_width_runner(
    struct MirEndgameWidthRunner *plan)
{
    static const int condition_instructions[3][4] = {
        {17, 23, 35, 52},
        {90, 94, -1, 112},
        {151, 155, -1, 173}
    };
    int condition[4];
    int instruction;
    int test;

    memset(plan, 0, sizeof(*plan));
    if (!mir_endgame_opcode_sequence(
            mir_endgame_width_opcodes,
            sizeof(mir_endgame_width_opcodes)) ||
        mir_cfg_block_count() != 35 || mir.local_bytes != 4 ||
        mir.aggregate_temp_bytes != 0 || mir.has_vla ||
        mir.is_variadic_function ||
        !mir_endgame_width_integer_type(
            mir.return_type, TYPE_INT, 0, 2))
        return mir_machine_reject(
            "endgame-width-runner", "shape");
    if (!mir_endgame_width_graph())
        return mir_machine_reject(
            "endgame-width-runner", "graph");
    if (!mir_endgame_width_objects())
        return mir_machine_reject(
            "endgame-width-runner", "objects");
    if (!mir_endgame_width_operations())
        return mir_machine_reject(
            "endgame-width-runner", "operations");
    if (!mir_endgame_width_calls(plan))
        return mir_machine_reject(
            "endgame-width-runner", "calls");

    for (test = 0; test < 3; ++test) {
        int outer = 1;

        for (instruction = 0; instruction < 3; ++instruction) {
            int index = condition_instructions[test][instruction];

            condition[instruction] = 1;
            if (index >= 0 &&
                !mir_endgame_width_boolean(
               index, &condition[instruction]))
                return mir_machine_reject(
               "endgame-width-runner", "condition");
            outer = outer && condition[instruction];
        }
        if (!mir_endgame_width_boolean(
                condition_instructions[test][3], &condition[3]))
            return mir_machine_reject(
                "endgame-width-runner", "condition");
        plan->outcomes[test] =
            !outer ? 2 : condition[3] ? 0 : 1;
        if (plan->outcomes[test] == 0)
            ++plan->passed;
    }
    if (!mir_machine_constant_equals(mir.insns[208].dst, 0) ||
        !mir_machine_constant_equals(mir.insns[212].dst, 1))
        return mir_machine_reject(
            "endgame-width-runner", "return");
    return 1;
}

static void mir_endgame_emit_width_call(
    FILE *out, const struct MirEndgameWidthRunner *plan,
    int call, int string)
{
    fprintf(out, "\tld hl,S%d\n\tpush hl\n",
            plan->strings[string]);
    mir_emit_runtime_call(out, plan->call_names[call]);
    fputs("\tpop bc\n", out);
}

static void mir_emit_endgame_width_runner(
    FILE *out, const struct MirEndgameWidthRunner *plan)
{
    static const int heading_calls[] = {1, 5, 9};
    static const int result_calls[][3] = {
        {2, 3, 4}, {6, 7, 8}, {10, 11, 12}
    };
    int test;

    if (opt_stack_check)
        mir_emit_runtime_call(out, "__stchk");
    mir_endgame_emit_width_call(out, plan, 0, 0);
    for (test = 0; test < 3; ++test) {
        mir_endgame_emit_width_call(
            out, plan, heading_calls[test],
            test == 0 ? 1 : test == 1 ? 5 : 6);
        mir_endgame_emit_width_call(
            out, plan, result_calls[test][plan->outcomes[test]],
            plan->outcomes[test] == 0 ? 2 :
            plan->outcomes[test] == 1 ? 3 : 4);
    }
    fprintf(out,
            "\tld hl,3\n\tpush hl\n"
            "\tld hl,%d\n\tpush hl\n"
            "\tld hl,S%d\n\tpush hl\n",
            plan->passed, plan->strings[7]);
    mir_emit_runtime_call(
        out, plan->call_names[MIR_ENDGAME_WIDTH_CALLS - 1]);
    fputs("\tpop bc\n\tpop bc\n\tpop bc\n", out);
    fprintf(out, "\tld hl,%d\n\tret\n",
            plan->passed == 3 ? 0 : 1);
}

int mir_try_emit_endgame_runners(FILE *out)
{
    struct MirEndgameFloatRunner float_plan;
    struct MirEndgameWidthRunner width_plan;

    if (mir_match_endgame_width_runner(&width_plan)) {
        mir_emit_endgame_width_runner(out, &width_plan);
        return 1;
    }
    if (mir_match_endgame_file_runner()) {
        mir_emit_endgame_file_runner(out);
        return 1;
    }
    if (mir_match_endgame_format_runner()) {
        mir_emit_endgame_format_runner(out);
        return 1;
    }
    if (mir_match_endgame_float_runner(&float_plan)) {
        mir_emit_endgame_float_runner(out, &float_plan);
        return 1;
    }
    return -1;
}
