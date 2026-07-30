/* dcc_mir.c - typed virtual-register machine IR and transactional backend.
 *
 * dcc currently assigns HL/DE/BC while walking one statement AST at a time.
 * This module is the first vertical slice toward a real allocator: before an
 * AST is emitted, lower it into a persistent per-function stream of unlimited
 * virtual values, then build CFG successors and solve virtual-value liveness.
 *
 * Set DCC_MIR_REPORT=1 to dump every generated function attempt, or
 * DCC_MIR_FUNCTION=name to restrict the dump. MIR emission remains opt-in and
 * transactional: unsupported functions replay the established backend body.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"

#define MIR_AGGREGATE_FORWARD_OFFSET (-32768L)
#define MIR_AGGREGATE_VALUE_DEST_OFFSET (-32767L)
#define MIR_AGGREGATE_GLOBAL_DEST_OFFSET (-32766L)
#define MIR_MAX_ROLLOUT_INSNS 4096

enum MirOpcode {
    MIR_NOP,
    MIR_PARAM,
    MIR_CONST,
    MIR_FLOAT_CONST,
    MIR_STRING_ADDRESS,
    MIR_ADDRESS,
    MIR_COMPOUND_ADDRESS,
    MIR_INDEX_ADDRESS,
    MIR_MEMBER_ADDRESS,
    MIR_VLA_SIZE,
    MIR_LOAD,
    MIR_LOAD_INDIRECT,
    MIR_INDEX_LOAD,
    MIR_STORE,
    MIR_STORE_INDIRECT,
    MIR_COPY_AGGREGATE,
    MIR_VLA_SAVE,
    MIR_VLA_ALLOC,
    MIR_VLA_RESTORE,
    MIR_UNARY,
    MIR_BINARY,
    MIR_ARG,
    MIR_CALL,
    MIR_CALL_AGGREGATE,
    MIR_VA_START,
    MIR_VA_END,
    MIR_VA_ARG,
    MIR_LABEL,
    MIR_JUMP,
    MIR_BRANCH_FALSE,
    MIR_DECL_PLACEHOLDER,
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
    int memory_size;
    int memory_flags;
    int bit_width;
    int bit_shift;
    unsigned int bit_mask;
    int secondary_offset;
    char name[64];
    char base_name[64];
};

struct MirObject {
    char name[64];
    int storage;
    int type;
    int offset;
    int entry_value;
};

struct MirSwitchContext {
    long values[MAX_SWITCH_CASES];
    int labels[MAX_SWITCH_CASES];
    int count;
    int default_label;
    int end_label;
};

struct MirFunction {
    struct MirInsn *insns;
    int count;
    int capacity;
    int next_value;
    int next_label;
    int next_call_id;
    int active;
    int sink_purpose;
    int break_labels[MAX_FLOW];
    int continue_labels[MAX_FLOW];
    int flow_depth;
    int has_vla;
    int has_runtime_stride_param;
    int is_variadic_function;
    char user_label_names[256][64];
    int user_label_ids[256];
    int user_label_count;
    struct MirSwitchContext switches[MAX_FLOW];
    int switch_depth;
    int declaration_placeholders[1024];
    int declaration_scope_ends[1024];
    const struct AstNode *declaration_nodes[1024];
    unsigned char declaration_consumed[1024];
    int declaration_count;
    int declaration_cursor;
    int declaration_capture_start;
    int declaration_placeholder;
    int declaration_active_index;
    int declaration_active;
    int compound_capture_starts[MAX_FLOW];
    int compound_depth;
    int scope_points[1024];
    int scope_count;
    int scope_cursor;
    int scope_replay_points[MAX_FLOW];
    int scope_replay_depth;
    int flow_points[1024];
    int flow_count;
    int flow_cursor;
    int flow_replay_point;
    int flow_replay_active;
    char label_replay_name[64];
    int label_replay_active;
    int emit_mode;
    int report_mode;
    int return_type;
    int local_bytes;
    int aggregate_temp_bytes;
    int opaque_count;
    int *allocation_colors;
    int *allocation_spills;
    int allocation_capacity;
    int allocation_spill_count;
    int *backend_slots;
    int backend_slot_capacity;
    int backend_slot_count;
    FILE *capture_stream;
    EmitSink saved_sink;
    struct MirObject objects[256];
    int object_count;
    char declared_names[MAX_LOCALS][64];
    int declared_types[MAX_LOCALS];
    int declared_storage[MAX_LOCALS];
    int declared_offsets[MAX_LOCALS];
    int declared_sizes[MAX_LOCALS];
    int declared_dim_counts[MAX_LOCALS];
    int declared_dims[MAX_LOCALS][MAX_ARRAY_DIMS];
    char declared_link_names[MAX_LOCALS][64];
    int declared_elem_sizes[MAX_LOCALS];
    int declared_vla_size_offsets[MAX_LOCALS];
    int declared_is_vla[MAX_LOCALS];
    int declared_is_array[MAX_LOCALS];
    int declared_dynamic_strides[MAX_LOCALS];
    char declared_runtime_stride_names[MAX_LOCALS][64];
    int declared_is_const[MAX_LOCALS];
    unsigned long declared_const_values[MAX_LOCALS];
    int declared_is_funcptr[MAX_LOCALS];
    int declared_has_proto[MAX_LOCALS];
    int declared_proto_nargs[MAX_LOCALS];
    int declared_proto_types[MAX_LOCALS][MAX_PROTO_PARAMS];
    int declared_count;
    char alias_source_names[MAX_LOCALS][64];
    char alias_internal_names[MAX_LOCALS][64];
    int alias_declaration_indices[MAX_LOCALS];
    int alias_count;
    struct Sym *initializer_target;
    int initializer_capture_start;
    struct Sym *init_expression_target;
    int init_expression_offset;
    int init_expression_type;
    struct Sym *vla_target;
    int vla_capture_start;
    char name[64];
};

static struct MirFunction mir;
static int mir_virtual_iy_base;
static int mir_virtual_iy_frame_bytes;
static int mir_emit_instruction_index = -1;
static int mir_forwarded_hl_value = -1;
static int mir_forwarded_hl_instruction = -1;
static int mir_forwarded_stack_value = -1;
static int mir_forwarded_stack_instruction = -1;
static int mir_cached_call_value = -1;
static int mir_cached_call_instruction = -1;
static int mir_cached_wide_call_value = -1;
static int mir_cached_wide_call_instruction = -1;

static int mir_inline_substitutable(const struct Sym *symbol)
{
    return symbol != NULL && symbol->is_static && symbol->is_inline &&
           (symbol->inline_return_expr != NULL ||
            symbol->inline_stmt_expr != NULL ||
            symbol->inline_stmt_body != NULL);
}

static const char *mir_opcode_name(int opcode)
{
    switch (opcode) {
    case MIR_NOP: return "nop";
    case MIR_PARAM: return "param";
    case MIR_CONST: return "const";
    case MIR_FLOAT_CONST: return "fconst";
    case MIR_STRING_ADDRESS: return "straddr";
    case MIR_ADDRESS: return "address";
    case MIR_COMPOUND_ADDRESS: return "clitaddr";
    case MIR_INDEX_ADDRESS: return "indexaddr";
    case MIR_MEMBER_ADDRESS: return "memberaddr";
    case MIR_VLA_SIZE: return "vlasize";
    case MIR_LOAD: return "load";
    case MIR_LOAD_INDIRECT: return "loadind";
    case MIR_INDEX_LOAD: return "index";
    case MIR_STORE: return "store";
    case MIR_STORE_INDIRECT: return "storeind";
    case MIR_COPY_AGGREGATE: return "copyagg";
    case MIR_VLA_SAVE: return "vlasave";
    case MIR_VLA_ALLOC: return "vlaalloc";
    case MIR_VLA_RESTORE: return "vlarestore";
    case MIR_UNARY: return "unary";
    case MIR_BINARY: return "binary";
    case MIR_ARG: return "arg";
    case MIR_CALL: return "call";
    case MIR_CALL_AGGREGATE: return "callagg";
    case MIR_VA_START: return "vastart";
    case MIR_VA_END: return "vaend";
    case MIR_VA_ARG: return "vaarg";
    case MIR_LABEL: return "label";
    case MIR_JUMP: return "jump";
    case MIR_BRANCH_FALSE: return "brfalse";
    case MIR_DECL_PLACEHOLDER: return "decl";
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

static int mir_user_label(const char *name)
{
    int i;

    if (name == NULL)
        return -1;
    for (i = 0; i < mir.user_label_count; ++i)
        if (strcmp(mir.user_label_names[i], name) == 0)
            return mir.user_label_ids[i];
    if (mir.user_label_count >= 256)
        return -1;
    i = mir.user_label_count++;
    mir_copy_name(mir.user_label_names[i], name);
    mir.user_label_ids[i] = mir_new_label();
    return mir.user_label_ids[i];
}

static int mir_object_eligible(const struct Sym *sym)
{
    const char *separator;
    if (sym == NULL)
        return 0;
    if (sym->storage != SC_LOCAL && sym->storage != SC_PARAM)
        return 0;
    if (strncmp(sym->name, "#clit", 5) == 0)
        return 0;
    if (sym->is_volatile || sym->is_array || sym->is_vla ||
        sym->is_const_value)
        return 0;
    if (type_is_struct_object(sym->type))
        return 0;
    if (type_ptr_depth(sym->type) > 0)
        return 0;
    if (type_size(sym->type) < 1 || type_size(sym->type) > 2)
        return 0;
    if (local_name_address_taken_in_function(sym->name))
        return 0;
    separator = strchr(sym->name, '#');
    if (separator != NULL) {
        char source_name[64];
        size_t length = (size_t)(separator - sym->name);
        if (length >= sizeof(source_name))
            length = sizeof(source_name) - 1;
        memcpy(source_name, sym->name, length);
        source_name[length] = 0;
        if (local_name_address_taken_in_function(source_name))
            return 0;
    }
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
static void mir_lower_stmt(const struct AstNode *node);
static const struct MirInsn *mir_definition(int value);
static struct MirInsn *mir_mutable_definition(int value);
static int mir_scalar_memory_location(const struct MirInsn *insn, int *type,
                                      int *storage, int *offset);
static int mir_load_is_single_call_argument(int value, int size);
static int mir_object_is_fully_promoted(int object);
static int mir_value_is_wide(int value);
static int mir_find_label(int label);
static int mir_lvalue_type(const struct AstNode *node);
static int mir_value_use_count(int value);
static int mir_fold_constant_binary(int op, long left, long right,
                                    int operand_type, long *result);
static int mir_fold_constant_compare(int op, long left, long right,
                                     int operand_type, long *result);

static const char *mir_ident_name(const struct AstNode *node)
{
    if (node == NULL || node->sval == NULL)
        return node != NULL && node->sym != NULL ? node->sym->name : "?";
    return resolve_local_rename(node->sval);
}

static struct Sym *mir_ident_symbol(const struct AstNode *node)
{
    const char *name;
    struct Sym *symbol;

    if (node == NULL)
        return NULL;
    name = mir_ident_name(node);
    symbol = find_sym(name);
    if (symbol != NULL)
        return symbol;
    if (node->sym != NULL && strcmp(node->sym->name, name) == 0)
        return node->sym;
    return NULL;
}

static int mir_emit_pointer_word_load(int address, int result_type)
{
    struct MirInsn *insn;
    int value;

    if (address < 0)
        return -1;
    value = mir_new_value();
    insn = mir_emit(MIR_LOAD_INDIRECT);
    insn->dst = value;
    insn->src1 = address;
    insn->type = result_type;
    insn->memory_size = 2;
    insn->memory_flags |= 256;
    return value;
}

static int mir_lower_conversion(int value, int target_type)
{
    struct MirInsn *definition;
    struct MirInsn *conversion;
    int result;

    if (value < 0 || target_type == 0)
        return value;
    definition = mir_mutable_definition(value);
    if (definition == NULL || definition->type == 0 ||
        definition->type == target_type)
        return value;
    result = mir_new_value();
    conversion = mir_emit(MIR_UNARY);
    conversion->dst = result;
    conversion->src1 = value;
    conversion->type = target_type;
    conversion->immediate = 0;
    return result;
}

static int mir_lower_ident_assignment_conversion(
    int value, const struct AstNode *ident)
{
    struct MirInsn *conversion;
    int result;

    if (value < 0 || ident == NULL || ident->kind != AST_IDENT)
        return value;
    result = mir_new_value();
    conversion = mir_emit(MIR_UNARY);
    conversion->dst = result;
    conversion->src1 = value;
    conversion->type = mir_lvalue_type(ident);
    conversion->immediate = 0;
    conversion->memory_flags |= 512;
    mir_copy_name(conversion->name, mir_ident_name(ident));
    return result;
}

static void mir_set_field_memory(struct MirInsn *insn,
                                 const struct FieldDef *field);

static int mir_reload_bitfield(int address, const struct FieldDef *field,
                               int type)
{
    struct MirInsn *insn;
    int value;

    if (field == NULL || field->bit_width <= 0)
        return -1;
    value = mir_new_value();
    insn = mir_emit(MIR_LOAD_INDIRECT);
    insn->dst = value;
    insn->src1 = address;
    insn->type = type;
    mir_set_field_memory(insn, field);
    return value;
}

static int mir_lower_aggregate_call_address(const struct AstNode *call,
                                            const struct Sym *temporary)
{
    struct MirInsn *insn;
    int argument;
    int call_id;
    int value;
    int i;
    struct Sym *function_symbol;

    if (call == NULL || call->kind != AST_CALL)
        return -1;
    function_symbol = call->a != NULL && call->a->kind == AST_IDENT
        ? (call->a->sym != NULL && call->a->sym->storage == SC_FUNC
            ? call->a->sym : find_global(call->a->sval))
        : NULL;
    if (function_symbol != NULL && !mir_inline_substitutable(function_symbol))
        function_symbol->deferred_body_needed = 1;
    call_id = mir.next_call_id++;
    for (i = 0; i < call->list_len; ++i) {
        int argument_type = ast_expr_type_for_sizeof(call->list[i]);
        struct Sym nested_temporary;
        if (function_symbol != NULL && i < function_symbol->proto_nargs) {
            argument_type = function_symbol->proto_types[i];
        }
        if (type_is_struct_object(argument_type) &&
            call->list[i]->kind == AST_CALL) {
            const struct Sym *temporary = call->list[i]->sym;
            if (temporary == NULL) {
                int size = type_size(argument_type);
                memset(&nested_temporary, 0, sizeof(nested_temporary));
                if ((size & 1) != 0)
                    ++size;
                mir.aggregate_temp_bytes += size;
                nested_temporary.type = argument_type;
                nested_temporary.offset = -mir.local_bytes -
                                          mir.aggregate_temp_bytes;
                strcpy(nested_temporary.name, "#miragg");
                temporary = &nested_temporary;
            }
            argument = mir_lower_aggregate_call_address(call->list[i],
                                                        temporary);
        } else
            argument = mir_lower_expr(call->list[i]);
        if (!type_is_struct_object(argument_type))
            argument = mir_lower_conversion(argument, argument_type);
        insn = mir_emit(MIR_ARG);
        insn->src1 = argument;
        insn->type = argument_type;
        insn->immediate = i;
        insn->secondary_offset = call_id;
    }
    value = mir_new_value();
    insn = mir_emit(MIR_CALL_AGGREGATE);
    insn->dst = value;
    insn->type = type_add_ptr(function_symbol != NULL
                              ? function_symbol->type : call->type);
    insn->immediate = temporary != NULL ? temporary->offset
                                        : MIR_AGGREGATE_FORWARD_OFFSET;
    insn->memory_size = type_size(function_symbol != NULL
                                  ? function_symbol->type : call->type);
    insn->secondary_offset = call_id;
    if (temporary != NULL)
        mir_copy_name(insn->base_name, temporary->name);
    if (call->a != NULL && call->a->kind == AST_IDENT)
        mir_copy_name(insn->name, call->a->sval);
    else
        mir_copy_name(insn->name, "<indirect>");
    return value;
}

static void mir_collect_switch_labels(const struct AstNode *node,
                                      struct MirSwitchContext *context)
{
    int i;

    if (node == NULL || node->kind == AST_SWITCH)
        return;
    switch (node->kind) {
    case AST_CASE:
        if (context->count < MAX_SWITCH_CASES) {
            context->values[context->count] = node->ival;
            context->labels[context->count] = mir_new_label();
            ++context->count;
        }
        mir_collect_switch_labels(node->b, context);
        return;
    case AST_DEFAULT:
        if (context->default_label < 0)
            context->default_label = mir_new_label();
        mir_collect_switch_labels(node->b, context);
        return;
    case AST_COMPOUND:
        for (i = 0; i < node->list_len; ++i)
            mir_collect_switch_labels(node->list[i], context);
        return;
    case AST_IF:
        mir_collect_switch_labels(node->b, context);
        mir_collect_switch_labels(node->c, context);
        return;
    case AST_WHILE:
    case AST_DOWHILE:
    case AST_LABEL:
        mir_collect_switch_labels(node->b, context);
        return;
    case AST_FOR:
        mir_collect_switch_labels(node->d, context);
        return;
    default:
        return;
    }
}

static int mir_switch_case_label(const struct MirSwitchContext *context,
                                 long value)
{
    int i;
    for (i = 0; i < context->count; ++i)
        if ((context->values[i] & 0xffffL) == (value & 0xffffL))
            return context->labels[i];
    return -1;
}

static struct FieldDef *mir_member_field(const struct AstNode *node)
{
    int base_type;
    int struct_id;

    if (node == NULL || node->kind != AST_MEMBER || node->a == NULL ||
        node->sval == NULL)
        return NULL;
    base_type = ast_expr_type_for_sizeof(node->a);
    if (node->op == TOK_ARROW)
        base_type = type_decay_ptr(base_type);
    struct_id = base_struct_id_from_type(base_type);
    {
        struct FieldDef *field = find_field_def(struct_id, node->sval);
        return field != NULL ? field : ast_unique_field_by_name(node->sval);
    }
}

static void mir_set_field_memory(struct MirInsn *insn,
                                 const struct FieldDef *field)
{
    insn->memory_size = field->size > 0 ? field->size : type_size(field->type);
    insn->memory_flags = field->is_volatile ? 1 : 0;
    if (field->is_array)
        insn->memory_flags |= 2;
    if (type_is_struct_object(field->type))
        insn->memory_flags |= 4;
    insn->bit_width = field->bit_width;
    insn->bit_shift = field->bit_shift;
    insn->bit_mask = field->bit_mask;
}

static struct Sym *mir_index_root(const struct AstNode *node, int *depth)
{
    const struct AstNode *root = node;
    struct Sym *symbol;
    int count = 0;

    while (root != NULL && root->kind == AST_INDEX) {
        ++count;
        root = root->a;
    }
    if (depth != NULL)
        *depth = count;
    if (root == NULL || root->kind != AST_IDENT)
        return NULL;
    symbol = mir_ident_symbol(root);
    return symbol;
}

static struct Sym *mir_pointer_array_root(const struct AstNode *node,
                                          int *dereference_depth);

static int mir_index_stride(const struct AstNode *node)
{
    struct Sym *root;
    const struct AstNode *base = node;
    struct FieldDef *field = NULL;
    int depth;
    int dereference_depth = 0;
    int stride;
    int dimension;

    root = mir_index_root(node, &depth);
    while (base != NULL && base->kind == AST_INDEX)
        base = base->a;
    if (base != NULL && base->kind == AST_MEMBER)
        field = mir_member_field(base);
    if (root != NULL && root->is_array)
        stride = sym_array_index_elem_size(root, depth - 1);
    else if (root != NULL)
        stride = sym_pointer_array_index_elem_size(root, root->type, depth - 1);
    else if (field != NULL && field->is_array) {
        stride = field->elem_size > 0 ? field->elem_size
                                      : type_size(field->elem_type);
        if (stride <= 0)
            stride = 1;
        for (dimension = depth; dimension < field->dim_count; ++dimension)
            if (field->dims[dimension] > 0)
                stride *= field->dims[dimension];
    }
    else {
        struct Sym *pointer_array = mir_pointer_array_root(
            node != NULL ? node->a : NULL, &dereference_depth);
        if (pointer_array != NULL)
            stride = sym_pointer_array_index_elem_size(
                pointer_array, pointer_array->type, dereference_depth);
        else
        stride = type_index_elem_size(ast_expr_type_for_sizeof(node->a));
    }
    return stride > 0 ? stride : 1;
}

static struct Sym *mir_pointer_array_root(const struct AstNode *node,
                                          int *dereference_depth)
{
    struct Sym *symbol;
    if (node == NULL)
        return NULL;
    if (node->kind == AST_IDENT) {
        symbol = node->sym != NULL ? node->sym : find_sym(node->sval);
        if (symbol != NULL &&
            ((symbol->is_array && symbol->dim_count > 1) ||
             (!symbol->is_array && type_ptr_depth(symbol->type) > 0 &&
              symbol->dim_count > 0)))
            return symbol;
        return NULL;
    }
    if (node->kind == AST_UNARY && node->op == '*') {
        symbol = mir_pointer_array_root(node->a, dereference_depth);
        if (symbol != NULL)
            ++*dereference_depth;
        return symbol;
    }
    if (node->kind == AST_BINARY &&
        (node->op == '+' || node->op == '-')) {
        symbol = mir_pointer_array_root(node->a, dereference_depth);
        if (symbol == NULL)
            symbol = mir_pointer_array_root(node->b, dereference_depth);
        return symbol;
    }
    return NULL;
}

static int mir_pointer_arithmetic_stride(const struct AstNode *node)
{
    int dereference_depth = 0;
    struct Sym *pointer_array = mir_pointer_array_root(
        node, &dereference_depth);
    if (pointer_array != NULL) {
        int stride;
        if (pointer_array->is_array && dereference_depth == 0)
            stride = pointer_array->elem_size;
        else
            stride = dereference_depth >= pointer_array->dim_count
                ? type_size(type_decay_ptr(pointer_array->type))
                : sym_pointer_array_index_elem_size(
                    pointer_array, pointer_array->type, dereference_depth);
        if (stride <= 0)
            stride = type_size(type_decay_ptr(pointer_array->type));
        if (stride > 0)
            return stride;
    }
    if (node != NULL && node->kind == AST_MEMBER) {
        struct FieldDef *field = mir_member_field(node);
        if (field != NULL && field->is_array && field->elem_size > 0)
            return field->elem_size;
    }
    if (node != NULL && node->kind == AST_IDENT) {
        struct Sym *symbol = node->sym != NULL ? node->sym : find_sym(node->sval);
        if (symbol != NULL && symbol->is_array && symbol->elem_size > 0)
            return symbol->elem_size;
    }
    return type_index_elem_size(ast_expr_type_for_sizeof(node));
}

static int mir_index_result_is_array(const struct AstNode *node)
{
    struct Sym *root;
    struct Sym *pointer_array;
    const struct AstNode *base = node;
    struct FieldDef *field = NULL;
    int depth;
    int dereference_depth = 0;

    root = mir_index_root(node, &depth);
    while (base != NULL && base->kind == AST_INDEX)
        base = base->a;
    if (base != NULL && base->kind == AST_MEMBER) {
        field = mir_member_field(base);
    }
    pointer_array = root == NULL
        ? mir_pointer_array_root(node != NULL ? node->a : NULL,
                                 &dereference_depth)
        : NULL;
    return (root != NULL &&
                        (root->is_array ? root->dim_count > depth
                                                        : type_ptr_depth(root->type) > 0 &&
                                                            root->dim_count >= depth)) ||
           (pointer_array != NULL &&
            pointer_array->dim_count > dereference_depth) ||
           (field != NULL && field->dim_count > depth);
}

static void mir_set_node_memory(struct MirInsn *insn,
                                const struct AstNode *node)
{
    int size = node != NULL ? type_size(node->type) : 0;
    insn->memory_size = size > 0 ? size : 1;
    if (node != NULL && type_is_struct_object(node->type))
        insn->memory_flags |= 4;
}

static int mir_lower_lvalue_address(const struct AstNode *node)
{
    struct MirInsn *insn;
    struct FieldDef *field;
    struct Sym *symbol;
    int base;
    int value;

    if (node == NULL)
        return -1;
    if (node->kind == AST_IDENT) {
        const char *name = mir_ident_name(node);
        symbol = mir_ident_symbol(node);
        value = mir_new_value();
        insn = mir_emit(MIR_ADDRESS);
        insn->dst = value;
        insn->type = type_add_ptr(node->type);
        mir_copy_name(insn->name, name);
        insn->object = mir_get_object(symbol, insn->name);
        return value;
    }
    if (node->kind == AST_COMPOUND_LITERAL && node->sym != NULL) {
        value = mir_new_value();
        insn = mir_emit(MIR_COMPOUND_ADDRESS);
        insn->dst = value;
        insn->type = type_add_ptr(node->type);
        insn->immediate = node->sym->offset;
        insn->memory_size = type_size(node->type);
        insn->memory_flags = 8;
        mir_copy_name(insn->name, node->sym->name);
        return value;
    }
    if (node->kind == AST_UNARY && node->op == '*' && node->a != NULL &&
        node->a->kind == AST_UNARY && node->a->op == '*')
        return mir_emit_pointer_word_load(
            mir_lower_lvalue_address(node->a), type_add_ptr(node->a->type));
    if (node->kind == AST_UNARY && node->op == '*')
        return mir_lower_expr(node->a);
    if (node->kind == AST_INDEX) {
        int index;
        int element_type = node->type;
        int element_type_resolved;
        if (node->a != NULL && node->a->kind == AST_INT_LIT &&
            node->b != NULL) {
            const struct MirInsn *base_definition;
            base = mir_lower_expr(node->b);
            index = mir_lower_expr(node->a);
            if (base < 0 || index < 0)
                return -1;
            value = mir_new_value();
            insn = mir_emit(MIR_INDEX_ADDRESS);
            insn->dst = value;
            insn->src1 = base;
            insn->src2 = index;
            insn->type = type_add_ptr(element_type);
            base_definition = mir_definition(base);
            insn->immediate = base_definition != NULL &&
                              type_ptr_depth(base_definition->type) > 0
                ? type_index_elem_size(base_definition->type)
                : type_size(element_type);
            if (insn->immediate <= 0)
                insn->immediate = 1;
            mir_set_node_memory(insn, node);
            insn->memory_size = type_size(element_type);
            return value;
        }
        struct Sym *array_symbol = node->a != NULL &&
                       node->a->kind == AST_IDENT
            ? (node->a->sym != NULL ? node->a->sym
                        : find_sym(node->a->sval))
            : NULL;
        if (node->a != NULL && node->a->kind == AST_UNARY &&
            node->a->op == '*') {
            int pointer_type;
            int no_deref;
            int dereference_depth = 0;
            struct Sym *pointer_array = mir_pointer_array_root(
                node->a, &dereference_depth);
            if ((ast_pointer_expr_type(node->a, &pointer_type, &no_deref) &&
                 no_deref) ||
                (pointer_array != NULL &&
                 pointer_array->dim_count >= dereference_depth))
                base = mir_lower_expr(node->a);
            else
                base = mir_emit_pointer_word_load(
                    mir_lower_lvalue_address(node->a),
                    node->a->type);
        } else
            base = mir_lower_expr(node->a);
        index = mir_lower_expr(node->b);
        if (base < 0 || index < 0)
            return -1;
        value = mir_new_value();
        insn = mir_emit(MIR_INDEX_ADDRESS);
        insn->dst = value;
        insn->src1 = base;
        insn->src2 = index;
        element_type_resolved = 0;
        if (array_symbol != NULL && array_symbol->is_array) {
            element_type = array_symbol->type;
            element_type_resolved = 1;
        } else
            element_type_resolved = ast_index_composite_elem_type(
                node, &element_type);
        if (!element_type_resolved) {
            const struct MirInsn *base_definition = mir_mutable_definition(base);
            if (base_definition != NULL &&
                type_ptr_depth(base_definition->type) > 0)
                element_type = type_decay_ptr(base_definition->type);
        }
        insn->type = type_add_ptr(element_type);
        insn->immediate = mir_index_stride(node);
        if (array_symbol != NULL &&
            array_symbol->runtime_stride_name[0] != 0) {
            mir_copy_name(insn->base_name,
                          array_symbol->runtime_stride_name);
            insn->secondary_offset = type_size(
                type_decay_ptr(array_symbol->type));
        }
        mir_set_node_memory(insn, node);
        insn->memory_size = type_size(element_type);
        return value;
    }
    if (node->kind != AST_MEMBER)
        return -1;
    if (node->op == '.' && node->a != NULL && node->a->kind == AST_CALL &&
        node->sym != NULL)
        base = mir_lower_aggregate_call_address(node->a, node->sym);
    else if (node->op == TOK_ARROW && node->a != NULL &&
             node->a->kind == AST_UNARY && node->a->op == '*') {
        int pointer_address = mir_lower_lvalue_address(node->a);
        base = mir_emit_pointer_word_load(pointer_address,
                                          type_add_ptr(node->a->type));
    }
    else
        base = node->op == TOK_ARROW ? mir_lower_expr(node->a)
                                     : mir_lower_lvalue_address(node->a);
    if (base < 0)
        return -1;
    field = mir_member_field(node);
    value = mir_new_value();
    insn = mir_emit(MIR_MEMBER_ADDRESS);
    insn->dst = value;
    insn->src1 = base;
    insn->type = type_add_ptr(field != NULL ? field->type : node->type);
    insn->immediate = field != NULL ? field->offset : 0;
    mir_copy_name(insn->name, node->sval);
    if (node->a != NULL && node->a->kind == AST_IDENT)
        mir_copy_name(insn->base_name, node->a->sval);
    if (node->op == TOK_ARROW)
        insn->memory_flags |= 16;
    if (field != NULL)
        mir_set_field_memory(insn, field);
    else
        insn->memory_flags |= 8;
    return value;
}

static void mir_emit_ident_store(const struct AstNode *ident, int value)
{
    struct MirInsn *store;
    struct Sym *symbol;

    if (ident == NULL || ident->kind != AST_IDENT)
        return;
    symbol = mir_ident_symbol(ident);
    store = mir_emit(MIR_STORE);
    store->src1 = value;
    store->type = ident->type;
    mir_copy_name(store->name, mir_ident_name(ident));
    store->object = mir_get_object(symbol, store->name);
}

static int mir_lvalue_type(const struct AstNode *node)
{
    int type;
    if (node == NULL)
        return 0;
    if (node->kind == AST_IDENT) {
        struct Sym *symbol = mir_ident_symbol(node);
        return symbol != NULL ? symbol->type : node->type;
    }
    if (node->kind == AST_MEMBER) {
        struct FieldDef *field = mir_member_field(node);
        return field != NULL ? field->type : node->type;
    }
    if (node->kind == AST_INDEX) {
        struct Sym *array_symbol = node->a != NULL &&
                                   node->a->kind == AST_IDENT
            ? (node->a->sym != NULL ? node->a->sym
                                    : find_sym(node->a->sval))
            : NULL;
        type = node->type;
        if (!ast_index_composite_elem_type(node, &type) &&
            array_symbol != NULL && array_symbol->is_array)
            type = array_symbol->type;
        return type;
    }
    return node->type != 0 ? node->type : ast_expr_type_for_sizeof(node);
}

static int mir_lower_compound_value(int left, int right, int operation,
                                    int target_type)
{
    struct MirInsn *insn;
    const struct MirInsn *right_definition;
    int computation_type = target_type;
    int value;

    if (type_ptr_depth(target_type) > 0 &&
        (operation == '+' || operation == '-')) {
        int stride = type_index_elem_size(target_type);
        if (stride > 1) {
            int scale = mir_new_value();
            int scaled = mir_new_value();
            insn = mir_emit(MIR_CONST);
            insn->dst = scale;
            insn->type = TYPE_INT;
            insn->immediate = stride;
            insn = mir_emit(MIR_BINARY);
            insn->dst = scaled;
            insn->src1 = right;
            insn->src2 = scale;
            insn->type = TYPE_INT;
            insn->secondary_offset = TYPE_INT;
            insn->immediate = '*';
            right = scaled;
        }
    } else if (operation != TOK_SHL && operation != TOK_SHR) {
        right_definition = mir_mutable_definition(right);
        computation_type = common_arith_type(
            target_type, right_definition != NULL
                ? right_definition->type : target_type);
        left = mir_lower_conversion(left, computation_type);
        right = mir_lower_conversion(right, computation_type);
    }
    value = mir_new_value();
    insn = mir_emit(MIR_BINARY);
    insn->dst = value;
    insn->src1 = left;
    insn->src2 = right;
    insn->type = computation_type;
    insn->secondary_offset = computation_type;
    insn->immediate = operation;
    return mir_lower_conversion(value, target_type);
}

static int mir_lower_incdec(const struct AstNode *operand, int operation,
                            int postfix)
{
    struct MirInsn *insn;
    struct FieldDef *field = NULL;
    int address = -1;
    int old_value;
    int one;
    int new_value;
    int operand_type;
    long step = 1;

    if (operand == NULL)
        return -1;
    operand_type = operand->type;
    if (operand->kind == AST_IDENT) {
        struct Sym *symbol = operand->sym != NULL
            ? operand->sym : find_sym(operand->sval);
        if (symbol != NULL)
            operand_type = symbol->type;
    } else if (operand->kind == AST_MEMBER) {
        field = mir_member_field(operand);
        if (field != NULL)
            operand_type = field->type;
    }
    if (type_ptr_depth(operand_type) > 0)
        step = type_index_elem_size(operand_type);
    if (operand->kind == AST_IDENT) {
        old_value = mir_lower_expr(operand);
    } else {
        address = mir_lower_lvalue_address(operand);
        if (address < 0)
            return -1;
        old_value = mir_new_value();
        insn = mir_emit(MIR_LOAD_INDIRECT);
        insn->dst = old_value;
        insn->src1 = address;
        insn->type = operand_type;
        if (field != NULL)
            mir_set_field_memory(insn, field);
        else if (operand->kind == AST_MEMBER)
            insn->memory_flags |= 8;
        else
            mir_set_node_memory(insn, operand);
    }
    one = mir_new_value();
    insn = mir_emit(MIR_CONST);
    insn->dst = one;
    insn->type = operand_type;
    insn->immediate = step;
    new_value = mir_new_value();
    insn = mir_emit(MIR_BINARY);
    insn->dst = new_value;
    insn->src1 = old_value;
    insn->src2 = one;
    insn->type = operand_type;
    insn->secondary_offset = operand_type;
    insn->immediate = operation == TOK_INC ? '+' : '-';
    if (operand->kind == AST_IDENT) {
        mir_emit_ident_store(operand, new_value);
    } else {
        insn = mir_emit(MIR_STORE_INDIRECT);
        insn->src1 = address;
        insn->src2 = new_value;
        insn->type = operand_type;
        if (field != NULL)
            mir_set_field_memory(insn, field);
        else if (operand->kind == AST_MEMBER)
            insn->memory_flags |= 8;
        else
            mir_set_node_memory(insn, operand);
    }
    if (!postfix && field != NULL && field->bit_width > 0)
        new_value = mir_reload_bitfield(address, field, operand_type);
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
    int rhs_label;
    int rhs_end_label;
    int then_exit_label;
    int else_exit_label;
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
    case AST_FLOAT_LIT:
        value = mir_new_value();
        insn = mir_emit(MIR_FLOAT_CONST);
        insn->dst = value;
        insn->type = node->type;
        insn->immediate = (long)node->uval;
        return value;
    case AST_STR_LIT:
        value = mir_new_value();
        insn = mir_emit(MIR_STRING_ADDRESS);
        insn->dst = value;
        insn->type = node->type;
        insn->immediate = node->str_index >= 0
            ? node->str_index
            : add_string_ex(node->sval, (int)node->uval, (int)node->ival);
        return value;
    case AST_SIZEOF_TYPE:
        value = mir_new_value();
        insn = mir_emit(MIR_CONST);
        insn->dst = value;
        insn->type = node->type;
        insn->immediate = node->ival;
        return value;
    case AST_SIZEOF_EXPR:
        {
            struct Sym *vla = ast_sizeof_whole_vla_sym(node->a);
            value = mir_new_value();
            if (vla != NULL && vla->vla_size_offset != 0) {
                insn = mir_emit(MIR_VLA_SIZE);
                insn->immediate = vla->vla_size_offset;
                mir_copy_name(insn->name, vla->name);
            } else if (node->a != NULL && node->a->kind == AST_IDENT) {
                insn = mir_emit(MIR_VLA_SIZE);
                insn->memory_flags |= 8;
                insn->secondary_offset = ast_sizeof_expr_value(node->a);
                mir_copy_name(insn->name, node->a->sval);
            } else if (node->a != NULL && node->a->kind == AST_INDEX) {
                int depth = 0;
                struct Sym *root = mir_index_root(node->a, &depth);
                const struct AstNode *base = node->a;
                while (base != NULL && base->kind == AST_INDEX)
                    base = base->a;
                insn = mir_emit(MIR_VLA_SIZE);
                insn->memory_flags |= 8;
                insn->secondary_offset = ast_sizeof_expr_value(node->a);
                insn->bit_width = depth;
                if (root != NULL)
                    mir_copy_name(insn->name, root->name);
                else if (base != NULL && base->kind == AST_IDENT)
                    mir_copy_name(insn->name, mir_ident_name(base));
            } else {
                insn = mir_emit(MIR_CONST);
                insn->immediate = ast_sizeof_expr_value(node->a);
            }
            insn->dst = value;
            insn->type = node->type;
            return value;
        }
    case AST_IDENT:
        {
            const char *name = mir_ident_name(node);
            struct Sym *symbol = mir_ident_symbol(node);
            int enum_index;
        if (!strcmp(name, "stdin") || !strcmp(name, "stdout") ||
            !strcmp(name, "stderr")) {
            value = mir_new_value();
            insn = mir_emit(MIR_CONST);
            insn->dst = value;
            insn->type = TYPE_INT;
            insn->immediate = !strcmp(name, "stdin") ? 0
                : !strcmp(name, "stdout") ? 1 : 2;
            return value;
        }
        enum_index = node->sval != NULL ? find_enum_const(name) : -1;
        if (symbol == NULL && enum_index >= 0) {
            value = mir_new_value();
            insn = mir_emit(MIR_CONST);
            insn->dst = value;
            insn->type = TYPE_INT;
            insn->immediate = enum_const_values[enum_index];
            return value;
        }
        value = mir_new_value();
        insn = mir_emit(symbol != NULL &&
                ((symbol->is_array && !symbol->is_vla) ||
             type_is_struct_object(symbol->type) ||
             symbol->storage == SC_FUNC)
                ? MIR_ADDRESS : MIR_LOAD);
        insn->dst = value;
        insn->type = insn->opcode == MIR_ADDRESS
            ? type_add_ptr(symbol != NULL ? symbol->type : node->type)
            : symbol != NULL && symbol->is_vla
            ? type_add_ptr(symbol->type)
            : symbol != NULL ? symbol->type : node->type;
        mir_copy_name(insn->name, name);
        insn->object = mir_get_object(symbol, insn->name);
        return value;
        }
    case AST_INDEX:
        {
        int element_type = node->type;
        int row_pointer_type;
        struct Sym *array_symbol = node->a != NULL &&
                       node->a->kind == AST_IDENT
            ? (node->a->sym != NULL ? node->a->sym
                        : find_sym(node->a->sval))
            : NULL;
        left = mir_lower_lvalue_address(node);
        if (left < 0)
            break;
        if (ast_index_array_row_ptr_type(node, &row_pointer_type)) {
            struct MirInsn *address_definition = mir_mutable_definition(left);
            if (address_definition != NULL)
                address_definition->type = row_pointer_type;
            return left;
        }
        if (array_symbol != NULL && array_symbol->is_array)
            element_type = array_symbol->type;
        else if (!ast_index_composite_elem_type(node, &element_type)) {
            const struct MirInsn *address_definition =
                mir_mutable_definition(left);
            if (address_definition != NULL &&
                type_ptr_depth(address_definition->type) > 0)
                element_type = type_decay_ptr(address_definition->type);
        }
        if (mir_index_result_is_array(node) || type_is_struct_object(element_type))
            return left;
        value = mir_new_value();
        insn = mir_emit(MIR_LOAD_INDIRECT);
        insn->dst = value;
        insn->src1 = left;
        insn->type = element_type;
        mir_set_node_memory(insn, node);
        insn->memory_size = type_size(element_type);
        return value;
        }
    case AST_MEMBER:
        {
            struct FieldDef *field = mir_member_field(node);
            left = mir_lower_lvalue_address(node);
            if (left < 0)
                break;
            if (field != NULL &&
                (field->is_array || type_is_struct_object(field->type)))
                return left;
            value = mir_new_value();
            insn = mir_emit(MIR_LOAD_INDIRECT);
            insn->dst = value;
            insn->src1 = left;
            insn->type = field != NULL ? field->type : node->type;
            mir_copy_name(insn->name, node->sval);
            if (field != NULL)
                mir_set_field_memory(insn, field);
            else
                insn->memory_flags |= 8;
            return value;
        }
    case AST_COMPOUND_LITERAL:
        left = mir_lower_lvalue_address(node);
        if (left < 0)
            break;
        if (type_ptr_depth(node->type) == 0 &&
            type_is_struct_object(node->type))
            return left;
        value = mir_new_value();
        insn = mir_emit(MIR_LOAD_INDIRECT);
        insn->dst = value;
        insn->src1 = left;
        insn->type = node->type;
        mir_set_node_memory(insn, node);
        return value;
    case AST_CAST:
    case AST_UNARY:
        if (node->kind == AST_UNARY && node->op == '*') {
            int argument_type;
            if (ast_va_arg_deref_type(node, &argument_type)) {
                const struct AstNode *call = node->a->a;
                struct Sym *ap = find_sym(call->list[0]->sval);
                int size = type_size(argument_type);
                if (ap != NULL) {
                    if (size < 2)
                        size = 2;
                    value = mir_new_value();
                    insn = mir_emit(MIR_VA_ARG);
                    insn->dst = value;
                    insn->type = argument_type;
                    insn->immediate = ap->offset;
                    insn->secondary_offset = size;
                    insn->object = mir_get_object(ap, ap->name);
                    mir_copy_name(insn->name, ap->name);
                    return value;
                }
            }
        }
        if (node->op == TOK_INC || node->op == TOK_DEC) {
            value = mir_lower_incdec(node->a, node->op, 0);
            if (value >= 0)
                return value;
        }
        if (node->kind == AST_UNARY && node->op == '&') {
            value = mir_lower_lvalue_address(node->a);
            if (value >= 0) {
                struct MirInsn *address = mir_mutable_definition(value);
                if (address != NULL)
                    address->memory_flags |= 1024;
                return value;
            }
            break;
        }
        if (node->kind == AST_UNARY && node->op == '*') {
            int pointer_type;
            int no_deref;
            int dereferenced_type = node->type;
            int dereference_depth = 0;
            struct Sym *pointer_array = mir_pointer_array_root(
                node->a, &dereference_depth);
            if (pointer_array != NULL &&
                pointer_array->dim_count > dereference_depth) {
                value = mir_lower_expr(node->a);
                insn = mir_mutable_definition(value);
                if (insn != NULL)
                    insn->type = type_add_ptr(type_decay_ptr(
                        pointer_array->type));
                return value;
            }
            if (ast_pointer_expr_type(node, &pointer_type, &no_deref)) {
                if (no_deref)
                    return mir_lower_expr(node->a);
                dereferenced_type = pointer_type;
            }
            left = mir_lower_lvalue_address(node);
            if (left < 0)
                break;
            if (type_ptr_depth(dereferenced_type) == 0 &&
                type_is_struct_object(dereferenced_type))
                return left;
            value = mir_new_value();
            insn = mir_emit(MIR_LOAD_INDIRECT);
            insn->dst = value;
            insn->src1 = left;
            insn->type = dereferenced_type;
            mir_set_node_memory(insn, node);
            insn->memory_size = type_size(dereferenced_type);
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
        {
        int left_pointer_type = 0;
        int right_pointer_type = 0;
        int left_no_deref = 0;
        int right_no_deref = 0;
        int left_is_pointer = ast_pointer_expr_type(
            node->a, &left_pointer_type, &left_no_deref);
        int right_is_pointer = ast_pointer_expr_type(
            node->b, &right_pointer_type, &right_no_deref);
        left = mir_lower_expr(node->a);
        right = mir_lower_expr(node->b);
        if (!left_is_pointer) {
            const struct MirInsn *definition = mir_mutable_definition(left);
            if (definition != NULL && type_ptr_depth(definition->type) > 0) {
                left_is_pointer = 1;
                left_pointer_type = definition->type;
            }
        }
        if (!right_is_pointer) {
            const struct MirInsn *definition = mir_mutable_definition(right);
            if (definition != NULL && type_ptr_depth(definition->type) > 0) {
                right_is_pointer = 1;
                right_pointer_type = definition->type;
            }
        }
        if ((node->op == '+' || node->op == '-') && node->a != NULL &&
            left_is_pointer && !right_is_pointer) {
            int stride = mir_pointer_arithmetic_stride(node->a);
            const struct MirInsn *pointer_definition =
                mir_mutable_definition(left);
            if (stride <= 1 && pointer_definition != NULL &&
                type_ptr_depth(pointer_definition->type) > 0) {
                int typed_stride = type_index_elem_size(
                    pointer_definition->type);
                if (typed_stride > stride)
                    stride = typed_stride;
            }
            if (stride > 1) {
                int scale = mir_new_value();
                int scaled = mir_new_value();
                insn = mir_emit(MIR_CONST);
                insn->dst = scale;
                insn->type = TYPE_INT;
                insn->immediate = stride;
                insn = mir_emit(MIR_BINARY);
                insn->dst = scaled;
                insn->src1 = right;
                insn->src2 = scale;
                insn->type = TYPE_INT;
                insn->secondary_offset = TYPE_INT;
                insn->immediate = '*';
                right = scaled;
            }
        } else if (node->op == '+' && node->b != NULL &&
                   right_is_pointer && !left_is_pointer) {
            int stride = mir_pointer_arithmetic_stride(node->b);
            const struct MirInsn *pointer_definition =
                mir_mutable_definition(right);
            if (stride <= 1 && pointer_definition != NULL &&
                type_ptr_depth(pointer_definition->type) > 0) {
                int typed_stride = type_index_elem_size(
                    pointer_definition->type);
                if (typed_stride > stride)
                    stride = typed_stride;
            }
            if (stride > 1) {
                int scale = mir_new_value();
                int scaled = mir_new_value();
                insn = mir_emit(MIR_CONST);
                insn->dst = scale;
                insn->type = TYPE_INT;
                insn->immediate = stride;
                insn = mir_emit(MIR_BINARY);
                insn->dst = scaled;
                insn->src1 = left;
                insn->src2 = scale;
                insn->type = TYPE_INT;
                insn->secondary_offset = TYPE_INT;
                insn->immediate = '*';
                left = right;
                right = scaled;
            }
        }
        if (node->operand_type != 0 &&
            !left_is_pointer && !right_is_pointer) {
            left = mir_lower_conversion(left, node->operand_type);
            if (node->op != TOK_SHL && node->op != TOK_SHR)
                right = mir_lower_conversion(right, node->operand_type);
        }
        if (!left_is_pointer && !right_is_pointer) {
            struct MirInsn *left_definition = mir_mutable_definition(left);
            struct MirInsn *right_definition = mir_mutable_definition(right);
            long folded;
            int is_compare_op = node->op == TOK_EQ || node->op == TOK_NE ||
                                 node->op == '<' || node->op == '>' ||
                                 node->op == TOK_LE || node->op == TOK_GE;
            int fold_type = node->operand_type != 0
                                ? node->operand_type
                                : node->type != 0
                                      ? node->type
                                      : ast_expr_type_for_sizeof(node);
            if (left_definition != NULL && right_definition != NULL &&
                left_definition->opcode == MIR_CONST &&
                right_definition->opcode == MIR_CONST &&
                (is_compare_op
                     ? mir_fold_constant_compare(
                           node->op, left_definition->immediate,
                           right_definition->immediate, fold_type, &folded)
                     : mir_fold_constant_binary(
                           node->op, left_definition->immediate,
                           right_definition->immediate, fold_type,
                           &folded))) {
                /* A literal constant value id can be shared by more than
                 * one consumer (e.g. the frontend's constant-value
                 * caching), so only retire an operand's MIR_CONST to
                 * MIR_NOP when nothing else in the function still
                 * references it - otherwise leave it in place, still
                 * unused by this fold but available to its other
                 * consumer(s). Retiring genuinely dead ones avoids
                 * leaving unreferenced MIR_CONST instructions in the
                 * stream, which downstream backend-slot assignment does
                 * not expect and can mishandle. */
                if (mir_value_use_count(left) == 0) {
                    left_definition->opcode = MIR_NOP;
                    left_definition->dst = -1;
                }
                if (mir_value_use_count(right) == 0) {
                    right_definition->opcode = MIR_NOP;
                    right_definition->dst = -1;
                }
                value = mir_new_value();
                insn = mir_emit(MIR_CONST);
                insn->dst = value;
                insn->type = node->type != 0 ? node->type
                                              : ast_expr_type_for_sizeof(node);
                insn->immediate = folded;
                return value;
            }
        }
        value = mir_new_value();
        insn = mir_emit(MIR_BINARY);
        insn->dst = value;
        insn->src1 = left;
        insn->src2 = right;
        insn->type = left_is_pointer != right_is_pointer &&
                     (node->op == '+' || node->op == '-')
            ? (left_is_pointer ? left_pointer_type : right_pointer_type)
            : node->type != 0 ? node->type : ast_expr_type_for_sizeof(node);
        insn->secondary_offset = left_is_pointer != right_is_pointer &&
                                 (node->op == '+' || node->op == '-')
            ? TYPE_INT
            : node->operand_type != 0 ? node->operand_type : insn->type;
        insn->immediate = node->op;
        if (node->op == '-' && left_is_pointer && right_is_pointer) {
            int stride = mir_pointer_arithmetic_stride(node->a);
            if (stride > 1) {
                int divisor = mir_new_value();
                int difference = mir_new_value();
                insn->type = TYPE_INT;
                insn->secondary_offset = TYPE_INT;
                insn = mir_emit(MIR_CONST);
                insn->dst = divisor;
                insn->type = TYPE_INT;
                insn->immediate = stride;
                insn = mir_emit(MIR_BINARY);
                insn->dst = difference;
                insn->src1 = value;
                insn->src2 = divisor;
                insn->type = TYPE_INT;
                insn->secondary_offset = TYPE_INT;
                insn->immediate = '/';
                value = difference;
            }
        }
        return value;
        }
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
    case AST_LOGOR:
        /* Normalize the RHS in its own two-way region, then merge that value
         * with the short-circuit true path. Keeping the two merges separate
         * gives every two-input phi exact incoming-edge labels. */
        rhs_label = mir_new_label();
        true_label = mir_new_label();
        false_label = mir_new_label();
        rhs_end_label = mir_new_label();
        end_label = mir_new_label();
        left = mir_lower_expr(node->a);
        insn = mir_emit(MIR_BRANCH_FALSE);
        insn->src1 = left;
        insn->label = rhs_label;
        mir_emit_label(true_label);
        true_value = mir_new_value();
        insn = mir_emit(MIR_CONST);
        insn->dst = true_value;
        insn->type = node->type;
        insn->immediate = 1;
        mir_emit_jump(end_label);
        mir_emit_label(rhs_label);
        right = mir_lower_expr(node->b);
        insn = mir_emit(MIR_BRANCH_FALSE);
        insn->src1 = right;
        insn->label = false_label;
        mir_emit_label(then_exit_label = mir_new_label());
        left = mir_new_value();
        insn = mir_emit(MIR_CONST);
        insn->dst = left;
        insn->type = node->type;
        insn->immediate = 1;
        mir_emit_jump(rhs_end_label);
        mir_emit_label(false_label);
        false_value = mir_new_value();
        insn = mir_emit(MIR_CONST);
        insn->dst = false_value;
        insn->type = node->type;
        insn->immediate = 0;
        mir_emit_label(rhs_end_label);
        right = mir_new_value();
        insn = mir_emit(MIR_PHI);
        insn->dst = right;
        insn->src1 = left;
        insn->src2 = false_value;
        insn->phi_pred1 = then_exit_label;
        insn->phi_pred2 = false_label;
        insn->type = node->type;
        mir_emit_label(else_exit_label = mir_new_label());
        mir_emit_jump(end_label);
        mir_emit_label(end_label);
        value = mir_new_value();
        insn = mir_emit(MIR_PHI);
        insn->dst = value;
        insn->src1 = true_value;
        insn->src2 = right;
        insn->phi_pred1 = true_label;
        insn->phi_pred2 = else_exit_label;
        insn->type = node->type;
        return value;
    case AST_COND:
        {
        int conditional_type = node->type;
        int true_pointer_type;
        int false_pointer_type;
        int no_deref;
        if (ast_pointer_expr_type(node->b, &true_pointer_type, &no_deref) &&
            ast_pointer_expr_type(node->c, &false_pointer_type, &no_deref) &&
            type_ptr_depth(true_pointer_type) > 0 &&
            type_ptr_depth(false_pointer_type) > 0)
            conditional_type = true_pointer_type;
        false_label = mir_new_label();
        end_label = mir_new_label();
        left = mir_lower_expr(node->a);
        insn = mir_emit(MIR_BRANCH_FALSE);
        insn->src1 = left;
        insn->label = false_label;
        true_value = mir_lower_expr(node->b);
        true_value = mir_lower_conversion(true_value, conditional_type);
        mir_emit_label(then_exit_label = mir_new_label());
        mir_emit_jump(end_label);
        mir_emit_label(false_label);
        false_value = mir_lower_expr(node->c);
        false_value = mir_lower_conversion(false_value, conditional_type);
        mir_emit_label(else_exit_label = mir_new_label());
        mir_emit_label(end_label);
        if ((conditional_type & 15) == TYPE_VOID)
            return -1;
        value = mir_new_value();
        insn = mir_emit(MIR_PHI);
        insn->dst = value;
        insn->src1 = true_value;
        insn->src2 = false_value;
        insn->phi_pred1 = then_exit_label;
        insn->phi_pred2 = else_exit_label;
        insn->type = conditional_type;
        return value;
        }
    case AST_ASSIGN:
        if (node->a == NULL)
            break;
        if (type_is_struct_object(node->a->type)) {
            if (node->b != NULL && node->b->kind == AST_CALL &&
                node->a->kind == AST_IDENT) {
                struct Sym *destination_symbol = node->a->sym != NULL
                    ? node->a->sym : find_sym(node->a->sval);
                value = mir_lower_aggregate_call_address(node->b,
                                                          destination_symbol);
                if (value >= 0)
                    return value;
            }
            int destination = mir_lower_lvalue_address(node->a);
            int source = mir_lower_expr(node->b);
            if (destination < 0 || source < 0)
                break;
            insn = mir_emit(MIR_COPY_AGGREGATE);
            insn->src1 = destination;
            insn->src2 = source;
            insn->type = node->a->type;
            insn->memory_size = type_size(node->a->type);
            return destination;
        }
        if (node->a->kind == AST_MEMBER) {
            struct FieldDef *field = mir_member_field(node->a);
            int address;
            int target_type = mir_lvalue_type(node->a);
            address = mir_lower_lvalue_address(node->a);
            if (address < 0)
                break;
            if (node->op == '=') {
                value = mir_lower_expr(node->b);
                value = mir_lower_conversion(value, target_type);
            } else {
                int binary_operator = mir_compound_binary_operator(node->op);
                if (binary_operator == 0)
                    break;
                left = mir_new_value();
                insn = mir_emit(MIR_LOAD_INDIRECT);
                insn->dst = left;
                insn->src1 = address;
                insn->type = node->a->type;
                if (field != NULL)
                    mir_set_field_memory(insn, field);
                else
                    insn->memory_flags |= 8;
                right = mir_lower_expr(node->b);
                value = mir_lower_compound_value(left, right, binary_operator,
                                                 target_type);
            }
            insn = mir_emit(MIR_STORE_INDIRECT);
            insn->src1 = address;
            insn->src2 = value;
            insn->type = node->a->type;
            mir_copy_name(insn->name, node->a->sval);
            if (field != NULL)
                mir_set_field_memory(insn, field);
            else
                insn->memory_flags |= 8;
            if (field != NULL && field->bit_width > 0)
                value = mir_reload_bitfield(address, field, target_type);
            return value;
        }
        if (node->a->kind == AST_INDEX ||
            (node->a->kind == AST_UNARY && node->a->op == '*')) {
            int address = mir_lower_lvalue_address(node->a);
            int target_type = mir_lvalue_type(node->a);
            const struct MirInsn *address_definition;
            if (address < 0)
                break;
            address_definition = mir_mutable_definition(address);
            if (address_definition != NULL &&
                type_ptr_depth(address_definition->type) > 0)
                target_type = type_decay_ptr(address_definition->type);
            if (node->op == '=') {
                value = mir_lower_expr(node->b);
                value = mir_lower_conversion(value, target_type);
            } else {
                int binary_operator = mir_compound_binary_operator(node->op);
                if (binary_operator == 0)
                    break;
                left = mir_new_value();
                insn = mir_emit(MIR_LOAD_INDIRECT);
                insn->dst = left;
                insn->src1 = address;
                insn->type = target_type;
                mir_set_node_memory(insn, node->a);
                insn->memory_size = type_size(target_type);
                right = mir_lower_expr(node->b);
                value = mir_lower_compound_value(left, right, binary_operator,
                                                 target_type);
            }
            insn = mir_emit(MIR_STORE_INDIRECT);
            insn->src1 = address;
            insn->src2 = value;
            insn->type = target_type;
            mir_set_node_memory(insn, node->a);
            insn->memory_size = type_size(target_type);
            return value;
        }
        if (node->a->kind != AST_IDENT)
            break;
        if (node->op == '=') {
            value = mir_lower_expr(node->b);
            value = mir_lower_ident_assignment_conversion(value, node->a);
        } else {
            int binary_operator = mir_compound_binary_operator(node->op);
            if (binary_operator == 0)
                break;
            left = mir_lower_expr(node->a);
            right = mir_lower_expr(node->b);
            {
                int target_type = mir_lvalue_type(node->a);
            value = mir_lower_compound_value(left, right, binary_operator,
                                             target_type);
            }
        }
        mir_emit_ident_store(node->a, value);
        return value;
    case AST_CALL:
        {
        int call_id = mir.next_call_id++;
        int callee_value = -1;
        const char *syntactic_name = node->a != NULL &&
                                     node->a->kind == AST_IDENT
            ? node->a->sval : "<indirect>";
        const char *call_name = syntactic_name;
        struct Sym *callee_identifier = node->a != NULL &&
                                        node->a->kind == AST_IDENT
            ? mir_ident_symbol(node->a) : NULL;
        int function_pointer_call = callee_identifier != NULL &&
                                    callee_identifier->is_funcptr;
        struct Sym *function_symbol = node->a != NULL &&
                                      node->a->kind == AST_IDENT
            ? (callee_identifier != NULL &&
               callee_identifier->storage == SC_FUNC
                ? callee_identifier : find_global(call_name))
            : NULL;
        struct Sym *call_prototype = function_symbol;
        if ((function_symbol == NULL || function_symbol->storage != SC_FUNC) &&
            (node->a == NULL || node->a->kind != AST_IDENT ||
             function_pointer_call)) {
            call_name = "<indirect>";
            function_symbol = NULL;
        } else if (function_symbol != NULL &&
                   function_symbol->storage != SC_FUNC)
            function_symbol = NULL;
        if (function_symbol != NULL &&
            !mir_inline_substitutable(function_symbol))
            function_symbol->deferred_body_needed = 1;
        if ((!strcmp(syntactic_name, "__va_start") && node->list_len == 2) ||
            (!strcmp(syntactic_name, "__va_end") && node->list_len == 1)) {
            struct Sym *ap = node->list[0]->kind == AST_IDENT
                ? find_sym(node->list[0]->sval) : NULL;
            struct Sym *last = node->list_len == 2 &&
                               node->list[1]->kind == AST_IDENT
                ? find_sym(node->list[1]->sval) : NULL;
            if (ap != NULL && (node->list_len == 1 || last != NULL)) {
                value = mir_new_value();
                insn = mir_emit(node->list_len == 2 ? MIR_VA_START : MIR_VA_END);
                insn->dst = value;
                insn->type = TYPE_INT;
                insn->immediate = ap->offset;
                if (last != NULL) {
                    int size = type_size(last->type);
                    if (size < 2)
                        size = 2;
                    insn->secondary_offset = last->offset + size;
                }
                mir_copy_name(insn->name, ap->name);
                return value;
            }
        }
        if (strcmp(call_name, "<indirect>") == 0) {
            const struct AstNode *callee = node->a;
            while (callee != NULL && callee->kind == AST_UNARY &&
                   callee->op == '*')
                callee = callee->a;
            if (callee != NULL && callee->kind == AST_IDENT)
                call_prototype = mir_ident_symbol(callee);
            callee_value = mir_lower_expr(callee);
        }
        for (i = 0; i < node->list_len; ++i) {
            int argument_type = ast_expr_type_for_sizeof(node->list[i]);
            struct Sym nested_temporary;
            if (call_prototype != NULL && i < call_prototype->proto_nargs) {
                argument_type = call_prototype->proto_types[i];
            }
            if (type_is_struct_object(argument_type) &&
                node->list[i]->kind == AST_CALL) {
                const struct Sym *temporary = node->list[i]->sym;
                if (temporary == NULL) {
                    int size = type_size(argument_type);
                    memset(&nested_temporary, 0, sizeof(nested_temporary));
                    if ((size & 1) != 0)
                        ++size;
                    mir.aggregate_temp_bytes += size;
                    nested_temporary.type = argument_type;
                    nested_temporary.offset = -mir.local_bytes -
                                              mir.aggregate_temp_bytes;
                    strcpy(nested_temporary.name, "#miragg");
                    temporary = &nested_temporary;
                }
                left = mir_lower_aggregate_call_address(node->list[i],
                                                        temporary);
            } else
                left = mir_lower_expr(node->list[i]);
            if (call_prototype == NULL || i >= call_prototype->proto_nargs) {
                const struct MirInsn *argument_definition =
                    mir_definition(left);
                if (argument_definition != NULL &&
                    argument_definition->type != 0)
                    argument_type = argument_definition->type;
            }
            if (!type_is_struct_object(argument_type))
                left = mir_lower_conversion(left, argument_type);
            insn = mir_emit(MIR_ARG);
            insn->src1 = left;
            insn->type = argument_type;
            insn->immediate = i;
            insn->secondary_offset = call_id;
        }
        value = mir_new_value();
        insn = mir_emit(MIR_CALL);
        insn->dst = value;
        insn->src1 = callee_value;
        insn->type = function_symbol != NULL ? function_symbol->type
            : call_prototype != NULL ? type_decay_ptr(call_prototype->type)
            : node->type;
        mir_copy_name(insn->name, call_name);
        insn->secondary_offset = call_id;
        if (function_symbol != NULL) {
            if (mir_inline_substitutable(function_symbol))
                insn->memory_flags |= 2048;
            int format_index = asm_printf_family_fmt_arg_index(call_name);
            if (format_index >= 0 && format_index < node->list_len) {
                int needs_float = 0;
                int needs_long = 0;
                int needs_hex = 0;
                int needs_octal = 0;
                const struct AstNode *format = node->list[format_index];
                if (format->kind == AST_STR_LIT)
                    asm_scan_format_specifiers(format->sval, &needs_float,
                                               &needs_long, &needs_hex,
                                               &needs_octal);
                else
                    needs_float = needs_long = needs_hex = needs_octal = 1;
                if (opt_floatio > 0) needs_float = 1;
                else if (opt_floatio < 0) needs_float = 0;
                if (opt_longio > 0) needs_long = 1;
                else if (opt_longio < 0) needs_long = 0;
                if (opt_hexio > 0) needs_hex = 1;
                else if (opt_hexio < 0) needs_hex = 0;
                if (opt_octio > 0) needs_octal = 1;
                else if (opt_octio < 0) needs_octal = 0;
                mir_copy_name(insn->base_name,
                              asm_name_for_pf_call(call_name, needs_float,
                                                   needs_long));
                if (needs_hex)
                    insn->memory_flags |= 32;
                if (needs_octal)
                    insn->memory_flags |= 64;
            }
        }
        return value;
        }
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
    int next_label;
    int case_value;
    int compare_value;
    struct MirSwitchContext *switch_context;
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
        insn = mir_emit(MIR_DECL_PLACEHOLDER);
        if (mir.declaration_count < 1024) {
            mir.declaration_placeholders[mir.declaration_count] =
                (int)(insn - mir.insns);
            mir.declaration_scope_ends[mir.declaration_count] = -1;
            mir.declaration_nodes[mir.declaration_count] = node;
            mir.declaration_consumed[mir.declaration_count] = 0;
            ++mir.declaration_count;
        }
        return;
    case AST_COMPOUND:
        {
        int first_declaration = mir.declaration_count;
        int scope_slot = mir.scope_count++;
        for (i = 0; i < node->list_len; ++i)
            mir_lower_stmt(node->list[i]);
        insn = mir_emit(MIR_VLA_RESTORE);
        insn->immediate = 0;
        insn->label = mir_new_label();
        if (scope_slot < 1024)
            mir.scope_points[scope_slot] = insn->label;
        for (i = first_declaration; i < mir.declaration_count; ++i)
            if (mir.declaration_scope_ends[i] < 0)
                mir.declaration_scope_ends[i] = mir.count;
        return;
        }
    case AST_RETURN:
        if (type_is_struct_object(mir.return_type) && node->a != NULL &&
            node->a->kind == AST_CALL)
            condition = mir_lower_aggregate_call_address(node->a, NULL);
        else {
            condition = mir_lower_expr(node->a);
            if (!type_is_struct_object(mir.return_type))
                condition = mir_lower_conversion(condition, mir.return_type);
        }
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
    case AST_SWITCH:
        if (mir.switch_depth >= MAX_FLOW || mir.flow_depth >= MAX_FLOW)
            break;
        condition = mir_lower_expr(node->a);
        switch_context = &mir.switches[mir.switch_depth++];
        memset(switch_context, 0, sizeof(*switch_context));
        switch_context->default_label = -1;
        switch_context->end_label = mir_new_label();
        mir_collect_switch_labels(node->b, switch_context);
        for (i = 0; i < switch_context->count; ++i) {
            next_label = mir_new_label();
            case_value = mir_new_value();
            insn = mir_emit(MIR_CONST);
            insn->dst = case_value;
            insn->type = node->a != NULL ? node->a->type : TYPE_INT;
            insn->immediate = switch_context->values[i];
            compare_value = mir_new_value();
            insn = mir_emit(MIR_BINARY);
            insn->dst = compare_value;
            insn->src1 = condition;
            insn->src2 = case_value;
            insn->type = TYPE_INT;
            insn->immediate = TOK_EQ;
            insn = mir_emit(MIR_BRANCH_FALSE);
            insn->src1 = compare_value;
            insn->label = next_label;
            mir_emit_jump(switch_context->labels[i]);
            mir_emit_label(next_label);
        }
        mir_emit_jump(switch_context->default_label >= 0
                      ? switch_context->default_label
                      : switch_context->end_label);
        mir.break_labels[mir.flow_depth] = switch_context->end_label;
        mir.continue_labels[mir.flow_depth] = mir.flow_depth > 0
            ? mir.continue_labels[mir.flow_depth - 1] : -1;
        ++mir.flow_depth;
        mir_lower_stmt(node->b);
        --mir.flow_depth;
        mir_emit_label(switch_context->end_label);
        --mir.switch_depth;
        return;
    case AST_CASE:
        if (mir.switch_depth > 0) {
            switch_context = &mir.switches[mir.switch_depth - 1];
            next_label = mir_switch_case_label(switch_context, node->ival);
            if (next_label >= 0) {
                mir_emit_label(next_label);
                mir_lower_stmt(node->b);
                return;
            }
        }
        break;
    case AST_DEFAULT:
        if (mir.switch_depth > 0) {
            switch_context = &mir.switches[mir.switch_depth - 1];
            if (switch_context->default_label >= 0) {
                mir_emit_label(switch_context->default_label);
                mir_lower_stmt(node->b);
                return;
            }
        }
        break;
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
        if (node->a != NULL && node->a->kind == AST_DECL)
            for (i = 0; i < mir.declaration_count; ++i)
                if (mir.declaration_nodes[i] == node->a) {
                    mir.declaration_scope_ends[i] = mir.count;
                    break;
                }
        return;
    case AST_FOR:
        top_label = mir_new_label();
        end_label = mir_new_label();
        continue_label = mir_new_label();
        if (node->a != NULL) {
            if (node->a->kind == AST_DECL)
                mir_lower_stmt(node->a);
            else
                (void)mir_lower_expr(node->a);
        }
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
        if (mir.flow_depth > 0) {
            int flow_slot = mir.flow_count++;
            insn = mir_emit(MIR_VLA_RESTORE);
            insn->label = mir_new_label();
            if (flow_slot < 1024)
                mir.flow_points[flow_slot] = insn->label;
            mir_emit_jump(mir.break_labels[mir.flow_depth - 1]);
        } else
            (void)mir_emit(MIR_OPAQUE);
        return;
    case AST_CONTINUE:
        if (mir.flow_depth > 0 &&
            mir.continue_labels[mir.flow_depth - 1] >= 0) {
            int flow_slot = mir.flow_count++;
            insn = mir_emit(MIR_VLA_RESTORE);
            insn->label = mir_new_label();
            if (flow_slot < 1024)
                mir.flow_points[flow_slot] = insn->label;
            mir_emit_jump(mir.continue_labels[mir.flow_depth - 1]);
        } else
            (void)mir_emit(MIR_OPAQUE);
        return;
    case AST_GOTO:
        {
            int label = mir_user_label(node->sval);
            if (label >= 0) {
                int flow_slot = mir.flow_count++;
                insn = mir_emit(MIR_VLA_RESTORE);
                insn->label = mir_new_label();
                mir_copy_name(insn->name, node->sval);
                if (flow_slot < 1024)
                    mir.flow_points[flow_slot] = insn->label;
                mir_emit_jump(label);
                return;
            }
        }
        break;
    case AST_LABEL:
        {
            int label = mir_user_label(node->sval);
            if (label >= 0) {
                mir_emit_label(label);
                mir_lower_stmt(node->b);
                return;
            }
        }
        break;
    default:
        break;
    }
    insn = mir_emit(MIR_OPAQUE);
    insn->immediate = node->kind;
}

