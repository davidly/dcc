/**
 * @file dcc_mir_verify.c
 * @brief Verifies MIR predecessor topology and reachable value dominance.
 *
 * @par Role
 * Builds an independent instruction CFG and an immediate-dominator tree in
 * reverse postorder, then checks ordinary uses, logical block-entry PHI
 * predecessors, and PHI-edge values. Storage is linear in instructions,
 * labels, and virtual values.
 *
 * @par Boundary
 * Requires structurally checked MIR. Does not rewrite instructions, use cached
 * analysis, promote objects, allocate registers, or select machine schedules.
 */
#include <limits.h>
#include "dcc_mir_internal.h"

static int *mir_dom_allocate(size_t count)
{
    int *storage;

    if (count > ((size_t)-1 / sizeof(*storage)) - 1)
        fatal("MIR dominance storage overflow");
    storage = (int *)malloc((count + 1) * sizeof(*storage));
    if (storage == NULL)
        fatal("out of memory verifying MIR dominance");
    return storage;
}

static int mir_dom_intersect(int left, int right,
                             const int *parent, const int *rank)
{
    while (left != right) {
        while (rank[left] > rank[right])
            left = parent[left];
        while (rank[right] > rank[left])
            right = parent[right];
    }
    return left;
}

static int mir_dom_contains(int definition, int use,
                            const int *entry, const int *exit)
{
    return definition >= 0 && entry[definition] >= 0 && entry[use] >= 0 &&
           entry[definition] <= entry[use] && exit[use] <= exit[definition];
}

static int mir_dom_phi_predecessors_valid(
    int phi_instruction, const int *offsets,
    const int *predecessors, const int *labels,
    const int *definitions, const int *entry,
    const int *exit, int *dominance_valid)
{
    const struct MirInsn *phi = &mir.insns[phi_instruction];
    int block_start = mir_phi_physical_start(phi_instruction);
    int block_entry;
    int slot_predecessors[2] = {-1, -1};
    int slot_has_alternate[2] = {0, 0};
    int target;

    /* Collapse label/NOP entry aliases, but count only external CFG edges. */
    while (block_start > 0 &&
           (mir.insns[block_start - 1].opcode == MIR_LABEL ||
            mir.insns[block_start - 1].opcode == MIR_NOP))
        --block_start;
    block_entry = mir_first_nonlabel_successor(block_start);
    for (target = block_start; target <= block_entry; ++target) {
        int edge;

        for (edge = offsets[target]; edge < offsets[target + 1]; ++edge) {
            int predecessor = predecessors[edge];
            const struct MirInsn *prior = &mir.insns[predecessor];
            int predecessor_label;
            int edge_label = -1;
            int slot;
            int source;

            if (predecessor >= block_start && predecessor < block_entry)
                continue;
            predecessor_label = mir_block_label_before(predecessor);
            if ((prior->opcode == MIR_JUMP ||
                 prior->opcode == MIR_BRANCH_FALSE) &&
                prior->label >= 0 && prior->label < mir.next_label &&
                labels[prior->label] == target)
                edge_label = prior->label;
            slot = mir_phi_slot_for_edge(
                phi, predecessor_label, edge_label,
                target, phi_instruction);
            if (slot < 0)
                return 0;
            if (slot_predecessors[slot] < 0)
                slot_predecessors[slot] = predecessor;
            else if (slot_predecessors[slot] != predecessor)
                slot_has_alternate[slot] = 1;
            source = slot == 0 ? phi->src1 : phi->src2;
            if (entry[predecessor] >= 0 &&
                !mir_dom_contains(
                    definitions[source], predecessor, entry, exit)) {
                fprintf(stderr,
                        "; MIR %s: instruction %d has non-dominating "
                        "PHI predecessor %d\n",
                        mir.name, phi_instruction, predecessor);
                *dominance_valid = 0;
            }
        }
    }
    return slot_predecessors[0] >= 0 &&
           slot_predecessors[1] >= 0 &&
           (slot_predecessors[0] != slot_predecessors[1] ||
            slot_has_alternate[0] || slot_has_alternate[1]);
}

