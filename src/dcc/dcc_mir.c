/* dcc_mir.c - MIR core: lowering, capture API, CFG/dataflow analysis,
 * register allocation, and the top-level transactional accept/replay
 * plumbing shared by every selector.
 *
 * This is one of several dcc_mir_*.c translation units that together
 * implement the typed virtual-register machine IR and transactional
 * backend described in dcc_mir_internal.h. See that header for the
 * shared IR types and cross-file helper prototypes.
 *
 * Set DCC_MIR_REPORT=1 to dump every generated function attempt, or
 * DCC_MIR_FUNCTION=name to restrict the dump. MIR emission remains opt-in
 * and transactional: unsupported functions replay the established
 * backend body.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dcc.h"
#include "dcc_ast.h"
#include "dcc_mir.h"
#include "dcc_mir_internal.h"

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


struct MirFunction mir;
int mir_virtual_iy_base;
int mir_virtual_iy_frame_bytes;
int mir_emit_instruction_index = -1;
int mir_forwarded_hl_value = -1;
int mir_forwarded_hl_instruction = -1;
int mir_forwarded_wide_value = -1;
int mir_forwarded_wide_instruction = -1;
int mir_forwarded_stack_value = -1;
int mir_forwarded_stack_instruction = -1;
int mir_cached_call_value = -1;
int mir_cached_call_instruction = -1;
int mir_cached_wide_call_value = -1;
int mir_cached_wide_call_instruction = -1;

static int *mir_lazy_saved_colors;
static int *mir_lazy_saved_spills;
static int mir_lazy_saved_spill_count;
static int mir_lazy_saved_fixed_moves;
static int mir_lazy_saved_operand_moves;
static int mir_lazy_saved_phi_moves;
static int *mir_rematerialized_saved_colors;
static int *mir_rematerialized_saved_spills;
static int mir_rematerialized_saved_spill_count;
static int mir_rematerialized_saved_fixed_moves;
static int mir_rematerialized_saved_operand_moves;
static int mir_rematerialized_saved_phi_moves;
static int mir_rematerialized_home_allocation_active;
static int *mir_regional_saved_colors;
static int *mir_regional_saved_spills;
static int *mir_regional_base_colors;
static int *mir_regional_base_spills;
static int mir_regional_saved_spill_count;
static int mir_regional_saved_fixed_moves;
static int mir_regional_saved_operand_moves;
static int mir_regional_saved_phi_moves;
static int mir_regional_home_plan_active;
static unsigned char *mir_regional_register_valid;
static int mir_regional_register_valid_capacity;
static int mir_regional_emission_region = -1;
static int mir_regional_boundary_pending;
static int mir_lazy_allocation_active;
static int mir_extended_integer_constant_conversion_fold_count;
static unsigned long mir_inline_live_temp_mask;
static int mir_inline_temp_ids[MAX_PROTO_PARAMS];
static const char *mir_inline_local_src_name;
static const char *mir_inline_local_temp_name;
static int mir_inline_expand_depth;

struct MirInlineCallScope {
    unsigned long live_temp_mask;
    int temp_ids[MAX_PROTO_PARAMS];
};

static int mir_inline_substitutable(const struct Sym *symbol)
{
    return symbol != NULL && symbol->is_static && symbol->is_inline &&
           (symbol->inline_return_expr != NULL ||
            symbol->inline_stmt_expr != NULL ||
            symbol->inline_stmt_body != NULL);
}

const char *mir_opcode_name(int opcode)
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

const char *mir_sink_name(int purpose)
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
    if (strncmp(sym->name, "#itmp", 5) == 0)
        return 0;
    if (sym->is_volatile || sym->is_array || sym->is_vla ||
        sym->is_const_value)
        return 0;
    if (type_is_struct_object(sym->type))
        return 0;
    if (type_ptr_depth(sym->type) > 0 && sym->storage != SC_PARAM)
        return 0;
    /* Item T35 (mir-text-size-plan.md): this used to reject anything
     * over 2 bytes, dating to the original mem2reg/object-promotion
     * commit (0771448) which scoped itself to "1/2-byte locals and
     * parameters" as a first milestone. mir_param_value_is_direct and
     * mir_emit_virtual_load_wide (dcc_mir_spilled_cfg.c) both already
     * contain fully-written support for a 4-byte ("wide": float/long)
     * object - they explicitly test `type_size(...) == 4` alongside
     * `== 2` - but could never actually reach that code, because no
     * wide local or parameter was ever admitted to mir.objects[] in
     * the first place. Pointers (type_size 2, but excluded above via
     * type_ptr_depth) and structs (excluded above via
     * type_is_struct_object) are unaffected by widening this to 4;
     * only TYPE_LONG/TYPE_FLOAT scalars newly qualify. */
    if (type_size(sym->type) < 1 || type_size(sym->type) > 4)
        return 0;
    /* MIR can prove scalar local/parameter address escapes precisely from its
     * own lowered MIR_ADDRESS instructions later, which avoids the legacy
     * lexer scan's broad false positives on the RHS of bitwise '&'
     * expressions (`tbits.ti16_bits`: `a & b` had marked `b` address-taken
     * and blocked object promotion entirely). Keep the lexical gate only for
     * pointer parameters, whose safety proof still depends on the separate
     * pointer-reference classifier below. */
    if (type_ptr_depth(sym->type) > 0) {
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
    }
    return 1;
}

#define MIR_POINTER_USE_DEREFERENCE 0x01U
#define MIR_POINTER_USE_INDEX 0x02U
#define MIR_POINTER_USE_MEMBER 0x04U
#define MIR_POINTER_USE_COMPARE 0x08U
#define MIR_POINTER_USE_RETURN 0x10U

static int mir_pointer_value_uses_are_eligible(int value,
                                                unsigned char *visiting,
                                                int *use_count,
                                                unsigned int *use_kinds,
                                                const char **reason)
{
    int instruction;

    if (value < 0 || value >= mir.next_value || visiting[value]) {
        *reason = "alias-cycle";
        return 0;
    }
    visiting[value] = 1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int uses_src1 = insn->src1 == value;
        int uses_src2 = insn->src2 == value;

        if (mir_call_uses_value(insn, value)) {
            *reason = "call-argument";
            visiting[value] = 0;
            return 0;
        }
        if (!uses_src1 && !uses_src2)
            continue;
        ++*use_count;
        if (insn->opcode == MIR_ARG && uses_src1) {
            *reason = "call-argument";
            visiting[value] = 0;
            return 0;
        }
        if (insn->opcode == MIR_UNARY && uses_src1 &&
            insn->immediate == 0 && insn->dst >= 0 &&
            type_ptr_depth(insn->type) > 0) {
            if (!mir_pointer_value_uses_are_eligible(
                    insn->dst, visiting, use_count, use_kinds, reason)) {
                visiting[value] = 0;
                return 0;
            }
            continue;
        }
        if ((insn->opcode == MIR_LOAD_INDIRECT ||
             insn->opcode == MIR_INDEX_LOAD) && uses_src1) {
            *use_kinds |= MIR_POINTER_USE_DEREFERENCE;
            continue;
        }
        if (insn->opcode == MIR_INDEX_ADDRESS && uses_src1) {
            *use_kinds |= MIR_POINTER_USE_INDEX;
            continue;
        }
        if (insn->opcode == MIR_MEMBER_ADDRESS && uses_src1) {
            *use_kinds |= MIR_POINTER_USE_MEMBER;
            continue;
        }
        if (insn->opcode == MIR_STORE_INDIRECT && uses_src1) {
            *use_kinds |= MIR_POINTER_USE_DEREFERENCE;
            continue;
        }
        if (insn->opcode == MIR_BINARY &&
            (insn->immediate == TOK_EQ || insn->immediate == TOK_NE ||
             insn->immediate == '<' || insn->immediate == '>' ||
             insn->immediate == TOK_LE || insn->immediate == TOK_GE)) {
            *use_kinds |= MIR_POINTER_USE_COMPARE;
            continue;
        }
        if (insn->opcode == MIR_BRANCH_FALSE && uses_src1) {
            *use_kinds |= MIR_POINTER_USE_COMPARE;
            continue;
        }
        if (insn->opcode == MIR_RETURN && uses_src1) {
            *use_kinds |= MIR_POINTER_USE_RETURN;
            continue;
        }
        if (insn->opcode == MIR_UNARY && uses_src1 &&
            insn->immediate == '!') {
            *use_kinds |= MIR_POINTER_USE_COMPARE;
            continue;
        }
        *reason = mir_opcode_name(insn->opcode);
        visiting[value] = 0;
        return 0;
    }
    visiting[value] = 0;
    return 1;
}

static int mir_pointer_parameter_address_taken(const char *name)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_ADDRESS && strcmp(insn->name, name) == 0)
            return 1;
    }
    return 0;
}

static int mir_pointer_parameter_references_eligible(
    const char *name, int *use_count, unsigned int *use_kinds,
    const char **reason)
{
    unsigned char *visiting;
    int instruction;
    int found_definition = 0;

    *use_count = 0;
    *use_kinds = 0;
    *reason = "no-use";
    if (mir_pointer_parameter_address_taken(name)) {
        *reason = "address-of";
        return 0;
    }
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (insn->opcode == MIR_STORE && strcmp(insn->name, name) == 0) {
            *reason = "written";
            return 0;
        }
    }
    visiting = (unsigned char *)calloc((size_t)mir.next_value, 1);
    if (mir.next_value > 0 && visiting == NULL)
        fatal("out of memory classifying MIR pointer parameter uses");
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if ((insn->opcode != MIR_PARAM && insn->opcode != MIR_LOAD) ||
            insn->dst < 0 || strcmp(insn->name, name) != 0)
            continue;
        found_definition = 1;
        if (!mir_pointer_value_uses_are_eligible(
                insn->dst, visiting, use_count, use_kinds, reason)) {
            free(visiting);
            return 0;
        }
    }
    free(visiting);
    if (!found_definition || *use_count == 0)
        return 0;
    *reason = "eligible";
    return 1;
}

static void mir_report_pointer_parameter_eligibility(void)
{
    int instruction;

    if (getenv("DCC_MIR_POINTER_PARAM_REPORT") == NULL)
        return;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        const char *reason;
        int uses;
        unsigned int use_kinds;
        int eligible;

        if (insn->opcode != MIR_PARAM || insn->dst < 0 ||
            type_ptr_depth(insn->type) == 0)
            continue;
        eligible = mir_pointer_parameter_references_eligible(
            insn->name, &uses, &use_kinds, &reason);
        fprintf(stderr,
                "; MIR pointer-param function=%s name=%s eligible=%d "
                "uses=%d kinds=%u reason=%s\n",
                mir.name, insn->name, eligible, uses, use_kinds, reason);
    }
}

static void mir_filter_pointer_parameter_objects(void)
{
    int eligible_parameter_count = 0;
    int object;

    for (object = 0; object < mir.count; ++object) {
        const struct MirInsn *insn = &mir.insns[object];
        const char *reason;
        int uses;
        unsigned int use_kinds;

        if (insn->opcode == MIR_PARAM && insn->dst >= 0 &&
            type_ptr_depth(insn->type) > 0 &&
            mir_pointer_parameter_references_eligible(
                insn->name, &uses, &use_kinds, &reason))
            ++eligible_parameter_count;
    }

    for (object = mir.object_count - 1; object >= 0; --object) {
        struct MirObject *candidate = &mir.objects[object];
        const char *reason;
        int instruction;
        int uses;
        unsigned int use_kinds;
        int eligible;

        if (candidate->storage != SC_PARAM ||
            type_ptr_depth(candidate->type) == 0)
            continue;
        eligible = mir_pointer_parameter_references_eligible(
            candidate->name, &uses, &use_kinds, &reason);
        if (eligible &&
            (uses > 1 || eligible_parameter_count > 1 ||
             (use_kinds &
              (MIR_POINTER_USE_INDEX | MIR_POINTER_USE_MEMBER)) != 0))
            continue;
        for (instruction = 0; instruction < mir.count; ++instruction) {
            if (mir.insns[instruction].object == object) {
                /* Deferred loop-header merges have already been resolved.
                 * Preserve the named load when this object is filtered. */
                if (mir.insns[instruction].opcode == MIR_OBJECT_MERGE)
                    mir.insns[instruction].opcode = MIR_LOAD;
                mir.insns[instruction].object = -1;
            } else if (mir.insns[instruction].object > object) {
                --mir.insns[instruction].object;
            }
        }
        if (object + 1 < mir.object_count)
            memmove(&mir.objects[object], &mir.objects[object + 1],
                    (size_t)(mir.object_count - object - 1) *
                        sizeof(mir.objects[0]));
        --mir.object_count;
    }
}

static int mir_scalar_object_has_address_escape(int object)
{
    int instruction;

    if (object < 0 || object >= mir.object_count)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_ADDRESS &&
            mir.insns[instruction].object == object)
            return 1;
    return 0;
}

/* Scalar MIR objects can use a stricter address-taken proof than the legacy
 * lexer pre-scan: after lowering, only a real MIR_ADDRESS of the object's own
 * storage can make it aliasable. This keeps true `&name` escapes rejected
 * while admitting ordinary bitwise uses such as `a & b`, whose token stream
 * the shared pre-scan intentionally over-approximates. */
