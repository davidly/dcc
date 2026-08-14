/* dcc_mir.h - typed virtual-register MIR capture and generated backend. */
#ifndef DCC_MIR_H
#define DCC_MIR_H

struct AstNode;
struct Sym;

void mir_begin_function(const char *name, int sink_purpose, int has_vla,
						int local_bytes, int implicit_zero_return);
void mir_capture_stmt(const struct AstNode *stmt);
void mir_begin_declaration(const struct AstNode *node);
void mir_end_declaration(void);
void mir_set_initializer_target(struct Sym *symbol);
void mir_set_vla_target(struct Sym *symbol);
void mir_capture_initializer(const struct AstNode *expr);
void mir_capture_struct_initializer(struct Sym *target,
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
void mir_begin_scope_replay(void);
void mir_end_scope_replay(void);
void mir_begin_flow_replay(void);
void mir_end_flow_replay(void);
void mir_begin_label_replay(const char *name);
void mir_end_label_replay(void);
void mir_note_declared_symbol(struct Sym *symbol);
void mir_note_declared_alias(const char *source_name, struct Sym *symbol);
void mir_note_narrowed_for_counter(void);
int mir_capture_debug_location(const char *file, int line);
int mir_capture_debug_variable(
    const char *function, const struct Sym *symbol, int end);
int mir_capture_debug_function_end(
    const char *assembly_name, const char *source_name);
void mir_begin_prelegacy_scheduled_attempt(void);
int mir_end_prelegacy_scheduled_attempt(void);
void mir_report_buffered_selection(FILE *stream, const char *name);
void mir_end_function(void);
void mir_finish_translation_unit(void);

#endif
