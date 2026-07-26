/*
 * dcc.h - foundational shared contract for the modular dcc compiler.
 *
 * dcc uses C89 as its base language plus selected C99/C11 features suitable
 * for CP/M/Z80, and emits Z80 assembly for the M80-compatible toolchain.
 * Function bodies lower through a typed,
 * function-local AST. This header holds broadly shared target constants, core
 * records, state declarations, and cross-module APIs; narrower contracts live
 * in dcc_ast_gen_internal.h, dcc_preproc_internal.h, and
 * dcc_regalloc_internal.h.
 *
 * This foundational contract includes:
 *   1. System headers used across the compiler.
 *   2. Capacity / translation-limit macros (MAX_*).
 *   3. Type-kind, storage-class and token-kind constants.
 *   4. The core record types (Token, Sym, Def, AsmName, TypeDef, FieldDef,
 *      StructDef, ConstVal, ByteOperand).
 *   5. `extern` declarations for the shared global state (defined once in
 *      dcc_state.c).
 *   6. Broadly used cross-module functions, grouped by owning module.
 *
 * Module .c files and their responsibilities:
 *   dcc_state.c     definitions of cross-module compiler state
 *   dcc_asmname.c   C identifier -> M80 assembler symbol mapping
 *   dcc_diag_emit.c diagnostics, allocation, emit primitives, char input
 *   dcc_preproc.c   preprocessor + macro engine + lexer (next_token)
 *   dcc_pp_expr.c   preprocessor #if/#elif expression evaluator
 *   dcc_types.c     type system, struct/union/typedef parsing
 *   dcc_constexpr.c integer constant-expression parser
 *   dcc_symbols.c   symbol tables + symbol-access codegen + EXTRN
 *   dcc_fold.c      constant folding + sizeof/offsetof
 *   dcc_expr.c      declarator parsing + low-level expression emit helpers
 *   dcc_cmp.c       comparison + conditional-branch codegen
 *   dcc_ops.c       binary-operator / arithmetic codegen
 *   dcc_assign.c    float constants + global byte-array address helper
 *   dcc_stmt_fast.c in-place increment/decrement address helper
 *   dcc_decl.c      local declaration + initializer codegen
 *   dcc_stmt.c      token-to-AST statement bridge + switch helpers
 *   dcc_func.c      functions/top-level declarations + inline-body capture
 *   dcc_global_init.c file-scope object initializer parsing (record path)
 *   dcc_regalloc.c  speculative no-IX / BC-register-allocation codegen
 *   dcc_loop_regalloc.c loop-scoped BC register allocation
 *   dcc_global_scan.c whole-file lexical global-write/address scan
 *   dcc_array_narrow.c conservative byte-narrowing proof
 *   dcc_licm.c      loop-invariant code motion and loop-local CSE
 *   dcc_ast*.c      function-local AST storage, building, gates and emission
 *   dcc_data.c      data-section emission
 *   dcc.c           driver: file I/O, #include, CLI, and main()
 *
 * Examples of state intentionally kept private to its owner:
 *   - pp_expr_p / pp_expr_depth        (dcc_pp_expr.c: #if expression cursor)
 *   - include_dirs / num_include_dirs  (dcc.c: include search path)
 */
#ifndef DCC_H
#define DCC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/*
 * Portable null-device path for throwaway fopen() sinks (used to suppress
 * output during speculative/frame-sizing AST replay passes - see
 * ast_scan_for_stmt and the AST_FOR/AST_COMPOUND probes in
 * dcc_ast_gen_cond.c).  "/dev/null" does not exist on native Windows: MSVC's
 * fopen() there fails on it (it looks for a "dev" subdirectory), silently
 * leaving the caller's `g_emit_sink.stream` pointed at the real output file instead of a
 * sink - so the replay's speculative emission leaks into real output.  The
 * Windows null device is "NUL".
 */
#ifdef _WIN32
#define DCC_NULL_DEVICE "NUL"
#else
#define DCC_NULL_DEVICE "/dev/null"
#endif

/* ------------------------------------------------------------------------- *
 * Capacity / translation-limit macros.
 * ------------------------------------------------------------------------- */

/*
 * Maximum stored length (including the terminating NUL) of a single token's
 * spelling: identifiers, numeric tokens, and an individual string-literal
 * token before concatenation.  C89 (ISO 9899:1990) section 2.2.4.1
 * "Translation limits" requires a conforming implementation to accept at
 * least 509 characters in a (logical, post-concatenation) string literal, so
 * this must exceed 509; the previous value of 128 was below that minimum and
 * silently truncated long literals.  Adjacent string literals are joined in a
 * separately grown buffer (read_adjacent_string_literals_ex), so the limit on
 * the concatenated result is not bounded by this constant.
 */
#define MAX_TOK_TEXT   512
#define MAX_SYMS       4096
#define MAX_LOCALS     1024
#define MAX_STRINGS    2048
#define MAX_DEFINES    512
#define MAX_MACRO_TEXT 8192

/* Function-like macro invocation argument capture.  Each argument's text is
 * stored raw, including the newlines and indentation of a multi-line call, so
 * the per-argument length must be generous: a small cap silently drops the
 * trailing argument(s) of an indented multi-line macro invocation (a
 * multi-line variadic __VA_ARGS__ tail is especially easy to overflow). */
#define MAX_MACRO_ARGS    8
#define MAX_MACRO_ARG_LEN 4096

/* C99 for-init declaration scoping limits */
#define MAX_FOR_SCOPES 256
#define MAX_FOR_SCOPE_RENAMES 16
#define MAX_FORREN     128
/* General lexical block-scope nesting depth (C block scope, beyond for-init) */
#define MAX_SCOPE_DEPTH 64
#define MAX_FLOW       128
#define MAX_SNAPSHOT   256
#define MAX_PROTO_PARAMS 16
#define MAX_SWITCH_CASES 512
#define MAX_ASM_NAMES 2048
#define MAX_TYPEDEFS   512
#define MAX_STRUCTS    256
#define MAX_FIELDS     2048
#define MAX_IFSTACK    64
#define MAX_USED_EXTRNS 512
#define MAX_USER_LABELS 256
#define MAX_ENUM_CONSTS 512
#define MAX_INCLUDE_DIRS 32

/*
 * Maximum number of array dimensions for a single object (array rank), and the
 * maximum subscript depth when indexing.  C99/C11 5.2.4.1 "Translation limits"
 * requires a conforming implementation to support at least 12 pointer, array,
 * and function declarators in any combination modifying one type, so array rank
 * is capped at 12.  Indexing a pointer-to-array adds one subscript level beyond
 * the pointed-to array's rank, so index/dereference-chain buffers are sized one
 * larger.
 */