static void mir_filter_address_taken_scalar_objects(void)
{
    int object;

    for (object = mir.object_count - 1; object >= 0; --object) {
        struct MirObject *candidate = &mir.objects[object];
        int instruction;

        if ((candidate->storage != SC_LOCAL && candidate->storage != SC_PARAM) ||
            type_ptr_depth(candidate->type) != 0 ||
            type_is_struct_object(candidate->type) ||
            type_size(candidate->type) < 1 ||
            type_size(candidate->type) > 4 ||
            !mir_scalar_object_has_address_escape(object))
            continue;
        for (instruction = 0; instruction < mir.count; ++instruction) {
            if (mir.insns[instruction].object == object) {
                if (mir.insns[instruction].opcode == MIR_OBJECT_MERGE)
                    mir.insns[instruction].opcode = MIR_LOAD;
                mir.insns[instruction].object = -1;
            } else if (mir.insns[instruction].object > object) {
                --mir.insns[instruction].object;
            }
        }
        if (object + 1 < mir.object_count)
            memmove(&mir.objects[object], &mir.objects[object + 1],
                    (size_t)(mir.object_count - object - 1) *
                        sizeof(mir.objects[0]));
        --mir.object_count;
    }
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

    if (sym != NULL && sym->is_register)
        mir.has_declared_register_object = 1;
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
    object->is_register = sym->is_register;
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
static int mir_lvalue_type(const struct AstNode *node);
static int mir_lower_conversion(int value, int target_type);
static void mir_emit_ident_store(const struct AstNode *ident, int value);
static int mir_try_lower_inline_call_expr(const struct AstNode *call,
                                          struct Sym *fn_sym,
                                          int *out_value);
static int mir_try_lower_inline_call_stmt(const struct AstNode *call);


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

static int mir_inline_arg_reusable(const struct AstNode *node)
{
    if (node == NULL)
        return 0;
    return node->kind == AST_INT_LIT || node->kind == AST_STR_LIT ||
           node->kind == AST_SIZEOF_EXPR || node->kind == AST_SIZEOF_TYPE ||
           node->kind == AST_IDENT;
}

static int mir_inline_param_index_for_call(const struct Sym *fn,
                                           const char *name)
{
    int i;

    if (fn == NULL || name == NULL)
        return -1;
    for (i = 0; i < fn->proto_nargs && i < MAX_PROTO_PARAMS; ++i)
        if (!strcmp(fn->inline_param_names[i], name))
            return i;
    return -1;
}

static void mir_inline_temp_name_for_call(char *dst, int dstsz, int index)
{
    sprintf(dst, "#itmp%d", index);
    (void)dstsz;
}

int mir_inline_temp_slot(const char *name)
{
    const char *digit;
    int slot;

    if (name == NULL || strncmp(name, "#itmp", 5) != 0)
        return -1;
    digit = name + 5;
    if (*digit < '0' || *digit > '9')
        return -1;
    slot = 0;
    do {
        slot = slot * 10 + (*digit - '0');
        ++digit;
    } while (*digit >= '0' && *digit <= '9');
    return *digit == 0 && slot < MAX_PROTO_PARAMS ? slot : -1;
}

static int mir_current_inline_temp_id(const char *name)
{
    int slot = mir_inline_temp_slot(name);
    return slot >= 0 ? mir_inline_temp_ids[slot] : 0;
}

static const struct AstNode *mir_inline_substitution_body(const struct Sym *fn)
{
    if (fn == NULL)
        return NULL;
    if (fn->inline_return_expr != NULL)
        return fn->inline_return_expr;
    if (fn->inline_stmt_expr != NULL)
        return fn->inline_stmt_expr;
    return fn->inline_stmt_body;
}

static int mir_inline_arg_is_body_independent(const struct AstNode *node)
{
    struct Sym *symbol;
    int i;

    if (node == NULL)
        return 1;
    switch (node->kind) {
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_STR_LIT:
    case AST_SIZEOF_EXPR:
    case AST_SIZEOF_TYPE:
        return 1;
    case AST_IDENT:
        if (find_enum_const(node->sval) >= 0)
            return 1;
        symbol = find_local(node->sval);
        return symbol != NULL && !symbol->is_static && !symbol->is_volatile &&
               (symbol->storage == SC_LOCAL || symbol->storage == SC_PARAM) &&
               local_name_address_taken_in_function(node->sval) == 0;
    case AST_UNARY:
        if (node->op == '*' || node->op == TOK_INC || node->op == TOK_DEC)
            return 0;
        break;
    case AST_BINARY:
    case AST_LOGAND:
    case AST_LOGOR:
    case AST_COMMA:
    case AST_COND:
    case AST_CAST:
        break;
    default:
        return 0;
    }
    if (!mir_inline_arg_is_body_independent(node->a) ||
        !mir_inline_arg_is_body_independent(node->b) ||
        !mir_inline_arg_is_body_independent(node->c) ||
        !mir_inline_arg_is_body_independent(node->d))
        return 0;
    for (i = 0; i < node->list_len; ++i)
        if (!mir_inline_arg_is_body_independent(node->list[i]))
            return 0;
    return 1;
}

static int mir_inline_arg_needs_temp(const struct AstNode *node, int use_count)
{
    if (!mir_inline_arg_is_body_independent(node))
        return 1;
    return use_count > 1 && !mir_inline_arg_reusable(node);
}

static int mir_inline_tree_contains_call(const struct AstNode *node)
{
    int i;

    if (node == NULL)
        return 0;
    if (node->kind == AST_CALL)
        return 1;
    if (mir_inline_tree_contains_call(node->a) ||
        mir_inline_tree_contains_call(node->b) ||
        mir_inline_tree_contains_call(node->c) ||
        mir_inline_tree_contains_call(node->d))
        return 1;
    for (i = 0; i < node->list_len; ++i)
        if (mir_inline_tree_contains_call(node->list[i]))
            return 1;
    return 0;
}

static struct AstNode *mir_clone_inline_expr(struct AstArena *arena,
                                             const struct Sym *fn,
                                             const struct AstNode *src,
                                             const struct AstNode *call,
                                             const char **temp_names)
{
    struct AstNode *dst;
    int i;

    if (src == NULL)
        return NULL;
    if (src->kind == AST_IDENT) {
        if (mir_inline_local_src_name != NULL &&
            !strcmp(src->sval, mir_inline_local_src_name)) {
            dst = ast_new(arena, AST_IDENT);
            dst->type = src->type;
            dst->sval = ast_arena_strdup(arena, mir_inline_local_temp_name);
            dst->line = src->line;
            return dst;
        }
        i = mir_inline_param_index_for_call(fn, src->sval);
        if (i >= 0 && i < call->list_len && temp_names != NULL &&
            temp_names[i] != NULL) {
            dst = ast_new(arena, AST_IDENT);
            dst->type = src->type;
            dst->sval = ast_arena_strdup(arena, temp_names[i]);
            dst->line = src->line;
            return dst;
        }
        if (i >= 0 && i < call->list_len)
            return (struct AstNode *)call->list[i];
    }

    dst = ast_new(arena, src->kind);
    dst->type = src->type;
    dst->op = src->op;
    dst->ival = src->ival;
    dst->uval = src->uval;
    dst->str_index = src->str_index;
    dst->sym = src->sym;
    if (src->sval == NULL)
        dst->sval = NULL;
    else if (src->kind == AST_STR_LIT)
        dst->sval = ast_arena_memdup(arena, src->sval, (int)src->uval);
    else
        dst->sval = ast_arena_strdup(arena, src->sval);
    dst->peek_type = src->peek_type;
    dst->operand_type = src->operand_type;
    dst->line = src->line;
    dst->file = src->file;
    dst->end_file = src->end_file;
    dst->end_line = src->end_line;

    dst->a = mir_clone_inline_expr(arena, fn, src->a, call, temp_names);
    dst->b = mir_clone_inline_expr(arena, fn, src->b, call, temp_names);
    dst->c = mir_clone_inline_expr(arena, fn, src->c, call, temp_names);
    dst->d = mir_clone_inline_expr(arena, fn, src->d, call, temp_names);
    for (i = 0; i < src->list_len; ++i)
        ast_list_push(arena, dst,
                      mir_clone_inline_expr(arena, fn, src->list[i], call,
                                            temp_names));
    return dst;
}

static void mir_emit_symbol_store(struct Sym *symbol, int value)
{
    struct AstNode ident;

    if (symbol == NULL)
        return;
    memset(&ident, 0, sizeof(ident));
    ident.kind = AST_IDENT;
    ident.type = symbol->type;
    ident.sym = symbol;
    ident.sval = symbol->name;
    mir_emit_ident_store(&ident, value);
}

static int mir_plan_inline_arg_temps(const struct AstNode *call,
                                     struct Sym *fn_sym,
                                     const char **temp_names,
                                     struct Sym **temp_symbols,
                                     int *temp_types,
                                     char temp_name_buf[MAX_PROTO_PARAMS][64],
                                     unsigned long *temp_mask)
{
    unsigned long unavailable;
    unsigned long current_mask;
    int i;

    if (call == NULL || fn_sym == NULL || temp_mask == NULL)
        return 0;
    unavailable = mir_inline_live_temp_mask;
    current_mask = 0;
    for (i = 0; i < call->list_len; ++i) {
        struct Sym *tmp;
        int want_type;
        int slot;

        temp_names[i] = NULL;
        temp_symbols[i] = NULL;
        temp_types[i] = 0;
        if (!mir_inline_arg_needs_temp(call->list[i],
                                       fn_sym->inline_param_use_count[i]))
            continue;
        want_type = fn_sym->proto_types[i] ? fn_sym->proto_types[i] : TYPE_INT;
        if (type_size(want_type) != 2 || type_is_float(want_type) ||
            type_is_long(want_type))
            return 0;
        slot = i;
        if ((unavailable & (1UL << slot)) != 0) {
            for (slot = 0; slot < MAX_PROTO_PARAMS; ++slot)
                if ((unavailable & (1UL << slot)) == 0)
                    break;
            if (slot >= MAX_PROTO_PARAMS)
                return 0;
        }
        unavailable |= 1UL << slot;
        current_mask |= 1UL << slot;
        mir_inline_temp_name_for_call(temp_name_buf[i], 64, slot);
        tmp = find_local(temp_name_buf[i]);
        if (tmp == NULL)
            return 0;
        temp_names[i] = temp_name_buf[i];
        temp_symbols[i] = tmp;
        temp_types[i] = want_type;
    }
    *temp_mask = current_mask;
    return 1;
}

static int mir_plan_inline_local_temp(struct Sym *fn_sym, unsigned long used_mask,
                                      struct Sym **temp_symbol,
                                      char temp_name_buf[64],
                                      unsigned long *temp_bit)
{
    int slot;
    struct Sym *tmp;

    if (temp_symbol == NULL || fn_sym == NULL || !fn_sym->has_inline_local)
        return 0;
    slot = fn_sym->proto_nargs;
    if ((used_mask & (1UL << slot)) != 0) {
        for (slot = 0; slot < MAX_PROTO_PARAMS; ++slot)
            if ((used_mask & (1UL << slot)) == 0)
                break;
        if (slot >= MAX_PROTO_PARAMS)
            return 0;
    }
    mir_inline_temp_name_for_call(temp_name_buf, 64, slot);
    tmp = find_local(temp_name_buf);
    if (tmp == NULL || type_size(fn_sym->inline_local_type) != 2)
        return 0;
    *temp_symbol = tmp;
    if (temp_bit != NULL)
        *temp_bit = 1UL << slot;
    return 1;
}

static void mir_emit_planned_inline_arg_temps(const struct AstNode *call,
                                              const struct Sym *fn_sym,
                                              struct Sym **temp_symbols,
                                              int *temp_types)
{
    int i;

    for (i = call->list_len - 1; i >= 0; --i) {
        int value;

        if (temp_symbols[i] == NULL)
            continue;
        temp_symbols[i]->type = temp_types[i];
        value = mir_lower_expr(call->list[i]);
        value = mir_lower_conversion(value, temp_types[i]);
        mir_emit_symbol_store(temp_symbols[i], value);
    }
    (void)fn_sym;
}

static int mir_emit_inline_local_temp(struct Sym *fn_sym,
                                      const struct AstNode *call,
                                      const char **temp_names,
                                      struct Sym *tmp)
{
    struct AstNode *init_substituted;
    const char *saved_src_name;
    const char *saved_temp_name;
    int value;

    if (fn_sym == NULL || tmp == NULL || !fn_sym->has_inline_local)
        return 0;
    saved_src_name = mir_inline_local_src_name;
    saved_temp_name = mir_inline_local_temp_name;
    mir_inline_local_src_name = NULL;
    mir_inline_local_temp_name = NULL;
    init_substituted = mir_clone_inline_expr(&g_ast_arena, fn_sym,
                                             fn_sym->inline_local_init,
                                             call, temp_names);
    mir_inline_local_src_name = saved_src_name;
    mir_inline_local_temp_name = saved_temp_name;
    tmp->type = fn_sym->inline_local_type;
    value = mir_lower_expr(init_substituted);
    value = mir_lower_conversion(value, tmp->type);
    mir_emit_symbol_store(tmp, value);
    return 1;
}

static int mir_try_lower_inline_call_common(const struct AstNode *call,
                                            struct Sym *fn_sym,
                                            struct AstNode **out_expr,
                                            struct AstNode **out_stmt,
                                            struct MirInlineCallScope *scope_out)
{
    const struct AstNode *src_expr;
    const char *temp_names[MAX_PROTO_PARAMS];
    struct Sym *temp_symbols[MAX_PROTO_PARAMS];
    int temp_types[MAX_PROTO_PARAMS];
    char temp_name_buf[MAX_PROTO_PARAMS][64];
    char local_temp_name[64];
    struct Sym *local_temp_symbol;
    unsigned long local_temp_bit;
    unsigned long current_mask;
    unsigned long active_mask;
    const char *saved_local_src_name;
    const char *saved_local_temp_name;
    int slot;

    if (scope_out == NULL)
        return 0;
    if (out_expr != NULL)
        *out_expr = NULL;
    if (out_stmt != NULL)
        *out_stmt = NULL;
    if (opt_debug || fn_sym == NULL || !fn_sym->is_static || !fn_sym->is_inline ||
        mir_inline_substitution_body(fn_sym) == NULL)
        return 0;
    if (mir_inline_expand_depth >= 8)
        return 0;
    if (call->list_len != fn_sym->proto_nargs || call->list_len > MAX_PROTO_PARAMS)
        return 0;
    if (!mir_plan_inline_arg_temps(call, fn_sym, temp_names, temp_symbols,
                                   temp_types, temp_name_buf, &current_mask))
        return 0;
    local_temp_symbol = NULL;
    local_temp_name[0] = 0;
    local_temp_bit = 0;
    if (fn_sym->has_inline_local &&
        !mir_plan_inline_local_temp(fn_sym,
                                    mir_inline_live_temp_mask | current_mask,
                                    &local_temp_symbol, local_temp_name,
                                    &local_temp_bit))
        return 0;

    scope_out->live_temp_mask = mir_inline_live_temp_mask;
    memcpy(scope_out->temp_ids, mir_inline_temp_ids,
           sizeof(scope_out->temp_ids));
    active_mask = current_mask | local_temp_bit;
    mir_inline_live_temp_mask |= active_mask;
    for (slot = 0; slot < MAX_PROTO_PARAMS; ++slot)
        if ((active_mask & (1UL << slot)) != 0)
            mir_inline_temp_ids[slot] = mir.next_inline_temp_id++;
    mir_emit_planned_inline_arg_temps(call, fn_sym, temp_symbols, temp_types);
    if (local_temp_symbol != NULL && !mir_emit_inline_local_temp(fn_sym, call,
                                                                 temp_names,
                                                                 local_temp_symbol)) {
        mir_inline_live_temp_mask = scope_out->live_temp_mask;
        memcpy(mir_inline_temp_ids, scope_out->temp_ids,
               sizeof(scope_out->temp_ids));
        return 0;
    }

    saved_local_src_name = mir_inline_local_src_name;
    saved_local_temp_name = mir_inline_local_temp_name;
    if (local_temp_symbol != NULL) {
        mir_inline_local_src_name = fn_sym->inline_local_name;
        mir_inline_local_temp_name = local_temp_name;
    } else {
        mir_inline_local_src_name = NULL;
        mir_inline_local_temp_name = NULL;
    }
    mir_inline_expand_depth++;
    src_expr = mir_inline_substitution_body(fn_sym);
    if (fn_sym->inline_stmt_body != NULL)
        *out_stmt = mir_clone_inline_expr(&g_ast_arena, fn_sym, src_expr, call,
                                          temp_names);
    else
        *out_expr = mir_clone_inline_expr(&g_ast_arena, fn_sym, src_expr, call,
                                          temp_names);
    mir_inline_local_src_name = saved_local_src_name;
    mir_inline_local_temp_name = saved_local_temp_name;
    return 1;
}

static void mir_end_inline_call_scope(const struct MirInlineCallScope *scope)
{
    mir_inline_expand_depth--;
    mir_inline_live_temp_mask = scope->live_temp_mask;
    memcpy(mir_inline_temp_ids, scope->temp_ids, sizeof(scope->temp_ids));
}

static int mir_try_lower_inline_call_expr(const struct AstNode *call,
                                          struct Sym *fn_sym,
                                          int *out_value)
{
    struct AstNode *expr;
    struct AstNode *stmt;
    struct MirInlineCallScope scope;

    if (out_value == NULL)
        return 0;
    if (fn_sym != NULL &&
        (fn_sym->inline_stmt_expr != NULL || fn_sym->inline_stmt_body != NULL))
        return 0;
    expr = NULL;
    stmt = NULL;
    if (!mir_try_lower_inline_call_common(
            call, fn_sym, &expr, &stmt, &scope))
        return 0;
    if (expr == NULL || stmt != NULL) {
        mir_end_inline_call_scope(&scope);
        return 0;
    }
    *out_value = mir_lower_expr(expr);
    mir_end_inline_call_scope(&scope);
    return 1;
}

static int mir_try_lower_inline_call_stmt(const struct AstNode *call)
{
    struct Sym *fn_sym;
    struct AstNode *expr;
    struct AstNode *stmt;
    struct MirInlineCallScope scope;

    if (call == NULL || call->kind != AST_CALL || call->a == NULL ||
        call->a->kind != AST_IDENT)
        return 0;
    fn_sym = find_global(call->a->sval);
    if (fn_sym == NULL ||
        (fn_sym->inline_stmt_expr == NULL && fn_sym->inline_stmt_body == NULL))
        return 0;
    /* Keep call-free void store helpers behind the existing fallback gate for
     * now: tinlnpar.main proved that lowering those directly into MIR can be
     * text-smaller yet still regress the checked peep path. Call-containing
     * void helpers such as attnc11's add_clamped remain worth replaying here
     * because they clear the correctness-critical nested-call class. */
    if (!mir_inline_tree_contains_call(mir_inline_substitution_body(fn_sym)))
        return 0;
    expr = NULL;
    stmt = NULL;
    if (!mir_try_lower_inline_call_common(
            call, fn_sym, &expr, &stmt, &scope))
        return 0;
    if (stmt != NULL)
        mir_lower_stmt(stmt);
    else if (expr != NULL)
        (void)mir_lower_expr(expr);
    else {
        mir_end_inline_call_scope(&scope);
        return 0;
    }
    mir_end_inline_call_scope(&scope);
    return 1;
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
    if (field != NULL && field->is_array) {
        stride = field->elem_size > 0 ? field->elem_size
                                      : type_size(field->elem_type);
        if (stride <= 0)
            stride = 1;
        for (dimension = depth; dimension < field->dim_count; ++dimension)
            if (field->dims[dimension] > 0)
                stride *= field->dims[dimension];
    } else if (field != NULL && type_ptr_depth(field->type) > 0) {
        stride = type_index_elem_size(field->type);
    } else if (root != NULL && root->is_array)
        stride = sym_array_index_elem_size(root, depth - 1);
    else if (root != NULL)
        stride = sym_pointer_array_index_elem_size(root, root->type, depth - 1);
    else {
        struct Sym *pointer_array = mir_pointer_array_root(
            node != NULL ? node->a : NULL, &dereference_depth);
        if (pointer_array != NULL) {
            if (dereference_depth >= pointer_array->dim_count)
                stride = type_size(type_decay_ptr(pointer_array->type));
            else
                stride = sym_pointer_array_index_elem_size(
                    pointer_array, pointer_array->type, dereference_depth);
        } else
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
    if (symbol != NULL && symbol->is_volatile)
        insn->memory_flags |= 1;
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
        int dereferenced_pointer_array = 0;
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
            dereferenced_pointer_array = pointer_array != NULL;
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
        if (dereferenced_pointer_array)
            insn->memory_flags |=
                MIR_MEMORY_FLAG_DEREFERENCED_POINTER_ARRAY;
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
    store->inline_temp_id = mir_current_inline_temp_id(store->name);
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
        insn->inline_temp_id = mir_current_inline_temp_id(insn->name);
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
            value = mir_lower_ident_assignment_conversion(value, node->a);
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
        if (strcmp(call_name, "<indirect>") != 0 &&
            mir_try_lower_inline_call_expr(node, function_symbol, &value))
            return value;
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
        if ((function_symbol != NULL && function_symbol->proto_variadic) ||
            (call_prototype != NULL && call_prototype->proto_variadic))
            insn->memory_flags |= MIR_CALL_FLAG_VARIADIC;
        if (function_symbol != NULL) {
            if (mir_inline_substitutable(function_symbol))
                insn->memory_flags |= MIR_CALL_FLAG_INLINE_SUBSTITUTABLE;
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
                    insn->memory_flags |= MIR_CALL_FLAG_FORMAT_HEX;
                if (needs_octal)
                    insn->memory_flags |= MIR_CALL_FLAG_FORMAT_OCTAL;
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
    int then_label;
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
        if (node->a != NULL && node->a->kind == AST_CALL &&
            mir_try_lower_inline_call_stmt(node->a))
            return;
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
        /* Only an if/else has a genuine two-predecessor join at end_label
         * below; a bare if with no else has nothing to merge there (the
         * then-arm either returns or falls straight into else_label, which
         * is already labeled) - Item 39 tried extending this label to the
         * bare-if/fallthrough case too and deferred it, see the Execution
         * Log. Scoped to the if/else case only, this label gives
         * mir_try_make_object_phi() a physical predecessor identity for the
         * join: without it, an object stored identically from both arms of
         * an if/else can never be recognized as a safe reuse, because
         * mir_block_label_before() requires a real label on every
         * predecessor block. */
        if (node->c != NULL) {
            then_label = mir_new_label();
            mir_emit_label(then_label);
        }
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
    mir.next_inline_temp_id = 1;
    mir_inline_live_temp_mask = 0;
    memset(mir_inline_temp_ids, 0, sizeof(mir_inline_temp_ids));
    mir_inline_expand_depth = 0;
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
    mir.has_declared_register_object = 0;
    mir.region_count = 0;
    mir.regional_segment_count = 0;
    mir.regional_spill_slot_count = 0;
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
                      getenv("DCC_MIR_EMIT_FUNCTION") != NULL ||
                      getenv("DCC_MIR_GENERAL_FUNCTION") != NULL;
    mir.return_type = current_return_type != 0 ? current_return_type
        : function_symbol != NULL ? function_symbol->type : TYPE_INT;
    mir.local_bytes = local_bytes;
    mir.dead_local_suffix_bytes = 0;
    mir.aggregate_temp_bytes = 0;
    mir.opaque_count = 0;
    mir_extended_integer_constant_conversion_fold_count = 0;
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

            if (locals[local].storage == SC_LOCAL &&
                strncmp(locals[local].name, "#itmp", 5) == 0)
                /* Inline argument temporaries are reserved frame locals, not
                 * lexical declarations, so no later declaration replay can
                 * publish their otherwise-valid memory locations. Keep them
                 * out of SSA promotion but make them addressable by selectors. */
                mir_note_declared_symbol(&locals[local]);
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

struct MirInsn *mir_mutable_definition(int value)
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

static int mir_block_cse_count;
static int mir_global_field_vn_count;

int mir_global_field_value_numbering_count(void)
{
    return mir_global_field_vn_count;
}

static int mir_same_isolated_global_field_address(
    const struct MirResolvedNamedAddress *left,
    const struct MirResolvedNamedAddress *right)
{
    return left->storage == right->storage &&
           left->offset == right->offset &&
           !strcmp(left->base_name, right->base_name) &&
           !strcmp(left->leaf_member_name, right->leaf_member_name);
}

static int mir_isolated_global_field_call_safe(
    const struct MirResolvedNamedAddress *resolved)
{
    int writes = global_text_field_write_count(
        resolved->base_name, resolved->leaf_member_name);

    return writes == 0 ||
           (writes == 1 &&
            !global_text_field_written_in_function(
                resolved->base_name, resolved->leaf_member_name,
                g_current_compiling_func));
}

static int mir_resolve_isolated_global_field_load(
    const struct MirInsn *insn, struct MirResolvedNamedAddress *resolved)
{
    return insn != NULL && insn->opcode == MIR_LOAD_INDIRECT &&
           insn->bit_width == 0 &&
           insn->memory_size > 0 && insn->memory_size <= 2 &&
           mir_resolve_isolated_global_field_address(insn->src1, resolved);
}

static int mir_resolve_isolated_global_field_store(
    const struct MirInsn *insn, struct MirResolvedNamedAddress *resolved)
{
    return insn != NULL && insn->opcode == MIR_STORE_INDIRECT &&
           insn->bit_width == 0 &&
           insn->memory_size > 0 && insn->memory_size <= 2 &&
           mir_resolve_isolated_global_field_address(insn->src1, resolved);
}

static int mir_last_value_use(int value)
{
    int instruction;
    int last = -1;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->src1 == value || insn->src2 == value ||
            mir_call_uses_value(insn, value))
            last = instruction;
    }
    return last;
}

static int mir_value_use_count_after(int value, int instruction)
{
    int count = 0;
    int i;

    for (i = instruction + 1; i < mir.count; ++i) {
        const struct MirInsn *insn = &mir.insns[i];

        if (insn->src1 == value)
            ++count;
        if (insn->src2 == value)
            ++count;
        if (mir_call_uses_value(insn, value))
            ++count;
    }
    return count;
}

static int mir_value_live_out_of_instruction(int value, int instruction)
{
    if (instruction >= 0 && instruction < mir.count &&
        value >= 0 && value < mir.next_value &&
        mir.live_out != NULL)
        return mir.live_out[(size_t)instruction * mir.next_value + value] != 0;
    return mir_value_has_use_after(value, instruction);
}

int mir_value_number_global_field_loads(void)
{
    struct MirGlobalFieldValue {
        struct MirResolvedNamedAddress address;
        int value;
        int call_safe;
    } available[128];
    int available_count = 0;
    int replaced = 0;
    int instruction;

    mir_global_field_vn_count = 0;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        struct MirInsn *insn = &mir.insns[instruction];
        struct MirResolvedNamedAddress resolved;
        int entry;

        if (insn->opcode == MIR_LABEL)
            available_count = 0;
        if (mir_resolve_isolated_global_field_load(insn, &resolved)) {
            for (entry = 0; entry < available_count; ++entry)
                if (mir_same_isolated_global_field_address(
                        &available[entry].address, &resolved) &&
                    mir_value_use_count(insn->dst) > 0 &&
                    mir_value_use_count_after(
                        available[entry].value, instruction) > 0 &&
                    mir_value_live_out_of_instruction(
                        available[entry].value, instruction) &&
                    mir_last_value_use(available[entry].value) >=
                        mir_last_value_use(insn->dst)) {
                    mir_replace_value_uses(insn->dst, available[entry].value);
                    insn->opcode = MIR_NOP;
                    insn->dst = -1;
                    insn->src1 = -1;
                    insn->src2 = -1;
                    ++replaced;
                    break;
                }
            if (entry == available_count) {
                if (available_count < (int)(sizeof(available) /
                                            sizeof(available[0]))) {
                    available[available_count].address = resolved;
                    available[available_count].value = insn->dst;
                    available[available_count].call_safe =
                        mir_isolated_global_field_call_safe(&resolved);
                    ++available_count;
                } else {
                    available_count = 0;
                }
            }
        }
        if (mir_resolve_isolated_global_field_store(insn, &resolved))
            for (entry = 0; entry < available_count; ++entry)
                if (mir_same_isolated_global_field_address(
                        &available[entry].address, &resolved)) {
                    available[entry] = available[available_count - 1];
                    --available_count;
                    break;
                }
        if (insn->opcode == MIR_CALL || insn->opcode == MIR_CALL_AGGREGATE)
            for (entry = available_count - 1; entry >= 0; --entry)
                if (!available[entry].call_safe) {
                    available[entry] = available[available_count - 1];
                    --available_count;
                }
        if (insn->opcode == MIR_OPAQUE ||
            insn->opcode == MIR_COPY_AGGREGATE ||
            insn->opcode == MIR_VLA_SAVE ||
            insn->opcode == MIR_VLA_ALLOC ||
            insn->opcode == MIR_VLA_RESTORE ||
            insn->opcode == MIR_VA_START ||
            insn->opcode == MIR_VA_END ||
            insn->opcode == MIR_VA_ARG)
            available_count = 0;
        if (insn->opcode == MIR_JUMP ||
            insn->opcode == MIR_BRANCH_FALSE ||
            insn->opcode == MIR_RETURN ||
            insn->opcode == MIR_VLA_ALLOC ||
            insn->opcode == MIR_VLA_RESTORE)
            available_count = 0;
    }
    if (replaced != 0 && getenv("DCC_MIR_CSE_REPORT") != NULL)
        fprintf(stderr,
                "; MIR field-vn function=%s eliminated=%d\n",
                mir.name, replaced);
    mir_global_field_vn_count = replaced;
    return replaced;
}

int mir_repeated_named_pointer_load_count(void)
{
    struct MirNamedPointerLoad {
        int storage;
        long offset;
        int is_field;
        char base_name[64];
        char member_name[64];
    } seen[128];
    int seen_count = 0;
    int repeats = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        struct MirResolvedNamedAddress resolved;
        struct MirNamedPointerLoad key;
        int memory_type;
        int memory_storage;
        int memory_offset;
        int entry;

        memset(&key, 0, sizeof(key));
        key.storage = -1;
        if (type_ptr_depth(insn->type) == 0)
            continue;
        if (insn->opcode == MIR_LOAD &&
            mir_scalar_memory_location(
                insn, &memory_type, &memory_storage, &memory_offset) &&
            (memory_storage == SC_GLOBAL || memory_storage == SC_EXTERN) &&
            type_ptr_depth(memory_type) > 0) {
            key.storage = memory_storage;
            key.offset = memory_offset;
            snprintf(key.base_name, sizeof(key.base_name), "%s", insn->name);
        } else if (mir_resolve_isolated_global_field_load(insn, &resolved)) {
            key.storage = resolved.storage;
            key.offset = resolved.offset;
            key.is_field = 1;
            snprintf(key.base_name, sizeof(key.base_name), "%s",
                     resolved.base_name);
            snprintf(key.member_name, sizeof(key.member_name), "%s",
                     resolved.leaf_member_name);
        } else {
            continue;
        }

        for (entry = 0; entry < seen_count; ++entry)
            if (seen[entry].storage == key.storage &&
                seen[entry].offset == key.offset &&
                seen[entry].is_field == key.is_field &&
                !strcmp(seen[entry].base_name, key.base_name) &&
                !strcmp(seen[entry].member_name, key.member_name)) {
                ++repeats;
                break;
            }
        if (entry == seen_count) {
            if (seen_count < (int)(sizeof(seen) / sizeof(seen[0])))
                seen[seen_count++] = key;
            else
                return repeats + 1;
        }
    }
    return repeats;
}

