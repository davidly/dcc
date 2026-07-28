/* dcc_mir.c - analysis-only virtual-register machine IR prototype.
 *
 * dcc currently assigns HL/DE/BC while walking one statement AST at a time.
 * This module is the first vertical slice toward a real allocator: before an
 * AST is emitted, lower it into a persistent per-function stream of unlimited
 * virtual values, then build CFG successors and solve virtual-value liveness.
 *
 * It is deliberately read-only. Set DCC_MIR_REPORT=1 to dump every generated
 * function attempt, or DCC_MIR_FUNCTION=name to restrict the dump. Normal
 * codegen does not call into the lowering work at all beyond three cheap
 * inactive guards.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"

enum MirOpcode {
    MIR_NOP,
    MIR_PARAM,
    MIR_CONST,
    MIR_ADDRESS,
    MIR_LOAD,
    MIR_INDEX_LOAD,
    MIR_STORE,
    MIR_UNARY,
    MIR_BINARY,
    MIR_ARG,
    MIR_CALL,
    MIR_LABEL,
    MIR_JUMP,
    MIR_BRANCH_FALSE,
    MIR_OBJECT_MERGE,
    MIR_PHI,
    MIR_RETURN,
    MIR_OPAQUE
};

struct MirInsn {
    int opcode;
    int dst;
    int src1;
    int src2;
    int type;
    long immediate;
    int label;
    int phi_pred1;
    int phi_pred2;
    int successors[2];
    int successor_count;
    int object;
    char name[64];
};

struct MirObject {
    char name[64];
    int storage;
    int type;
    int offset;
    int entry_value;
};

struct MirFunction {
    struct MirInsn *insns;
    int count;
    int capacity;
    int next_value;
    int next_label;
    int active;
    int sink_purpose;
    int break_labels[MAX_FLOW];
    int continue_labels[MAX_FLOW];
    int flow_depth;
    int emit_mode;
    int report_mode;
    int return_type;
    FILE *capture_stream;
    EmitSink saved_sink;
    struct MirObject objects[256];
    int object_count;
    struct Sym *initializer_target;
    char name[64];
};

static struct MirFunction mir;

static const char *mir_opcode_name(int opcode)
{
    switch (opcode) {
    case MIR_NOP: return "nop";
    case MIR_PARAM: return "param";
    case MIR_CONST: return "const";
    case MIR_ADDRESS: return "address";
    case MIR_LOAD: return "load";
    case MIR_INDEX_LOAD: return "index";
    case MIR_STORE: return "store";
    case MIR_UNARY: return "unary";
    case MIR_BINARY: return "binary";
    case MIR_ARG: return "arg";
    case MIR_CALL: return "call";
    case MIR_LABEL: return "label";
    case MIR_JUMP: return "jump";
    case MIR_BRANCH_FALSE: return "brfalse";
    case MIR_OBJECT_MERGE: return "objmerge";
    case MIR_PHI: return "phi";
    case MIR_RETURN: return "return";
    default: return "opaque";
    }
}

static const char *mir_sink_name(int purpose)
{
    switch (purpose) {
    case EMIT_SINK_FINAL: return "final";
    case EMIT_SINK_DISCARD: return "discard";
    case EMIT_SINK_VERIFY: return "verify";
    case EMIT_SINK_DEFERRED: return "deferred";
    default: return "unknown";
    }
}

static int mir_report_enabled(const char *name)
{
    const char *all = getenv("DCC_MIR_REPORT");
    const char *filter = getenv("DCC_MIR_FUNCTION");
    const char *emit_filter = getenv("DCC_MIR_EMIT_FUNCTION");
    const char *candidates = getenv("DCC_MIR_CANDIDATES");
    const char *emit_all = getenv("DCC_MIR_EMIT_ALL");

    if (all == NULL && filter == NULL && emit_filter == NULL &&
        candidates == NULL && emit_all == NULL)
        return 0;
    if (emit_filter != NULL && emit_filter[0] != 0 &&
        strcmp(emit_filter, name) == 0)
        return 1;
    if (filter != NULL && filter[0] != 0 && strcmp(filter, name) != 0)
        return 0;
    return 1;
}

static struct MirInsn *mir_emit(int opcode)
{
    struct MirInsn *insn;

    if (!mir.active)
        return NULL;
    if (mir.count == mir.capacity) {
        int new_capacity = mir.capacity ? mir.capacity * 2 : 128;
        struct MirInsn *new_insns = (struct MirInsn *)realloc(
            mir.insns, (size_t)new_capacity * sizeof(*new_insns));
        if (new_insns == NULL)
            fatal("out of memory building MIR");
        mir.insns = new_insns;
        mir.capacity = new_capacity;
    }
    insn = &mir.insns[mir.count++];
    memset(insn, 0, sizeof(*insn));
    insn->opcode = opcode;
    insn->dst = -1;
    insn->src1 = -1;
    insn->src2 = -1;
    insn->label = -1;
    insn->phi_pred1 = -1;
    insn->phi_pred2 = -1;
    insn->object = -1;
    return insn;
}

static int mir_new_value(void)
{
    return mir.next_value++;
}

static int mir_new_label(void)
{
    return mir.next_label++;
}

static void mir_copy_name(char *dst, const char *name)
{
    size_t length;

    if (name == NULL)
        name = "?";
    length = strlen(name);
    if (length >= 64)
        length = 63;
    memcpy(dst, name, length);
    dst[length] = 0;
}

static int mir_object_eligible(const struct Sym *sym)
{
    if (sym == NULL)
        return 0;
    if (sym->storage != SC_LOCAL && sym->storage != SC_PARAM)
        return 0;
    if (sym->is_volatile || sym->is_array || sym->is_vla ||
        sym->is_const_value)
        return 0;
    if (type_size(sym->type) < 1 || type_size(sym->type) > 2)
        return 0;
    if (local_name_address_taken_in_function(sym->name))
        return 0;
    return 1;
}

static int mir_find_object(const char *name)
{
    int object;

    for (object = 0; object < mir.object_count; ++object)
        if (strcmp(mir.objects[object].name, name) == 0)
            return object;
    return -1;
}

static int mir_get_object(const struct Sym *sym, const char *name)
{
    struct MirObject *object;
    int index;

    if (!mir_object_eligible(sym))
        return -1;
    index = mir_find_object(name);
    if (index >= 0)
        return index;
    if (mir.object_count >= (int)(sizeof(mir.objects) / sizeof(mir.objects[0])))
        return -1;
    index = mir.object_count++;
    object = &mir.objects[index];
    memset(object, 0, sizeof(*object));
    mir_copy_name(object->name, name);
    object->storage = sym->storage;
    object->type = sym->type;
    object->offset = sym->offset;
    object->entry_value = -1;
    return index;
}

static void mir_emit_label(int label)
{
    struct MirInsn *insn = mir_emit(MIR_LABEL);
    if (insn != NULL)
        insn->label = label;
}

static void mir_emit_jump(int label)
{
    struct MirInsn *insn = mir_emit(MIR_JUMP);
    if (insn != NULL)
        insn->label = label;
}

static void mir_emit_object_merges(void)
{
    int object;

    for (object = 0; object < mir.object_count; ++object) {
        struct MirInsn *merge = mir_emit(MIR_OBJECT_MERGE);
        merge->dst = mir_new_value();
        merge->type = mir.objects[object].type;
        merge->object = object;
        mir_copy_name(merge->name, mir.objects[object].name);
    }
}

static int mir_lower_expr(const struct AstNode *node);

static void mir_emit_ident_store(const struct AstNode *ident, int value)
{
    struct MirInsn *store;
    struct Sym *symbol;

    if (ident == NULL || ident->kind != AST_IDENT)
        return;
    symbol = ident->sym;
    if (symbol == NULL && ident->sval != NULL)
        symbol = find_sym(ident->sval);
    store = mir_emit(MIR_STORE);
    store->src1 = value;
    store->type = ident->type;
    mir_copy_name(store->name, ident->sval ? ident->sval :
                               (symbol ? symbol->name : "?"));
    store->object = mir_get_object(symbol, store->name);
}

static int mir_lower_incdec(const struct AstNode *operand, int operation,
                            int postfix)
{
    struct MirInsn *insn;
    int old_value;
    int one;
    int new_value;

    if (operand == NULL || operand->kind != AST_IDENT)
        return -1;
    old_value = mir_lower_expr(operand);
    one = mir_new_value();
    insn = mir_emit(MIR_CONST);
    insn->dst = one;
    insn->type = operand->type;
    insn->immediate = 1;
    new_value = mir_new_value();
    insn = mir_emit(MIR_BINARY);
    insn->dst = new_value;
    insn->src1 = old_value;
    insn->src2 = one;
    insn->type = operand->type;
    insn->immediate = operation == TOK_INC ? '+' : '-';
    mir_emit_ident_store(operand, new_value);
    return postfix ? old_value : new_value;
}

static int mir_compound_binary_operator(int assignment_operator)
{
    switch (assignment_operator) {
    case TOK_ADDEQ: return '+';
    case TOK_SUBEQ: return '-';
    case TOK_MULEQ: return '*';
    case TOK_DIVEQ: return '/';
    case TOK_MODEQ: return '%';
    case TOK_ANDEQ: return '&';
    case TOK_OREQ: return '|';
    case TOK_XOREQ: return '^';
    case TOK_SHLEQ: return TOK_SHL;
    case TOK_SHREQ: return TOK_SHR;
    default: return 0;
    }
}

static int mir_lower_expr(const struct AstNode *node)
{
    struct MirInsn *insn;
    int left;
    int right;
    int value;
    int false_label;
    int end_label;
    int true_value;
    int false_value;
    int true_label;
    int i;

    if (node == NULL)
        return -1;
    switch (node->kind) {
    case AST_INT_LIT:
        value = mir_new_value();
        insn = mir_emit(MIR_CONST);
        insn->dst = value;
        insn->type = node->type;
        insn->immediate = node->ival;
        return value;
    case AST_IDENT:
        {
            struct Sym *symbol = node->sym;
            if (symbol == NULL && node->sval != NULL)
                symbol = find_sym(node->sval);
        value = mir_new_value();
        insn = mir_emit(symbol != NULL && symbol->is_array
                        ? MIR_ADDRESS : MIR_LOAD);
        insn->dst = value;
        insn->type = node->type;
        mir_copy_name(insn->name, node->sval ? node->sval :
                                  (node->sym ? node->sym->name : "?"));
        insn->object = mir_get_object(symbol, insn->name);
        return value;
        }
    case AST_INDEX:
        left = mir_lower_expr(node->a);
        right = mir_lower_expr(node->b);
        value = mir_new_value();
        insn = mir_emit(MIR_INDEX_LOAD);
        insn->dst = value;
        insn->src1 = left;
        insn->src2 = right;
        insn->type = node->type;
        return value;
    case AST_CAST:
    case AST_UNARY:
        if (node->op == TOK_INC || node->op == TOK_DEC) {
            value = mir_lower_incdec(node->a, node->op, 0);
            if (value >= 0)
                return value;
        }
        left = mir_lower_expr(node->a);
        value = mir_new_value();
        insn = mir_emit(MIR_UNARY);
        insn->dst = value;
        insn->src1 = left;
        insn->type = node->type;
        insn->immediate = node->op;
        return value;
    case AST_POSTFIX:
        if (node->op == TOK_INC || node->op == TOK_DEC) {
            value = mir_lower_incdec(node->a, node->op, 1);
            if (value >= 0)
                return value;
        }
        break;
    case AST_BINARY:
        left = mir_lower_expr(node->a);
        right = mir_lower_expr(node->b);
        value = mir_new_value();
        insn = mir_emit(MIR_BINARY);
        insn->dst = value;
        insn->src1 = left;
        insn->src2 = right;
        insn->type = node->type;
        insn->immediate = node->op;
        return value;
    case AST_LOGAND:
        /* Preserve C short-circuit semantics explicitly. The result is a
         * fresh boolean value merged from constants on the true and false
         * paths; the RHS is unreachable when the LHS is false. */
        false_label = mir_new_label();
        end_label = mir_new_label();
        true_label = mir_new_label();
        left = mir_lower_expr(node->a);
        insn = mir_emit(MIR_BRANCH_FALSE);
        insn->src1 = left;
        insn->label = false_label;
        right = mir_lower_expr(node->b);
        insn = mir_emit(MIR_BRANCH_FALSE);
        insn->src1 = right;
        insn->label = false_label;
        mir_emit_label(true_label);
        true_value = mir_new_value();
        insn = mir_emit(MIR_CONST);
        insn->dst = true_value;
        insn->type = node->type;
        insn->immediate = 1;
        mir_emit_jump(end_label);
        mir_emit_label(false_label);
        false_value = mir_new_value();
        insn = mir_emit(MIR_CONST);
        insn->dst = false_value;
        insn->type = node->type;
        insn->immediate = 0;
        mir_emit_label(end_label);
        value = mir_new_value();
        insn = mir_emit(MIR_PHI);
        insn->dst = value;
        insn->src1 = true_value;
        insn->src2 = false_value;
        insn->phi_pred1 = true_label;
        insn->phi_pred2 = false_label;
        insn->type = node->type;
        return value;
    case AST_ASSIGN:
        if (node->a == NULL || node->a->kind != AST_IDENT)
            break;
        if (node->op == '=') {
            value = mir_lower_expr(node->b);
        } else {
            int binary_operator = mir_compound_binary_operator(node->op);
            if (binary_operator == 0)
                break;
            left = mir_lower_expr(node->a);
            right = mir_lower_expr(node->b);
            value = mir_new_value();
            insn = mir_emit(MIR_BINARY);
            insn->dst = value;
            insn->src1 = left;
            insn->src2 = right;
            insn->type = node->type;
            insn->immediate = binary_operator;
        }
        mir_emit_ident_store(node->a, value);
        return value;
    case AST_CALL:
        for (i = 0; i < node->list_len; ++i) {
            left = mir_lower_expr(node->list[i]);
            insn = mir_emit(MIR_ARG);
            insn->src1 = left;
            insn->immediate = i;
        }
        value = mir_new_value();
        insn = mir_emit(MIR_CALL);
        insn->dst = value;
        insn->type = node->type;
        if (node->a != NULL && node->a->kind == AST_IDENT)
            mir_copy_name(insn->name, node->a->sval);
        else
            mir_copy_name(insn->name, "<indirect>");
        return value;
    case AST_COMMA:
        (void)mir_lower_expr(node->a);
        return mir_lower_expr(node->b);
    default:
        break;
    }

    /* Unsupported expressions remain explicit barriers in the prototype.
     * They still define a value so surrounding supported operations preserve
     * their use/def structure. */
    value = mir_new_value();
    insn = mir_emit(MIR_OPAQUE);
    insn->dst = value;
    insn->type = node->type;
    insn->immediate = node->kind;
    return value;
}