#define MAX_ARRAY_DIMS 12
#define MAX_INDEX_DEPTH (MAX_ARRAY_DIMS + 1)


/* ------------------------------------------------------------------------- *
 * Type-kind bit encoding. A "type" is an int: low bits select the base kind,
 * high bits are flags (pointer levels, unsignedness, struct). STRUCT_SHIFT
 * carries the struct id.
 * ------------------------------------------------------------------------- */
#define TYPE_CHAR      1
#define TYPE_INT       2
#define TYPE_VOID      3
#define TYPE_LONG      4
#define TYPE_FLOAT     5   /* syntax/storage only: 32-bit opaque float */
#define TYPE_BOOL      6
#define TYPE_PTR       16
#define TYPE_PTR2      64
#define TYPE_UNSIGNED  32
#define TYPE_STRUCT    128
#define STRUCT_SHIFT   8

/* ------------------------------------------------------------------------- *
 * Storage classes.
 * ------------------------------------------------------------------------- */
#define SC_GLOBAL      1
#define SC_LOCAL       2
#define SC_PARAM       3
#define SC_FUNC        4
#define SC_EXTERN      5
#define SC_REGISTER    6   /* unused; reserved */

/* ------------------------------------------------------------------------- *
 * Lexer token kinds. Single-character tokens use their ASCII code; multi-byte
 * tokens and keywords use the numbered values below (>= 256).
 * ------------------------------------------------------------------------- */
#define TOK_EOF        0
#define TOK_ID         256
#define TOK_NUM        257
#define TOK_STR        258
#define TOK_CHARLIT    259
#define TOK_INT        260
#define TOK_CHAR       261
#define TOK_VOID       262
#define TOK_UNSIGNED   263
#define TOK_SIGNED     264
#define TOK_EXTERN     265
#define TOK_STATIC     266
#define TOK_CONST      267
#define TOK_IF         268
#define TOK_ELSE       269
#define TOK_WHILE      270
#define TOK_FOR        271
#define TOK_RETURN     272
#define TOK_BREAK      273
#define TOK_CONTINUE   274
#define TOK_SIZEOF     275
#define TOK_EQ         276
#define TOK_NE         277
#define TOK_LE         278
#define TOK_GE         279
#define TOK_ANDAND     280
#define TOK_OROR       281
#define TOK_SHL        282
#define TOK_SHR        283
#define TOK_INC        284
#define TOK_DEC        285
#define TOK_ADDEQ      286
#define TOK_SUBEQ      287
#define TOK_MULEQ      288
#define TOK_DIVEQ      289
#define TOK_MODEQ      290
#define TOK_ANDEQ      291
#define TOK_OREQ       292
#define TOK_XOREQ      293
#define TOK_SHLEQ      294
#define TOK_SHREQ      295
#define TOK_TYPEDEF    296
#define TOK_REGISTER   303
#define TOK_AUTO       313
#define TOK_DO         304
#define TOK_INLINE     305
#define TOK_GOTO       306
#define TOK_ENUM       307
#define TOK_UNION      308
#define TOK_ELLIPSIS   309
#define TOK_VOLATILE  310
#define TOK_STRUCT     297
#define TOK_ARROW      298
#define TOK_LONG       299
#define TOK_FLOAT      311
#define TOK_FLOATLIT   312
#define TOK_SHORT      315
#define TOK_WSTR       314
#define TOK_BOOL       316
#define TOK_STATIC_ASSERT 317
#define TOK_NORETURN   318
#define TOK_SWITCH     300
#define TOK_CASE       301
#define TOK_DEFAULT    302

/* ------------------------------------------------------------------------- *
 * Core record types.
 * ------------------------------------------------------------------------- */

struct Token {
    int kind;
    long val;
    char text[MAX_TOK_TEXT];
    char file[256];
    /* True byte length of text for TOK_STR/TOK_WSTR, since a string literal
     * may contain an embedded NUL from a \0 escape and text[] alone (a plain
     * C string) can't carry that past a strlen() call. Unused/undefined for
     * other token kinds. */
    int text_len;
};

/*
 * Snapshot of the lexer cursor for speculative look-ahead / rewind.
 *
 * The parser repeatedly saves the current lexer position, tries a speculative
 * parse (calling next_token() a few times), then rewinds if the speculation
 * does not pan out. LexState + lex_save()/lex_restore() capture the five core
 * lexer-position fields (posi, tok_start_pos, line_no, tok_line, tok) as one
 * unit so every rewind site is spelled the same way and can never forget a
 * field. Callers that must also look ahead across an integer literal's suffix
 * flags (g_tok_long_suffix / g_tok_unsigned_suffix) save and restore those two
 * globals explicitly around the lex_save()/lex_restore() pair, because most
 * rewind sites do not consume the restored token's suffix and must not perturb
 * those flags.
 */
typedef struct LexState {
    long posi;
    long tok_start_pos;
    int  line_no;
    int  tok_line;
    struct Token tok;
} LexState;

/*
 * Current function's frame-layout state, grouped so the three fields always
 * travel together: the number of locals declared so far (nlocals, also the
 * block-scope watermark that leave_scope truncates), the total local storage
 * in bytes (local_size, monotonic), and the parameter-area offset
 * (param_offset). Both the frame-sizing scan and the codegen pass must derive
 * identical frame offsets from these, so they are snapshotted/restored as a
 * unit around the speculative body scans.
 */
typedef struct FrameState {
    int nlocals;
    int local_size;
    int param_offset;
} FrameState;

/* Scalar counters and cursors restarted for each function scan/codegen pass. */
typedef struct FunctionPassState {
    int for_seq;
    int forren_n;
    int for_decl_seq;
    int for_decl_rename_index;
    int for_decl_recording;
    int scope_depth;
    int static_local_func_index;
    int static_local_seq;
    int compound_literal_seq;
    int licm_seq;
} FunctionPassState;

/* Storage-class and qualifier flags for the declaration currently parsed. */
typedef struct DeclState {
    int is_extern;
    int is_static;
    int is_inline;
    int is_noreturn;
    int is_const;
    int is_volatile;
    int pointee_is_volatile;
    int is_register;
} DeclState;

/* Destination role is metadata, not suppression: VERIFY sinks may still need
 * raw formatted writes while scan_mode is active so their text can be read
 * back for a commit/decline decision. */
enum EmitSinkPurpose {
    EMIT_SINK_FINAL,
    EMIT_SINK_DISCARD,
    EMIT_SINK_VERIFY,
    EMIT_SINK_DEFERRED
};

typedef struct EmitSink {
    FILE *stream;
    int purpose;
} EmitSink;

