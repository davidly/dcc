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

void ast_build_init(void);
struct AstNode *ast_build_expr(struct AstArena *ar);  /* full expression       */
struct AstNode *ast_build_assign_expr(struct AstArena *ar); /* no-comma expr    */
struct AstNode *ast_build_stmt(struct AstArena *ar);  /* one statement, or NULL */
const char *ast_kind_name(int kind);
void ast_dump(const struct AstNode *n, int depth);

/* ------------------------------------------------------------------------- *
 * AST-driven code generation.
 *
 * AST codegen is enabled by default.  DCC_AST_GEN=2 keeps verbose emit reports.
 * ------------------------------------------------------------------------- */
extern int g_ast_gen_enabled;       /* 0 internal suppress, 1 emit, 2 report */

int ast_gen_supported(const struct AstNode *n);
void ast_gen_expr(const struct AstNode *n);   /* emit; sets g_expr_type        */

/* Pure-AST emission of a declaration initializer's assignment-expression.
 * Builds into the isolated g_ast_init_arena; fatal on unsupported constructs. */
void ast_emit_init_expr(void);

/* Statement hook.  Called from gen_statement to build the next statement from
 * the token stream and emit it from the AST.  Returns 0 only in scanner/debug
 * paths that deliberately bypass AST codegen. */
int ast_try_emit_statement(void);

#endif /* DCC_AST_H */
