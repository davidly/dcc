/**
 * @file peep_dataflow.c
 * @brief Builds dccpeep's versioned CFG and backward liveness solution.
 *
 * @par Role
 * Derives per-function successors and basic blocks, then solves register and
 * flag liveness against the current line-program version for safety queries.
 *
 * @par Key entry points
 * peep_flow_line(), peep_basic_block(), peep_basic_block_count(),
 * peep_registers_dead_after(), and peep_flags_dead_after().
 *
 * @par Boundary
 * peep_effects.c classifies individual instructions and
 * peep_control_flow.c owns shared textual label indexes. This module never
 * mutates the line program, and unknown effects remain fully live.
 */
#include "dccpeep_internal.h"

#define PEEP_ALL_REGISTERS ((1u << 10) - 1u)
#define PEEP_ALL_FLAGS (PEEP_FLAG_C | PEEP_FLAG_Z | PEEP_FLAG_S | PEEP_FLAG_PV)

static int is_return_line(const char *line)
{
    char clean[MAX_LINE];

    strip_peep_comment_lower_copy(clean, line);
    return !strcmp(clean, "ret") || !strcmp(clean, "reti") ||
           !strcmp(clean, "retn") || !strncmp(clean, "ret ", 4);
}

static int parse_branch(const char *line, char *target, int *conditional)
{
    char clean[MAX_LINE];
    const char *comma;

    strip_peep_comment_copy(clean, line);
    if (strncmp(clean, "jp ", 3) && strncmp(clean, "jr ", 3) &&
        strncmp(clean, "djnz ", 5))
        return 0;
    if (!strncmp(clean, "djnz ", 5)) {
        const char *p = clean + 5;
        while (*p == ' ' || *p == '\t') ++p;
        if (!*p || strlen(p) >= 128)
            return 0;
        strcpy(target, p);
        *conditional = 1;
        return 1;
    }
    if (!jump_target_any(clean, target))
        return 0;
    comma = strchr(clean + 3, ',');
    *conditional = comma != NULL;
    return 1;
}

static void ensure_flow_capacity(PeepIndexes *indexes)
{
    if (indexes->flow_line_capacity < nlines) {
        PeepFlowLine *flow_lines = (PeepFlowLine *)realloc(
            indexes->flow_lines, (size_t)nlines * sizeof(*flow_lines));
        PeepBasicBlock *blocks = (PeepBasicBlock *)realloc(
            indexes->blocks, (size_t)nlines * sizeof(*blocks));
        if (nlines && (!flow_lines || !blocks)) {
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        indexes->flow_lines = flow_lines;
        indexes->blocks = blocks;
        indexes->flow_line_capacity = nlines;
        indexes->block_capacity = nlines;
    }
}

static void add_successor(PeepFlowLine *flow, int line)
{
    int i;

    if (line < 0 || line >= nlines)
        return;
    for (i = 0; i < flow->successor_count; ++i)
        if (flow->successors[i] == line)
            return;
    if (flow->successor_count < 2)
        flow->successors[flow->successor_count++] = line;
}

static void build_function_flow(int start, int end, unsigned char *leaders)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int i;

    if (start >= end)
        return;
    leaders[start] = 1;
    for (i = start; i < end; ++i) {
        PeepFlowLine *flow = &indexes->flow_lines[i];
        char target[128];
        int conditional;
        int target_line;

        if (parse_branch(lines[i], target, &conditional)) {
            target_line = find_label_line_in_range(target, start, end);
            if (target_line >= 0) {
                add_successor(flow, target_line);
                leaders[target_line] = 1;
            }
            if (conditional && i + 1 < end)
                add_successor(flow, i + 1);
            if (i + 1 < end)
                leaders[i + 1] = 1;
        } else if (!is_return_line(lines[i]) && i + 1 < end) {
            add_successor(flow, i + 1);
        }
        if (starts_label(lines[i]))
            leaders[i] = 1;
    }
}

static void build_blocks(unsigned char *leaders, int start, int end)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int i = start;

    while (i < end) {
        int block_start = i;
        int block_index = indexes->block_count++;
        PeepBasicBlock *block = &indexes->blocks[block_index];

        ++i;
        while (i < end && !leaders[i])
            ++i;
        block->start = block_start;
        block->end = i;
        block->function_start = start;
        block->function_end = end;
        while (block_start < i)
            indexes->flow_lines[block_start++].block = block_index;
    }
}

