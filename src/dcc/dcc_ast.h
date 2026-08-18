/**
 * @file dcc_ast.h
 * @brief Defines dcc's function-local AST and its shared frontend API.
 *
 * @par Role
 * Declares arena storage, node kinds and payloads, constructors, parsers,
 * semantic classifiers, statement analysis, and metadata entry points. The
 * parser builds one statement or expression tree at a time; production
 * function semantics are captured into MIR, while post-parse AST traversal
 * preserves declarations, scopes/VLAs, inline temporaries, strings, labels,
 * diagnostics, and debug events.
 *
 * @par Design
 * Nodes are arena-allocated and use fixed generic child slots plus optional
 * child lists. They carry resolved symbols, dcc type codes, operators, folded
 * literals, and source locations needed by MIR lowering and metadata replay.
 * Top-level declarations, types, structs, and preprocessing remain outside
 * this function-local representation.
 *
 * @par Module map
 * - dcc_ast.c: arena ownership, node construction, and small tree queries.
 * - dcc_ast_build.c: token-to-AST expression and statement parsing.
 * - dcc_ast_stmt_meta.c: statement gating, MIR capture, sizing, exit analysis.
 * - dcc_ast_metadata.c: non-emitting declaration/scope/debug metadata replay.
 * - dcc_ast_gen.c: type, lvalue, pointer, member, and index classifiers.
 * - dcc_ast_gen_support.c: support gates, constant folds, structural proofs.
 * - dcc_ast_gen_expr.c: initializer capture, inline metadata, expression
 *   helpers.
 * - dcc_ast_gen_cond.c: statement/condition gates and branch-shape helpers.
 * - dcc_ast_gen_internal.h: private contract shared by the split modules.
 *
 * @par Boundary
 * The dcc_ast_gen* helpers do not provide a production body-codegen fallback.
 * dcc_mir.h exposes function capture; dcc_mir_select.c owns generated
 * candidate selection.
 */
#ifndef DCC_AST_H
#define DCC_AST_H

#include <stddef.h>

/* struct Sym is defined in dcc.h; the AST only needs to point at resolved
 * symbols, so a forward declaration keeps this header independent of include
 * order. */
struct Sym;

/* ------------------------------------------------------------------------- *
 * Node kinds.
 * ------------------------------------------------------------------------- */
enum AstKind {
    AST_NONE = 0,

    /* ---- expressions ---- */
    AST_INT_LIT,        /* integer / char constant: ival, type            */
    AST_FLOAT_LIT,      /* floating constant: uval = IEEE bits, type       */
    AST_STR_LIT,        /* string literal: str_index into the string table */
    AST_IDENT,          /* object / function reference: sym, type          */
    AST_CALL,           /* a = callee expr; list = argument expressions    */
    AST_INDEX,          /* a[b]: a = base, b = index                       */
    AST_MEMBER,         /* a.field / a->field: a = base, op = '.' or ARROW */
    AST_UNARY,          /* prefix op on a: - ! ~ * & + and prefix ++/--    */
    AST_POSTFIX,        /* postfix a++ / a--: op = TOK_INC / TOK_DEC       */
    AST_BINARY,         /* a op b: + - * / % & | ^ << >> < > etc.          */
    AST_LOGAND,         /* a && b (short-circuit; separate for control)    */
    AST_LOGOR,          /* a || b (short-circuit)                          */
    AST_ASSIGN,         /* a op= b (op '=' for plain assignment)           */
    AST_COND,           /* a ? b : c                                       */
    AST_CAST,           /* (type)a: type = target type, a = operand        */
    AST_COMPOUND_LITERAL, /* (type){...}: sym = hidden auto object          */
    AST_COMMA,          /* a , b                                           */
    AST_SIZEOF_EXPR,    /* sizeof a                                        */
    AST_SIZEOF_TYPE,    /* sizeof(type): type = operand type               */