void mir_begin_function(const char *name, int sink_purpose, int has_vla,
                        int local_bytes)
{
    struct Sym *function_symbol;

    mir.count = 0;
    mir.next_value = 0;
    mir.next_label = 0;
    mir.next_call_id = 0;
    mir.flow_depth = 0;
    mir.has_vla = has_vla;
    function_symbol = find_global(name);
    mir.is_variadic_function = function_symbol != NULL &&
                               function_symbol->proto_variadic;
    mir.has_runtime_stride_param = 0;
    mir.user_label_count = 0;
    mir.switch_depth = 0;
    mir.declaration_count = 0;
    mir.declaration_cursor = 0;
    mir.declaration_active = 0;
    mir.compound_depth = 0;
    mir.scope_count = 0;
    mir.scope_cursor = 0;
    mir.scope_replay_depth = 0;
    mir.flow_count = 0;
    mir.flow_cursor = 0;
    mir.flow_replay_active = 0;
    mir.label_replay_active = 0;
    mir.object_count = 0;
    mir.declared_count = 0;
    mir.alias_count = 0;
    mir.initializer_target = NULL;
    mir.init_expression_target = NULL;
    mir.vla_target = NULL;
    mir.sink_purpose = sink_purpose;
    mir.emit_mode = 1;
    mir.report_mode = getenv("DCC_MIR_REPORT") != NULL ||
                      getenv("DCC_MIR_FUNCTION") != NULL ||
                      getenv("DCC_MIR_CANDIDATES") != NULL ||
                      getenv("DCC_MIR_GENERAL_CANDIDATES") != NULL ||
                      getenv("DCC_MIR_HOME_CFG_CANDIDATES") != NULL ||
                      getenv("DCC_MIR_EMIT_FUNCTION") != NULL ||
                      getenv("DCC_MIR_GENERAL_FUNCTION") != NULL;
    mir.return_type = current_return_type != 0 ? current_return_type
        : function_symbol != NULL ? function_symbol->type : TYPE_INT;
    mir.local_bytes = local_bytes;
    mir.aggregate_temp_bytes = 0;
    mir.opaque_count = 0;
    mir.capture_stream = NULL;
    mir_copy_name(mir.name, name);
    mir.active = 1;
    mir.capture_stream = tmpfile();
    if (mir.capture_stream == NULL)
        fatal("cannot create MIR capture stream");
    mir.saved_sink = emit_sink_push(mir.capture_stream, sink_purpose);
    mir_emit_label(mir_new_label());
    {
        int local;
        int parameter_offset = type_is_struct_object(mir.return_type) ? 6 : 4;
        for (local = 0; local < g_frame.nlocals; ++local) {
            int object_index;
            int value;
            struct MirInsn *insn;

            if (locals[local].storage == SC_PARAM) {
                int declared;
                mir_note_declared_symbol(&locals[local]);
                for (declared = 0; declared < mir.declared_count; ++declared)
                    if (strcmp(mir.declared_names[declared],
                               locals[local].name) == 0) {
                        mir.declared_offsets[declared] = parameter_offset;
                        break;
                    }
                if (locals[local].dim_count > 0 ||
                    locals[local].runtime_stride_name[0] != 0)
                    mir.has_runtime_stride_param = 1;
            }
            if (locals[local].storage != SC_PARAM)
                continue;
            object_index = mir_object_eligible(&locals[local])
                ? mir_get_object(&locals[local], locals[local].name) : -1;
            if (object_index >= 0)
                mir.objects[object_index].offset = parameter_offset;
            value = mir_new_value();
            if (object_index >= 0)
                mir.objects[object_index].entry_value = value;
            insn = mir_emit(MIR_PARAM);
            insn->dst = value;
            insn->type = locals[local].type;
            insn->object = object_index;
            mir_copy_name(insn->name, locals[local].name);
            {
                int size = type_size(locals[local].type);
                if (size < 2)
                    size = 2;
                parameter_offset += size;
            }
        }
    }
}