static int mir_expression_is_address(const struct MirInsn *insn)
{
    return insn->opcode == MIR_ADDRESS ||
           insn->opcode == MIR_MEMBER_ADDRESS ||
           insn->opcode == MIR_INDEX_ADDRESS;
}

/* True if reusing an earlier MIR_LOAD of this object across any
 * intervening call (however deep or opaque) is safe. MIR_LOAD/MIR_LOADIND
 * are deliberately excluded from mir_common_expressions_equal's whitelist
 * in general, since an intervening call could write back to the loaded
 * location through an escaped alias and a static scan of this function's
 * own MIR cannot see that (mir-text-size-plan.md Item T390's cobint.
 * add_var finding). This carves out the one class where that concern
 * provably cannot apply: a `static` (internal-linkage) file-scope object
 * whose address is never taken and which is never written anywhere in
 * the whole translation unit. Both facts come from the same whole-file
 * lexical pre-scan (dcc_global_scan.c) that
 * ast_for_hoist_global_member_value_supported already uses for an
 * equivalent invariance proof, restricted here to `static` storage
 * specifically because that scan only sees the current file - a plain
 * `extern` global could still be written from another translation unit
 * the scan never looks at. A zero write count means the object's value
 * is exactly its static initializer for the program's entire execution:
 * with no writer anywhere and no way to alias it, no call can ever change
 * it, so two loads of it are always equal regardless of what runs
 * between them. */
static int mir_load_object_is_call_safe(int object)
{
    struct Sym *symbol;

    if (object < 0 || object >= mir.object_count)
        return 0;
    if (mir.objects[object].storage != SC_GLOBAL)
        return 0;
    symbol = find_sym(mir.objects[object].name);
    if (symbol == NULL || !symbol->is_static)
        return 0;
    if (global_text_addr_taken_count(mir.objects[object].name) != 0)
        return 0;
    if (global_text_write_count(mir.objects[object].name) != 0)
        return 0;
    return 1;
}

static int mir_block_local_load_reusable_between(
    const struct MirInsn *load, int previous, int current)
{
    struct Sym *symbol;
    const char *name;
    int instruction;

    if (load->memory_flags != 0)
        return 0;
    name = load->object >= 0 && load->object < mir.object_count
        ? mir.objects[load->object].name : load->name;
    symbol = name[0] != 0 ? find_sym(name) : NULL;
    if (symbol == NULL || symbol->storage != SC_GLOBAL ||
        !symbol->is_static || global_text_addr_taken_count(name) != 0)
        return 0;

    for (instruction = previous + 1; instruction < current; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->opcode == MIR_CALL ||
            insn->opcode == MIR_CALL_AGGREGATE ||
            insn->opcode == MIR_OPAQUE)
            return 0;
        if (insn->opcode == MIR_STORE &&
            ((load->object >= 0 && insn->object == load->object) ||
             (insn->name[0] != 0 && !strcmp(insn->name, name))))
            return 0;
    }
    return 1;
}

/* Like mir_load_object_is_call_safe() above, but for the exact
 * `static_global.field` loadind shape already proven by T403's field-level
 * scan: isolated file-local global-field loads whose field address never
 * escapes, and whose one textual writer (if any) is not in the function
 * currently being optimized, can be reused safely across intervening calls. */
static int mir_load_indirect_is_call_safe(const struct MirInsn *insn)
{
    struct MirResolvedNamedAddress resolved;

    return mir_resolve_isolated_global_field_load(insn, &resolved) &&
           mir_isolated_global_field_call_safe(&resolved);
}

static int mir_common_expressions_equal(const struct MirInsn *left,
                                        const struct MirInsn *right)
{
    if (left->opcode != right->opcode)
        return 0;
    switch (left->opcode) {
    case MIR_ADDRESS:
    case MIR_MEMBER_ADDRESS:
    case MIR_INDEX_ADDRESS:
    case MIR_CONST:
    case MIR_FLOAT_CONST:
    case MIR_STRING_ADDRESS:
    case MIR_BINARY:
        break;
    case MIR_UNARY:
        if (left->memory_flags != 0 || right->memory_flags != 0)
            return 0;
        break;
    case MIR_LOAD:
        if (left->memory_flags != 0 ||
            (!mir_load_object_is_call_safe(left->object) &&
             mir_cfg_block_count() != 2))
            return 0;
        break;
    case MIR_LOAD_INDIRECT:
        if (!mir_load_indirect_is_call_safe(left))
            return 0;
        break;
    default:
        return 0;
    }
    return left->src1 == right->src1 &&
           left->src2 == right->src2 &&
           left->immediate == right->immediate &&
           left->object == right->object &&
           left->type == right->type &&
           left->secondary_offset == right->secondary_offset &&
           left->memory_size == right->memory_size &&
           left->memory_flags == right->memory_flags &&
           left->bit_width == right->bit_width &&
           left->bit_shift == right->bit_shift &&
           left->bit_mask == right->bit_mask &&
           strcmp(left->name, right->name) == 0 &&
           strcmp(left->base_name, right->base_name) == 0;
}

/* Reuse equivalent pure SSA values within one block. Addresses may safely
 * remain live longer; other expressions are reused only when the earlier
 * value is already live beyond the duplicate, so allocation pressure cannot
 * increase. */
int mir_eliminate_common_block_expressions(void)
{
    int block_start = 0;
    int eliminated = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        struct MirInsn *insn = &mir.insns[instruction];
        int previous;

        if (insn->opcode == MIR_LABEL)
            block_start = instruction;
        for (previous = instruction - 1;
             previous >= block_start; --previous) {
            struct MirInsn *candidate = &mir.insns[previous];
            if (!mir_common_expressions_equal(candidate, insn) ||
                (insn->opcode == MIR_LOAD &&
                 !mir_load_object_is_call_safe(insn->object) &&
                 !mir_block_local_load_reusable_between(
                     insn, previous, instruction)) ||
                (!mir_expression_is_address(insn) &&
                  (insn->opcode != MIR_LOAD ||
                   mir_cfg_block_count() != 2) &&
                  !mir_value_has_use_after(candidate->dst, instruction)))
                continue;
            mir_replace_value_uses(insn->dst, candidate->dst);
            insn->opcode = MIR_NOP;
            insn->dst = -1;
            insn->src1 = -1;
            insn->src2 = -1;
            ++eliminated;
            break;
        }
        if (insn->opcode == MIR_JUMP ||
            insn->opcode == MIR_BRANCH_FALSE ||
            insn->opcode == MIR_RETURN ||
            insn->opcode == MIR_VLA_ALLOC ||
            insn->opcode == MIR_VLA_RESTORE)
            block_start = instruction + 1;
    }
    if (eliminated != 0 && getenv("DCC_MIR_CSE_REPORT") != NULL)
        fprintf(stderr, "; MIR block-cse function=%s eliminated=%d\n",
                mir.name, eliminated);
    mir_block_cse_count = eliminated;
    return eliminated;
}

int mir_common_block_expression_elimination_count(void)
{
    return mir_block_cse_count;
}

static int mir_region_expression_boundary(const struct MirInsn *insn)
{
    if (insn == NULL)
        return 1;
    switch (insn->opcode) {
    case MIR_LABEL:
    case MIR_JUMP:
    case MIR_BRANCH_FALSE:
    case MIR_RETURN:
    case MIR_CALL:
    case MIR_CALL_AGGREGATE:
    case MIR_VLA_ALLOC:
    case MIR_VLA_RESTORE:
        return 1;
    default:
        return 0;
    }
}

static int mir_region_expression_supported(const struct MirInsn *insn)
{
    return insn != NULL &&
           (insn->opcode == MIR_LOAD ||
            insn->opcode == MIR_ADDRESS ||
            insn->opcode == MIR_MEMBER_ADDRESS ||
            insn->opcode == MIR_INDEX_ADDRESS);
}

static int mir_region_expressions_equal(const struct MirInsn *left,
                                        const struct MirInsn *right)
{
    if (left->opcode != MIR_LOAD || right->opcode != MIR_LOAD) {
        return mir_common_expressions_equal(left, right);
    }
    if (left->memory_flags != 0 || right->memory_flags != 0)
        return 0;
    return left->src1 == right->src1 &&
           left->src2 == right->src2 &&
           left->immediate == right->immediate &&
           left->object == right->object &&
           left->type == right->type &&
           left->secondary_offset == right->secondary_offset &&
           left->memory_size == right->memory_size &&
           left->memory_flags == right->memory_flags &&
           left->bit_width == right->bit_width &&
           left->bit_shift == right->bit_shift &&
           left->bit_mask == right->bit_mask &&
           strcmp(left->name, right->name) == 0 &&
           strcmp(left->base_name, right->base_name) == 0;
}

/*
 * Reuse stable loads and address chains only inside one call-free CFG
 * region. Unlike whole-block address CSE, this never retains a value across
 * a call, label, branch, jump, return, or VLA stack boundary; the existing
 * spilled backend therefore provides the explicit boundary home while the
 * value remains in SSA inside the region.
 */
int mir_eliminate_common_region_expressions(void)
{
    int region_start = 0;
    int eliminated = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        struct MirInsn *insn = &mir.insns[instruction];
        int previous;

        if (mir_region_expression_boundary(insn)) {
            region_start = instruction + 1;
            continue;
        }
        if (!mir_region_expression_supported(insn))
            continue;
        for (previous = instruction - 1;
             previous >= region_start; --previous) {
            struct MirInsn *candidate = &mir.insns[previous];

            if (!mir_region_expression_supported(candidate) ||
                !mir_region_expressions_equal(candidate, insn) ||
                (insn->opcode == MIR_LOAD &&
                 !mir_load_object_is_call_safe(insn->object) &&
                 !mir_block_local_load_reusable_between(
                     insn, previous, instruction)))
                continue;
            mir_replace_value_uses(insn->dst, candidate->dst);
            insn->opcode = MIR_NOP;
            insn->dst = -1;
            insn->src1 = -1;
            insn->src2 = -1;
            ++eliminated;
            break;
        }
    }
    if (eliminated != 0 &&
        (getenv("DCC_MIR_CSE_REPORT") != NULL ||
         getenv("DCC_MIR_REGIONAL_HOME_REPORT") != NULL))
        fprintf(stderr,
                "; MIR regional-cse function=%s eliminated=%d\n",
                mir.name, eliminated);
    return eliminated;
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

int mir_declared_location(const char *name, int *type, int *storage,
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

const char *mir_declared_link_name(const char *name)
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

int mir_declared_is_vla_object(const char *name)
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

/* Item 35/36 (mir-migration-plan-100): thread an explicit jump/branch
 * through a chain of labels whose only content is an immediately-
 * following unconditional jump, retargeting straight to the final
 * destination. The canonical case is a loop's "continue:" landing block
 * with no actual continue statement targeting it (and_expr,
 * tests/adaint.c: "L3575: jp L3573") - the legacy AST backend's loop
 * lowering never needs a separate continuation block for the simple
 * case, but mir_lower_stmt's AST_WHILE/AST_FOR/AST_DOWHILE handling
 * always allocates one so AST_CONTINUE has somewhere to target, whether
 * or not the loop body actually contains a continue statement.
 *
 * This is purely jump threading: every intermediate label was always
 * going to fall straight through to the next jump in the chain, so
 * retargeting an explicit predecessor to skip the whole chain cannot
 * change which instruction executes next for any input. Running this
 * before mir_cfg_block_count(), mir_has_cfg_backedge(), and the
 * phi-construction pass that all run later in mir_end_function/the
 * selectors means every one of those later analyses sees the
 * already-simplified CFG directly, rather than needing their own
 * special-casing for the redundant hops.
 *
 * Item 36 generalizes Item 35's single-hop chase to transitive chains
 * (label -> jump -> label -> jump -> ... -> final target), and also
 * skips over any MIR_NOP instructions between a label and the jump that
 * follows it - user-named goto labels (unlike compiler-synthesized loop
 * labels) get an MIR_NOP carrying the source name immediately after the
 * MIR_LABEL for diagnostics, which would otherwise hide an identical
 * jump-only shape from Item 35's original immediately-next-instruction
 * check. MIR_NOP never emits any code (dcc_mir.c's emitter simply
 * `break`s on it), so skipping past one changes nothing about which
 * instruction the retargeted jump actually reaches. The chase tracks
 * each label id visited so far in a small fixed-size buffer; if a
 * jump-only chain ever revisits an id (which cannot arise from real
 * lowering - a legitimate loop's label graph is acyclic through pure
 * unconditional jumps - but would otherwise spin the loop forever), the
 * chase stops at the last good target instead of following the cycle.
 * The buffer size bounds the longest chain threaded in one pass; a chain
 * longer than that is left partially threaded, which is only a missed
 * optimization, never a correctness problem. */
#define MIR_THREAD_JUMPS_MAX_CHAIN 256
void mir_thread_jumps(void)
{
    int i;

    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        int visited[MIR_THREAD_JUMPS_MAX_CHAIN];
        int visited_count = 0;
        int current = insn->label;

        if (insn->opcode != MIR_JUMP && insn->opcode != MIR_BRANCH_FALSE)
            continue;
        visited[visited_count++] = current;
        for (;;) {
            int label_index = mir_find_label(current);
            int scan;
            int next;
            int j;
            int cyclic;

            if (label_index < 0)
                break;
            scan = label_index + 1;
            while (scan < mir.count && mir.insns[scan].opcode == MIR_NOP)
                ++scan;
            if (scan >= mir.count || mir.insns[scan].opcode != MIR_JUMP)
                break;
            next = mir.insns[scan].label;
            cyclic = 0;
            for (j = 0; j < visited_count; ++j) {
                if (visited[j] == next) {
                    cyclic = 1;
                    break;
                }
            }
            if (cyclic || visited_count >= MIR_THREAD_JUMPS_MAX_CHAIN)
                break;
            visited[visited_count++] = next;
            current = next;
        }
        insn->label = current;
    }
}
#undef MIR_THREAD_JUMPS_MAX_CHAIN

