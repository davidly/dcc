/*
 * dcc_ast.h - function-local abstract syntax tree for dcc.
 *
 * dcc now lowers function bodies through a function-local AST.  The parser
 * builds one statement/expression tree at a time, then hands it to the AST
 * codegen walker.  Nothing outside a function is represented here (top-level
 * declarations, types, structs and the preprocessor keep using the existing
 * tables) - hence "function-local".
 *
 * Design constraints:
 *   - C89 source (the host compiler builds dcc as -std=c89).
 *   - Must be able to drive the shared emit_* helpers,
 *     so the AST records exactly the information those helpers need (resolved
 *     struct Sym*, dcc type codes, operator token kinds, folded literals).
 *   - Arena-allocated and reset per function so there is no per-node free and
 *     no long-lived heap growth across a translation unit.
 *
 * The node payload is a deliberately small, fixed struct with a handful of
 * generic child slots (a..d) plus an optional child LIST (for compound-
 * statement bodies and call argument lists).  This keeps construction cheap
 * and avoids a sprawling tagged union while still expressing every C89
 * construct dcc accepts, including its C99 extensions (for-init declarations,
 * mid-block declarations, // comments are a lexer concern and need no node).
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
    AST_EMPTY           /* lone ';'                                        */
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
    int line;               /* source line, for diagnostics                */
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
void ast_emit_decl_span(const struct AstNode *n);
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
 * to reserve the rolling-counter's frame slot) and ast_gen_for_stmt
 * (codegen time, to re-extract the same constants). */
int ast_for_mod_fill_supported(const struct AstNode *n, struct Sym **out_arr,
                                      long *out_init, long *out_base,
                                      long *out_mod, const char **out_ivar_name);

/* General-purpose expression predicates (dcc_ast_gen_support.c): does the
 * subtree reference a given identifier anywhere, and does it contain any
 * observable side effect anywhere. Used together to prove a candidate
 * lvalue address is loop-invariant (see ast_for_hoist_lvalue_addr_supported). */
int ast_expr_references_ident(const struct AstNode *n, const char *name);
int ast_expr_has_side_effects(const struct AstNode *n);

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
struct Sym *ast_sizeof_whole_vla_sym(const struct AstNode *n);

/* Detects a for-loop whose whole body is one assignment to an array-element
 * lvalue whose address is provably the same on every iteration (see
 * dcc_ast_gen_support.c for the full shape and rationale). Callable from
 * ast_gen_for_stmt to decide whether to hoist the address computation. */
int ast_for_hoist_lvalue_addr_supported(const struct AstNode *n,
                                               const char **out_ivar_name,
                                               const struct AstNode **out_lhs,
                                               int *out_val_type);

/* Extracts a for-loop's induction-variable name and its body's assignment
 * rhs, for the caller to scan for row-invariant 2D array reads worth
 * hoisting (see dcc_ast_gen_stmt.c's ast_hoist_row_invariant_2d_reads).
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
 * tests/cint.c's run() dispatch loop is the motivating case). Callable from
 * ast_gen_for_stmt to decide whether to cache that value once before the
 * loop instead of re-fetching it every iteration. */
int ast_for_hoist_global_member_value_supported(const struct AstNode *n,
                                                  const struct AstNode **out_member,
                                                  int *out_val_type);

/* Copy a NUL-terminated string into the arena. */
char *ast_arena_strdup(struct AstArena *ar, const char *s);

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
void ast_gen_stmt(const struct AstNode *n);
int ast_stmt_has_reentry_label(const struct AstNode *n);
int ast_stmt_exits(const struct AstNode *n);
int ast_last_statement_exits(void);

/* Reset the per-statement support-probe caches (ast_gen_supported and
 * friends memoize by AST node pointer within a single statement's checks;
 * arena nodes are reused across statements, so the cache must be dropped
 * before probing a freshly-built one - see dcc_ast_gen_support.c). Called
 * from dcc_stmt.c's gen_compound before ast_stmt_supported on a new node,
 * as well as from within the ast_gen_stmt* files themselves. */
void ast_support_cache_begin(void);

/* Pure-AST emission of a declaration initializer's assignment-expression.
 * Builds into the isolated g_ast_init_arena; fatal on unsupported constructs. */
void ast_emit_init_expr(void);
void ast_emit_struct_init_expr_assign(struct Sym *s);

/* Statement hook.  Called from gen_statement to build the next statement from
 * the token stream and emit it from the AST.  Returns 0 only in scanner/debug
 * paths that deliberately bypass AST codegen. */
int ast_try_emit_statement(void);

/* For scan_function_body's frame-sizing scan (dcc_func.c): build and replay
 * the for-statement at the current token position through the same AST
 * builder/emitter the real codegen pass uses, with output redirected to a
 * throwaway sink. Keeps frame sizing - and any AST-level for-loop fast path
 * that reserves extra frame space - automatically in sync with the real
 * pass, by construction, instead of needing a hand-written parallel token
 * scanner kept in sync by hand. Returns 1 on success (tokens consumed,
 * locals sized); 0 if the AST builder declined (the real pass will report
 * this properly; the caller here should not treat 0 as fatal). */
int ast_scan_for_stmt(void);

#endif /* DCC_AST_H */
