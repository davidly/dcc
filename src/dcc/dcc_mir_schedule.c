/* dcc_mir_schedule.c - shadow sparse register scheduling for MIR.
 *
 * The first stage builds live segments split at CFG and call boundaries and
 * accounts for edge-specific PHI uses. It deliberately emits no Z80 yet.
 */

#include "dcc_mir_internal.h"

struct MirScheduleBlock {
    int first;
    int last;
};

static int mir_schedule_is_call(const struct MirInsn *insn)
{
    struct MirTargetConstraint constraint;

    return insn != NULL &&
           mir_target_constraint_for_insn(insn, &constraint) &&
           (constraint.flags & MIR_TARGET_FLAG_CALL) != 0;
}

static unsigned mir_schedule_allowed_colors(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int width;

    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_ADDRESS ||
        definition->opcode == MIR_COMPOUND_ADDRESS ||
        definition->opcode == MIR_INDEX_ADDRESS ||
        definition->opcode == MIR_MEMBER_ADDRESS ||
        definition->opcode == MIR_CALL_AGGREGATE)
        width = 2;
    else if ((definition->opcode == MIR_LOAD_INDIRECT ||
              definition->opcode == MIR_INDEX_LOAD) &&
             definition->memory_size > 0)
        width = definition->memory_size;
    else
        width = type_size(definition->type);
    if (width == 4)
        return (1u << MIR_COLOR_HL_DE) |
               (1u << MIR_COLOR_BC_IY);
    if (width == 1 || width == 2)
        return (1u << MIR_COLOR_HL) |
               (1u << MIR_COLOR_DE) |
               (1u << MIR_COLOR_BC) |
               (1u << MIR_COLOR_IY);
    return 0;
}

static int mir_schedule_is_rematerializable(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    if (definition == NULL)
        return 0;
    return definition->opcode == MIR_CONST ||
           definition->opcode == MIR_FLOAT_CONST ||
           definition->opcode == MIR_STRING_ADDRESS ||
           definition->opcode == MIR_ADDRESS;
}

static void mir_schedule_mark_block_starts(unsigned char *starts)
{
    int instruction;

    starts[0] = 1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int successor;

        if (insn->opcode == MIR_LABEL ||
            mir_schedule_is_call(insn))
            starts[instruction] = 1;
        if ((insn->opcode == MIR_JUMP ||
             insn->opcode == MIR_BRANCH_FALSE ||
             insn->opcode == MIR_RETURN ||
             mir_schedule_is_call(insn)) &&
            instruction + 1 < mir.count)
            starts[instruction + 1] = 1;
        for (successor = 0;
             successor < insn->successor_count; ++successor) {
            int target = insn->successors[successor];

            if (target >= 0 && target < mir.count &&
                target != instruction + 1)
                starts[target] = 1;
        }
    }
}

static int mir_schedule_build_blocks(
    struct MirScheduleBlock **blocks_out, int **instruction_blocks_out)
{
    unsigned char *starts;
    struct MirScheduleBlock *blocks;
    int *instruction_blocks;
    int block_count = 0;
    int instruction;

    starts = (unsigned char *)calloc((size_t)mir.count, 1);
    blocks = (struct MirScheduleBlock *)malloc(
        (size_t)mir.count * sizeof(*blocks));
    instruction_blocks = (int *)malloc(
        (size_t)mir.count * sizeof(*instruction_blocks));
    if (starts == NULL || blocks == NULL || instruction_blocks == NULL)
        fatal("out of memory building MIR schedule blocks");
    mir_schedule_mark_block_starts(starts);
    for (instruction = 0; instruction < mir.count;) {
        int first = instruction;
        int last;

        if (!starts[first])
            fatal("invalid MIR schedule block start");
        ++instruction;
        while (instruction < mir.count && !starts[instruction])
            ++instruction;
        last = instruction - 1;
        blocks[block_count].first = first;
        blocks[block_count].last = last;
        while (first <= last)
            instruction_blocks[first++] = block_count;
        ++block_count;
    }
    free(starts);
    *blocks_out = blocks;
    *instruction_blocks_out = instruction_blocks;
    return block_count;
}

