/**
 * @file dcc_mir.h
 * @brief Declares the frontend-facing lifecycle and capture API for MIR.
 *
 * @par Role
 * Parser, statement, initializer, scope, and debug-metadata code call this
 * interface to describe one function without depending on MIR internals.
 * Implementations live primarily in dcc_mir.c; mir_end_function() hands the
 * completed function to candidate selection in dcc_mir_select.c.
 *
 * @par Boundary
 * IR types, analysis helpers, allocation state, and emitter contracts are
 * private to dcc_mir_internal.h.
 */
#ifndef DCC_MIR_H
#define DCC_MIR_H

struct AstNode;
struct Sym;
struct FieldDef;

/* Function lifecycle. */
void mir_begin_function(const char *name, const char *assembly_name,
						int sink_purpose, int has_vla,
						int local_bytes, int implicit_zero_return);
int mir_is_active(void);

/* Statements, declarations, and initializer events. */
void mir_capture_stmt(const struct AstNode *stmt);
void mir_begin_declaration(const struct AstNode *node);
void mir_end_declaration(void);
int mir_instruction_checkpoint(void);
void mir_neutralize_since(int checkpoint);
void mir_set_initializer_target(struct Sym *symbol);
void mir_set_vla_target(struct Sym *symbol);
void mir_capture_initializer(const struct AstNode *expr);
void mir_capture_struct_initializer(struct Sym *target,
									const struct AstNode *expr);
void mir_capture_bitfield_init_expr(struct Sym *symbol, int offset,
									const struct FieldDef *field,
									const struct AstNode *expr);
void mir_capture_vla_save(int offset);
void mir_capture_vla_restore(int offset);
void mir_capture_init_constant(struct Sym *symbol, int offset, int type,
							   long value);
void mir_capture_init_char_array(struct Sym *symbol, const char *bytes,
								 int length);
void mir_set_init_expression_target(struct Sym *symbol, int offset, int type);
void mir_begin_compound_literal(struct Sym *symbol);
void mir_end_compound_literal(struct Sym *symbol);

/* Metadata-only scope, control-flow, and label replay. */
void mir_begin_scope_replay(void);
void mir_end_scope_replay(void);
void mir_begin_flow_replay(void);
void mir_end_flow_replay(void);
void mir_begin_label_replay(const char *name);
void mir_end_label_replay(void);

/* Declared-symbol facts used while resolving lowered values. */
void mir_note_declared_symbol(struct Sym *symbol);
void mir_note_declared_alias(const char *source_name, struct Sym *symbol);
void mir_note_narrowed_for_counter(void);

/* Debug records preserved for the selected generated candidate. */
int mir_capture_debug_location(const char *file, int line);
int mir_capture_debug_variable(
    const char *function, const struct Sym *symbol, int end);
int mir_capture_debug_function_end(
    const char *assembly_name, const char *source_name);

/* Finalize one function, then enforce translation-unit completeness. */
void mir_end_function(void);
void mir_finish_translation_unit(void);

#endif
