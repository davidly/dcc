/*
 * dcc_state.c - definitions of the shared mutable state for the dcc compiler.
 *
 * MODULE: its own translation unit. This file DEFINES the compiler's
 * file-scope globals; every other module references them through the matching
 * `extern` declarations in the umbrella header dcc.h.
 *
 * Why dcc keeps so much shared state: parser, AST builder, and codegen helpers
 * share a large amount of "current position" state (the source buffer, the
 * lookahead token, the symbol tables, per-function codegen flags, ...).
 * Keeping all of it in one place, declared once in dcc.h, lets the modules
 * cooperate without threading the state through every call.
 *
 * Intentionally NOT here (kept private/static to one module for locality):
 *   - pp_expr_p / pp_expr_depth        -> dcc_preproc.c (#if expression cursor)
 *   - include_dirs / num_include_dirs  -> dcc.c (include search path)
 *
 * Source provenance: monolith src/ddc.c lines 199-203, 347-348, 378-489.
 */

#include "dcc.h"

/* ---- assembler symbol-name table + command-line options ---------------- */
struct AsmName asm_names[MAX_ASM_NAMES];
int nasm_names;
int opt_floatio;
int opt_longio;      /* -flongio/-fno-longio: 32-bit (long) printf format specifiers */
int opt_hexio;       /* -fhexio/-fno-hexio: %x/%X printf format specifiers */
int opt_octio;       /* -foctio/-fno-octio: %o printf format specifiers */
int opt_module;      /* -c/-module: emit linkable helper module, not final app TU */
int opt_stack_size;  /* bytes reserved above heap for C stack */
int opt_stack_check; /* -fstack-check: emit a stack-overflow guard at function entry */
int opt_no_narrow;   /* -fno-narrow: disable every byte-narrowing pass */
int opt_debug;       /* -g: emit source-level debug annotations */

/* ---- typedef table ----------------------------------------------------- */
struct TypeDef typedefs[MAX_TYPEDEFS];
int ntypedefs;

/* ---- struct/union + field tables --------------------------------------- */
struct StructDef struct_defs[MAX_STRUCTS];
int nstruct_defs;
struct FieldDef field_defs[MAX_FIELDS];
int nfield_defs;

/* ---- in-progress field metadata (filled while parsing one declarator) -- */
int current_field_array_elem_size;
int current_field_array_dim_count;
int current_field_array_dims[4];
int current_field_bit_width;
int current_field_bit_shift;
unsigned int current_field_bit_mask;

/* ---- source buffer + lexer position + lookahead token ------------------ */
char *src;
long src_len;
long g_src_generation;
long posi;
long tok_start_pos;
int line_no;
int tok_line;
struct Token tok;
FILE *outf;
const char *input_name;
const char *output_name;
char current_file_name[256];
char predefined_date_text[16];
char predefined_time_text[16];

/* ---- symbol tables ----------------------------------------------------- */
struct Sym globals[MAX_SYMS];
int nglobals;
struct Sym locals[MAX_LOCALS];
int nlocals;
int local_size;
int param_offset;

/* ---- preprocessor macro table ------------------------------------------ */
struct Def defs[MAX_DEFINES];
int ndefs;

/* ---- #if/#ifdef conditional-inclusion stack ---------------------------- */
int if_parent_active[MAX_IFSTACK];
int if_this_active[MAX_IFSTACK];
int if_seen_else[MAX_IFSTACK];
int if_branch_taken[MAX_IFSTACK];
int if_sp;
int pp_active = 1;

/* ---- string-literal pool ----------------------------------------------- */
char *strings[MAX_STRINGS];
int string_wide[MAX_STRINGS];
int string_len[MAX_STRINGS]; /* true byte length, may exceed strlen(strings[i])
                              * if the literal has an embedded \0 escape */
int nstrings;

/* ---- deferred EXTRN emission list --------------------------------------- */
struct Sym *used_extrns[MAX_USED_EXTRNS];
int nused_extrns;

/* ---- per-function code-generation state -------------------------------- */
int label_id;
int current_return_label;
int current_return_type;
int parse_function_return_type;
int current_local_bytes;
int max_function_local_bytes;
int current_omit_ix_frame;
int current_function_has_call;
int g_inline_body_buffering;
int g_buffering_epoch;
struct Sym *g_bc_regalloc_sym;
int g_regalloc_address_escaped;
int g_e_regalloc_claim_active;
int g_e_regalloc_claimed;
struct Sym *g_e_regalloc_sym;

/* ---- loop break/continue target stack + parser flags ------------------- */
int break_stack[MAX_FLOW];
int cont_stack[MAX_FLOW];
int nflow;

/* ---- C99 for-init declaration scoping ---------------------------------- */
/* Per-function counter of for-loops, advanced in source/pre-order in BOTH the
 * frame-sizing scan and the real codegen so the two agree on which loop is
 * which.  The frame-sizing scan records the source-name -> unique-local-name
 * mappings for each C99 for-init declaration; codegen replays them while
 * compiling the corresponding loop. */