void mir_capture_stmt(const struct AstNode *stmt)
{
    if (mir.active)
        mir_lower_stmt(stmt);
}

void mir_note_declared_symbol(struct Sym *symbol)
{
    int i;
    int is_new;
    if (!mir.active || symbol == NULL)
        return;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], symbol->name) == 0)
            break;
    is_new = i == mir.declared_count;
    if (is_new) {
        if (mir.declared_count >= MAX_LOCALS)
            return;
        mir_copy_name(mir.declared_names[i], symbol->name);
        ++mir.declared_count;
    }
    mir.declared_types[i] = symbol->type;
    mir.declared_storage[i] = symbol->storage;
    mir.declared_offsets[i] = symbol->offset;
    mir.declared_sizes[i] = symbol->size;
    if (is_new || symbol->dim_count > 0) {
        mir.declared_dim_counts[i] = symbol->dim_count;
        memcpy(mir.declared_dims[i], symbol->dims,
               sizeof(mir.declared_dims[i]));
    }
    mir_copy_name(mir.declared_link_names[i], symbol->link_name);
    mir.declared_elem_sizes[i] = symbol->elem_size;
    mir.declared_vla_size_offsets[i] = symbol->vla_size_offset;
    mir.declared_is_vla[i] = symbol->is_vla;
    mir.declared_is_array[i] = symbol->is_array;
    mir.declared_dynamic_strides[i] = symbol->runtime_stride_name[0] != 0;
    mir_copy_name(mir.declared_runtime_stride_names[i],
                  symbol->runtime_stride_name);
    mir.declared_is_const[i] = symbol->is_const_value;
    mir.declared_const_values[i] = symbol->const_value;
    mir.declared_is_funcptr[i] = symbol->is_funcptr ||
        (symbol->storage != SC_FUNC && symbol->has_proto &&
         type_ptr_depth(symbol->type) > 0);
    mir.declared_has_proto[i] = symbol->has_proto;
    mir.declared_proto_nargs[i] = symbol->proto_nargs;
    memcpy(mir.declared_proto_types[i], symbol->proto_types,
           sizeof(mir.declared_proto_types[i]));
}

void mir_note_declared_alias(const char *source_name, struct Sym *symbol)
{
    if (!mir.active || source_name == NULL || symbol == NULL ||
        strcmp(source_name, symbol->name) == 0 || mir.alias_count >= MAX_LOCALS)
        return;
    if (!mir.declaration_active && strchr(symbol->name, '#') != NULL)
        return;
    mir_copy_name(mir.alias_source_names[mir.alias_count], source_name);
    mir_copy_name(mir.alias_internal_names[mir.alias_count], symbol->name);
    mir.alias_declaration_indices[mir.alias_count] =
        mir.declaration_active ? mir.declaration_active_index : -1;
    ++mir.alias_count;
}

void mir_begin_declaration(const struct AstNode *node)
{
    int i;
    if (!mir.active) {
        mir.declaration_active = 0;
        return;
    }
    for (i = 0; i < mir.declaration_count; ++i)
        if (!mir.declaration_consumed[i] && mir.declaration_nodes[i] == node)
            break;
    if (i == mir.declaration_count) {
        mir.declaration_active = 0;
        return;
    }
    mir.declaration_consumed[i] = 1;
    mir.declaration_active_index = i;
    mir.declaration_placeholder = mir.declaration_placeholders[i];
    mir.declaration_capture_start = mir.count;
    mir.declaration_active = 1;
}

void mir_end_declaration(void)
{
    struct MirInsn *captured;
    int captured_count;
    int middle_count;
    int i;

    if (!mir.active || !mir.declaration_active)
        return;
    mir.declaration_active = 0;
    mir.initializer_target = NULL;
    mir.vla_target = NULL;
    captured_count = mir.count - mir.declaration_capture_start;
    if (mir.declaration_placeholder < 0 ||
        mir.declaration_placeholder >= mir.declaration_capture_start)
        fatal("invalid MIR declaration placeholder");
    mir.insns[mir.declaration_placeholder].opcode = MIR_NOP;
    if (captured_count == 0)
        return;
    captured = (struct MirInsn *)malloc(
        (size_t)captured_count * sizeof(*captured));
    if (captured == NULL)
        fatal("out of memory splicing MIR declaration");
    memcpy(captured, &mir.insns[mir.declaration_capture_start],
           (size_t)captured_count * sizeof(*captured));
    middle_count = mir.declaration_capture_start -
                   mir.declaration_placeholder - 1;
    memmove(&mir.insns[mir.declaration_placeholder + 1 + captured_count],
            &mir.insns[mir.declaration_placeholder + 1],
            (size_t)middle_count * sizeof(*mir.insns));
    memcpy(&mir.insns[mir.declaration_placeholder + 1], captured,
           (size_t)captured_count * sizeof(*captured));
    free(captured);
    for (i = 0; i < mir.declaration_count; ++i)
        if (!mir.declaration_consumed[i] &&
            mir.declaration_placeholders[i] > mir.declaration_placeholder &&
            mir.declaration_placeholders[i] < mir.declaration_capture_start)
            mir.declaration_placeholders[i] += captured_count;
    for (i = 0; i < mir.declaration_count; ++i)
        if (mir.declaration_scope_ends[i] > mir.declaration_placeholder &&
            mir.declaration_scope_ends[i] < mir.declaration_capture_start)
            mir.declaration_scope_ends[i] += captured_count;
}

void mir_set_initializer_target(struct Sym *symbol)
{
    int i;
    if (mir.active) {
        if (mir.declaration_active) {
            for (i = 0; i < mir.count; ++i) {
                struct MirInsn *prior = &mir.insns[i];
                if (prior->opcode == MIR_STORE &&
                    (prior->memory_flags & 128) != 0 &&
                    strcmp(prior->name, symbol->name) == 0) {
                    int first = prior->label;
                    int j;
                    if (first < 0 || first > i)
                        first = i;
                    for (j = first; j <= i; ++j)
                        mir.insns[j].opcode = MIR_NOP;
                }
            }
        }
        mir.initializer_target = symbol;
        mir.initializer_capture_start = mir.count;
    }
}

void mir_set_vla_target(struct Sym *symbol)
{
    if (mir.active) {
        int i;
        if (mir.declaration_active)
            for (i = 0; i < mir.count; ++i) {
                struct MirInsn *prior = &mir.insns[i];
                if (prior->opcode == MIR_VLA_ALLOC &&
                    (prior->memory_flags & 128) != 0 &&
                    strcmp(prior->name, symbol->name) == 0) {
                    int first = prior->label;
                    int j;
                    if (first < 0 || first > i)
                        first = i;
                    for (j = first; j <= i; ++j)
                        mir.insns[j].opcode = MIR_NOP;
                }
            }
        mir.vla_target = symbol;
    }
}

void mir_capture_vla_save(int offset)
{
    struct MirInsn *insn;
    if (!mir.active)
        return;
    insn = mir_emit(MIR_VLA_SAVE);
    insn->immediate = offset;
    mir.vla_capture_start = mir.count - 1;
}

static struct MirInsn *mir_restore_point(int token)
{
    int i;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_VLA_RESTORE &&
            mir.insns[i].label == token)
            return &mir.insns[i];
    return NULL;
}

void mir_begin_scope_replay(void)
{
    if (!mir.active || mir.scope_cursor >= mir.scope_count ||
        mir.scope_replay_depth >= MAX_FLOW)
        return;
    mir.scope_replay_points[mir.scope_replay_depth++] =
        mir.scope_points[mir.scope_cursor++];
}

void mir_end_scope_replay(void)
{
    struct MirInsn *point;
    int token;
    if (!mir.active || mir.scope_replay_depth <= 0)
        return;
    token = mir.scope_replay_points[--mir.scope_replay_depth];
    point = mir_restore_point(token);
    if (point != NULL && point->immediate == 0) {
        point->opcode = MIR_NOP;
        point->label = -1;
    }
}

void mir_begin_flow_replay(void)
{
    if (!mir.active || mir.flow_cursor >= mir.flow_count)
        return;
    mir.flow_replay_point = mir.flow_points[mir.flow_cursor++];
    mir.flow_replay_active = 1;
}

void mir_end_flow_replay(void)
{
    struct MirInsn *point;
    if (!mir.active || !mir.flow_replay_active)
        return;
    point = mir_restore_point(mir.flow_replay_point);
    if (point != NULL && point->immediate == 0 &&
        (!mir.has_vla || point->name[0] == 0)) {
        point->opcode = MIR_NOP;
        point->label = -1;
    }
    mir.flow_replay_active = 0;
}

void mir_begin_label_replay(const char *name)
{
    if (!mir.active)
        return;
    mir_copy_name(mir.label_replay_name, name);
    mir.label_replay_active = 1;
}

void mir_end_label_replay(void)
{
    int i;
    if (!mir.active || !mir.label_replay_active)
        return;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_VLA_RESTORE &&
            mir.insns[i].immediate == 0 &&
            strcmp(mir.insns[i].name, mir.label_replay_name) == 0) {
            mir.insns[i].opcode = MIR_NOP;
            mir.insns[i].label = -1;
        }
    mir.label_replay_active = 0;
}

void mir_capture_vla_restore(int offset)
{
    struct MirInsn *insn;
    if (!mir.active)
        return;
    if (mir.flow_replay_active) {
        int i;
        insn = mir_restore_point(mir.flow_replay_point);
        if (insn == NULL)
            for (i = mir.count - 1; i >= 0; --i)
                if (mir.insns[i].opcode == MIR_NOP &&
                    mir.insns[i].name[0] != 0 &&
                    mir.insns[i].immediate == 0) {
                    insn = &mir.insns[i];
                    break;
                }
        if (insn != NULL) {
            insn->opcode = MIR_VLA_RESTORE;
            insn->immediate = offset;
            return;
        }
    }
    if (mir.label_replay_active) {
        int i;
        for (i = 0; i < mir.count; ++i)
            if (mir.insns[i].opcode == MIR_VLA_RESTORE &&
                mir.insns[i].immediate == 0 &&
                strcmp(mir.insns[i].name, mir.label_replay_name) == 0) {
                mir.insns[i].opcode = MIR_VLA_RESTORE;
                mir.insns[i].immediate = offset;
                return;
            }
    }
    if (mir.scope_replay_depth > 0) {
        insn = mir_restore_point(
            mir.scope_replay_points[mir.scope_replay_depth - 1]);
        if (insn != NULL) {
            insn->opcode = MIR_VLA_RESTORE;
            insn->immediate = offset;
            return;
        }
    }
    insn = mir_emit(MIR_VLA_RESTORE);
    insn->immediate = offset;
}

static int mir_initializer_type_at_offset(int aggregate_type, int offset)
{
    int struct_id = base_struct_id_from_type(aggregate_type);
    int i;

    if (struct_id <= 0 || offset < 0)
        return 0;
    for (i = 0; i < nfield_defs; ++i) {
        const struct FieldDef *field = &field_defs[i];
        int relative;
        int nested_type;
        if (field->parent_struct_id != struct_id || field->is_promoted ||
            offset < field->offset || offset >= field->offset + field->size)
            continue;
        relative = offset - field->offset;
        if (field->is_array && field->elem_size > 0) {
            int element_relative = relative % field->elem_size;
            if (type_is_struct_object(field->elem_type)) {
                nested_type = mir_initializer_type_at_offset(
                    field->elem_type, element_relative);
                if (nested_type != 0)
                    return nested_type;
            }
            if (element_relative == 0)
                return field->elem_type;
        }
        if (type_is_struct_object(field->type)) {
            nested_type = mir_initializer_type_at_offset(field->type,
                                                         relative);
            if (nested_type != 0)
                return nested_type;
        }
        if (relative == 0)
            return field->type;
    }
    return 0;
}

void mir_capture_init_constant(struct Sym *symbol, int offset, int type,
                               long value)
{
    struct MirInsn *constant;
    struct MirInsn *store;
    int result;

    if (!mir.active || symbol == NULL)
        return;
    {
        int aggregate_type = type_ptr_depth(symbol->type) > 0
            ? type_decay_ptr(symbol->type) : symbol->type;
        if (type_is_struct_object(aggregate_type)) {
        int layout_type = mir_initializer_type_at_offset(aggregate_type,
                                                         offset);
        if (layout_type != 0)
            type = layout_type;
        }
    }
    result = mir_new_value();
    constant = mir_emit(type_is_float(type) ? MIR_FLOAT_CONST : MIR_CONST);
    constant->dst = result;
    constant->type = type;
    constant->immediate = value;
    store = mir_emit(MIR_STORE);
    store->src1 = result;
    store->type = type;
    store->immediate = offset;
    store->memory_size = type_size(type);
    mir_copy_name(store->name, symbol->name);
    store->object = offset == 0 ? mir_get_object(symbol, store->name) : -1;
}

void mir_capture_init_char_array(struct Sym *symbol, const char *bytes,
                                 int length)
{
    int offset;
    int size;
    if (!mir.active || symbol == NULL || bytes == NULL)
        return;
    size = symbol->size > 0 ? symbol->size : symbol->array_len;
    for (offset = 0; offset < size; ++offset) {
        unsigned char value = offset < length
            ? (unsigned char)bytes[offset] : 0;
        mir_capture_init_constant(symbol, offset,
                                  TYPE_CHAR | TYPE_UNSIGNED, value);
    }
}

void mir_set_init_expression_target(struct Sym *symbol, int offset, int type)
{
    if (!mir.active)
        return;
    mir.init_expression_target = symbol;
    mir.init_expression_offset = offset;
    mir.init_expression_type = type;
}

void mir_begin_compound_literal(struct Sym *symbol)
{
    if (!mir.active || symbol == NULL || mir.compound_depth >= MAX_FLOW)
        return;
    mir.compound_capture_starts[mir.compound_depth++] = mir.count;
}

void mir_end_compound_literal(struct Sym *symbol)
{
    struct MirInsn address;
    struct MirInsn *captured;
    int capture_start;
    int captured_count;
    int placeholder = -1;
    int middle_count;
    int i;

    if (!mir.active || symbol == NULL || mir.compound_depth <= 0)
        return;
    capture_start = mir.compound_capture_starts[--mir.compound_depth];
    for (i = capture_start - 1; i >= 0; --i)
        if (mir.insns[i].opcode == MIR_COMPOUND_ADDRESS &&
            strcmp(mir.insns[i].name, symbol->name) == 0) {
            placeholder = i;
            break;
        }
    if (placeholder < 0)
        return;
    address = mir.insns[placeholder];
    captured_count = mir.count - capture_start;
    if (captured_count > 0) {
        captured = (struct MirInsn *)malloc(
            (size_t)captured_count * sizeof(*captured));
        if (captured == NULL)
            fatal("out of memory splicing MIR compound literal");
        memcpy(captured, &mir.insns[capture_start],
               (size_t)captured_count * sizeof(*captured));
        middle_count = capture_start - placeholder - 1;
        memmove(&mir.insns[placeholder + captured_count + 1],
                &mir.insns[placeholder + 1],
                (size_t)middle_count * sizeof(*mir.insns));
        memcpy(&mir.insns[placeholder], captured,
               (size_t)captured_count * sizeof(*captured));
        free(captured);
        placeholder += captured_count;
    }
    mir.insns[placeholder] = address;
    mir.insns[placeholder].memory_flags &= ~8;
}

void mir_capture_initializer(const struct AstNode *expr)
{
    struct MirInsn *store;
    int value;

    if (!mir.active)
        return;
    if (mir.init_expression_target != NULL) {
        struct Sym *target = mir.init_expression_target;
        int offset = mir.init_expression_offset;
        int target_type = mir.init_expression_type;
        mir.init_expression_target = NULL;
        {
            int aggregate_type = type_ptr_depth(target->type) > 0
                ? type_decay_ptr(target->type) : target->type;
            if (type_is_struct_object(aggregate_type)) {
            int layout_type = mir_initializer_type_at_offset(aggregate_type,
                                                             offset);
            if (layout_type != 0)
                target_type = layout_type;
            }
        }
        value = mir_lower_expr(expr);
        if (!type_is_struct_object(target_type))
            value = mir_lower_conversion(value, target_type);
        store = mir_emit(MIR_STORE);
        store->src1 = value;
        store->type = target_type;
        store->immediate = offset;
        store->memory_size = type_size(target_type);
        mir_copy_name(store->name, target->name);
        store->object = offset == 0 ? mir_get_object(target, store->name) : -1;
        return;
    }
    if (mir.vla_target != NULL) {
        struct Sym *target = mir.vla_target;
        mir.vla_target = NULL;
        value = mir_lower_expr(expr);
        if (target->elem_size > 1) {
            int scale = mir_new_value();
            int bytes = mir_new_value();
            struct MirInsn *insn = mir_emit(MIR_CONST);
            insn->dst = scale;
            insn->type = TYPE_INT;
            insn->immediate = target->elem_size;
            insn = mir_emit(MIR_BINARY);
            insn->dst = bytes;
            insn->src1 = value;
            insn->src2 = scale;
            insn->type = TYPE_INT;
            insn->immediate = '*';
            value = bytes;
        }
        store = mir_emit(MIR_VLA_ALLOC);
        store->src1 = value;
        store->type = target->type;
        store->immediate = target->offset;
        store->secondary_offset = target->vla_size_offset;
        store->memory_size = target->elem_size;
        mir_copy_name(store->name, target->name);
        if (!mir.declaration_active) {
            store->memory_flags |= 128;
            store->label = mir.vla_capture_start;
        }
        return;
    }
    if (mir.initializer_target == NULL)
        return;
    value = mir_lower_expr(expr);
    if (!type_is_struct_object(mir.initializer_target->type))
        value = mir_lower_conversion(value, mir.initializer_target->type);
    store = mir_emit(MIR_STORE);
    store->src1 = value;
    store->type = mir.initializer_target->type;
    mir_copy_name(store->name, mir.initializer_target->name);
    store->object = mir_get_object(mir.initializer_target, store->name);
    if (!mir.declaration_active) {
        store->memory_flags |= 128;
        store->label = mir.initializer_capture_start;
    }
    mir.initializer_target = NULL;
}

void mir_capture_struct_initializer(struct Sym *target,
                                    const struct AstNode *expr)
{
    struct MirInsn *insn;
    int destination;
    int source;

    if (!mir.active || target == NULL || expr == NULL)
        return;
    if (expr->kind == AST_CALL && type_is_struct_object(target->type)) {
        (void)mir_lower_aggregate_call_address(expr, target);
        return;
    }
    destination = mir_new_value();
    insn = mir_emit(MIR_ADDRESS);
    insn->dst = destination;
    insn->type = type_add_ptr(target->type);
    insn->object = mir_get_object(target, target->name);
    mir_copy_name(insn->name, target->name);
    source = mir_lower_expr(expr);
    if (source < 0) {
        insn = mir_emit(MIR_OPAQUE);
        insn->immediate = AST_DECL;
        return;
    }
    insn = mir_emit(MIR_COPY_AGGREGATE);
    insn->src1 = destination;
    insn->src2 = source;
    insn->type = target->type;
    insn->memory_size = type_size(target->type);
}

static struct MirInsn *mir_mutable_definition(int value)
{
    int i;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].dst == value)
            return &mir.insns[i];
    return NULL;
}

static void mir_replace_value_uses(int old_value, int new_value)
{
    int i;
    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].src1 == old_value)
            mir.insns[i].src1 = new_value;
        if (mir.insns[i].src2 == old_value)
            mir.insns[i].src2 = new_value;
    }
}

static int mir_named_type(const char *name)
{
    struct Sym *global;
    int object;
    int i;

    if (name == NULL || name[0] == 0)
        return 0;
    object = mir_find_object(name);
    if (object >= 0)
        return mir.objects[object].type;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_types[i];
    global = find_global(name);
    if (global != NULL)
        return global->type;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].name[0] != 0 &&
            strcmp(mir.insns[i].name, name) == 0 &&
            mir.insns[i].type != 0 &&
            (mir.insns[i].opcode == MIR_STORE ||
             mir.insns[i].opcode == MIR_PARAM ||
             mir.insns[i].opcode == MIR_LOAD))
            return mir.insns[i].type;
    return 0;
}

static int mir_declared_location(const char *name, int *type, int *storage,
                                 int *offset)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0) {
            if (type != NULL)
                *type = mir.declared_types[i];
            if (storage != NULL)
                *storage = mir.declared_storage[i];
            if (offset != NULL)
                *offset = mir.declared_offsets[i];
            return 1;
        }
    return 0;
}

static const char *mir_declared_link_name(const char *name)
{
    int declared;
    for (declared = 0; declared < mir.declared_count; ++declared)
        if (strcmp(mir.declared_names[declared], name) == 0 &&
            mir.declared_link_names[declared][0] != 0)
            return mir.declared_link_names[declared];
    return name;
}

static int mir_declared_elem_size(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_elem_sizes[i];
    return 0;
}

static int mir_declared_object_size(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_sizes[i];
    return 0;
}

static int mir_declared_subobject_size(const char *name, int depth)
{
    int i;
    int dimension;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0) {
            int size = mir.declared_sizes[i];
            for (dimension = 0;
                 dimension < depth &&
                 dimension < mir.declared_dim_counts[i];
                 ++dimension) {
                if (mir.declared_dims[i][dimension] <= 0)
                    return 0;
                size /= mir.declared_dims[i][dimension];
            }
            return size;
        }
    return 0;
}

static int mir_declared_has_dynamic_stride(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_dynamic_strides[i];
    return 0;
}

static int mir_declared_is_vla_object(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_is_vla[i];
    return 0;
}

static int mir_declared_is_array_object(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_is_array[i] && !mir.declared_is_vla[i];
    return 0;
}

static int mir_declared_is_pointer_array(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return type_ptr_depth(mir.declared_types[i]) > 0 &&
                   (mir.declared_dim_counts[i] > 0 ||
                    mir.declared_elem_sizes[i] >
                        type_size(type_decay_ptr(mir.declared_types[i])));
    return 0;
}

static int mir_named_pointer_has_unique_array_address(const char *name)
{
    int instruction;
    int stores = 0;
    int array_source = 0;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *store = &mir.insns[instruction];
        const struct MirInsn *source;
        if (store->opcode != MIR_STORE || strcmp(store->name, name) != 0)
            continue;
        ++stores;
        source = mir_definition(store->src1);
        while (source != NULL && source->opcode == MIR_UNARY &&
               source->immediate == 0)
            source = mir_definition(source->src1);
        if (source != NULL && source->opcode == MIR_ADDRESS &&
            (source->memory_flags & 1024) != 0 && source->name[0] != 0) {
            struct Sym *global = find_global(source->name);
            if ((global != NULL && global->is_array) ||
                mir_declared_is_array_object(source->name))
                array_source = 1;
        }
    }
    return stores == 1 && array_source;
}

static int mir_declared_vla_size_offset(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_vla_size_offsets[i];
    return 0;
}

static int mir_declared_constant(const char *name, int *type,
                                 unsigned long *value)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0 &&
            mir.declared_is_const[i]) {
            if (type != NULL)
                *type = mir.declared_types[i];
            if (value != NULL)
                *value = mir.declared_const_values[i];
            return 1;
        }
    return 0;
}

static int mir_declared_is_function_pointer(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return mir.declared_is_funcptr[i] ||
                   (mir.declared_storage[i] != SC_FUNC &&
                    type_ptr_depth(mir.declared_types[i]) > 0);
    return 0;
}

static int mir_declared_index(const char *name)
{
    int i;
    for (i = 0; i < mir.declared_count; ++i)
        if (strcmp(mir.declared_names[i], name) == 0)
            return i;
    return -1;
}

static struct MirInsn *mir_insert_instruction_before(int index, int opcode)
{
    struct MirInsn inserted;

    if (index < 0 || index > mir.count)
        return NULL;
    inserted = *mir_emit(opcode);
    memmove(&mir.insns[index + 1], &mir.insns[index],
            (size_t)(mir.count - index - 1) * sizeof(*mir.insns));
    mir.insns[index] = inserted;
    return &mir.insns[index];
}

static void mir_resolve_deferred_metadata(void)
{
    int i;

    for (i = mir.alias_count - 1; i >= 0; --i) {
        int internal_object = mir_find_object(mir.alias_internal_names[i]);
        int declaration = mir.alias_declaration_indices[i];
        int first = 0;
        int last = mir.count;
        int instruction;
        if (declaration >= 0 && declaration < mir.declaration_count) {
            first = mir.declaration_placeholders[declaration];
            last = mir.declaration_scope_ends[declaration];
            if (first < 0)
                first = 0;
            if (last < first || last > mir.count)
                last = mir.count;
            for (instruction = first; instruction < last; ++instruction)
                if (mir.insns[instruction].opcode == MIR_BRANCH_FALSE) {
                    int target = mir_find_label(mir.insns[instruction].label);
                    if (target > instruction) {
                        last = target + 1;
                        break;
                    }
                }
        }
        for (instruction = first; instruction < last; ++instruction) {
            struct MirInsn *insn = &mir.insns[instruction];
            if (strcmp(insn->name, mir.alias_source_names[i]) == 0) {
                mir_copy_name(insn->name, mir.alias_internal_names[i]);
                insn->object = internal_object;
            }
            if (strcmp(insn->base_name, mir.alias_source_names[i]) == 0)
                mir_copy_name(insn->base_name, mir.alias_internal_names[i]);
        }
    }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_OBJECT_MERGE &&
            mir.insns[i].object < 0)
            mir.insns[i].opcode = MIR_LOAD;

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        int named_type;
        if (insn->name[0] == 0)
            continue;
        named_type = mir_named_type(insn->name);
        if (named_type == 0)
            continue;
        if (insn->opcode == MIR_LOAD &&
            mir_declared_is_array_object(insn->name)) {
            insn->opcode = MIR_ADDRESS;
            insn->type = type_add_ptr(named_type);
        } else if (insn->opcode == MIR_LOAD || insn->opcode == MIR_PARAM)
            insn->type = named_type;
        else if (insn->opcode == MIR_ADDRESS)
            insn->type = type_add_ptr(named_type);
        else if (insn->opcode == MIR_UNARY &&
                 (insn->memory_flags & 512) != 0)
            insn->type = named_type;
        else if (insn->opcode == MIR_STORE && insn->immediate == 0 &&
                 !type_is_struct_object(named_type)) {
            insn->type = named_type;
            insn->memory_size = type_size(named_type);
        }
    }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *call = &mir.insns[i];
        struct MirInsn *load;
        char callee_name[64];
        int declaration;
        int callee_value;
        int callee_type;
        if (call->opcode != MIR_CALL || call->name[0] == 0 ||
            strcmp(call->name, "<indirect>") == 0 ||
            !mir_declared_is_function_pointer(call->name))
            continue;
        mir_copy_name(callee_name, call->name);
        declaration = mir_declared_index(callee_name);
        callee_type = mir_named_type(callee_name);
        callee_value = mir_new_value();
        load = mir_insert_instruction_before(i, MIR_LOAD);
        if (load == NULL)
            fatal("cannot insert deferred function-pointer load");
        load->dst = callee_value;
        load->type = callee_type;
        load->object = mir_find_object(callee_name);
        mir_copy_name(load->name, callee_name);
        call = &mir.insns[i + 1];
        call->src1 = callee_value;
        call->type = type_decay_ptr(callee_type);
        mir_copy_name(call->name, "<indirect>");
        if (declaration >= 0 && mir.declared_has_proto[declaration]) {
            int argument;
            for (argument = 0; argument < mir.count; ++argument)
                if (mir.insns[argument].opcode == MIR_ARG &&
                    mir.insns[argument].secondary_offset ==
                        call->secondary_offset &&
                    mir.insns[argument].immediate >= 0 &&
                    mir.insns[argument].immediate <
                        mir.declared_proto_nargs[declaration])
                    mir.insns[argument].type =
                        mir.declared_proto_types[declaration]
                                                [mir.insns[argument].immediate];
        }
        ++i;
    }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *left;
        struct MirInsn *right;
        int comparison;
        if (insn->opcode != MIR_BINARY || insn->src1 < 0 || insn->src2 < 0)
            continue;
        comparison = insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
                     insn->immediate == '<' || insn->immediate == '>' ||
                     insn->immediate == TOK_LE || insn->immediate == TOK_GE;
        left = mir_mutable_definition(insn->src1);
        right = mir_mutable_definition(insn->src2);
        if (left != NULL && right != NULL &&
            (insn->immediate == '/' || insn->immediate == '%') &&
            type_ptr_depth(left->type) == 0 &&
            type_ptr_depth(right->type) == 0) {
            insn->type = common_arith_type(left->type, right->type);
            insn->secondary_offset = insn->type;
        }
        if (left != NULL && right != NULL &&
            type_size(left->type) > type_size(insn->secondary_offset)) {
            insn->secondary_offset = left->type;
            if (!comparison)
                insn->type = left->type;
        }
        if (right != NULL &&
            type_size(right->type) > type_size(insn->secondary_offset)) {
            insn->secondary_offset = right->type;
            if (!comparison)
                insn->type = right->type;
        }
        if (!type_is_float(insn->secondary_offset)) {
            if (left != NULL && left->opcode == MIR_CONST &&
                type_size(left->type) < type_size(insn->secondary_offset))
                left->type = insn->secondary_offset;
            if (right != NULL && right->opcode == MIR_CONST &&
                type_size(right->type) < type_size(insn->secondary_offset))
                right->type = insn->secondary_offset;
        }
    }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_ARG && mir.insns[i].src1 >= 0) {
            struct MirInsn *source =
                mir_mutable_definition(mir.insns[i].src1);
            if (source != NULL && source->type != 0 &&
                !type_is_struct_object(mir.insns[i].type))
                mir.insns[i].type = source->type;
        }

    for (i = 0; i < mir.count; ++i)
        if ((mir.insns[i].opcode == MIR_CALL ||
             mir.insns[i].opcode == MIR_CALL_AGGREGATE) &&
            strcmp(mir.insns[i].name, "<indirect>") != 0) {
            struct Sym *callee = find_global(mir.insns[i].name);
            int argument;
            if (callee == NULL)
                continue;
            for (argument = 0; argument < mir.count; ++argument)
                if (mir.insns[argument].opcode == MIR_ARG &&
                    mir.insns[argument].secondary_offset ==
                        mir.insns[i].secondary_offset &&
                    mir.insns[argument].immediate >= 0 &&
                    mir.insns[argument].immediate < callee->proto_nargs)
                    mir.insns[argument].type = callee->proto_types[
                        mir.insns[argument].immediate];
        }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_LOAD &&
            type_is_struct_object(mir.insns[i].type)) {
            mir.insns[i].opcode = MIR_ADDRESS;
            mir.insns[i].type = type_add_ptr(mir.insns[i].type);
        }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_LOAD &&
            mir.insns[i].name[0] != 0) {
            int constant_type;
            unsigned long constant_value;
            if (mir_declared_constant(mir.insns[i].name, &constant_type,
                                      &constant_value)) {
                mir.insns[i].opcode = type_is_float(constant_type)
                    ? MIR_FLOAT_CONST : MIR_CONST;
                mir.insns[i].type = constant_type;
                mir.insns[i].immediate = (long)constant_value;
                mir.insns[i].object = -1;
            }
        }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_STORE && mir.insns[i].src1 >= 0) {
            int target_type;
            int target_storage;
            int target_offset;
            struct MirInsn *conversion =
                mir_mutable_definition(mir.insns[i].src1);
            struct MirInsn *call = conversion;
            struct Sym *callee;
            if (conversion != NULL && conversion->opcode == MIR_UNARY &&
                conversion->immediate == 0 &&
                (conversion->memory_flags & 512) != 0)
                call = mir_mutable_definition(conversion->src1);
            if (!mir_scalar_memory_location(&mir.insns[i], &target_type,
                                            &target_storage, &target_offset) ||
                !type_is_struct_object(target_type) || call == NULL ||
                call->opcode != MIR_CALL)
                continue;
            callee = find_global(call->name);
            if (callee == NULL || !type_is_struct_object(callee->type) ||
                (target_storage != SC_LOCAL && target_storage != SC_PARAM &&
                 target_storage != SC_GLOBAL && target_storage != SC_EXTERN))
                continue;
            call->opcode = MIR_CALL_AGGREGATE;
            call->type = type_add_ptr(target_type);
            call->immediate = (target_storage == SC_GLOBAL ||
                               target_storage == SC_EXTERN)
                ? MIR_AGGREGATE_GLOBAL_DEST_OFFSET : target_offset;
            call->memory_size = type_size(target_type);
            mir_copy_name(call->base_name, mir.insns[i].name);
            mir.insns[i].opcode = MIR_NOP;
            mir.insns[i].src1 = -1;
            if (conversion != call) {
                conversion->opcode = MIR_NOP;
                conversion->dst = -1;
                conversion->src1 = -1;
            }
        }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        int object;
           if ((insn->opcode != MIR_LOAD && insn->opcode != MIR_STORE &&
               insn->opcode != MIR_ADDRESS) ||
            insn->object >= 0 || insn->name[0] == 0)
            continue;
        object = mir_find_object(insn->name);
        if (object >= 0) {
            insn->object = object;
            if (insn->type == 0)
                insn->type = insn->opcode == MIR_ADDRESS
                    ? type_add_ptr(mir.objects[object].type)
                    : mir.objects[object].type;
        }
    }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_COMPOUND_ADDRESS &&
            (mir.insns[i].memory_flags & 8) != 0) {
            mir.insns[i].opcode = MIR_OPAQUE;
            mir.insns[i].immediate = AST_COMPOUND_LITERAL;
        }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_VLA_SIZE &&
            (mir.insns[i].memory_flags & 8) != 0) {
            int offset = 0;
            int prior;
            if (mir.insns[i].bit_width > 0) {
                int subobject_size =
                    mir_declared_is_array_object(mir.insns[i].name)
                    ? mir_declared_subobject_size(
                          mir.insns[i].name, mir.insns[i].bit_width)
                    : 0;
                mir.insns[i].opcode = MIR_CONST;
                mir.insns[i].immediate = subobject_size > 0
                    ? subobject_size : mir.insns[i].secondary_offset;
                mir.insns[i].secondary_offset = 0;
                mir.insns[i].bit_width = 0;
                mir.insns[i].memory_flags &= ~8;
                continue;
            }
            for (prior = i - 1; prior >= 0; --prior)
                if (mir.insns[prior].opcode == MIR_VLA_ALLOC &&
                    strcmp(mir.insns[prior].name, mir.insns[i].name) == 0) {
                    offset = mir.insns[prior].secondary_offset;
                    break;
                }
            if (offset == 0)
                offset = mir_declared_vla_size_offset(mir.insns[i].name);
            if (offset != 0) {
                mir.insns[i].immediate = offset;
                mir.insns[i].memory_flags &= ~8;
            } else {
                mir.insns[i].opcode = MIR_CONST;
                mir.insns[i].immediate =
                    mir_declared_is_array_object(mir.insns[i].name) &&
                    mir_declared_object_size(mir.insns[i].name) > 0
                    ? (mir.insns[i].bit_width > 0
                       ? mir_declared_subobject_size(
                             mir.insns[i].name, mir.insns[i].bit_width)
                       : mir_declared_object_size(mir.insns[i].name))
                    : mir.insns[i].secondary_offset;
                mir.insns[i].secondary_offset = 0;
                mir.insns[i].bit_width = 0;
                mir.insns[i].memory_flags &= ~8;
            }
        }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *base_definition;
        struct FieldDef *field;
        int base_type = 0;
        int object;

        if (insn->opcode != MIR_MEMBER_ADDRESS ||
            (insn->memory_flags & 8) == 0)
            continue;
        object = insn->base_name[0] != 0
            ? mir_find_object(insn->base_name) : -1;
        if (object >= 0)
            base_type = mir.objects[object].type;
        else if (insn->base_name[0] != 0)
            base_type = mir_named_type(insn->base_name);
        if (base_type == 0) {
            base_definition = mir_mutable_definition(insn->src1);
            if (base_definition != NULL)
                base_type = base_definition->type;
        }
        if (type_ptr_depth(base_type) > 0)
            base_type = type_decay_ptr(base_type);
        field = find_field_def(base_struct_id_from_type(base_type), insn->name);
        if (field == NULL)
            field = ast_unique_field_by_name(insn->name);
        if (field == NULL) {
            insn->opcode = MIR_OPAQUE;
            insn->immediate = AST_MEMBER;
            continue;
        }
        insn->immediate = field->offset;
        insn->type = type_add_ptr(field->type);
        mir_set_field_memory(insn, field);
    }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *base;
        int declared_type;
        int declared_size;
        int element_size;
        struct Sym *global;
        int old_size;
        if (insn->opcode != MIR_INDEX_ADDRESS || insn->src1 < 0)
            continue;
        base = mir_mutable_definition(insn->src1);
        if (base == NULL || base->opcode != MIR_ADDRESS ||
            base->name[0] == 0)
            continue;
        declared_type = mir_named_type(base->name);
        declared_size = type_size(declared_type);
        element_size = mir_declared_elem_size(base->name);
        global = find_global(base->name);
        if (element_size <= 0 && global != NULL && global->elem_size > 0)
            element_size = global->elem_size;
        if (element_size > 0)
            declared_size = element_size;
        if (declared_size <= 0)
            continue;
        old_size = insn->memory_size;
        insn->type = type_add_ptr(declared_type);
        insn->memory_size = declared_size;
        if (insn->immediate == old_size ||
            (element_size > 0 && insn->immediate < element_size))
            insn->immediate = declared_size;
    }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_INDEX_ADDRESS &&
            mir.insns[i].src1 >= 0) {
            struct MirInsn *base =
                mir_mutable_definition(mir.insns[i].src1);
            if (base != NULL && base->opcode != MIR_MEMBER_ADDRESS &&
                base->opcode != MIR_INDEX_ADDRESS &&
                type_ptr_depth(base->type) > 0) {
                int named_base = (base->opcode == MIR_ADDRESS ||
                                  base->opcode == MIR_LOAD) &&
                                 base->name[0] != 0;
                struct Sym *global = named_base
                    ? find_global(base->name) : NULL;
                int stride = named_base &&
                             base->name[0] != 0 &&
                             !mir_declared_has_dynamic_stride(base->name)
                    ? mir_declared_elem_size(base->name) : 0;
                if (stride <= 0 && global != NULL &&
                    global->dim_count > 0 && global->elem_size > 0)
                    stride = mir.insns[i].immediate;
                if (stride <= 0 &&
                    !(named_base &&
                      mir_declared_has_dynamic_stride(base->name)))
                    stride = type_index_elem_size(base->type);
                if (stride > 0)
                    mir.insns[i].immediate = stride;
            }
        }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_BINARY &&
            (mir.insns[i].immediate == '+' ||
             mir.insns[i].immediate == '-') &&
            type_ptr_depth(mir.insns[i].type) == 0) {
            struct MirInsn *left =
                mir_mutable_definition(mir.insns[i].src1);
            struct MirInsn *right =
                mir_mutable_definition(mir.insns[i].src2);
            struct MirInsn *pointer = NULL;
            struct MirInsn *offset = NULL;
            if (left != NULL && type_ptr_depth(left->type) > 0 &&
                right != NULL && type_ptr_depth(right->type) == 0) {
                pointer = left;
                offset = right;
            } else if (mir.insns[i].immediate == '+' && right != NULL &&
                       type_ptr_depth(right->type) > 0 && left != NULL &&
                       type_ptr_depth(left->type) == 0) {
                pointer = right;
                offset = left;
            }
            if (pointer != NULL && offset != NULL &&
                offset->opcode == MIR_CONST &&
                mir_value_use_count(offset->dst) == 1) {
                int stride = type_index_elem_size(pointer->type);
                if (stride > 1)
                    offset->immediate *= stride;
                mir.insns[i].type = pointer->type;
                mir.insns[i].secondary_offset = TYPE_INT;
            }
        }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *address;
        if ((insn->opcode != MIR_LOAD_INDIRECT &&
             insn->opcode != MIR_STORE_INDIRECT) ||
            (insn->memory_flags & 8) == 0)
            continue;
        address = mir_mutable_definition(insn->src1);
        if (address == NULL || address->opcode == MIR_OPAQUE)
            continue;
        if (address->opcode != MIR_MEMBER_ADDRESS)
            continue;
        if ((address->memory_flags & 2) != 0 &&
            insn->opcode == MIR_LOAD_INDIRECT) {
            mir_replace_value_uses(insn->dst, insn->src1);
            insn->opcode = MIR_NOP;
            insn->dst = -1;
            continue;
        }
        insn->memory_size = address->memory_size;
        insn->memory_flags = address->memory_flags;
        insn->bit_width = address->bit_width;
        insn->bit_shift = address->bit_shift;
        insn->bit_mask = address->bit_mask;
    }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *address;
        int pointee_type;
        if ((insn->opcode != MIR_LOAD_INDIRECT &&
             insn->opcode != MIR_STORE_INDIRECT) ||
            insn->bit_width > 0 || insn->src1 < 0)
            continue;
        address = mir_mutable_definition(insn->src1);
        if (address == NULL || type_ptr_depth(address->type) == 0)
            continue;
        if (insn->opcode == MIR_LOAD_INDIRECT &&
            address->opcode == MIR_LOAD && address->name[0] != 0 &&
            (mir_declared_is_pointer_array(address->name) ||
             mir_named_pointer_has_unique_array_address(address->name))) {
            mir_replace_value_uses(insn->dst, insn->src1);
            insn->opcode = MIR_NOP;
            insn->dst = -1;
            continue;
        }
        if ((insn->memory_flags & 256) != 0)
            continue;
        pointee_type = type_decay_ptr(address->type);
        if (type_size(pointee_type) <= 0)
            continue;
        if (insn->opcode == MIR_LOAD_INDIRECT &&
            type_ptr_depth(pointee_type) == 0 &&
            type_is_struct_object(pointee_type)) {
            mir_replace_value_uses(insn->dst, insn->src1);
            insn->opcode = MIR_NOP;
            insn->dst = -1;
            continue;
        }
        insn->type = pointee_type;
        insn->memory_size = type_size(pointee_type);
    }

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_INDEX_ADDRESS &&
            mir.insns[i].src1 >= 0) {
            struct MirInsn *base =
                mir_mutable_definition(mir.insns[i].src1);
            if (base != NULL && base->opcode == MIR_LOAD_INDIRECT &&
                type_ptr_depth(base->type) > 0) {
                int stride = type_index_elem_size(base->type);
                if (stride > 0)
                    mir.insns[i].immediate = stride;
            }
        }

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_UNARY && insn->immediate == 0 &&
            type_is_struct_object(insn->type) && insn->src1 >= 0) {
            struct MirInsn *source = mir_mutable_definition(insn->src1);
            if (source != NULL && type_ptr_depth(source->type) > 0) {
                mir_replace_value_uses(insn->dst, insn->src1);
                insn->opcode = MIR_NOP;
                insn->dst = -1;
            }
        }
    }
    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *source;
        if (insn->opcode != MIR_UNARY || insn->immediate != 0 ||
            insn->src1 < 0)
            continue;
        source = mir_mutable_definition(insn->src1);
        if (insn->memory_flags != 0 &&
            !(insn->memory_flags == 512 && source != NULL &&
              source->type == insn->type))
            continue;
        if (source == NULL || type_size(source->type) != 2 ||
            type_size(insn->type) != 2 || type_is_float(source->type) ||
            type_is_float(insn->type) || type_is_struct_object(source->type) ||
            type_is_struct_object(insn->type))
            continue;
        mir_replace_value_uses(insn->dst, insn->src1);
        insn->opcode = MIR_NOP;
        insn->dst = -1;
        insn->src1 = -1;
        insn->src2 = -1;
    }
    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *source;
        unsigned long bits;
        if (insn->opcode != MIR_UNARY || insn->src1 < 0 ||
            type_size(insn->type) != 2)
            continue;
        source = mir_mutable_definition(insn->src1);
        if (source == NULL || source->opcode != MIR_CONST)
            continue;
        bits = (unsigned long)source->immediate & 0xffffUL;
        if (insn->immediate == '-')
            bits = (0UL - bits) & 0xffffUL;
        else if (insn->immediate == '~')
            bits = (~bits) & 0xffffUL;
        else if (insn->immediate == '!')
            bits = bits == 0;
        else if (insn->immediate != 0 && insn->immediate != '+')
            continue;
        insn->opcode = MIR_CONST;
        insn->src1 = -1;
        insn->src2 = -1;
        insn->immediate = (long)bits;
    }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_STORE_INDIRECT &&
            mir.insns[i].memory_size > 4)
            mir.insns[i].opcode = MIR_COPY_AGGREGATE;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_STORE_INDIRECT &&
            mir.insns[i].src1 >= 0 && mir.insns[i].src2 >= 0) {
            struct MirInsn *destination = mir_mutable_definition(
                mir.insns[i].src1);
            struct MirInsn *source = mir_mutable_definition(
                mir.insns[i].src2);
            if (destination != NULL && source != NULL &&
                type_ptr_depth(destination->type) > 0 &&
                type_ptr_depth(source->type) > 0 &&
                type_is_struct_object(type_decay_ptr(destination->type)) &&
                type_is_struct_object(type_decay_ptr(source->type))) {
                mir.insns[i].opcode = MIR_COPY_AGGREGATE;
                mir.insns[i].memory_size = type_size(
                    type_decay_ptr(destination->type));
            }
        }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_COPY_AGGREGATE &&
            mir.insns[i].src2 >= 0) {
            struct MirInsn *call = mir_mutable_definition(mir.insns[i].src2);
            struct Sym *callee;
            if (call == NULL || call->opcode != MIR_CALL)
                continue;
            callee = find_global(call->name);
            if (callee == NULL || !type_is_struct_object(callee->type))
                continue;
            call->opcode = MIR_CALL_AGGREGATE;
            call->src1 = mir.insns[i].src1;
            call->type = type_add_ptr(callee->type);
            call->immediate = MIR_AGGREGATE_VALUE_DEST_OFFSET;
            call->memory_size = type_size(callee->type);
            mir.insns[i].opcode = MIR_NOP;
            mir.insns[i].src1 = -1;
            mir.insns[i].src2 = -1;
        }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_STORE_INDIRECT &&
            mir.insns[i].src1 >= 0 && mir.insns[i].src2 >= 0) {
            struct MirInsn *call = mir_mutable_definition(mir.insns[i].src2);
            struct Sym *callee;
            if (call == NULL || call->opcode != MIR_CALL)
                continue;
            callee = find_global(call->name);
            if (callee == NULL || !type_is_struct_object(callee->type))
                continue;
            call->opcode = MIR_CALL_AGGREGATE;
            call->src1 = mir.insns[i].src1;
            call->type = type_add_ptr(callee->type);
            call->immediate = MIR_AGGREGATE_VALUE_DEST_OFFSET;
            call->memory_size = type_size(callee->type);
            mir.insns[i].opcode = MIR_NOP;
            mir.insns[i].src1 = -1;
            mir.insns[i].src2 = -1;
        }
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
           (mir.insns[successor].opcode == MIR_LABEL ||
            mir.insns[successor].opcode == MIR_NOP))
        ++successor;
    return successor;
}