int mir_verify_dominance(void)
{
    int *targets;
    int *offsets;
    int *predecessors;
    int *cursor;
    int *stack;
    int *order;
    int *rank;
    int *parent;
    int *progress;
    int *definitions;
    int *labels;
    int instruction;
    int count = mir.count;
    int depth = 0;
    int visited = 0;
    int changed;
    int valid = 1;
    int clock = 0;

    if (count == 0)
        return 1;
    if (count < 0 || count > (INT_MAX - 1) / 2)
        return 0;
    targets = mir_dom_allocate((size_t)count * 2);
    offsets = mir_dom_allocate((size_t)count + 1);
    predecessors = mir_dom_allocate((size_t)count * 2);
    cursor = mir_dom_allocate((size_t)count);
    stack = mir_dom_allocate((size_t)count);
    order = mir_dom_allocate((size_t)count);
    rank = mir_dom_allocate((size_t)count);
    parent = mir_dom_allocate((size_t)count);
    progress = mir_dom_allocate((size_t)count);
    definitions = mir_dom_allocate((size_t)mir.next_value);
    labels = mir_dom_allocate((size_t)mir.next_label);
    for (instruction = 0; instruction < mir.next_label; ++instruction)
        labels[instruction] = -1;
    for (instruction = 0; instruction < mir.next_value; ++instruction)
        definitions[instruction] = -1;
    memset(offsets, 0, ((size_t)count + 1) * sizeof(*offsets));
    for (instruction = 0; instruction < count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        rank[instruction] = -1;
        parent[instruction] = -1;
        progress[instruction] = 0;
        if (insn->opcode == MIR_LABEL)
            labels[insn->label] = instruction;
        if (insn->opcode != MIR_NOP && insn->dst >= 0)
            definitions[insn->dst] = insn->opcode == MIR_PHI
                ? mir_phi_physical_start(instruction) : instruction;
    }
    for (instruction = 0; instruction < count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int first = -1;
        int second = -1;
        if (insn->opcode == MIR_JUMP || insn->opcode == MIR_BRANCH_FALSE)
            first = labels[insn->label];
        if (insn->opcode != MIR_JUMP && insn->opcode != MIR_RETURN &&
            instruction + 1 < count) {
            if (first < 0)
                first = instruction + 1;
            else
                second = instruction + 1;
        }
        targets[instruction * 2] = first;
        targets[instruction * 2 + 1] = second;
        if (first >= 0)
            ++offsets[first + 1];
        if (second >= 0)
            ++offsets[second + 1];
    }
    for (instruction = 1; instruction <= count; ++instruction)
        offsets[instruction] += offsets[instruction - 1];
    memcpy(cursor, offsets, (size_t)count * sizeof(*cursor));
    for (instruction = 0; instruction < count; ++instruction) {
        int edge;
        for (edge = 0; edge < 2; ++edge) {
            int target = targets[instruction * 2 + edge];
            if (target >= 0)
                predecessors[cursor[target]++] = instruction;
        }
    }
    rank[0] = 0;
    stack[depth++] = 0;
    while (depth > 0) {
        int current = stack[depth - 1];
        if (progress[current] < 2) {
            int target = targets[current * 2 + progress[current]++];
            if (target >= 0 && rank[target] < 0) {
                rank[target] = 0;
                stack[depth++] = target;
            }
        } else {
            order[visited++] = current;
            --depth;
        }
    }
    for (instruction = 0; instruction < visited; ++instruction)
        rank[order[instruction]] = visited - instruction - 1;
    parent[0] = 0;
    do {
        changed = 0;
        for (instruction = visited - 2; instruction >= 0; --instruction) {
            int current = order[instruction];
            int next_parent = -1;
            int edge;
            for (edge = offsets[current]; edge < offsets[current + 1]; ++edge) {
                int prior = predecessors[edge];
                if (parent[prior] >= 0)
                    next_parent = next_parent < 0 ? prior :
                        mir_dom_intersect(next_parent, prior, parent, rank);
            }
            if (parent[current] != next_parent) {
                parent[current] = next_parent;
                changed = 1;
            }
        }
    } while (changed);
    for (instruction = 0; instruction < count; ++instruction) {
        cursor[instruction] = -1;
        rank[instruction] = -1;
    }
    for (instruction = 1; instruction < count; ++instruction)
        if (parent[instruction] >= 0) {
            progress[instruction] = cursor[parent[instruction]];
            cursor[parent[instruction]] = instruction;
        }
    rank[0] = clock++;
    stack[depth++] = 0;
    while (depth > 0) {
        int current = stack[depth - 1];
        int child = cursor[current];
        if (child >= 0) {
            cursor[current] = progress[child];
            rank[child] = clock++;
            stack[depth++] = child;
        } else {
            order[current] = clock++;
            --depth;
        }
    }
    for (instruction = 0; instruction < count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_PHI &&
            !mir_dom_phi_predecessors_valid(
                instruction, offsets, predecessors, labels,
                definitions, rank, order, &valid)) {
            fprintf(stderr,
                    "; MIR %s: instruction %d has invalid PHI predecessors\n",
                    mir.name, instruction);
            valid = 0;
        }
        if (rank[instruction] < 0 || insn->opcode == MIR_NOP)
            continue;
        if (insn->opcode == MIR_PHI) {
            int start = mir_phi_physical_start(instruction);
            if (start == 0) {
                fprintf(stderr, "; MIR %s: instruction %d has no incoming PHI edge\n",
                        mir.name, instruction);
                valid = 0;
            }
        } else {
            int operand;
            for (operand = 0; operand < 2; ++operand) {
                int source = operand == 0 ? insn->src1 : insn->src2;
                if (source >= 0 &&
                    (source == insn->dst ||
                     !mir_dom_contains(definitions[source], instruction, rank, order))) {
                    fprintf(stderr,
                            "; MIR %s: instruction %d uses non-dominating v%d\n",
                            mir.name, instruction, source);
                    valid = 0;
                }
            }
        }
        if (insn->opcode == MIR_CALL || insn->opcode == MIR_CALL_AGGREGATE) {
            int argument;
            for (argument = 0; argument < instruction; ++argument) {
                const struct MirInsn *arg = &mir.insns[argument];
                if (arg->opcode != MIR_ARG ||
                    arg->secondary_offset != insn->secondary_offset)
                    continue;
                if (!mir_dom_contains(argument, instruction, rank, order) ||
                    !mir_dom_contains(definitions[arg->src1], instruction, rank, order)) {
                    fprintf(stderr,
                            "; MIR %s: argument %d does not dominate call %d\n",
                            mir.name, argument, instruction);
                    valid = 0;
                }
            }
        }
    }
    free(labels);
    free(definitions);
    free(progress);
    free(parent);
    free(rank);
    free(order);
    free(stack);
    free(cursor);
    free(predecessors);
    free(offsets);
    free(targets);
    return valid;
}