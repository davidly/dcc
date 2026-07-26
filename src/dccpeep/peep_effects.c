/* peep_effects.c - cached structured line classification and effects.
 *
 * Recognized instructions receive conservative register/flag summaries.
 * Unknown instructions remain explicitly unsafe rather than being guessed.
 */
#include "dccpeep_internal.h"

static unsigned register_mask(const char *token)
{
    if (!strcmp(token, "a")) return PEEP_REG_A;
    if (!strcmp(token, "b")) return PEEP_REG_B;
    if (!strcmp(token, "c")) return PEEP_REG_C;
    if (!strcmp(token, "d")) return PEEP_REG_D;
    if (!strcmp(token, "e")) return PEEP_REG_E;
    if (!strcmp(token, "h")) return PEEP_REG_H;
    if (!strcmp(token, "l")) return PEEP_REG_L;
    if (!strcmp(token, "af")) return PEEP_REG_A;
    if (!strcmp(token, "bc")) return PEEP_REG_B | PEEP_REG_C;
    if (!strcmp(token, "de")) return PEEP_REG_D | PEEP_REG_E;
    if (!strcmp(token, "hl")) return PEEP_REG_H | PEEP_REG_L;
    if (!strcmp(token, "ix")) return PEEP_REG_IX;
    if (!strcmp(token, "iy")) return PEEP_REG_IY;
    if (!strcmp(token, "sp")) return PEEP_REG_SP;
    return 0;
}

static unsigned operand_registers(const char *operand)
{
    char token[16];
    unsigned mask = 0;
    int count = 0;

    while (*operand) {
        if (isalnum((unsigned char)*operand) || *operand == '_') {
            count = 0;
            while ((isalnum((unsigned char)*operand) || *operand == '_') &&
                   count < (int)sizeof(token) - 1)
                token[count++] = (char)tolower((unsigned char)*operand++);
            while (isalnum((unsigned char)*operand) || *operand == '_')
                operand++;
            token[count] = 0;
            mask |= register_mask(token);
        } else {
            operand++;
        }
    }
    return mask;
}

static int split_instruction(const char *line, char *mnemonic,
                             char *left, char *right)
{
    char clean[MAX_LINE];
    char *space;
    char *comma;
    char *p;

    strip_peep_comment_lower_copy(clean, line);
    if (!clean[0])
        return 0;
    space = clean;
    while (*space && !isspace((unsigned char)*space))
        space++;
    {
        size_t mnemonic_length = (size_t)(space - clean);
        if (mnemonic_length >= 32)
            return 0;
        memcpy(mnemonic, clean, mnemonic_length);
        mnemonic[mnemonic_length] = 0;
    }
    while (*space && isspace((unsigned char)*space))
        space++;
    strcpy(left, space);
    right[0] = 0;
    comma = strchr(left, ',');
    if (comma) {
        *comma++ = 0;
        while (*comma && isspace((unsigned char)*comma))
            comma++;
        strcpy(right, comma);
    }
    for (p = left + strlen(left); p > left && isspace((unsigned char)p[-1]); )
        *--p = 0;
    return 1;
}