static int mir_phi_edge_uses_value(int predecessor, int successor, int value)
{
    int first = mir_first_nonlabel_successor(successor);
    int predecessor_label;

    if (first < 0 || first >= mir.count || mir.insns[first].opcode != MIR_PHI)
        return 0;
    predecessor_label = mir_block_label_before(predecessor);
    while (first < mir.count) {
        const struct MirInsn *phi = &mir.insns[first];
        if (phi->opcode == MIR_NOP) {
            ++first;
            continue;
        }
        if (phi->opcode != MIR_PHI)
            break;
        if (predecessor_label == phi->phi_pred1 && value == phi->src1)
            return 1;
        if (predecessor_label == phi->phi_pred2 && value == phi->src2)
            return 1;
        ++first;
    }
    return 0;
}

static int mir_call_uses_value(const struct MirInsn *call, int value)
{
    int i;
    if (call->opcode != MIR_CALL && call->opcode != MIR_CALL_AGGREGATE)
        return 0;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_ARG &&
            mir.insns[i].secondary_offset == call->secondary_offset &&
            mir.insns[i].src1 == value)
            return 1;
    return 0;
}

static int mir_value_has_use(int value)
{
    int instruction;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value ||
            mir_call_uses_value(insn, value))
            return 1;
    }
    return 0;
}

static int mir_value_has_use_after(int value, int instruction)
{
    int i;

    for (i = instruction + 1; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->src1 == value || insn->src2 == value ||
            mir_call_uses_value(insn, value))
            return 1;
    }
    return 0;
}

static int mir_value_use_count(int value)
{
    int count = 0;
    int instruction;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value)
            ++count;
        if (insn->src2 == value)
            ++count;
        if (mir_call_uses_value(insn, value))
            ++count;
    }
    return count;
}

#define MIR_OBJECT_UNDEFINED (-1)
#define MIR_OBJECT_AMBIGUOUS (-2)
#define MIR_OBJECT_UNREACHED (-3)

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
    else if ((insn->opcode == MIR_VA_START || insn->opcode == MIR_VA_END ||
              insn->opcode == MIR_VA_ARG) && insn->object >= 0)
        output[insn->object] = MIR_OBJECT_UNDEFINED;
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
        in_state[i] = MIR_OBJECT_UNREACHED;
        out_state[i] = MIR_OBJECT_UNREACHED;
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
                            for (object = 0; object < mir.object_count; ++object) {
                                if (next_state[object] == MIR_OBJECT_UNREACHED)
                                    next_state[object] = predecessor_out[object];
                                else if (predecessor_out[object] ==
                                         MIR_OBJECT_UNREACHED)
                                    continue;
                                else if (next_state[object] !=
                                         predecessor_out[object])
                                    next_state[object] = MIR_OBJECT_AMBIGUOUS;
                            }
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
    case MIR_CALL_AGGREGATE:
    case MIR_BINARY:
    case MIR_UNARY:
    case MIR_INDEX_LOAD:
    case MIR_VLA_SIZE:
    case MIR_INDEX_ADDRESS:
    case MIR_MEMBER_ADDRESS:
    case MIR_LOAD_INDIRECT:
    case MIR_VA_ARG:
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
static void mir_allocate_registers(const unsigned char *live_in,
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
    if (mir.allocation_capacity < value_count) {
        int *new_colors = (int *)realloc(
            mir.allocation_colors, (size_t)value_count * sizeof(*new_colors));
        int *new_spills = (int *)realloc(
            mir.allocation_spills, (size_t)value_count * sizeof(*new_spills));
        if (new_colors == NULL || new_spills == NULL)
            fatal("out of memory retaining MIR allocation");
        mir.allocation_colors = new_colors;
        mir.allocation_spills = new_spills;
        mir.allocation_capacity = value_count;
    }
    for (i = 0; i < value_count; ++i) {
        mir.allocation_colors[i] = -1;
        mir.allocation_spills[i] = -1;
    }
    mir.allocation_spill_count = 0;
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
            mir.insns[i].opcode == MIR_CALL_AGGREGATE ||
            mir.insns[i].opcode == MIR_OPAQUE) {
            for (value = 0; value < value_count; ++value) {
                if (!in[value] || !out[value])
                    continue;
                if (mir.insns[i].opcode == MIR_CALL ||
                    mir.insns[i].opcode == MIR_CALL_AGGREGATE)
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
            mir.allocation_spills[value] = mir.allocation_spill_count++;
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
            mir.allocation_spills[value] = mir.allocation_spill_count++;
        } else {
            color[value] = chosen;
            mir.allocation_colors[value] = chosen;
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
    int opaque_kinds[AST_DIVMOD_CALL + 1];
    int promoted_objects;
    struct MirAllocationSummary allocation;
    int changed;
    int i;

    if (mir.next_value < 0) {
        fprintf(stderr, "; MIR %s: invalid virtual value count (%d)\n",
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

    memset(opaque_kinds, 0, sizeof(opaque_kinds));
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
            insn->src1 >= 0 && !defined[insn->src1]) {
            if (mir.report_mode)
                fprintf(stderr, "; MIR %s: instruction %d uses undefined v%d\n",
                        mir.name, i, insn->src1);
            ++errors;
        }
        if (insn->opcode != MIR_PHI &&
            insn->src2 >= 0 && !defined[insn->src2]) {
            if (mir.report_mode)
                fprintf(stderr, "; MIR %s: instruction %d uses undefined v%d\n",
                        mir.name, i, insn->src2);
            ++errors;
        }
        if (insn->dst >= 0) {
            if (defined[insn->dst]) {
                if (mir.report_mode)
                    fprintf(stderr, "; MIR %s: instruction %d redefines v%d\n",
                            mir.name, i, insn->dst);
                ++errors;
            }
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
                if (mir_call_uses_value(insn, value))
                    next_in = 1;
                if (out[value] != next_out || in[value] != next_in) {
                    out[value] = (unsigned char)next_out;
                    in[value] = (unsigned char)next_in;
                    changed = 1;
                }
            }
        }
    } while (changed);

    mir_allocate_registers(live_in, live_out, &allocation);

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
        if (insn->opcode == MIR_OPAQUE) {
            ++opaque_count;
            if (insn->immediate >= AST_NONE &&
                insn->immediate <= AST_DIVMOD_CALL)
                ++opaque_kinds[insn->immediate];
        }
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
        if (insn->dst >= 0 && insn->type != 0)
            fprintf(stderr, " type=%d", insn->type);
        if (insn->memory_size > 0)
            fprintf(stderr, " mem=%d%s", insn->memory_size,
                    (insn->memory_flags & 1) != 0 ? "v" : "");
        if (insn->bit_width > 0)
            fprintf(stderr, " bit=%d:%d/%u", insn->bit_shift,
                    insn->bit_width, insn->bit_mask);
                if (insn->secondary_offset != 0)
                    fprintf(stderr, " off2=%d", insn->secondary_offset);
        if (insn->opcode == MIR_CONST || insn->opcode == MIR_FLOAT_CONST ||
            insn->opcode == MIR_STRING_ADDRESS ||
            insn->opcode == MIR_VLA_SIZE)
            fprintf(stderr, " %ld", insn->immediate);
        if (insn->opcode == MIR_UNARY || insn->opcode == MIR_BINARY)
            fprintf(stderr, " op=%ld", insn->immediate);
        if (insn->opcode == MIR_INDEX_ADDRESS)
            fprintf(stderr, " stride=%ld", insn->immediate);
        if (insn->opcode == MIR_STORE && insn->immediate != 0)
            fprintf(stderr, " off=%ld", insn->immediate);
        if (insn->opcode == MIR_OPAQUE)
            fprintf(stderr, " ast=%ld", insn->immediate);
        if (insn->opcode == MIR_LABEL || insn->opcode == MIR_JUMP ||
            insn->opcode == MIR_BRANCH_FALSE)
            fprintf(stderr, " L%d", insn->label);
        if (insn->opcode == MIR_PHI)
            fprintf(stderr, " [L%d,L%d]", insn->phi_pred1, insn->phi_pred2);
        if (insn->dst >= 0 && insn->dst < mir.next_value) {
            static const char *homes[] = { "hl", "de", "bc", "iy" };
            if (mir.allocation_colors[insn->dst] >= 0)
                fprintf(stderr, " home=%s",
                        homes[mir.allocation_colors[insn->dst]]);
            else if (mir.allocation_spills[insn->dst] >= 0)
                fprintf(stderr, " spill=%d",
                        mir.allocation_spills[insn->dst]);
        }
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
    if (getenv("DCC_MIR_COVERAGE") != NULL && opaque_count != 0) {
        int kind;
        int first = 1;
        fprintf(stderr, "; MIR coverage function=%s opaque=%d kinds=",
                mir.name, opaque_count);
        for (kind = AST_NONE; kind <= AST_DIVMOD_CALL; ++kind)
            if (opaque_kinds[kind] != 0) {
            fprintf(stderr, "%s%s:%d", first ? "" : ",",
                ast_kind_name(kind), opaque_kinds[kind]);
            first = 0;
            }
        fputc('\n', stderr);
    }
    if (getenv("DCC_MIR_REQUIRE_COMPLETE") != NULL && opaque_count != 0)
        ++errors;
    mir.opaque_count = opaque_count;

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

static void mir_emit_iy_prologue(FILE *out)
{
    fputs("\tpush iy\n", out);
    mir_emit_prologue(out);
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

static void mir_emit_scalar_compare(FILE *out, int operation, int is_unsigned)
{
    int true_label = new_label();
    int end_label = new_label();

    if (operation == '>' || operation == TOK_LE) {
        fputs("\tex de,hl\n", out);
        operation = operation == '>' ? '<' : TOK_GE;
    }
    if (!is_unsigned && operation != TOK_EQ && operation != TOK_NE)
        fputs("\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n", out);
    fputs("\tor a\n\tsbc hl,de\n\tld hl,0\n", out);
    if (operation == TOK_EQ)
        fprintf(out, "\tjp z,L%d\n", true_label);
    else if (operation == TOK_NE)
        fprintf(out, "\tjp nz,L%d\n", true_label);
    else if (operation == '<')
        fprintf(out, "\tjp c,L%d\n", true_label);
    else
        fprintf(out, "\tjp nc,L%d\n", true_label);
    fprintf(out, "\tjp L%d\nL%d:\n\tinc l\nL%d:\n",
            end_label, true_label, end_label);
}

static void mir_emit_scalar_compare_biased_right(FILE *out, int operation)
{
    int true_label = new_label();
    int end_label = new_label();

    fputs("\tld a,h\n\txor 128\n\tld h,a\n"
          "\tsbc hl,de\n\tld hl,0\n", out);
    if (operation == '<')
        fprintf(out, "\tjp c,L%d\n", true_label);
    else
        fprintf(out, "\tjp nc,L%d\n", true_label);
    fprintf(out, "\tjp L%d\nL%d:\n\tinc l\nL%d:\n",
            end_label, true_label, end_label);
}

static void mir_emit_scalar_shift(FILE *out, int operation, int is_unsigned)
{
    int loop_label = new_label();
    int end_label = new_label();

    fputs("\tld b,e\n\tld a,b\n\tor a\n", out);
    fprintf(out, "\tjp z,L%d\nL%d:\n", end_label, loop_label);
    if (operation == TOK_SHL)
        fputs("\tadd hl,hl\n", out);
    else if (is_unsigned)
        fputs("\tsrl h\n\trr l\n", out);
    else
        fputs("\tsra h\n\trr l\n", out);
    fprintf(out, "\tdjnz L%d\nL%d:\n", loop_label, end_label);
}

static int mir_emit_scalar_value(FILE *out, int value, int depth)
{
    const struct MirInsn *definition;
    const struct MirObject *object;
    int false_label;
    int end_label;

    if (depth > 256)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL || type_size(definition->type) > 2)
        return 0;
    switch (definition->opcode) {
    case MIR_PARAM:
        if (definition->object < 0 || definition->object >= mir.object_count)
            return 0;
        object = &mir.objects[definition->object];
        if (object->storage != SC_PARAM || type_size(object->type) > 2)
            return 0;
        if (type_size(object->type) == 1) {
            fprintf(out, "\tld l,(ix%+d)\n", object->offset);
            if (type_is_bool(object->type)) {
                end_label = new_label();
                fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                        end_label, end_label);
            } else if ((object->type & TYPE_UNSIGNED) != 0)
                fputs("\tld h,0\n", out);
            else {
                end_label = new_label();
                fputs("\tld h,0\n\tbit 7,l\n", out);
                fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                        end_label, end_label);
            }
        } else {
            fprintf(out, "\tld l,(ix%+d)\n", object->offset);
            fprintf(out, "\tld h,(ix%+d)\n", object->offset + 1);
        }
        return 1;
    case MIR_CONST:
        fprintf(out, "\tld hl,%ld\n", definition->immediate & 0xffffL);
        return 1;
    case MIR_UNARY:
        if (!mir_emit_scalar_value(out, definition->src1, depth + 1))
            return 0;
        if (definition->immediate == 0 || definition->immediate == '+')
            return 1;
        if (definition->immediate == '-') {
            fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
            return 1;
        }
        if (definition->immediate == '~') {
            fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
            return 1;
        }
        if (definition->immediate == '!') {
            false_label = new_label();
            end_label = new_label();
            fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
            fprintf(out, "\tjp nz,L%d\n\tinc hl\nL%d:\n", false_label,
                    false_label);
            (void)end_label;
            return 1;
        }
        return 0;
    case MIR_BINARY:
        if (!mir_emit_scalar_value(out, definition->src1, depth + 1))
            return 0;
        fputs("\tpush hl\n", out);
        if (!mir_emit_scalar_value(out, definition->src2, depth + 1))
            return 0;
        fputs("\tex de,hl\n\tpop hl\n", out);
        switch ((int)definition->immediate) {
        case '+': fputs("\tadd hl,de\n", out); return 1;
        case '-': fputs("\tor a\n\tsbc hl,de\n", out); return 1;
        case '&':
            fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
            return 1;
        case '|':
            fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
            return 1;
        case '^':
            fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
            return 1;
        case '*':
            fputs("\textrn __mulu\n\tcall __mulu\n", out);
            return 1;
        case '/':
            fprintf(out, "\textrn %s\n\tcall %s\n",
                    (definition->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs",
                    (definition->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs");
            return 1;
        case '%':
            fprintf(out, "\textrn %s\n\tcall %s\n",
                    (definition->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods",
                    (definition->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods");
            return 1;
        case TOK_EQ: case TOK_NE: case '<': case '>': case TOK_LE: case TOK_GE:
            {
                const struct MirInsn *left = mir_definition(definition->src1);
                const struct MirInsn *right = mir_definition(definition->src2);
                int is_unsigned = (left != NULL &&
                                   (left->type & TYPE_UNSIGNED) != 0) ||
                                  (right != NULL &&
                                   (right->type & TYPE_UNSIGNED) != 0);
                mir_emit_scalar_compare(out, (int)definition->immediate,
                                        is_unsigned);
            }
            return 1;
        case TOK_SHL: case TOK_SHR:
            {
                const struct MirInsn *left = mir_definition(definition->src1);
                mir_emit_scalar_shift(out, (int)definition->immediate,
                                      left != NULL &&
                                      (left->type & TYPE_UNSIGNED) != 0);
            }
            return 1;
        default:
            return 0;
        }
    default:
        return 0;
    }
}

static int mir_try_emit_scalar_dag(FILE *out)
{
    const struct MirInsn *return_insn = NULL;
    int i;

    if ((mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_RETURN) {
            if (return_insn != NULL)
                return 0;
            return_insn = insn;
        } else if (insn->opcode != MIR_NOP && insn->opcode != MIR_LABEL &&
                   insn->opcode != MIR_PARAM && insn->opcode != MIR_CONST &&
                   insn->opcode != MIR_UNARY && insn->opcode != MIR_BINARY &&
                   !(insn->opcode == MIR_STORE && insn->object >= 0)) {
            return 0;
        }
    }
    if (return_insn == NULL || return_insn->src1 < 0)
        return 0;
    mir_emit_prologue(out);
    if (!mir_emit_scalar_value(out, return_insn->src1, 0))
        return 0;
    fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

static int mir_home_uses_iy(void)
{
    int value;
    for (value = 0; value < mir.next_value; ++value)
        if (mir.allocation_colors[value] == MIR_COLOR_IY)
            return 1;
    return 0;
}

static int mir_emit_home_to_hl(FILE *out, int value)
{
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: return 1;
    case MIR_COLOR_DE: fputs("\tpush de\n\tpop hl\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tld h,b\n\tld l,c\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush iy\n\tpop hl\n", out); return 1;
    default: return 0;
    }
}

static int mir_emit_home_to_de(FILE *out, int value)
{
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_DE: return 1;
    case MIR_COLOR_BC: fputs("\tld d,b\n\tld e,c\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush iy\n\tpop de\n", out); return 1;
    default: return 0;
    }
}

static int mir_emit_hl_to_home(FILE *out, int value)
{
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: return 1;
    case MIR_COLOR_DE: fputs("\tex de,hl\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tld b,h\n\tld c,l\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush hl\n\tpop iy\n", out); return 1;
    default: return 0;
    }
}

static int mir_emit_constant_to_home(FILE *out, int value, long immediate)
{
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fprintf(out, "\tld hl,%ld\n", immediate & 0xffffL); return 1;
    case MIR_COLOR_DE: fprintf(out, "\tld de,%ld\n", immediate & 0xffffL); return 1;
    case MIR_COLOR_BC: fprintf(out, "\tld bc,%ld\n", immediate & 0xffffL); return 1;
    case MIR_COLOR_IY: fprintf(out, "\tld iy,%ld\n", immediate & 0xffffL); return 1;
    default: return 0;
    }
}

static int mir_emit_word_param_to_home(FILE *out, int value, int offset)
{
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n", offset, offset + 1);
        return 1;
    case MIR_COLOR_DE:
        fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n", offset, offset + 1);
        return 1;
    case MIR_COLOR_BC:
        fprintf(out, "\tld c,(ix%+d)\n\tld b,(ix%+d)\n", offset, offset + 1);
        return 1;
    case MIR_COLOR_IY:
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n", offset, offset + 1);
        fputs("\tpush hl\n\tpop iy\n\tpop hl\n", out);
        return 1;
    default:
        return 0;
    }
}

static int mir_emit_push_home(FILE *out, int value)
{
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fputs("\tpush hl\n", out); return 1;
    case MIR_COLOR_DE: fputs("\tpush de\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tpush bc\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpush iy\n", out); return 1;
    default: return 0;
    }
}

static int mir_emit_pop_home(FILE *out, int value)
{
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL: fputs("\tpop hl\n", out); return 1;
    case MIR_COLOR_DE: fputs("\tpop de\n", out); return 1;
    case MIR_COLOR_BC: fputs("\tpop bc\n", out); return 1;
    case MIR_COLOR_IY: fputs("\tpop iy\n", out); return 1;
    default: return 0;
    }
}

static int mir_phi_source_for_edge(const struct MirInsn *phi,
                                   int predecessor_label, int edge_label,
                                   int successor, int phi_instruction)
{
    int instruction;
    if (predecessor_label == phi->phi_pred1 || edge_label == phi->phi_pred1)
        return phi->src1;
    if (predecessor_label == phi->phi_pred2 || edge_label == phi->phi_pred2)
        return phi->src2;
    for (instruction = successor;
         instruction >= 0 && instruction < phi_instruction;
         ++instruction)
        if (mir.insns[instruction].opcode == MIR_LABEL) {
            if (mir.insns[instruction].label == phi->phi_pred1)
                return phi->src1;
            if (mir.insns[instruction].label == phi->phi_pred2)
                return phi->src2;
        }
    return -1;
}

static int mir_emit_homed_phi_copies(FILE *out, int predecessor,
                                     int successor)
{
    int sources[256];
    int destinations[256];
    int count = 0;
    int predecessor_label = mir_block_label_before(predecessor);
    int edge_label = -1;
    int instruction = mir_first_nonlabel_successor(successor);
    int i;

    if (predecessor >= 0 && predecessor < mir.count &&
        (mir.insns[predecessor].opcode == MIR_JUMP ||
         mir.insns[predecessor].opcode == MIR_BRANCH_FALSE) &&
        mir_find_label(mir.insns[predecessor].label) == successor)
        edge_label = mir.insns[predecessor].label;

    while (instruction >= 0 && instruction < mir.count &&
           (mir.insns[instruction].opcode == MIR_PHI ||
            mir.insns[instruction].opcode == MIR_NOP)) {
        const struct MirInsn *phi = &mir.insns[instruction];
        int source;
        if (phi->opcode == MIR_NOP) {
            ++instruction;
            continue;
        }
        if (!mir_value_has_use(phi->dst)) {
            ++instruction;
            continue;
        }
        source = mir_phi_source_for_edge(phi, predecessor_label, edge_label,
                                         successor, instruction);
        if (source < 0)
            return 0;
        if (mir.allocation_colors[source] != mir.allocation_colors[phi->dst]) {
            if (count >= 256)
                return 0;
            sources[count] = source;
            destinations[count] = phi->dst;
            ++count;
        }
        ++instruction;
    }
    for (i = 0; i < count; ++i)
        if (!mir_emit_push_home(out, sources[i]))
            return 0;
    for (i = count - 1; i >= 0; --i)
        if (!mir_emit_pop_home(out, destinations[i]))
            return 0;
    return 1;
}

static int mir_edge_phi_names_predecessor(int predecessor, int successor)
{
    int instruction = mir_first_nonlabel_successor(successor);
    int predecessor_label = mir_block_label_before(predecessor);
    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_PHI)
        return 0;
    return mir.insns[instruction].phi_pred1 == predecessor_label ||
           mir.insns[instruction].phi_pred2 == predecessor_label;
}

static int mir_direct_branch_for_comparison(int instruction)
{
    const struct MirInsn *compare;
    int use_count = 0;
    int branch = -1;
    int i;

    if (instruction < 0 || instruction >= mir.count)
        return -1;
    compare = &mir.insns[instruction];
    if (compare->opcode != MIR_BINARY ||
        (compare->immediate != TOK_EQ && compare->immediate != TOK_NE &&
         compare->immediate != '<' && compare->immediate != '>' &&
         compare->immediate != TOK_LE && compare->immediate != TOK_GE))
        return -1;
    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].src1 == compare->dst ||
            mir.insns[i].src2 == compare->dst) {
            ++use_count;
            if (mir.insns[i].opcode == MIR_BRANCH_FALSE &&
                mir.insns[i].src1 == compare->dst)
                branch = i;
        }
    }
    return use_count == 1 ? branch : -1;
}

static int mir_compare_definition_for_branch(int instruction)
{
    const struct MirInsn *definition;
    int index;
    if (instruction < 0 || instruction >= mir.count ||
        mir.insns[instruction].opcode != MIR_BRANCH_FALSE)
        return -1;
    definition = mir_definition(mir.insns[instruction].src1);
    if (definition == NULL)
        return -1;
    index = (int)(definition - mir.insns);
    return mir_direct_branch_for_comparison(index) == instruction ? index : -1;
}

static int mir_emit_stack_word_param_to_home(FILE *out, int value, int offset)
{
    int stack_offset = offset - 2;
    switch (mir.allocation_colors[value]) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n", stack_offset);
        fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
        return 1;
    case MIR_COLOR_DE:
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n", stack_offset + 2);
        fputs("\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n\tpop hl\n", out);
        return 1;
    case MIR_COLOR_BC:
        fputs("\tpush hl\n", out);
        fprintf(out, "\tld hl,%d\n\tadd hl,sp\n", stack_offset + 2);
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n\tpop hl\n", out);
        return 1;
    default:
        return 0;
    }
}

static void mir_emit_home_prologue(FILE *out, int uses_iy)
{
    if (uses_iy)
        fputs("\tpush iy\n", out);
    mir_emit_prologue(out);
}

static void mir_emit_home_epilogue(FILE *out, int uses_iy)
{
    fputs("\tld sp,ix\n\tpop ix\n", out);
    if (uses_iy)
        fputs("\tpop iy\n", out);
    fputs("\tret\n", out);
}

static int mir_emit_homed_unary_instruction(FILE *out,
                                            const struct MirInsn *insn)
{
    int preserve_hl = mir.allocation_colors[insn->src1] != MIR_COLOR_HL &&
                      mir.allocation_colors[insn->dst] != MIR_COLOR_HL;
    int label;

    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (!mir_emit_home_to_hl(out, insn->src1))
        return 0;
    if (insn->immediate == 0 || insn->immediate == '+') {
        /* Cast/no-op in the 16-bit home subset. */
    } else if (insn->immediate == '-') {
        fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
    } else if (insn->immediate == '~') {
        fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
    } else if (insn->immediate == '!') {
        label = new_label();
        fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
        fprintf(out, "\tjp nz,L%d\n\tinc hl\nL%d:\n", label, label);
    } else {
        return 0;
    }
    if (!mir_emit_hl_to_home(out, insn->dst))
        return 0;
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    return 1;
}

static int mir_emit_homed_binary_instruction(FILE *out,
                                             const struct MirInsn *insn,
                                             int allow_comparison)
{
    int instruction = (int)(insn - mir.insns);
    int left = insn->src1;
    int right = insn->src2;
    int commutative = insn->immediate == '+' || insn->immediate == '&' ||
                      insn->immediate == '|' || insn->immediate == '^' ||
                      insn->immediate == TOK_EQ || insn->immediate == TOK_NE;
    int preserve_hl;
    int preserve_de;
    const struct MirInsn *left_definition;
    const struct MirInsn *right_definition;
    int comparison_unsigned;
    int biased_right_constant;

    if (mir.allocation_colors[right] == MIR_COLOR_HL) {
        int temporary;
        if (!commutative)
            return 0;
        temporary = left;
        left = right;
        right = temporary;
    }
        preserve_hl = mir.allocation_colors[insn->dst] != MIR_COLOR_HL &&
                                    !(mir.allocation_colors[left] == MIR_COLOR_HL &&
                                        !mir_value_has_use_after(left, instruction));
        preserve_de = mir.allocation_colors[insn->dst] != MIR_COLOR_DE &&
                                    !(mir.allocation_colors[right] == MIR_COLOR_DE &&
                                        !mir_value_has_use_after(right, instruction));
        left_definition = mir_definition(left);
        right_definition = mir_definition(right);
        comparison_unsigned = (left_definition != NULL &&
                               (left_definition->type & TYPE_UNSIGNED) != 0) ||
                              (right_definition != NULL &&
                               (right_definition->type & TYPE_UNSIGNED) != 0);
        biased_right_constant = allow_comparison && !comparison_unsigned &&
                                (insn->immediate == '<' ||
                                 insn->immediate == TOK_GE) &&
                                right_definition != NULL &&
                                right_definition->opcode == MIR_CONST;
    if (preserve_hl)
        fputs("\tpush hl\n", out);
    if (preserve_de)
        fputs("\tpush de\n", out);
    if (!mir_emit_home_to_hl(out, left))
        return 0;
    if (biased_right_constant)
        fprintf(out, "\tld de,%ld\n",
                (right_definition->immediate ^ 0x8000L) & 0xffffL);
    else if (!mir_emit_home_to_de(out, right))
        return 0;
    if (insn->immediate == '+')
        fputs("\tadd hl,de\n", out);
    else if (insn->immediate == '-')
        fputs("\tor a\n\tsbc hl,de\n", out);
    else if (insn->immediate == '&')
        fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
    else if (insn->immediate == '|')
        fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
    else if (insn->immediate == '^')
        fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
    else if (allow_comparison &&
             (insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
              insn->immediate == '<' || insn->immediate == '>' ||
              insn->immediate == TOK_LE || insn->immediate == TOK_GE)) {
        if (biased_right_constant)
            mir_emit_scalar_compare_biased_right(
                out, (int)insn->immediate);
        else
            mir_emit_scalar_compare(out, (int)insn->immediate,
                                    comparison_unsigned);
    } else {
        return 0;
    }
    if (!mir_emit_hl_to_home(out, insn->dst))
        return 0;
    if (preserve_de)
        fputs("\tpop de\n", out);
    if (preserve_hl)
        fputs("\tpop hl\n", out);
    return 1;
}