    /* ---- statements ---- */
    AST_EXPR_STMT,      /* a = expression (may be NULL for empty ';')      */
    AST_COMPOUND,       /* { ... }: list = ordered child statements/decls  */
    AST_DECL,           /* local declaration: aux = lexer span replayed       */
                        /* through declaration codegen (offset parity)        */
    AST_IF,             /* a = cond, b = then, c = else (or NULL)          */
    AST_WHILE,          /* a = cond, b = body                              */
    AST_DOWHILE,        /* b = body, a = cond                              */
    AST_FOR,            /* a = init, b = cond, c = post, d = body          */
    AST_SWITCH,         /* a = control expr, b = body                      */
    AST_CASE,           /* ival = case constant; b = following statement   */
    AST_DEFAULT,        /* b = following statement                         */
    AST_RETURN,         /* a = value expr (or NULL)                        */
    AST_BREAK,
    AST_CONTINUE,
    AST_GOTO,           /* sym/str_index identifies the target label       */
    AST_LABEL,          /* user label: b = following statement             */
    AST_EMPTY,          /* lone ';'                                        */
    AST_DIVMOD_CALL     /* compiler-synthesized only (never built by the    */
                        /* parser - see ast_divmod_fuse_compound): a = div-  */
                        /* idend, b = divisor (both bare int idents), sym =  */
                        /* quotient temp, sval = remainder temp's name, ival */
                        /* = 1 for signed (__sdivmod) / 0 for unsigned       */
                        /* (__udivmod). Evaluates both operands, makes one   */
                        /* fused call, stores quotient and remainder into    */
                        /* their own temps for the statements that used to   */
                        /* separately compute a%b and a/b to read instead.   */
};

/* ------------------------------------------------------------------------- *
 * Node.
 * ------------------------------------------------------------------------- */
struct AstNode {
    int kind;               /* enum AstKind                                */
    int type;               /* dcc type code of an expression's result     */
    int op;                 /* operator token kind (unary/binary/assign)   */
    long ival;              /* integer/char literal, case value, label id  */
    unsigned long uval;     /* float bits or other unsigned payload        */
    int str_index;          /* string-literal / label-name table index     */
    struct Sym *sym;        /* resolved symbol for IDENT/DECL/CALL/GOTO     */
    char *sval;             /* identifier / string / member-name text       */

    struct AstNode *a;      /* generic child 0 (see per-kind comments)      */
    struct AstNode *b;      /* generic child 1                              */
    struct AstNode *c;      /* generic child 2                              */
    struct AstNode *d;      /* generic child 3                              */

    struct AstNode **list;  /* ordered children (compound body, call args)  */
    int list_len;
    int list_cap;

    void *aux;              /* AST_DECL: opaque lexer-span snapshot          */

    int peek_type;          /* AST_BINARY: peek_simple_unary_type() of the   */
                            /* rhs, captured at build time so the walker     */
                            /* computes the arithmetic common-type choice    */
    int operand_type;       /* AST_BINARY: effective common/left computation */
                            /* type; distinct from int comparison result      */
    char *file;             /* arena-owned source filename                  */
    int line;               /* source line, for diagnostics                */
    char *end_file;         /* AST_COMPOUND closing-brace source filename  */
    int end_line;           /* AST_COMPOUND closing-brace source line      */
};

#define AST_INT_UVAL_CHARLIT       1UL
#define AST_INT_UVAL_PLAIN_DECIMAL 2UL

/* ------------------------------------------------------------------------- *
 * Arena.  Nodes are bump-allocated from a list of large blocks; the whole
 * arena is reset (not individually freed) when a function has been emitted.
 * ------------------------------------------------------------------------- */
struct AstArena {
    char **blocks;          /* allocated block pointers                    */
    int nblocks;            /* number of live blocks                       */
    int cap_blocks;         /* capacity of the blocks array                */
    size_t block_size;      /* bytes per block                             */
    size_t used;            /* bytes used in the current (last) block      */
};

struct AstCompoundLitSpan {
    long posi;
    long tok_start_pos;
    int line_no;
    int tok_line;
    struct Token tok;
};

/* ---- arena lifecycle ---- */
void ast_arena_init(struct AstArena *ar);
void *ast_arena_alloc(struct AstArena *ar, size_t n);
void ast_arena_reset(struct AstArena *ar);   /* keep first block, drop rest */

/* ---- node construction (all allocate from `ar`) ---- */
struct AstNode *ast_new(struct AstArena *ar, int kind);