static int mir_boolean_phi_predecessor_is_transparent(int label)
{
    int instruction = mir_find_label(label);

    if (instruction < 0)
        return 0;
    for (++instruction; instruction < mir.count; ++instruction) {
        int opcode = mir.insns[instruction].opcode;
        if (opcode == MIR_LABEL)
            return 1;
        if (opcode == MIR_NOP)
            continue;
        if (opcode == MIR_JUMP)
            return 1;
        return 0;
    }
    return 1;
}

static int mir_boolean_constant_edge_is_transparent(
    int definition_index, int predecessor_label)
{
    int instruction;

    if (mir_block_label_before(definition_index) != predecessor_label)
        return 0;
    for (instruction = definition_index + 1;
         instruction < mir.count; ++instruction) {
        int opcode = mir.insns[instruction].opcode;

        if (opcode == MIR_LABEL || opcode == MIR_JUMP)
            return 1;
        if (opcode != MIR_NOP)
            return 0;
    }
    return 1;
}

static int mir_collect_boolean_phi_chain(
    int value, int predecessor_label, unsigned char *actions,
    int *definition_indices, int depth)
{
    const struct MirInsn *definition;
    int definition_index;

    if (value < 0 || value >= mir.next_value || depth > mir.next_value)
        return 0;
    if (actions[value] != 0)
        return actions[value] != 4;
    definition = mir_definition(value);
    if (definition == NULL || mir_value_use_count(value) != 1)
        return 0;
    definition_index = (int)(definition - mir.insns);
    definition_indices[value] = definition_index;
    actions[value] = 4;
    if (definition->opcode == MIR_CONST) {
        if (type_size(definition->type) > 2 ||
            (definition->immediate != 0 && definition->immediate != 1) ||
            !mir_boolean_constant_edge_is_transparent(
                definition_index, predecessor_label))
            return 0;
        actions[value] = definition->immediate != 0 ? 2 : 3;
        return 1;
    }
    if (definition->opcode != MIR_PHI || type_size(definition->type) > 2 ||
        (predecessor_label >= 0 &&
         !mir_boolean_phi_predecessor_is_transparent(predecessor_label)) ||
        !mir_collect_boolean_phi_chain(
            definition->src1, definition->phi_pred1, actions,
            definition_indices, depth + 1) ||
        !mir_collect_boolean_phi_chain(
            definition->src2, definition->phi_pred2, actions,
            definition_indices, depth + 1))
        return 0;
    actions[value] = 1;
    return 1;
}

static void mir_make_nop(struct MirInsn *insn)
{
    insn->opcode = MIR_NOP;
    insn->dst = -1;
    insn->src1 = -1;
    insn->src2 = -1;
    insn->label = -1;
    insn->successor_count = 0;
}

static int mir_boolean_phi_branch_simplifications;

int mir_boolean_phi_branch_simplification_count(void)
{
    return mir_boolean_phi_branch_simplifications;
}

void mir_reset_boolean_phi_branch_simplification_count(void)
{
    mir_boolean_phi_branch_simplifications = 0;
}

static int mir_collect_phi_forward_predecessors(int successor,
                                                int *pred0, int *pred1)
{
    int instruction;
    int count = 0;

    *pred0 = -1;
    *pred1 = -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        int successor_index;

        for (successor_index = 0;
             successor_index < insn->successor_count;
             ++successor_index) {
            if (insn->successors[successor_index] != successor)
                continue;
            if (count == 0)
                *pred0 = instruction;
            else if (count == 1)
                *pred1 = instruction;
            else
                return 0;
            ++count;
            break;
        }
    }
    return count == 2;
}

static int mir_find_phi_forward_terminal(int phi_instruction,
                                         int *consumer_instruction,
                                         int *terminal_instruction)
{
    const struct MirInsn *phi;
    int instruction;

    if (phi_instruction < 0 || phi_instruction >= mir.count)
        return 0;
    phi = &mir.insns[phi_instruction];
    if (phi->opcode != MIR_PHI || type_size(phi->type) > 2 ||
        type_is_float(phi->type) || type_is_struct_object(phi->type) ||
        mir_value_use_count(phi->dst) != 1)
        return 0;
    instruction = phi_instruction + 1;
    while (instruction < mir.count &&
           mir.insns[instruction].opcode == MIR_NOP)
        ++instruction;
    if (instruction >= mir.count)
        return 0;
    if (mir.insns[instruction].opcode == MIR_RETURN &&
        mir.insns[instruction].src1 == phi->dst &&
        !type_is_struct_object(mir.return_type)) {
        *consumer_instruction = -1;
        *terminal_instruction = instruction;
        return 1;
    }
    if ((mir.insns[instruction].opcode == MIR_UNARY ||
         mir.insns[instruction].opcode == MIR_BINARY) &&
        type_size(mir.insns[instruction].type) <= 2 &&
        !type_is_float(mir.insns[instruction].type) &&
        !type_is_struct_object(mir.insns[instruction].type) &&
        (mir.insns[instruction].src1 == phi->dst ||
         mir.insns[instruction].src2 == phi->dst) &&
        mir.insns[instruction].dst >= 0 &&
        mir_value_use_count(mir.insns[instruction].dst) == 1) {
        int terminal = instruction + 1;

        while (terminal < mir.count &&
               mir.insns[terminal].opcode == MIR_NOP)
            ++terminal;
        if (terminal < mir.count &&
            mir.insns[terminal].opcode == MIR_RETURN &&
            mir.insns[terminal].src1 == mir.insns[instruction].dst &&
            !type_is_struct_object(mir.return_type)) {
            *consumer_instruction = instruction;
            *terminal_instruction = terminal;
            return 1;
        }
    }
    return 0;
}

static int mir_phi_forward_source_for_predecessor(const struct MirInsn *phi,
                                                  int predecessor)
{
    int predecessor_label = mir_block_label_before(predecessor);

    if (predecessor_label == phi->phi_pred1)
        return phi->src1;
    if (predecessor_label == phi->phi_pred2)
        return phi->src2;
    return -1;
}

static void mir_init_phi_forward_return(struct MirInsn *insn,
                                        int source_value,
                                        int terminal_type)
{
    mir_make_nop(insn);
    insn->opcode = MIR_RETURN;
    insn->src1 = source_value;
    insn->type = terminal_type;
}

static void mir_init_phi_forward_consumer(struct MirInsn *insn,
                                          int source_value,
                                          int phi_value,
                                          const struct MirInsn *consumer_template)
{
    *insn = *consumer_template;
    insn->dst = mir_new_value();
    if (insn->src1 == phi_value)
        insn->src1 = source_value;
    if (insn->src2 == phi_value)
        insn->src2 = source_value;
    insn->successor_count = 0;
}

static void mir_insert_phi_forward_return_before(int index,
                                                 int source_value,
                                                 int phi_value,
                                                 const struct MirInsn *consumer,
                                                 int terminal_type)
{
    if (consumer != NULL) {
        struct MirInsn *consumer_insn =
            mir_insert_instruction_before(
                index, consumer->opcode);
        struct MirInsn *ret;

        if (consumer_insn == NULL)
            fatal("cannot insert MIR phi-forward consumer");
        mir_init_phi_forward_consumer(consumer_insn, source_value, phi_value,
                                      consumer);
        ret = mir_insert_instruction_before(index + 1, MIR_RETURN);
        if (ret == NULL)
            fatal("cannot insert MIR phi-forward return");
        mir_init_phi_forward_return(ret, consumer_insn->dst, terminal_type);
    } else {
        struct MirInsn *ret = mir_insert_instruction_before(index, MIR_RETURN);

        if (ret == NULL)
            fatal("cannot insert MIR phi-forward return");
        mir_init_phi_forward_return(ret, source_value, terminal_type);
    }
}

static int mir_forward_single_phi_return_join(int successor)
{
    const struct MirInsn *phi;
    int phi_instruction;
    int consumer_instruction;
    int terminal_instruction;
    int predecessor0;
    int predecessor1;
    int label_predecessor;
    int explicit_predecessor;
    int source0;
    int source1;
    struct MirInsn consumer_copy;
    const struct MirInsn *consumer_template = NULL;
    int terminal_type;

    if (successor < 0 || successor >= mir.count ||
        mir.insns[successor].opcode != MIR_LABEL ||
        !mir_collect_phi_forward_predecessors(
            successor, &predecessor0, &predecessor1))
        return 0;
    if (mir.insns[predecessor0].opcode == MIR_LABEL &&
        predecessor0 + 1 == successor) {
        label_predecessor = predecessor0;
        explicit_predecessor = predecessor1;
    } else if (mir.insns[predecessor1].opcode == MIR_LABEL &&
               predecessor1 + 1 == successor) {
        label_predecessor = predecessor1;
        explicit_predecessor = predecessor0;
    } else {
        return 0;
    }
    if (mir.insns[explicit_predecessor].opcode != MIR_JUMP &&
        (mir.insns[explicit_predecessor].opcode != MIR_BRANCH_FALSE ||
         mir_find_label(mir.insns[explicit_predecessor].label) != successor))
        return 0;
    phi_instruction = mir_first_phi_or_block_end(successor);
    if (phi_instruction < 0 || phi_instruction >= mir.count ||
        mir.insns[phi_instruction].opcode != MIR_PHI)
        return 0;
    if (phi_instruction + 1 < mir.count &&
        mir.insns[phi_instruction + 1].opcode == MIR_PHI)
        return 0;
    if (!mir_find_phi_forward_terminal(phi_instruction, &consumer_instruction,
                                       &terminal_instruction))
        return 0;
    phi = &mir.insns[phi_instruction];
    source0 = mir_phi_forward_source_for_predecessor(phi, explicit_predecessor);
    source1 = mir_phi_forward_source_for_predecessor(phi, label_predecessor);
    if (source0 < 0 || source1 < 0)
        return 0;
    terminal_type = mir.insns[terminal_instruction].type;
    if (consumer_instruction >= 0) {
        consumer_copy = mir.insns[consumer_instruction];
        consumer_template = &consumer_copy;
    }
    mir_make_nop(&mir.insns[phi_instruction]);
    if (consumer_instruction >= 0)
        mir_make_nop(&mir.insns[consumer_instruction]);
    mir_make_nop(&mir.insns[terminal_instruction]);
    mir_insert_phi_forward_return_before(successor, source1, phi->dst,
                                         consumer_template, terminal_type);
    if (consumer_template != NULL) {
        struct MirInsn *explicit_consumer = &mir.insns[explicit_predecessor];
        struct MirInsn *ret;

        mir_init_phi_forward_consumer(explicit_consumer, source0, phi->dst,
                                      consumer_template);
        ret = mir_insert_instruction_before(explicit_predecessor + 1,
                                            MIR_RETURN);
        if (ret == NULL)
            fatal("cannot insert MIR phi-forward return");
        mir_init_phi_forward_return(ret, explicit_consumer->dst,
                                    terminal_type);
    } else {
        mir_init_phi_forward_return(&mir.insns[explicit_predecessor], source0,
                                    terminal_type);
    }
    return 1;
}

static int mir_phi_return_forwarding_count;

int mir_phi_return_forwarding_count_value(void)
{
    return mir_phi_return_forwarding_count;
}

void mir_reset_phi_return_forwarding_count(void)
{
    mir_phi_return_forwarding_count = 0;
}

void mir_forward_immediate_phi_returns(void)
{
    int changed;

    do {
        int successor;

        changed = 0;
        for (successor = 0; successor < mir.count; ++successor) {
            if (!mir_forward_single_phi_return_join(successor))
                continue;
            ++mir_phi_return_forwarding_count;
            changed = 1;
            break;
        }
    } while (changed);
}

void mir_simplify_boolean_phi_branches(void)
{
    unsigned char *actions;
    int *definition_indices;
    int branch_index;

    /* Short-circuit lowering can build a tree of one-use 0/1 constants and
     * PHIs solely to feed one false branch. Redirect each proven-transparent
     * false edge to the branch target and let true edges fall through. */
    mir_boolean_phi_branch_simplifications = 0;
    if (mir.next_value <= 0)
        return;
    actions = (unsigned char *)calloc((size_t)mir.next_value, 1);
    definition_indices =
        (int *)malloc((size_t)mir.next_value * sizeof(*definition_indices));
    if (actions == NULL || definition_indices == NULL)
        fatal("out of memory simplifying MIR boolean phis");
    for (branch_index = 0; branch_index < mir.count; ++branch_index) {
        struct MirInsn *branch = &mir.insns[branch_index];
        int value;

        if (branch->opcode != MIR_BRANCH_FALSE)
            continue;
        memset(actions, 0, (size_t)mir.next_value);
        if (!mir_collect_boolean_phi_chain(
                branch->src1, -1, actions, definition_indices, 0) ||
            actions[branch->src1] != 1)
            continue;
        for (value = 0; value < mir.next_value; ++value) {
            struct MirInsn *definition;

            if (actions[value] == 0 || actions[value] == 4)
                continue;
            definition = &mir.insns[definition_indices[value]];
            if (actions[value] == 3) {
                mir_make_nop(definition);
                definition->opcode = MIR_JUMP;
                definition->label = branch->label;
            } else {
                mir_make_nop(definition);
            }
        }
        mir_make_nop(branch);
        ++mir_boolean_phi_branch_simplifications;
    }
    free(definition_indices);
    free(actions);
}

static int mir_unary_is_representation_identity(
    const struct MirInsn *insn, const struct MirInsn *source)
{
    int instruction;
    int source_size;
    int target_size;

    if (insn == NULL || source == NULL ||
        insn->opcode != MIR_UNARY || insn->immediate != 0 ||
        type_is_struct_object(source->type) ||
        type_is_struct_object(insn->type))
        return 0;
    source_size = type_size(source->type);
    target_size = type_size(insn->type);
    if (source->type == insn->type)
        return source_size == 1 || source_size == 2 || source_size == 4;
    if (source_size != target_size ||
        (source_size != 2 && source_size != 4) ||
        type_is_float(source->type) || type_is_float(insn->type))
        return 0;
    /*
     * Equal-width signed/unsigned casts preserve bits, but a later widening
     * must still see the cast's type to choose sign versus zero extension.
     * Other consumers already carry their effective computation/memory type
     * and can safely use the original representation directly.
     */
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_UNARY &&
            mir.insns[instruction].immediate == 0 &&
            mir.insns[instruction].src1 == insn->dst &&
            type_size(mir.insns[instruction].type) > target_size)
            return 0;
    return 1;
}

void mir_resolve_deferred_metadata(void)
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
        /* A C99 for-init alias can retarget a merge emitted while the
         * shadowed outer object's type was visible. */
        if (insn->opcode == MIR_LOAD &&
            mir_declared_is_array_object(insn->name)) {
            insn->opcode = MIR_ADDRESS;
            insn->type = type_add_ptr(named_type);
        } else if (insn->opcode == MIR_LOAD ||
                   insn->opcode == MIR_PARAM ||
                   insn->opcode == MIR_OBJECT_MERGE)
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
                int stride = 0;
                if ((mir.insns[i].memory_flags &
                     MIR_MEMORY_FLAG_DEREFERENCED_POINTER_ARRAY) != 0)
                    stride = type_index_elem_size(base->type);
                else if (named_base && base->name[0] != 0 &&
                         !mir_declared_has_dynamic_stride(base->name))
                    stride = mir_declared_elem_size(base->name);
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
        /* TYPE_PTR2 is the deepest representable pointer type, so taking
         * the address of a pointer-to-pointer saturates instead of creating
         * a third pointer level.  Do not let repair infer a shallower load
         * type from that saturated address and lose the original pointee
         * depth; later index scaling needs the preserved pointer-to-pointer
         * type to select a two-byte pointer-element stride. */
        if (type_ptr_depth(address->type) == 2 &&
            type_ptr_depth(insn->type) >
                type_ptr_depth(pointee_type))
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
        if (!mir_unary_is_representation_identity(insn, source))
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
        unsigned long mask;
        int operand_bytes = type_size(insn->type);
        /* Item T75 (mir-text-size-plan.md): widened from 2-byte-only to
         * also fold 4-byte (long/wide) unary constant operations, using
         * the same in-place-to-MIR_CONST rewrite and orphan-retirement
         * as the narrow case immediately below. Before this, a wide
         * negative literal such as `-80000L` lowered as a MIR_CONST of
         * the positive magnitude (80000) followed by a MIR_UNARY '-',
         * which never got folded here (the size check rejected every
         * 4-byte type) and so reached emission as a genuine runtime
         * 32-bit two's-complement negation (four `cpl` + a 32-bit
         * increment) instead of the pre-folded constant legacy always
         * emits directly. Floats are explicitly excluded even though
         * they are also 4 bytes on this target: sign negation of an
         * IEEE-754-style representation is not the same operation as
         * two's-complement bit negation, and this fold's bit-mask
         * arithmetic below is only valid for integer representations.
         * Found via tests/tlngfptr.c's main (`(*table_call)(-80000L,
         * 7)`), newly MIR-reachable once Item T74's base-address
         * forwarding fix unlocked the whole function. */
        if (insn->opcode != MIR_UNARY || insn->src1 < 0 ||
            (operand_bytes != 1 && operand_bytes != 2 &&
             operand_bytes != 4) ||
            type_is_float(insn->type))
            continue;
        source = mir_mutable_definition(insn->src1);
        if (source == NULL || source->opcode != MIR_CONST ||
            type_is_float(source->type))
            continue;
        if (type_is_bool(insn->type)) {
            if (insn->immediate != 0)
                continue;
            bits = source->immediate != 0;
        } else {
            mask = operand_bytes == 4 ? 0xffffffffUL :
                   operand_bytes == 2 ? 0xffffUL : 0xffUL;
            if (operand_bytes == 4 && type_size(source->type) != 4) {
                int source_bytes = type_size(source->type);
                unsigned long source_mask;
                unsigned long sign_bit;

                if (insn->immediate != 0 ||
                    (source_bytes != 1 && source_bytes != 2))
                    continue;
                source_mask = source_bytes == 1 ? 0xffUL : 0xffffUL;
                sign_bit = source_bytes == 1 ? 0x80UL : 0x8000UL;
                bits = (unsigned long)source->immediate & source_mask;
                if ((source->type & TYPE_UNSIGNED) == 0 &&
                    type_ptr_depth(source->type) == 0 &&
                    (bits & sign_bit) != 0)
                    bits |= ~source_mask;
                bits &= mask;
            } else {
                bits = (unsigned long)source->immediate & mask;
            }
            if (insn->immediate == '-')
                bits = (0UL - bits) & mask;
            else if (insn->immediate == '~')
                bits = (~bits) & mask;
            else if (insn->immediate == '!')
                bits = bits == 0;
            else if (insn->immediate != 0 && insn->immediate != '+')
                continue;
            if (operand_bytes == 1 &&
                (insn->type & TYPE_UNSIGNED) == 0 &&
                type_ptr_depth(insn->type) == 0 &&
                (bits & 0x80UL) != 0)
                bits |= ~0xffUL;
        }
        if (operand_bytes == 1 ||
            (operand_bytes == 4 && type_size(source->type) != 4))
            ++mir_extended_integer_constant_conversion_fold_count;
        {
            /* Item T50 (mir-text-size-plan.md): folding this MIR_UNARY
             * in place into a plain MIR_CONST orphans its operand
             * (`source`, e.g. the un-negated magnitude of a negative
             * literal like the 50 in `-50`) once insn->src1 below is
             * cleared - unlike the other constant-fold site in
             * mir_lower_expr (which explicitly retires an operand to
             * MIR_NOP once its use count reaches zero), this loop left
             * the orphan as a live-looking MIR_CONST with its own
             * assigned home register. At least the spilled-scalar-cfg
             * and homed-scalar-cfg selectors materialize *every*
             * MIR_CONST unconditionally (they have no separate
             * liveness check for constants, since one is normally
             * unnecessary - dead values are supposed to never reach
             * emission at all), so the orphan's "ld hl,<magnitude>"
             * was emitted for real, momentarily clobbering whatever
             * value the selector had just placed in that register (most
             * visibly, HL right before mir_emit_homed_binary_instruction's
             * biased_right_constant path or the spilled-cfg comparison
             * path performed their sbc) - a genuine, already-shipped
             * silent-wrong-result bug for any signed comparison against
             * a negative compile-time constant reached through either
             * selector, found via a synthetic ntvcm-executed regression
             * test while investigating Item T50's biased-comparison
             * extension. Retiring the orphan here, mirroring the
             * existing mir_lower_expr precedent exactly, removes it
             * from emission consideration entirely. */
            int operand = insn->src1;
            insn->opcode = MIR_CONST;
            insn->src1 = -1;
            insn->src2 = -1;
            insn->immediate = (long)bits;
            if (mir_value_use_count(operand) == 0) {
                source->opcode = MIR_NOP;
                source->dst = -1;
            }
        }
    }
    /*
     * Cast/conversion repair above can turn both operands of an already-built
     * wide binary into MIR_CONST only after mir_lower_expr's eager constant
     * fold has passed. Fold the measured fixed-point scale class now so
     * selectors see the same single constant legacy AST emission sees for
     * expressions such as `(long)32767 * 256L`; other deferred expressions
     * retain their established output until separately profiled.
     */
    for (i = 0; i < mir.count; ++i) {
        struct MirInsn *insn = &mir.insns[i];
        struct MirInsn *left;
        struct MirInsn *right;
        long folded;
        int left_value;
        int right_value;

        if (insn->opcode != MIR_BINARY ||
            insn->immediate != '*' ||
            type_size(insn->secondary_offset) != 4 ||
            type_is_float(insn->secondary_offset) ||
            insn->src1 < 0 || insn->src2 < 0)
            continue;
        left = mir_mutable_definition(insn->src1);
        right = mir_mutable_definition(insn->src2);
        if (left == NULL || right == NULL ||
            left->opcode != MIR_CONST || right->opcode != MIR_CONST)
            continue;
        if (left->immediate != 256 && right->immediate != 256)
            continue;
        if (!mir_fold_constant_binary(
                insn->immediate, left->immediate,
                right->immediate, insn->secondary_offset, &folded))
            continue;
        left_value = insn->src1;
        right_value = insn->src2;
        insn->opcode = MIR_CONST;
        insn->src1 = -1;
        insn->src2 = -1;
        insn->immediate = folded;
        insn->secondary_offset = 0;
        insn->memory_flags |= MIR_MEMORY_FLAG_DEFERRED_WIDE_CONST;
        if (mir_value_use_count(left_value) == 0) {
            left->opcode = MIR_NOP;
            left->dst = -1;
        }
        if (right_value != left_value &&
            mir_value_use_count(right_value) == 0) {
            right->opcode = MIR_NOP;
            right->dst = -1;
        }
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

int mir_extended_integer_constant_conversion_folds(void)
{
    return mir_extended_integer_constant_conversion_fold_count;
}

int mir_find_label(int label)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].opcode == MIR_LABEL && mir.insns[i].label == label)
            return i;
    return -1;
}