/*
 * Description of the value the most recently generated expression left in
 * registers, grouped so the related result-state travels together:
 *   type          - the expression's result type (g_expr_type)
 *   long_from16   - the DE:HL long was just widened from 16-bit: 0 no,
 *                   1 signed, 2 unsigned (g_long_from16)
 *   decay_stride  - stride override when a multi-dim array decays to a
 *                   pointer; 0 = use the type default (g_array_decay_stride)
 *   no_deref      - suppress the next '*' load, a phantom deref for a
 *                   multi-dim array row pointer (g_expr_no_deref)
 */
typedef struct ExprState {
    int type;
    int long_from16;
    int decay_stride;
    int no_deref;
} ExprState;

struct AstNode;

struct Sym {
    char name[64];
    char link_name[64];
    int type;
    int storage;
    int offset;
    int size;
    int has_init;
    long init_value;
    int init_count;
    int init_cap;
    char (*init_labels)[64];
    int *init_sizes;
    int is_array;
    int is_vla;     /* C99 variable-length array: slot holds a runtime pointer */
    int vla_size_offset; /* frame slot holding the runtime byte size for sizeof */
    int array_len;
    int elem_size; /* stride per first-dimension element */
    int dim_count; /* C array dimensions, e.g. a[2][3] -> 2 */
    int dims[MAX_ARRAY_DIMS];   /* dims[0] may be 0 until inferred for a[][N] */
    char runtime_stride_name[64]; /* parameter name for a runtime inner VLA bound */
    int needs_extrn; /* 1 = symbol has external linkage and may need EXTRN if referenced */
    int is_defined;  /* 1 = this translation unit emits storage/PUBLIC for the symbol */
    int is_static;   /* file-scope static: internal linkage, mangle and do not PUBLIC */
    int is_volatile; /* object declared with the volatile qualifier: access-
                      * contracting fast paths must decline for it */
    int pointee_is_volatile; /* immediate pointed-to type is volatile */
    int is_register; /* object declared with the register qualifier: a
                      * signal dcc_loop_regalloc.c uses to decline promoting
                      * anything ELSE in a loop that also contains one - see
                      * loop_regalloc_sym_eligible's comment for why. */
    int is_inline;   /* function declared with inline specifier */
    int is_noreturn; /* function declared with _Noreturn: licm_scan_modified
                      * (dcc_licm.c) tolerates a call to it in an otherwise-
                      * disqualifying eligibility scan, since nothing after
                      * the call in that control-flow path is ever reached -
                      * whatever it clobbers can't matter. */
    struct AstNode *inline_return_expr; /* simple static inline body, if captured */
    struct AstNode *inline_stmt_expr;   /* simple void inline expression body */
    struct AstNode *inline_stmt_body;   /* simple void inline statement body */
    int inline_param_use_count[MAX_PROTO_PARAMS];
    char inline_param_names[MAX_PROTO_PARAMS][64];
    /* One leading local declaration in a static inline body, captured
     * separately from inline_return_expr/inline_stmt_expr/inline_stmt_body
     * (which never include it - those are built from the statements AFTER
     * it). See dcc_func.c's try_scan_inline_local_decl for the eligibility
     * scan and dcc_ast_gen_expr.c's emit_inline_local_temp for how it's
     * materialized once per call site. */
    int has_inline_local;
    char inline_local_name[64];
    struct AstNode *inline_local_init;
    int inline_local_type;
    struct AstNode *narrow_return_expr; /* captured return expr of a zero-arg,
                                         * single-return function - independent
                                         * of is_inline, used only to bound
                                         * calls like rndrm() for array-narrowing
                                         * analysis (see dcc_array_narrow.c) */
    FILE *deferred_body_file; /* buffered out-of-line body for dead static-function
                               * elimination - both a static inline's fallback body
                               * and a plain (non-inline) static function's only body */
    int deferred_body_needed;
    int stack_check_enabled;
    int has_proto;
    int proto_nargs;
    int proto_variadic;
    int proto_types[MAX_PROTO_PARAMS];
    int is_funcptr;           /* object has function-pointer declarator type */
    int is_const_value;        /* local const scalar folded as immediate */
    unsigned long const_value; /* raw integer bits or IEEE float bits */
    int has_addr_cache;    /* this local array's address is materialized once
                            * at function entry into addr_cache_offset instead
                            * of being recomputed at every use (see dcc_func.c's
                            * per-function address-cache scan) */
    int addr_cache_offset; /* frame offset of the 2-byte pointer slot holding
                            * this array's address, valid when has_addr_cache */
    int reg_alloc;         /* REG_NONE or REG_BC - this pointer parameter is
                            * resident in BC for the whole function body
                            * instead of a frame slot (see dcc_func.c's
                            * find_bc_regalloc_candidate /
                            * try_speculative_bc_regalloc_function_body) */
};

#define REG_NONE 0
#define REG_BC   1
#define REG_E    2

struct Def {
    char name[64];
    char value[MAX_MACRO_TEXT];
    int is_func;
    int nargs;
    char params[8][32];
    int is_variadic;  /* C99 `...`: params[nargs-1] is "__VA_ARGS__" */
};

struct AsmName {
    char cname[64];
    char aname[66]; /* '_' + up to 63-char name + '\0' = 65 bytes */
};

struct TypeDef {
    char name[64];
    int type;
    int is_volatile;
    int pointee_is_volatile;
    int array_len; /* >0 when typedef is an array type, e.g. typedef int T[4] */
    int is_func;   /* typedef names a function type, e.g. typedef int F(int); */
    int has_proto;
    int proto_nargs;
    int proto_variadic;
    int proto_types[MAX_PROTO_PARAMS];
};

struct FieldDef {
    char name[64];
    int type;
    int is_volatile;
    int offset;
    int size;
    int is_array;
    int array_len;
    int elem_type;
    int elem_size;
    int dim_count;
    int dims[4];
    int parent_struct_id; /* which struct/union owns this field */
    int is_anonymous;   /* unnamed C11 struct/union member; owns layout/init slot */
    int is_promoted;    /* synthetic lookup alias through an anonymous member */
    int bit_width;      /* >0 for C89 int bitfield */
    int bit_shift;      /* bit offset within containing 16-bit storage unit */
    unsigned int bit_mask;
};

struct StructDef {
    char name[64];
    int first_field;
    int field_count;
    int size;
    int is_union; /* 1 = union: all fields at offset 0, size = max(field_sizes) */
};

/* Constant-folding value: a raw 32-bit payload plus the dcc type it carries. */
struct ConstVal {
    unsigned long u;
    int type;
};