static void mir_lower_stmt(const struct AstNode *node)
{
    struct MirInsn *insn;
    int condition;
    int else_label;
    int end_label;
    int top_label;
    int continue_label;
    int i;

    if (node == NULL)
        return;
    switch (node->kind) {
    case AST_EMPTY:
        return;
    case AST_EXPR_STMT:
        (void)mir_lower_expr(node->a);
        return;
    case AST_DECL:
        insn = mir_emit(MIR_OPAQUE);
        insn->immediate = AST_DECL;
        return;
    case AST_COMPOUND:
        for (i = 0; i < node->list_len; ++i)
            mir_lower_stmt(node->list[i]);
        return;
    case AST_RETURN:
        condition = mir_lower_expr(node->a);
        insn = mir_emit(MIR_RETURN);
        insn->src1 = condition;
        return;
    case AST_IF:
        else_label = mir_new_label();
        end_label = mir_new_label();
        condition = mir_lower_expr(node->a);
        insn = mir_emit(MIR_BRANCH_FALSE);
        insn->src1 = condition;
        insn->label = else_label;
        mir_lower_stmt(node->b);
        if (node->c != NULL)
            mir_emit_jump(end_label);
        mir_emit_label(else_label);
        if (node->c != NULL) {
            mir_lower_stmt(node->c);
            mir_emit_label(end_label);
        }
        return;
    case AST_WHILE:
    case AST_DOWHILE:
        top_label = mir_new_label();
        end_label = mir_new_label();
        continue_label = mir_new_label();
        mir.break_labels[mir.flow_depth] = end_label;
        mir.continue_labels[mir.flow_depth] = continue_label;
        ++mir.flow_depth;
        mir_emit_label(top_label);
        mir_emit_object_merges();
        if (node->kind == AST_WHILE) {
            condition = mir_lower_expr(node->a);
            insn = mir_emit(MIR_BRANCH_FALSE);
            insn->src1 = condition;
            insn->label = end_label;
        }
        mir_lower_stmt(node->b);
        mir_emit_label(continue_label);
        if (node->kind == AST_DOWHILE) {
            condition = mir_lower_expr(node->a);
            insn = mir_emit(MIR_BRANCH_FALSE);
            insn->src1 = condition;
            insn->label = end_label;
        }
        mir_emit_jump(top_label);
        mir_emit_label(end_label);
        --mir.flow_depth;
        return;
    case AST_FOR:
        top_label = mir_new_label();
        end_label = mir_new_label();
        continue_label = mir_new_label();
        if (node->a != NULL)
            (void)mir_lower_expr(node->a);
        mir.break_labels[mir.flow_depth] = end_label;
        mir.continue_labels[mir.flow_depth] = continue_label;
        ++mir.flow_depth;
        mir_emit_label(top_label);
        mir_emit_object_merges();
        if (node->b != NULL) {
            condition = mir_lower_expr(node->b);
            insn = mir_emit(MIR_BRANCH_FALSE);
            insn->src1 = condition;
            insn->label = end_label;
        }
        mir_lower_stmt(node->d);
        mir_emit_label(continue_label);
        if (node->c != NULL)
            (void)mir_lower_expr(node->c);
        mir_emit_jump(top_label);
        mir_emit_label(end_label);
        --mir.flow_depth;
        return;
    case AST_BREAK:
        if (mir.flow_depth > 0)
            mir_emit_jump(mir.break_labels[mir.flow_depth - 1]);
        else
            (void)mir_emit(MIR_OPAQUE);
        return;
    case AST_CONTINUE:
        if (mir.flow_depth > 0)
            mir_emit_jump(mir.continue_labels[mir.flow_depth - 1]);
        else
            (void)mir_emit(MIR_OPAQUE);
        return;
    default:
        insn = mir_emit(MIR_OPAQUE);
        insn->immediate = node->kind;
        return;
    }
}