static int mir_schedule_count_cfg_edges(
    const struct MirScheduleBlock *blocks,
    const int *instruction_blocks, int block_count)
{
    int edge_count = 0;
    int block;

    for (block = 0; block < block_count; ++block) {
        const struct MirInsn *insn =
            &mir.insns[blocks[block].last];
        int targets[2] = { -1, -1 };
        int successor;

        for (successor = 0;
             successor < insn->successor_count; ++successor) {
            int target_instruction = insn->successors[successor];
            int target;

            if (target_instruction < 0 ||
                target_instruction >= mir.count)
                continue;
            target = instruction_blocks[target_instruction];
            if (block == target ||
                target == targets[0] || target == targets[1])
                continue;
            targets[successor < 2 ? successor : 1] = target;
            ++edge_count;
        }
    }
    return edge_count;
}

static int mir_schedule_count_phi_edge_uses(void)
{
    int count = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_PHI) {
            if (mir.insns[instruction].src1 >= 0)
                ++count;
            if (mir.insns[instruction].src2 >= 0)
                ++count;
        }
    return count;
}

static int mir_schedule_collect_block_phi_uses(
    const struct MirScheduleBlock *block, int *edge_uses)
{
    const struct MirInsn *terminator = &mir.insns[block->last];
    int predecessor_label = mir_block_label_before(block->last);
    int successor;

    for (successor = 0;
         successor < terminator->successor_count; ++successor) {
        int target = terminator->successors[successor];
        int edge_label = -1;
        int instruction;

        if (target < 0 || target >= mir.count)
            return 0;
        if ((terminator->opcode == MIR_JUMP ||
             terminator->opcode == MIR_BRANCH_FALSE) &&
            mir_find_label(terminator->label) == target)
            edge_label = terminator->label;
        instruction = mir_next_phi_in_block(target, target);
        while (instruction >= 0) {
            const struct MirInsn *phi = &mir.insns[instruction];
            int source;

            source = mir_phi_source_for_edge(
                phi, predecessor_label, edge_label,
                target, instruction);
            if (source < 0) {
                instruction =
                    mir_next_phi_in_block(target, instruction + 1);
                continue;
            }
            if (source >= mir.next_value)
                return 0;
            ++edge_uses[source];
            instruction =
                mir_next_phi_in_block(target, instruction + 1);
        }
    }
    return 1;
}

static int mir_schedule_maximum_pressure(void)
{
    int maximum = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        int pressure = 0;
        int value;

        for (value = 0; value < mir.next_value; ++value)
            if (mir.live_in[(size_t)instruction *
                            mir.next_value + value] ||
                mir.live_out[(size_t)instruction *
                             mir.next_value + value] ||
                mir.insns[instruction].dst == value)
                ++pressure;
        if (maximum < pressure)
            maximum = pressure;
    }
    return maximum;
}

static void mir_schedule_append_segment(
    struct MirLiveSegment **segments, int *count, int *capacity,
    int value, int block, int first, int last, int uses,
    unsigned flags)
{
    struct MirLiveSegment *segment;

    if (*count >= *capacity) {
        int new_capacity = *capacity > 0 ? *capacity * 2 : 256;
        struct MirLiveSegment *new_segments =
            (struct MirLiveSegment *)realloc(
                *segments,
                (size_t)new_capacity * sizeof(*new_segments));

        if (new_segments == NULL)
            fatal("out of memory building MIR live segments");
        *segments = new_segments;
        *capacity = new_capacity;
    }
    segment = &(*segments)[(*count)++];
    segment->value = value;
    segment->block = block;
    segment->first_point = first;
    segment->last_point = last;
    segment->use_count = uses;
    segment->color = -1;
    segment->spill_slot = -1;
    segment->allowed_colors = mir_schedule_allowed_colors(value);
    segment->flags = flags;
    if (mir_schedule_is_rematerializable(value))
        segment->flags |= MIR_SCHEDULE_REMATERIALIZABLE;
    if ((segment->allowed_colors &
         ((1u << MIR_COLOR_HL_DE) |
          (1u << MIR_COLOR_BC_IY))) != 0)
        segment->flags |= MIR_SCHEDULE_WIDE;
}

static int mir_schedule_build_segments(
    const struct MirScheduleBlock *blocks, int block_count,
    struct MirLiveSegment **segments_out)
{
    struct MirLiveSegment *segments = NULL;
    int segment_count = 0;
    int segment_capacity = 0;
    int *first;
    int *last;
    int *uses;
    int *edge_uses;
    unsigned char *defined;
    int block;

