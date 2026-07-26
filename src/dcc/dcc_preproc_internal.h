/* dcc_preproc_internal.h - macro-table and #if evaluator contract shared by
 * the driver, preprocessor, diagnostics, and dcc_pp_expr.c. */
#ifndef DCC_PREPROC_INTERNAL_H
#define DCC_PREPROC_INTERNAL_H

#include "dcc.h"

int find_define(const char *name);
void add_define_ex(const char *name, const char *value, int is_func, int nargs,
                   char params[8][32]);
void add_define(const char *name, const char *value);
void remove_define(const char *name);
int pp_eval_simple_expr(const char *s);
void strip_macro_replacement_comments(char *s);

#endif