void mir_begin_function(const char *name, int sink_purpose)
{
    const char *emit_filter;

    if (!mir_report_enabled(name)) {
        mir.active = 0;
        return;
    }
    mir.count = 0;
    mir.next_value = 0;
    mir.next_label = 0;
    mir.flow_depth = 0;
    mir.object_count = 0;
    mir.initializer_target = NULL;
    mir.sink_purpose = sink_purpose;
    mir.emit_mode = 0;
    mir.report_mode = getenv("DCC_MIR_REPORT") != NULL ||
                      getenv("DCC_MIR_FUNCTION") != NULL ||
                      getenv("DCC_MIR_CANDIDATES") != NULL ||
                      getenv("DCC_MIR_EMIT_FUNCTION") != NULL;
    mir.return_type = current_return_type;
    mir.capture_stream = NULL;
    mir_copy_name(mir.name, name);
    mir.active = 1;
    emit_filter = getenv("DCC_MIR_EMIT_FUNCTION");
    if ((emit_filter != NULL && emit_filter[0] != 0 &&
         strcmp(emit_filter, name) == 0) ||
        getenv("DCC_MIR_EMIT_ALL") != NULL) {
        mir.capture_stream = tmpfile();
        if (mir.capture_stream == NULL)
            fatal("cannot create MIR capture stream");
        mir.saved_sink = emit_sink_push(mir.capture_stream, sink_purpose);
        mir.emit_mode = 1;
    }
    mir_emit_label(mir_new_label());
    {
        int local;
        for (local = 0; local < g_frame.nlocals; ++local) {
            int object_index;
            int value;
            struct MirInsn *insn;

            if (locals[local].storage != SC_PARAM ||
                !mir_object_eligible(&locals[local]))
                continue;
            object_index = mir_get_object(&locals[local], locals[local].name);
            if (object_index < 0)
                continue;
            value = mir_new_value();
            mir.objects[object_index].entry_value = value;
            insn = mir_emit(MIR_PARAM);
            insn->dst = value;
            insn->type = locals[local].type;
            insn->object = object_index;
            mir_copy_name(insn->name, locals[local].name);
        }
    }
}

void mir_capture_stmt(const struct AstNode *stmt)
{
    if (mir.active)
        mir_lower_stmt(stmt);
}

void mir_set_initializer_target(struct Sym *symbol)
{
    if (mir.active)
        mir.initializer_target = symbol;
}

void mir_capture_initializer(const struct AstNode *expr)
{
    struct MirInsn *store;
    int value;

    if (!mir.active || mir.initializer_target == NULL)
        return;
    value = mir_lower_expr(expr);
    store = mir_emit(MIR_STORE);
    store->src1 = value;
    store->type = mir.initializer_target->type;
    mir_copy_name(store->name, mir.initializer_target->name);
    store->object = mir_get_object(mir.initializer_target, store->name);
    mir.initializer_target = NULL;
}

static int mir_find_label(int label)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_LABEL && mir.insns[i].label == label)
            return i;
    return -1;
}

static int mir_block_label_before(int instruction)
{
    int i;

    for (i = instruction; i >= 0; --i) {
        if (mir.insns[i].opcode == MIR_LABEL)
            return mir.insns[i].label;
        if (i < instruction &&
            (mir.insns[i].opcode == MIR_JUMP ||
             mir.insns[i].opcode == MIR_BRANCH_FALSE ||
             mir.insns[i].opcode == MIR_RETURN))
            break;
    }
    return -1;
}

/* A successor may start with one or more label pseudo-instructions; return
 * the first executable/pseudo-value operation in that block. */
static int mir_first_nonlabel_successor(int successor)
{
    while (successor >= 0 && successor < mir.count &&
           mir.insns[successor].opcode == MIR_LABEL)
        ++successor;
    return successor;
}

static int mir_phi_edge_uses_value(int predecessor, int successor, int value)
{
    int first = mir_first_nonlabel_successor(successor);
    int predecessor_label;
    const struct MirInsn *phi;

    if (first < 0 || first >= mir.count || mir.insns[first].opcode != MIR_PHI)
        return 0;
    phi = &mir.insns[first];
    predecessor_label = mir_block_label_before(predecessor);
    if (predecessor_label == phi->phi_pred1)
        return value == phi->src1;
    if (predecessor_label == phi->phi_pred2)
        return value == phi->src2;
    return 0;
}

#define MIR_OBJECT_UNDEFINED (-1)
#define MIR_OBJECT_AMBIGUOUS (-2)

static int mir_resolve_alias(const int *aliases, int value)
{
    int hops = 0;

    while (value >= 0 && aliases[value] >= 0 && aliases[value] != value &&
           hops++ < mir.next_value)
        value = aliases[value];
    return value;
}

static void mir_object_transfer(const struct MirInsn *insn,
                                const int *input, int *output)
{
    int object;

    memcpy(output, input, (size_t)mir.object_count * sizeof(*output));
    if ((insn->opcode == MIR_STORE || insn->opcode == MIR_PARAM ||
         insn->opcode == MIR_PHI) &&
        insn->object >= 0)
        output[insn->object] =
            insn->opcode == MIR_STORE ? insn->src1 : insn->dst;
    else if (insn->opcode == MIR_OPAQUE)
        for (object = 0; object < mir.object_count; ++object)
            output[object] = MIR_OBJECT_UNDEFINED;
}

static int mir_block_start_for_instruction(int instruction)
{
    int start = instruction;

    while (start > 0) {
        int previous_opcode;
        if (mir.insns[start].opcode == MIR_LABEL)
            break;
        previous_opcode = mir.insns[start - 1].opcode;
        if (previous_opcode == MIR_JUMP ||
            previous_opcode == MIR_BRANCH_FALSE ||
            previous_opcode == MIR_RETURN)
            break;
        --start;
    }
    return start;
}