static int mir_emit_homed_compare_false(FILE *out,
                                        const struct MirInsn *compare,
                                        int false_label)
{
    int left = compare->src1;
    int right = compare->src2;
    int operation = (int)compare->immediate;
    const struct MirInsn *left_definition;
    const struct MirInsn *right_definition;
    int is_unsigned;

    right_definition = mir_definition(right);
    left_definition = mir_definition(left);
    if (right_definition != NULL && right_definition->opcode == MIR_CONST &&
        right_definition->immediate == 0 &&
        mir.allocation_colors[left] == MIR_COLOR_HL) {
        is_unsigned = left_definition != NULL &&
                      (left_definition->type & TYPE_UNSIGNED) != 0;
        if (operation == '>') {
            if (is_unsigned) {
                fputs("\tld a,h\n\tor l\n", out);
                fprintf(out, "\tjp z,L%d\n", false_label);
            } else {
                fputs("\tld a,h\n\tor a\n", out);
                fprintf(out, "\tjp m,L%d\n", false_label);
                fputs("\tor l\n", out);
                fprintf(out, "\tjp z,L%d\n", false_label);
            }
            return 1;
        }
        if (operation == TOK_GE) {
            if (!is_unsigned) {
                fputs("\tbit 7,h\n", out);
                fprintf(out, "\tjp nz,L%d\n", false_label);
            }
            return 1;
        }
        if (operation == '<') {
            if (is_unsigned) {
                fprintf(out, "\tjp L%d\n", false_label);
            } else {
                fputs("\tbit 7,h\n", out);
                fprintf(out, "\tjp z,L%d\n", false_label);
            }
            return 1;
        }
        if (operation == TOK_EQ || operation == TOK_NE) {
            fputs("\tld a,h\n\tor l\n", out);
            fprintf(out, operation == TOK_EQ ? "\tjp nz,L%d\n"
                                             : "\tjp z,L%d\n",
                    false_label);
            return 1;
        }
    }

    if (operation == '>' || operation == TOK_LE) {
        int temporary = left;
        left = right;
        right = temporary;
        operation = operation == '>' ? '<' : TOK_GE;
    }
    left_definition = mir_definition(left);
    right_definition = mir_definition(right);
    is_unsigned = (left_definition != NULL &&
                   (left_definition->type & TYPE_UNSIGNED) != 0) ||
                  (right_definition != NULL &&
                   (right_definition->type & TYPE_UNSIGNED) != 0);

    /* Preserve the lifetime homes while using HL/DE as comparison operands. */
    fputs("\tpush hl\n\tpush de\n", out);
    if (!mir_emit_push_home(out, right) ||
        !mir_emit_home_to_hl(out, left))
        return 0;
    fputs("\tpop de\n", out);
    if (!is_unsigned && operation != TOK_EQ && operation != TOK_NE)
        fputs("\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n", out);
    fputs("\tor a\n\tsbc hl,de\n\tpop de\n\tpop hl\n", out);
    if (operation == TOK_EQ)
        fprintf(out, "\tjp nz,L%d\n", false_label);
    else if (operation == TOK_NE)
        fprintf(out, "\tjp z,L%d\n", false_label);
    else if (operation == '<')
        fprintf(out, "\tjp nc,L%d\n", false_label);
    else
        fprintf(out, "\tjp c,L%d\n", false_label);
    return 1;
}

static int mir_try_emit_homed_scalar_dag(FILE *out)
{
    int uses_iy;
    int frameless;
    int return_value = -1;
    int parameter_count = 0;
    int operation_count = 0;
    int i;

    if ((mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2 ||
        mir.allocation_spill_count != 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if ((insn->dst >= 0 && type_size(insn->type) > 2) ||
            (insn->opcode == MIR_BINARY &&
             type_size(insn->secondary_offset) > 2))
            return 0;
        if (insn->dst >= 0 && mir.allocation_colors[insn->dst] < 0)
            return 0;
        if (insn->opcode == MIR_STORE && insn->object < 0)
            return 0;
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_CONST:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_UNARY:
            ++operation_count;
            if (insn->immediate != 0 && insn->immediate != '+' &&
                insn->immediate != '-' && insn->immediate != '~' &&
                insn->immediate != '!')
                return 0;
            break;
        case MIR_BINARY:
            ++operation_count;
            if (insn->immediate != '+' && insn->immediate != '-' &&
                insn->immediate != '&' && insn->immediate != '|' &&
                insn->immediate != '^')
                return 0;
            if (mir.allocation_colors[insn->src2] == MIR_COLOR_HL)
                return 0;
            break;
        case MIR_RETURN:
            if (return_value >= 0)
                return 0;
            return_value = insn->src1;
            break;
        default:
            return 0;
        }
    }
    if (return_value < 0)
        return 0;
    if (parameter_count == 0 && operation_count == 0)
        return 0;

    uses_iy = mir_home_uses_iy();
    frameless = !uses_iy && mir.local_bytes == 0;
    if (frameless) {
        for (i = 0; i < mir.count; ++i)
            if (mir.insns[i].opcode == MIR_PARAM &&
                mir.insns[i].object >= 0 &&
                type_size(mir.objects[mir.insns[i].object].type) != 2)
                frameless = 0;
    }
    if (frameless) {
        if (opt_stack_check)
            fputs("\textrn __stchk\n\tcall __stchk\n", out);
    } else {
        mir_emit_home_prologue(out, uses_iy);
    }
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        const struct MirObject *object;
        int parameter_offset;
        int true_label;

        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL:
            break;
        case MIR_PARAM:
            if (insn->object < 0 || insn->object >= mir.object_count)
                return 0;
            object = &mir.objects[insn->object];
            parameter_offset = object->offset + (uses_iy ? 2 : 0);
            if (type_size(object->type) == 1) {
                int preserve_hl = mir.allocation_colors[insn->dst] != MIR_COLOR_HL;
                if (preserve_hl)
                    fputs("\tpush hl\n", out);
                fprintf(out, "\tld l,(ix%+d)\n", parameter_offset);
                if (type_is_bool(object->type)) {
                    true_label = new_label();
                    fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                    fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                            true_label, true_label);
                } else if ((object->type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    true_label = new_label();
                    fputs("\tld h,0\n\tbit 7,l\n", out);
                    fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                            true_label, true_label);
                }
                if (!mir_emit_hl_to_home(out, insn->dst))
                    return 0;
                if (preserve_hl)
                    fputs("\tpop hl\n", out);
                break;
            } else {
                if (!(frameless
                    ? mir_emit_stack_word_param_to_home(
                        out, insn->dst, object->offset)
                    : mir_emit_word_param_to_home(
                        out, insn->dst, parameter_offset)))
                    return 0;
                break;
            }
            break;
        case MIR_CONST:
            if (!mir_emit_constant_to_home(out, insn->dst, insn->immediate))
                return 0;
            break;
        case MIR_UNARY:
            {
            int preserve_hl = mir.allocation_colors[insn->src1] != MIR_COLOR_HL &&
                              mir.allocation_colors[insn->dst] != MIR_COLOR_HL;
            if (preserve_hl)
                fputs("\tpush hl\n", out);
            if (!mir_emit_home_to_hl(out, insn->src1))
                return 0;
            if (insn->immediate == '-')
                fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
            else if (insn->immediate == '~')
                fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
            else if (insn->immediate == '!') {
                true_label = new_label();
                fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
                fprintf(out, "\tjp nz,L%d\n\tinc hl\nL%d:\n",
                        true_label, true_label);
            }
            if (!mir_emit_hl_to_home(out, insn->dst))
                return 0;
            if (preserve_hl)
                fputs("\tpop hl\n", out);
            break;
            }
        case MIR_BINARY:
            {
            int preserve_hl = mir.allocation_colors[insn->src1] != MIR_COLOR_HL &&
                              mir.allocation_colors[insn->dst] != MIR_COLOR_HL;
            int preserve_de = mir.allocation_colors[insn->src2] != MIR_COLOR_DE &&
                              mir.allocation_colors[insn->dst] != MIR_COLOR_DE;
            if (preserve_hl)
                fputs("\tpush hl\n", out);
            if (preserve_de)
                fputs("\tpush de\n", out);
            if (!mir_emit_home_to_hl(out, insn->src1) ||
                !mir_emit_home_to_de(out, insn->src2))
                return 0;
            if (insn->immediate == '+')
                fputs("\tadd hl,de\n", out);
            else if (insn->immediate == '-')
                fputs("\tor a\n\tsbc hl,de\n", out);
            else if (insn->immediate == '&')
                fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
            else if (insn->immediate == '|')
                fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
            else if (insn->immediate == '^')
                fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
            if (!mir_emit_hl_to_home(out, insn->dst))
                return 0;
            if (preserve_de)
                fputs("\tpop de\n", out);
            if (preserve_hl)
                fputs("\tpop hl\n", out);
            break;
            }
        case MIR_RETURN:
            if (!mir_emit_home_to_hl(out, insn->src1))
                return 0;
            if (frameless)
                fputs("\tret\n", out);
            else
                mir_emit_home_epilogue(out, uses_iy);
            break;
        default:
            return 0;
        }
    }
    return 1;
}

static int mir_try_emit_homed_scalar_cfg(FILE *out)
{
    int *labels;
    int uses_iy;
    int frameless;
    int return_count = 0;
    int i;
    int accepted = 0;

    if ((mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2 ||
        mir.allocation_spill_count != 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if ((insn->dst >= 0 && type_size(insn->type) > 2) ||
            (insn->opcode == MIR_BINARY &&
             type_size(insn->secondary_offset) > 2))
            return 0;
        if (insn->dst >= 0 && mir.allocation_colors[insn->dst] < 0)
            return 0;
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_PARAM: case MIR_CONST:
        case MIR_PHI: case MIR_JUMP: case MIR_BRANCH_FALSE:
            break;
        case MIR_STORE:
            if (!mir_object_is_fully_promoted(insn->object))
                return 0;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0 && insn->immediate != '+' &&
                insn->immediate != '-' && insn->immediate != '~' &&
                insn->immediate != '!')
                return 0;
            if (insn->immediate == '!')
                return 0;
            break;
        case MIR_BINARY:
            if (insn->immediate != '+' && insn->immediate != '-' &&
                insn->immediate != '&' && insn->immediate != '|' &&
                insn->immediate != '^' && insn->immediate != TOK_EQ &&
                insn->immediate != TOK_NE && insn->immediate != '<' &&
                insn->immediate != '>' && insn->immediate != TOK_LE &&
                insn->immediate != TOK_GE)
                return 0;
            break;
        case MIR_RETURN:
            ++return_count;
            break;
        default:
            return 0;
        }
    }
    if (return_count == 0)
        return 0;

    labels = (int *)malloc((size_t)mir.next_label * sizeof(*labels));
    if (labels == NULL)
        fatal("out of memory selecting homed MIR CFG labels");
    for (i = 0; i < mir.next_label; ++i)
        labels[i] = new_label();

    uses_iy = mir_home_uses_iy();
    frameless = !uses_iy;
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_PARAM &&
            (mir.insns[i].object < 0 ||
             type_size(mir.objects[mir.insns[i].object].type) != 2)) {
            free(labels);
            return 0;
        }
    if (frameless) {
        if (opt_stack_check)
            fputs("\textrn __stchk\n\tcall __stchk\n", out);
    } else {
        mir_emit_home_prologue(out, uses_iy);
    }

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        const struct MirObject *object;
        int target;
        int true_label;
        int preserve_hl;

        switch (insn->opcode) {
        case MIR_NOP: case MIR_PHI: case MIR_STORE:
            break;
        case MIR_LABEL:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            fprintf(out, "L%d:\n", labels[insn->label]);
            break;
        case MIR_PARAM:
            if (!mir_value_has_use(insn->dst))
                break;
            object = &mir.objects[insn->object];
            if (!(frameless
                  ? mir_emit_stack_word_param_to_home(
                        out, insn->dst, object->offset)
                  : mir_emit_word_param_to_home(
                        out, insn->dst, object->offset + 2)))
                goto done;
            break;
        case MIR_CONST:
            if (!mir_emit_constant_to_home(out, insn->dst, insn->immediate))
                goto done;
            break;
        case MIR_UNARY:
            if (!mir_emit_homed_unary_instruction(out, insn))
                goto done;
            break;
        case MIR_BINARY:
            if (mir_direct_branch_for_comparison(i) >= 0)
                break;
            if (!mir_emit_homed_binary_instruction(out, insn, 1))
                goto done;
            break;
        case MIR_JUMP:
            target = mir_find_label(insn->label);
            if (target < 0 || !mir_emit_homed_phi_copies(out, i, target))
                goto done;
            fprintf(out, "\tjp L%d\n", labels[insn->label]);
            break;
        case MIR_BRANCH_FALSE:
            target = mir_find_label(insn->label);
            if (target < 0)
                goto done;
            {
                int compare_index = mir_compare_definition_for_branch(i);
                if (compare_index >= 0) {
                    int false_has_phi = mir_edge_phi_names_predecessor(i, target);
                    int true_has_phi = i + 1 < mir.count &&
                        mir_edge_phi_names_predecessor(i, i + 1);
                    if (!false_has_phi) {
                        if (!mir_emit_homed_compare_false(
                                out, &mir.insns[compare_index],
                                labels[insn->label]))
                            goto done;
                        if (true_has_phi &&
                            !mir_emit_homed_phi_copies(out, i, i + 1))
                            goto done;
                    } else {
                        int false_stub = new_label();
                        int continue_label = new_label();
                        if (!mir_emit_homed_compare_false(
                                out, &mir.insns[compare_index], false_stub))
                            goto done;
                        if (true_has_phi &&
                            !mir_emit_homed_phi_copies(out, i, i + 1))
                            goto done;
                        fprintf(out, "\tjp L%d\nL%d:\n",
                                continue_label, false_stub);
                        if (!mir_emit_homed_phi_copies(out, i, target))
                            goto done;
                        fprintf(out, "\tjp L%d\nL%d:\n",
                                labels[insn->label], continue_label);
                    }
                    break;
                }
            }
            true_label = new_label();
            preserve_hl = mir.allocation_colors[insn->src1] != MIR_COLOR_HL;
            if (preserve_hl)
                fputs("\tpush hl\n", out);
            if (!mir_emit_home_to_hl(out, insn->src1))
                goto done;
            fputs("\tld a,h\n\tor l\n", out);
            if (preserve_hl)
                fputs("\tpop hl\n", out);
            fprintf(out, "\tjp nz,L%d\n", true_label);
            if (!mir_emit_homed_phi_copies(out, i, target))
                goto done;
            fprintf(out, "\tjp L%d\nL%d:\n", labels[insn->label], true_label);
            if (i + 1 < mir.count &&
                !mir_emit_homed_phi_copies(out, i, i + 1))
                goto done;
            break;
        case MIR_RETURN:
            if (!mir_emit_home_to_hl(out, insn->src1))
                goto done;
            if (frameless)
                fputs("\tret\n", out);
            else
                mir_emit_home_epilogue(out, uses_iy);
            break;
        default:
            goto done;
        }
        if (insn->opcode != MIR_JUMP && insn->opcode != MIR_BRANCH_FALSE &&
            insn->opcode != MIR_RETURN && i + 1 < mir.count &&
            mir_edge_phi_names_predecessor(i, i + 1) &&
            !mir_emit_homed_phi_copies(out, i, i + 1))
            goto done;
    }
    accepted = 1;
done:
    if (!accepted && getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr, "; MIR home-cfg reject function=%s insn=%d opcode=%s\n",
                mir.name, i,
                i >= 0 && i < mir.count
                    ? mir_opcode_name(mir.insns[i].opcode) : "preflight");
    free(labels);
    return accepted;
}

static int mir_virtual_offset(int value)
{
    int slot = value;
    if (value >= 0 && value < mir.next_value && mir.backend_slots != NULL &&
        mir.backend_slots[value] >= 0)
        slot = mir.backend_slots[value];
    return -mir.local_bytes - mir.aggregate_temp_bytes - 2 * (slot + 1);
}

static int mir_virtual_iy_offset(int value)
{
    return mir_virtual_offset(value) + mir.local_bytes +
           mir.aggregate_temp_bytes;
}

static int mir_function_has_any_call(void)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_CALL ||
            mir.insns[i].opcode == MIR_CALL_AGGREGATE)
            return 1;
    return 0;
}

static int mir_can_forward_hl_to_next(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    const struct MirInsn *next;
    int next_instruction;
    int instruction;

    if (mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 1 >= mir.count)
        return 0;
    if (definition != NULL &&
        (definition->opcode == MIR_CALL ||
         definition->opcode == MIR_CALL_AGGREGATE))
        return 0;
    next_instruction = mir_emit_instruction_index + 1;
    while (next_instruction < mir.count &&
           mir.insns[next_instruction].opcode == MIR_NOP)
        ++next_instruction;
    if (next_instruction >= mir.count)
        return 0;
    next = &mir.insns[next_instruction];
    if (next_instruction != mir_emit_instruction_index + 1) {
        if (next->opcode != MIR_RETURN)
            return 0;
    } else if (!mir_virtual_iy_base && next->opcode != MIR_RETURN &&
               next->opcode != MIR_STORE)
        return 0;
    if (next->opcode == MIR_RETURN &&
        (mir.has_vla || mir_function_has_any_call()))
        return 0;
    if (next->opcode == MIR_INDEX_ADDRESS) {
        if (next->src2 != value)
            return 0;
    } else if (next->src1 != value)
            return 0;
    switch (next->opcode) {
    case MIR_INDEX_ADDRESS:
    case MIR_MEMBER_ADDRESS: case MIR_LOAD_INDIRECT: case MIR_UNARY:
        break;
    case MIR_BINARY:
        if (type_size(next->secondary_offset) == 4)
            return 0;
        break;
    case MIR_STORE_INDIRECT:
        if (next->bit_width > 0 || next->memory_size > 2)
            return 0;
        break;
    case MIR_RETURN:
        break;
    case MIR_STORE:
        {
            int memory_type;
            int memory_storage;
            int memory_offset;
            int producer_opcode = mir.insns[mir_emit_instruction_index].opcode;
            if (mir_object_is_fully_promoted(next->object) ||
                (producer_opcode != MIR_LOAD_INDIRECT &&
                 producer_opcode != MIR_BINARY &&
                 producer_opcode != MIR_UNARY) ||
                !mir_scalar_memory_location(next, &memory_type,
                                            &memory_storage, &memory_offset) ||
                type_is_struct_object(memory_type) ||
                type_size(memory_type) > 2)
                return 0;
        }
        break;
    default:
        return 0;
    }
    for (instruction = next_instruction + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value)
            return 0;
        if ((insn->opcode == MIR_CALL ||
             insn->opcode == MIR_CALL_AGGREGATE) &&
            mir_call_uses_value(insn, value))
            return 0;
    }
    return 1;
}

static int mir_can_forward_stack_to_index(int value)
{
    const struct MirInsn *middle;
    const struct MirInsn *index;
    int instruction;

    if (!mir_virtual_iy_base || mir_emit_instruction_index < 0 ||
        mir_emit_instruction_index + 2 >= mir.count)
        return 0;
    middle = &mir.insns[mir_emit_instruction_index + 1];
    index = &mir.insns[mir_emit_instruction_index + 2];
    if (middle->opcode != MIR_CONST || index->opcode != MIR_INDEX_ADDRESS ||
        index->src1 != value || index->src2 != middle->dst)
        return 0;
    for (instruction = mir_emit_instruction_index + 3;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value || insn->src2 == value)
            return 0;
        if ((insn->opcode == MIR_CALL ||
             insn->opcode == MIR_CALL_AGGREGATE) &&
            mir_call_uses_value(insn, value))
            return 0;
    }
    return 1;
}

static int mir_call_only_constant(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int argument_count = 0;
    int instruction;

    if (definition == NULL ||
        (definition->opcode != MIR_CONST &&
         definition->opcode != MIR_FLOAT_CONST &&
         definition->opcode != MIR_STRING_ADDRESS))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_ARG && insn->src1 == value) {
            ++argument_count;
            continue;
        }
        if (insn->src1 == value || insn->src2 == value)
            return 0;
    }
    return argument_count == 1;
}

/* Evaluates a scalar binary arithmetic/bitwise/shift operation over two
 * compile-time constant operands, truncated and sign-extended to match
 * `operand_type` (16-bit int or 32-bit long, per its TYPE_UNSIGNED flag),
 * so a later Item 13 fold sees the exact result the target Z80 code would
 * compute at runtime. Deliberately excludes relational/equality operators
 * (Item 14's scope, a separate fold) and refuses to fold a division or
 * modulo by a zero divisor, leaving that case to emit its normal runtime
 * instruction sequence unchanged (matching prior behavior for this
 * already-undefined-in-C case). Assumes the host `long` is at least
 * 32 bits wide, true for every host this project builds on. */
static int mir_fold_constant_binary(int op, long left, long right,
                                    int operand_type, long *result)
{
    int type_bytes = type_size(operand_type);
    int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;
    long value;
    unsigned long mask;

    switch (op) {
    case '+': value = left + right; break;
    case '-': value = left - right; break;
    case '*': value = left * right; break;
    case '/':
        if (right == 0)
            return 0;
        value = left / right;
        break;
    case '%':
        if (right == 0)
            return 0;
        value = left % right;
        break;
    case '&': value = left & right; break;
    case '|': value = left | right; break;
    case '^': value = left ^ right; break;
    case TOK_SHL:
        if (right < 0 || right >= type_bytes * 8)
            return 0;
        value = left << right;
        break;
    case TOK_SHR:
        if (right < 0 || right >= type_bytes * 8)
            return 0;
        value = left >> right;
        break;
    default:
        return 0;
    }
    mask = type_bytes == 2 ? 0xffffUL
         : type_bytes == 4 ? 0xffffffffUL : (unsigned long)-1L;
    value = (long)((unsigned long)value & mask);
    if (!is_unsigned) {
        if (type_bytes == 2 && (value & 0x8000L) != 0)
            value -= 0x10000L;
        else if (type_bytes == 4 && (value & 0x80000000L) != 0)
            value -= 0x100000000L;
    }
    *result = value;
    return 1;
}

/* Evaluates a scalar relational/equality operation over two compile-time
 * constant operands, comparing them per `operand_type`'s width and
 * signedness (the common type both operands were already converted to by
 * the caller), and produces the boolean 0/1 result the target Z80 compare
 * sequence would compute at runtime. A separate fold from Item 13's
 * arithmetic/bitwise/shift fold above since the result here is always a
 * width-independent boolean rather than a value truncated to
 * `operand_type`. */
static int mir_fold_constant_compare(int op, long left, long right,
                                     int operand_type, long *result)
{
    int type_bytes = type_size(operand_type);
    int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;
    int cmp;

    if (is_unsigned) {
        unsigned long mask = type_bytes == 2 ? 0xffffUL
                            : type_bytes == 4 ? 0xffffffffUL
                                               : (unsigned long)-1L;
        unsigned long uleft = (unsigned long)left & mask;
        unsigned long uright = (unsigned long)right & mask;
        cmp = uleft < uright ? -1 : uleft > uright ? 1 : 0;
    } else {
        cmp = left < right ? -1 : left > right ? 1 : 0;
    }
    switch (op) {
    case TOK_EQ: *result = cmp == 0; break;
    case TOK_NE: *result = cmp != 0; break;
    case '<':    *result = cmp < 0; break;
    case '>':    *result = cmp > 0; break;
    case TOK_LE: *result = cmp <= 0; break;
    case TOK_GE: *result = cmp >= 0; break;
    default: return 0;
    }
    return 1;
}

static int mir_binary_only_constant(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int binary_count = 0;
    int binary_result = -1;
    int instruction;

    if (definition == NULL ||
        definition->opcode != MIR_CONST)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_BINARY &&
            (insn->src1 == value || insn->src2 == value) &&
            type_size(insn->secondary_offset) <= 2) {
            if (mir.has_vla &&
                (insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
                 insn->immediate == '<' || insn->immediate == '>' ||
                 insn->immediate == TOK_LE || insn->immediate == TOK_GE))
                return 0;
            ++binary_count;
            binary_result = insn->dst;
            continue;
        }
        if (insn->src1 == value || insn->src2 == value)
            return 0;
    }
    if (binary_count != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_VLA_ALLOC &&
            mir.insns[instruction].src1 == binary_result)
            return 0;
    return 1;
}

/* Caps how many Z80 instructions the shift/add decomposition below may
 * unroll to before falling back to __mulu. Mirrors MUL_CONST_MAX_OPS in
 * dcc_ops.c's emit_mul_hl_const, which this routine is a port of, so the
 * MIR backend reaches the same code-size/quality tradeoff as the legacy
 * AST backend for constant multiplication. */
#define MIR_MUL_CONST_MAX_OPS 10

/* Number of instructions mir_emit_mul_hl_const_general would emit for uv (a
 * 16-bit unsigned pattern, uv != 0 and not already a single power of two):
 * one "add hl,hl" per bit position below the highest set bit (the
 * doublings), plus one "add hl,de" per OTHER set bit (the highest bit
 * itself is free - it's the starting value). */
static int mir_mul_const_op_count(unsigned long uv)
{
    int bit;
    int highest = -1;
    int adds = 0;

    for (bit = 15; bit >= 0; --bit) {
        if (uv & (1uL << (unsigned)bit)) {
            if (highest < 0)
                highest = bit;
            else
                ++adds;
        }
    }
    if (highest <= 0)
        return 0;
    return highest + adds;
}

/* True if value 'v' is the size operand feeding a VLA allocation.
 * A general (non-power-of-two) strength-reduced multiply that both
 * sizes a VLA allocation *and* shares the function with a later integer
 * division/modulo drives a runtime stack-pointer adjustment whose
 * surrounding VLA frame slot traffic is undercounted by the static
 * byte/instruction cost gate: it is cheap in bytes yet not free in
 * cycles, and only the peephole optimizer (dccpeep) currently cleans it
 * up. Restrict that specific combination to the existing __mulu path;
 * VLA-alloc-sizing multiplies without a subsequent division (e.g. a
 * pure sizeof expression) are unaffected and still benefit below. */
static int mir_value_feeds_vla_alloc(int v)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_VLA_ALLOC && mir.insns[i].src1 == v)
            return 1;
    return 0;
}

static int mir_has_integer_division(void)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_BINARY &&
            (mir.insns[i].immediate == '/' || mir.insns[i].immediate == '%'))
            return 1;
    return 0;
}

/* Single source of truth for whether a constant multiply may use the
 * unrolled shift/add fast path (mir_emit_mul_hl_const) instead of __mulu.
 * Both the frame slot-count accounting (mir_multiply_by_small_constant)
 * and the actual emission site in mir_try_emit_spilled_scalar_cfg call
 * this, so they cannot fall out of sync the way they once did. */
static int mir_mul_const_fast_path_eligible(unsigned long multiplier, int dst)
{
    return multiplier == 0 ||
           (multiplier & (multiplier - 1)) == 0 ||
           (mir_mul_const_op_count(multiplier) <= MIR_MUL_CONST_MAX_OPS &&
            !(mir_value_feeds_vla_alloc(dst) && mir_has_integer_division()));
}

static int mir_multiply_by_small_constant(int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int uses = 0;
    int instruction;

    if (definition == NULL || definition->opcode != MIR_CONST)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src1 == value)
            return 0;
        if (insn->src2 != value)
            continue;
        if (insn->opcode != MIR_BINARY || insn->immediate != '*')
            return 0;
        ++uses;
    }
    if (uses != 1)
        return 0;
    {
        unsigned long multiplier =
            (unsigned long)definition->immediate & 0xffffUL;
        int multiply_instruction;
        int multiply_dst = -1;
        for (multiply_instruction = 0; multiply_instruction < mir.count;
             ++multiply_instruction)
            if (mir.insns[multiply_instruction].src2 == value) {
                multiply_dst = mir.insns[multiply_instruction].dst;
                break;
            }
        return mir_mul_const_fast_path_eligible(multiplier, multiply_dst);
    }
}

/* HL = HL * uv via a fully unrolled left-to-right binary-method shift/add
 * sequence - no runtime loop, unlike __mulu. Port of emit_mul_hl_const_general
 * in dcc_ops.c (the legacy AST backend); keeping the MIR backend's constant
 * multiplication at the same quality avoids MIR losing the cost-gate race
 * against captured legacy output for any function that multiplies by a
 * compile-time constant (array/struct element sizes, VLA row strides, etc).
 * Caller guarantees uv is nonzero, fits 16 bits, and is not a single power
 * of two (those are handled separately by the caller with plain shifts). */
static void mir_emit_mul_hl_const_general(FILE *out, unsigned long uv)
{
    int bit;
    int highest = -1;

    for (bit = 15; bit >= 0; --bit) {
        if (uv & (1uL << (unsigned)bit)) {
            highest = bit;
            break;
        }
    }
    fputs("\tld d,h\n\tld e,l\n", out);
    for (bit = highest - 1; bit >= 0; --bit) {
        fputs("\tadd hl,hl\n", out);
        if (uv & (1uL << (unsigned)bit))
            fputs("\tadd hl,de\n", out);
    }
}

/* HL = HL * v for any compile-time constant multiplier, matching the
 * legacy AST backend's emit_mul_hl_const: exact power-of-two constants
 * become plain shifts, other constants that stay within the instruction
 * budget use the general shift/add decomposition above, and anything else
 * still falls back to a runtime __mulu call. */
static void mir_emit_mul_hl_const(FILE *out, unsigned long multiplier)
{
    if (multiplier == 0) {
        fputs("\tld hl,0\n", out);
    } else if ((multiplier & (multiplier - 1)) == 0) {
        unsigned long remaining = multiplier;
        while (remaining > 1) {
            fputs("\tadd hl,hl\n", out);
            remaining >>= 1;
        }
    } else if (mir_mul_const_op_count(multiplier) <= MIR_MUL_CONST_MAX_OPS) {
        mir_emit_mul_hl_const_general(out, multiplier);
    } else {
        fprintf(out, "\tld de,%lu\n\textrn __mulu\n\tcall __mulu\n",
                multiplier);
    }
}

static int mir_emit_rematerialized_argument(FILE *out, int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    unsigned long bits;

    if ((size == 2 || size == 4) &&
        mir_load_is_single_call_argument(value, size)) {
        int memory_type;
        int memory_storage;
        int memory_offset;
        if (!mir_scalar_memory_location(definition, &memory_type,
                                        &memory_storage, &memory_offset))
            return 0;
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                memory_offset, memory_offset + 1);
        if (size == 4)
            fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                    memory_offset + 2, memory_offset + 3);
        return 1;
    }

    if (!mir_call_only_constant(value))
        return 0;
    if (definition->opcode == MIR_STRING_ADDRESS) {
        fprintf(out, "\tld hl,S%ld\n", definition->immediate);
        return 1;
    }
    bits = (unsigned long)definition->immediate;
    if (size == 4)
        fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                bits & 0xffffUL, (bits >> 16) & 0xffffUL);
    else
        fprintf(out, "\tld hl,%lu\n", bits & 0xffffUL);
    return 1;
}

static int mir_emit_cached_call_argument(FILE *out, int value)
{
    if (mir_cached_call_value != value ||
        mir_cached_call_instruction != mir_emit_instruction_index)
        return 0;
    fputs("\tld l,c\n\tld h,b\n", out);
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    return 1;
}

static int mir_emit_cached_wide_call_argument(FILE *out, int value)
{
    if (mir_cached_wide_call_value != value ||
        mir_cached_wide_call_instruction != mir_emit_instruction_index)
        return 0;
    fputs("\texx\n", out);
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    return 1;
}

static int mir_object_is_fully_promoted(int object)
{
    int instruction;

    if (object < 0 || object >= mir.object_count)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_LOAD &&
            mir.insns[instruction].object == object)
            return 0;
    return 1;
}

static int mir_object_address_taken(int object)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ADDRESS &&
            mir.insns[instruction].object == object)
            return 1;
    return 0;
}

/* True if the value written by the MIR_STORE at `instruction` can never be
 * observed: no real MIR_LOAD of the same object is reachable along any
 * successor path before the object is next stored or the function ends.
 * A backward liveness fixed point mirrors the per-value computation in
 * mir_verify_and_dump, specialised so a MIR_LOAD of the object is a "use"
 * (forces liveness) and a MIR_STORE of the object is a "kill" (the store's
 * own liveness does not depend on anything reachable before it). This
 * single mechanism covers both a dead initialisation store (never read
 * before the function ends) and a store overwritten by a later store
 * before any intervening read. Bails conservatively if the object's
 * address is ever taken, since a store could then be observed through an
 * escaped pointer that a static scan of MIR_LOAD instructions cannot see. */
static int mir_store_is_dead(int instruction)
{
    int object;
    unsigned char *live_in;
    unsigned char *live_out;
    int i;
    int changed;
    int dead;

    if (mir.insns[instruction].opcode != MIR_STORE)
        return 0;
    object = mir.insns[instruction].object;
    if (object < 0 || object >= mir.object_count ||
        mir_object_address_taken(object))
        return 0;
    live_in = (unsigned char *)calloc((size_t)mir.count, 1);
    live_out = (unsigned char *)calloc((size_t)mir.count, 1);
    if (mir.count && (live_in == NULL || live_out == NULL))
        fatal("out of memory computing MIR store liveness");
    do {
        changed = 0;
        for (i = mir.count - 1; i >= 0; --i) {
            const struct MirInsn *insn = &mir.insns[i];
            int successor;
            int next_out = 0;
            int next_in;

            for (successor = 0; successor < insn->successor_count; ++successor)
                next_out |= live_in[insn->successors[successor]];
            if (insn->opcode == MIR_LOAD && insn->object == object)
                next_in = 1;
            else if (insn->opcode == MIR_STORE && insn->object == object)
                next_in = 0;
            else
                next_in = next_out;
            if (live_out[i] != next_out || live_in[i] != next_in) {
                live_out[i] = (unsigned char)next_out;
                live_in[i] = (unsigned char)next_in;
                changed = 1;
            }
        }
    } while (changed);
    dead = !live_out[instruction];
    free(live_in);
    free(live_out);
    return dead;
}

static int mir_call_argument_cache_target(int value)
{
    int argument_instruction = -1;
    int call_id = -1;
    int call_instruction = -1;
    int instruction;

    if ((mir_value_is_wide(value) && mir_cached_wide_call_value >= 0) ||
        (!mir_value_is_wide(value) && mir_cached_call_value >= 0))
        return -1;
    for (instruction = mir_emit_instruction_index + 1;
         instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_ARG && insn->src1 == value) {
            if (argument_instruction >= 0)
                return -1;
            argument_instruction = instruction;
            call_id = insn->secondary_offset;
        } else if (insn->src1 == value || insn->src2 == value) {
            if (!(insn->opcode == MIR_STORE &&
                  mir_object_is_fully_promoted(insn->object)))
                return -1;
        }
    }
    if (argument_instruction < 0)
        return -1;
    if ((type_size(mir.insns[argument_instruction].type) > 2) !=
        mir_value_is_wide(value))
        return -1;
    for (instruction = argument_instruction + 1;
         instruction < mir.count; ++instruction)
        if ((mir.insns[instruction].opcode == MIR_CALL ||
             mir.insns[instruction].opcode == MIR_CALL_AGGREGATE) &&
            mir.insns[instruction].secondary_offset == call_id) {
            call_instruction = instruction;
            break;
        }
    if (call_instruction < 0)
        return -1;
    for (instruction = mir_emit_instruction_index + 1;
         instruction < call_instruction; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_NOP || insn->opcode == MIR_ARG)
            continue;
        if ((insn->opcode == MIR_CONST ||
             insn->opcode == MIR_FLOAT_CONST ||
             insn->opcode == MIR_STRING_ADDRESS) &&
            mir_call_only_constant(insn->dst))
            continue;
        if (insn->opcode == MIR_STORE && insn->src1 == value &&
            mir_object_is_fully_promoted(insn->object))
            continue;
        return -1;
    }
    return call_instruction;
}