    first = (int *)malloc((size_t)mir.next_value * sizeof(*first));
    last = (int *)malloc((size_t)mir.next_value * sizeof(*last));
    uses = (int *)malloc((size_t)mir.next_value * sizeof(*uses));
    edge_uses =
        (int *)malloc((size_t)mir.next_value * sizeof(*edge_uses));
    defined = (unsigned char *)malloc((size_t)mir.next_value);
    if (first == NULL || last == NULL || uses == NULL ||
        edge_uses == NULL || defined == NULL)
        fatal("out of memory building MIR live segments");
    for (block = 0; block < block_count; ++block) {
        int instruction;
        int value;

        for (value = 0; value < mir.next_value; ++value) {
            first[value] = mir.count;
            last[value] = -1;
            uses[value] = 0;
            edge_uses[value] = 0;
            defined[value] = 0;
        }
        for (instruction = blocks[block].first;
             instruction <= blocks[block].last; ++instruction) {
            const struct MirInsn *insn = &mir.insns[instruction];

            if (insn->dst >= 0) {
                value = insn->dst;
                if (first[value] > instruction)
                    first[value] = instruction;
                if (last[value] < instruction)
                    last[value] = instruction;
                defined[value] = 1;
            }
            if (insn->opcode != MIR_PHI && insn->src1 >= 0) {
                value = insn->src1;
                if (first[value] > instruction)
                    first[value] = instruction;
                if (last[value] < instruction)
                    last[value] = instruction;
                ++uses[value];
            }
            if (insn->opcode != MIR_PHI && insn->src2 >= 0) {
                value = insn->src2;
                if (first[value] > instruction)
                    first[value] = instruction;
                if (last[value] < instruction)
                    last[value] = instruction;
                ++uses[value];
            }
            if (mir_schedule_is_call(insn))
                for (value = 0; value < mir.next_value; ++value)
                    if (mir_call_uses_value(insn, value)) {
                        if (first[value] > instruction)
                            first[value] = instruction;
                        if (last[value] < instruction)
                            last[value] = instruction;
                        ++uses[value];
                    }
        }
        if (!mir_schedule_collect_block_phi_uses(
                &blocks[block], edge_uses)) {
            free(defined);
            free(edge_uses);
            free(uses);
            free(last);
            free(first);
            free(segments);
            *segments_out = NULL;
            return -1;
        }
        for (value = 0; value < mir.next_value; ++value) {
            unsigned flags = 0;
            int live_in =
                mir.live_in[(size_t)blocks[block].first *
                            mir.next_value + value] != 0;
            int live_out =
                mir.live_out[(size_t)blocks[block].last *
                             mir.next_value + value] != 0;

            if (!live_in && !live_out && !defined[value] &&
                uses[value] == 0 && edge_uses[value] == 0)
                continue;
            if (!live_in && !live_out && uses[value] == 0 &&
                edge_uses[value] == 0 &&
                mir_schedule_allowed_colors(value) == 0)
                continue;
            if (live_in) {
                flags |= MIR_SCHEDULE_LIVE_IN;
                if (first[value] > blocks[block].first)
                    first[value] = blocks[block].first;
            }
            if (live_out) {
                flags |= MIR_SCHEDULE_LIVE_OUT;
                if (last[value] < blocks[block].last)
                    last[value] = blocks[block].last;
            }
            if (defined[value])
                flags |= MIR_SCHEDULE_DEFINES;
            if (edge_uses[value] != 0) {
                flags |= MIR_SCHEDULE_PHI_EDGE;
                uses[value] += edge_uses[value];
                if (first[value] > blocks[block].last)
                    first[value] = blocks[block].last;
                if (last[value] < blocks[block].last)
                    last[value] = blocks[block].last;
            }
            if (blocks[block].first == blocks[block].last &&
                mir_schedule_is_call(
                    &mir.insns[blocks[block].first]) &&
                live_in && live_out)
                flags |= MIR_SCHEDULE_CROSSES_CALL;
            mir_schedule_append_segment(
                &segments, &segment_count, &segment_capacity,
                value, block, first[value], last[value],
                uses[value], flags);
        }
    }
    free(defined);
    free(edge_uses);
    free(uses);
    free(last);
    free(first);
    *segments_out = segments;
    return segment_count;
}