static int mir_try_make_object_phi(int instruction, int object,
                                   const int *out_state)
{
    int block_start = mir_block_start_for_instruction(instruction);
    int predecessor_values[2];
    int predecessor_labels[2];
    int predecessor_count = 0;
    int predecessor;
    struct MirInsn *load;

    /* Phi association uses labels, so decline unlabeled fallthrough blocks
     * rather than inventing an imprecise edge identity. */
    if (block_start < 0 || block_start >= mir.count ||
        mir.insns[block_start].opcode != MIR_LABEL)
        return 0;
    for (predecessor = 0; predecessor < mir.count; ++predecessor) {
        int successor;
        for (successor = 0;
             successor < mir.insns[predecessor].successor_count;
             ++successor) {
            int value;
            int label;
            if (mir.insns[predecessor].successors[successor] != block_start)
                continue;
            if (predecessor_count >= 2)
                return 0;
            value = out_state[(size_t)predecessor * mir.object_count + object];
            label = mir_block_label_before(predecessor);
            if (value < 0 || label < 0)
                return 0;
            predecessor_values[predecessor_count] = value;
            predecessor_labels[predecessor_count] = label;
            ++predecessor_count;
            break;
        }
    }
    if (predecessor_count != 2 ||
        predecessor_values[0] == predecessor_values[1])
        return 0;
    load = &mir.insns[instruction];
    load->opcode = MIR_PHI;
    load->src1 = predecessor_values[0];
    load->src2 = predecessor_values[1];
    load->phi_pred1 = predecessor_labels[0];
    load->phi_pred2 = predecessor_labels[1];
    load->object = object;
    return 1;
}

/* Promote scalar object loads when every CFG predecessor agrees on the same
 * virtual value. Ambiguous joins and values crossing an opaque barrier remain
 * explicit memory operations; this prototype deliberately does not insert
 * object phis yet. Returns the number of loads folded into persistent values. */
static int mir_promote_objects(void)
{
    size_t state_count;
    int *in_state;
    int *out_state;
    int *next_state;
    int *aliases;
    int changed;
    int promoted = 0;
    int inserted_phi = 0;
    int i;

    if (mir.object_count == 0 || mir.count == 0)
        return 0;
    state_count = (size_t)mir.count * mir.object_count;
    in_state = (int *)malloc(state_count * sizeof(*in_state));
    out_state = (int *)malloc(state_count * sizeof(*out_state));
    next_state = (int *)malloc((size_t)mir.object_count * sizeof(*next_state));
    aliases = (int *)malloc((size_t)mir.next_value * sizeof(*aliases));
    if (in_state == NULL || out_state == NULL || next_state == NULL ||
        aliases == NULL)
        fatal("out of memory promoting MIR objects");
    for (i = 0; i < (int)state_count; ++i) {
        in_state[i] = MIR_OBJECT_UNDEFINED;
        out_state[i] = MIR_OBJECT_UNDEFINED;
    }
    for (i = 0; i < mir.next_value; ++i)
        aliases[i] = -1;

    do {
        changed = 0;
        for (i = 0; i < mir.count; ++i) {
            int *input = &in_state[(size_t)i * mir.object_count];
            int *output = &out_state[(size_t)i * mir.object_count];
            int object;

            if (i > 0) {
                int predecessor_count = 0;
                int predecessor;
                for (predecessor = 0; predecessor < mir.count; ++predecessor) {
                    int successor;
                    for (successor = 0;
                         successor < mir.insns[predecessor].successor_count;
                         ++successor) {
                        if (mir.insns[predecessor].successors[successor] != i)
                            continue;
                        if (predecessor_count == 0) {
                            memcpy(next_state,
                                   &out_state[(size_t)predecessor * mir.object_count],
                                   (size_t)mir.object_count * sizeof(*next_state));
                        } else {
                            int *predecessor_out =
                                &out_state[(size_t)predecessor * mir.object_count];
                            for (object = 0; object < mir.object_count; ++object)
                                if (next_state[object] != predecessor_out[object])
                                    next_state[object] = MIR_OBJECT_AMBIGUOUS;
                        }
                        ++predecessor_count;
                        break;
                    }
                }
                if (predecessor_count == 0)
                    for (object = 0; object < mir.object_count; ++object)
                        next_state[object] = MIR_OBJECT_UNDEFINED;
                if (memcmp(input, next_state,
                           (size_t)mir.object_count * sizeof(*input)) != 0) {
                    memcpy(input, next_state,
                           (size_t)mir.object_count * sizeof(*input));
                    changed = 1;
                }
            }
            mir_object_transfer(&mir.insns[i], input, next_state);
            if (memcmp(output, next_state,
                       (size_t)mir.object_count * sizeof(*output)) != 0) {
                memcpy(output, next_state,
                       (size_t)mir.object_count * sizeof(*output));
                changed = 1;
            }
        }
    } while (changed);

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        int reaching;

        if (insn->opcode != MIR_OBJECT_MERGE || insn->object < 0)
            continue;
        reaching = in_state[(size_t)i * mir.object_count + insn->object];
        if (reaching == MIR_OBJECT_AMBIGUOUS &&
            mir_try_make_object_phi(i, insn->object, out_state)) {
            inserted_phi = 1;
            break;
        }
        if (reaching >= 0)
            aliases[insn->dst] = mir_resolve_alias(aliases, reaching);
        insn->opcode = MIR_NOP;
        insn->dst = -1;
    }
    if (!inserted_phi) {
        for (i = 0; i < mir.count; ++i) {
            struct MirInsn *insn = &mir.insns[i];
            int reaching;

            if (insn->opcode != MIR_LOAD || insn->object < 0)
                continue;
            reaching = in_state[(size_t)i * mir.object_count + insn->object];
            if (reaching == MIR_OBJECT_AMBIGUOUS &&
                mir_try_make_object_phi(i, insn->object, out_state)) {
                inserted_phi = 1;
                break;
            }
            if (reaching < 0 && getenv("DCC_MIR_OBJECT_REPORT") != NULL)
                fprintf(stderr,
                        "; MIR object unresolved function=%s insn=%d object=%s "
                        "state=%d block-start=%d\n",
                        mir.name, i, mir.objects[insn->object].name, reaching,
                        mir_block_start_for_instruction(i));
            if (reaching < 0)
                continue;
            aliases[insn->dst] = mir_resolve_alias(aliases, reaching);
            insn->opcode = MIR_NOP;
            insn->dst = -1;
            ++promoted;
        }
    }
    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        if (insn->src1 >= 0)
            insn->src1 = mir_resolve_alias(aliases, insn->src1);
        if (insn->src2 >= 0)
            insn->src2 = mir_resolve_alias(aliases, insn->src2);
    }

    free(aliases);
    free(next_state);
    free(out_state);
    free(in_state);
    /* Negative encoding asks the caller to rerun dataflow after the inserted
     * phi: -(N+1) preserves how many ordinary loads were already folded. */
    return inserted_phi ? -(promoted + 1) : promoted;
}

struct MirAllocationSummary {
    int colors[4];              /* HL, DE, BC, IY */
    int spills;
    int cross_call_values;
    int opaque_crossing_values;
    int fixed_moves;
    int operand_moves;
    int phi_moves;
};

enum MirPhysicalColor {
    MIR_COLOR_HL,
    MIR_COLOR_DE,
    MIR_COLOR_BC,
    MIR_COLOR_IY,
    MIR_COLOR_COUNT
};

static int mir_fixed_color_for_definition(const struct MirInsn *insn)
{
    if (insn->dst < 0)
        return -1;
    switch (insn->opcode) {
    case MIR_CALL:
    case MIR_BINARY:
    case MIR_UNARY:
    case MIR_INDEX_LOAD:
        /* Current Z80 contracts return expression/call results in HL. A later
         * instruction selector may relax some arithmetic operations, but the
         * allocation simulation must not assume that work has happened. */
        return MIR_COLOR_HL;
    default:
        return -1;
    }
}

static int mir_values_interfere(const unsigned char *interference,
                                int value_count, int left, int right)
{
    return interference[(size_t)left * value_count + right] != 0;
}

static void mir_add_live_set_interference(unsigned char *interference,
                                          int value_count,
                                          const unsigned char *live)
{
    int left;
    int right;

    for (left = 0; left < value_count; ++left) {
        if (!live[left])
            continue;
        for (right = left + 1; right < value_count; ++right) {
            if (!live[right])
                continue;
            interference[(size_t)left * value_count + right] = 1;
            interference[(size_t)right * value_count + left] = 1;
        }
    }
}

/* Greedy upper-bound allocation over MIR values. This does NOT emit code and
 * does not yet model instruction-specific register constraints (HL result
 * conventions, DE address formation, byte halves). It answers the preceding
 * architectural question: with exact MIR liveness, is global pressure itself
 * low enough for the Z80's three caller-saved pairs plus callee-saved IY?
 *
 * Values live across a call may use only IY. Values live across an opaque
 * barrier are forced to spill. Everything else prefers HL, DE, BC, then IY.
 * Order is descending interference degree, with call-crossing values first. */