/* One operand of a fast byte-comparison/branch peephole. */
struct ByteOperand {
    int kind;          /* 1 = IX direct byte, 2 = constant, 3 = global byte array[index],
                         * 4 = *p, p an IX-direct pointer-to-unsigned-char local/param,
                         * 5 = p[i], p an IX-direct pointer-to-byte and i const/IX-direct int
                         *     or unsigned byte (e.g. a narrowed loop counter),
                         * 6 = low byte of IX-direct scalar plus optional IX-direct scalar/const */
    struct Sym *sym;
    struct Sym *idx_sym;
    long val;
};

/* ------------------------------------------------------------------------- *
 * Shared global state. Defined once in dcc_state.c; declared extern here so
 * every module can reach the source buffer, lookahead token, symbol tables
 * and per-function codegen flags without passing them around.
 * ------------------------------------------------------------------------- */

/* assembler symbol-name table + command-line options */
extern struct AsmName asm_names[MAX_ASM_NAMES];
extern int nasm_names;
/* printf-family float/long/hex/octal support: tristate, not boolean.
 * 0 = auto (default: per-call-site detection, conservative-assume-all for a
 * non-literal format string), 1 = -f<x>io forces every call site to support
 * it, -1 = -fno-<x>io forces every call site to NOT support it (even a
 * literal format string that actually uses it, or the conservative
 * non-literal fallback) - an explicit assertion by the caller that no call
 * site anywhere in this translation unit needs it, for trimming the
 * conservative fallback's cost when a non-literal format string is known to
 * be safe. */
extern int opt_floatio;
extern int opt_longio;      /* -flongio/-fno-longio: 32-bit (long) printf format specifiers */
extern int opt_hexio;       /* -fhexio/-fno-hexio: %x/%X printf format specifiers */
extern int opt_octio;       /* -foctio/-fno-octio: %o printf format specifiers */
extern int opt_module;      /* -c/-module: emit linkable helper module, not final app TU */
extern int opt_stack_size;  /* bytes reserved above heap for C stack */
extern int opt_stack_check; /* -fstack-check: emit a stack-overflow guard at function entry */
extern int opt_no_narrow;   /* -fno-narrow: disable every int-array/scalar/for-counter
                              * byte-narrowing pass (dcc_array_narrow.c), so a build can be
                              * diffed against a normal one to isolate a narrowing-introduced
                              * output difference from any other cause - see
                              * scripts/runall.ps1 -NarrowDiff. */
extern int opt_debug;       /* -g: emit source-level debug annotations */

/* typedef table */
extern struct TypeDef typedefs[MAX_TYPEDEFS];
extern int ntypedefs;

/* struct/union + field tables */
extern struct StructDef struct_defs[MAX_STRUCTS];
extern int nstruct_defs;
extern struct FieldDef field_defs[MAX_FIELDS];
extern int nfield_defs;

/* Capture / restore the live lexer cursor `g_lex` as one unit (a plain struct
 * copy). Defined in dcc_preproc.c alongside next_token(). */
LexState lex_save(void);
void lex_restore(const LexState *s);
/* in-progress field metadata (filled while parsing one declarator) */
extern int current_field_array_elem_size;
extern int current_field_array_dim_count;
extern int current_field_array_dims[4];
extern int current_field_bit_width;
extern int current_field_bit_shift;
extern unsigned int current_field_bit_mask;

/* source buffer + lexer position + lookahead token */
extern char *src;
extern long src_len;
/* Bumped every time `src`/`src_len` is reassigned to point at different
 * text (initial preprocessing, replace_source_range's in-place macro
 * expansion, dcc_global_scan.c's pre-pass save/restore) - lets
 * source_location_at's precomputed line table (dcc_diag_emit.c) detect that
 * it must rebuild rather than answer from stale text. */
extern long g_src_generation;
/* The live lexer cursor: current read position, current token's start
 * position, current line, the token's line, and the one-token lookahead.
 * Grouped into a single struct so the fields always travel together and a
 * speculative parse can snapshot/rewind them with one struct copy
 * (lex_save()/lex_restore()). */
extern LexState g_lex;
extern EmitSink g_emit_sink;
EmitSink emit_sink_push(FILE *stream, int purpose);
void emit_sink_restore(const EmitSink *saved);
extern const char *input_name;
extern const char *output_name;
extern char current_file_name[256];
extern char predefined_date_text[16];
extern char predefined_time_text[16];

/* symbol tables */
extern struct Sym globals[MAX_SYMS];
extern int nglobals;
extern struct Sym locals[MAX_LOCALS];
extern FrameState g_frame;

/* preprocessor macro table */
extern struct Def defs[MAX_DEFINES];
extern int ndefs;

/* #if/#ifdef conditional-inclusion stack */
extern int if_parent_active[MAX_IFSTACK];
extern int if_this_active[MAX_IFSTACK];
extern int if_seen_else[MAX_IFSTACK];
extern int if_branch_taken[MAX_IFSTACK];
extern int if_sp;
extern int pp_active;

/* string-literal pool */
extern char *strings[MAX_STRINGS];
extern int string_wide[MAX_STRINGS];
extern int string_len[MAX_STRINGS];
extern int nstrings;

/* deferred EXTRN emission list */
extern struct Sym *used_extrns[MAX_USED_EXTRNS];
extern int nused_extrns;

/* per-function code-generation state */
extern int label_id;
extern int current_return_label;
/* Position in `g_emit_sink.stream` of a "jp current_return_label" tail jump gen_return_ast
 * just emitted (byte offset right before it), and the label it targets, or
 * (-1, -1) if none is pending. Debug (-g) builds only - see
 * emit_function_epilogue's elide_redundant_tail_jp. */
extern long g_return_jp_check_pos;
extern int g_return_jp_check_label;
/* Closing-brace source location of the current function body, captured when
 * the body always exits so emit_function_epilogue can map the shared return
 * label to the closing brace. 0 = none. */
extern int g_func_close_line;
extern char g_func_close_file[256];
extern int current_return_type;
extern int parse_function_return_type;
extern int current_local_bytes;
extern int max_function_local_bytes;
extern int current_omit_ix_frame;
extern int current_function_has_call;
extern int g_inline_body_buffering;
extern int g_buffering_epoch;

/* loop break/continue target stack + parser flags */
extern int break_stack[MAX_FLOW];
extern int cont_stack[MAX_FLOW];
extern int nflow;

