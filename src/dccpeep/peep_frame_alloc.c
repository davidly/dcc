/* peep_frame_alloc.c - machine-level frame-slot value analysis.
 *
 * dcc's `(ix+n)` frame slots are virtual registers that have been eagerly
 * spilled. This module discovers the subset whose surviving post-convergence
 * machine code proves can be promoted back into a physical register.
 *
 * Phase 1 is analysis-only and deliberately basic-block scoped. A slot value
 * starts at a direct store to `(ix+n)`, has one reaching definition by
 * construction inside the block, and ends at the last direct load before the
 * next store. Calls, opaque instructions, labels and control transfers are
 * block boundaries through the existing CFG, so nothing here guesses across
 * control-flow joins. The report is emitted only under -fstats and changes no
 * program text.
 *
 * This first answers the question the source-level heuristics could not:
 * after every structural peephole pass has run, how much real frame traffic is
 * still present in spans where BC or DE is genuinely dead? Only if that census
 * is material should rewriting be enabled.
 */
#include "dccpeep_internal.h"

#define FRAME_SLOT_MIN -128
#define FRAME_SLOT_MAX 127
#define FRAME_SLOT_COUNT (FRAME_SLOT_MAX - FRAME_SLOT_MIN + 1)

static int slot_index(int offset)
{
    return offset - FRAME_SLOT_MIN;
}

static int frame_ld_load(const PeepLineInfo *info, int *offset,
                         unsigned *destination)
{
    if (info == NULL || info->opcode != PEEP_OPCODE_LD ||
        info->left.kind != PEEP_OPERAND_REGISTER ||
        info->right.kind != PEEP_OPERAND_FRAME ||
        !info->right.frame_offset_valid)
        return 0;
    if (info->left.registers != PEEP_REG_A &&
        info->left.registers != PEEP_REG_B &&
        info->left.registers != PEEP_REG_C &&
        info->left.registers != PEEP_REG_D &&
        info->left.registers != PEEP_REG_E &&
        info->left.registers != PEEP_REG_H &&
        info->left.registers != PEEP_REG_L)
        return 0;
    *offset = info->right.frame_offset;
    *destination = info->left.registers;
    return 1;
}

static int frame_ld_store(const PeepLineInfo *info, int *offset,
                          unsigned *source)
{
    if (info == NULL || info->opcode != PEEP_OPCODE_LD ||
        info->left.kind != PEEP_OPERAND_FRAME ||
        !info->left.frame_offset_valid ||
        info->right.kind != PEEP_OPERAND_REGISTER)
        return 0;
    if (info->right.registers != PEEP_REG_A &&
        info->right.registers != PEEP_REG_B &&
        info->right.registers != PEEP_REG_C &&
        info->right.registers != PEEP_REG_D &&
        info->right.registers != PEEP_REG_E &&
        info->right.registers != PEEP_REG_H &&
        info->right.registers != PEEP_REG_L)
        return 0;
    *offset = info->left.frame_offset;
    *source = info->right.registers;
    return 1;
}

typedef struct FrameBlockSlot {
    int defined_at;
    int first_use;
    int last_use;
    int use_count;
    unsigned source_register;
} FrameBlockSlot;

typedef struct FrameAllocStats {
    unsigned long byte_definitions;
    unsigned long byte_loads;
    unsigned long unique_reaching_loads;
    unsigned long bc_free_loads;
    unsigned long de_free_loads;
    unsigned long either_free_loads;
    unsigned long profitable_values;
    unsigned long profitable_uses;
    unsigned long predicted_bc_cycles;
    unsigned long predicted_de_cycles;
    unsigned long cross_block_unique_loads;
    unsigned long entry_parameter_loads;
    unsigned long ambiguous_loads;
    unsigned long killed_by_barrier;
    unsigned long endpoint_bc_loads;
    unsigned long endpoint_de_loads;
    unsigned long endpoint_either_loads;
    unsigned long endpoint_predicted_cycles;
    unsigned long full_span_values;
    unsigned long full_span_uses;
    unsigned long full_span_bc_uses;
    unsigned long full_span_de_uses;
    unsigned long full_span_predicted_cycles;
    unsigned long split_regions;
    unsigned long split_uses;
    unsigned long split_bc_uses;
    unsigned long split_de_uses;
    unsigned long split_predicted_cycles;
} FrameAllocStats;