static void mir_simulate_allocation(const unsigned char *live_in,
                                    const unsigned char *live_out,
                                    struct MirAllocationSummary *summary)
{
    int value_count = mir.next_value;
    unsigned char *interference;
    unsigned char *cross_call;
    unsigned char *cross_opaque;
    int *degree;
    int *order;
    int *color;
    int *fixed_color;
    int *preferences;
    int i;

    memset(summary, 0, sizeof(*summary));
    if (value_count == 0)
        return;
    interference = (unsigned char *)calloc(
        (size_t)value_count * value_count, 1);
    cross_call = (unsigned char *)calloc((size_t)value_count, 1);
    cross_opaque = (unsigned char *)calloc((size_t)value_count, 1);
    degree = (int *)calloc((size_t)value_count, sizeof(*degree));
    order = (int *)malloc((size_t)value_count * sizeof(*order));
    color = (int *)malloc((size_t)value_count * sizeof(*color));
    fixed_color = (int *)malloc((size_t)value_count * sizeof(*fixed_color));
    preferences = (int *)calloc((size_t)value_count * MIR_COLOR_COUNT,
                                sizeof(*preferences));
    if (interference == NULL || cross_call == NULL || cross_opaque == NULL ||
        degree == NULL || order == NULL || color == NULL || fixed_color == NULL ||
        preferences == NULL)
        fatal("out of memory simulating MIR allocation");

    for (i = 0; i < mir.count; ++i) {
        const unsigned char *in = &live_in[(size_t)i * value_count];
        const unsigned char *out = &live_out[(size_t)i * value_count];
        int value;

        mir_add_live_set_interference(interference, value_count, in);
        mir_add_live_set_interference(interference, value_count, out);
        if (mir.insns[i].opcode == MIR_CALL ||
            mir.insns[i].opcode == MIR_OPAQUE) {
            for (value = 0; value < value_count; ++value) {
                if (!in[value] || !out[value])
                    continue;
                if (mir.insns[i].opcode == MIR_CALL)
                    cross_call[value] = 1;
                else
                    cross_opaque[value] = 1;
            }
        }
    }
    for (i = 0; i < value_count; ++i) {
        int other;
        order[i] = i;
        color[i] = -1;
        fixed_color[i] = -1;
        for (other = 0; other < value_count; ++other)
            degree[i] += mir_values_interfere(interference, value_count,
                                               i, other);
    }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].dst >= 0) {
            fixed_color[mir.insns[i].dst] =
                mir_fixed_color_for_definition(&mir.insns[i]);
            if (fixed_color[mir.insns[i].dst] >= 0)
                ++preferences[(size_t)mir.insns[i].dst * MIR_COLOR_COUNT +
                              fixed_color[mir.insns[i].dst]];
        }
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        int required1 = -1;
        int required2 = -1;

        switch (insn->opcode) {
        case MIR_BINARY:
        case MIR_INDEX_LOAD:
            required1 = MIR_COLOR_HL;
            required2 = MIR_COLOR_DE;
            break;
        case MIR_UNARY:
        case MIR_ARG:
        case MIR_BRANCH_FALSE:
        case MIR_RETURN:
        case MIR_STORE:
            required1 = MIR_COLOR_HL;
            break;
        default:
            break;
        }
        if (required1 >= 0 && insn->src1 >= 0)
            ++preferences[(size_t)insn->src1 * MIR_COLOR_COUNT + required1];
        if (required2 >= 0 && insn->src2 >= 0)
            ++preferences[(size_t)insn->src2 * MIR_COLOR_COUNT + required2];
    }
    /* Stable selection sort is plenty for the small per-function prototype. */
    for (i = 0; i < value_count; ++i) {
        int best = i;
        int candidate;
        for (candidate = i + 1; candidate < value_count; ++candidate) {
            int left = order[best];
            int right = order[candidate];
            if (cross_call[right] > cross_call[left] ||
                (cross_call[right] == cross_call[left] &&
                 degree[right] > degree[left]))
                best = candidate;
        }
        if (best != i) {
            int temporary = order[i];
            order[i] = order[best];
            order[best] = temporary;
        }
    }
    for (i = 0; i < value_count; ++i) {
        int value = order[i];
        int first_color = cross_call[value] ? MIR_COLOR_IY : MIR_COLOR_HL;
        int last_color = cross_call[value] ? MIR_COLOR_IY : MIR_COLOR_IY;
        int chosen;

        if (cross_call[value])
            ++summary->cross_call_values;
        if (cross_opaque[value]) {
            ++summary->opaque_crossing_values;
            ++summary->spills;
            continue;
        }
        chosen = -1;
        {
            int candidate;
            int best_preference = -1;
            for (candidate = first_color; candidate <= last_color; ++candidate) {
                int other;
                int available = 1;
                int preference = preferences[(size_t)value * MIR_COLOR_COUNT +
                                             candidate];
                for (other = 0; other < value_count; ++other) {
                    if (color[other] == candidate &&
                        mir_values_interfere(interference, value_count,
                                             value, other)) {
                        available = 0;
                        break;
                    }
                }
                if (available && preference > best_preference) {
                    chosen = candidate;
                    best_preference = preference;
                }
            }
        }
        if (chosen < 0) {
            ++summary->spills;
        } else {
            color[value] = chosen;
            ++summary->colors[chosen];
            /* The instruction produces the value in a fixed result register,
             * then a boundary move places it in its allocated lifetime home.
             * This is live-range splitting, not a spill to memory. */
            if (fixed_color[value] >= 0 && fixed_color[value] != chosen)
                ++summary->fixed_moves;
        }
    }

    /* Price the operand side of current Z80 instruction contracts. These are
     * boundary copies, not lifetime homes: the allocator may keep a value in
     * BC/IY and move it to HL only for an operation that requires HL. */
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        int required1 = -1;
        int required2 = -1;

        switch (insn->opcode) {
        case MIR_BINARY:
        case MIR_INDEX_LOAD:
            required1 = MIR_COLOR_HL;
            required2 = MIR_COLOR_DE;
            break;
        case MIR_UNARY:
        case MIR_ARG:
        case MIR_BRANCH_FALSE:
        case MIR_RETURN:
        case MIR_STORE:
            required1 = MIR_COLOR_HL;
            break;
        case MIR_PHI:
            if (insn->dst >= 0 && insn->src1 >= 0 &&
                color[insn->dst] >= 0 && color[insn->src1] >= 0 &&
                color[insn->dst] != color[insn->src1])
                ++summary->phi_moves;
            if (insn->dst >= 0 && insn->src2 >= 0 &&
                color[insn->dst] >= 0 && color[insn->src2] >= 0 &&
                color[insn->dst] != color[insn->src2])
                ++summary->phi_moves;
            break;
        default:
            break;
        }
        if (required1 >= 0 && insn->src1 >= 0 &&
            color[insn->src1] >= 0 && color[insn->src1] != required1)
            ++summary->operand_moves;
        if (required2 >= 0 && insn->src2 >= 0 &&
            color[insn->src2] >= 0 && color[insn->src2] != required2)
            ++summary->operand_moves;
    }

    free(color);
    free(preferences);
    free(fixed_color);
    free(order);
    free(degree);
    free(cross_opaque);
    free(cross_call);
    free(interference);
}