/* C99 for-init declaration scoping (see dcc_state.c for details) */
extern FunctionPassState g_func_pass;
extern int g_for_rename_count[MAX_FOR_SCOPES];
extern char g_for_rename_from[MAX_FOR_SCOPES][MAX_FOR_SCOPE_RENAMES][64];
extern char g_for_rename_to[MAX_FOR_SCOPES][MAX_FOR_SCOPE_RENAMES][64];
extern char g_forren_from[MAX_FORREN][64];
extern char g_forren_to[MAX_FORREN][64];
extern int g_for_decl_saw_nonobject;
const char *resolve_local_rename(const char *name);
void make_for_rename_name(char *dst, int dstsz, const char *from, int for_seq, int rename_index);
void add_for_scope_rename(int for_seq, const char *from);
const char *enter_for_decl_rename(const char *name);
void push_for_rename(const char *from, const char *to);
void pop_for_rename(void);

/* General lexical block scope stack (see dcc_state.c).  enter_scope/leave_scope
 * bracket every { } block in both the frame-sizing scan and codegen.  Codegen
 * rebuilds the locals table exactly as the scan did, so both passes truncate
 * nlocals on block exit and a local declared in an inner block stops resolving
 * once the block ends. */
extern int g_scope_watermark[MAX_SCOPE_DEPTH];
void enter_scope(void);
void leave_scope(void);
int vla_scope_ensure_save_slot(void);
int vla_active_scope_depth(void);
void emit_vla_save_sp(int off);
void emit_vla_restore_sp(int off);
void emit_vla_restore_for_flow(int floor_depth);
int  vla_record_fwd_goto(int label_index, int line);
void vla_resolve_fwd_gotos(int label_index, int real_id);
void vla_snapshot_user_label(int label_index);
int  vla_jump_enters_label_scope(int label_index);
void emit_vla_restore_to_label_scope(int label_index);
struct Sym *find_local_decl(const char *name);

extern int errors;
extern int warnings;
extern int scan_mode;
extern DeclState g_decl;
extern int expr_result_dead;
extern ExprState g_expr;
extern int g_tok_long_suffix; /* set by lexer when L/l suffix seen on integer literal */
extern int g_tok_unsigned_suffix; /* set for U/u suffix or non-decimal unsigned-int literal */
extern int g_parse_type_was_enum; /* most recent parse_base_type consumed enum */

/* Pending #asm block output buffered to avoid duplicate emission from posi save/restore */
extern char pending_asm_buf[8192];
extern int  pending_asm_len;
extern int  asm_suppress_depth;
extern int  g_diag_error_count;
void flush_pending_asm(void);
void pp_reset_asm_dedupe(void);

/* ---- whole-file global-write pre-scan (dcc_global_scan.c) -------------- */
extern char g_current_compiling_func[64];
void scan_global_write_info(void);
void reset_preproc_scan_state(void);
int global_text_write_count(const char *name);
int global_text_addr_taken_count(const char *name);
int global_text_written_in_function(const char *name, const char *func);

/* user-defined goto labels (function-scoped) */
extern char ulabel_names[MAX_USER_LABELS][64];
extern int  ulabel_ids[MAX_USER_LABELS];
extern int  ulabel_defined[MAX_USER_LABELS];
extern int  ulabel_referenced[MAX_USER_LABELS];
extern int  ulabel_vla_snap_depth[MAX_USER_LABELS];
extern int  ulabel_vla_snap_off[MAX_USER_LABELS][MAX_SCOPE_DEPTH];
/* A forward goto issued from OUTSIDE every VLA scope: if the label turns out to
 * be inside a VLA scope, that jump enters the scope of a VLA and is rejected. */
extern int  ulabel_shallow_fwd_ref[MAX_USER_LABELS];
extern int  nulabels;

/* enum constants (file-scoped) */
extern char enum_const_names[MAX_ENUM_CONSTS][64];
extern int  enum_const_values[MAX_ENUM_CONSTS];
extern int  nenum_consts;

/* communicates array length from array-typedef through parse_base_type */
extern int g_typedef_array_len;
extern int g_typedef_is_func;
extern int g_typedef_has_proto;
extern int g_typedef_proto_nargs;
extern int g_typedef_proto_variadic;
extern int g_typedef_proto_types[MAX_PROTO_PARAMS];

/* counter for naming anonymous structs/unions uniquely */
extern int g_anon_struct_counter;

/* most recently parsed function prototype/parameter-list metadata */
extern int g_proto_has;
extern int g_proto_nargs;
extern int g_proto_variadic;
extern int g_proto_types[MAX_PROTO_PARAMS];
extern int g_funcptr_decl_array_len;
extern int g_funcptr_is_funcret_decl;
extern int g_funcptr_has_proto;
extern int g_funcptr_proto_nargs;
extern int g_funcptr_proto_variadic;
extern int g_funcptr_proto_types[MAX_PROTO_PARAMS];
extern int g_ptr_array_dim_count;
extern int g_ptr_array_dims[MAX_ARRAY_DIMS];
extern int g_ptr_array_elem_size;
extern char g_ptr_array_runtime_stride_name[64];
extern int g_last_array_dim_count;
extern int g_last_array_dims[MAX_ARRAY_DIMS];

/* C99 VLA: parse_array_declarator_dims() captures the first-dimension size
 * expression here when it is not a constant, so the local-declaration codegen
 * can re-emit it at the declaration site to allocate the block at run time. */
extern int g_vla_pending;
extern long g_vla_dim_posi;
extern long g_vla_dim_tok_start;
extern int g_vla_dim_line;
extern int g_vla_dim_tok_line;
extern struct Token g_vla_dim_tok;

/* C99 VLA block-scope reclamation: for each open block scope depth, the frame
 * offset of a hidden slot holding the SP to restore to when the scope exits
 * (0 = the scope has no VLA).  flow_scope_depth records the scope depth at each
 * loop/switch entry so break/continue can reclaim the VLAs allocated inside. */
extern int g_vla_scope_off[MAX_SCOPE_DEPTH];
extern int flow_scope_depth[MAX_FLOW];

/* C99 forward-goto VLA fixups.  A forward goto issued from within a VLA scope
 * cannot know its target label's scope until the label is emitted later, so the
 * jump is routed to a per-goto fixup stub.  Each pending entry snapshots the
 * goto's active VLA save-slot offsets (offsets uniquely identify a scope
 * instance); when the target label is reached, vla_resolve_fwd_gotos() emits
 * each stub, restoring SP to reclaim exactly the VLA scopes the goto leaves and
 * rejecting jumps that would enter a VLA scope. */