/* Item T61 (mir-text-size-plan.md): every scalar-cfg backend emits one
 * MIR_LABEL per basic-block boundary unconditionally, whether or not the
 * block is ever entered by anything other than straight-line fallthrough
 * from its predecessor. A block whose only "predecessor" is the previous
 * instruction never needs a printed `Lnn:` line at all - nothing ever
 * jumps to it - yet the label text ("Lnn:\n") still counts toward the
 * generated-bytes text-size metric used to decide MIR acceptance
 * (mir_stream_size measures the literal generated assembly-text stream
 * length, not real assembled machine bytes; a label emits zero real
 * bytes either way). Found via tests/tbug.c's chk(): the MIR-emitted
 * form had two such labels (the function's very first block, entered
 * only by falling out of the prologue, and the true-branch's entry
 * block, entered only by falling through a MIR_BRANCH_FALSE that jumps
 * away on the false path) versus legacy's single unavoidable one,
 * accounting for the whole 5-byte generated/captured gap. Only
 * MIR_JUMP and MIR_BRANCH_FALSE ever cause a textual jump to a label by
 * name; MIR_PHI's phi_pred1/phi_pred2 fields only identify which
 * predecessor's value to select at a merge point and never require the
 * predecessor block itself to have a printed label. */
int mir_label_is_jump_target(int label)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if ((mir.insns[i].opcode == MIR_JUMP ||
             mir.insns[i].opcode == MIR_BRANCH_FALSE) &&
            mir.insns[i].label == label)
            return 1;
    return 0;
}

/* Item T62 (mir-text-size-plan.md), a direct follow-up to T61: a
 * MIR_JUMP whose only "predecessor in program order" - after skipping
 * over any immediately-preceding dead labels already elided by T61 -
 * is itself an unconditional MIR_JUMP or MIR_RETURN, is unreachable:
 * nothing can ever fall through to it (the prior unconditional
 * transfer never falls through) and nothing branches to it directly
 * (any label that could have been a branch target was already found
 * live and would have stopped the backward scan). Found via
 * tests/tbug.c's swdf() and tests/tc99scpe.c's switch_body_decl(): a
 * sequential if-else-if chain's final comparison emits `jump
 * <default-case-label>` immediately after its own true-branch's `jump
 * <case-label>`, guarded only by a now-elided dead label - MIR's own
 * liveness annotations already show `live in=0 out=0` for both the
 * dead label and this jump, confirming zero value dependency crosses
 * this point either way. Only used to gate MIR_JUMP emission for now;
 * generalizing to every opcode is deferred until corpus evidence shows
 * a non-jump instruction in this position.
 *
 * The backward scan also skips MIR_NOP, the same way (and for the same
 * reason) mir_thread_jumps()'s Item 36 chain-walk already does: user-
 * named goto labels get an MIR_NOP carrying the source name immediately
 * after the MIR_LABEL for diagnostics, and MIR_NOP never emits any code
 * (dcc_mir.c's emitter simply `break`s on it). Found via
 * tests/tgoto.c's gt_switch(): a `switch` with an explicit `case: goto`
 * and a fall-through `goto done` after the switch body lowers to three
 * separate `label(dead)/nop("done")/jump L10` groups back to back: only
 * the first jump is real, but without also skipping the intervening
 * MIR_NOP the scan stopped one instruction short of the prior jump and
 * treated the second and third copies as reachable. */
int mir_insn_is_reachable(int i)
{
    int j = i - 1;

    for (;;) {
        if (j >= 0 && mir.insns[j].opcode == MIR_NOP) {
            --j;
            continue;
        }
        if (j >= 0 && mir.insns[j].opcode == MIR_LABEL &&
            !mir_label_is_jump_target(mir.insns[j].label)) {
            --j;
            continue;
        }
        break;
    }
    if (j < 0)
        return 1;
    return mir.insns[j].opcode != MIR_JUMP && mir.insns[j].opcode != MIR_RETURN;
}

int mir_block_label_before(int instruction)
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
int mir_first_nonlabel_successor(int successor)
{
    while (successor >= 0 && successor < mir.count &&
           (mir.insns[successor].opcode == MIR_LABEL ||
            mir.insns[successor].opcode == MIR_NOP))
        ++successor;
    return successor;
}

/* Item T65 (mir-migration): every phi-copy-collection site used to locate
 * a merge block's phi node(s) via mir_first_nonlabel_successor, which
 * only skips MIR_LABEL/MIR_NOP - implicitly assuming a phi is always the
 * very first "real" instruction of its block (the usual SSA-form
 * placement). That assumption does not hold for every shape this front
 * end lowers: a statement whose own value does not depend on which
 * predecessor edge was taken (e.g. an unrelated function call scheduled
 * before a merged local's use - a recursive-descent parser's `term();
 * emit(op, ...);`, where `op` was assigned differently in each branch of
 * an earlier if/else chain) can be emitted before the phi in program
 * order, even though the phi still logically belongs to the top of the
 * merged block. Every caller that stopped at that leading instruction
 * instead of the phi silently treated the edge as phi-free, skipping
 * phi-resolution copy insertion entirely and leaving the phi's
 * destination reading an uninitialized backend slot - a real, confirmed
 * miscompilation (tests/bint.c's sum(), tests/adaint.c's add_expr(),
 * both exactly this "for (;;) { if (...) op = A; else if (...) op = B;
 * else break; ...; use(op); }" shape). This helper keeps scanning past
 * ordinary non-branching instructions to find the phi if one exists
 * anywhere before the block truly ends (a jump, branch, return, or the
 * end of the instruction stream) - callers that only care whether a phi
 * exists on this edge, or need its exact position to insert copies, both
 * get the correct answer regardless of what was scheduled before it.
 *
 * A later label after a substantive instruction starts another basic block
 * and must stop the scan. Without that boundary, an edge entering an
 * intermediate labeled block can incorrectly inherit a phi from a later
 * block and copy that phi's source before the intermediate block defines it.
 * Leading/consecutive labels and NOP metadata still belong to the same entry
 * position and may be skipped safely. */
int mir_first_phi_or_block_end(int successor)
{
    int saw_instruction = 0;

    while (successor >= 0 && successor < mir.count) {
        int opcode = mir.insns[successor].opcode;
        if (opcode == MIR_PHI || opcode == MIR_JUMP ||
            opcode == MIR_BRANCH_FALSE || opcode == MIR_RETURN)
            return successor;
        if (opcode == MIR_LABEL) {
            if (saw_instruction)
                return successor;
        } else if (opcode != MIR_NOP) {
            saw_instruction = 1;
        }
        ++successor;
    }
    return successor;
}

static int mir_phi_edge_uses_value(int predecessor, int successor, int value)
{
    int first = mir_first_phi_or_block_end(successor);
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

int mir_call_uses_value(const struct MirInsn *call, int value)
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

int mir_value_has_use(int value)
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

int mir_value_has_use_after(int value, int instruction)
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

/* True if a value occupies `color` on both sides of this instruction.
 * Prefer the verifier's CFG-aware liveness so mutually exclusive branch
 * values do not cause unnecessary saves; the textual scan is retained for
 * callers that run before verification has persisted those matrices. */
int mir_home_color_live_across(int instruction, int color)
{
    int value;

    if (instruction >= 0 && instruction < mir.count &&
        mir.live_in != NULL && mir.live_out != NULL) {
        size_t row = (size_t)instruction * mir.next_value;

        for (value = 0; value < mir.next_value; ++value)
            if (mir.allocation_colors[value] == color &&
                mir.live_in[row + value] != 0 &&
                mir.live_out[row + value] != 0)
                return 1;
        return 0;
    }

    for (value = 0; value < mir.next_value; ++value) {
        const struct MirInsn *definition;
        int def_index;

        if (mir.allocation_colors[value] != color)
            continue;
        definition = mir_definition(value);
        if (definition == NULL)
            continue;
        def_index = (int)(definition - mir.insns);
        if (def_index >= instruction)
            continue;
        if (mir_value_has_use_after(value, instruction))
            return 1;
    }
    return 0;
}

int mir_value_use_count(int value)
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
                                   const int *out_state,
                                   const unsigned char *reachable)
{
    int block_start = mir_block_start_for_instruction(instruction);
    int predecessor_values[2];
    int predecessor_labels[2];
    int predecessor_count = 0;
    int predecessor;
    struct MirInsn *load;
    int phi_report = getenv("DCC_MIR_PHI_REPORT") != NULL;

    /* Phi association uses labels, so decline unlabeled fallthrough blocks
     * rather than inventing an imprecise edge identity. */
    if (block_start < 0 || block_start >= mir.count ||
        mir.insns[block_start].opcode != MIR_LABEL) {
        if (phi_report)
            fprintf(stderr,
                    "; MIR phi miss function=%s insn=%d object=%s "
                    "reason=unlabeled-block block-start=%d\n",
                    mir.name, instruction, mir.objects[object].name,
                    block_start);
        return 0;
    }
    for (predecessor = 0; predecessor < mir.count; ++predecessor) {
        int successor;
        if (!reachable[predecessor])
            continue;
        for (successor = 0;
             successor < mir.insns[predecessor].successor_count;
             ++successor) {
            int value;
            int label;
            if (mir.insns[predecessor].successors[successor] != block_start)
                continue;
            if (predecessor_count >= 2) {
                if (phi_report)
                    fprintf(stderr,
                            "; MIR phi miss function=%s insn=%d object=%s "
                            "reason=too-many-predecessors\n",
                            mir.name, instruction, mir.objects[object].name);
                return 0;
            }
            value = out_state[(size_t)predecessor * mir.object_count + object];
            label = mir_block_label_before(predecessor);
            if (value < 0 || label < 0) {
                if (phi_report)
                    fprintf(stderr,
                            "; MIR phi miss function=%s insn=%d object=%s "
                            "reason=predecessor-unresolved predecessor=%d "
                            "value=%d label=%d\n",
                            mir.name, instruction, mir.objects[object].name,
                            predecessor, value, label);
                return 0;
            }
            predecessor_values[predecessor_count] = value;
            predecessor_labels[predecessor_count] = label;
            ++predecessor_count;
            break;
        }
    }
    if (predecessor_count != 2) {
        if (phi_report)
            fprintf(stderr,
                    "; MIR phi miss function=%s insn=%d object=%s "
                    "reason=insufficient-predecessors count=%d\n",
                    mir.name, instruction, mir.objects[object].name,
                    predecessor_count);
        return 0;
    }
    if (predecessor_values[0] == predecessor_values[1]) {
        if (phi_report)
            fprintf(stderr,
                    "; MIR phi miss function=%s insn=%d object=%s "
                    "reason=already-identical value=%d\n",
                    mir.name, instruction, mir.objects[object].name,
                    predecessor_values[0]);
        return 0;
    }
    load = &mir.insns[instruction];
    load->opcode = MIR_PHI;
    load->src1 = predecessor_values[0];
    load->src2 = predecessor_values[1];
    load->phi_pred1 = predecessor_labels[0];
    load->phi_pred2 = predecessor_labels[1];
    load->object = object;
    if (phi_report)
        fprintf(stderr,
                "; MIR phi hit function=%s insn=%d object=%s dst=%d "
                "pred1=v%d@L%d pred2=v%d@L%d\n",
                mir.name, instruction, mir.objects[object].name, load->dst,
                predecessor_values[0], predecessor_labels[0],
                predecessor_values[1], predecessor_labels[1]);
    return 1;
}

static int mir_is_profiled_boolean_reachable_join(void)
{
    int calls = 0;
    int instruction;

    if (mir_boolean_phi_branch_simplification_count() <= 0 ||
        (mir.return_type & 15) != TYPE_INT ||
        mir_cfg_block_count() != 14 || mir.local_bytes != 2)
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_CALL ||
            mir.insns[instruction].opcode == MIR_CALL_AGGREGATE)
            ++calls;
    return calls == 10;
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
    unsigned char *reachable;
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
    reachable = (unsigned char *)calloc((size_t)mir.count, 1);
    if (in_state == NULL || out_state == NULL || next_state == NULL ||
        aliases == NULL || reachable == NULL)
        fatal("out of memory promoting MIR objects");
    if (mir_is_profiled_boolean_reachable_join()) {
        reachable[0] = 1;
        do {
            changed = 0;
            for (i = 0; i < mir.count; ++i) {
                int successor;
                if (!reachable[i])
                    continue;
                for (successor = 0;
                     successor < mir.insns[i].successor_count;
                     ++successor) {
                    int target = mir.insns[i].successors[successor];
                    if (target >= 0 && target < mir.count &&
                        !reachable[target]) {
                        reachable[target] = 1;
                        changed = 1;
                    }
                }
            }
        } while (changed);
    } else {
        memset(reachable, 1, (size_t)mir.count);
    }
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

            if (!reachable[i])
                continue;
            if (i > 0) {
                int predecessor_count = 0;
                int predecessor;
                for (predecessor = 0; predecessor < mir.count; ++predecessor) {
                    int successor;
                    if (!reachable[predecessor])
                        continue;
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
            mir_try_make_object_phi(
                i, insn->object, out_state, reachable)) {
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
                mir_try_make_object_phi(
                    i, insn->object, out_state, reachable)) {
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
    free(reachable);
    free(next_state);
    free(out_state);
    free(in_state);
    /* Negative encoding asks the caller to rerun dataflow after the inserted
     * phi: -(N+1) preserves how many ordinary loads were already folded. */
    return inserted_phi ? -(promoted + 1) : promoted;
}

struct MirAllocationSummary {
    int colors[MIR_COLOR_COUNT]; /* HL, DE, BC, IY, (reserved) HL:DE, BC:IY */
    int spills;
    int cross_call_values;
    int opaque_crossing_values;
    int fixed_moves;
    int operand_moves;
    int phi_moves;
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
    if (mir_lazy_allocation_active &&
        (mir_is_lazy_parameter(left) || mir_is_lazy_parameter(right)))
        return 0;
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
static int mir_color_shares_slot(int left, int right)
{
    /* Reserved wide pair-colors (mir-migration-plan-to-100pct.md Item 20)
     * occupy two adjacent single-register slots simultaneously; every
     * other color occupies exactly its own slot. This generalizes the
     * old `left == right` check to slot-overlap so a scalar value can
     * never share a physical register with a wide value's pair, while
     * reducing to the exact old behavior when neither side is a wide
     * color (each singleton footprint only overlaps itself). */
    if (left == right)
        return 1;
    if (left == MIR_COLOR_HL_DE || right == MIR_COLOR_HL_DE) {
        int other = (left == MIR_COLOR_HL_DE) ? right : left;
        if (other == MIR_COLOR_HL || other == MIR_COLOR_DE)
            return 1;
    }
    if (left == MIR_COLOR_BC_IY || right == MIR_COLOR_BC_IY) {
        int other = (left == MIR_COLOR_BC_IY) ? right : left;
        if (other == MIR_COLOR_BC || other == MIR_COLOR_IY)
            return 1;
    }
    return 0;
}

static int mir_instruction_clobbers_caller_registers(
    const struct MirInsn *insn)
{
    return insn != NULL &&
           (insn->opcode == MIR_CALL ||
            insn->opcode == MIR_CALL_AGGREGATE ||
            (insn->opcode == MIR_BINARY &&
             (insn->immediate == '/' || insn->immediate == '%')));
}

int mir_iy_home_live_across_caller_clobber(void)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_instruction_clobbers_caller_registers(
                &mir.insns[instruction]) &&
            mir_home_color_live_across(instruction, MIR_COLOR_IY))
            return 1;
    return 0;
}