static int mir_verify_and_dump(void)
{
    unsigned char *defined;
    unsigned char *live_in;
    unsigned char *live_out;
    int errors = 0;
    int block_count = 0;
    int max_live = 0;
    int opaque_count = 0;
    int promoted_objects;
    struct MirAllocationSummary allocation;
    int changed;
    int i;

    if (mir.next_value < 0 || mir.next_value > 4096) {
        fprintf(stderr, "; MIR %s: too many virtual values (%d)\n",
                mir.name, mir.next_value);
        return 0;
    }
    defined = (unsigned char *)calloc((size_t)mir.next_value, 1);
    live_in = (unsigned char *)calloc((size_t)mir.count * mir.next_value, 1);
    live_out = (unsigned char *)calloc((size_t)mir.count * mir.next_value, 1);
    if ((mir.next_value && defined == NULL) ||
        (mir.count && mir.next_value && (live_in == NULL || live_out == NULL)))
        fatal("out of memory verifying MIR");

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        int target;

        insn->successor_count = 0;
        if (insn->src1 >= 0 && !defined[insn->src1])
            ++errors;
        if (insn->src2 >= 0 && !defined[insn->src2])
            ++errors;
        if (insn->dst >= 0) {
            if (defined[insn->dst])
                ++errors;
            defined[insn->dst] = 1;
        }
        if (insn->opcode == MIR_JUMP || insn->opcode == MIR_BRANCH_FALSE) {
            target = mir_find_label(insn->label);
            if (target < 0)
                ++errors;
            else
                insn->successors[insn->successor_count++] = target;
        }
        if (insn->opcode == MIR_BRANCH_FALSE && i + 1 < mir.count)
            insn->successors[insn->successor_count++] = i + 1;
        else if (insn->opcode != MIR_JUMP && insn->opcode != MIR_RETURN &&
                 i + 1 < mir.count)
            insn->successors[insn->successor_count++] = i + 1;
        if (i == 0 || insn->opcode == MIR_LABEL)
            ++block_count;
    }

    promoted_objects = 0;
    for (;;) {
        int promoted_pass = mir_promote_objects();
        if (promoted_pass < 0) {
            promoted_objects += -promoted_pass - 1;
            continue;
        }
        promoted_objects += promoted_pass;
        break;
    }

    /* Object promotion rewrites uses and removes load definitions. Rebuild
     * the simple defined-value check from the transformed stream. */
    memset(defined, 0, (size_t)mir.next_value);
    errors = 0;
    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode != MIR_PHI &&
            insn->src1 >= 0 && !defined[insn->src1])
            ++errors;
        if (insn->opcode != MIR_PHI &&
            insn->src2 >= 0 && !defined[insn->src2])
            ++errors;
        if (insn->dst >= 0) {
            if (defined[insn->dst])
                ++errors;
            defined[insn->dst] = 1;
        }
    }

    do {
        changed = 0;
        for (i = mir.count - 1; i >= 0; --i) {
            struct MirInsn *insn = &mir.insns[i];
            unsigned char *in = &live_in[(size_t)i * mir.next_value];
            unsigned char *out = &live_out[(size_t)i * mir.next_value];
            int value;
            int successor;

            for (value = 0; value < mir.next_value; ++value) {
                int next_out = 0;
                int next_in;
                for (successor = 0; successor < insn->successor_count; ++successor) {
                    if (insn->successors[successor] < 0 ||
                        insn->successors[successor] >= mir.count) {
                        fprintf(stderr,
                                "; MIR %s: instruction %d has invalid successor %d\n",
                                mir.name, i, insn->successors[successor]);
                        ++errors;
                        continue;
                    }
                    next_out |= live_in[(size_t)insn->successors[successor] *
                                        mir.next_value + value];
                    if (mir_phi_edge_uses_value(
                            i, insn->successors[successor], value))
                        next_out = 1;
                }
                next_in = next_out && value != insn->dst;
                if (insn->opcode != MIR_PHI &&
                    (value == insn->src1 || value == insn->src2))
                    next_in = 1;
                if (out[value] != next_out || in[value] != next_in) {
                    out[value] = (unsigned char)next_out;
                    in[value] = (unsigned char)next_in;
                    changed = 1;
                }
            }
        }
    } while (changed);

    mir_simulate_allocation(live_in, live_out, &allocation);

    if (mir.report_mode)
        fprintf(stderr,
                "; MIR function=%s sink=%s insns=%d values=%d errors=%d\n",
                mir.name, mir_sink_name(mir.sink_purpose), mir.count,
                mir.next_value, errors);
    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        int in_count = 0;
        int out_count = 0;
        int value;

        for (value = 0; value < mir.next_value; ++value) {
            in_count += live_in[(size_t)i * mir.next_value + value] != 0;
            out_count += live_out[(size_t)i * mir.next_value + value] != 0;
        }
        if (in_count > max_live)
            max_live = in_count;
        if (out_count > max_live)
            max_live = out_count;
        if (insn->opcode == MIR_OPAQUE)
            ++opaque_count;
        if (!mir.report_mode)
            continue;
        fprintf(stderr, ";   %4d %-8s", i, mir_opcode_name(insn->opcode));
        if (insn->dst >= 0)
            fprintf(stderr, " v%d =", insn->dst);
        if (insn->src1 >= 0)
            fprintf(stderr, " v%d", insn->src1);
        if (insn->src2 >= 0)
            fprintf(stderr, ",v%d", insn->src2);
        if (insn->name[0])
            fprintf(stderr, " %s", insn->name);
        if (insn->object >= 0)
            fprintf(stderr, " {o%d}", insn->object);
        if (insn->opcode == MIR_CONST)
            fprintf(stderr, " %ld", insn->immediate);
        if (insn->opcode == MIR_UNARY || insn->opcode == MIR_BINARY)
            fprintf(stderr, " op=%ld", insn->immediate);
        if (insn->opcode == MIR_OPAQUE)
            fprintf(stderr, " ast=%ld", insn->immediate);
        if (insn->opcode == MIR_LABEL || insn->opcode == MIR_JUMP ||
            insn->opcode == MIR_BRANCH_FALSE)
            fprintf(stderr, " L%d", insn->label);
        if (insn->opcode == MIR_PHI)
            fprintf(stderr, " [L%d,L%d]", insn->phi_pred1, insn->phi_pred2);
        fprintf(stderr, "  ; live in=%d out=%d\n", in_count, out_count);
    }

    if (mir.report_mode) {
        fprintf(stderr,
                "; MIR summary function=%s blocks=%d max-live=%d opaque=%d "
                "objects=%d promoted-loads=%d\n",
                mir.name, block_count, max_live, opaque_count,
                mir.object_count, promoted_objects);
        fprintf(stderr,
            "; MIR allocation function=%s hl=%d de=%d bc=%d iy=%d spills=%d "
            "cross-call=%d opaque-cross=%d fixed-moves=%d operand-moves=%d "
            "phi-moves=%d\n",
            mir.name, allocation.colors[0], allocation.colors[1],
            allocation.colors[2], allocation.colors[3], allocation.spills,
            allocation.cross_call_values,
            allocation.opaque_crossing_values, allocation.fixed_moves,
            allocation.operand_moves, allocation.phi_moves);
    }

    free(live_out);
    free(live_in);
    free(defined);
    return errors == 0;
}

static const struct MirInsn *mir_definition(int value)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].dst == value)
            return &mir.insns[i];
    return NULL;
}

static void mir_emit_prologue(FILE *out)
{
    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (opt_stack_check)
        fputs("\textrn __stchk\n\tcall __stchk\n", out);
}

static int mir_emit_load_param(FILE *out, const struct MirInsn *param)
{
    const struct MirObject *object;

    if (param == NULL || param->opcode != MIR_PARAM || param->object < 0 ||
        param->object >= mir.object_count)
        return 0;
    object = &mir.objects[param->object];
    if (object->storage != SC_PARAM || type_size(object->type) != 2)
        return 0;
    fprintf(out, "\tld l,(ix%+d)\n", object->offset);
    fprintf(out, "\tld h,(ix%+d)\n", object->offset + 1);
    return 1;
}

static int mir_emit_load_param_de(FILE *out, const struct MirInsn *param)
{
    const struct MirObject *object;

    if (param == NULL || param->opcode != MIR_PARAM || param->object < 0 ||
        param->object >= mir.object_count)
        return 0;
    object = &mir.objects[param->object];
    if (object->storage != SC_PARAM || type_size(object->type) != 2)
        return 0;
    fprintf(out, "\tld e,(ix%+d)\n", object->offset);
    fprintf(out, "\tld d,(ix%+d)\n", object->offset + 1);
    return 1;
}

/* Recognize VALUE as one parameter plus a constant. PARAM is NULL for a pure
 * constant. This is intentionally not a general expression selector; it is a
 * proof that promoted local temporaries can disappear end-to-end before the
 * backend grows arbitrary register/stack expression handling. */
