/* dcc_mir.h - analysis-only virtual-register machine IR prototype.
 *
 * The current milestone records a function's statement ASTs before physical
 * Z80 register assignment, builds CFG successors and verifies virtual-value
 * liveness. It is enabled only by DCC_MIR_REPORT and does not affect codegen.
 */
#ifndef DCC_MIR_H
#define DCC_MIR_H

struct AstNode;

void mir_begin_function(const char *name, int sink_purpose);
void mir_capture_stmt(const struct AstNode *stmt);
void mir_end_function(void);

#endif