static int line_register_free_after(int line, unsigned mask);
static int line_register_free_at_use(int line, unsigned mask);
static int span_register_free(int start, int end, unsigned mask);

static void record_split_region(FrameAllocStats *stats, int uses,
                                int bc_free, int de_free,
                                int first_line, int last_line)
{
    if (uses < 2)
        return;
    ++stats->split_regions;
    stats->split_uses += (unsigned long)uses;
    stats->split_predicted_cycles += (unsigned long)((uses - 1) * 15 - 4);
    if (bc_free)
        stats->split_bc_uses += (unsigned long)uses;
    if (de_free)
        stats->split_de_uses += (unsigned long)uses;
    if (getenv("DCCPEEP_FRAME_REPORT") != NULL)
        fprintf(stderr,
                "frame-alloc split: first=%d last=%d uses=%d bc=%d de=%d "
                "predicted=%dT\n",
                first_line + 1, last_line + 1, uses, bc_free, de_free,
                (uses - 1) * 15 - 4);
}

#define FRAME_RD_UNDEFINED (-1)
#define FRAME_RD_AMBIGUOUS (-2)
#define FRAME_RD_ENTRY (-3)

static int frame_alias_barrier(const PeepLineInfo *info)
{
    if (info == NULL)
        return 1;
    if (info->effects.unknown || info->opcode == PEEP_OPCODE_CALL)
        return 1;
    return (info->effects.memory_written &
            (PEEP_MEM_INDIRECT | PEEP_MEM_OPAQUE)) != 0;
}

static void frame_rd_transfer_block(const PeepBasicBlock *block,
                                    const int *input, int *output,
                                    FrameAllocStats *stats)
{
    int line;

    memcpy(output, input, FRAME_SLOT_COUNT * sizeof(*output));
    for (line = block->start; line < block->end; ++line) {
        const PeepLineInfo *info = peep_line_info(line);
        int offset;
        unsigned reg;

        if (frame_alias_barrier(info)) {
            int slot;
            for (slot = 0; slot < FRAME_SLOT_COUNT; ++slot)
                output[slot] = FRAME_RD_UNDEFINED;
            if (stats != NULL)
                ++stats->killed_by_barrier;
            continue;
        }
        if (frame_ld_store(info, &offset, &reg) &&
            offset >= FRAME_SLOT_MIN && offset <= FRAME_SLOT_MAX)
            output[slot_index(offset)] = line;
    }
}

static int frame_block_predecessor_count(int block_index,
                                         int function_first_block,
                                         int function_last_block,
                                         int *predecessors,
                                         int predecessor_capacity)
{
    const PeepBasicBlock *target = peep_basic_block(block_index);
    int count = 0;
    int candidate;

    if (target == NULL)
        return 0;
    for (candidate = function_first_block;
         candidate < function_last_block;
         ++candidate) {
        const PeepBasicBlock *block = peep_basic_block(candidate);
        const PeepFlowLine *flow;
        int successor;

        if (block == NULL || block->end <= block->start)
            continue;
        flow = peep_flow_line(block->end - 1);
        if (flow == NULL)
            continue;
        for (successor = 0; successor < flow->successor_count; ++successor) {
            const PeepFlowLine *next = peep_flow_line(flow->successors[successor]);
            if (next != NULL && next->block == block_index) {
                if (count < predecessor_capacity)
                    predecessors[count] = candidate;
                ++count;
                break;
            }
        }
    }
    return count;
}