#define MAX_VLA_FWD_GOTOS MAX_LOCALS
struct VlaFwdGoto {
    int label_index;                 /* target label (index into ulabel_*)   */
    int fixup_id;                    /* stub label the goto jumps to          */
    int snap_depth;                  /* g_func_pass.scope_depth at the goto             */
    int snap_off[MAX_SCOPE_DEPTH];   /* g_vla_scope_off snapshot at the goto  */
    int line;                        /* goto source line, for diagnostics     */
};
extern struct VlaFwdGoto g_vla_fwd_gotos[MAX_VLA_FWD_GOTOS];
extern int g_vla_fwd_ngoto;

/* ------------------------------------------------------------------------- *
 * Cross-module function prototypes, grouped by owning module. (Generated to
 * match each definition exactly; the compiler verifies the match.)
 * ------------------------------------------------------------------------- */

/* ---- asmname ---- */
int asm_name_must_mangle(const char *cname);
int asm_name_is_internal_public(const char *cname);
const char *asm_name_for_runtime(const char *cname);
const char *asm_name_for(const char *cname);
void asm_name_check_public_collision(const char *cname);
const char *sym_asm_name(struct Sym *s);

/* ---- printf-family per-call-site entry-point selection ---- */
int asm_printf_family_fmt_arg_index(const char *cname);
void asm_scan_format_specifiers(const char *s, int *needs_float, int *needs_long,
                                 int *needs_hex, int *needs_octal);
const char *asm_name_for_pf_call(const char *cname, int needs_float, int needs_long);

/* ---- diag_emit ---- */
void fatal(const char *msg);
void init_predefined_macro_texts(void);
void dcc_copy_str(char *dst, size_t dstsz, const char *src);
void source_location_at(long ofs, char *filebuf, int filebufsz, int *linep);
const char *dcc_diag_code_for_message(const char *msg);
void dcc_error_at(const char *file, int line, long ofs, const char *msg, const char *near_text);
void error_here(const char *msg);
void warn_at(const char *file, int line, const char *msg);
void *xmalloc(size_t n);
char *xstrdup2(const char *s);
char *dcc_read_stream_text(FILE *stream, long *size_out, const char *error_msg);
int new_label(void);
void emit_ld_de_const(long v);
void emit_add_const_to_hl(long v);
void emit(const char *s);
void emit_label(int n);
void emit_jp_label(const char *op, int n);
int is_ident_start(int c);
int is_ident_char(int c);
int peekc(void);
int getc_src(void);

/* ---- preproc / lexer ---- */
void macro_expand_argument_text(const char *in, char *out, int outsz, int depth);
void pp_recompute_active(void);
void parse_preprocessor_line(void);
void skip_ws_and_comments(void);
int keyword_kind(const char *s);
int read_escape(void);
long parse_number_string(const char *s);
unsigned long parse_float_literal_bits(const char *text);
int parse_escape_string_char(const char **ps);
int parse_charlit_string_value(const char *s, long *out);
void replace_source_range(long start, long end, const char *text);
void trim_arg(char *s);
int read_macro_call_args(char args[MAX_MACRO_ARGS][MAX_MACRO_ARG_LEN], int *nargs, int variadic_named_count);
void append_macro_string_literal(const char *arg, char *out, int *oip, int outsz);
int macro_param_index(int di, const char *ident);
int read_macro_call_args_text(const char **pp, char args[MAX_MACRO_ARGS][MAX_MACRO_ARG_LEN], int *nargs,
                                     int variadic_named_count);
void macro_expand_argument_text(const char *in, char *out, int outsz, int depth);
void paste_tokens_in_text(char *s);
int replacement_param_raw_context(const char *start, const char *param_start, const char *param_end);
void expand_function_macro(int di, char args[MAX_MACRO_ARGS][MAX_MACRO_ARG_LEN], char *out, int outsz);
int macro_value_is_float_literal(const char *s);
int macro_number_should_expand_textually(const char *s);
int define_number_value(const char *name, long *out, int depth);
void next_token(void);
int accept(int k);
void expect(int k);

/* ---- types ---- */
int find_enum_const(const char *name);
int is_type_qualifier_token(int k);
int is_restrict_qualifier_token(void);
void skip_parameter_array_qualifiers(void);
void skip_type_qualifiers(void);
int skip_type_qualifiers_volatile(void);
int type_struct_id(int type);
int make_struct_type(int id);
int type_size(int type);
int type_scalar_atom_count(int type);
int type_is_long(int type);
int type_is_float(int type);
int type_is_bool(int type);
int object_array_size(int type, int count);
int target_size_multiply(int left, int right, int *result);
int type_ptr_depth(int type);
int type_add_ptr(int type);
int type_decay_ptr(int type);
int type_index_elem_size(int type);
void copy_last_array_dims_to_sym(struct Sym *s);
int sym_array_inner_count_from(struct Sym *s, int from_dim);
int sym_array_index_elem_size(struct Sym *s, int index_count);
int sym_pointer_array_index_elem_size(struct Sym *s, int cur_type, int index_count);
void infer_omitted_first_dim_from_init(struct Sym *s, int init_elems);
int find_struct_def(const char *name);
int add_struct_def(const char *name);
struct FieldDef *find_field_def(int struct_id, const char *field_name);
void parse_struct_definition(int struct_id);
int find_typedef(const char *name);
void add_typedef_name_ex(const char *name, int type, int array_len, int is_func,
                         int is_volatile, int pointee_is_volatile);
void add_typedef_name(const char *name, int type, int array_len);
int parse_base_type(void);
int is_unsupported_target_type_name(const char *name);
int parse_type(void);
void skip_type_name_param_list(void);
int parse_type_name_decl(int *typep, int *sizep);

/* ---- constexpr ---- */
int parse_typed_const_int_expr(void);
int parse_typed_array_bound_expr(void);
int parse_typed_designator_index_expr(void);
long parse_typed_const_expr_long(void);
int parse_typed_enum_const_expr(void);
long parse_typed_const_long_expr(void);
void parse_static_assert_decl(void);
int starts_type(void);