static unsigned mir_schedule_color_units(int color)
{
    switch (color) {
    case MIR_COLOR_HL: return MIR_Z80_HL;
    case MIR_COLOR_DE: return MIR_Z80_DE;
    case MIR_COLOR_BC: return MIR_Z80_BC;
    case MIR_COLOR_IY: return MIR_Z80_IY;
    case MIR_COLOR_HL_DE: return MIR_Z80_HL | MIR_Z80_DE;
    case MIR_COLOR_BC_IY: return MIR_Z80_BC | MIR_Z80_IY;
    default: return 0;
    }
}

static int mir_schedule_segments_overlap(
    const struct MirLiveSegment *left,
    const struct MirLiveSegment *right)
{
    return left->first_point <= right->last_point &&
           right->first_point <= left->last_point;
}

static int mir_schedule_color_available(
    const struct MirLiveSegment *segments, int segment_count,
    int candidate, int color)
{
    const struct MirLiveSegment *item = &segments[candidate];
    unsigned units = mir_schedule_color_units(color);
    int other;

    if ((item->allowed_colors & (1u << color)) == 0)
        return 0;
    if ((item->flags & MIR_SCHEDULE_CROSSES_CALL) != 0) {
        if ((item->flags & MIR_SCHEDULE_WIDE) != 0 ||
            color != MIR_COLOR_IY)
            return 0;
    }
    for (other = 0; other < segment_count; ++other) {
        const struct MirLiveSegment *conflict = &segments[other];

        if (other == candidate || conflict->block != item->block ||
            conflict->color < 0 ||
            !mir_schedule_segments_overlap(item, conflict))
            continue;
        if ((units &
             mir_schedule_color_units(conflict->color)) != 0)
            return 0;
    }
    return 1;
}

static int mir_schedule_color_affinity(
    const struct MirLiveSegment *segments, int segment_count,
    int candidate, int color)
{
    const struct MirLiveSegment *segment = &segments[candidate];
    unsigned units = mir_schedule_color_units(color);
    int score = color == MIR_COLOR_IY ||
                color == MIR_COLOR_BC_IY ? -2 : 0;
    int instruction;
    int other;

    for (instruction = segment->first_point;
         instruction <= segment->last_point; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        struct MirTargetConstraint constraint;

        if (!mir_target_constraint_for_insn(insn, &constraint))
            continue;
        if (insn->src1 == segment->value &&
            constraint.required_input1 == units)
            score += 8;
        if (insn->src2 == segment->value &&
            constraint.required_input2 == units)
            score += 8;
        if (insn->dst == segment->value &&
            constraint.required_output == units)
            score += 6;
    }
    for (other = 0; other < segment_count; ++other)
        if (other != candidate &&
            segments[other].value == segment->value &&
            segments[other].color == color)
            score +=
                (segments[other].flags &
                 MIR_SCHEDULE_CROSSES_CALL) != 0 ? 4 : 12;
    return score;
}

static void mir_schedule_allocate_segments(
    struct MirLiveSegment *segments, int segment_count,
    struct MirScheduleSummary *summary)
{
    static const int narrow_preference[] = {
        MIR_COLOR_BC, MIR_COLOR_DE, MIR_COLOR_HL, MIR_COLOR_IY
    };
    static const int wide_preference[] = {
        MIR_COLOR_HL_DE, MIR_COLOR_BC_IY
    };
    int spill_slot = 0;
    int remaining = segment_count;