static void analyze_function_reaching_definitions(int first_block,
                                                   int last_block,
                                                   FrameAllocStats *stats)
{
    int block_count = last_block - first_block;
    size_t state_count = (size_t)block_count * FRAME_SLOT_COUNT;
    int *in_state;
    int *out_state;
    int *predecessors;
    int *value_use_count;
    int *value_first_use;
    int *value_last_use;
    int *line_value_index;
    int function_start;
    int function_end;
    int function_lines;
    int changed;
    int block;

    if (block_count <= 0)
        return;
    function_start = peep_basic_block(first_block)->function_start;
    function_end = peep_basic_block(first_block)->function_end;
    function_lines = function_end - function_start;
    in_state = (int *)malloc(state_count * sizeof(*in_state));
    out_state = (int *)malloc(state_count * sizeof(*out_state));
    predecessors = (int *)malloc((size_t)block_count * sizeof(*predecessors));
    value_use_count = (int *)calloc((size_t)function_lines + FRAME_SLOT_COUNT,
                                    sizeof(*value_use_count));
    value_first_use = (int *)calloc((size_t)function_lines + FRAME_SLOT_COUNT,
                                    sizeof(*value_first_use));
    value_last_use = (int *)calloc((size_t)function_lines + FRAME_SLOT_COUNT,
                                   sizeof(*value_last_use));
    line_value_index = (int *)malloc((size_t)function_lines *
                                     sizeof(*line_value_index));
    if (in_state == NULL || out_state == NULL || predecessors == NULL ||
        value_use_count == NULL || value_first_use == NULL ||
        value_last_use == NULL || line_value_index == NULL) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    for (block = 0; block < function_lines; ++block)
        line_value_index[block] = -1;
    for (block = 0; block < (int)state_count; ++block) {
        in_state[block] = FRAME_RD_UNDEFINED;
        out_state[block] = FRAME_RD_UNDEFINED;
    }
    /* Positive IX offsets are incoming parameters and therefore have a
     * function-entry definition. Negative offsets are locals and remain
     * undefined until their first direct store. */
    for (block = 4; block <= FRAME_SLOT_MAX; ++block)
        in_state[slot_index(block)] = FRAME_RD_ENTRY;

    do {
        changed = 0;
        for (block = first_block; block < last_block; ++block) {
            const PeepBasicBlock *basic = peep_basic_block(block);
            int local = block - first_block;
            int *input = &in_state[(size_t)local * FRAME_SLOT_COUNT];
            int *output = &out_state[(size_t)local * FRAME_SLOT_COUNT];
            int pred_count;
            int slot;
            int next_output[FRAME_SLOT_COUNT];

            if (basic == NULL)
                continue;
            if (block != first_block) {
                pred_count = frame_block_predecessor_count(
                    block, first_block, last_block, predecessors, block_count);
                for (slot = 0; slot < FRAME_SLOT_COUNT; ++slot) {
                    int merged = FRAME_RD_UNDEFINED;
                    int pred;

                    if (pred_count > 0)
                        merged = out_state[(size_t)(predecessors[0] - first_block) *
                                           FRAME_SLOT_COUNT + slot];
                    for (pred = 1; pred < pred_count; ++pred) {
                        int value = out_state[
                            (size_t)(predecessors[pred] - first_block) *
                            FRAME_SLOT_COUNT + slot];
                        if (value != merged) {
                            merged = FRAME_RD_AMBIGUOUS;
                            break;
                        }
                    }
                    if (input[slot] != merged) {
                        input[slot] = merged;
                        changed = 1;
                    }
                }
            }
            frame_rd_transfer_block(basic, input, next_output, NULL);
            if (memcmp(output, next_output, sizeof(next_output)) != 0) {
                memcpy(output, next_output, sizeof(next_output));
                changed = 1;
            }
        }
    } while (changed);

    for (block = first_block; block < last_block; ++block) {
        const PeepBasicBlock *basic = peep_basic_block(block);
        int local = block - first_block;
        int state[FRAME_SLOT_COUNT];
        int line;

        if (basic == NULL)
            continue;
        memcpy(state, &in_state[(size_t)local * FRAME_SLOT_COUNT], sizeof(state));
        for (line = basic->start; line < basic->end; ++line) {
            const PeepLineInfo *info = peep_line_info(line);
            int offset;
            unsigned reg;

            if (frame_alias_barrier(info)) {
                int slot;
                for (slot = 0; slot < FRAME_SLOT_COUNT; ++slot)
                    state[slot] = FRAME_RD_UNDEFINED;
                continue;
            }
            if (frame_ld_store(info, &offset, &reg) &&
                offset >= FRAME_SLOT_MIN && offset <= FRAME_SLOT_MAX) {
                state[slot_index(offset)] = line;
                continue;
            }
            if (frame_ld_load(info, &offset, &reg) &&
                offset >= FRAME_SLOT_MIN && offset <= FRAME_SLOT_MAX) {
                int reaching = state[slot_index(offset)];
                int bc_free = 0;
                int de_free = 0;
                int value_index = -1;

                if (reaching >= 0) {
                    const PeepFlowLine *def_flow = peep_flow_line(reaching);
                    if (def_flow != NULL && def_flow->block != block) {
                        ++stats->cross_block_unique_loads;
                        value_index = reaching - function_start;
                        bc_free = line_register_free_after(
                                      reaching, PEEP_REG_B | PEEP_REG_C) &&
                                  line_register_free_at_use(
                                      line, PEEP_REG_B | PEEP_REG_C);
                        de_free = line_register_free_after(
                                      reaching, PEEP_REG_D | PEEP_REG_E) &&
                                  line_register_free_at_use(
                                      line, PEEP_REG_D | PEEP_REG_E);
                    }
                } else if (reaching == FRAME_RD_ENTRY) {
                    ++stats->entry_parameter_loads;
                    value_index = function_lines + slot_index(offset);
                    bc_free = line_register_free_at_use(
                        line, PEEP_REG_B | PEEP_REG_C);
                    de_free = line_register_free_at_use(
                        line, PEEP_REG_D | PEEP_REG_E);
                } else if (reaching == FRAME_RD_AMBIGUOUS) {
                    ++stats->ambiguous_loads;
                }
                if (bc_free)
                    ++stats->endpoint_bc_loads;
                if (de_free)
                    ++stats->endpoint_de_loads;
                if (bc_free || de_free) {
                    ++stats->endpoint_either_loads;
                    /* Upper bound only: a byte frame load is 19 T-states
                     * and a byte register copy 4, before paying any prime or
                     * spill and before checking the lines between endpoints. */
                    stats->endpoint_predicted_cycles += 15;
                    if (getenv("DCCPEEP_FRAME_REPORT") != NULL)
                        fprintf(stderr,
                                "frame-alloc endpoint: line=%d offset=%d "
                                "def=%d source=%s bc=%d de=%d\n",
                                line + 1, offset,
                                reaching >= 0 ? reaching + 1 : 0,
                                reaching == FRAME_RD_ENTRY ? "param" : "store",
                                bc_free, de_free);
                }
                if (value_index >= 0) {
                    if (value_first_use[value_index] == 0)
                        value_first_use[value_index] = line + 1;
                    value_last_use[value_index] = line + 1;
                    ++value_use_count[value_index];
                    line_value_index[line - function_start] = value_index;
                }
            }
        }
    }

    {
        int value;
        int value_count = function_lines + FRAME_SLOT_COUNT;

        for (value = 0; value < value_count; ++value) {
            int uses = value_use_count[value];
            int first_use;
            int last_use;
            int bc_free;
            int de_free;

            if (uses < 2)
                continue;
            first_use = value_first_use[value] - 1;
            last_use = value_last_use[value] - 1;
            /* Prime at the first surviving load, not at the reaching store.
             * This avoids reserving the register across dead space before
             * the value is first needed. The first load pays a 4T transfer
             * into the register; each later byte load saves 15T. */
            bc_free = span_register_free(first_use, last_use,
                                         PEEP_REG_B | PEEP_REG_C);
            de_free = span_register_free(first_use, last_use,
                                         PEEP_REG_D | PEEP_REG_E);
            if (!bc_free && !de_free)
                continue;
            ++stats->full_span_values;
            stats->full_span_uses += (unsigned long)uses;
            if (bc_free)
                stats->full_span_bc_uses += (unsigned long)uses;
            if (de_free)
                stats->full_span_de_uses += (unsigned long)uses;
            stats->full_span_predicted_cycles +=
                (unsigned long)((uses - 1) * 15 - 4);
        }
    }

    /* Live-range splitting upper bound. A physical register need not be free
     * from the first use to the last: after an intervening clobber the value
     * can be re-primed from its still-valid frame slot. For every value,
     * consider each pair of consecutive uses and group adjacent free links
     * into maximal regions. A two-use region costs a 4T prime at its first
     * use and saves 15T at its second; each extension saves another 15T. */
    {
        int value;
        int value_count = function_lines + FRAME_SLOT_COUNT;

        for (value = 0; value < value_count; ++value) {
            int previous = -1;
            int run_uses = 0;
            int run_bc = 0;
            int run_de = 0;
            int run_start = -1;
            int line;

            if (value_use_count[value] < 2)
                continue;
            for (line = function_start; line < function_end; ++line) {
                int bc_free;
                int de_free;

                if (line_value_index[line - function_start] != value)
                    continue;
                if (previous < 0) {
                    previous = line;
                    continue;
                }
                bc_free = span_register_free(previous, line,
                                             PEEP_REG_B | PEEP_REG_C);
                de_free = span_register_free(previous, line,
                                             PEEP_REG_D | PEEP_REG_E);
                if (bc_free || de_free) {
                    if (run_uses == 0) {
                        run_uses = 2;
                        run_bc = bc_free;
                        run_de = de_free;
                        run_start = previous;
                    } else if ((run_bc && bc_free) || (run_de && de_free)) {
                        ++run_uses;
                        run_bc = run_bc && bc_free;
                        run_de = run_de && de_free;
                    } else {
                        record_split_region(stats, run_uses, run_bc, run_de,
                                            run_start, previous);
                        run_uses = 2;
                        run_bc = bc_free;
                        run_de = de_free;
                        run_start = previous;
                    }
                } else if (run_uses > 0) {
                    record_split_region(stats, run_uses, run_bc, run_de,
                                        run_start, previous);
                    run_uses = 0;
                    run_bc = 0;
                    run_de = 0;
                    run_start = -1;
                }
                previous = line;
            }
            if (run_uses > 0) {
                record_split_region(stats, run_uses, run_bc, run_de,
                                    run_start, previous);
            }
        }
    }

    free(line_value_index);
    free(value_last_use);
    free(value_first_use);
    free(value_use_count);
    free(predecessors);
    free(out_state);
    free(in_state);
}