/* Re-emit a captured local-declaration span through declaration codegen so the
 * local symbol table / frame offsets are rebuilt exactly as the frame-sizing
 * scan built them.  Defined in dcc_ast_build.c. */
void ast_replay_decl_span(const struct AstNode *n);
void ast_scan_decl_span(const struct AstNode *n);
void ast_replay_compound_literal(const struct AstNode *n);
void ast_process_expr_metadata(const struct AstNode *n);
void ast_validate_expr_symbols(const struct AstNode *n);
int ast_process_inline_call_metadata(
    const struct AstNode *call, int result_dead);
void ast_process_stmt_metadata(const struct AstNode *n);

/* Non-emitting counterpart used by dcc_func.c's inliner eligibility scan:
 * seeks the lexer to an AST_DECL span's start (saving the caller's own
 * position for ast_decl_span_restore) without running declaration codegen,
 * so the caller can speculatively re-parse just the declarator + an
 * initializer expression (e.g. via ast_build_expr) on its own. struct
 * DeclSpan itself stays private to dcc_ast_build.c. Defined there. */
struct DeclSpanSave {
    long posi;
    long tok_start_pos;
    int line_no;
    int tok_line;
    struct Token tok;
};
int ast_decl_span_seek(const struct AstNode *n, struct DeclSpanSave *save);
void ast_decl_span_restore(const struct DeclSpanSave *save);
struct AstNode *ast_int_lit(struct AstArena *ar, long value, int type);
struct AstNode *ast_float_lit(struct AstArena *ar, unsigned long bits, int type);
struct AstNode *ast_unary(struct AstArena *ar, int op, struct AstNode *operand,
                          int type);
struct AstNode *ast_binary(struct AstArena *ar, int kind, int op,
                           struct AstNode *lhs, struct AstNode *rhs, int type);
struct AstNode *ast_assign(struct AstArena *ar, int op, struct AstNode *lhs,
                           struct AstNode *rhs, int type);
struct AstNode *ast_cond(struct AstArena *ar, struct AstNode *cnd,
                         struct AstNode *then_e, struct AstNode *else_e,
                         int type);
struct AstNode *ast_cast(struct AstArena *ar, int type, struct AstNode *operand);
struct AstNode *ast_call(struct AstArena *ar, struct AstNode *callee, int type);

/* Append a child to a node's ordered list (compound body / call args). */
void ast_list_push(struct AstArena *ar, struct AstNode *parent,
                   struct AstNode *child);

/* Detects the "cyclic byte fill" for-loop idiom (see dcc_ast_gen_support.c
 * for the full shape). Callable from both ast_build_for_stmt (build time,
 * to reserve the rolling-counter's frame slot) and metadata planning. */
int ast_for_mod_fill_supported(const struct AstNode *n, struct Sym **out_arr,
                                      long *out_init, long *out_base,
                                      long *out_mod, const char **out_ivar_name);

/* General-purpose expression predicates (dcc_ast_gen_support.c): does the
 * subtree reference a given identifier anywhere, and does it contain any
 * observable side effect anywhere. Used together to prove a candidate
 * lvalue address is loop-invariant (see ast_for_hoist_lvalue_addr_supported). */
int ast_expr_references_ident(const struct AstNode *n, const char *name);
int ast_expr_has_side_effects(const struct AstNode *n);

/* True if `n` provably yields exactly 0 or 1 on its own: a bool-typed
 * subexpression, a 0/1 integer literal, `!`, a comparison operator, `&&`/
 * `||`, or a cast to bool - the same proof dcc_ast_gen_expr.c trusts
 * elsewhere for an RHS being stored into a bool. Used by dcc_func.c to gate
 * inlining a bool-returning function's return expression, since splicing it
 * directly at a call site bypasses AST_RETURN's own 0/1 canonicalization. */
int ast_expr_yields_bool01(const struct AstNode *n);

/* Byte-memory word-packing idiom (mem_get_word/mem_set_word-shaped code -
 * see dcc_ast_gen_support.c for the full rationale): `arr[E] | (arr[E+1]
 * << 8)` for a read, `arr[E] = lo; arr[E+1] = hi;` for a write, where E is
 * a non-trivial shared index expression currently recomputed twice. */