    while (remaining > 0) {
        int best = -1;
        int best_score = -1;
        int segment;

        for (segment = 0; segment < segment_count; ++segment) {
            const struct MirLiveSegment *item = &segments[segment];
            int length;
            int score;

            if (item->color != -1 || item->spill_slot != -1)
                continue;
            length = item->last_point - item->first_point + 1;
            score = item->use_count * 32 - length;
            if ((item->flags & (MIR_SCHEDULE_LIVE_IN |
                                MIR_SCHEDULE_LIVE_OUT)) != 0)
                score += 16;
            if (best < 0 || score > best_score) {
                best = segment;
                best_score = score;
            }
        }
        if (best < 0)
            break;
        if ((segments[best].flags &
             MIR_SCHEDULE_REMATERIALIZABLE) != 0 &&
            segments[best].use_count <= 1) {
            segments[best].color = -2;
            ++summary->rematerialized_segments;
        } else {
            const int *preference;
            int preference_count;
            int preference_index;
            int selected_color = -1;
            int selected_affinity = -32767;

            if ((segments[best].flags & MIR_SCHEDULE_WIDE) != 0) {
                preference = wide_preference;
                preference_count =
                    (int)(sizeof(wide_preference) /
                          sizeof(wide_preference[0]));
            } else {
                preference = narrow_preference;
                preference_count =
                    (int)(sizeof(narrow_preference) /
                          sizeof(narrow_preference[0]));
            }
            for (preference_index = 0;
                 preference_index < preference_count;
                 ++preference_index) {
                int color = preference[preference_index];
                int affinity;

                if (!mir_schedule_color_available(
                        segments, segment_count, best, color))
                    continue;
                affinity = mir_schedule_color_affinity(
                    segments, segment_count, best, color);
                if (selected_color < 0 ||
                    affinity > selected_affinity) {
                    selected_color = color;
                    selected_affinity = affinity;
                }
            }
            if (selected_color >= 0) {
                segments[best].color = selected_color;
                ++summary->colored_segments;
                if (selected_color == MIR_COLOR_IY ||
                    selected_color == MIR_COLOR_BC_IY)
                    ++summary->iy_segments;
            }
            if (segments[best].color == -1) {
                if ((segments[best].flags &
                     MIR_SCHEDULE_REMATERIALIZABLE) != 0) {
                    segments[best].color = -2;
                    ++summary->rematerialized_segments;
                } else {
                    segments[best].spill_slot = spill_slot++;
                    ++summary->spilled_segments;
                }
            }
        }
        --remaining;
    }
}

static const struct MirLiveSegment *mir_schedule_find_segment(
    const struct MirLiveSegment *segments, int segment_count,
    int block, int value)
{
    int low = 0;
    int high = segment_count;

    while (low < high) {
        int middle = low + (high - low) / 2;
        const struct MirLiveSegment *item = &segments[middle];

        if (item->block < block ||
            (item->block == block && item->value < value))
            low = middle + 1;
        else
            high = middle;
    }
    if (low < segment_count &&
        segments[low].block == block &&
        segments[low].value == value)
        return &segments[low];
    return NULL;
}

static int mir_schedule_location_differs(
    const struct MirLiveSegment *left,
    const struct MirLiveSegment *right)
{
    if (left == NULL || right == NULL)
        return 0;
    if (left->color != right->color)
        return 1;
    if (left->color < 0 &&
        left->spill_slot != right->spill_slot)
        return 1;
    return 0;
}

static int mir_schedule_count_split_moves(
    const struct MirScheduleBlock *blocks, int block_count,
    const int *instruction_blocks,
    const struct MirLiveSegment *segments, int segment_count)
{
    int moves = 0;
    int block;

    for (block = 0; block < block_count; ++block) {
        const struct MirInsn *terminator =
            &mir.insns[blocks[block].last];
        int successor;

        for (successor = 0;
             successor < terminator->successor_count; ++successor) {
            int target_instruction = terminator->successors[successor];
            int target_block;
            int segment;

            if (target_instruction < 0 ||
                target_instruction >= mir.count)
                continue;
            target_block = instruction_blocks[target_instruction];
            if (target_block == block)
                continue;
            for (segment = 0; segment < segment_count; ++segment) {
                const struct MirLiveSegment *source =
                    &segments[segment];
                const struct MirLiveSegment *target;

                if (source->block != block ||
                    (source->flags & MIR_SCHEDULE_LIVE_OUT) == 0)
                    continue;
                target = mir_schedule_find_segment(
                    segments, segment_count,
                    target_block, source->value);
                if (target != NULL &&
                    (target->flags & MIR_SCHEDULE_LIVE_IN) != 0 &&
                    mir_schedule_location_differs(source, target))
                    ++moves;
            }
        }
    }
    return moves;
}