static void mir_allocate_registers(const unsigned char *live_in,
                                   const unsigned char *live_out,
                                   struct MirAllocationSummary *summary,
                                   int allow_wide_colors,
                                   const unsigned char *rematerializable,
                                   int prioritize_wide)
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
    mir.allocation_fixed_moves = 0;
    mir.allocation_operand_moves = 0;
    mir.allocation_phi_moves = 0;
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
        if (mir_instruction_clobbers_caller_registers(&mir.insns[i]) ||
            mir.insns[i].opcode == MIR_OPAQUE) {
            for (value = 0; value < value_count; ++value) {
                if (!in[value] || !out[value])
                    continue;
                if (mir_instruction_clobbers_caller_registers(&mir.insns[i]))
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
            const struct MirInsn *left_definition =
                prioritize_wide ? mir_definition(left) : NULL;
            const struct MirInsn *right_definition =
                prioritize_wide ? mir_definition(right) : NULL;
            int left_wide =
                left_definition != NULL &&
                type_size(left_definition->type) == 4;
            int right_wide =
                right_definition != NULL &&
                type_size(right_definition->type) == 4;
            if (cross_call[right] > cross_call[left] ||
                (cross_call[right] == cross_call[left] &&
                 (right_wide > left_wide ||
                  (right_wide == left_wide &&
                   degree[right] > degree[left]))))
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
        int is_wide = 0;
        int first_color;
        int last_color;
        int chosen;

        if (mir_is_lazy_parameter(value) ||
            (rematerializable != NULL && rematerializable[value]))
            continue;
        if (allow_wide_colors) {
            const struct MirInsn *definition = mir_definition(value);
            is_wide = definition != NULL && type_size(definition->type) == 4;
        }
        if (is_wide) {
            /* A wide value crossing a call cannot be safely held in a
             * single register pair today (no callee-saved wide home
             * exists) - force it to spill instead of attempting an
             * unsupported color, leaving first_color > last_color so the
             * candidate loop below finds nothing available. */
            if (cross_call[value]) {
                first_color = MIR_COLOR_COUNT;
                last_color = MIR_COLOR_COUNT - 1;
            } else {
                first_color = MIR_COLOR_HL_DE;
                last_color = MIR_COLOR_BC_IY;
            }
        } else {
            first_color = cross_call[value] ? MIR_COLOR_IY : MIR_COLOR_HL;
            last_color = cross_call[value] ? MIR_COLOR_IY : MIR_COLOR_IY;
        }

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
                    if (color[other] >= 0 &&
                        mir_color_shares_slot(color[other], candidate) &&
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

    mir.allocation_fixed_moves = summary->fixed_moves;
    mir.allocation_operand_moves = summary->operand_moves;
    mir.allocation_phi_moves = summary->phi_moves;

    free(color);
    free(preferences);
    free(fixed_color);
    free(order);
    free(degree);
    free(cross_opaque);
    free(cross_call);
    free(interference);
}

static int mir_lazy_parameter_semantic_use_count(int value)
{
    int count = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];

        if (insn->src1 == value && insn->opcode != MIR_ARG)
            ++count;
        if (insn->src2 == value)
            ++count;
        if (mir_call_uses_value(insn, value))
            ++count;
    }
    return count;
}

static int mir_lazy_parameter_eligible(const struct MirInsn *parameter)
{
    const struct MirObject *object;
    int instruction;

    if (parameter->opcode != MIR_PARAM || parameter->dst < 0 ||
        parameter->object < 0 || parameter->object >= mir.object_count ||
        mir.has_vla || type_ptr_depth(parameter->type) != 0 ||
        type_size(parameter->type) < 1 || type_size(parameter->type) > 4 ||
        type_is_struct_object(parameter->type) ||
        mir_lazy_parameter_semantic_use_count(parameter->dst) != 1)
        return 0;
    object = &mir.objects[parameter->object];
    if (object->storage != SC_PARAM || type_ptr_depth(object->type) != 0 ||
        type_size(object->type) < 1 || type_size(object->type) > 4 ||
        type_is_struct_object(object->type))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_STORE &&
            mir.insns[instruction].object == parameter->object)
            return 0;
    return 1;
}

int mir_is_lazy_parameter(int value)
{
    return mir_lazy_allocation_active && value >= 0 &&
        value < mir.next_value && mir.lazy_parameter_values != NULL &&
        mir.lazy_parameter_values[value] != 0;
}

int mir_has_lazy_parameters(void)
{
    return mir_lazy_parameter_count() != 0;
}

int mir_lazy_parameter_count(void)
{
    int count = 0;
    int value;

    if (!mir_lazy_allocation_active)
        return 0;
    for (value = 0; value < mir.next_value; ++value)
        if (mir_is_lazy_parameter(value))
            ++count;
    return count;
}

int mir_lazy_byte_parameter_count(void)
{
    int count = 0;
    int value;

    if (!mir_lazy_allocation_active)
        return 0;
    for (value = 0; value < mir.next_value; ++value) {
        int type;
        if (mir_lazy_parameter_offset(value, NULL, &type) &&
            type_size(type) == 1)
            ++count;
    }
    return count;
}

int mir_lazy_parameter_offset(int value, int *offset, int *type)
{
    const struct MirInsn *parameter;
    const struct MirObject *object;

    if (!mir_is_lazy_parameter(value))
        return 0;
    parameter = mir_definition(value);
    if (parameter == NULL || parameter->opcode != MIR_PARAM ||
        parameter->object < 0 || parameter->object >= mir.object_count)
        return 0;
    object = &mir.objects[parameter->object];
    if (offset != NULL)
        *offset = object->offset;
    if (type != NULL)
        *type = object->type;
    return 1;
}

int mir_begin_lazy_parameter_allocation(void)
{
    int value_count = mir.next_value;
    int instruction;
    int eligible_count = 0;
    struct MirAllocationSummary summary;

    if (mir_lazy_allocation_active || mir.live_in == NULL ||
        mir.live_out == NULL || value_count == 0)
        return 0;
    if (mir.lazy_parameter_capacity < value_count) {
        unsigned char *new_values = (unsigned char *)realloc(
            mir.lazy_parameter_values, (size_t)value_count);
        if (new_values == NULL)
            fatal("out of memory planning lazy MIR parameters");
        mir.lazy_parameter_values = new_values;
        mir.lazy_parameter_capacity = value_count;
    }
    memset(mir.lazy_parameter_values, 0, (size_t)value_count);
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *insn = &mir.insns[instruction];
        if (mir_lazy_parameter_eligible(insn)) {
            mir.lazy_parameter_values[insn->dst] = 1;
            ++eligible_count;
        }
    }
    if (eligible_count == 0)
        return 0;

    mir_lazy_saved_colors =
        (int *)malloc((size_t)value_count * sizeof(*mir_lazy_saved_colors));
    mir_lazy_saved_spills =
        (int *)malloc((size_t)value_count * sizeof(*mir_lazy_saved_spills));
    if (mir_lazy_saved_colors == NULL || mir_lazy_saved_spills == NULL)
        fatal("out of memory saving MIR allocation");
    memcpy(mir_lazy_saved_colors, mir.allocation_colors,
           (size_t)value_count * sizeof(*mir_lazy_saved_colors));
    memcpy(mir_lazy_saved_spills, mir.allocation_spills,
           (size_t)value_count * sizeof(*mir_lazy_saved_spills));
    mir_lazy_saved_spill_count = mir.allocation_spill_count;
    mir_lazy_saved_fixed_moves = mir.allocation_fixed_moves;
    mir_lazy_saved_operand_moves = mir.allocation_operand_moves;
    mir_lazy_saved_phi_moves = mir.allocation_phi_moves;
    mir_lazy_allocation_active = 1;
    mir_allocate_registers(mir.live_in, mir.live_out, &summary, 0, NULL, 0);
    return 1;
}

void mir_end_lazy_parameter_allocation(void)
{
    int value_count = mir.next_value;

    if (!mir_lazy_allocation_active)
        return;
    memcpy(mir.allocation_colors, mir_lazy_saved_colors,
           (size_t)value_count * sizeof(*mir_lazy_saved_colors));
    memcpy(mir.allocation_spills, mir_lazy_saved_spills,
           (size_t)value_count * sizeof(*mir_lazy_saved_spills));
    mir.allocation_spill_count = mir_lazy_saved_spill_count;
    mir.allocation_fixed_moves = mir_lazy_saved_fixed_moves;
    mir.allocation_operand_moves = mir_lazy_saved_operand_moves;
    mir.allocation_phi_moves = mir_lazy_saved_phi_moves;
    memset(mir.lazy_parameter_values, 0, (size_t)value_count);
    mir_lazy_allocation_active = 0;
    free(mir_lazy_saved_colors);
    free(mir_lazy_saved_spills);
    mir_lazy_saved_colors = NULL;
    mir_lazy_saved_spills = NULL;
}

int mir_begin_rematerialized_home_allocation(void)
{
    int value_count = mir.next_value;

    if (mir_rematerialized_home_allocation_active || value_count == 0)
        return 0;
    mir_rematerialized_saved_colors = (int *)malloc(
        (size_t)value_count * sizeof(*mir_rematerialized_saved_colors));
    mir_rematerialized_saved_spills = (int *)malloc(
        (size_t)value_count * sizeof(*mir_rematerialized_saved_spills));
    if (mir_rematerialized_saved_colors == NULL ||
        mir_rematerialized_saved_spills == NULL)
        fatal("out of memory saving rematerialized MIR allocation");
    memcpy(mir_rematerialized_saved_colors, mir.allocation_colors,
           (size_t)value_count * sizeof(*mir_rematerialized_saved_colors));
    memcpy(mir_rematerialized_saved_spills, mir.allocation_spills,
           (size_t)value_count * sizeof(*mir_rematerialized_saved_spills));
    mir_rematerialized_saved_spill_count = mir.allocation_spill_count;
    mir_rematerialized_saved_fixed_moves = mir.allocation_fixed_moves;
    mir_rematerialized_saved_operand_moves = mir.allocation_operand_moves;
    mir_rematerialized_saved_phi_moves = mir.allocation_phi_moves;
    mir_rematerialized_home_allocation_active = 1;
    return 1;
}

void mir_end_rematerialized_home_allocation(void)
{
    int value_count = mir.next_value;

    if (!mir_rematerialized_home_allocation_active)
        return;
    memcpy(mir.allocation_colors, mir_rematerialized_saved_colors,
           (size_t)value_count * sizeof(*mir_rematerialized_saved_colors));
    memcpy(mir.allocation_spills, mir_rematerialized_saved_spills,
           (size_t)value_count * sizeof(*mir_rematerialized_saved_spills));
    mir.allocation_spill_count = mir_rematerialized_saved_spill_count;
    mir.allocation_fixed_moves = mir_rematerialized_saved_fixed_moves;
    mir.allocation_operand_moves = mir_rematerialized_saved_operand_moves;
    mir.allocation_phi_moves = mir_rematerialized_saved_phi_moves;
    mir_rematerialized_home_allocation_active = 0;
    free(mir_rematerialized_saved_colors);
    free(mir_rematerialized_saved_spills);
    mir_rematerialized_saved_colors = NULL;
    mir_rematerialized_saved_spills = NULL;
}

int mir_rematerialized_home_allocation_is_active(void)
{
    return mir_rematerialized_home_allocation_active;
}

static int mir_regional_boundary_instruction(int instruction)
{
    const struct MirInsn *insn;

    if (instruction < 0 || instruction >= mir.count)
        return 1;
    insn = &mir.insns[instruction];
    return insn->opcode == MIR_LABEL ||
           insn->opcode == MIR_PHI ||
           insn->opcode == MIR_JUMP ||
           insn->opcode == MIR_BRANCH_FALSE ||
           insn->opcode == MIR_RETURN ||
           mir_instruction_clobbers_caller_registers(insn);
}

static void mir_regional_reserve_regions(int count)
{
    if (mir.region_capacity < count) {
        struct MirRegion *new_regions = (struct MirRegion *)realloc(
            mir.regions, (size_t)count * sizeof(*new_regions));
        if (new_regions == NULL)
            fatal("out of memory planning MIR regions");
        mir.regions = new_regions;
        mir.region_capacity = count;
    }
    if (mir.instruction_region_capacity < mir.count) {
        int *new_instruction_regions = (int *)realloc(
            mir.instruction_regions,
            (size_t)mir.count * sizeof(*new_instruction_regions));
        if (new_instruction_regions == NULL)
            fatal("out of memory mapping MIR regions");
        mir.instruction_regions = new_instruction_regions;
        mir.instruction_region_capacity = mir.count;
    }
}

static void mir_build_call_free_regions(void)
{
    int instruction = 0;

    mir_regional_reserve_regions(mir.count > 0 ? mir.count : 1);
    mir.region_count = 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        mir.instruction_regions[instruction] = -1;
    instruction = 0;
    while (instruction < mir.count) {
        int first;
        int last;
        int region;

        while (instruction < mir.count &&
               mir_regional_boundary_instruction(instruction))
            ++instruction;
        if (instruction >= mir.count)
            break;
        first = instruction;
        region = mir.region_count++;
        while (instruction < mir.count &&
               !mir_regional_boundary_instruction(instruction)) {
            mir.instruction_regions[instruction] = region;
            ++instruction;
        }
        last = instruction - 1;
        mir.regions[region].first = first;
        mir.regions[region].last = last;
        mir.regions[region].boundary_after =
            instruction < mir.count ? instruction : -1;
    }
}

static int mir_regional_instruction_use_count(int instruction, int value)
{
    const struct MirInsn *insn = &mir.insns[instruction];
    int count = 0;

    if (insn->opcode == MIR_ARG)
        return 0;
    if (insn->src1 == value)
        ++count;
    if (insn->src2 == value)
        ++count;
    return count;
}

static int mir_regional_boundary_use_count(int region, int value)
{
    int boundary = mir.regions[region].boundary_after;
    const struct MirInsn *insn;
    int count = 0;

    if (boundary < 0 || boundary >= mir.count)
        return 0;
    insn = &mir.insns[boundary];
    if (insn->src1 == value)
        ++count;
    if (insn->src2 == value)
        ++count;
    if (mir_call_uses_value(insn, value))
        ++count;
    return count;
}

static int mir_regional_definition_index(int value)
{
    const struct MirInsn *definition = mir_definition(value);

    return definition != NULL ? (int)(definition - mir.insns) : -1;
}

static int mir_regional_value_call_crossings(int value)
{
    int count = 0;
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir_instruction_clobbers_caller_registers(
                &mir.insns[instruction]) &&
            mir.live_in[(size_t)instruction * mir.next_value + value] &&
            mir.live_out[(size_t)instruction * mir.next_value + value])
            ++count;
    return count;
}

static int mir_regional_used_region_count(int value)
{
    int count = 0;
    int definition = mir_regional_definition_index(value);
    int region;

    for (region = 0; region < mir.region_count; ++region) {
        int uses = 0;
        int instruction;

        for (instruction = mir.regions[region].first;
             instruction <= mir.regions[region].last; ++instruction)
            uses += mir_regional_instruction_use_count(instruction, value);
        uses += mir_regional_boundary_use_count(region, value);
        if (uses != 0 ||
            (definition >= mir.regions[region].first &&
             definition <= mir.regions[region].last))
            ++count;
    }
    return count;
}

static int mir_regional_value_crosses_region(int value)
{
    int region;

    for (region = 0; region < mir.region_count; ++region)
        if (mir.live_out[(size_t)mir.regions[region].last *
                         mir.next_value + value])
            return 1;
    return 0;
}

static int mir_regional_source_backs_object(int value, int object)
{
    const struct MirInsn *definition = mir_definition(value);
    int instruction;

    if (definition != NULL &&
        definition->opcode == MIR_PARAM &&
        definition->object == object)
        return 1;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_STORE &&
            mir.insns[instruction].object == object &&
            mir.insns[instruction].src1 == value)
            return 1;
    return 0;
}

static int mir_regional_object_home_for_value(int value)
{
    int instruction;

    if (!mir_regional_home_plan_active)
        return -1;
    for (instruction = 0; instruction < mir.count; ++instruction) {
        const struct MirInsn *phi = &mir.insns[instruction];
        const struct MirObject *object;

        if (phi->opcode != MIR_PHI ||
            phi->object < 0 || phi->object >= mir.object_count ||
            (phi->dst != value &&
             phi->src1 != value &&
             phi->src2 != value))
            continue;
        object = &mir.objects[phi->object];
        if ((object->storage != SC_LOCAL &&
             object->storage != SC_PARAM) ||
            type_size(object->type) != type_size(phi->type) ||
            mir_object_address_taken(phi->object) ||
            (object->storage == SC_LOCAL &&
             (object->offset >= 0 ||
              -object->offset > mir_effective_local_bytes())) ||
            !mir_regional_source_backs_object(phi->src1, phi->object) ||
            !mir_regional_source_backs_object(phi->src2, phi->object))
            continue;
        return phi->object;
    }
    return -1;
}

int mir_regional_object_home_offset(int value, int *offset)
{
    int object = mir_regional_object_home_for_value(value);
    int result;

    if (object < 0)
        return 0;
    result = mir.objects[object].offset;
    if (mir.objects[object].storage == SC_PARAM &&
        mir_home_uses_iy())
        result += 2;
    if (offset != NULL)
        *offset = result;
    return 1;
}

int mir_regional_store_uses_object_home(const struct MirInsn *store)
{
    return mir_regional_home_plan_active &&
           store != NULL &&
           store->opcode == MIR_STORE &&
           store->object >= 0 &&
           mir_regional_object_home_for_value(store->src1) ==
               store->object;
}

static void mir_regional_reserve_segments(int count)
{
    if (mir.regional_segment_capacity < count) {
        int new_capacity =
            mir.regional_segment_capacity > 0
                ? mir.regional_segment_capacity : 256;
        struct MirRegionalSegment *new_segments =
            NULL;

        while (new_capacity < count)
            new_capacity *= 2;
        new_segments = (struct MirRegionalSegment *)realloc(
            mir.regional_segments,
            (size_t)new_capacity * sizeof(*new_segments));
        if (new_segments == NULL)
            fatal("out of memory planning MIR regional homes");
        mir.regional_segments = new_segments;
        mir.regional_segment_capacity = new_capacity;
    }
    if (mir.regional_segment_head_capacity < mir.next_value) {
        int *new_heads = (int *)realloc(
            mir.regional_segment_heads,
            (size_t)mir.next_value * sizeof(*new_heads));
        if (new_heads == NULL)
            fatal("out of memory indexing MIR regional homes");
        mir.regional_segment_heads = new_heads;
        mir.regional_segment_head_capacity = mir.next_value;
    }
}

static int mir_regional_add_segment(int value, int region)
{
    struct MirRegionalSegment *segment;
    int index;
    int definition = mir_regional_definition_index(value);
    int instruction;

    mir_regional_reserve_segments(
        mir.regional_segment_count + 1);
    index = mir.regional_segment_count++;
    segment = &mir.regional_segments[index];
    memset(segment, 0, sizeof(*segment));
    segment->value = value;
    segment->region = region;
    segment->first_use = mir.regions[region].last + 1;
    segment->last_use = -1;
    segment->color = -1;
    segment->spill_slot = -1;
    segment->next_for_value =
        mir.regional_segment_heads[value];
    mir.regional_segment_heads[value] = index;
    if (mir.live_in[(size_t)mir.regions[region].first *
                    mir.next_value + value])
        segment->flags |= MIR_REGIONAL_LIVE_IN;
    if (mir.live_out[(size_t)mir.regions[region].last *
                     mir.next_value + value])
        segment->flags |= MIR_REGIONAL_LIVE_OUT;
    if (definition >= mir.regions[region].first &&
        definition <= mir.regions[region].last) {
        segment->flags |= MIR_REGIONAL_DEFINES;
        segment->first_use = definition;
        segment->last_use = definition;
    }
    for (instruction = mir.regions[region].first;
         instruction <= mir.regions[region].last; ++instruction) {
        int uses = mir_regional_instruction_use_count(
            instruction, value);
        if (uses == 0)
            continue;
        if (segment->first_use > instruction)
            segment->first_use = instruction;
        segment->last_use = instruction;
        segment->use_count += uses;
    }
    if (mir_regional_boundary_use_count(region, value)) {
        if (segment->first_use > mir.regions[region].last)
            segment->first_use = mir.regions[region].last;
        if (segment->last_use < mir.regions[region].last)
            segment->last_use = mir.regions[region].last;
        ++segment->use_count;
    }
    return index;
}