static int span_register_free(int start, int end, unsigned mask)
{
    int line;

    for (line = start; line <= end; ++line) {
        const PeepLineInfo *info = peep_line_info(line);
        const PeepFlowLine *flow = peep_flow_line(line);

        if (info == NULL || flow == NULL || info->effects.unknown)
            return 0;
        if ((info->effects.reads | info->effects.writes |
             flow->live_in | flow->live_out) & mask)
            return 0;
    }
    return 1;
}

static int line_register_free_after(int line, unsigned mask)
{
    const PeepFlowLine *flow = peep_flow_line(line);
    const PeepLineInfo *info = peep_line_info(line);

    if (flow == NULL || info == NULL || info->effects.unknown)
        return 0;
    return !(flow->live_out & mask);
}

static int line_register_free_at_use(int line, unsigned mask)
{
    const PeepFlowLine *flow = peep_flow_line(line);
    const PeepLineInfo *info = peep_line_info(line);

    if (flow == NULL || info == NULL || info->effects.unknown)
        return 0;
    return !((flow->live_in | flow->live_out |
              info->effects.reads | info->effects.writes) & mask);
}

void peep_frame_alloc_analyze(void)
{
    FrameAllocStats stats;
    int block_index;

    memset(&stats, 0, sizeof(stats));
    for (block_index = 0; block_index < peep_basic_block_count(); ++block_index) {
        const PeepBasicBlock *block = peep_basic_block(block_index);
        FrameBlockSlot slots[FRAME_SLOT_COUNT];
        int line;

        if (block == NULL)
            continue;
        memset(slots, 0, sizeof(slots));
        for (line = block->start; line < block->end; ++line) {
            const PeepLineInfo *info = peep_line_info(line);
            int offset;
            unsigned reg;

            if (frame_ld_store(info, &offset, &reg) &&
                offset >= FRAME_SLOT_MIN && offset <= FRAME_SLOT_MAX) {
                FrameBlockSlot *slot = &slots[slot_index(offset)];
                slot->defined_at = line + 1; /* zero means no reaching def */
                slot->first_use = 0;
                slot->last_use = 0;
                slot->use_count = 0;
                slot->source_register = reg;
                ++stats.byte_definitions;
                continue;
            }
            if (frame_ld_load(info, &offset, &reg) &&
                offset >= FRAME_SLOT_MIN && offset <= FRAME_SLOT_MAX) {
                FrameBlockSlot *slot = &slots[slot_index(offset)];
                ++stats.byte_loads;
                if (slot->defined_at == 0)
                    continue;
                ++stats.unique_reaching_loads;
                if (slot->first_use == 0)
                    slot->first_use = line + 1;
                slot->last_use = line + 1;
                ++slot->use_count;
            }
        }

        for (line = 0; line < FRAME_SLOT_COUNT; ++line) {
            FrameBlockSlot *slot = &slots[line];
            int start, end;
            int bc_free, de_free;

            if (slot->defined_at == 0 || slot->use_count == 0)
                continue;
            start = slot->defined_at - 1;
            end = slot->last_use - 1;
            bc_free = span_register_free(start, end, PEEP_REG_B | PEEP_REG_C);
            de_free = span_register_free(start, end, PEEP_REG_D | PEEP_REG_E);
            if (bc_free)
                stats.bc_free_loads += (unsigned long)slot->use_count;
            if (de_free)
                stats.de_free_loads += (unsigned long)slot->use_count;
            if (bc_free || de_free)
                stats.either_free_loads += (unsigned long)slot->use_count;

            /* One byte store to seed the register costs 4 T-states. A frame
             * byte load costs 19; a register copy costs 4, saving 15 per use.
             * Require strict positive gain, not break-even. */
            if ((bc_free || de_free) && slot->use_count * 15 > 4) {
                ++stats.profitable_values;
                stats.profitable_uses += (unsigned long)slot->use_count;
                if (bc_free)
                    stats.predicted_bc_cycles +=
                        (unsigned long)(slot->use_count * 15 - 4);
                else
                    stats.predicted_de_cycles +=
                        (unsigned long)(slot->use_count * 15 - 4);
            }
        }
    }

    /* Blocks belonging to one function are contiguous in the index. */
    for (block_index = 0; block_index < peep_basic_block_count(); ) {
        const PeepBasicBlock *first = peep_basic_block(block_index);
        int last = block_index + 1;

        if (first == NULL) {
            ++block_index;
            continue;
        }
        while (last < peep_basic_block_count()) {
            const PeepBasicBlock *next = peep_basic_block(last);
            if (next == NULL || next->function_start != first->function_start)
                break;
            ++last;
        }
        analyze_function_reaching_definitions(block_index, last, &stats);
        block_index = last;
    }

    fprintf(stderr,
            "frame-alloc analysis: defs=%lu loads=%lu unique=%lu "
            "bc-free=%lu de-free=%lu either=%lu values=%lu uses=%lu "
            "predicted=%luT cross-block=%lu params=%lu ambiguous=%lu "
            "barriers=%lu endpoint-bc=%lu endpoint-de=%lu endpoint-either=%lu "
            "endpoint-upper=%luT full-values=%lu full-uses=%lu full-bc=%lu "
            "full-de=%lu full-predicted=%luT split-regions=%lu split-uses=%lu "
            "split-bc=%lu split-de=%lu split-predicted=%luT\n",
            stats.byte_definitions, stats.byte_loads,
            stats.unique_reaching_loads, stats.bc_free_loads,
            stats.de_free_loads, stats.either_free_loads,
            stats.profitable_values, stats.profitable_uses,
            stats.predicted_bc_cycles + stats.predicted_de_cycles,
            stats.cross_block_unique_loads, stats.entry_parameter_loads,
            stats.ambiguous_loads, stats.killed_by_barrier,
            stats.endpoint_bc_loads, stats.endpoint_de_loads,
            stats.endpoint_either_loads, stats.endpoint_predicted_cycles,
            stats.full_span_values, stats.full_span_uses,
            stats.full_span_bc_uses, stats.full_span_de_uses,
            stats.full_span_predicted_cycles, stats.split_regions,
            stats.split_uses, stats.split_bc_uses, stats.split_de_uses,
            stats.split_predicted_cycles);
}