/* ---- symbols ---- */
struct Sym *find_local(const char *name);
struct Sym *find_global(const char *name);
struct Sym *find_sym(const char *name);
int is_global_char_array_sym(struct Sym *s);
void emit_global_char_index_addr(struct Sym *s);
void emit_test_global_char_index_zero(struct Sym *s, int false_label);
struct Sym *add_global(const char *name, int type, int storage);
struct Sym *add_local_known(const char *name, int type, int storage, int offset, int bytes);
struct Sym *add_local_alloc(const char *name, int type, int bytes);
struct Sym *add_compound_literal_local(int type);
struct Sym *add_param_alloc(const char *name, int type);
int add_string_ex(const char *s, int len, int is_wide);
char *read_adjacent_string_literals_ex(int *is_widep, int *lenp);
void emit_extrn_if_needed(struct Sym *s);
void emit_deferred_extrns(void);
void emit_runtime_extrn_if_needed(const char *name);
void emit_runtime_call(const char *name);
int frame_sp_offset_for_sym(struct Sym *s);
void emit_load_frame_addr_hl(struct Sym *s);
void emit_load_sym_addr(struct Sym *s);
int sym_can_ix_direct(struct Sym *s);
int local_offset_can_ix_direct(struct Sym *s, int off, int size);
int is_global_word_sym(struct Sym *s);
void emit_load_global_word_direct(struct Sym *s);
void emit_store_global_word_direct(struct Sym *s);
void emit_load_sym_value_direct(struct Sym *s);
int sym_word_load_is_two_byte_fetch(struct Sym *s);
void emit_load_sym_low_byte_and_const(struct Sym *s, unsigned int mask);
int sym_is_direct_byte_fetch(struct Sym *s);
void emit_load_sym_byte_to_a(struct Sym *s);
void emit_load_sym_de_direct(struct Sym *s);
void emit_store_hl_to_sym_direct(struct Sym *s);
int try_emit_post_update_sym_direct(struct Sym *s, int op);
void emit_incdec_sym_direct(struct Sym *s, int op);
int base_struct_id_from_type(int type);
void emit_add_field_offset(struct FieldDef *fd);
void skip_balanced_bracket(int open_ch, int close_ch);
int parse_offsetof_value(void);
int sizeof_common_type(int a, int b, int op);
int sizeof_parse_primary_type(int *typep, int *sizep);

/* ---- fold ---- */
unsigned long cf_mask_for_type(int type);
unsigned long cf_sign_bit_for_type(int type);
long cf_signed_value(struct ConstVal v);
int cf_promote_type(int type);
int cf_common_arith_type(int a, int b);
void cf_cast_to_type(struct ConstVal *v, int type);
void cf_convert_to_type(struct ConstVal *v, int type);
unsigned long cf_parse_integer_literal_bits(const char *text);
int cf_parse_primary(struct ConstVal *out);
int cf_parse_unary(struct ConstVal *out);
int cf_parse_mul(struct ConstVal *out);
int cf_parse_add(struct ConstVal *out);
int cf_parse_shift(struct ConstVal *out);
int cf_parse_rel(struct ConstVal *out);
int cf_parse_eq(struct ConstVal *out);
int cf_parse_band(struct ConstVal *out);
int cf_parse_bxor(struct ConstVal *out);
int cf_parse_bor(struct ConstVal *out);
int cf_parse_land(struct ConstVal *out);
int cf_parse_lor(struct ConstVal *out);
int cf_parse_cond(struct ConstVal *out);
int try_parse_const_expr_value(struct ConstVal *out);
int try_parse_integer_const_expr_value(struct ConstVal *out);
void emit_const_value(struct ConstVal v);

/* ---- expr ---- */
int parse_sizeof_expr_operand(void);
void emit_load_from_hl(int type);
void emit_bool_normalize_hl(int source_type);
void emit_store_de_to_addr_hl(int type);
int type_is_struct_object(int type);
int same_struct_type(int a, int b);
void emit_copy_de_to_hl_bytes(int n);
void emit_push_struct_arg_from_hl(int n);
void emit_load_hl_from_sp_offset(int off);
int parse_funcptr_declarator(int *ptype, char *name, int namesz);
int parse_abstract_funcptr_declarator(int *ptype);
int char_array_string_initializer_size(int base_type);
void parse_array_declarator_dims(int base_type, int *total_len, int *first_stride_bytes, int allow_empty_first);
int array_dim_has_runtime_identifier(void);
void skip_array_dim_to_close(void);
int count_initializer_atoms_level(void);
int count_omitted_array_initializer_atoms(void);
int count_omitted_array_initializer_top_elems(void);
void emit_init_auto_char_array_from_string(struct Sym *s, const char *str, int srclen);
int find_or_alloc_user_label_index(const char *name);
int mark_user_label_reference(const char *name);
int define_user_label(const char *name);
int find_or_alloc_user_label_index(const char *name);
void check_undefined_user_labels(void);
int parse_enum_const_value(void);
void gen_post_update_symbol_addr_value(struct Sym *s, int op);
void gen_post_update_from_addr(int type, int op);
void emit_promote_byte_to_int(int actual_type);
void emit_promote_int_to_long(int actual_type, int expected_type);
void emit_convert_int_to_float(int actual_type);
void emit_convert_float_to_intlike(int target_type);
int expected_arg_type(struct Sym *fn, int arg_index, int *ptype);
void emit_cleanup_stack_bytes(int bytes);
void emit_call_hl_from_stack_offset(int off);
void emit_extract_bitfield(void);
void emit_store_bitfield_from_hl(void);
void emit_store_bitfield_de_to_addr_hl(int keep_result);
int paren_starts_cast(void);
void emit_incdec_value_in_dehl(int type, int op);
void emit_pre_incdec_lvalue(int type, int op);

/* ---- cmp ---- */
void gen_cmp(int op);
void gen_cmp_typed(int op, int lhs_type);
void emit_signed_bias_for_relop(int op);
void emit_cmp_branch_false(int op, int lfalse);
void emit_cmp_branch_true(int op, int ltrue);
void emit_cmp_branch_false_unsigned(int op, int lfalse);
void emit_cmp_branch_true_unsigned(int op, int ltrue);
int invert_relop_for_swap(int op);
void emit_byte_operand_to_a(struct ByteOperand *op);
void emit_cp_byte_operand(struct ByteOperand *op);
int byte_operand_can_be_lhs(struct ByteOperand *op);
void emit_byte_cmp_branch_after_cp(int op, int label, int branch_when_true);
void emit_branch_on_bool_hl(int label, int branch_when_true);
int emit_cmp_const_branch_for_signed_local16(struct Sym *s, int op, long c, int label, int branch_when_true);

/* ---- ops ---- */
void gen_signed_divmod16(int op);
void gen_binop(int op);
void gen_binop_typed(int op, int lhs_type);
void emit_extend_to_long_typed(int source_type);
void emit_extend_to_long(int source_is_unsigned);
void gen_binop32(int op, int lhs_type);
void gen_cmp32(int op, int lhs_type);
void gen_binop32_typed(int op, int lhs_type);
void emit_mul_hl_const(long v);
int type_is_unsigned(int t);
int type_is_arith(int t);
int promote_int_type(int t);
int common_arith_type(int a, int b);
void emit_cast_16_to_common(int from_type, int common_type);
int peek_simple_unary_type(void);
void scale_hl_by_elem_size(int elem);
int int_log2_pow2(int v);
void emit_arith_shift_right_hl_const(int count);
void emit_logical_shift_right_hl_const(int count);
void emit_and_hl_const(unsigned int mask);
void emit_and_long_const(unsigned long mask);
void divide_hl_by_elem_size(int elem);
int emit_shift_const_long(int op, int lhs_type, long count);
void emit_shift_loop(int op, int lhs_type);
int emit_mul_pow2_long_const(long multiplier);
void emit_float_compare_call(int op);
void emit_test_expr_nonzero(int expr_type, int true_label, int branch_when_true);