static int mir_schedule_count_boundary_moves(
    const int *instruction_blocks,
    const struct MirLiveSegment *segments, int segment_count)
{
    int moves = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        struct MirTargetConstraint constraint;
        const struct MirInsn *insn = &mir.insns[instruction];
        const struct MirLiveSegment *segment;
        int block = instruction_blocks[instruction];

        if (!mir_target_constraint_for_insn(insn, &constraint))
            continue;
        if (insn->src1 >= 0 && constraint.required_input1 != 0) {
            segment = mir_schedule_find_segment(
                segments, segment_count, block, insn->src1);
            if (segment == NULL ||
                (segment->color < 0 && segment->color != -2) ||
                (segment->color >= 0 &&
                mir_schedule_color_units(segment->color) !=
                    constraint.required_input1))
                ++moves;
        }
        if (insn->src2 >= 0 && constraint.required_input2 != 0) {
            segment = mir_schedule_find_segment(
                segments, segment_count, block, insn->src2);
            if (segment == NULL ||
                (segment->color < 0 && segment->color != -2) ||
                (segment->color >= 0 &&
                mir_schedule_color_units(segment->color) !=
                    constraint.required_input2))
                ++moves;
        }
        if (insn->dst >= 0 && constraint.required_output != 0) {
            segment = mir_schedule_find_segment(
                segments, segment_count, block, insn->dst);
            if (segment == NULL ||
                (segment->color < 0 && segment->color != -2) ||
                (segment->color >= 0 &&
                mir_schedule_color_units(segment->color) !=
                    constraint.required_output))
                ++moves;
        }
    }
    return moves;
}

int mir_build_shadow_schedule(struct MirScheduleSummary *summary)
{
    struct MirScheduleBlock *blocks;
    struct MirLiveSegment *segments;
    int *instruction_blocks;
    int block_count;
    int instruction;

    if (summary == NULL)
        return 0;
    memset(summary, 0, sizeof(*summary));
    if (mir.count <= 0)
        return 1;
    block_count =
        mir_schedule_build_blocks(&blocks, &instruction_blocks);
    summary->blocks = block_count;
    summary->cfg_edges =
        mir_schedule_count_cfg_edges(
            blocks, instruction_blocks, block_count);
    summary->phi_edge_uses = mir_schedule_count_phi_edge_uses();
    segments = NULL;
    if (mir.next_value > 0 &&
        mir.live_in != NULL && mir.live_out != NULL) {
        summary->segments =
            mir_schedule_build_segments(blocks, block_count, &segments);
        if (summary->segments < 0) {
            ++summary->unsupported;
            summary->segments = 0;
        }
        summary->maximum_pressure = mir_schedule_maximum_pressure();
        mir_schedule_allocate_segments(
            segments, summary->segments, summary);
        summary->split_moves =
            mir_schedule_count_split_moves(
                blocks, block_count, instruction_blocks,
                segments, summary->segments);
        summary->boundary_moves =
            mir_schedule_count_boundary_moves(
                instruction_blocks, segments, summary->segments);
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        struct MirTargetConstraint constraint;

        if (!mir_target_constraint_for_insn(
                &mir.insns[instruction], &constraint))
            ++summary->unsupported;
        else {
            if (constraint.required_input1 != 0 ||
                constraint.required_input2 != 0 ||
                constraint.required_output != 0)
                ++summary->fixed_constraints;
            if (mir_schedule_is_call(&mir.insns[instruction]))
                ++summary->call_splits;
        }
    }
    free(segments);
    free(instruction_blocks);
    free(blocks);
    return summary->unsupported == 0;
}

void mir_schedule_report_shadow_plan(void)
{
    const char *filter;
    struct MirScheduleSummary summary;
    int valid;

    if (getenv("DCC_MIR_SCHEDULE_REPORT") == NULL &&
        getenv("DCC_MIR_SCHEDULE_REQUIRE") == NULL)
        return;
    filter = getenv("DCC_MIR_SCHEDULE_FUNCTION");
    if (filter != NULL && filter[0] != 0 && strcmp(filter, mir.name))
        return;
    valid = mir_build_shadow_schedule(&summary);
    if (getenv("DCC_MIR_SCHEDULE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR schedule-plan function=%s valid=%d blocks=%d "
                "segments=%d edges=%d phi-edge-uses=%d call-splits=%d "
                "fixed=%d pressure=%d colored=%d remat=%d spills=%d "
                "iy=%d boundary-moves=%d split-moves=%d unsupported=%d\n",
                mir.name, valid, summary.blocks, summary.segments,
                summary.cfg_edges, summary.phi_edge_uses,
                summary.call_splits, summary.fixed_constraints,
                summary.maximum_pressure, summary.colored_segments,
                summary.rematerialized_segments,
                summary.spilled_segments, summary.iy_segments,
                summary.boundary_moves, summary.split_moves,
                summary.unsupported);
    if (!valid && getenv("DCC_MIR_SCHEDULE_REQUIRE") != NULL)
        fatal("cannot build MIR shadow schedule");
}
