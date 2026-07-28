/* dcc_mir.h - typed virtual-register MIR capture and transactional backend. */
#ifndef DCC_MIR_H
#define DCC_MIR_H

struct AstNode;
struct Sym;

void mir_begin_function(const char *name, int sink_purpose, int has_vla);
void mir_capture_stmt(const struct AstNode *stmt);
void mir_begin_declaration(void);
void mir_end_declaration(void);
void mir_set_initializer_target(struct Sym *symbol);
void mir_set_vla_target(struct Sym *symbol);
void mir_capture_initializer(const struct AstNode *expr);
void mir_capture_vla_save(int offset);
void mir_capture_vla_restore(int offset);
void mir_end_function(void);

#endif