/* ---- assign ---- */
void emit_load_float_bits(unsigned long bits);
void emit_global_byte_array_index_addr(struct Sym *arr, struct Sym *idx_sym, long idx_const, int has_const);

/* ---- stmt_fast ---- */
void emit_incdec_addr(int type, int op);

/* ---- decl ---- */
int parse_float_init_literal(unsigned long *bits);
int type_is_const_scalar_candidate(int type);
int try_parse_local_const_initializer(int type, unsigned long *valuep);
struct Sym *try_const_fold_local(const char *store_name, const char *src_name,
                                 int type, int has_array);
void emit_load_const_sym_value(struct Sym *s);
int try_parse_auto_const_init_value(int type, long *valuep);
void emit_store_const_to_local_array_elem(struct Sym *s, int elem_type, int index, long v);
void emit_store_const_to_local_offset(struct Sym *s, int off, int type, long v);
void emit_store_expr_to_local_offset(struct Sym *s, int off, int type);
void emit_store_expr_to_local_array_elem(struct Sym *s, int elem_type, int index);
void emit_zero_local_bytes(struct Sym *s, int off, int count);
void emit_init_auto_char_array_at_offset_from_string(struct Sym *s, int baseoff, int count, const char *str, int n);
void emit_init_auto_struct_scalar(struct Sym *s, int off, int type);
void emit_init_auto_struct_array(struct Sym *s, int baseoff, int elem_type, int count, int elem_size);
long parse_struct_init_const_value(void);
unsigned int bitfield_init_part(struct FieldDef *fd, long v);
unsigned int bitfield_field_mask(struct FieldDef *fd);
int next_parent_field_index(int sid, int start);
unsigned int pack_struct_bitfield_unit(int sid, int i, struct FieldDef *fd,
                                       int allow_promoted_owner,
                                       int *unit_offs, unsigned int *unit_vals,
                                       int *nunits, int cap,
                                       int *out_unit_off, int *out_k,
                                       int *out_stop);
void emit_store_const_bitfield_unit_to_local(struct Sym *s, int off, unsigned int unit);
void emit_init_auto_struct_type(struct Sym *s, int baseoff, int type);
void emit_init_auto_struct_from_list(struct Sym *s);
void emit_init_auto_struct_array_from_list(struct Sym *s);
int sym_array_elems_from_level(struct Sym *s, int level);
int sym_array_total_elems(struct Sym *s);
void emit_init_auto_array_scalar(struct Sym *s, int elem_type, int *np);
void emit_init_auto_array_level(struct Sym *s, int elem_type, int *np, int level);
void emit_init_auto_array_from_list(struct Sym *s, int elem_type);
void gen_local_decl_after_type(int base);

/* ---- stmt ---- */
void gen_compound(void);
int switch_label_for_value(int value, int *case_vals, int *case_labs, int ncase, int default_lab, int lend);
void emit_switch_jump_table(int minv, int maxv, int *case_vals, int *case_labs, int ncase, int default_lab, int lend);
void gen_statement(void);

/* ---- func ---- */
int current_void_is_empty_param_list(void);
void skip_prototype_array_suffixes(int *ptype);
void skip_prototype_function_suffix(void);
void clear_parsed_prototype(void);
void copy_parsed_prototype_to_sym(struct Sym *s);
void copy_funcptr_prototype_to_sym(struct Sym *s, int direct_declarator);
void remember_proto_param_type(int type);
int old_style_param_list_starts(void);
void recompute_param_offsets(void);
void parse_old_style_param_id_list(void);
void parse_old_style_param_declarations(void);
void parse_param_list(void);
int current_function_param_count(void);
int current_function_safe_to_omit_ix(int return_type, int local_bytes);
void emit_function_prologue(const char *name, int local_bytes, int omit_ix_frame);
void emit_debug_variable(struct Sym *s);
void emit_debug_variable_end(struct Sym *s);
void emit_debug_types_once(void);
void emit_debug_global(struct Sym *s);
void maybe_reserve_addr_cache_for_array(struct Sym *s, const char *name);
void emit_function_epilogue(int implicit_zero_return);
void emit_needed_deferred_bodies(void);
void skip_initializer_or_decl_tail(void);
int local_name_address_taken_ahead(const char *name);
int local_name_address_taken_in_function(const char *name);
int local_name_used_ahead(const char *name);
int narrow_array_is_byte_safe(const struct AstNode *scope, const char *arr_name);
int narrow_scalar_is_byte_safe(const struct AstNode *scope, const char *name);
int narrow_for_counter_is_byte_safe(const struct AstNode *scope, const char *name);
int try_narrow_local_int_array(const char *name, int type, int arrlen, int total_elems);
int try_narrow_register_scalar(const char *name, int type, int is_register,
                               int arrlen, int total_elems);
int try_narrow_for_counter(const char *name, int type, int arrlen, int total_elems);
void scan_local_decl_after_type(int base);
void scan_static_local_decl_after_type(int base);
void scan_function_body(void);
void parse_typedef_decl(void);
int parse_global_init_atom(long *val, char *label, int labelsz);
void append_global_init(struct Sym *s, const char *label, long v, int bytes, int is_label);
void append_global_zero_bytes(struct Sym *s, int bytes);
void append_global_char_array_string(struct Sym *s, int count, const char *str);
void parse_global_init_array(struct Sym *s, int elem_type, int count, int elem_size);
void parse_global_init_struct(struct Sym *s, int type);
void parse_global_init_type(struct Sym *s, int type, int size);
void parse_global_scalar_array_init_scalar(struct Sym *s, int *np);
void parse_global_scalar_array_zero_to(struct Sym *s, int *np, int limit);
void parse_global_scalar_array_init_level(struct Sym *s, int *np, int level);
void parse_global_init_list(struct Sym *s);
void parse_function_or_global(int base_type);

void add_predefined_extern(const char *name, int type, int storage);
void parse_translation_unit(void);

/* ---- data ---- */
int init_label_is_number(const char *p);
void emit_init_numeric(long v, int bytes);
int init_label_is_string_literal_label(const char *p);
void emit_init_label_or_number(const char *p, int bytes);
void emit_data(void);

#endif /* DCC_H */