int ast_index_exprs_structurally_equal(const struct AstNode *a, const struct AstNode *b);
int ast_index_expr_is_plus_one(const struct AstNode *base_expr, const struct AstNode *plus_one);
const struct AstNode *ast_byte_pair_word_read_match(const struct AstNode *n);
int ast_byte_pair_word_write_match(const struct AstNode *s1, const struct AstNode *s2,
                                   const struct AstNode **out_lo, const struct AstNode **out_s2_assign);

/* Recursive, side-effect-free static type inference for an expression node -
 * originally written for sizeof, general-purpose enough to reuse anywhere a
 * node's result type is needed before/without running its codegen (e.g.
 * deciding whether a multiply subexpression is float-valued for fusion). */
int ast_expr_type_for_sizeof(const struct AstNode *n);

/* Compute the constant byte size of a `sizeof expr` operand, and (separately)
 * detect when that operand is a whole variable-length array.  Both resolve
 * symbols by name, so they must be called at EMIT time - when a nested-block
 * declaration's span has already run and its symbol is in scope - not at
 * AST-build time, where such a symbol is not yet in the local table. */
int ast_sizeof_expr_value(const struct AstNode *n);
struct FieldDef *ast_unique_field_by_name(const char *name);
int ast_va_arg_deref_type(const struct AstNode *n, int *out_type);
int ast_index_composite_elem_type(const struct AstNode *n, int *out_elem);
int ast_pointer_expr_type(const struct AstNode *n, int *out_type,
                          int *out_no_deref);
int ast_index_array_row_ptr_type(const struct AstNode *n, int *out_type);
struct Sym *ast_sizeof_whole_vla_sym(const struct AstNode *n);

/* Detects a for-loop whose whole body is one assignment to an array-element
 * lvalue whose address is provably the same on every iteration (see
 * dcc_ast_gen_support.c for the full shape and rationale). */
int ast_for_hoist_lvalue_addr_supported(const struct AstNode *n,
                                               const char **out_ivar_name,
                                               const struct AstNode **out_lhs,
                                               int *out_val_type);

/* Extracts a for-loop's induction-variable name and its body's assignment
 * rhs, for the caller to scan for row-invariant 2D array reads worth
 * hoisting (see dcc_ast_stmt_meta.c).
 * Unlike ast_for_hoist_lvalue_addr_supported, this says nothing about the
 * lhs - it fires whether or not the lhs address is itself hoistable. */
int ast_for_rhs_hoist_scan_supported(const struct AstNode *n,
                                            const char **out_ivar_name,
                                            const struct AstNode **out_rhs);

/* Detects a for-loop whose body's first statement reads a global's member
 * value that is provably invariant across the whole file (via the
 * dcc_global_scan.c whole-file write scan), even though the rest of the
 * loop body is full of calls ordinary side-effect analysis can't see
 * through (see dcc_ast_gen_support.c for the full shape and rationale -
 * tests/cint.c's run() dispatch loop is the motivating case). */
int ast_for_hoist_global_member_value_supported(const struct AstNode *n,
                                                  const struct AstNode **out_member,
                                                  int *out_val_type);

/* General loop-invariant code motion for a for-loop's body (dcc_licm.c):
 * hoists every pure scalar-arithmetic subexpression that doesn't depend on
 * anything the loop's condition/increment/body modifies, computing it once
 * before the loop into a fresh compiler-temp local instead of recomputing it
 * every iteration. Unlike the three hoists above, this is not limited to a
 * single-statement body. Returns a rewritten copy of for_node->d to use in
 * its place, or NULL if nothing qualifies (use for_node->d unchanged). */
struct AstNode *ast_licm_plan_invariants(const struct AstNode *for_node);
void ast_plan_for_metadata(const struct AstNode *for_node);

/* Set of names assigned, incremented/decremented, or address-taken anywhere
 * in a scanned subtree; ->overflowed means "assume everything is modified"
 * (a call, a nested loop/switch/goto, or any other construct this doesn't
 * specifically recognize was seen - see licm_scan_modified in dcc_licm.c).
 * LICM declines every candidate when the scan overflows. */