static int mir_definition_is_wide(const struct MirInsn *definition)
{
    if (definition == NULL)
        return 0;
    if (definition->opcode == MIR_ADDRESS ||
        definition->opcode == MIR_COMPOUND_ADDRESS ||
        definition->opcode == MIR_INDEX_ADDRESS ||
        definition->opcode == MIR_MEMBER_ADDRESS ||
        definition->opcode == MIR_CALL_AGGREGATE)
        return 0;
    if ((definition->opcode == MIR_LOAD_INDIRECT ||
         definition->opcode == MIR_INDEX_LOAD) &&
        definition->memory_size > 2)
        return 1;
    return type_size(definition->type) > 2;
}

static int mir_load_is_single_call_argument(int value, int size)
{
    const struct MirInsn *definition = mir_definition(value);
    int argument_count = 0;
    int call_id = -1;
    int call_argument_count = 0;
    int memory_type;
    int memory_storage;
    int memory_offset;
    int instruction;

    if (definition == NULL || definition->opcode != MIR_LOAD ||
        !mir_scalar_memory_location(definition, &memory_type,
                                    &memory_storage, &memory_offset) ||
        type_size(memory_type) != size ||
        (memory_storage != SC_LOCAL && memory_storage != SC_PARAM) ||
        memory_offset < -128 || memory_offset + size - 1 > 127)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->src2 == value)
            return 0;
        if (insn->src1 != value)
            continue;
        if (insn->opcode != MIR_ARG || type_size(insn->type) != size ||
            ++argument_count > 1)
            return 0;
        call_id = insn->secondary_offset;
    }
    if (argument_count != 1)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ARG &&
            mir.insns[instruction].secondary_offset == call_id)
            ++call_argument_count;
    return call_argument_count <= 3;
}

static int mir_binary_is_fusable_comparison(int i);

/* Item 9 (mir-migration-plan-100): DCC_MIR_FUSE_REPORT=1 prints, per function,
 * how many scalar comparisons the Item 1/4 fusion caught versus how many
 * still fell through to mir_emit_scalar_compare's materialize-then-store
 * path, so future regressions in fusion coverage are visible without
 * re-reading assembly. */
static int mir_fuse_report_fused_count;
static int mir_fuse_report_materialized_count;

/* Item 8 (mir-migration-plan-100): when set, mir_prepare_backend_slots must
 * not allocate a frame slot for a comparison result (or intervening '!'
 * result) that mir_try_emit_spilled_scalar_cfg's Items 1/4 fusion consumes
 * entirely in registers - only that selector actually skips the store/load
 * for such values, so this stays off for every other caller. */
static int mir_backend_slots_skip_fused_comparisons = 0;

static int mir_prepare_backend_slots(void)
{
    int *first;
    int *last;
    int *slot_end;
    char *fused_away = NULL;
    int value;
    int i;

    if (mir.next_value <= 0) {
        mir.backend_slot_count = 0;
        return 0;
    }

    if (mir.backend_slot_capacity < mir.next_value) {
        int *new_slots = (int *)realloc(
            mir.backend_slots, (size_t)mir.next_value * sizeof(*new_slots));
        if (new_slots == NULL)
            fatal("out of memory allocating MIR backend slots");
        mir.backend_slots = new_slots;
        mir.backend_slot_capacity = mir.next_value;
    }
    first = (int *)malloc((size_t)mir.next_value * sizeof(*first));
    last = (int *)malloc((size_t)mir.next_value * sizeof(*last));
    slot_end = (int *)malloc((size_t)mir.next_value * 2 * sizeof(*slot_end));
    if (first == NULL || last == NULL || slot_end == NULL)
        fatal("out of memory computing MIR backend intervals");
    if (mir_backend_slots_skip_fused_comparisons) {
        fused_away = (char *)calloc((size_t)mir.next_value, 1);
        if (fused_away == NULL)
            fatal("out of memory computing MIR fused-comparison set");
        for (i = 0; i < mir.count; ++i) {
            int skip = mir_binary_is_fusable_comparison(i);
            if (skip > 0)
                fused_away[mir.insns[i].dst] = 1;
            if (skip == 2)
                fused_away[mir.insns[i + 1].dst] = 1;
        }
    }
    for (value = 0; value < mir.next_value; ++value) {
        first[value] = mir.count;
        last[value] = -1;
        mir.backend_slots[value] = -1;
        slot_end[value] = -1;
        slot_end[mir.next_value + value] = -1;
    }
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->dst >= 0) {
            first[insn->dst] = i;
            last[insn->dst] = i;
        }
        if (insn->src1 >= 0 && last[insn->src1] < i)
            last[insn->src1] = i;
        if (insn->src2 >= 0 && last[insn->src2] < i)
            last[insn->src2] = i;
        if (insn->opcode == MIR_CALL ||
            insn->opcode == MIR_CALL_AGGREGATE) {
            int argument;
            for (argument = 0; argument < mir.next_value; ++argument)
                if (mir_call_uses_value(insn, argument) &&
                    last[argument] < i)
                    last[argument] = i;
        }
    }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_PHI) {
            int phi_start = i;
            int predecessor;
            while (phi_start > 0 &&
               (mir.insns[phi_start - 1].opcode == MIR_PHI ||
                mir.insns[phi_start - 1].opcode == MIR_NOP))
                --phi_start;
            while (phi_start < i &&
               mir.insns[phi_start].opcode == MIR_NOP)
            ++phi_start;
            for (predecessor = 0; predecessor < mir.count; ++predecessor) {
                int successor;
                int predecessor_label = mir_block_label_before(predecessor);
                for (successor = 0;
                     successor < mir.insns[predecessor].successor_count;
                     ++successor) {
                    int target = mir_first_nonlabel_successor(
                        mir.insns[predecessor].successors[successor]);
                    if (target != phi_start)
                        continue;
                    if (predecessor_label == mir.insns[i].phi_pred1 &&
                        last[mir.insns[i].src1] < predecessor)
                        last[mir.insns[i].src1] = predecessor;
                    if (predecessor_label == mir.insns[i].phi_pred2 &&
                        last[mir.insns[i].src2] < predecessor)
                        last[mir.insns[i].src2] = predecessor;
                }
            }
        }
    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_JUMP) {
            int target = mir_find_label(mir.insns[i].label);
            if (target >= 0 && target < i)
                for (value = 0; value < mir.next_value; ++value)
                    if (first[value] < target && last[value] >= target &&
                        last[value] < i)
                        last[value] = i;
        }
    mir.backend_slot_count = 0;
    for (i = 0; i < mir.count; ++i)
        for (value = 0; value < mir.next_value; ++value)
            if (first[value] == i) {
                int slot;
                const struct MirInsn *definition = mir_definition(value);
                int units = mir_definition_is_wide(definition) ? 2 : 1;
                int reusable_source = -1;
                if (last[value] <= first[value] ||
                                        mir_call_only_constant(value) ||
                                        mir_multiply_by_small_constant(value) ||
                                        (fused_away != NULL && fused_away[value]) ||
                                        ((type_size(definition->type) == 2 ||
                                            type_size(definition->type) == 4) &&
                                         mir_load_is_single_call_argument(value,
                                                                                                            type_size(definition->type))))
                    continue;
                if (definition != NULL && definition->opcode == MIR_BINARY &&
                    ((units == 1 && type_size(definition->secondary_offset) == 2) ||
                     (units == 2 && type_size(definition->secondary_offset) == 4) ||
                     (units == 1 && type_size(definition->secondary_offset) == 4))) {
                    /* A narrow (16-bit) boolean result from a wide (32-bit)
                     * comparison still has two dying 32-bit operand units
                     * available; match reuse against the operand width, not
                     * the (narrower) result width, and take only the first
                     * unit of a wide operand's slot for the result. */
                    int operand_units =
                        type_size(definition->secondary_offset) == 4 ? 2 : 1;
                    if (definition->src1 >= 0 && last[definition->src1] == i &&
                        mir.backend_slots[definition->src1] >= 0 &&
                        (mir_definition_is_wide(mir_definition(
                             definition->src1)) ? 2 : 1) == operand_units)
                        reusable_source = definition->src1;
                    else if (definition->src2 >= 0 && last[definition->src2] == i &&
                             mir.backend_slots[definition->src2] >= 0 &&
                             (mir_definition_is_wide(mir_definition(
                                  definition->src2)) ? 2 : 1) == operand_units)
                        reusable_source = definition->src2;
                } else if (definition != NULL && definition->opcode == MIR_UNARY &&
                    definition->src1 >= 0 &&
                    (mir_definition_is_wide(mir_definition(
                         definition->src1)) ? 2 : 1) == units &&
                    last[definition->src1] == i &&
                    mir.backend_slots[definition->src1] >= 0) {
                    reusable_source = definition->src1;
                }
                if (reusable_source >= 0) {
                    int unit;
                    slot = mir.backend_slots[reusable_source];
                    mir.backend_slots[value] = slot;
                    for (unit = 0; unit < units; ++unit)
                        slot_end[slot + unit] = last[value];
                    continue;
                }
                for (slot = 0; slot + units <= mir.backend_slot_count; ++slot) {
                    int unit;
                    int available = 1;
                    for (unit = 0; unit < units; ++unit)
                        if (slot_end[slot + unit] >= i) {
                            available = 0;
                            break;
                        }
                    if (available)
                        break;
                }
                if (slot + units > mir.backend_slot_count) {
                    slot = mir.backend_slot_count;
                    mir.backend_slot_count += units;
                }
                mir.backend_slots[value] = slot;
                {
                    int unit;
                    for (unit = 0; unit < units; ++unit)
                        slot_end[slot + unit] = last[value];
                }
            }
    free(fused_away);
    free(slot_end);
    free(last);
    free(first);
    return mir.backend_slot_count;
}

static void mir_emit_virtual_load(FILE *out, int value)
{
    int offset = mir_virtual_offset(value);
    int iy_offset = mir_virtual_iy_offset(value);
    if (mir_forwarded_hl_value == value &&
        mir_forwarded_hl_instruction + 1 == mir_emit_instruction_index) {
        mir_forwarded_hl_value = -1;
        mir_forwarded_hl_instruction = -1;
        return;
    }
    if (mir_virtual_iy_base && iy_offset >= -128 && iy_offset + 1 <= 127) {
        fprintf(out, "\tld l,(iy%+d)\n\tld h,(iy%+d)\n",
                iy_offset, iy_offset + 1);
        return;
    }
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
    } else {
        fputs("\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n"
                     "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
                offset);
    }
}

static void mir_emit_virtual_store(FILE *out, int value)
{
    int has_slot = value >= 0 && value < mir.next_value &&
                   mir.backend_slots != NULL && mir.backend_slots[value] >= 0;
    int forward_instruction = mir_emit_instruction_index + 1;
    if (!has_slot)
        return;
    while (forward_instruction < mir.count &&
           mir.insns[forward_instruction].opcode == MIR_NOP)
        ++forward_instruction;
    int offset = mir_virtual_offset(value);
    int iy_offset = mir_virtual_iy_offset(value);
    int forward_to_store = mir_can_forward_hl_to_next(value) &&
        forward_instruction < mir.count &&
        mir.insns[forward_instruction].opcode == MIR_STORE;
    if (!forward_to_store && mir_can_forward_hl_to_next(value)) {
        mir_forwarded_hl_value = value;
        mir_forwarded_hl_instruction = forward_instruction - 1;
        return;
    }
    if (mir_can_forward_stack_to_index(value)) {
        fputs("\tpush hl\n", out);
        mir_forwarded_stack_value = value;
        mir_forwarded_stack_instruction = mir_emit_instruction_index;
        return;
    }
    {
        int call_instruction = mir_call_argument_cache_target(value);
        if (call_instruction >= 0) {
            fputs("\tld c,l\n\tld b,h\n", out);
            mir_cached_call_value = value;
            mir_cached_call_instruction = call_instruction;
            return;
        }
    }
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    if (mir_virtual_iy_base && iy_offset >= -128 && iy_offset + 1 <= 127) {
        fprintf(out, "\tld (iy%+d),l\n\tld (iy%+d),h\n",
                iy_offset, iy_offset + 1);
        if (forward_to_store) {
            mir_forwarded_hl_value = value;
            mir_forwarded_hl_instruction = forward_instruction - 1;
        }
        return;
    }
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1);
    } else {
        fputs("\tex de,hl\n\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n"
                     "\tld (hl),e\n\tinc hl\n\tld (hl),d\n",
                offset);
    }
    if (forward_to_store) {
        mir_forwarded_hl_value = value;
        mir_forwarded_hl_instruction = forward_instruction - 1;
    }
}

static int mir_value_is_wide(int value)
{
    return mir_definition_is_wide(mir_definition(value));
}

static int mir_divmod_partner(int instruction)
{
    const struct MirInsn *candidate;
    int partner;

    if (instruction < 0 || instruction >= mir.count)
        return -1;
    candidate = &mir.insns[instruction];
    if (candidate->opcode != MIR_BINARY ||
        (candidate->immediate != '/' && candidate->immediate != '%') ||
        type_size(candidate->secondary_offset) > 2)
        return -1;
    for (partner = 0; partner < mir.count; ++partner) {
        const struct MirInsn *other = &mir.insns[partner];
        if (partner == instruction || other->opcode != MIR_BINARY ||
            other->src1 != candidate->src1 || other->src2 != candidate->src2 ||
            other->secondary_offset != candidate->secondary_offset)
            continue;
        if ((candidate->immediate == '/' && other->immediate == '%') ||
            (candidate->immediate == '%' && other->immediate == '/'))
            return partner;
    }
    return -1;
}

static void mir_emit_virtual_load_wide(FILE *out, int value)
{
    const struct MirInsn *definition = mir_definition(value);
    int offset = mir_virtual_offset(value);
    int iy_offset = mir_virtual_iy_offset(value);

    if (!mir_definition_is_wide(definition)) {
        mir_emit_virtual_load(out, value);
        if (definition != NULL &&
            ((definition->type & TYPE_UNSIGNED) != 0 ||
             type_ptr_depth(definition->type) > 0 ||
             type_is_bool(definition->type)))
            fputs("\tld de,0\n", out);
        else
            fputs("\tld a,h\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n",
                  out);
        return;
    }
    if (mir_virtual_iy_base && iy_offset - 2 >= -128 &&
        iy_offset + 1 <= 127) {
        fprintf(out,
                "\tld l,(iy%+d)\n\tld h,(iy%+d)\n"
                "\tld e,(iy%+d)\n\tld d,(iy%+d)\n",
                iy_offset, iy_offset + 1, iy_offset - 2, iy_offset - 1);
        return;
    }
    if (offset - 2 >= -128 && offset + 1 <= 127) {
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                offset, offset + 1, offset - 2, offset - 1);
    } else {
        fputs("\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
        fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
              "\tdec hl\n\tdec hl\n\tdec hl\n"
              "\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
              "\tld h,b\n\tld l,c\n", out);
    }
}

static void mir_emit_virtual_store_wide(FILE *out, int value)
{
    int has_slot = value >= 0 && value < mir.next_value &&
                   mir.backend_slots != NULL && mir.backend_slots[value] >= 0;
    if (!has_slot)
        return;
    int offset = mir_virtual_offset(value);
    int iy_offset = mir_virtual_iy_offset(value);
    int call_instruction = mir_call_argument_cache_target(value);
    if (call_instruction >= 0) {
        fputs("\texx\n", out);
        mir_cached_wide_call_value = value;
        mir_cached_wide_call_instruction = call_instruction;
        return;
    }
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    if (mir_virtual_iy_base && iy_offset - 2 >= -128 &&
        iy_offset + 1 <= 127) {
        fprintf(out,
                "\tld (iy%+d),l\n\tld (iy%+d),h\n"
                "\tld (iy%+d),e\n\tld (iy%+d),d\n",
                iy_offset, iy_offset + 1, iy_offset - 2, iy_offset - 1);
        return;
    }
    if (offset - 2 >= -128 && offset + 1 <= 127) {
        fprintf(out,
                "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                "\tld (ix%+d),e\n\tld (ix%+d),d\n",
                offset, offset + 1, offset - 2, offset - 1);
    } else {
        fputs("\tpush de\n\tpush hl\n\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n", offset);
        fputs("\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
              "\tdec hl\n\tdec hl\n\tdec hl\n"
              "\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n", out);
    }
}

static void mir_emit_virtual_iy_epilogue(FILE *out)
{
    if (!mir_virtual_iy_base) {
        fputs("\tld sp,ix\n\tpop ix\n\tret\n", out);
        return;
    }
    fputs("\texx\n\tpush ix\n\tpop hl\n", out);
    fprintf(out, "\tld de,-%d\n\tadd hl,de\n\tld sp,hl\n",
            mir_virtual_iy_frame_bytes + 2);
    fputs("\tpop iy\n\tld sp,ix\n\tpop ix\n\texx\n\tret\n", out);
}

static void mir_emit_restore_virtual_iy(FILE *out)
{
    if (!mir_virtual_iy_base)
        return;
    fputs("\tpush ix\n\tpop iy\n", out);
    if (mir.local_bytes + mir.aggregate_temp_bytes != 0)
        fprintf(out, "\tld bc,-%d\n\tadd iy,bc\n",
                mir.local_bytes + mir.aggregate_temp_bytes);
}

static void mir_emit_frame_word_store(FILE *out, int offset)
{
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1);
    } else {
        fputs("\tex de,hl\n\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n"
                     "\tld (hl),e\n\tinc hl\n\tld (hl),d\n\tex de,hl\n",
                offset);
    }
}

static void mir_emit_frame_word_load(FILE *out, int offset)
{
    if (offset >= -128 && offset + 1 <= 127) {
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
    } else {
        fputs("\tpush ix\n\tpop hl\n", out);
        fprintf(out, "\tld de,%d\n\tadd hl,de\n"
                     "\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n",
                offset);
    }
}

static int mir_emit_scalar_operation(FILE *out, const struct MirInsn *insn)
{
    switch ((int)insn->immediate) {
    case '+': fputs("\tadd hl,de\n", out); return 1;
    case '-': fputs("\tor a\n\tsbc hl,de\n", out); return 1;
    case '&':
        fputs("\tld a,h\n\tand d\n\tld h,a\n\tld a,l\n\tand e\n\tld l,a\n", out);
        return 1;
    case '|':
        fputs("\tld a,h\n\tor d\n\tld h,a\n\tld a,l\n\tor e\n\tld l,a\n", out);
        return 1;
    case '^':
        fputs("\tld a,h\n\txor d\n\tld h,a\n\tld a,l\n\txor e\n\tld l,a\n", out);
        return 1;
    case '*': fputs("\textrn __mulu\n\tcall __mulu\n", out); return 1;
    case '/':
        fprintf(out, "\textrn %s\n\tcall %s\n",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__divu" : "__divs");
        return 1;
    case '%':
        fprintf(out, "\textrn %s\n\tcall %s\n",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods",
                (insn->type & TYPE_UNSIGNED) != 0 ? "__modu" : "__mods");
        return 1;
    case TOK_EQ: case TOK_NE: case '<': case '>': case TOK_LE: case TOK_GE:
        {
            const struct MirInsn *left = mir_definition(insn->src1);
            const struct MirInsn *right = mir_definition(insn->src2);
            int is_unsigned = (left != NULL &&
                               (left->type & TYPE_UNSIGNED) != 0) ||
                              (right != NULL &&
                               (right->type & TYPE_UNSIGNED) != 0);
            mir_emit_scalar_compare(out, (int)insn->immediate, is_unsigned);
        }
        return 1;
    case TOK_SHL: case TOK_SHR:
        {
            const struct MirInsn *left = mir_definition(insn->src1);
            mir_emit_scalar_shift(out, (int)insn->immediate,
                                  left != NULL &&
                                  (left->type & TYPE_UNSIGNED) != 0);
        }
        return 1;
    default:
        return 0;
    }
}

static int mir_emit_spilled_phi_copies(FILE *out, int predecessor,
                                       int successor);

/* Item 1 (mir-migration-plan-100): when a scalar comparison feeds nothing
 * but the MIR_BRANCH_FALSE that immediately follows it, mir_emit_scalar_compare
 * would otherwise materialize an explicit 0/1 boolean into HL, spill it to a
 * backend slot, and reload it just so the branch can re-test it with
 * `ld a,h / or l`. Test the flags directly instead and jump straight to the
 * branch's targets, eliding the materialize/spill/reload entirely.
 *
 * Item 4 extends this through a single intervening logical-not: `!(a OP b)`
 * feeding a branch is exactly the branch on the complementary operator, so
 * `mir_binary_is_fusable_comparison` returns 2 (skip compare, not, branch)
 * instead of 1 (skip compare, branch) and the caller negates the operator. */
static int mir_negate_comparison_operator(int operation)
{
    switch (operation) {
    case TOK_EQ: return TOK_NE;
    case TOK_NE: return TOK_EQ;
    case '<': return TOK_GE;
    case TOK_GE: return '<';
    case '>': return TOK_LE;
    default: return '>'; /* TOK_LE */
    }
}

static int mir_binary_is_fusable_comparison(int i)
{
    const struct MirInsn *insn = &mir.insns[i];
    const struct MirInsn *next;

    if (insn->opcode != MIR_BINARY || type_size(insn->secondary_offset) == 4)
        return 0;
    switch ((int)insn->immediate) {
    case TOK_EQ: case TOK_NE: case '<': case '>': case TOK_LE: case TOK_GE:
        break;
    default:
        return 0;
    }
    if (mir_value_use_count(insn->dst) != 1 || i + 1 >= mir.count)
        return 0;
    next = &mir.insns[i + 1];
    if (next->opcode == MIR_BRANCH_FALSE && next->src1 == insn->dst)
        return 1;
    if (next->opcode == MIR_UNARY && next->immediate == '!' &&
        next->src1 == insn->dst && !mir_value_is_wide(next->src1) &&
        mir_value_use_count(next->dst) == 1 && i + 2 < mir.count &&
        mir.insns[i + 2].opcode == MIR_BRANCH_FALSE &&
        mir.insns[i + 2].src1 == next->dst)
        return 2;
    return 0;
}

static int mir_emit_fused_comparison_branch(FILE *out, const int *labels,
                                             int compare_index, int negate)
{
    const struct MirInsn *compare = &mir.insns[compare_index];
    const struct MirInsn *branch = &mir.insns[compare_index + 1 + negate];
    const struct MirInsn *left = mir_definition(compare->src1);
    const struct MirInsn *right = mir_definition(compare->src2);
    int is_unsigned = (left != NULL && (left->type & TYPE_UNSIGNED) != 0) ||
                       (right != NULL && (right->type & TYPE_UNSIGNED) != 0);
    int operation = negate ? mir_negate_comparison_operator(
                                 (int)compare->immediate)
                           : (int)compare->immediate;
    int target;
    int fallthrough_label;
    const char *true_condition;

    if (branch->label < 0 || branch->label >= mir.next_label)
        return 0;
    target = mir_find_label(branch->label);
    if (target < 0)
        return 0;
    if (operation == '>' || operation == TOK_LE) {
        fputs("\tex de,hl\n", out);
        operation = operation == '>' ? '<' : TOK_GE;
    }
    if (!is_unsigned && operation != TOK_EQ && operation != TOK_NE)
        fputs("\tld a,h\n\txor 128\n\tld h,a\n"
              "\tld a,d\n\txor 128\n\tld d,a\n", out);
    fputs("\tor a\n\tsbc hl,de\n", out);
    switch (operation) {
    case TOK_EQ: true_condition = "z"; break;
    case TOK_NE: true_condition = "nz"; break;
    case '<': true_condition = "c"; break;
    default: true_condition = "nc"; break; /* TOK_GE */
    }
    fallthrough_label = new_label();
    fprintf(out, "\tjp %s,L%d\n", true_condition, fallthrough_label);
    if (!mir_emit_spilled_phi_copies(out, compare_index + 1 + negate, target))
        return 0;
    fprintf(out, "\tjp L%d\nL%d:\n", labels[branch->label], fallthrough_label);
    return 1;
}

static void mir_emit_hl_and_const(FILE *out, unsigned int mask)
{
    fprintf(out,
            "\tld a,l\n\tand %u\n\tld l,a\n"
            "\tld a,h\n\tand %u\n\tld h,a\n",
            mask & 0xffU, (mask >> 8) & 0xffU);
}

static void mir_emit_hl_or_const(FILE *out, unsigned int mask)
{
    fprintf(out,
            "\tld a,l\n\tor %u\n\tld l,a\n"
            "\tld a,h\n\tor %u\n\tld h,a\n",
            mask & 0xffU, (mask >> 8) & 0xffU);
}

static void mir_emit_bitfield_extract(FILE *out, const struct MirInsn *insn)
{
    int shift;
    int sign_label;
    unsigned int value_mask;

    for (shift = 0; shift < insn->bit_shift; ++shift)
        fputs("\tsrl h\n\trr l\n", out);
    value_mask = insn->bit_width >= 16
        ? 0xffffU : (1U << insn->bit_width) - 1U;
    mir_emit_hl_and_const(out, value_mask);
    if ((insn->type & TYPE_UNSIGNED) == 0 && insn->bit_width > 0 &&
        insn->bit_width < 16) {
        sign_label = new_label();
        if (insn->bit_width <= 8)
            fprintf(out, "\tbit %d,l\n", insn->bit_width - 1);
        else
            fprintf(out, "\tbit %d,h\n", insn->bit_width - 9);
        fprintf(out, "\tjp z,L%d\n", sign_label);
        mir_emit_hl_or_const(out, (~value_mask) & 0xffffU);
        fprintf(out, "L%d:\n", sign_label);
    }
}

static int mir_emit_wide_operation(FILE *out, const struct MirInsn *insn)
{
    const char *helper = NULL;
    int operation = (int)insn->immediate;
    int operand_type = insn->secondary_offset;
    if (type_is_float(operand_type)) {
        if (operation == '+' || operation == '-' || operation == '*' ||
            operation == '/') {
            helper = operation == '+' ? "__faf" : operation == '-' ? "__fsf" :
                     operation == '*' ? "__fmf" : "__fdf";
        } else if (operation == TOK_EQ || operation == TOK_NE ||
                   operation == '<' || operation == '>' ||
                   operation == TOK_LE || operation == TOK_GE) {
            helper = operation == TOK_EQ ? "__feqf" :
                     operation == TOK_NE ? "__fnef" :
                     operation == '<' ? "__fgtf" :
                     operation == '>' ? "__fltf" :
                     operation == TOK_LE ? "__fgef" : "__flef";
        } else {
            return 0;
        }
        fprintf(out, "\textrn %s\n\tcall %s\n\tpop bc\n\tpop bc\n",
                helper, helper);
        return 1;
    }
    if (operation == TOK_EQ || operation == TOK_NE) {
        int true_label = new_label();
        int end_label = new_label();
        fputs("\tpop bc\n\tld a,c\n\txor l\n\tld l,a\n"
              "\tld a,b\n\txor h\n\tor l\n\tld l,a\n"
              "\tpop bc\n\tld a,c\n\txor e\n\tor l\n\tld l,a\n"
              "\tld a,b\n\txor d\n\tor l\n", out);
        fprintf(out, operation == TOK_EQ ? "\tjp z,L%d\n" : "\tjp nz,L%d\n",
                true_label);
        fprintf(out, "\tld hl,0\n\tjp L%d\nL%d:\n\tld hl,1\nL%d:\n",
                end_label, true_label, end_label);
        return 1;
    }
    if (operation == '<' || operation == '>' || operation == TOK_LE ||
        operation == TOK_GE) {
        int is_unsigned = (operand_type & TYPE_UNSIGNED) != 0;
        helper = operation == '<' ? (is_unsigned ? "__ltu" : "__lts") :
                 operation == TOK_LE ? (is_unsigned ? "__leu" : "__les") :
                 operation == '>' ? (is_unsigned ? "__lgu" : "__lgs") :
                 (is_unsigned ? "__lku" : "__lks");
        fprintf(out, "\tpush de\n\tpush hl\n\textrn %s\n\tcall %s\n"
                     "\tex de,hl\n\tld hl,8\n\tadd hl,sp\n"
                     "\tld sp,hl\n\tex de,hl\n",
                helper, helper);
        return 1;
    }
    switch ((int)insn->immediate) {
    case '+':
        fputs("\tpop bc\n\tor a\n\tadd hl,bc\n"
              "\tex de,hl\n\tpop bc\n\tadc hl,bc\n\tex de,hl\n", out);
        return 1;
    case '-':
        fputs("\tld b,h\n\tld c,l\n\tpop hl\n\tor a\n\tsbc hl,bc\n"
              "\tld b,h\n\tld c,l\n\tpop hl\n\tsbc hl,de\n"
              "\tld d,b\n\tld e,c\n\tex de,hl\n", out);
        return 1;
    case '&': case '|': case '^':
        {
            const char *operation = insn->immediate == '&' ? "and" :
                                    insn->immediate == '|' ? "or" : "xor";
            fprintf(out,
                    "\tpop bc\n\tld a,l\n\t%s c\n\tld l,a\n"
                    "\tld a,h\n\t%s b\n\tld h,a\n"
                    "\tex de,hl\n\tpop bc\n"
                    "\tld a,l\n\t%s c\n\tld l,a\n"
                    "\tld a,h\n\t%s b\n\tld h,a\n\tex de,hl\n",
                    operation, operation, operation, operation);
        }
        return 1;
    case TOK_SHL: case TOK_SHR:
        {
            int loop_label = new_label();
            int done_label = new_label();
            fputs("\tld a,l\n\tpop hl\n\tpop de\n\tld b,a\n", out);
            fprintf(out, "L%d:\n\tld a,b\n\tor a\n\tjp z,L%d\n",
                    loop_label, done_label);
            if (insn->immediate == TOK_SHL)
                fputs("\tadd hl,hl\n\trl e\n\trl d\n", out);
            else if ((operand_type & TYPE_UNSIGNED) != 0)
                fputs("\tsrl d\n\trr e\n\trr h\n\trr l\n", out);
            else
                fputs("\tsra d\n\trr e\n\trr h\n\trr l\n", out);
            fprintf(out, "\tdec b\n\tjp L%d\nL%d:\n",
                    loop_label, done_label);
        }
        return 1;
    case '*': helper = "__lmul"; break;
    case '/': helper = (insn->type & TYPE_UNSIGNED) != 0 ? "__ldu" : "__lds"; break;
    case '%': helper = (insn->type & TYPE_UNSIGNED) != 0 ? "__lmu" : "__lms"; break;
    default: return 0;
    }
    fprintf(out, "\textrn %s\n\tcall %s\n\tpop bc\n\tpop bc\n",
            helper, helper);
    return 1;
}

static int mir_emit_cast(FILE *out, int source_type, int target_type)
{
    const char *helper;
    if (type_is_bool(target_type) && !type_is_bool(source_type)) {
        int nonzero_label = new_label();
        int end_label = new_label();
        if (type_size(source_type) > 2)
            fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
        else
            fputs("\tld a,h\n\tor l\n", out);
        fputs("\tld hl,0\n", out);
        fprintf(out, "\tjp nz,L%d\n\tjp L%d\nL%d:\n\tinc hl\nL%d:\n",
                nonzero_label, end_label, nonzero_label, end_label);
        return 1;
    }
    if (source_type == 0 || target_type == 0 || source_type == target_type)
        return 1;
    if (type_is_float(target_type) && !type_is_float(source_type)) {
        if (type_is_long(source_type))
            helper = (source_type & TYPE_UNSIGNED) != 0 ? "__fulf" : "__flf";
        else
            helper = ((source_type & TYPE_UNSIGNED) != 0 ||
                      type_ptr_depth(source_type) > 0) ? "__fuf" : "__fif";
        fprintf(out, "\textrn %s\n\tcall %s\n", helper, helper);
        return 1;
    }
    if (type_is_float(source_type) && !type_is_float(target_type)) {
        if (type_is_long(target_type))
            helper = (target_type & TYPE_UNSIGNED) != 0 ? "__fful" : "__ffl";
        else
            helper = ((target_type & TYPE_UNSIGNED) != 0 ||
                      type_ptr_depth(target_type) > 0) ? "__ffu" : "__ffi";
        fprintf(out, "\textrn %s\n\tcall %s\n", helper, helper);
        if (type_size(target_type) == 1) {
            if ((target_type & TYPE_UNSIGNED) != 0)
                fputs("\tld h,0\n", out);
            else
                fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
        }
        return 1;
    }
    if (type_size(target_type) == 4 && type_size(source_type) <= 2) {
        if ((source_type & TYPE_UNSIGNED) != 0 ||
            type_ptr_depth(source_type) > 0)
            fputs("\tld de,0\n", out);
        else
            fputs("\tld a,h\n\trlca\n\tsbc a,a\n\tld d,a\n\tld e,a\n", out);
        return 1;
    }
    if (type_size(target_type) == 1) {
        if ((target_type & TYPE_UNSIGNED) != 0)
            fputs("\tld h,0\n", out);
        else
            fputs("\tld a,l\n\trlca\n\tsbc a,a\n\tld h,a\n", out);
        return 1;
    }
    if (type_size(target_type) <= 2 && type_size(source_type) == 4)
        return 1;
    return 1;
}

static int mir_emit_spilled_phi_copies(FILE *out, int predecessor,
                                       int successor)
{
    int predecessor_label = mir_block_label_before(predecessor);
    int edge_label = -1;
    int instruction = mir_first_nonlabel_successor(successor);
    int sources[MAX_FLOW];
    int destinations[MAX_FLOW];
    int copy_count = 0;
    int copy;

    if (predecessor >= 0 && predecessor < mir.count &&
        (mir.insns[predecessor].opcode == MIR_JUMP ||
         mir.insns[predecessor].opcode == MIR_BRANCH_FALSE) &&
        mir_find_label(mir.insns[predecessor].label) == successor)
        edge_label = mir.insns[predecessor].label;

    while (instruction >= 0 && instruction < mir.count &&
           (mir.insns[instruction].opcode == MIR_PHI ||
            mir.insns[instruction].opcode == MIR_NOP)) {
        const struct MirInsn *phi = &mir.insns[instruction];
        int source;
        if (phi->opcode == MIR_NOP) {
            ++instruction;
            continue;
        }
        if (!mir_value_has_use(phi->dst)) {
            ++instruction;
            continue;
        }
        source = mir_phi_source_for_edge(phi, predecessor_label, edge_label,
                                         successor, instruction);
        if (source < 0)
            return 0;
        if (copy_count >= (int)(sizeof(sources) / sizeof(sources[0])))
            return 0;
        sources[copy_count] = source;
        destinations[copy_count] = phi->dst;
        ++copy_count;
        ++instruction;
    }
    for (copy = 0; copy < copy_count; ++copy) {
        if (mir_value_is_wide(sources[copy])) {
            mir_emit_virtual_load_wide(out, sources[copy]);
            fputs("\tpush de\n\tpush hl\n", out);
        } else {
            mir_emit_virtual_load(out, sources[copy]);
            fputs("\tpush hl\n", out);
        }
    }
    for (copy = copy_count - 1; copy >= 0; --copy) {
        if (mir_value_is_wide(destinations[copy])) {
            fputs("\tpop hl\n\tpop de\n", out);
            mir_emit_virtual_store_wide(out, destinations[copy]);
        } else {
            fputs("\tpop hl\n", out);
            mir_emit_virtual_store(out, destinations[copy]);
        }
    }
    return 1;
}

static int mir_scalar_memory_location(const struct MirInsn *insn, int *type,
                                      int *storage, int *offset)
{
    struct Sym *global;
    int instruction;
    if (insn->object >= 0 && insn->object < mir.object_count) {
        const struct MirObject *object = &mir.objects[insn->object];
        *type = object->type;
        *storage = object->storage;
        *offset = object->offset + (int)insn->immediate;
        return 1;
    }
    if (mir_declared_location(insn->name, type, storage, offset)) {
        *offset += (int)insn->immediate;
        return 1;
    }
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_COMPOUND_ADDRESS &&
            strcmp(mir.insns[instruction].name, insn->name) == 0) {
            *type = insn->type;
            *storage = SC_LOCAL;
            *offset = (int)mir.insns[instruction].immediate +
                      (int)insn->immediate;
            return 1;
        }
    global = find_global(insn->name);
    if (global != NULL) {
        *type = global->type;
        *storage = global->storage;
        *offset = global->offset + (int)insn->immediate;
        return 1;
    }
    return 0;
}

static int mir_scalar_cfg_preflight_reject(const char *reason, int instruction)
{
    if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr,
                "; MIR scalar-cfg preflight function=%s reason=%s insn=%d\n",
                mir.name, reason, instruction);
    return 0;
}