static int mir_affine_value(int value, const struct MirInsn **parameter,
                            long *constant, int depth)
{
    const struct MirInsn *definition;
    const struct MirInsn *left_parameter;
    const struct MirInsn *right_parameter;
    long left_constant;
    long right_constant;

    if (depth > 64)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_PARAM) {
        *parameter = definition;
        *constant = 0;
        return 1;
    }
    if (definition->opcode == MIR_CONST) {
        *parameter = NULL;
        *constant = definition->immediate;
        return 1;
    }
    if (definition->opcode != MIR_BINARY ||
        (definition->immediate != '+' && definition->immediate != '-'))
        return 0;
    if (!mir_affine_value(definition->src1, &left_parameter, &left_constant,
                          depth + 1) ||
        !mir_affine_value(definition->src2, &right_parameter, &right_constant,
                          depth + 1))
        return 0;
    if (definition->immediate == '+') {
        if (left_parameter != NULL && right_parameter != NULL)
            return 0;
        *parameter = left_parameter != NULL ? left_parameter : right_parameter;
        *constant = left_constant + right_constant;
        return 1;
    }
    /* PARAM-or-constant minus a parameter needs coefficient -1, outside the
     * first affine subset. */
    if (right_parameter != NULL)
        return 0;
    *parameter = left_parameter;
    *constant = left_constant - right_constant;
    return 1;
}

static void mir_emit_return_constant(FILE *out, long value)
{
    fprintf(out, "\tld hl,%ld\n", value);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
}

static int mir_is_const_value(int value, long expected)
{
    const struct MirInsn *definition = mir_definition(value);
    return definition != NULL && definition->opcode == MIR_CONST &&
           definition->immediate == expected;
}

/* Strict first loop selector:
 *
 *     while (n > 0) --n;
 *     return n;
 *
 * N is one word parameter represented by an object phi at the loop header.
 * Keep it in BC for the complete loop: this is the first emitted path that
 * consumes a MIR allocation decision rather than merely using fixed HL/DE
 * expression conventions. */
static int mir_try_emit_countdown_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *phi = NULL;
    const struct MirInsn *decrement = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *branch = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirObject *object;
    int object_index = -1;
    int top_label;
    int end_label;
    int i;
    int unsigned_value;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            object_index = insn->object;
        } else if (insn->opcode == MIR_PHI && insn->object == object_index) {
            phi = insn;
        } else if (insn->opcode == MIR_STORE && insn->object == object_index) {
            decrement = mir_definition(insn->src1);
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == '>') {
                compare = candidate;
                branch = insn;
            }
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (parameter == NULL || phi == NULL || decrement == NULL ||
        compare == NULL || branch == NULL || return_insn == NULL ||
        object_index < 0 || object_index >= mir.object_count)
        return 0;
    if (decrement->opcode != MIR_BINARY || decrement->immediate != '-' ||
        decrement->src1 != phi->dst ||
        !mir_is_const_value(decrement->src2, 1))
        return 0;
    if (compare->src1 != phi->dst || !mir_is_const_value(compare->src2, 0) ||
        return_insn->src1 != phi->dst)
        return 0;
    if (!((phi->src1 == parameter->dst && phi->src2 == decrement->dst) ||
          (phi->src2 == parameter->dst && phi->src1 == decrement->dst)))
        return 0;

    object = &mir.objects[object_index];
    if (object->storage != SC_PARAM || type_size(object->type) != 2)
        return 0;
    unsigned_value = (object->type & TYPE_UNSIGNED) != 0 ||
                     type_ptr_depth(object->type) > 0;
    top_label = new_label();
    end_label = new_label();

    mir_emit_prologue(out);
    fprintf(out, "\tld c,(ix%+d)\n", object->offset);
    fprintf(out, "\tld b,(ix%+d)\n", object->offset + 1);
    fprintf(out, "L%d:\n", top_label);
    if (unsigned_value) {
        fputs("\tld a,b\n\tor c\n", out);
        fprintf(out, "\tjp z,L%d\n", end_label);
    } else {
        fputs("\tld a,b\n\tor a\n", out);
        fprintf(out, "\tjp m,L%d\n", end_label);
        fputs("\tor c\n", out);
        fprintf(out, "\tjp z,L%d\n", end_label);
    }
    fputs("\tdec bc\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tld l,c\n\tld h,b\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

/* Two-register loop selector:
 *
 *     int sum = 0;
 *     while (n > 0) { sum += n; --n; }
 *     return sum;
 *
 * BC holds n and DE holds sum. Both values are object phis at the header and
 * neither is written back to the frame inside the loop. */
static int mir_try_emit_accumulator_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *n_phi = NULL;
    const struct MirInsn *sum_phi = NULL;
    const struct MirInsn *n_update = NULL;
    const struct MirInsn *sum_update = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *return_insn = NULL;
    int n_object = -1;
    int sum_object = -1;
    int i;
    int top_label;
    int end_label;
    int unsigned_value;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            n_object = insn->object;
        }
    }
    if (parameter == NULL || n_object < 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PHI) {
            if (insn->object == n_object)
                n_phi = insn;
            else if (sum_phi == NULL) {
                sum_phi = insn;
                sum_object = insn->object;
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_STORE) {
            const struct MirInsn *definition = mir_definition(insn->src1);
            if (insn->object == n_object)
                n_update = definition;
            else if (insn->object == sum_object || sum_object < 0)
                sum_update = definition;
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == '>')
                compare = candidate;
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (n_phi == NULL || sum_phi == NULL || n_update == NULL ||
        sum_update == NULL || compare == NULL || return_insn == NULL ||
        sum_object < 0)
        return 0;
    if (n_update->opcode != MIR_BINARY || n_update->immediate != '-' ||
        n_update->src1 != n_phi->dst || !mir_is_const_value(n_update->src2, 1))
        return 0;
    if (sum_update->opcode != MIR_BINARY || sum_update->immediate != '+' ||
        !((sum_update->src1 == sum_phi->dst && sum_update->src2 == n_phi->dst) ||
          (sum_update->src2 == sum_phi->dst && sum_update->src1 == n_phi->dst)))
        return 0;
    if (compare->src1 != n_phi->dst || !mir_is_const_value(compare->src2, 0) ||
        return_insn->src1 != sum_phi->dst)
        return 0;
    if (!((n_phi->src1 == parameter->dst && n_phi->src2 == n_update->dst) ||
          (n_phi->src2 == parameter->dst && n_phi->src1 == n_update->dst)))
        return 0;
    if (!((mir_is_const_value(sum_phi->src1, 0) &&
           sum_phi->src2 == sum_update->dst) ||
          (mir_is_const_value(sum_phi->src2, 0) &&
           sum_phi->src1 == sum_update->dst)))
        return 0;

    unsigned_value = (mir.objects[n_object].type & TYPE_UNSIGNED) != 0 ||
                     type_ptr_depth(mir.objects[n_object].type) > 0;
    top_label = new_label();
    end_label = new_label();
    mir_emit_prologue(out);
    fprintf(out, "\tld c,(ix%+d)\n", mir.objects[n_object].offset);
    fprintf(out, "\tld b,(ix%+d)\n", mir.objects[n_object].offset + 1);
    fputs("\tld de,0\n", out);
    fprintf(out, "L%d:\n", top_label);
    if (unsigned_value) {
        fputs("\tld a,b\n\tor c\n", out);
        fprintf(out, "\tjp z,L%d\n", end_label);
    } else {
        fputs("\tld a,b\n\tor a\n", out);
        fprintf(out, "\tjp m,L%d\n", end_label);
        fputs("\tor c\n", out);
        fprintf(out, "\tjp z,L%d\n", end_label);
    }
    fputs("\tex de,hl\n\tadd hl,bc\n\tex de,hl\n\tdec bc\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

/* Strict first CFG selector:
 *
 *     if (a == b) return C1; return C2;
 *
 * (or !=). This validates labels, branch polarity, two live inputs and
 * multiple exits without claiming general relational/comparison support. */
static int mir_try_emit_comparison_branch(FILE *out)
{
    const struct MirInsn *branch = NULL;
    const struct MirInsn *compare;
    const struct MirInsn *left;
    const struct MirInsn *right;
    const struct MirInsn *true_return = NULL;
    const struct MirInsn *false_return = NULL;
    const struct MirInsn *true_value;
    const struct MirInsn *false_value;
    int branch_index = -1;
    int target_index;
    int false_label;
    int operation;
    int unsigned_compare;
    int i;

    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].opcode == MIR_BRANCH_FALSE) {
            if (branch != NULL)
                return 0;
            branch = &mir.insns[i];
            branch_index = i;
        }
    }
    if (branch == NULL)
        return 0;
    target_index = mir_find_label(branch->label);
    if (target_index <= branch_index)
        return 0;
    compare = mir_definition(branch->src1);
    if (compare == NULL || compare->opcode != MIR_BINARY ||
        (compare->immediate != TOK_EQ && compare->immediate != TOK_NE &&
         compare->immediate != '<' && compare->immediate != TOK_GE &&
         compare->immediate != '>' && compare->immediate != TOK_LE))
        return 0;
    left = mir_definition(compare->src1);
    right = mir_definition(compare->src2);
    if (left == NULL || right == NULL || left->opcode != MIR_PARAM ||
        right->opcode != MIR_PARAM)
        return 0;
    if (left->object < 0 || left->object >= mir.object_count)
        return 0;
    operation = (int)compare->immediate;
    if (operation == '>') {
        const struct MirInsn *temporary = left;
        left = right;
        right = temporary;
        operation = '<';
    } else if (operation == TOK_LE) {
        const struct MirInsn *temporary = left;
        left = right;
        right = temporary;
        operation = TOK_GE;
    }
    unsigned_compare =
        (mir.objects[left->object].type & TYPE_UNSIGNED) != 0 ||
        type_ptr_depth(mir.objects[left->object].type) > 0;
    for (i = branch_index + 1; i < target_index; ++i)
        if (mir.insns[i].opcode == MIR_RETURN)
            true_return = &mir.insns[i];
    for (i = target_index + 1; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_RETURN) {
            false_return = &mir.insns[i];
            break;
        }
    if (true_return == NULL || false_return == NULL)
        return 0;
    true_value = mir_definition(true_return->src1);
    false_value = mir_definition(false_return->src1);
    if (true_value == NULL || false_value == NULL ||
        true_value->opcode != MIR_CONST || false_value->opcode != MIR_CONST)
        return 0;

    /* Reject any operation outside this exact graph shape. */
    for (i = 0; i < mir.count; ++i) {
        int opcode = mir.insns[i].opcode;
        if (opcode != MIR_PARAM && opcode != MIR_NOP && opcode != MIR_CONST &&
            opcode != MIR_BINARY && opcode != MIR_BRANCH_FALSE &&
            opcode != MIR_LABEL && opcode != MIR_RETURN)
            return 0;
    }

    false_label = new_label();
    mir_emit_prologue(out);
    if (!mir_emit_load_param(out, left) || !mir_emit_load_param_de(out, right))
        return 0;
    if (!unsigned_compare && (operation == '<' || operation == TOK_GE)) {
        /* Bias the sign bit on both operands, mapping signed order onto
         * unsigned order before the ordinary 16-bit subtract. */
        fputs("\tld a,h\n\txor 80h\n\tld h,a\n"
              "\tld a,d\n\txor 80h\n\tld d,a\n", out);
    }
    fputs("\tor a\n\tsbc hl,de\n", out);
    if (operation == TOK_EQ)
        fprintf(out, "\tjp nz,L%d\n", false_label);
    else if (operation == TOK_NE)
        fprintf(out, "\tjp z,L%d\n", false_label);
    else if (operation == '<')
        fprintf(out, "\tjp nc,L%d\n", false_label);
    else
        fprintf(out, "\tjp c,L%d\n", false_label);
    mir_emit_return_constant(out, true_value->immediate);
    fprintf(out, "L%d:\n", false_label);
    mir_emit_return_constant(out, false_value->immediate);
    return 1;
}