static int mir_regional_available_colors(
    int value, int first, int last,
    const unsigned char *candidate)
{
    const struct MirInsn *definition = mir_definition(value);
    int wide = definition != NULL &&
               type_size(definition->type) == 4;
    int available = wide
        ? (1 << MIR_COLOR_HL_DE) |
          (1 << MIR_COLOR_BC_IY)
        : (1 << MIR_COLOR_HL) |
          (1 << MIR_COLOR_DE) |
          (1 << MIR_COLOR_BC);
    int instruction;

    for (instruction = first; instruction <= last; ++instruction) {
        int other;

        for (other = 0; other < mir.next_value; ++other) {
            int color;

            if (other == value ||
                (candidate != NULL && candidate[other]) ||
                (!mir.live_in[(size_t)instruction *
                              mir.next_value + other] &&
                 !mir.live_out[(size_t)instruction *
                               mir.next_value + other] &&
                 mir.insns[instruction].dst != other))
                continue;
            color = mir_regional_base_colors[other];
            if (color < 0)
                continue;
            {
                int candidate_color;

                for (candidate_color = 0;
                     candidate_color < MIR_COLOR_COUNT;
                     ++candidate_color)
                    if ((available & (1 << candidate_color)) != 0 &&
                        mir_color_shares_slot(color, candidate_color))
                        available &= ~(1 << candidate_color);
            }
        }
    }
    return available;
}

static int mir_regional_color_slot_mask(int color)
{
    switch (color) {
    case MIR_COLOR_HL_DE:
        return (1 << MIR_COLOR_HL) | (1 << MIR_COLOR_DE);
    case MIR_COLOR_BC_IY:
        return (1 << MIR_COLOR_BC) | (1 << MIR_COLOR_IY);
    default:
        return color >= MIR_COLOR_HL && color <= MIR_COLOR_IY
            ? 1 << color : 0;
    }
}

static int mir_regional_segments_overlap(
    const struct MirRegionalSegment *left,
    const struct MirRegionalSegment *right)
{
    int left_last = (left->flags & MIR_REGIONAL_LIVE_OUT) != 0
        ? mir.regions[left->region].last : left->last_use;
    int right_last = (right->flags & MIR_REGIONAL_LIVE_OUT) != 0
        ? mir.regions[right->region].last : right->last_use;

    return left->first_use <= right_last &&
           right->first_use <= left_last;
}

static void mir_allocate_regional_colors(
    const unsigned char *candidate)
{
    int region;

    for (region = 0; region < mir.region_count; ++region) {
        for (;;) {
            int best = -1;
            int segment_index;
            int color = -1;

            for (segment_index = 0;
                 segment_index < mir.regional_segment_count;
                 ++segment_index) {
                struct MirRegionalSegment *segment =
                    &mir.regional_segments[segment_index];
                const struct MirInsn *definition =
                    mir_definition(segment->value);

                if (segment->region != region ||
                    segment->color != -1 ||
                    (segment->use_count < 2 &&
                     ((segment->flags & MIR_REGIONAL_DEFINES) == 0 ||
                      definition == NULL ||
                      type_size(definition->type) > 2)))
                    continue;
                if (best < 0 ||
                    segment->use_count >
                        mir.regional_segments[best].use_count)
                    best = segment_index;
            }
            if (best < 0)
                break;
            {
                static const int preference[] = {
                    MIR_COLOR_BC, MIR_COLOR_DE, MIR_COLOR_HL,
                    MIR_COLOR_HL_DE, MIR_COLOR_BC_IY
                };
                size_t preference_index;

                for (preference_index = 0;
                     preference_index <
                         sizeof(preference) / sizeof(preference[0]);
                     ++preference_index) {
                    int proposed = preference[preference_index];
                    int conflict = 0;

                    if ((mir.regional_segments[best].allocatable_colors &
                         (1 << proposed)) == 0)
                        continue;
                    for (segment_index = 0;
                         segment_index < mir.regional_segment_count;
                         ++segment_index) {
                        const struct MirRegionalSegment *other =
                            &mir.regional_segments[segment_index];

                        if (other->region != region ||
                            other->color < 0 ||
                            (mir_regional_color_slot_mask(other->color) &
                             mir_regional_color_slot_mask(proposed)) == 0 ||
                            !mir_regional_segments_overlap(
                                &mir.regional_segments[best], other))
                            continue;
                        conflict = 1;
                        break;
                    }
                    if (!conflict) {
                        color = proposed;
                        break;
                    }
                }
            }
            if (color >= 0) {
                mir.regional_segments[best].color = color;
            } else {
                mir.regional_segments[best].color = -2;
            }
        }
        {
            int segment_index;
            for (segment_index = 0;
                 segment_index < mir.regional_segment_count;
                 ++segment_index)
                if (mir.regional_segments[segment_index].region == region &&
                    mir.regional_segments[segment_index].color == -2)
                    mir.regional_segments[segment_index].color = -1;
        }
    }
    (void)candidate;
}

static void mir_regional_mark_value_occupancy(
    unsigned char *occupancy, int value,
    const unsigned char *candidate)
{
    unsigned char *row =
        &occupancy[(size_t)value * mir.count];
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.live_in[(size_t)instruction *
                        mir.next_value + value] ||
            mir.live_out[(size_t)instruction *
                         mir.next_value + value] ||
            mir.insns[instruction].dst == value ||
            mir_regional_instruction_use_count(instruction, value) != 0)
            row[instruction] = 1;
    if (candidate[value]) {
        int segment_index =
            mir.regional_segment_heads[value];

        while (segment_index >= 0) {
            const struct MirRegionalSegment *segment =
                &mir.regional_segments[segment_index];
            const struct MirRegion *region =
                &mir.regions[segment->region];

            if (segment->color >= 0) {
                for (instruction = region->first;
                     instruction <= region->last; ++instruction)
                    row[instruction] = 0;
                if ((segment->flags & MIR_REGIONAL_LIVE_IN) != 0)
                    for (instruction = region->first;
                         instruction <= segment->first_use;
                         ++instruction)
                        row[instruction] = 1;
                if ((segment->flags & MIR_REGIONAL_LIVE_OUT) != 0)
                    row[region->boundary_after >= 0
                            ? region->boundary_after
                            : segment->last_use] = 1;
            }
            segment_index = segment->next_for_value;
        }
    }
}

static int mir_regional_occupancy_conflicts(
    const unsigned char *left, const unsigned char *right)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (left[instruction] && right[instruction])
            return 1;
    return 0;
}

static int mir_regional_occupancy_nonempty(
    const unsigned char *occupancy)
{
    int instruction;

    for (instruction = 0; instruction < mir.count; ++instruction)
        if (occupancy[instruction])
            return 1;
    return 0;
}

static void mir_allocate_regional_spill_slots(
    const unsigned char *candidate)
{
    unsigned char *occupancy;
    unsigned char *slot_occupancy;
    int value;
    int slot_count = 0;

    occupancy = (unsigned char *)calloc(
        (size_t)mir.next_value * mir.count, 1);
    slot_occupancy = (unsigned char *)calloc(
        (size_t)mir.next_value * mir.count, 1);
    if (occupancy == NULL || slot_occupancy == NULL)
        fatal("out of memory allocating regional MIR spills");
    for (value = 0; value < mir.next_value; ++value)
        if (mir.regional_rematerializable[value] ==
                MIR_REGIONAL_REMAT_NONE &&
            !mir_regional_object_home_offset(value, NULL) &&
            !mir_homed_value_is_rematerializable(value) &&
            (candidate[value] ||
             mir_regional_base_spills[value] >= 0))
            mir_regional_mark_value_occupancy(
                occupancy, value, candidate);
    for (value = 0; value < mir.next_value; ++value) {
        const unsigned char *value_occupancy =
            &occupancy[(size_t)value * mir.count];
        int slot;

        mir.allocation_spills[value] = -1;
        if (mir.regional_rematerializable[value] !=
                MIR_REGIONAL_REMAT_NONE)
            continue;
        if (mir_regional_object_home_offset(value, NULL))
            continue;
        if (mir_homed_value_is_rematerializable(value))
            continue;
        if (!candidate[value] &&
            mir_regional_base_spills[value] < 0)
            continue;
        if (!mir_regional_occupancy_nonempty(value_occupancy))
            continue;
        for (slot = 0; slot < slot_count; ++slot)
            if (!mir_regional_occupancy_conflicts(
                    value_occupancy,
                    &slot_occupancy[(size_t)slot * mir.count]))
                break;
        if (slot == slot_count)
            ++slot_count;
        mir.allocation_spills[value] = slot;
        {
            unsigned char *target =
                &slot_occupancy[(size_t)slot * mir.count];
            int instruction;

            for (instruction = 0;
                 instruction < mir.count; ++instruction)
                target[instruction] |= value_occupancy[instruction];
        }
    }
    mir.allocation_spill_count = slot_count;
    mir.regional_spill_slot_count = slot_count;
    for (value = 0; value < mir.regional_segment_count; ++value)
        mir.regional_segments[value].spill_slot =
            mir.allocation_spills[
                mir.regional_segments[value].value];
    free(slot_occupancy);
    free(occupancy);
}

static void mir_report_regional_home_plan(
    const unsigned char *candidate)
{
    static const char *const homes[] = {
        "hl", "de", "bc", "iy", "hl:de", "bc:iy"
    };
    int region;
    int segment;

    if (getenv("DCC_MIR_REGIONAL_HOME_REPORT") == NULL)
        return;
    fprintf(stderr,
            "; MIR regional-plan function=%s regions=%d segments=%d "
            "spill-slots=%d spill-bytes=%d\n",
            mir.name, mir.region_count,
            mir.regional_segment_count,
            mir.regional_spill_slot_count,
            mir_home_spill_bytes());
    for (region = 0; region < mir.region_count; ++region)
        fprintf(stderr,
                "; MIR regional-region function=%s region=%d first=%d "
                "last=%d boundary=%d\n",
                mir.name, region, mir.regions[region].first,
                mir.regions[region].last,
                mir.regions[region].boundary_after);
    for (region = 0; region < mir.next_value; ++region)
        if (mir.regional_rematerializable[region] !=
            MIR_REGIONAL_REMAT_NONE)
            fprintf(stderr,
                    "; MIR regional-remat function=%s value=%d kind=%s\n",
                    mir.name, region,
                    mir.regional_rematerializable[region] ==
                            MIR_REGIONAL_REMAT_PARAMETER
                        ? "parameter" : "address");
    for (segment = 0;
         segment < mir.regional_segment_count; ++segment) {
        const struct MirRegionalSegment *item =
            &mir.regional_segments[segment];
        fprintf(stderr,
                "; MIR regional-segment function=%s value=%d region=%d "
                "first=%d last=%d uses=%d available=%c%c%c "
                "allocatable=%c%c%c home=%s "
                "slot=%d flags=%d\n",
                mir.name, item->value, item->region,
                item->first_use, item->last_use,
                item->use_count,
                (item->available_colors &
                 (1 << MIR_COLOR_HL)) ? 'h' : '-',
                (item->available_colors &
                 (1 << MIR_COLOR_DE)) ? 'd' : '-',
                (item->available_colors &
                 (1 << MIR_COLOR_BC)) ? 'b' : '-',
                (item->allocatable_colors &
                 (1 << MIR_COLOR_HL)) ? 'h' : '-',
                (item->allocatable_colors &
                 (1 << MIR_COLOR_DE)) ? 'd' : '-',
                (item->allocatable_colors &
                 (1 << MIR_COLOR_BC)) ? 'b' : '-',
                item->color >= 0 ? homes[item->color] : "spill",
                item->spill_slot, item->flags);
    }
    (void)candidate;
}

static int mir_regional_parameter_rematerializable(
    const struct MirInsn *parameter)
{
    int instruction;

    if (parameter == NULL || parameter->opcode != MIR_PARAM ||
        parameter->dst < 0 || parameter->object < 0 ||
        parameter->object >= mir.object_count ||
        mir.objects[parameter->object].storage != SC_PARAM ||
        type_size(parameter->type) < 1 ||
        type_size(parameter->type) > 2 ||
        mir_object_address_taken(parameter->object))
        return 0;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if (mir.insns[instruction].opcode == MIR_STORE &&
            mir.insns[instruction].object == parameter->object)
            return 0;
    return 1;
}

static int mir_regional_address_rematerializable(
    const struct MirInsn *address)
{
    int memory_type;
    int memory_storage;
    int memory_offset;

    return address != NULL && address->opcode == MIR_ADDRESS &&
           !mir_declared_is_vla_object(address->name) &&
           mir_scalar_memory_location(
               address, &memory_type, &memory_storage,
               &memory_offset) &&
           (memory_storage == SC_LOCAL ||
            memory_storage == SC_PARAM ||
            memory_storage == SC_GLOBAL ||
            memory_storage == SC_EXTERN ||
            memory_storage == SC_FUNC);
}

static int mir_regional_has_wide_values(void)
{
    int instruction;

    if (type_size(mir.return_type) == 4)
        return 1;
    for (instruction = 0; instruction < mir.count; ++instruction)
        if ((mir.insns[instruction].dst >= 0 &&
             type_size(mir.insns[instruction].type) == 4) ||
            (mir.insns[instruction].opcode == MIR_BINARY &&
             type_size(mir.insns[instruction].secondary_offset) == 4))
            return 1;
    return 0;
}

static int mir_regional_prepare_wide_base(
    const unsigned char *excluded)
{
    struct MirAllocationSummary summary;
    int wide_spills = 0;
    int spill;

    if (!mir_regional_has_wide_values())
        return 1;
    mir_allocate_registers(
        mir.live_in, mir.live_out, &summary, 1, excluded, 1);
    for (spill = 0; spill < mir.allocation_spill_count; ++spill) {
        int value;
        int width = 0;

        for (value = 0; value < mir.next_value; ++value)
            if (mir.allocation_spills[value] == spill) {
                const struct MirInsn *definition =
                    mir_definition(value);
                int value_width = definition != NULL
                    ? type_size(definition->type) : 0;

                if (value_width > width)
                    width = value_width;
            }
        if (width > 2)
            ++wide_spills;
    }
    if (getenv("DCC_MIR_REGIONAL_HOME_REPORT") != NULL)
        fprintf(stderr,
                "; MIR regional-wide-base function=%s spills=%d "
                "wide-spills=%d\n",
                mir.name, mir.allocation_spill_count, wide_spills);
    return wide_spills <= 1;
}

/*
 * Split narrow values only at existing CFG/call boundaries. The verifier's
 * persisted live_in/live_out matrices define segment entry/exit state; no
 * second liveness solution is built. Colored segments keep a caller-saved
 * home locally, while their boundary slot is available to non-overlapping
 * segments and is restored before the value leaves the region.
 */
int mir_begin_regional_home_plan(void)
{
    unsigned char *candidate;
    unsigned char *excluded;
    int has_wide = 0;
    int value;
    int region;

    if (mir_regional_home_plan_active ||
        mir.live_in == NULL || mir.live_out == NULL ||
        mir.next_value <= 0 || mir.count <= 0)
        return 0;
    mir_regional_saved_colors = (int *)malloc(
        (size_t)mir.next_value * sizeof(*mir_regional_saved_colors));
    mir_regional_saved_spills = (int *)malloc(
        (size_t)mir.next_value * sizeof(*mir_regional_saved_spills));
    mir_regional_base_colors = (int *)malloc(
        (size_t)mir.next_value * sizeof(*mir_regional_base_colors));
    mir_regional_base_spills = (int *)malloc(
        (size_t)mir.next_value * sizeof(*mir_regional_base_spills));
    candidate = (unsigned char *)calloc(
        (size_t)mir.next_value, 1);
    excluded = (unsigned char *)calloc(
        (size_t)mir.next_value, 1);
    if (mir_regional_saved_colors == NULL ||
        mir_regional_saved_spills == NULL ||
        mir_regional_base_colors == NULL ||
        mir_regional_base_spills == NULL ||
        candidate == NULL || excluded == NULL)
        fatal("out of memory saving regional MIR allocation");
    if (mir.regional_rematerializable_capacity < mir.next_value) {
        unsigned char *new_rematerializable =
            (unsigned char *)realloc(
                mir.regional_rematerializable,
                (size_t)mir.next_value);
        if (new_rematerializable == NULL)
            fatal("out of memory planning regional rematerialization");
        mir.regional_rematerializable = new_rematerializable;
        mir.regional_rematerializable_capacity = mir.next_value;
    }
    memset(mir.regional_rematerializable, 0,
           (size_t)mir.next_value);
    memcpy(mir_regional_saved_colors, mir.allocation_colors,
           (size_t)mir.next_value *
               sizeof(*mir_regional_saved_colors));
    memcpy(mir_regional_saved_spills, mir.allocation_spills,
           (size_t)mir.next_value *
               sizeof(*mir_regional_saved_spills));
    mir_regional_saved_spill_count =
        mir.allocation_spill_count;
    mir_regional_saved_fixed_moves =
        mir.allocation_fixed_moves;
    mir_regional_saved_operand_moves =
        mir.allocation_operand_moves;
    mir_regional_saved_phi_moves =
        mir.allocation_phi_moves;
    for (value = 0; value < mir.next_value; ++value) {
        const struct MirInsn *definition = mir_definition(value);

        if (definition != NULL &&
            type_size(definition->type) == 4)
            has_wide = 1;
    }
    if (has_wide) {
        struct MirAllocationSummary summary;

        mir_allocate_registers(
            mir.live_in, mir.live_out, &summary, 1, NULL, 1);
        for (value = 0; value < mir.next_value; ++value)
            if (mir.allocation_colors[value] == MIR_COLOR_BC_IY) {
                mir.allocation_colors[value] = -1;
                mir.allocation_spills[value] =
                    mir.allocation_spill_count++;
            }
    }
    memcpy(mir_regional_base_spills, mir.allocation_spills,
           (size_t)mir.next_value *
               sizeof(*mir_regional_base_spills));
    mir_regional_home_plan_active = 1;
    mir_build_call_free_regions();
    mir_regional_reserve_segments(
        mir.next_value > 0 ? mir.next_value : 1);
    for (value = 0; value < mir.next_value; ++value)
        mir.regional_segment_heads[value] = -1;
    mir.regional_segment_count = 0;
    for (value = 0; value < mir.next_value; ++value) {
        const struct MirInsn *definition = mir_definition(value);
        int crosses_call;
        int crosses_region;
        int used_regions;

        if (definition == NULL)
            continue;
        if (mir_regional_parameter_rematerializable(definition)) {
            mir.regional_rematerializable[value] =
                MIR_REGIONAL_REMAT_PARAMETER;
            continue;
        }
        if (mir_regional_address_rematerializable(definition)) {
            mir.regional_rematerializable[value] =
                MIR_REGIONAL_REMAT_ADDRESS;
            continue;
        }
        crosses_call = mir_regional_value_call_crossings(value);
        crosses_region = mir_regional_value_crosses_region(value);
        used_regions = mir_regional_used_region_count(value);
        if (getenv("DCC_MIR_REGIONAL_HOME_REPORT") != NULL &&
            crosses_call && used_regions >= 2)
            fprintf(stderr,
                    "; MIR regional-value function=%s value=%d opcode=%s "
                    "type=%d size=%d calls=%d regions=%d\n",
                    mir.name, value,
                    mir_opcode_name(definition->opcode),
                    definition->type, type_size(definition->type),
                    crosses_call, used_regions);
        if (type_size(definition->type) < 1 ||
            type_size(definition->type) > 4 ||
            (type_size(definition->type) == 4 &&
             !type_is_long(definition->type) &&
             !type_is_float(definition->type)) ||
            (mir_regional_base_spills[value] < 0 &&
             !crosses_call &&
             !(type_size(definition->type) == 4 &&
               crosses_region)) ||
            used_regions < 1)
            continue;
        candidate[value] = 1;
    }
    for (value = 0; value < mir.next_value; ++value)
        excluded[value] = (unsigned char)(
            candidate[value] ||
            mir.regional_rematerializable[value] !=
                MIR_REGIONAL_REMAT_NONE);
    if (!mir_regional_prepare_wide_base(excluded)) {
        free(excluded);
        free(candidate);
        mir_end_regional_home_plan();
        return 0;
    }
    memcpy(mir_regional_base_colors, mir.allocation_colors,
           (size_t)mir.next_value *
               sizeof(*mir_regional_base_colors));
    memcpy(mir_regional_base_spills, mir.allocation_spills,
           (size_t)mir.next_value *
               sizeof(*mir_regional_base_spills));
    for (value = 0; value < mir.next_value; ++value) {
        int definition;

        if (!candidate[value])
            continue;
        definition = mir_regional_definition_index(value);
        for (region = 0; region < mir.region_count; ++region) {
            int uses = 0;
            int instruction;

            for (instruction = mir.regions[region].first;
                 instruction <= mir.regions[region].last;
                 ++instruction)
                uses += mir_regional_instruction_use_count(
                    instruction, value);
            uses += mir_regional_boundary_use_count(
                region, value);
            if (uses != 0 ||
                (definition >= mir.regions[region].first &&
                 definition <= mir.regions[region].last))
                mir_regional_add_segment(value, region);
        }
    }
    for (value = 0;
         value < mir.regional_segment_count; ++value) {
        struct MirRegionalSegment *segment =
            &mir.regional_segments[value];
        int color_first =
            (segment->flags & MIR_REGIONAL_LIVE_IN) != 0
                ? mir.regions[segment->region].first
                : segment->first_use;
        int color_last =
            (segment->flags & MIR_REGIONAL_LIVE_OUT) != 0
                ? mir.regions[segment->region].last
                : segment->last_use;
        segment->available_colors =
            mir_regional_available_colors(
                segment->value, color_first,
                color_last, NULL);
        segment->allocatable_colors =
            mir_regional_available_colors(
                segment->value, color_first,
                color_last, candidate);
    }
    mir_allocate_regional_colors(candidate);
    mir_allocate_regional_spill_slots(candidate);
    for (value = 0; value < mir.next_value; ++value)
        if (candidate[value] ||
            mir_regional_object_home_offset(value, NULL) ||
            mir.regional_rematerializable[value] !=
                MIR_REGIONAL_REMAT_NONE)
            mir.allocation_colors[value] = -1;
    mir_report_regional_home_plan(candidate);
    free(excluded);
    free(candidate);
    if (mir.regional_segment_count == 0) {
        mir_end_regional_home_plan();
        return 0;
    }
    return 1;
}