static int mir_try_emit_spilled_scalar_cfg(FILE *out)
{
    int *labels;
    int frame_bytes;
    int i;
    int accepted = 0;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_RETURN)
            break;
    if (i == mir.count && (mir.return_type & 15) != TYPE_VOID)
        return mir_scalar_cfg_preflight_reject("implicit-return", -1);

    if ((!type_is_struct_object(mir.return_type) &&
            (mir.return_type & 15) != TYPE_VOID &&
         type_size(mir.return_type) > 4) ||
        (type_is_struct_object(mir.return_type) &&
         (type_size(mir.return_type) <= 0 || type_size(mir.return_type) > 1024)))
        return mir_scalar_cfg_preflight_reject("return-type", -1);
    mir_fuse_report_fused_count = 0;
    mir_fuse_report_materialized_count = 0;
    mir_backend_slots_skip_fused_comparisons = 1;
    frame_bytes = mir.local_bytes + mir.aggregate_temp_bytes +
                  2 * mir_prepare_backend_slots();
    mir_backend_slots_skip_fused_comparisons = 0;
    if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr,
                "; MIR scalar-cfg frame function=%s locals=%d slots=%d bytes=%d\n",
                mir.name, mir.local_bytes + mir.aggregate_temp_bytes,
                mir.backend_slot_count, frame_bytes);
    if (frame_bytes < 0 || frame_bytes > 30000)
        return mir_scalar_cfg_preflight_reject("frame-size", -1);
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
                if (insn->dst >= 0 && type_size(insn->type) > 4 &&
                        !(insn->opcode == MIR_PARAM &&
                            type_is_struct_object(insn->type)))
            return mir_scalar_cfg_preflight_reject("wide-value", i);
        switch (insn->opcode) {
        case MIR_NOP: case MIR_PARAM: case MIR_CONST: case MIR_FLOAT_CONST:
        case MIR_STRING_ADDRESS:
        case MIR_ADDRESS: case MIR_COMPOUND_ADDRESS: case MIR_INDEX_ADDRESS:
        case MIR_MEMBER_ADDRESS: case MIR_VLA_SIZE: case MIR_LOAD:
        case MIR_LOAD_INDIRECT:
        case MIR_STORE: case MIR_STORE_INDIRECT: case MIR_UNARY:
        case MIR_VLA_SAVE: case MIR_VLA_ALLOC: case MIR_VLA_RESTORE:
        case MIR_COPY_AGGREGATE: case MIR_VA_START: case MIR_VA_END:
        case MIR_VA_ARG:
        case MIR_BINARY: case MIR_ARG: case MIR_CALL: case MIR_CALL_AGGREGATE:
        case MIR_LABEL:
        case MIR_JUMP: case MIR_BRANCH_FALSE: case MIR_PHI: case MIR_RETURN:
            break;
        default:
            return mir_scalar_cfg_preflight_reject("opcode", i);
        }
        if (insn->opcode == MIR_LOAD || insn->opcode == MIR_STORE ||
            insn->opcode == MIR_PARAM || insn->opcode == MIR_ADDRESS) {
            int memory_type;
            int memory_storage;
            int memory_offset;
            if (!mir_scalar_memory_location(insn, &memory_type,
                            &memory_storage, &memory_offset) ||
                (memory_storage != SC_LOCAL && memory_storage != SC_PARAM &&
                 memory_storage != SC_GLOBAL && memory_storage != SC_EXTERN &&
                 memory_storage != SC_FUNC)) {
                if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
                    fprintf(stderr,
                            "; MIR unresolved-memory function=%s insn=%d opcode=%s name=%s object=%d\n",
                            mir.name, i, mir_opcode_name(insn->opcode),
                            insn->name, insn->object);
                return mir_scalar_cfg_preflight_reject("memory-location", i);
            }
        }
           if ((insn->opcode == MIR_LOAD_INDIRECT ||
               insn->opcode == MIR_STORE_INDIRECT) &&
            (insn->memory_size <= 0 ||
             (insn->memory_size != 1 && insn->memory_size != 2 &&
              insn->memory_size != 4) ||
             (insn->bit_width > 0 && insn->memory_size != 2)))
            return mir_scalar_cfg_preflight_reject("indirect-width", i);
        if (insn->opcode == MIR_CALL &&
            ((strcmp(insn->name, "<indirect>") == 0 && insn->src1 < 0) ||
             type_size(insn->type) > 4))
            return mir_scalar_cfg_preflight_reject("call-abi", i);
        if (insn->opcode == MIR_CALL_AGGREGATE &&
            (strcmp(insn->name, "<indirect>") == 0 ||
             insn->memory_size <= 0))
            return mir_scalar_cfg_preflight_reject("aggregate-call-abi", i);
        if (insn->opcode == MIR_VA_ARG &&
            (insn->immediate < -128 || insn->immediate + 1 > 127 ||
             (insn->secondary_offset != 2 && insn->secondary_offset != 4)))
            return mir_scalar_cfg_preflight_reject("va-arg", i);
    }
    labels = (int *)malloc((size_t)mir.next_label * sizeof(*labels));
    if (labels == NULL)
        fatal("out of memory selecting MIR labels");
    for (i = 0; i < mir.next_label; ++i)
        labels[i] = new_label();

    fputs("\tpush ix\n\tld ix,0\n\tadd ix,sp\n", out);
    if (frame_bytes != 0)
        fprintf(out, "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n", frame_bytes);
    mir_virtual_iy_base = 0;
    mir_virtual_iy_frame_bytes = frame_bytes;
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    mir_forwarded_stack_value = -1;
    mir_forwarded_stack_instruction = -1;
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    mir_cached_wide_call_value = -1;
    mir_cached_wide_call_instruction = -1;
    if (opt_stack_check)
        fputs("\textrn __stchk\n\tcall __stchk\n", out);
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        int end_label;

        mir_emit_instruction_index = i;

        switch (insn->opcode) {
        case MIR_NOP:
            break;
        case MIR_LABEL:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            fprintf(out, "L%d:\n", labels[insn->label]);
            break;
        case MIR_PHI:
            break;
        case MIR_PARAM:
        case MIR_LOAD:
            {
            int memory_type;
            int memory_storage;
            int memory_offset;
            if (insn->dst >= 0 && mir.backend_slots != NULL &&
                mir.backend_slots[insn->dst] < 0 &&
                !mir_value_has_use(insn->dst))
                break;
            if ((type_size(insn->type) == 2 || type_size(insn->type) == 4) &&
                mir_load_is_single_call_argument(insn->dst,
                                                  type_size(insn->type)))
                break;
            if (!mir_scalar_memory_location(insn, &memory_type,
                                            &memory_storage, &memory_offset))
                goto done;
            if (insn->memory_size > 0 &&
                !type_is_struct_object(insn->type))
                memory_type = insn->type;
            if (type_is_struct_object(memory_type)) {
                if (memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN) {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global) : insn->name);
                    if (memory_storage == SC_EXTERN)
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tld hl,%s\n", assembly_name);
                } else {
                    fputs("\tpush ix\n\tpop hl\n", out);
                    if (memory_offset != 0)
                        fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                                memory_offset);
                }
                mir_emit_virtual_store(out, insn->dst);
                break;
            }
            if ((memory_storage == SC_LOCAL || memory_storage == SC_PARAM) &&
                (memory_offset < -128 ||
                 memory_offset + type_size(memory_type) - 1 > 127)) {
                fputs("\tpush ix\n\tpop hl\n", out);
                fprintf(out, "\tld de,%d\n\tadd hl,de\n", memory_offset);
                if (type_size(memory_type) == 4) {
                    fputs("\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                          "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                          "\tld h,b\n\tld l,c\n", out);
                    mir_emit_virtual_store_wide(out, insn->dst);
                } else {
                    fputs("\tld a,(hl)\n", out);
                    if (type_size(memory_type) == 2)
                        fputs("\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
                    else if (type_is_bool(memory_type)) {
                        int bool_label = new_label();
                        fputs("\tor a\n\tld hl,0\n", out);
                        fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                                bool_label, bool_label);
                    } else if ((memory_type & TYPE_UNSIGNED) != 0)
                        fputs("\tld l,a\n\tld h,0\n", out);
                    else {
                        int sign_label = new_label();
                        fputs("\tld l,a\n\tld h,0\n\tbit 7,l\n", out);
                        fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                                sign_label, sign_label);
                    }
                    mir_emit_virtual_store(out, insn->dst);
                }
                break;
            }
            if (memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN ||
                memory_storage == SC_FUNC) {
                struct Sym *global = find_global(insn->name);
                const char *assembly_name = asm_name_for(
                    global != NULL ? sym_asm_name(global)
                                   : mir_declared_link_name(insn->name));
                if (memory_storage == SC_EXTERN ||
                    (memory_storage == SC_FUNC && global != NULL &&
                     global->needs_extrn))
                    fprintf(out, "\textrn %s\n", assembly_name);
                if (type_size(memory_type) == 4 &&
                    memory_storage == SC_EXTERN)
                    fprintf(out,
                            "\tld hl,%s\n"
                            "\tld c,(hl)\n\tinc hl\n\tld b,(hl)\n"
                            "\tinc hl\n\tld e,(hl)\n\tinc hl\n\tld d,(hl)\n"
                            "\tld h,b\n\tld l,c\n",
                            assembly_name);
                else if (type_size(memory_type) == 4)
                    fprintf(out, "\tld hl,(%s)\n\tld de,(%s+2)\n",
                            assembly_name, assembly_name);
                else if (type_size(memory_type) == 1)
                    fprintf(out, "\tld a,(%s)\n\tld l,a\n", assembly_name);
                else
                    fprintf(out, "\tld hl,(%s)\n", assembly_name);
            } else {
                fprintf(out, "\tld l,(ix%+d)\n", memory_offset);
            }
            if (type_size(memory_type) == 4) {
                if (memory_storage != SC_GLOBAL && memory_storage != SC_EXTERN)
                    fprintf(out,
                            "\tld h,(ix%+d)\n\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                            memory_offset + 1, memory_offset + 2,
                            memory_offset + 3);
                mir_emit_virtual_store_wide(out, insn->dst);
                break;
            }
            if (type_size(memory_type) == 1) {
                if (type_is_bool(memory_type)) {
                    int bool_label = new_label();
                    fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                    fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                            bool_label, bool_label);
                } else if ((memory_type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    end_label = new_label();
                    fputs("\tld h,0\n\tbit 7,l\n", out);
                    fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                            end_label, end_label);
                }
            } else if (memory_storage != SC_GLOBAL &&
                       memory_storage != SC_EXTERN) {
                fprintf(out, "\tld h,(ix%+d)\n", memory_offset + 1);
            }
            mir_emit_virtual_store(out, insn->dst);
            break;
            }
        case MIR_CONST:
            if (!mir_value_has_use(insn->dst) ||
                mir_call_only_constant(insn->dst) ||
                mir_binary_only_constant(insn->dst) ||
                mir_multiply_by_small_constant(insn->dst))
                break;
            fprintf(out, "\tld hl,%ld\n", insn->immediate & 0xffffL);
            if (type_size(insn->type) == 4) {
                fprintf(out, "\tld de,%lu\n",
                        ((unsigned long)insn->immediate >> 16) & 0xffffUL);
                mir_emit_virtual_store_wide(out, insn->dst);
            } else {
                mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_FLOAT_CONST:
            if (mir_call_only_constant(insn->dst))
                break;
            fprintf(out, "\tld hl,%lu\n\tld de,%lu\n",
                    (unsigned long)insn->immediate & 0xffffUL,
                    ((unsigned long)insn->immediate >> 16) & 0xffffUL);
            mir_emit_virtual_store_wide(out, insn->dst);
            break;
        case MIR_STRING_ADDRESS:
            if (mir_call_only_constant(insn->dst))
                break;
            fprintf(out, "\tld hl,S%ld\n", insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_ADDRESS:
            {
            int memory_type;
            int memory_storage;
            int memory_offset;
            struct Sym *global = find_global(insn->name);
            if (!mir_scalar_memory_location(insn, &memory_type,
                                            &memory_storage, &memory_offset))
                goto done;
            if ((global != NULL && global->storage == SC_FUNC) ||
                memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN ||
                memory_storage == SC_FUNC) {
                const char *assembly_name = asm_name_for(
                    global != NULL ? sym_asm_name(global)
                                   : mir_declared_link_name(insn->name));
                if (memory_storage == SC_EXTERN ||
                    (global != NULL && global->storage == SC_FUNC &&
                     global->needs_extrn))
                    fprintf(out, "\textrn %s\n", assembly_name);
                fprintf(out, "\tld hl,%s\n", assembly_name);
            } else if (mir_declared_is_vla_object(insn->name)) {
                fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                        memory_offset, memory_offset + 1);
            } else {
                fputs("\tpush ix\n\tpop hl\n", out);
                if (memory_offset != 0)
                    fprintf(out, "\tld de,%d\n\tadd hl,de\n", memory_offset);
            }
            mir_emit_virtual_store(out, insn->dst);
            break;
            }
        case MIR_COMPOUND_ADDRESS:
            fputs("\tpush ix\n\tpop hl\n", out);
            if (insn->immediate != 0)
                fprintf(out, "\tld de,%ld\n\tadd hl,de\n", insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_VLA_SIZE:
            if (insn->immediate < -128 || insn->immediate + 1 > 127)
                goto done;
            fprintf(out, "\tld l,(ix%+ld)\n\tld h,(ix%+ld)\n",
                    insn->immediate, insn->immediate + 1);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_MEMBER_ADDRESS:
            mir_emit_virtual_load(out, insn->src1);
            if (insn->immediate != 0)
                fprintf(out, "\tld de,%ld\n\tadd hl,de\n", insn->immediate);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_INDEX_ADDRESS:
            {
            const struct MirInsn *index_definition =
                mir_definition(insn->src2);
            if (insn->base_name[0] != 0) {
                int stride_type;
                int stride_storage;
                int stride_offset;
                if (!mir_declared_location(insn->base_name, &stride_type,
                                           &stride_storage, &stride_offset))
                    goto done;
                mir_emit_virtual_load(out, insn->src2);
                fputs("\tpush hl\n", out);
                if (stride_storage == SC_GLOBAL ||
                    stride_storage == SC_EXTERN)
                    fprintf(out, "\tld de,(%s)\n", asm_name_for(
                        mir_declared_link_name(insn->base_name)));
                else
                    fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                            stride_offset, stride_offset + 1);
                fputs("\tpop hl\n\textrn __mulu\n\tcall __mulu\n", out);
                if (insn->secondary_offset == 2)
                    fputs("\tadd hl,hl\n", out);
                else if (insn->secondary_offset > 2) {
                    fprintf(out, "\tld de,%d\n\textrn __mulu\n"
                                 "\tcall __mulu\n",
                            insn->secondary_offset);
                }
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpop de\n\tadd hl,de\n", out);
                mir_emit_virtual_store(out, insn->dst);
                break;
            }
            if (index_definition != NULL &&
                index_definition->opcode == MIR_CONST) {
                long byte_offset = index_definition->immediate *
                                   insn->immediate;
                if (mir_forwarded_hl_value == insn->src2 &&
                    mir_forwarded_hl_instruction + 1 == i) {
                    mir_forwarded_hl_value = -1;
                    mir_forwarded_hl_instruction = -1;
                }
                if (mir_forwarded_stack_value == insn->src1 &&
                    mir_forwarded_stack_instruction + 2 == i) {
                    fputs("\tpop hl\n", out);
                    mir_forwarded_stack_value = -1;
                    mir_forwarded_stack_instruction = -1;
                } else
                    mir_emit_virtual_load(out, insn->src1);
                if (byte_offset != 0)
                    fprintf(out, "\tld de,%ld\n\tadd hl,de\n",
                            byte_offset & 0xffffL);
            } else {
                mir_emit_virtual_load(out, insn->src2);
                if (insn->immediate != 1) {
                    fprintf(out,
                            "\tld de,%ld\n\textrn __mulu\n\tcall __mulu\n",
                            insn->immediate);
                }
                if (mir_forwarded_stack_value == insn->src1 &&
                    mir_forwarded_stack_instruction + 2 == i) {
                    fputs("\tpop de\n\tadd hl,de\n", out);
                    mir_forwarded_stack_value = -1;
                    mir_forwarded_stack_instruction = -1;
                } else {
                    fputs("\tpush hl\n", out);
                    mir_emit_virtual_load(out, insn->src1);
                    fputs("\tpop de\n\tadd hl,de\n", out);
                }
            }
            mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_LOAD_INDIRECT:
            mir_emit_virtual_load(out, insn->src1);
            if (insn->memory_size == 4) {
                fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
                      "\tld h,(hl)\n\tld l,a\n\tex (sp),hl\n"
                      "\tinc hl\n\tinc hl\n\tld e,(hl)\n\tinc hl\n"
                      "\tld d,(hl)\n\tpop hl\n", out);
            } else if (insn->memory_size == 1) {
                fputs("\tld l,(hl)\n", out);
                if (type_is_bool(insn->type)) {
                    end_label = new_label();
                    fputs("\tld a,l\n\tor a\n\tld hl,0\n", out);
                    fprintf(out, "\tjp z,L%d\n\tinc hl\nL%d:\n",
                            end_label, end_label);
                } else if ((insn->type & TYPE_UNSIGNED) != 0) {
                    fputs("\tld h,0\n", out);
                } else {
                    end_label = new_label();
                    fputs("\tld h,0\n\tbit 7,l\n", out);
                    fprintf(out, "\tjp z,L%d\n\tdec h\nL%d:\n",
                            end_label, end_label);
                }
            } else {
                fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
            }
            if (insn->bit_width > 0)
                mir_emit_bitfield_extract(out, insn);
            if (insn->memory_size == 4)
                mir_emit_virtual_store_wide(out, insn->dst);
            else
                mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_STORE:
            {
            int memory_type;
            int memory_storage;
            int memory_offset;
            if (mir_object_is_fully_promoted(insn->object) ||
                mir_store_is_dead(i))
                break;
            if (!mir_scalar_memory_location(insn, &memory_type,
                                            &memory_storage, &memory_offset))
                goto done;
            if (insn->memory_size > 0 &&
                !type_is_struct_object(insn->type))
                memory_type = insn->type;
            if (type_is_struct_object(memory_type)) {
                int byte;
                int size = type_size(memory_type);
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tex de,hl\n", out);
                if (memory_storage == SC_GLOBAL ||
                    memory_storage == SC_EXTERN) {
                    struct Sym *global = find_global(insn->name);
                    const char *assembly_name = asm_name_for(
                        global != NULL ? sym_asm_name(global)
                                       : mir_declared_link_name(insn->name));
                    if (memory_storage == SC_EXTERN)
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tld hl,%s\n", assembly_name);
                } else {
                    fputs("\tpush ix\n\tpop hl\n", out);
                    if (memory_offset != 0)
                        fprintf(out, "\tld bc,%d\n\tadd hl,bc\n",
                                memory_offset);
                }
                for (byte = 0; byte < size; ++byte) {
                    fputs("\tld a,(de)\n\tld (hl),a\n", out);
                    if (byte + 1 < size)
                        fputs("\tinc de\n\tinc hl\n", out);
                }
                break;
            }
            if ((memory_storage == SC_LOCAL || memory_storage == SC_PARAM) &&
                (memory_offset < -128 ||
                 memory_offset + type_size(memory_type) - 1 > 127)) {
                if (type_size(memory_type) == 4) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    fputs("\tpush de\n\tpush hl\n\tpush ix\n\tpop hl\n", out);
                    fprintf(out, "\tld de,%d\n\tadd hl,de\n", memory_offset);
                    fputs("\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n"
                          "\tinc hl\n\tpop bc\n\tld (hl),c\n\tinc hl\n\tld (hl),b\n",
                          out);
                } else {
                    mir_emit_virtual_load(out, insn->src1);
                    fputs("\tex de,hl\n\tpush ix\n\tpop hl\n", out);
                    fprintf(out, "\tld bc,%d\n\tadd hl,bc\n\tld (hl),e\n",
                            memory_offset);
                    if (type_size(memory_type) == 2)
                        fputs("\tinc hl\n\tld (hl),d\n", out);
                }
                break;
            }
            mir_emit_virtual_load(out, insn->src1);
            if (memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN) {
                struct Sym *global = find_global(insn->name);
                const char *assembly_name = asm_name_for(
                    global != NULL ? sym_asm_name(global)
                                   : mir_declared_link_name(insn->name));
                if (memory_storage == SC_EXTERN)
                    fprintf(out, "\textrn %s\n", assembly_name);
                if (type_size(memory_type) == 4) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    if (memory_storage == SC_EXTERN)
                        fprintf(out,
                                "\tpush de\n\tpush hl\n\tld hl,%s\n"
                                "\tpop bc\n\tld (hl),c\n\tinc hl\n"
                                "\tld (hl),b\n\tinc hl\n\tpop bc\n"
                                "\tld (hl),c\n\tinc hl\n\tld (hl),b\n",
                                assembly_name);
                    else
                        fprintf(out, "\tld (%s),hl\n\tld (%s+2),de\n",
                                assembly_name, assembly_name);
                } else if (type_size(memory_type) == 1)
                    fprintf(out, "\tld a,l\n\tld (%s),a\n", assembly_name);
                else
                    fprintf(out, "\tld (%s),hl\n", assembly_name);
            } else {
                if (type_size(memory_type) == 4) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    fprintf(out,
                            "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                            "\tld (ix%+d),e\n\tld (ix%+d),d\n",
                            memory_offset, memory_offset + 1,
                            memory_offset + 2, memory_offset + 3);
                } else {
                    fprintf(out, "\tld (ix%+d),l\n", memory_offset);
                }
                if (type_size(memory_type) == 2)
                    fprintf(out, "\tld (ix%+d),h\n", memory_offset + 1);
            }
            break;
            }
        case MIR_STORE_INDIRECT:
            if (insn->bit_width > 0) {
                int shift;
                mir_emit_virtual_load(out, insn->src2);
                mir_emit_hl_and_const(
                    out, insn->bit_width >= 16
                        ? 0xffffU : (1U << insn->bit_width) - 1U);
                for (shift = 0; shift < insn->bit_shift; ++shift)
                    fputs("\tadd hl,hl\n", out);
                mir_emit_hl_and_const(out, insn->bit_mask);
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
                      "\tld h,(hl)\n\tld l,a\n", out);
                mir_emit_hl_and_const(out, (~insn->bit_mask) & 0xffffU);
                fputs("\tpop bc\n\tpop de\n"
                      "\tld a,l\n\tor e\n\tld l,a\n"
                      "\tld a,h\n\tor d\n\tld h,a\n"
                      "\tex de,hl\n\tld h,b\n\tld l,c\n"
                      "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
                break;
            }
            if (insn->memory_size == 4) {
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load_wide(out, insn->src2);
                fputs("\tpop bc\n\tpush de\n\tex de,hl\n"
                      "\tld h,b\n\tld l,c\n\tld (hl),e\n\tinc hl\n"
                      "\tld (hl),d\n\tinc hl\n\tpop de\n"
                      "\tld (hl),e\n\tinc hl\n\tld (hl),d\n", out);
                break;
            }
            mir_emit_virtual_load(out, insn->src1);
            fputs("\tpush hl\n", out);
            mir_emit_virtual_load(out, insn->src2);
            fputs("\tex de,hl\n\tpop hl\n\tld (hl),e\n", out);
            if (insn->memory_size > 1)
                fputs("\tinc hl\n\tld (hl),d\n", out);
            break;
        case MIR_VLA_SAVE:
            fputs("\tld hl,0\n\tadd hl,sp\n", out);
            mir_emit_frame_word_store(out, (int)insn->immediate);
            break;
        case MIR_VLA_ALLOC:
            mir_emit_virtual_load(out, insn->src1);
            mir_emit_frame_word_store(out, insn->secondary_offset);
            fputs("\tex de,hl\n\tld hl,0\n\tadd hl,sp\n"
                  "\tor a\n\tsbc hl,de\n\tld sp,hl\n", out);
            if (opt_stack_check)
                fputs("\textrn __stchk\n\tcall __stchk\n", out);
            fputs("\tld hl,0\n\tadd hl,sp\n", out);
            mir_emit_frame_word_store(out, (int)insn->immediate);
            break;
        case MIR_VLA_RESTORE:
            if (insn->immediate == 0)
                break;
            mir_emit_frame_word_load(out, (int)insn->immediate);
            fputs("\tld sp,hl\n", out);
            break;
        case MIR_COPY_AGGREGATE:
            {
                int byte;
                if (insn->memory_size <= 0 || insn->memory_size > 1024)
                    goto done;
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tpush hl\n", out);
                mir_emit_virtual_load(out, insn->src2);
                fputs("\tld b,h\n\tld c,l\n\tpop hl\n", out);
                for (byte = 0; byte < insn->memory_size; ++byte) {
                    fputs("\tld a,(bc)\n\tld (hl),a\n", out);
                    if (byte + 1 < insn->memory_size)
                        fputs("\tinc bc\n\tinc hl\n", out);
                }
            }
            break;
        case MIR_UNARY:
            if (mir_value_is_wide(insn->src1))
                mir_emit_virtual_load_wide(out, insn->src1);
            else
                mir_emit_virtual_load(out, insn->src1);
            if (insn->immediate == 0) {
                const struct MirInsn *source = mir_definition(insn->src1);
                if (!mir_emit_cast(out, source != NULL ? source->type : 0,
                                   insn->type))
                    goto done;
            } else if (insn->immediate == '+') {
                /* Unary plus. */
            } else if (insn->immediate == '-') {
                if (type_is_float(insn->type)) {
                    fputs("\tld a,d\n\txor 128\n\tld d,a\n", out);
                } else if (mir_value_is_wide(insn->src1)) {
                    int carry_label = new_label();
                    fputs("\tld a,l\n\tcpl\n\tld l,a\n"
                          "\tld a,h\n\tcpl\n\tld h,a\n"
                          "\tld a,e\n\tcpl\n\tld e,a\n"
                          "\tld a,d\n\tcpl\n\tld d,a\n"
                          "\tinc hl\n\tld a,h\n\tor l\n", out);
                      fprintf(out, "\tjp nz,L%d\n\tinc de\nL%d:\n",
                            carry_label, carry_label);
                    } else
                    fputs("\txor a\n\tsub l\n\tld l,a\n\tsbc a,a\n\tsub h\n\tld h,a\n", out);
            } else if (insn->immediate == '~') {
                fputs("\tld a,l\n\tcpl\n\tld l,a\n\tld a,h\n\tcpl\n\tld h,a\n", out);
                if (mir_value_is_wide(insn->src1))
                    fputs("\tld a,e\n\tcpl\n\tld e,a\n"
                          "\tld a,d\n\tcpl\n\tld d,a\n", out);
            } else if (insn->immediate == '!') {
                int true_label = new_label();
                end_label = new_label();
                if (mir_value_is_wide(insn->src1))
                    fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n\tld hl,0\n", out);
                else
                    fputs("\tld a,h\n\tor l\n\tld hl,0\n", out);
                fprintf(out, "\tjp z,L%d\n\tjp L%d\nL%d:\n\tinc hl\nL%d:\n",
                        true_label, end_label, true_label, end_label);
            } else {
                goto done;
            }
            if (type_size(insn->type) == 4)
                mir_emit_virtual_store_wide(out, insn->dst);
            else
                mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_BINARY:
            if (type_size(insn->secondary_offset) == 4) {
                mir_emit_virtual_load_wide(out, insn->src1);
                fputs("\tpush de\n\tpush hl\n", out);
                mir_emit_virtual_load_wide(out, insn->src2);
                if (!mir_emit_wide_operation(out, insn))
                    goto done;
                if (type_size(insn->type) == 4)
                    mir_emit_virtual_store_wide(out, insn->dst);
                else
                    mir_emit_virtual_store(out, insn->dst);
            } else {
                int divmod_partner = mir_divmod_partner(i);
                const struct MirInsn *right_definition =
                    mir_definition(insn->src2);
                unsigned long multiplier = right_definition != NULL
                    ? (unsigned long)right_definition->immediate & 0xffffUL
                    : 0;
                if (mir_binary_only_constant(insn->src1)) {
                    const struct MirInsn *constant =
                        mir_definition(insn->src1);
                    fprintf(out, "\tld hl,%ld\n",
                            constant->immediate & 0xffffL);
                } else
                    mir_emit_virtual_load(out, insn->src1);
                if (divmod_partner >= 0) {
                    const struct MirInsn *other = &mir.insns[divmod_partner];
                    int modulo_value = insn->immediate == '%' ? insn->dst
                                                               : other->dst;
                    int division_value = insn->immediate == '/' ? insn->dst
                                                                 : other->dst;
                    int saved_instruction = mir_emit_instruction_index;
                    if (divmod_partner < i)
                        break;
                    fputs("\tpush hl\n", out);
                    mir_emit_virtual_load(out, insn->src2);
                    fputs("\tex de,hl\n\tpop hl\n", out);
                    if ((insn->secondary_offset & TYPE_UNSIGNED) != 0)
                        fputs("\textrn __udivmod\n\tcall __udivmod\n", out);
                    else
                        fputs("\textrn __sdivmod\n\tcall __sdivmod\n", out);
                    mir_emit_instruction_index = -1;
                    fputs("\tpush hl\n\tex de,hl\n", out);
                    mir_emit_virtual_store(out, modulo_value);
                    fputs("\tpop hl\n", out);
                    mir_emit_virtual_store(out, division_value);
                    mir_emit_instruction_index = saved_instruction;
                    break;
                }
                if (insn->immediate == '*' && right_definition != NULL &&
                    right_definition->opcode == MIR_CONST &&
                    mir_mul_const_fast_path_eligible(multiplier, insn->dst)) {
                    mir_emit_mul_hl_const(out, multiplier);
                    mir_emit_virtual_store(out, insn->dst);
                    break;
                }
                /* A constant divisor/modulus needs no dividend-preserving
                 * push/pop dance: the dividend is already sitting in HL
                 * from src1's evaluation above and a constant load cannot
                 * clobber it, so the divisor can be materialized directly
                 * into DE (Item 16). Every other operator keeps the
                 * existing push/ex/pop sequence, since src2 there may be
                 * evaluated via a call or memory access that does clobber
                 * HL.
                 *
                 * Skip this shortcut in functions with a VLA: shaving a
                 * few bytes off this one instruction can tip a borderline
                 * function's byte-size-based accept/fallback gate over to
                 * "mir accepted" (particularly under -fstack-check, whose
                 * extra call-site bytes shift the size comparison), and
                 * VLA frames lean on ix-relative slot traffic that is
                 * byte-cheap but T-state-expensive - the resulting switch
                 * from the legacy path to the MIR path can be a net cycle
                 * regression even though every individual instruction
                 * changed here is unambiguously cheaper in isolation. */
                if ((insn->immediate == '/' || insn->immediate == '%') &&
                    !mir.has_vla &&
                    mir_binary_only_constant(insn->src2)) {
                    const struct MirInsn *constant =
                        mir_definition(insn->src2);
                    fprintf(out, "\tld de,%ld\n",
                            constant->immediate & 0xffffL);
                } else {
                    fputs("\tpush hl\n", out);
                    if (mir_binary_only_constant(insn->src2)) {
                        const struct MirInsn *constant =
                            mir_definition(insn->src2);
                        fprintf(out, "\tld hl,%ld\n",
                                constant->immediate & 0xffffL);
                    } else
                        mir_emit_virtual_load(out, insn->src2);
                    fputs("\tex de,hl\n\tpop hl\n", out);
                }
                {
                    int fuse_skip = mir_binary_is_fusable_comparison(i);
                    if (fuse_skip > 0) {
                        if (!mir_emit_fused_comparison_branch(
                                out, labels, i, fuse_skip - 1))
                            goto done;
                        ++mir_fuse_report_fused_count;
                        i += fuse_skip;
                        continue;
                    }
                }
                switch ((int)insn->immediate) {
                case TOK_EQ: case TOK_NE: case '<': case '>':
                case TOK_LE: case TOK_GE:
                    ++mir_fuse_report_materialized_count;
                    break;
                default:
                    break;
                }
                if (!mir_emit_scalar_operation(out, insn))
                    goto done;
                mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_ARG:
            break;
        case MIR_CALL:
            {
                struct Sym *callee = find_global(insn->name);
                int is_indirect = strcmp(insn->name, "<indirect>") == 0;
                const char *assembly_name = insn->base_name[0] != 0
                    ? insn->base_name
                    : asm_name_for(callee != NULL ? sym_asm_name(callee)
                                                  : insn->name);
                int call_arg_count = 0;
                int argument_bytes = 0;
                int argument;
                int scan;
                for (scan = 0; scan < i; ++scan)
                    if (mir.insns[scan].opcode == MIR_ARG &&
                        mir.insns[scan].secondary_offset ==
                            insn->secondary_offset) {
                        int index = (int)mir.insns[scan].immediate;
                        if (index != call_arg_count)
                            goto done;
                        ++call_arg_count;
                    }
                argument = call_arg_count - 1;
                for (scan = i - 1; scan >= 0; --scan) {
                    const struct MirInsn *arg = &mir.insns[scan];
                    int size;
                    if (arg->opcode != MIR_ARG ||
                        arg->secondary_offset != insn->secondary_offset)
                        continue;
                    if (arg->immediate != argument--)
                        goto done;
                    size = type_size(arg->type);
                    if (type_is_struct_object(arg->type)) {
                        int byte;
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, 2))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tex de,hl\n", out);
                        fprintf(out,
                                "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
                                size);
                        for (byte = 0; byte < size; ++byte) {
                            fputs("\tld a,(de)\n\tld (hl),a\n", out);
                            if (byte + 1 < size)
                                fputs("\tinc de\n\tinc hl\n", out);
                        }
                        argument_bytes += size;
                    } else if (size == 4) {
                        int cached = mir_emit_cached_wide_call_argument(
                            out, arg->src1);
                        if (!cached && !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load_wide(out, arg->src1);
                        fputs("\tpush de\n\tpush hl\n", out);
                        if (cached)
                            fputs("\texx\n", out);
                        argument_bytes += 4;
                    } else {
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tpush hl\n", out);
                        argument_bytes += 2;
                    }
                }
                if (argument != -1)
                    goto done;
                if ((insn->memory_flags & 32) != 0)
                    fputs("\textrn __pfehx\n\tcall __pfehx\n", out);
                if ((insn->memory_flags & 64) != 0)
                    fputs("\textrn __pfeoc\n\tcall __pfeoc\n", out);
                if (is_indirect) {
                    int return_label = new_label();
                    mir_emit_virtual_load(out, insn->src1);
                    fprintf(out, "\tld de,L%d\n\tpush de\n\tjp (hl)\nL%d:\n",
                        return_label, return_label);
                } else {
                    if (callee == NULL || callee->needs_extrn)
                        fprintf(out, "\textrn %s\n", assembly_name);
                    fprintf(out, "\tcall %s\n", assembly_name);
                }
                if (is_indirect || callee == NULL || !callee->is_defined)
                    mir_emit_restore_virtual_iy(out);
                if ((argument_bytes & 1) != 0) {
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_store_wide(out, insn->dst);
                        else
                            mir_emit_virtual_store(out, insn->dst);
                    }
                    fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
                            argument_bytes);
                    if (type_ptr_depth(insn->type) > 0 ||
                        (insn->type & 15) != TYPE_VOID) {
                        if (type_size(insn->type) == 4)
                            mir_emit_virtual_load_wide(out, insn->dst);
                        else
                            mir_emit_virtual_load(out, insn->dst);
                    }
                } else {
                    for (argument = 0; argument < argument_bytes / 2;
                         ++argument)
                        fputs("\tpop bc\n", out);
                }
                if (type_ptr_depth(insn->type) > 0 ||
                    (insn->type & 15) != TYPE_VOID) {
                    if (type_size(insn->type) == 4)
                        mir_emit_virtual_store_wide(out, insn->dst);
                    else
                        mir_emit_virtual_store(out, insn->dst);
                }
            }
            break;
        case MIR_CALL_AGGREGATE:
            {
                struct Sym *callee = find_global(insn->name);
                const char *assembly_name = asm_name_for(
                    callee != NULL ? sym_asm_name(callee) : insn->name);
                int call_arg_count = 0;
                int argument_bytes = 0;
                int argument;
                int scan;
                for (scan = 0; scan < i; ++scan)
                    if (mir.insns[scan].opcode == MIR_ARG &&
                        mir.insns[scan].secondary_offset ==
                            insn->secondary_offset) {
                        int index = (int)mir.insns[scan].immediate;
                        if (index != call_arg_count)
                            goto done;
                        ++call_arg_count;
                    }
                argument = call_arg_count - 1;
                for (scan = i - 1; scan >= 0; --scan) {
                    const struct MirInsn *arg = &mir.insns[scan];
                    int size;
                    if (arg->opcode != MIR_ARG ||
                        arg->secondary_offset != insn->secondary_offset)
                        continue;
                    if (arg->immediate != argument--)
                        goto done;
                    size = type_size(arg->type);
                    if (type_is_struct_object(arg->type)) {
                        int byte;
                        if (size <= 0 || size > 1024)
                            goto done;
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, 2))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tex de,hl\n", out);
                        fprintf(out, "\tld hl,-%d\n\tadd hl,sp\n\tld sp,hl\n",
                                size);
                        for (byte = 0; byte < size; ++byte) {
                            fputs("\tld a,(de)\n\tld (hl),a\n", out);
                            if (byte + 1 < size)
                                fputs("\tinc de\n\tinc hl\n", out);
                        }
                        argument_bytes += size;
                    } else if (size == 4) {
                        int cached = mir_emit_cached_wide_call_argument(
                            out, arg->src1);
                        if (!cached && !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load_wide(out, arg->src1);
                        fputs("\tpush de\n\tpush hl\n", out);
                        if (cached)
                            fputs("\texx\n", out);
                        argument_bytes += 4;
                    } else {
                        if (!mir_emit_cached_call_argument(out, arg->src1) &&
                            !mir_emit_rematerialized_argument(
                                out, arg->src1, size))
                            mir_emit_virtual_load(out, arg->src1);
                        fputs("\tpush hl\n", out);
                        argument_bytes += 2;
                    }
                }
                if (argument != -1)
                    goto done;
                if (insn->immediate == MIR_AGGREGATE_GLOBAL_DEST_OFFSET) {
                    struct Sym *destination = find_global(insn->base_name);
                    const char *destination_name = asm_name_for(
                        destination != NULL ? sym_asm_name(destination)
                                            : mir_declared_link_name(
                                                insn->base_name));
                    if (destination != NULL && destination->needs_extrn)
                        fprintf(out, "\textrn %s\n", destination_name);
                    fprintf(out, "\tld hl,%s\n", destination_name);
                } else if (insn->immediate == MIR_AGGREGATE_VALUE_DEST_OFFSET)
                    mir_emit_virtual_load(out, insn->src1);
                else if (insn->immediate == MIR_AGGREGATE_FORWARD_OFFSET)
                    fputs("\tld l,(ix+4)\n\tld h,(ix+5)\n", out);
                else
                    fputs("\tpush ix\n\tpop hl\n", out);
                if (insn->immediate != 0 &&
                    insn->immediate != MIR_AGGREGATE_FORWARD_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_VALUE_DEST_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_GLOBAL_DEST_OFFSET)
                    fprintf(out, "\tld de,%ld\n\tadd hl,de\n",
                            insn->immediate);
                fputs("\tpush hl\n", out);
                if (callee == NULL || callee->needs_extrn)
                    fprintf(out, "\textrn %s\n", assembly_name);
                fprintf(out, "\tcall %s\n", assembly_name);
                fprintf(out, "\tld hl,%d\n\tadd hl,sp\n\tld sp,hl\n",
                        argument_bytes + 2);
                if (insn->immediate == MIR_AGGREGATE_GLOBAL_DEST_OFFSET) {
                    struct Sym *destination = find_global(insn->base_name);
                    const char *destination_name = asm_name_for(
                        destination != NULL ? sym_asm_name(destination)
                                            : mir_declared_link_name(
                                                insn->base_name));
                    fprintf(out, "\tld hl,%s\n", destination_name);
                } else if (insn->immediate == MIR_AGGREGATE_VALUE_DEST_OFFSET)
                    mir_emit_virtual_load(out, insn->src1);
                else if (insn->immediate == MIR_AGGREGATE_FORWARD_OFFSET)
                    fputs("\tld l,(ix+4)\n\tld h,(ix+5)\n", out);
                else
                    fputs("\tpush ix\n\tpop hl\n", out);
                if (insn->immediate != 0 &&
                    insn->immediate != MIR_AGGREGATE_FORWARD_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_VALUE_DEST_OFFSET &&
                    insn->immediate != MIR_AGGREGATE_GLOBAL_DEST_OFFSET)
                    fprintf(out, "\tld de,%ld\n\tadd hl,de\n",
                            insn->immediate);
                mir_emit_virtual_store(out, insn->dst);
            }
            break;
        case MIR_VA_START:
            if (insn->immediate < -128 || insn->immediate + 1 > 127)
                goto done;
            fputs("\tpush ix\n\tpop hl\n", out);
            if (insn->secondary_offset != 0)
                fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                        insn->secondary_offset);
            fprintf(out, "\tld (ix%+ld),l\n\tld (ix%+ld),h\n",
                    insn->immediate, insn->immediate + 1);
            fputs("\tld hl,0\n", out);
            mir_emit_virtual_store(out, insn->dst);
            break;
        case MIR_VA_END:
            if (insn->immediate < -128 || insn->immediate + 1 > 127)
                goto done;
            fprintf(out, "\tld (ix%+ld),0\n\tld (ix%+ld),0\n",
                    insn->immediate, insn->immediate + 1);
            fputs("\tld hl,0\n", out);
            mir_emit_virtual_store(out, insn->dst);
            break;
            case MIR_VA_ARG:
                fprintf(out, "\tld l,(ix%+ld)\n\tld h,(ix%+ld)\n",
                    insn->immediate, insn->immediate + 1);
                fputs("\tpush hl\n", out);
                    fprintf(out, "\tld de,%d\n\tadd hl,de\n",
                    insn->secondary_offset);
                fprintf(out, "\tld (ix%+ld),l\n\tld (ix%+ld),h\n",
                    insn->immediate, insn->immediate + 1);
                fputs("\tpop hl\n", out);
                if (insn->secondary_offset == 4) {
                fputs("\tpush hl\n\tld a,(hl)\n\tinc hl\n"
                      "\tld h,(hl)\n\tld l,a\n\tex (sp),hl\n"
                      "\tinc hl\n\tinc hl\n\tld e,(hl)\n\tinc hl\n"
                      "\tld d,(hl)\n\tpop hl\n", out);
                mir_emit_virtual_store_wide(out, insn->dst);
                } else {
                fputs("\tld a,(hl)\n\tinc hl\n\tld h,(hl)\n\tld l,a\n", out);
                mir_emit_virtual_store(out, insn->dst);
                }
                break;
        case MIR_JUMP:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            {
                int target = mir_find_label(insn->label);
                if (target < 0 || !mir_emit_spilled_phi_copies(out, i, target))
                    goto done;
            }
            fprintf(out, "\tjp L%d\n", labels[insn->label]);
            break;
        case MIR_BRANCH_FALSE:
            if (insn->label < 0 || insn->label >= mir.next_label)
                goto done;
            {
                int fallthrough_label = new_label();
                int target = mir_find_label(insn->label);
                const struct MirInsn *condition =
                    mir_definition(insn->src1);
                if (target < 0)
                    goto done;
                if (mir_value_is_wide(insn->src1)) {
                    mir_emit_virtual_load_wide(out, insn->src1);
                    if (condition != NULL && type_is_float(condition->type))
                        fputs("\tld a,d\n\tand 127\n\tor e\n\tor h\n\tor l\n",
                              out);
                    else
                        fputs("\tld a,d\n\tor e\n\tor h\n\tor l\n", out);
                } else {
                    mir_emit_virtual_load(out, insn->src1);
                    fputs("\tld a,h\n\tor l\n", out);
                }
                fprintf(out, "\tjp nz,L%d\n", fallthrough_label);
                if (!mir_emit_spilled_phi_copies(out, i, target))
                    goto done;
                fprintf(out, "\tjp L%d\nL%d:\n", labels[insn->label],
                        fallthrough_label);
            }
            break;
        case MIR_RETURN:
            if (type_is_struct_object(mir.return_type)) {
                int byte;
                int size = type_size(mir.return_type);
                if (insn->src1 < 0)
                    goto done;
                mir_emit_virtual_load(out, insn->src1);
                fputs("\tex de,hl\n\tld l,(ix+4)\n\tld h,(ix+5)\n", out);
                for (byte = 0; byte < size; ++byte) {
                    fputs("\tld a,(de)\n\tld (hl),a\n", out);
                    if (byte + 1 < size)
                        fputs("\tinc de\n\tinc hl\n", out);
                }
            } else if (insn->src1 >= 0) {
                if (mir_value_is_wide(insn->src1))
                    mir_emit_virtual_load_wide(out, insn->src1);
                else
                    mir_emit_virtual_load(out, insn->src1);
            }
            mir_emit_virtual_iy_epilogue(out);
            break;
        default:
            goto done;
        }
        if (insn->opcode != MIR_JUMP && insn->opcode != MIR_BRANCH_FALSE &&
            insn->opcode != MIR_RETURN && i + 1 < mir.count &&
            mir.insns[i + 1].opcode == MIR_LABEL) {
            int first = mir_first_nonlabel_successor(i + 1);
            int predecessor_label = mir_block_label_before(i);
            if (first >= 0 && first < mir.count &&
                mir.insns[first].opcode == MIR_PHI &&
                (mir.insns[first].phi_pred1 == predecessor_label ||
                 mir.insns[first].phi_pred2 == predecessor_label) &&
                !mir_emit_spilled_phi_copies(out, i, i + 1))
                goto done;
        }
    }
    mir_emit_virtual_iy_epilogue(out);
    accepted = 1;