#define LICM_MAX_MODIFIED_NAMES 32
struct LicmModifiedNames {
    const char *names[LICM_MAX_MODIFIED_NAMES];
    int count;
    int overflowed;
};
void licm_scan_modified(const struct AstNode *n, struct LicmModifiedNames *mod);

/* Detects two ADJACENT statements in a compound block whose list one
 * contains `X % Y` and the other `X / Y` (either order, bare-identifier
 * operands only in v1), and rewrites them to share one DCCRTL.MAC
 * __udivmod/__sdivmod call instead of each independently calling __modu/
 * __divu (or __mods/__divs) - see dcc_ast_gen_support.c for the full shape/
 * rationale/safety argument (tests/e.c's `a[n] = x % n; x = ...+ x/n;` is
 * the motivating case, found via dccprof profiling). Unlike the for-loop-
 * specific hoists above, this applies to any compound metadata walk.
 * Returns a rewritten copy of the compound to use in its place, or NULL if
 * nothing qualifies (use the original compound unchanged). */
struct AstNode *ast_divmod_fuse_compound(const struct AstNode *n);

/* Copy a NUL-terminated string into the arena. */
char *ast_arena_strdup(struct AstArena *ar, const char *s);
char *ast_arena_memdup(struct AstArena *ar, const char *s, int len);

/* ------------------------------------------------------------------------- *
 * AST builder + debug dump.
 *
 * ast_build_init() enables AST construction by default.  DCC_AST_BUILD=2 dumps
 * built trees to stderr for debugging.
 * ------------------------------------------------------------------------- */
extern int g_ast_build_enabled;     /* 0 internal suppress, 1 build, 2 dump  */
extern struct AstArena g_ast_arena; /* shared function-local build arena      */
extern struct AstArena g_ast_init_arena; /* isolated decl-initializer arena   */
extern struct AstArena g_ast_inline_arena; /* persistent inline function arena */

void ast_build_init(void);
struct AstNode *ast_build_expr(struct AstArena *ar);  /* full expression       */
struct AstNode *ast_build_assign_expr(struct AstArena *ar); /* no-comma expr    */
struct AstNode *ast_build_stmt(struct AstArena *ar);  /* one statement, or NULL */
const char *ast_kind_name(int kind);
void ast_dump(const struct AstNode *n, int depth);

/* ------------------------------------------------------------------------- *
 * AST-driven code generation.
 *
 * AST codegen is the compiler's only codegen path.  Set DCC_AST_REPORT to log
 * per-statement emit/unsupported diagnostics to stderr.
 * ------------------------------------------------------------------------- */
int ast_gen_supported(const struct AstNode *n);
void ast_gen_expr(const struct AstNode *n);   /* emit; sets g_expr_type        */
int ast_stmt_supported(const struct AstNode *n);
int ast_stmt_has_reentry_label(const struct AstNode *n);
int ast_stmt_exits(const struct AstNode *n);
int ast_last_statement_exits(void);

/* Reset the per-statement support-probe caches (ast_gen_supported and
 * friends memoize by AST node pointer within a single statement's checks;
 * arena nodes are reused across statements, so the cache must be dropped
 * before probing a freshly-built one - see dcc_ast_gen_support.c). */
void ast_support_cache_begin(void);

/* Pure-AST emission of a declaration initializer's assignment-expression.
 * Builds into the isolated g_ast_init_arena; fatal on unsupported constructs. */
void ast_emit_init_expr(void);
void ast_emit_struct_init_expr_assign(struct Sym *s);

/* Statement hook.  Called from gen_statement to build the next statement from
 * the token stream and emit it from the AST.  Returns 0 only in scanner/debug
 * paths that deliberately bypass AST codegen. */
int ast_process_statement(void);
void ast_record_debug_location(const char *file, int line);

/* For scan_function_body's frame-sizing scan (dcc_func.c): build and replay
 * the statement at the current token position through the AST builder and
 * non-emitting sizing walker. Returns 1 on success; 0 if the builder declined.
 */
int ast_scan_for_stmt(void);

#endif /* DCC_AST_H */