void mir_end_regional_home_plan(void)
{
    if (!mir_regional_home_plan_active)
        return;
    memcpy(mir.allocation_colors, mir_regional_saved_colors,
           (size_t)mir.next_value *
               sizeof(*mir_regional_saved_colors));
    memcpy(mir.allocation_spills, mir_regional_saved_spills,
           (size_t)mir.next_value *
               sizeof(*mir_regional_saved_spills));
    mir.allocation_spill_count =
        mir_regional_saved_spill_count;
    mir.allocation_fixed_moves =
        mir_regional_saved_fixed_moves;
    mir.allocation_operand_moves =
        mir_regional_saved_operand_moves;
    mir.allocation_phi_moves =
        mir_regional_saved_phi_moves;
    mir_regional_home_plan_active = 0;
    mir.region_count = 0;
    mir.regional_segment_count = 0;
    mir.regional_spill_slot_count = 0;
    if (mir.regional_rematerializable != NULL)
        memset(mir.regional_rematerializable, 0,
               (size_t)mir.next_value);
    free(mir_regional_saved_colors);
    free(mir_regional_saved_spills);
    free(mir_regional_base_colors);
    free(mir_regional_base_spills);
    mir_regional_saved_colors = NULL;
    mir_regional_saved_spills = NULL;
    mir_regional_base_colors = NULL;
    mir_regional_base_spills = NULL;
}

int mir_regional_home_plan_is_active(void)
{
    return mir_regional_home_plan_active;
}

const struct MirRegionalSegment *mir_regional_segment_for(
    int value, int instruction)
{
    int region;
    int segment;

    if (!mir_regional_home_plan_active ||
        value < 0 || value >= mir.next_value ||
        instruction < 0 || instruction >= mir.count)
        return NULL;
    region = mir.instruction_regions[instruction];
    if (region < 0)
        return NULL;
    segment = mir.regional_segment_heads[value];
    while (segment >= 0) {
        if (mir.regional_segments[segment].region == region)
            return &mir.regional_segments[segment];
        segment =
            mir.regional_segments[segment].next_for_value;
    }
    return NULL;
}

int mir_regional_rematerialization_kind(int value)
{
    return mir_regional_home_plan_active &&
           value >= 0 && value < mir.next_value &&
           mir.regional_rematerializable != NULL
        ? mir.regional_rematerializable[value]
        : MIR_REGIONAL_REMAT_NONE;
}

int mir_regional_parameter_location(int value, int *offset, int *type)
{
    const struct MirInsn *definition;
    const struct MirObject *object;

    if (mir_regional_rematerialization_kind(value) !=
            MIR_REGIONAL_REMAT_PARAMETER)
        return 0;
    definition = mir_definition(value);
    if (definition == NULL || definition->object < 0 ||
        definition->object >= mir.object_count)
        return 0;
    object = &mir.objects[definition->object];
    if (offset != NULL)
        *offset = object->offset;
    if (type != NULL)
        *type = object->type;
    return 1;
}

static int mir_regional_emit_slot_to_color(
    FILE *out, int value, int color)
{
    int offset;

    if (!mir_home_spill_offset(value, &offset))
        return 0;
    switch (color) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1);
        return 1;
    case MIR_COLOR_DE:
        fprintf(out, "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                offset, offset + 1);
        return 1;
    case MIR_COLOR_BC:
        fprintf(out, "\tld c,(ix%+d)\n\tld b,(ix%+d)\n",
                offset, offset + 1);
        return 1;
    case MIR_COLOR_HL_DE:
        fprintf(out,
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n"
                "\tld e,(ix%+d)\n\tld d,(ix%+d)\n",
                offset, offset + 1, offset + 2, offset + 3);
        return 1;
    case MIR_COLOR_BC_IY:
        fputs("\tpush hl\n", out);
        fprintf(out,
                "\tld c,(ix%+d)\n\tld b,(ix%+d)\n"
                "\tld l,(ix%+d)\n\tld h,(ix%+d)\n",
                offset, offset + 1, offset + 2, offset + 3);
        fputs("\tpush hl\n\tpop iy\n\tpop hl\n", out);
        return 1;
    default:
        return 0;
    }
}

static int mir_regional_emit_color_to_slot(
    FILE *out, int value, int color)
{
    int offset;

    if (!mir_home_spill_offset(value, &offset))
        return 0;
    switch (color) {
    case MIR_COLOR_HL:
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset, offset + 1);
        return 1;
    case MIR_COLOR_DE:
        fprintf(out, "\tld (ix%+d),e\n\tld (ix%+d),d\n",
                offset, offset + 1);
        return 1;
    case MIR_COLOR_BC:
        fprintf(out, "\tld (ix%+d),c\n\tld (ix%+d),b\n",
                offset, offset + 1);
        return 1;
    case MIR_COLOR_HL_DE:
        fprintf(out,
                "\tld (ix%+d),l\n\tld (ix%+d),h\n"
                "\tld (ix%+d),e\n\tld (ix%+d),d\n",
                offset, offset + 1, offset + 2, offset + 3);
        return 1;
    case MIR_COLOR_BC_IY:
        fprintf(out, "\tld (ix%+d),c\n\tld (ix%+d),b\n",
                offset, offset + 1);
        fputs("\tpush hl\n\tpush iy\n\tpop hl\n", out);
        fprintf(out, "\tld (ix%+d),l\n\tld (ix%+d),h\n",
                offset + 2, offset + 3);
        fputs("\tpop hl\n", out);
        return 1;
    default:
        return 0;
    }
}

static void mir_regional_set_region_colors(int region)
{
    int value;

    for (value = 0; value < mir.next_value; ++value)
        if (mir.regional_segment_heads[value] >= 0)
            mir.allocation_colors[value] = -1;
    if (region >= 0)
        for (value = 0; value < mir.next_value; ++value) {
            int segment = mir.regional_segment_heads[value];

            while (segment >= 0) {
                if (mir.regional_segments[segment].region == region) {
                    mir.allocation_colors[value] =
                        mir.regional_segments[segment].color;
                    break;
                }
                segment =
                    mir.regional_segments[segment].next_for_value;
            }
        }
}

void mir_regional_begin_emission(void)
{
    if (!mir_regional_home_plan_active)
        return;
    if (mir_regional_register_valid_capacity < mir.next_value) {
        unsigned char *new_valid = (unsigned char *)realloc(
            mir_regional_register_valid,
            (size_t)mir.next_value);
        if (new_valid == NULL)
            fatal("out of memory tracking regional MIR homes");
        mir_regional_register_valid = new_valid;
        mir_regional_register_valid_capacity = mir.next_value;
    }
    memset(mir_regional_register_valid, 0,
           (size_t)mir.next_value);
    mir_regional_emission_region = -1;
    mir_regional_boundary_pending = 0;
    mir_regional_set_region_colors(-1);
}

static int mir_regional_spill_region(
    FILE *out, int region)
{
    int segment;

    if (region < 0)
        return 1;
    for (segment = 0;
         segment < mir.regional_segment_count; ++segment) {
        const struct MirRegionalSegment *item =
            &mir.regional_segments[segment];

        if (item->region != region || item->color < 0 ||
            (item->flags & MIR_REGIONAL_LIVE_OUT) == 0 ||
            !mir_regional_register_valid[item->value])
            continue;
        if (!mir_regional_emit_color_to_slot(
                out, item->value, item->color))
            return 0;
    }
    return 1;
}

static int mir_regional_ensure_value(
    FILE *out, int value, int instruction)
{
    const struct MirRegionalSegment *segment;

    if (value < 0 || value >= mir.next_value)
        return 1;
    segment = mir_regional_segment_for(value, instruction);
    if (segment == NULL || segment->color < 0 ||
        mir_regional_register_valid[value])
        return 1;
    if (!mir_regional_emit_slot_to_color(
            out, value, segment->color))
        return 0;
    mir_regional_register_valid[value] = 1;
    return 1;
}

int mir_regional_before_instruction(FILE *out, int instruction)
{
    const struct MirInsn *insn;
    int region;

    if (!mir_regional_home_plan_active)
        return 1;
    region = instruction >= 0 && instruction < mir.count
        ? mir.instruction_regions[instruction] : -1;
    if (region != mir_regional_emission_region) {
        if (!mir_regional_spill_region(
                out, mir_regional_emission_region))
            return 0;
        if (region < 0 && mir_regional_emission_region >= 0 &&
            instruction >= 0 && instruction < mir.count &&
            !mir_instruction_clobbers_caller_registers(
                &mir.insns[instruction])) {
            mir_regional_boundary_pending = 1;
            return 1;
        }
        memset(mir_regional_register_valid, 0,
               (size_t)mir.next_value);
        mir_regional_emission_region = region;
        mir_regional_set_region_colors(region);
    }
    if (instruction < 0 || instruction >= mir.count ||
        region < 0)
        return 1;
    insn = &mir.insns[instruction];
    if (insn->opcode == MIR_ARG)
        return 1;
    if (!mir_regional_ensure_value(
            out, insn->src1, instruction) ||
        !mir_regional_ensure_value(
            out, insn->src2, instruction))
        return 0;
    return 1;
}

void mir_regional_after_instruction(int instruction)
{
    const struct MirInsn *insn;
    const struct MirRegionalSegment *segment;

    if (!mir_regional_home_plan_active ||
        instruction < 0 || instruction >= mir.count)
        return;
    if (mir_regional_boundary_pending) {
        memset(mir_regional_register_valid, 0,
               (size_t)mir.next_value);
        mir_regional_emission_region = -1;
        mir_regional_set_region_colors(-1);
        mir_regional_boundary_pending = 0;
        return;
    }
    insn = &mir.insns[instruction];
    if (insn->dst < 0 || insn->dst >= mir.next_value)
        return;
    segment = mir_regional_segment_for(
        insn->dst, instruction);
    if (segment != NULL && segment->color >= 0)
        mir_regional_register_valid[insn->dst] = 1;
}

/* Item 20d (mir-migration-plan-to-100pct.md): permanent (non-disposable)
 * wide-coloring probe for mir_try_emit_homed_scalar_cfg. Re-runs the
 * shared allocator with allow_wide_colors=1 using the persisted
 * mir.live_in/mir.live_out (Item 20d part 1) and reports whether every
 * wide (4-byte long) value fits in one of the two pair colors with zero
 * spills. The homed emitter supports transfers between HL:DE and BC:IY;
 * wide values crossing calls still spill because neither pair is wholly
 * callee-saved.
 *
 * A bounded hybrid candidate may retain one wide spill alongside
 * narrow spills. The homed emitter owns width-aware slots and PHI copies;
 * larger spill sets still fall back to the universal spilled backend.
 *
 * mir.allocation_colors/allocation_spills/allocation_spill_count are
 * shared, per-function state that other selectors later in the same
 * dispatch chain read if mir_try_emit_homed_scalar_cfg ultimately
 * rejects the function for any other reason (see mir_try_emit_z80's
 * dispatch order) - so a failed or unused probe must restore the
 * original width-blind coloring exactly, not leave the wide attempt's
 * side effects behind. */
int mir_probe_wide_colors_for_homed(
    const unsigned char *rematerializable, int bounded_hybrid)
{
    int value_count = mir.next_value;
    int *saved_colors;
    int *saved_spills;
    int saved_spill_count;
    struct MirAllocationSummary summary;
    int narrow_spills = 0;
    int wide_spills = 0;
    int ok;
    int value;

    if (mir.live_in == NULL || mir.live_out == NULL || value_count == 0)
        return 0;
    saved_colors = (int *)malloc((size_t)value_count * sizeof(*saved_colors));
    saved_spills = (int *)malloc((size_t)value_count * sizeof(*saved_spills));
    if (saved_colors == NULL || saved_spills == NULL) {
        free(saved_colors);
        free(saved_spills);
        return 0;
    }
    memcpy(saved_colors, mir.allocation_colors,
           (size_t)value_count * sizeof(*saved_colors));
    memcpy(saved_spills, mir.allocation_spills,
           (size_t)value_count * sizeof(*saved_spills));
    saved_spill_count = mir.allocation_spill_count;

    mir_allocate_registers(mir.live_in, mir.live_out, &summary, 1,
                           rematerializable, bounded_hybrid);

    for (value = 0; value < mir.allocation_spill_count; ++value) {
        int member;
        int width = 0;

        for (member = 0; member < value_count; ++member)
            if (mir.allocation_spills[member] == value) {
                const struct MirInsn *definition = mir_definition(member);
                int member_width =
                    definition != NULL ? type_size(definition->type) : 0;
                if (member_width > width)
                    width = member_width;
            }
        if (width <= 2)
            ++narrow_spills;
        else
            ++wide_spills;
    }
    ok = bounded_hybrid
             ? mir.allocation_spill_count <= 4 && wide_spills <= 1
             : summary.spills == 0;
    if (mir.allocation_spill_count != 0 &&
        getenv("DCC_MIR_HOMED_REPORT") != NULL)
    {
        fprintf(stderr,
                "; MIR homed-wide-color function=%s narrow-spills=%d "
                "wide-spills=%d\n",
                mir.name, narrow_spills, wide_spills);
        for (value = 0; value < value_count; ++value)
            if (mir.allocation_spills[value] >= 0) {
                const struct MirInsn *definition = mir_definition(value);
                fprintf(stderr,
                        "; MIR homed-wide-color-spill function=%s value=%d "
                        "opcode=%s type=%d spill=%d\n",
                        mir.name, value,
                        definition != NULL
                            ? mir_opcode_name(definition->opcode) : "none",
                        definition != NULL ? definition->type : 0,
                        mir.allocation_spills[value]);
            }
    }
    if (!ok) {
        memcpy(mir.allocation_colors, saved_colors,
               (size_t)value_count * sizeof(*saved_colors));
        memcpy(mir.allocation_spills, saved_spills,
               (size_t)value_count * sizeof(*saved_spills));
        mir.allocation_spill_count = saved_spill_count;
    }
    free(saved_colors);
    free(saved_spills);
    return ok;
}

int mir_verify_and_dump(void)
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
    mir_report_pointer_parameter_eligibility();
    mir_filter_pointer_parameter_objects();
    mir_filter_address_taken_scalar_objects();
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

    mir_allocate_registers(live_in, live_out, &allocation, 0, NULL, 0);

    if (getenv("DCC_MIR_ALLOCATION_REPORT") != NULL)
        fprintf(stderr,
                "; MIR allocation function=%s spills=%d "
                "hl=%d de=%d bc=%d iy=%d moves=%d phi-moves=%d "
                "return-base=%d return-size=%d locals=%d\n",
                mir.name, allocation.spills,
                allocation.colors[MIR_COLOR_HL],
                allocation.colors[MIR_COLOR_DE],
                allocation.colors[MIR_COLOR_BC],
                allocation.colors[MIR_COLOR_IY],
                allocation.operand_moves + allocation.fixed_moves,
                allocation.phi_moves, mir.return_type & 15,
                type_size(mir.return_type), mir_effective_local_bytes());
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
        if (insn->inline_temp_id != 0)
            fprintf(stderr, " {it%d}", insn->inline_temp_id);
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
            static const char *homes[] = { "hl", "de", "bc", "iy",
                                            "hl:de", "bc:iy" };
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

    free(defined);
    /* Transfer ownership to the persistent mir.live_in/mir.live_out fields
     * (Item 20d) instead of freeing here, so a later selector's acceptance
     * probe can reuse this exact liveness data. mir_end_function() frees
     * these once every selector has run. */
    free(mir.live_in);
    free(mir.live_out);
    mir.live_in = live_in;
    mir.live_out = live_out;
    return errors == 0;
}

const struct MirInsn *mir_definition(int value)
{
    int i;

    for (i = 0; i < mir.count; ++i)
        if (mir.insns[i].dst == value)
            return &mir.insns[i];
    return NULL;
}