int g_for_seq;
int g_for_rename_count[MAX_FOR_SCOPES];
char g_for_rename_from[MAX_FOR_SCOPES][MAX_FOR_SCOPE_RENAMES][64];
char g_for_rename_to[MAX_FOR_SCOPES][MAX_FOR_SCOPE_RENAMES][64];

/* Active for-init renames: while inside a for-loop with C99 init declarations,
 * source names are mapped to unique internal names so the variables have real
 * loop scope.  A small stack supports nested for scopes. */
char g_forren_from[MAX_FORREN][64];
char g_forren_to[MAX_FORREN][64];
int g_forren_n;
int g_for_decl_seq;
int g_for_decl_rename_index;
int g_for_decl_recording;
int g_for_decl_saw_nonobject;

/* General lexical block scope stack: the nlocals watermark saved at each open
 * { } block.  leave_scope truncates nlocals back so block-local names leave
 * scope.  Codegen rebuilds the table the same way the scan did, so the two
 * passes assign identical offsets; storage (local_size) is monotonic, so the
 * frame equals the sum over all scopes. */
int g_scope_watermark[MAX_SCOPE_DEPTH];
int g_scope_depth;
int g_static_local_func_index;
int g_static_local_seq;
int errors;
int scan_mode;
int decl_is_extern;
int decl_is_static;
int decl_is_inline;
int decl_is_const;      /* current declaration used const qualifier */
int decl_is_register;   /* current decl used 'register' keyword */
int expr_result_dead;
int g_expr_type;
int g_tok_long_suffix; /* set by lexer when L/l suffix seen on integer literal */
int g_tok_unsigned_suffix; /* set for U/u suffix or non-decimal unsigned-int literal */
int g_long_from16; /* the long value in DE:HL was just widened from 16-bit: 0 no, 1 signed, 2 unsigned */
int g_array_decay_stride; /* stride override when multi-dim array decays to pointer; 0 = use type default */
int g_expr_no_deref; /* 1 = suppress next * load (phantom deref for multi-dim array row pointer) */
int g_parse_type_was_enum;

/* Pending #asm block output: buffered until a safe flush point (function
 * epilogue or end of translation unit) to avoid duplication from the
 * scan_function_body() pre-passes that save/restore posi. */
char pending_asm_buf[8192];
int  pending_asm_len;
int  asm_suppress_depth;
int  g_diag_error_count;
int  g_compound_literal_seq;
int  g_licm_seq;
char g_current_compiling_func[64];

/* User-defined goto labels (function-scoped) */
char ulabel_names[MAX_USER_LABELS][64];
int  ulabel_ids[MAX_USER_LABELS];
int  ulabel_defined[MAX_USER_LABELS];
int  ulabel_referenced[MAX_USER_LABELS];
int  ulabel_vla_snap_depth[MAX_USER_LABELS];
int  ulabel_vla_snap_off[MAX_USER_LABELS][MAX_SCOPE_DEPTH];
int  ulabel_shallow_fwd_ref[MAX_USER_LABELS];
int  nulabels;

/* Enum constants (file-scoped) */
char enum_const_names[MAX_ENUM_CONSTS][64];
int  enum_const_values[MAX_ENUM_CONSTS];
int  nenum_consts;

/* Communicates array length from array-typedef through parse_base_type to declarators */
int g_typedef_array_len;
int g_typedef_is_func;
int g_typedef_has_proto;
int g_typedef_proto_nargs;
int g_typedef_proto_variadic;
int g_typedef_proto_types[MAX_PROTO_PARAMS];

/* Counter for naming anonymous structs/unions uniquely */
int g_anon_struct_counter;

/* Most recently parsed function prototype/parameter-list metadata. */
int g_proto_has;
int g_proto_nargs;
int g_proto_variadic;
int g_proto_types[MAX_PROTO_PARAMS];
int g_funcptr_decl_array_len;
int g_funcptr_is_funcret_decl;
int g_funcptr_has_proto;
int g_funcptr_proto_nargs;
int g_funcptr_proto_variadic;
int g_funcptr_proto_types[MAX_PROTO_PARAMS];
int g_ptr_array_dim_count;
int g_ptr_array_dims[MAX_ARRAY_DIMS];
int g_ptr_array_elem_size;
char g_ptr_array_runtime_stride_name[64];
int g_last_array_dim_count;
int g_last_array_dims[MAX_ARRAY_DIMS];

int g_vla_pending;
long g_vla_dim_posi;
long g_vla_dim_tok_start;
int g_vla_dim_line;
int g_vla_dim_tok_line;
struct Token g_vla_dim_tok;
int g_vla_scope_off[MAX_SCOPE_DEPTH];
int flow_scope_depth[MAX_FLOW];
struct VlaFwdGoto g_vla_fwd_gotos[MAX_VLA_FWD_GOTOS];
int g_vla_fwd_ngoto;