/* First emitted subset: one straight-line return of a word parameter,
 * constant, or parameter +/- constant. This intentionally proves the
 * transactional backend path before attempting general instruction
 * selection. Anything else falls back byte-for-byte to the captured existing
 * codegen. */
static int mir_try_emit_z80(FILE *out)
{
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *parameter;
    const struct MirInsn *definition;
    const struct MirInsn *left_parameter = NULL;
    const struct MirInsn *right_parameter = NULL;
    long constant;
    int two_parameters = 0;
    int two_parameter_operation = 0;
    int i;

    /* The current selectors implement only the ordinary 16-bit HL result
     * convention. Other return ABIs remain with the existing backend. */
    if ((mir.return_type & 15) != TYPE_INT)
        return 0;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_PARAM &&
            (mir.insns[i].type & (TYPE_PTR | TYPE_PTR2)) != 0)
            return 0;

    if (mir_try_emit_accumulator_loop(out))
        return 1;
    if (mir_try_emit_countdown_loop(out))
        return 1;
    if (mir_try_emit_comparison_branch(out))
        return 1;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_RETURN)
            return_insn = insn;
        else if (insn->opcode != MIR_NOP && insn->opcode != MIR_PARAM &&
                 insn->opcode != MIR_CONST && insn->opcode != MIR_BINARY &&
                 insn->opcode != MIR_LABEL &&
                 !(insn->opcode == MIR_STORE && insn->object >= 0))
            return 0;
    }
    if (return_insn == NULL || return_insn->src1 < 0)
        return 0;
    definition = mir_definition(return_insn->src1);
    if (definition != NULL && definition->opcode == MIR_BINARY &&
        (definition->immediate == '+' || definition->immediate == '-')) {
        left_parameter = mir_definition(definition->src1);
        right_parameter = mir_definition(definition->src2);
        if (left_parameter != NULL && right_parameter != NULL &&
            left_parameter->opcode == MIR_PARAM &&
            right_parameter->opcode == MIR_PARAM) {
            two_parameters = 1;
            two_parameter_operation = (int)definition->immediate;
        }
    }
    if (!two_parameters &&
        !mir_affine_value(return_insn->src1, &parameter, &constant, 0))
        return 0;

    mir_emit_prologue(out);
    if (two_parameters) {
        if (!mir_emit_load_param(out, left_parameter) ||
            !mir_emit_load_param_de(out, right_parameter))
            return 0;
        if (two_parameter_operation == '+')
            fputs("\tadd hl,de\n", out);
        else
            fputs("\tor a\n\tsbc hl,de\n", out);
        constant = 0;
    } else if (parameter != NULL) {
        if (!mir_emit_load_param(out, parameter))
            return 0;
    } else {
        fprintf(out, "\tld hl,%ld\n", constant);
        constant = 0;
    }
    if (constant == 1)
        fputs("\tinc hl\n", out);
    else if (constant == -1)
        fputs("\tdec hl\n", out);
    else if (constant != 0)
        fprintf(out, "\tld de,%ld\n\tadd hl,de\n", constant);
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

static int mir_try_emit_automatic_z80(FILE *out)
{
    int i;

    if ((mir.return_type & 15) != TYPE_INT)
        return 0;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_PARAM &&
            (mir.insns[i].type & (TYPE_PTR | TYPE_PTR2)) != 0)
            return 0;
    if (mir_try_emit_accumulator_loop(out))
        return 1;
    return mir_try_emit_countdown_loop(out);
}

static void mir_copy_capture(FILE *out)
{
    int character;

    rewind(mir.capture_stream);
    while ((character = fgetc(mir.capture_stream)) != EOF)
        fputc(character, out);
}

void mir_end_function(void)
{
    int verified;

    if (!mir.active)
        return;
    verified = mir_verify_and_dump();
    if (!mir.emit_mode && verified &&
        getenv("DCC_MIR_CANDIDATES") != NULL) {
        FILE *candidate = tmpfile();
        int accepted;
        if (candidate == NULL)
            fatal("cannot create MIR candidate stream");
        accepted = mir_try_emit_z80(candidate);
        fclose(candidate);
        if (accepted)
            fprintf(stderr, "; MIR candidate function=%s sink=%s\n",
                    mir.name, mir_sink_name(mir.sink_purpose));
    }
    if (mir.emit_mode) {
        FILE *destination = mir.saved_sink.stream;
        FILE *generated = NULL;
        int emitted = 0;

        emit_sink_restore(&mir.saved_sink);
        if (verified) {
            const char *emit_filter = getenv("DCC_MIR_EMIT_FUNCTION");
            generated = tmpfile();
            if (generated == NULL)
                fatal("cannot create MIR generated stream");
            if (emit_filter != NULL && emit_filter[0] != 0 &&
                strcmp(emit_filter, mir.name) == 0)
                emitted = mir_try_emit_z80(generated);
            else
                emitted = mir_try_emit_automatic_z80(generated);
        }
        if (emitted) {
            int character;
            rewind(generated);
            while ((character = fgetc(generated)) != EOF)
                fputc(character, destination);
        } else {
            mir_copy_capture(destination);
        }
        if (generated != NULL)
            fclose(generated);
        if (mir.report_mode)
            fprintf(stderr, "; MIR emit function=%s result=%s\n",
                mir.name, emitted ? "mir" : "fallback");
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
    }
    mir.active = 0;
}