done:
    if (getenv("DCC_MIR_FUSE_REPORT") != NULL &&
        (mir_fuse_report_fused_count > 0 ||
         mir_fuse_report_materialized_count > 0))
        fprintf(stderr,
                "; MIR fuse-report function=%s fused=%d materialized=%d\n",
                mir.name, mir_fuse_report_fused_count,
                mir_fuse_report_materialized_count);
    mir_virtual_iy_base = 0;
    mir_virtual_iy_frame_bytes = 0;
    mir_emit_instruction_index = -1;
    mir_forwarded_hl_value = -1;
    mir_forwarded_hl_instruction = -1;
    mir_forwarded_stack_value = -1;
    mir_forwarded_stack_instruction = -1;
    mir_cached_call_value = -1;
    mir_cached_call_instruction = -1;
    if (!accepted && getenv("DCC_MIR_SELECT_REPORT") != NULL)
        fprintf(stderr, "; MIR scalar-cfg reject function=%s insn=%d opcode=%s\n",
                mir.name, i,
                i >= 0 && i < mir.count
                    ? mir_opcode_name(mir.insns[i].opcode) : "preflight");
    free(labels);
    return accepted;
}

static int mir_try_emit_general_rollout(FILE *out)
{
    int i;
    int frame_bytes;
    int return_count = 0;
    int parameter_count = 0;

    if (mir.has_vla || mir.has_runtime_stride_param ||
        mir.is_variadic_function || mir.count > 64 ||
        mir.declaration_count > 0 ||
        (mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2)
        return 0;
    frame_bytes = mir.local_bytes + 2 * mir_prepare_backend_slots();
    if (frame_bytes > 64)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->dst >= 0 && (type_size(insn->type) > 2 ||
                               type_ptr_depth(insn->type) > 0))
            return 0;
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_CONST:
        case MIR_UNARY: case MIR_BINARY:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_RETURN:
            ++return_count;
            break;
        default:
            return 0;
        }
    }
    if (return_count != 1 || parameter_count == 0)
        return 0;
    return mir_try_emit_homed_scalar_dag(out);
}

static int mir_try_emit_home_cfg_rollout(FILE *out)
{
    int i;
    int has_loop_phi = 0;
    int parameter_count = 0;
    int return_count = 0;

    if (mir.has_vla || mir.has_runtime_stride_param ||
        mir.is_variadic_function || mir.count > 64 ||
        mir.declaration_count > 0 ||
        (mir.return_type & 15) != TYPE_INT || type_size(mir.return_type) > 2 ||
        mir.allocation_spill_count != 0 || mir_home_uses_iy())
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->dst >= 0 && (type_size(insn->type) > 2 ||
                               type_ptr_depth(insn->type) > 0))
            return 0;
        switch (insn->opcode) {
        case MIR_NOP: case MIR_LABEL: case MIR_CONST: case MIR_STORE:
            break;
        case MIR_PHI:
            if ((insn->phi_pred1 >= 0 &&
                 mir_find_label(insn->phi_pred1) > i) ||
                (insn->phi_pred2 >= 0 &&
                 mir_find_label(insn->phi_pred2) > i))
                has_loop_phi = 1;
            break;
        case MIR_JUMP: case MIR_BRANCH_FALSE:
            break;
        case MIR_PARAM:
            ++parameter_count;
            break;
        case MIR_UNARY:
            if (insn->immediate != 0 && insn->immediate != '+' &&
                insn->immediate != '-' && insn->immediate != '~' &&
                insn->immediate != '!')
                return 0;
            break;
        case MIR_BINARY:
            if (insn->immediate != '+' && insn->immediate != '-' &&
                insn->immediate != '&' && insn->immediate != '|' &&
                insn->immediate != '^' && insn->immediate != TOK_EQ &&
                insn->immediate != TOK_NE && insn->immediate != '<' &&
                insn->immediate != '>' && insn->immediate != TOK_LE &&
                insn->immediate != TOK_GE)
                return 0;
            break;
        case MIR_RETURN:
            ++return_count;
            break;
        default:
            return 0;
        }
    }
    if (!has_loop_phi || parameter_count == 0 || return_count == 0)
        return 0;
    return mir_try_emit_homed_scalar_cfg(out);
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

/* Unsigned quotient/remainder loop:
 *
 *     q = 0;
 *     while (K <= r) { r -= K; ++q; }
 *     return q;
 *
 * BC holds r and DE holds q. Adding -K to BC in HL provides both the
 * unsigned loop test (carry means r >= K) and the updated remainder. */
static int mir_try_emit_unsigned_division_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *remainder_phi = NULL;
    const struct MirInsn *quotient_phi = NULL;
    const struct MirInsn *remainder_update = NULL;
    const struct MirInsn *quotient_update = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *return_insn = NULL;
    const struct MirInsn *divisor_definition;
    int remainder_object = -1;
    int quotient_object = -1;
    long divisor;
    int top_label;
    int end_label;
    int i;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            remainder_object = insn->object;
        }
    }
    if (parameter == NULL || remainder_object < 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PHI) {
            if (insn->object == remainder_object)
                remainder_phi = insn;
            else if (quotient_phi == NULL) {
                quotient_phi = insn;
                quotient_object = insn->object;
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_STORE) {
            const struct MirInsn *definition = mir_definition(insn->src1);
            if (insn->object == remainder_object)
                remainder_update = definition;
            else if (insn->object == quotient_object || quotient_object < 0)
                quotient_update = definition;
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == TOK_LE)
                compare = candidate;
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (remainder_phi == NULL || quotient_phi == NULL ||
        remainder_update == NULL || quotient_update == NULL ||
        compare == NULL || return_insn == NULL || quotient_object < 0)
        return 0;
    if (remainder_update->opcode != MIR_BINARY ||
        remainder_update->immediate != '-' ||
        remainder_update->src1 != remainder_phi->dst ||
        quotient_update->opcode != MIR_BINARY ||
        quotient_update->immediate != '+' ||
        quotient_update->src1 != quotient_phi->dst ||
        !mir_is_const_value(quotient_update->src2, 1))
        return 0;
    divisor_definition = mir_definition(remainder_update->src2);
    if (divisor_definition == NULL ||
        divisor_definition->opcode != MIR_CONST)
        return 0;
    if (compare->src2 != remainder_phi->dst ||
        !mir_is_const_value(compare->src1, divisor_definition->immediate) ||
        return_insn->src1 != quotient_phi->dst)
        return 0;
    divisor = divisor_definition->immediate;
    if (divisor <= 0 || divisor > 32768)
        return 0;
    if (!((remainder_phi->src1 == parameter->dst &&
           remainder_phi->src2 == remainder_update->dst) ||
          (remainder_phi->src2 == parameter->dst &&
           remainder_phi->src1 == remainder_update->dst)))
        return 0;
    if (!((mir_is_const_value(quotient_phi->src1, 0) &&
           quotient_phi->src2 == quotient_update->dst) ||
          (mir_is_const_value(quotient_phi->src2, 0) &&
           quotient_phi->src1 == quotient_update->dst)))
        return 0;
    if (mir.objects[remainder_object].storage != SC_PARAM ||
        type_size(mir.objects[remainder_object].type) != 2 ||
        (mir.objects[remainder_object].type & TYPE_UNSIGNED) == 0 ||
        type_size(mir.objects[quotient_object].type) != 2 ||
        (mir.objects[quotient_object].type & TYPE_UNSIGNED) == 0)
        return 0;

    top_label = new_label();
    end_label = new_label();
    mir_emit_prologue(out);
    fprintf(out, "\tld c,(ix%+d)\n", mir.objects[remainder_object].offset);
    fprintf(out, "\tld b,(ix%+d)\n",
            mir.objects[remainder_object].offset + 1);
    fputs("\tld de,0\n", out);
    fprintf(out, "L%d:\n", top_label);
    fprintf(out, "\tld hl,%ld\n\tadd hl,bc\n", -divisor);
    fprintf(out, "\tjp nc,L%d\n", end_label);
    fputs("\tld b,h\n\tld c,l\n\tinc de\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tret\n", out);
    return 1;
}

/* Three-register invariant-add loop:
 *
 *     total = 0;
 *     for (i = 0; i < K; ++i) {
 *         total += factor;
 *         total += factor;
 *     }
 *
 * IY holds the loop-invariant 2*factor, BC holds i and DE holds total. */
static int mir_try_emit_repeated_invariant_add_loop(FILE *out)
{
    const struct MirInsn *parameter = NULL;
    const struct MirInsn *total_phi = NULL;
    const struct MirInsn *index_phi = NULL;
    const struct MirInsn *first_add = NULL;
    const struct MirInsn *second_add = NULL;
    const struct MirInsn *index_update = NULL;
    const struct MirInsn *compare = NULL;
    const struct MirInsn *return_insn = NULL;
    int factor_values[2];
    int factor_load_count = 0;
    int factor_object = -1;
    int total_object = -1;
    int index_object = -1;
    long limit;
    int top_label;
    int end_label;
    int i;

    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PARAM) {
            if (parameter != NULL)
                return 0;
            parameter = insn;
            factor_object = insn->object;
        } else if (insn->opcode == MIR_LOAD &&
                   insn->object == factor_object) {
            if (factor_load_count >= 2)
                return 0;
            factor_values[factor_load_count++] = insn->dst;
        }
    }
    if (parameter == NULL || factor_object < 0)
        return 0;
    if (factor_load_count == 0) {
        factor_values[0] = parameter->dst;
        factor_values[1] = parameter->dst;
    } else if (factor_load_count != 2) {
        return 0;
    }
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_PHI) {
            if (total_phi == NULL) {
                total_phi = insn;
                total_object = insn->object;
            } else if (index_phi == NULL) {
                index_phi = insn;
                index_object = insn->object;
            } else {
                return 0;
            }
        } else if (insn->opcode == MIR_STORE) {
            const struct MirInsn *definition = mir_definition(insn->src1);
            if (total_phi != NULL && insn->object == total_object) {
                if (definition != NULL && definition->opcode == MIR_BINARY &&
                    (definition->src1 == factor_values[1] ||
                     definition->src2 == factor_values[1]) &&
                    definition->src1 != total_phi->dst &&
                    definition->src2 != total_phi->dst)
                    second_add = definition;
                else if (definition != NULL &&
                         definition->opcode == MIR_BINARY)
                    first_add = definition;
            } else if (index_phi != NULL && insn->object == index_object) {
                index_update = definition;
            }
        } else if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *candidate = mir_definition(insn->src1);
            if (candidate != NULL && candidate->opcode == MIR_BINARY &&
                candidate->immediate == '<')
                compare = candidate;
        } else if (insn->opcode == MIR_RETURN) {
            return_insn = insn;
        } else if (insn->opcode == MIR_CALL || insn->opcode == MIR_OPAQUE ||
                   insn->opcode == MIR_INDEX_LOAD || insn->opcode == MIR_ARG) {
            return 0;
        }
    }
    if (total_phi == NULL || index_phi == NULL || first_add == NULL ||
        second_add == NULL || index_update == NULL || compare == NULL ||
        return_insn == NULL || total_object < 0 || index_object < 0)
        return 0;
    if (first_add->immediate != '+' ||
        !((first_add->src1 == total_phi->dst &&
              first_add->src2 == factor_values[0]) ||
          (first_add->src2 == total_phi->dst &&
              first_add->src1 == factor_values[0])))
        return 0;
    if (second_add->immediate != '+' ||
        !((second_add->src1 == first_add->dst &&
              second_add->src2 == factor_values[1]) ||
          (second_add->src2 == first_add->dst &&
              second_add->src1 == factor_values[1])))
        return 0;
    if (index_update->opcode != MIR_BINARY || index_update->immediate != '+' ||
        index_update->src1 != index_phi->dst ||
        !mir_is_const_value(index_update->src2, 1) ||
        compare->src1 != index_phi->dst || return_insn->src1 != total_phi->dst)
        return 0;
    {
        const struct MirInsn *limit_definition = mir_definition(compare->src2);
        if (limit_definition == NULL || limit_definition->opcode != MIR_CONST)
            return 0;
        limit = limit_definition->immediate;
    }
    if (limit <= 0 || limit > 32768)
        return 0;
    if (!((mir_is_const_value(total_phi->src1, 0) &&
           total_phi->src2 == second_add->dst) ||
          (mir_is_const_value(total_phi->src2, 0) &&
           total_phi->src1 == second_add->dst)) ||
        !((mir_is_const_value(index_phi->src1, 0) &&
           index_phi->src2 == index_update->dst) ||
          (mir_is_const_value(index_phi->src2, 0) &&
           index_phi->src1 == index_update->dst)))
        return 0;
    if (mir.objects[factor_object].storage != SC_PARAM ||
        type_size(mir.objects[factor_object].type) != 2 ||
        type_size(mir.objects[total_object].type) != 2 ||
        (type_size(mir.objects[index_object].type) != 2 &&
         type_size(mir.objects[index_object].type) != 1) ||
        (type_size(mir.objects[index_object].type) == 1 && limit > 255))
        return 0;

    top_label = new_label();
    end_label = new_label();
    mir_emit_iy_prologue(out);
    fprintf(out, "\tld l,(ix%+d)\n", mir.objects[factor_object].offset + 2);
    fprintf(out, "\tld h,(ix%+d)\n", mir.objects[factor_object].offset + 3);
    fputs("\tadd hl,hl\n\tpush hl\n\tpop iy\n", out);
    fputs("\tld bc,0\n\tld de,0\n", out);
    fprintf(out, "L%d:\n", top_label);
    fprintf(out, "\tld hl,%ld\n\tadd hl,bc\n", -limit);
    fprintf(out, "\tjp c,L%d\n", end_label);
    fputs("\tpush iy\n\tpop hl\n\tadd hl,de\n\tex de,hl\n\tinc bc\n", out);
    fprintf(out, "\tjp L%d\n", top_label);
    fprintf(out, "L%d:\n", end_label);
    fputs("\tex de,hl\n\tld sp,ix\n\tpop ix\n\tpop iy\n\tret\n", out);
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
static int mir_try_selector(FILE *out, int (*selector)(FILE *))
{
    FILE *candidate = tmpfile();
    int accepted;
    int character;

    if (candidate == NULL)
        fatal("cannot create MIR selector stream");
    accepted = selector(candidate);
    if (accepted) {
        rewind(candidate);
        while ((character = fgetc(candidate)) != EOF)
            fputc(character, out);
    }
    fclose(candidate);
    return accepted;
}

static long mir_stream_size(FILE *stream)
{
    long position = ftell(stream);
    long size;

    if (position < 0 || fseek(stream, 0, SEEK_END) != 0)
        return -1;
    size = ftell(stream);
    if (fseek(stream, position, SEEK_SET) != 0)
        return -1;
    return size;
}

static int mir_stream_instruction_count(FILE *stream)
{
    char line[512];
    long position = ftell(stream);
    int count = 0;

    if (position < 0 || fseek(stream, 0, SEEK_SET) != 0)
        return -1;
    while (fgets(line, sizeof(line), stream) != NULL) {
        char *text = line;
        char *end;
        while (*text == ' ' || *text == '\t')
            ++text;
        end = text + strlen(text);
        while (end > text && (end[-1] == '\n' || end[-1] == '\r' ||
                              end[-1] == ' ' || end[-1] == '\t'))
            *--end = 0;
        if (*text == 0 || *text == ';' || end[-1] == ':' ||
            !strncmp(text, "extrn ", 6) || !strncmp(text, "public ", 7) ||
            !strncmp(text, "cseg", 4) || !strncmp(text, "dseg", 4) ||
            !strncmp(text, "db ", 3) || !strncmp(text, "dw ", 3) ||
            !strncmp(text, "ds ", 3) || !strncmp(text, "equ ", 4))
            continue;
        ++count;
    }
    if (fseek(stream, position, SEEK_SET) != 0)
        return -1;
    return count;
}

static int mir_cfg_block_count(void)
{
    int blocks = 0;
    int i;

    for (i = 0; i < mir.count; ++i) {
        if (mir.insns[i].opcode == MIR_LABEL)
            ++blocks;
    }
    return blocks;
}

static int mir_has_inline_substitution_call(void)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_CALL &&
            (mir.insns[i].memory_flags & 2048) != 0)
            return 1;
    return 0;
}

static int mir_has_declared_pointer_array(void)
{
    int i;

    for (i = 0; i < mir.declared_count; ++i)
        if (type_ptr_depth(mir.declared_types[i]) > 0 &&
            mir.declared_dim_counts[i] > 0)
            return 1;
    return 0;
}

static int mir_has_cfg_backedge(void)
{
    int i;
    int j;

    for (i = 0; i < mir.count; ++i) {
        int target;
        if (mir.insns[i].opcode != MIR_JUMP &&
            mir.insns[i].opcode != MIR_BRANCH_FALSE)
            continue;
        target = mir.insns[i].label;
        for (j = 0; j <= i; ++j)
            if (mir.insns[j].opcode == MIR_LABEL &&
                mir.insns[j].label == target)
                return 1;
    }
    return 0;
}

static int mir_is_profiled_near_cost_single_block(long generated_size,
                                                   long captured_size,
                                                   int generated_instructions,
                                                   int captured_instructions)
{
    return !mir.has_vla && mir_cfg_block_count() == 1 &&
           generated_size <= captured_size + 24 &&
           generated_instructions <= captured_instructions + 2;
}

static int mir_is_byte_profitable_single_block(long generated_size,
                                                long captured_size,
                                                int generated_instructions,
                                                int captured_instructions)
{
    return !mir.has_vla && mir_cfg_block_count() == 1 &&
           generated_size <= captured_size - 20 &&
           generated_instructions <= captured_instructions + 3;
}

static int mir_is_profiled_constant_bound_loop_pair(
    long generated_size, long captured_size, int generated_instructions,
    int captured_instructions)
{
    int i;
    int branch_count = 0;

    if (mir_cfg_block_count() != 7 || mir.allocation_spill_count != 0 ||
        generated_size > captured_size ||
        generated_instructions > captured_instructions + 11)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_CALL || insn->opcode == MIR_CALL_AGGREGATE ||
            insn->opcode == MIR_OPAQUE)
            return 0;
        if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *comparison = mir_definition(insn->src1);
            const struct MirInsn *bound;
            if (comparison == NULL || comparison->opcode != MIR_BINARY ||
                comparison->type != TYPE_INT || comparison->immediate != '<')
                return 0;
            bound = mir_definition(comparison->src2);
            if (bound == NULL || bound->opcode != MIR_CONST ||
                bound->type != TYPE_INT)
                return 0;
            ++branch_count;
        }
    }
    return branch_count == 2 && mir_has_cfg_backedge();
}

static int mir_has_profiled_positive_loop(void)
{
    int i;
    int has_positive_condition = 0;

    if (mir_cfg_block_count() != 4 || mir.allocation_spill_count != 0)
        return 0;
    for (i = 0; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];
        if (insn->opcode == MIR_CALL || insn->opcode == MIR_CALL_AGGREGATE ||
            insn->opcode == MIR_OPAQUE)
            return 0;
        if (insn->opcode == MIR_BRANCH_FALSE) {
            const struct MirInsn *comparison = mir_definition(insn->src1);
            const struct MirInsn *zero;
            if (comparison == NULL || comparison->opcode != MIR_BINARY ||
                comparison->type != TYPE_INT || comparison->immediate != '>')
                continue;
            zero = mir_definition(comparison->src2);
            if (zero != NULL && zero->opcode == MIR_CONST &&
                zero->type == TYPE_INT && zero->immediate == 0)
                has_positive_condition = 1;
        }
    }
    return has_positive_condition && mir_has_cfg_backedge();
}

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

    if (mir_try_selector(out, mir_try_emit_homed_scalar_cfg))
        return 1;
    if (mir_try_selector(out, mir_try_emit_spilled_scalar_cfg))
        return 1;

    /* The current selectors implement only the ordinary 16-bit HL result
     * convention. Other return ABIs remain with the existing backend. */
    if ((mir.return_type & 15) != TYPE_INT)
        return 0;

    if (mir_try_selector(out, mir_try_emit_accumulator_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_unsigned_division_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_repeated_invariant_add_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_countdown_loop))
        return 1;
    if (mir_try_selector(out, mir_try_emit_comparison_branch))
        return 1;
    if (mir_try_selector(out, mir_try_emit_scalar_dag))
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

void mir_end_function(void)
{
    int verified;

    if (!mir.active)
        return;
    if (errors > 0) {
        emit_sink_restore(&mir.saved_sink);
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
        mir.active = 0;
        return;
    }
    if (mir.count > MIR_MAX_ROLLOUT_INSNS) {
        FILE *destination = mir.saved_sink.stream;
        int character;

        emit_sink_restore(&mir.saved_sink);
        rewind(mir.capture_stream);
        while ((character = fgetc(mir.capture_stream)) != EOF)
            fputc(character, destination);
        if (mir.report_mode)
            fprintf(stderr, "; MIR emit function=%s result=oversized-fallback\n",
                    mir.name);
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
        mir.active = 0;
        return;
    }
    mir_resolve_deferred_metadata();
    verified = mir_verify_and_dump();
    if (mir.opaque_count != 0 &&
        getenv("DCC_MIR_REQUIRE_COMPLETE") != NULL) {
        fprintf(stderr, "MIR completeness failed for function %s\n", mir.name);
        fatal("incomplete MIR coverage");
    }
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
    if (!mir.emit_mode && verified &&
        getenv("DCC_MIR_GENERAL_CANDIDATES") != NULL) {
        FILE *candidate = tmpfile();
        int accepted;
        if (candidate == NULL)
            fatal("cannot create MIR general candidate stream");
        accepted = mir_try_emit_general_rollout(candidate);
        fclose(candidate);
        if (accepted)
            fprintf(stderr, "; MIR general candidate function=%s sink=%s "
                            "slots=%d\n",
                    mir.name, mir_sink_name(mir.sink_purpose),
                    mir.backend_slot_count);
    }
    if (!mir.emit_mode && verified &&
        getenv("DCC_MIR_HOME_CFG_CANDIDATES") != NULL) {
        FILE *candidate = tmpfile();
        int accepted;
        if (candidate == NULL)
            fatal("cannot create MIR home CFG candidate stream");
        accepted = mir_try_selector(candidate, mir_try_emit_home_cfg_rollout);
        fclose(candidate);
        if (accepted)
            fprintf(stderr, "; MIR home-cfg candidate function=%s sink=%s\n",
                    mir.name, mir_sink_name(mir.sink_purpose));
    }
    if (mir.emit_mode) {
        FILE *destination = mir.saved_sink.stream;
        FILE *generated = NULL;
        int emitted = 0;
        const char *selector_name = "none";
        const char *fallback_reason = verified ? "selector" : "verify";
        long generated_size = -1;
        long captured_size = -1;
        int generated_instructions = -1;
        int captured_instructions = -1;

        emit_sink_restore(&mir.saved_sink);
        if (verified) {
            const char *emit_filter = getenv("DCC_MIR_EMIT_FUNCTION");
            const char *general_filter = getenv("DCC_MIR_GENERAL_FUNCTION");
            generated = tmpfile();
            if (generated == NULL)
                fatal("cannot create MIR generated stream");
            if (emit_filter != NULL && emit_filter[0] != 0 &&
                strcmp(emit_filter, mir.name) == 0) {
                selector_name = "specialized";
                emitted = mir_try_emit_z80(generated);
            } else if (general_filter != NULL && general_filter[0] != 0 &&
                       (strcmp(general_filter, "*") == 0 ||
                        strcmp(general_filter, mir.name) == 0)) {
                selector_name = "homed-scalar-cfg";
                emitted = mir_try_selector(generated,
                                           mir_try_emit_homed_scalar_cfg);
                if (!emitted) {
                    selector_name = "spilled-scalar-cfg";
                    emitted = mir_try_selector(generated,
                                               mir_try_emit_spilled_scalar_cfg);
                }
            } else if (getenv("DCC_MIR_EMIT_GENERAL") != NULL) {
                selector_name = "general-rollout";
                emitted = mir_try_selector(generated,
                                           mir_try_emit_general_rollout);
            } else if (getenv("DCC_MIR_EMIT_HOME_CFG") != NULL) {
                selector_name = "home-cfg-rollout";
                emitted = mir_try_selector(generated,
                                           mir_try_emit_home_cfg_rollout);
            } else {
                selector_name = "homed-scalar-cfg";
                emitted = mir_try_selector(generated,
                                           mir_try_emit_homed_scalar_cfg);
            }
            if (!emitted && (general_filter == NULL ||
                             general_filter[0] == 0)) {
                selector_name = "spilled-scalar-cfg";
                emitted = mir_try_selector(generated,
                                           mir_try_emit_spilled_scalar_cfg);
            }
            if (emitted) {
                generated_size = mir_stream_size(generated);
                captured_size = mir_stream_size(mir.capture_stream);
                generated_instructions = mir_stream_instruction_count(generated);
                captured_instructions =
                    mir_stream_instruction_count(mir.capture_stream);
                fallback_reason = NULL;
                {
                    const char *forced_function =
                        getenv("DCC_MIR_FORCE_FALLBACK_FUNCTION");
                    if (getenv("DCC_MIR_FORCE_FALLBACK") != NULL ||
                        (forced_function != NULL &&
                         !strcmp(forced_function, mir.name)))
                        fallback_reason = "forced";
                }
                if (fallback_reason != NULL) {
                    /* Keep the selected reason. */
                }
                else if (generated_size < 0 || captured_size < 0 ||
                    generated_instructions < 0 || captured_instructions < 0)
                    fallback_reason = "measurement";
                else if (!strcmp(selector_name, "spilled-scalar-cfg") &&
                                                 generated_size > captured_size + 1 &&
                                                 !(mir.local_bytes == 0 &&
                                                     mir.aggregate_temp_bytes == 0 &&
                                                     mir.backend_slot_count == 0 && !mir.has_vla &&
                                                     mir_cfg_block_count() == 1 &&
                                                     generated_instructions <= captured_instructions) &&
                                                 !mir_is_profiled_near_cost_single_block(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions) &&
                                                 !mir_is_byte_profitable_single_block(
                                                     generated_size, captured_size,
                                                     generated_instructions,
                                                     captured_instructions))
                    fallback_reason = "text-size";
                else if (generated_instructions > captured_instructions +
                        (!strcmp(selector_name, "homed-scalar-cfg")
                            ? (mir_cfg_block_count() <= 2 ? 2 : 1)
                        : (!strcmp(selector_name, "spilled-scalar-cfg") &&
                           generated_size <= captured_size ? 1 : 0)) &&
                         !mir_is_profiled_near_cost_single_block(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_byte_profitable_single_block(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions) &&
                         !mir_is_profiled_constant_bound_loop_pair(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions))
                    fallback_reason = "instruction-count";
                else if (mir_cfg_block_count() > 64)
                    fallback_reason = "cfg-block-count";
                else if (mir_has_inline_substitution_call() &&
                         !(generated_instructions * 100 >=
                             captured_instructions * 95 &&
                           generated_instructions <=
                               captured_instructions + 1 &&
                           generated_size <= captured_size))
                    fallback_reason = "inline-substitution";
                else if (mir_has_declared_pointer_array())
                    fallback_reason = "pointer-array";
                else if (mir_has_cfg_backedge() &&
                         !mir_has_profiled_positive_loop() &&
                         !mir_is_profiled_constant_bound_loop_pair(
                             generated_size, captured_size,
                             generated_instructions, captured_instructions))
                    fallback_reason = "cfg-backedge";
                {
                    const char *forced_accept =
                        getenv("DCC_MIR_FORCE_ACCEPT_FUNCTION");
                    if (forced_accept != NULL &&
                        !strcmp(forced_accept, mir.name))
                        fallback_reason = NULL;
                }
                if (fallback_reason != NULL)
                    emitted = 0;
            }
        }
        if (emitted) {
            int character;
            rewind(generated);
            while ((character = fgetc(generated)) != EOF)
                fputc(character, destination);
        } else {
            int character;
            rewind(mir.capture_stream);
            while ((character = fgetc(mir.capture_stream)) != EOF)
                fputc(character, destination);
        }
        if (generated != NULL)
            fclose(generated);
        if (mir.report_mode)
            fprintf(stderr, "; MIR emit function=%s result=%s\n",
                mir.name, emitted ? "mir" : "fallback");
        if (getenv("DCC_MIR_SELECT_REPORT") != NULL)
            fprintf(stderr,
                    "; MIR selection function=%s selector=%s result=%s "
                    "reason=%s generated-bytes=%ld captured-bytes=%ld "
                    "generated-insns=%d captured-insns=%d blocks=%d\n",
                    mir.name, selector_name, emitted ? "mir" : "fallback",
                    fallback_reason != NULL ? fallback_reason : "accepted",
                    generated_size, captured_size, generated_instructions,
                    captured_instructions, mir_cfg_block_count());
        fclose(mir.capture_stream);
        mir.capture_stream = NULL;
        mir.emit_mode = 0;
    }
    mir.active = 0;
}
