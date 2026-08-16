/**
 * @file dcc_preproc_internal.h
 * @brief Declares the focused cross-module preprocessing contract.
 *
 * @par Role
 * Exposes macro-table lookup and mutation, preprocessing-expression
 * evaluation, and replacement-comment stripping to the driver, lexer,
 * diagnostics, and directive evaluator.
 *
 * @par Key entry points
 * find_define(), add_define_ex(), remove_define(), pp_eval_simple_expr(), and
 * strip_macro_replacement_comments().
 *
 * @par Boundary
 * The public compiler-wide lexer and parser APIs remain in dcc.h; private
 * macro-expansion state stays in dcc_preproc.c and dcc_pp_expr.c.
 */
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