static void solve_liveness(int start, int end)
{
    PeepIndexes *indexes = &peep_context.indexes;
    int changed;

    do {
        int i;
        changed = 0;
        for (i = end - 1; i >= start; --i) {
            PeepFlowLine *flow = &indexes->flow_lines[i];
            const PeepLineInfo *info = peep_line_info(i);
            unsigned live_out = flow->successor_count ? 0 : PEEP_ALL_REGISTERS;
            unsigned flags_out = flow->successor_count ? 0 : PEEP_ALL_FLAGS;
            unsigned reads, writes, flags_read, flags_written;
            unsigned live_in, flags_in;
            int successor;

            for (successor = 0; successor < flow->successor_count; ++successor) {
                const PeepFlowLine *next =
                    &indexes->flow_lines[flow->successors[successor]];
                live_out |= next->live_in;
                flags_out |= next->flags_live_in;
            }
            reads = info->effects.reads;
            writes = info->effects.writes;
            flags_read = info->effects.flags_read;
            flags_written = info->effects.flags_written;
            if (info->effects.unknown) {
                reads |= PEEP_ALL_REGISTERS;
                writes |= PEEP_ALL_REGISTERS;
                flags_read |= PEEP_ALL_FLAGS;
                flags_written |= PEEP_ALL_FLAGS;
            }
            live_in = reads | (live_out & ~writes);
            flags_in = flags_read | (flags_out & ~flags_written);
            if (flow->live_in != live_in || flow->live_out != live_out ||
                flow->flags_live_in != flags_in ||
                flow->flags_live_out != flags_out) {
                flow->live_in = live_in;
                flow->live_out = live_out;
                flow->flags_live_in = flags_in;
                flow->flags_live_out = flags_out;
                changed = 1;
            }
        }
    } while (changed);
}

static void rebuild_flow(void)
{
    PeepIndexes *indexes = &peep_context.indexes;
    unsigned char *leaders;
    int function_index;
    int unused_start;
    int unused_end;

    if (indexes->flow_version == peep_context.program_version &&
        indexes->flow_lines)
        return;
    ensure_flow_capacity(indexes);
    memset(indexes->flow_lines, 0, (size_t)nlines * sizeof(*indexes->flow_lines));
    for (function_index = 0; function_index < nlines; ++function_index)
        indexes->flow_lines[function_index].block = -1;
    leaders = (unsigned char *)calloc((size_t)nlines, 1);
    if (nlines && !leaders) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    peep_indexed_function_bounds(0, 1, &unused_start, &unused_end);
    indexes->block_count = 0;
    if (indexes->all_function_count == 0) {
        build_function_flow(0, nlines, leaders);
        build_blocks(leaders, 0, nlines);
        solve_liveness(0, nlines);
    }
    for (function_index = 0;
         function_index < indexes->all_function_count;
         ++function_index) {
        int start = indexes->all_functions[function_index];
        int end = function_index + 1 < indexes->all_function_count
                ? indexes->all_functions[function_index + 1] : nlines;
        build_function_flow(start, end, leaders);
        build_blocks(leaders, start, end);
        solve_liveness(start, end);
    }
    free(leaders);
    indexes->flow_version = peep_context.program_version;
}

const PeepFlowLine *peep_flow_line(int line)
{
    if (line < 0 || line >= nlines)
        return NULL;
    rebuild_flow();
    return &peep_context.indexes.flow_lines[line];
}

const PeepBasicBlock *peep_basic_block(int block)
{
    rebuild_flow();
    if (block < 0 || block >= peep_context.indexes.block_count)
        return NULL;
    return &peep_context.indexes.blocks[block];
}

int peep_basic_block_count(void)
{
    rebuild_flow();
    return peep_context.indexes.block_count;
}

int peep_registers_dead_after(int line, unsigned registers)
{
    const PeepFlowLine *flow = peep_flow_line(line);
    return flow && !(flow->live_out & registers);
}

int peep_flags_dead_after(int line, unsigned flags)
{
    const PeepFlowLine *flow = peep_flow_line(line);
    return flow && !(flow->flags_live_out & flags);
}