static void classify_instruction(PeepLineInfo *info, const char *line)
{
    char mnemonic[32];
    char left[MAX_LINE];
    char right[MAX_LINE];
    unsigned left_regs;
    unsigned right_regs;

    info->kind = PEEP_LINE_INSTRUCTION;
    info->effects.unknown = 1;
    if (!split_instruction(line, mnemonic, left, right))
        return;
    left_regs = operand_registers(left);
    right_regs = operand_registers(right);

    if (!strcmp(mnemonic, "ld")) {
        info->opcode = PEEP_OPCODE_LD;
        info->effects.reads = right_regs;
        if (left[0] == '(') {
            info->effects.reads |= left_regs;
        } else {
            info->effects.writes = left_regs;
        }
        if (right[0] == '(')
            info->effects.reads |= right_regs;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "jp") || !strcmp(mnemonic, "jr")) {
        info->opcode = !strcmp(mnemonic, "jp") ? PEEP_OPCODE_JP : PEEP_OPCODE_JR;
        info->effects.control_flow = 1;
        info->effects.flags_read = PEEP_FLAG_C | PEEP_FLAG_Z | PEEP_FLAG_S | PEEP_FLAG_PV;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "call") || !strcmp(mnemonic, "rst")) {
        info->opcode = PEEP_OPCODE_CALL;
        info->effects.control_flow = 1;
    } else if (!strcmp(mnemonic, "ret") || !strcmp(mnemonic, "reti") ||
               !strcmp(mnemonic, "retn")) {
        info->opcode = PEEP_OPCODE_RET;
        info->effects.control_flow = 1;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "push")) {
        info->opcode = PEEP_OPCODE_PUSH;
        info->effects.reads = left_regs | PEEP_REG_SP;
        info->effects.writes = PEEP_REG_SP;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "pop")) {
        info->opcode = PEEP_OPCODE_POP;
        info->effects.reads = PEEP_REG_SP;
        info->effects.writes = left_regs | PEEP_REG_SP;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "inc") || !strcmp(mnemonic, "dec")) {
        info->opcode = PEEP_OPCODE_ALU;
        info->effects.reads = left_regs;
        info->effects.writes = left_regs;
        info->effects.flags_written = PEEP_FLAG_C | PEEP_FLAG_Z |
                                      PEEP_FLAG_S | PEEP_FLAG_PV;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "add") || !strcmp(mnemonic, "adc") ||
               !strcmp(mnemonic, "sbc")) {
        info->opcode = PEEP_OPCODE_ALU;
        if (right[0]) {
            info->effects.reads = left_regs | right_regs;
            info->effects.writes = left_regs;
        } else {
            info->effects.reads = PEEP_REG_A | left_regs;
            info->effects.writes = PEEP_REG_A;
        }
        info->effects.flags_written = PEEP_FLAG_C | PEEP_FLAG_Z |
                                      PEEP_FLAG_S | PEEP_FLAG_PV;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "sub") || !strcmp(mnemonic, "and") ||
               !strcmp(mnemonic, "or") || !strcmp(mnemonic, "xor") ||
               !strcmp(mnemonic, "cp")) {
        info->opcode = PEEP_OPCODE_ALU;
        info->effects.reads = PEEP_REG_A | left_regs;
        if (strcmp(mnemonic, "cp"))
            info->effects.writes = PEEP_REG_A;
        info->effects.flags_written = PEEP_FLAG_C | PEEP_FLAG_Z |
                                      PEEP_FLAG_S | PEEP_FLAG_PV;
        info->effects.unknown = 0;
    } else if (!strcmp(mnemonic, "ldi") || !strcmp(mnemonic, "ldir") ||
               !strcmp(mnemonic, "ldd") || !strcmp(mnemonic, "lddr") ||
               !strcmp(mnemonic, "cpi") || !strcmp(mnemonic, "cpir") ||
               !strcmp(mnemonic, "cpd") || !strcmp(mnemonic, "cpdr")) {
        info->opcode = PEEP_OPCODE_BLOCK;
        info->effects.reads = PEEP_REG_A | PEEP_REG_B | PEEP_REG_C |
                              PEEP_REG_D | PEEP_REG_E | PEEP_REG_H | PEEP_REG_L;
        info->effects.writes = PEEP_REG_B | PEEP_REG_C | PEEP_REG_D |
                               PEEP_REG_E | PEEP_REG_H | PEEP_REG_L;
        info->effects.flags_written = PEEP_FLAG_Z | PEEP_FLAG_PV;
        info->effects.unknown = 0;
    }
}

static void rebuild_line_info(void)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int i;

    if (indexes->line_info_capacity < nlines) {
        PeepLineInfo *line_info = (PeepLineInfo *)realloc(
            indexes->line_info, (size_t)nlines * sizeof(*line_info));
        if (nlines && !line_info) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        indexes->line_info = line_info;
        indexes->line_info_capacity = nlines;
    }
    for (i = 0; i < nlines; ++i) {
        PeepLineInfo *info = &indexes->line_info[i];
        memset(info, 0, sizeof(*info));
        if (user_asm_original[i]) {
            info->kind = PEEP_LINE_OPAQUE;
            info->effects.unknown = 1;
        } else if (!lines[i][0]) {
            info->kind = PEEP_LINE_BLANK;
        } else if (lines[i][0] == ';') {
            info->kind = PEEP_LINE_COMMENT;
        } else if (starts_label(lines[i])) {
            info->kind = PEEP_LINE_LABEL;
        } else if (peep_is_public_line(lines[i]) ||
                   !strncmp(lines[i], "extrn ", 6) ||
                   !strncmp(lines[i], "end", 3)) {
            info->kind = PEEP_LINE_DIRECTIVE;
        } else {
            classify_instruction(info, lines[i]);
        }
    }
    indexes->line_info_version = peep_context.program_version;
}

const PeepLineInfo *peep_line_info(int line)
{
    if (line < 0 || line >= nlines)
        return NULL;
    if (peep_context.indexes.line_info_version != peep_context.program_version ||
        !peep_context.indexes.line_info)
        rebuild_line_info();
    return &peep_context.indexes.line_info[line];
}
