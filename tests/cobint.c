/* cobint.c - tiny Microsoft COBOL v4.65 subset interpreter for DCC/C89.
 * Compile-once bytecode VM for sieve.cob, e.cob, and ttt.cob: each COBOL
 * sentence is tokenized once, then compiled once (recursive-descent
 * compiler mirroring the grammar of the interpreter this replaced) into a
 * flat struct Ins bytecode array. Statement execution walks that bytecode
 * (pint.c-style pointer dispatch) instead of re-parsing the token stream
 * on every loop iteration.
 * Integer-only, heap-backed state, Ctrl-Z tolerant input.
 */

#ifdef SDCC
#define ZCC
/*
 * z88dk newlib malloc configuration.
 *
 * A negative CLIB_MALLOC_HEAP_SIZE tells the CP/M CRT to initialize the
 * standard malloc heap with all free memory between the end of BSS and the
 * reserved stack area.  Without this, the default configuration used by some
 * recent z88dk builds may leave only a small fixed heap.
 *
 * CRT_STACK_SIZE is the amount excluded from the top of memory for stack use.
 * pint's C stack use is modest; the Pascal VM stacks and frames are allocated
 * separately by init_run_storage().
 */
#pragma output CLIB_MALLOC_HEAP_SIZE = -1
#pragma output CRT_STACK_SIZE = 1536
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXSRC 18000L
#define MAXLINE 160
#define MAXNAME 32
#define MAXVAR 32
#define MAXPARA 32
#define MAXSTMT 128
#define MAXTOK 64
#define MAXARR 1200
#define MAXTCODE 2200
#define MAXSTR 32
#define MAXPERFORM 32
#define MAXVMSTACK 64

#define TK_NUM 256
#define TK_STR 257
#define TK_VAR 258
#define TK_PARA 259
#define KW_MOVE 300
#define KW_TO 301
#define KW_COMPUTE 302
#define KW_ADD 303
#define KW_SUBTRACT 304
#define KW_FROM 305
#define KW_MULTIPLY 306
#define KW_BY 307
#define KW_GIVING 308
#define KW_DIVIDE 309
#define KW_DISPLAY 310
#define KW_PERFORM 311
#define KW_THRU 312
#define KW_TIMES 313
#define KW_VARYING 314
#define KW_UNTIL 315
#define KW_GO 316
#define KW_GOTO 317
#define KW_IF 318
#define KW_ELSE 319
#define KW_STOP 320
#define KW_EXIT 321
#define KW_NOT 322
#define KW_AND 323
#define KW_OR 324
#define KW_MOD 325
#define KW_RUN 326

/* Bytecode opcodes. */
#define OP_STMT_END    0
#define OP_RETVAL      1
#define OP_PUSH        2
#define OP_PUSHVAR_S   3
#define OP_PUSHVAR_IDX 4
#define OP_STORE_S     5
#define OP_STORE_IDX   6
#define OP_SAVE_IDX    7
#define OP_STORE_IDXT  8
#define OP_ADD_TO_S    9
#define OP_ADD_TO_IDX  10
#define OP_SUB_FROM_S  11
#define OP_SUB_FROM_IDX 12
#define OP_NEG         13
#define OP_ADD         14
#define OP_SUB         15
#define OP_MUL         16
#define OP_DIV         17
#define OP_MOD         18
#define OP_EQ          19
#define OP_LT          20
#define OP_GT          21
#define OP_NOT         22
#define OP_AND         23
#define OP_OR          24
#define OP_JMP         25
#define OP_JZ          26
#define OP_STOP        27
#define OP_EXIT_STMT   28
#define OP_GOTO        29
#define OP_DISPLAY_STR 30
#define OP_DISPLAY_VAL 31
#define OP_DISPLAY_SPACE 32
#define OP_DISPLAY_NL  33
#define OP_PERFORM     34

#define PM_COUNT   0
#define PM_VARYING 1

struct Var { char name[MAXNAME]; void *v; int len; unsigned char esize; };
struct Para { char name[MAXNAME]; int first; int last; };
struct Stmt { char *s; int parno; int ts; int te; int bc; };
struct Ins { unsigned char op; int a; int b; };
struct Perform { int p, q, mode, times, vname, until_start; };

struct State {
    char *src;
    long slen;
    struct Var *var;
    struct Para *para;
    struct Stmt *stmt;
    int *tc;
    char **strs;
    struct Ins *code;
    struct Perform *pf;
    int nv, np, ns, ntc, nstr, cp, npf, code_limit;
    int stopped, jumped, jtarget;
    int idxtmp;
    int verbose;
    int tp, tend;
};
/* A plain static instance, not a heap-allocated struct behind a runtime
 * pointer variable: every field macro below expands straight to Gst.field,
 * a compile-time-constant address, so the codegen it produces addresses
 * each field directly instead of first loading a pointer value out of
 * memory and adding the field offset. Profiling e.cob against the earlier
 * `struct State *G` + xcalloc showed that reload (`ld hl,(G)` before every
 * single field touch, e.g. twice per bytecode instruction just for the
 * stopped/jumped check in exec_range) was a real, broad cost across the
 * whole interpreter, not just the VM loop.
 *
 * Deliberately NOT routed through an intermediate `#define G (&Gst)` (the
 * field macros used to expand to G->field): that shape - a self-referential
 * macro like `#define stopped G->stopped` whose body names a second, separate
 * macro (G) - drives dcc's preprocessor into unbounded token growth and OOMs
 * the host compiler. Confirmed with a 1-field minimal repro
 * (struct S{int a;}; static struct S Gst; #define G (&Gst); #define a G->a;
 * a=1;) - not specific to this file's size. Expanding every field macro
 * directly to Gst.field (no G indirection at all) sidesteps it entirely
 * while producing the same direct-addressed codegen. */
static struct State Gst;

#define var Gst.var
#define para Gst.para
#define stmt Gst.stmt
#define tc Gst.tc
#define strs Gst.strs
#define code Gst.code
#define pf Gst.pf
#define nv Gst.nv
#define np Gst.np
#define ns Gst.ns
#define ntc Gst.ntc
#define nstr Gst.nstr
#define cp Gst.cp
#define npf Gst.npf
#define code_limit Gst.code_limit
#define stopped Gst.stopped
#define jumped Gst.jumped
#define jtarget Gst.jtarget
#define idxtmp Gst.idxtmp
#define tp Gst.tp
#define tend Gst.tend

static void die(const char *s);

static int vs[MAXVMSTACK];
static int vsp;

/* No bounds check, matching pint.c/cint.c's popv/pushv: every bytecode path
 * is compile-time stack-balanced (each statement or detached expr block nets
 * exactly the values it produces), so depth can't drift at runtime. An
 * earlier version added an if-guard + die() here and marked these inline;
 * dcc's inliner miscompiled that specific shape (if-guard calling a noreturn
 * die() followed by a return of a side-effecting expression) when inlined
 * into a caller like vpush(!vpop()) - confirmed via a minimal repro (nested
 * IF/ELSE with NOT evaluated wrong). Matching the sibling interpreters'
 * unchecked shape avoids the bug and restores inlining. */
static inline int vpop(void)
{
    return vs[--vsp];
}

static inline void vpush(int v)
{
    vs[vsp++] = v;
}

static void die(const char *s)
{
    fprintf(stderr, "cobint: %s\n", s);
    exit(1);
}

static void *xcalloc(unsigned int n, unsigned int z)
{
    void *p;
    p = calloc(n, z);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(1); }
    return p;
}

static char *xstrdup2(const char *s)
{
    char *p;
    p = (char *)malloc(strlen(s) + 1);
    if (!p) die("oom");
    strcpy(p, s);
    return p;
}

static void upcase(char *d, const char *s)
{
    int i;
    for (i = 0; s[i] && i < MAXNAME - 1; i++)
        d[i] = (char)toupper((unsigned char)s[i]);
    d[i] = 0;
}

static void trim(char *s)
{
    int i, j, n;
    i = 0;
    while (s[i] && isspace((unsigned char)s[i])) i++;
    if (i) {
        j = 0;
        while (s[i]) s[j++] = s[i++];
        s[j] = 0;
    }
    n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) s[--n] = 0;
}

static int is_label_line(const char *s, char *out)
{
    int i, n;
    char buf[MAXLINE];
    strcpy(buf, s);
    trim(buf);
    n = (int)strlen(buf);
    if (n < 2 || buf[n - 1] != '.') return 0;
    buf[n - 1] = 0;
    trim(buf);
    for (i = 0; buf[i]; i++)
        if (isspace((unsigned char)buf[i])) return 0;
    upcase(out, buf);
    return 1;
}

static int find_var_soft(const char *name)
{
    char n[MAXNAME];
    int i;
    upcase(n, name);
    for (i = nv - 1; i >= 0; i--)
        if (!strcmp(var[i].name, n)) return i;
    return -1;
}

static int find_var(const char *name)
{
    int i;
    i = find_var_soft(name);
    if (i >= 0) return i;
    fprintf(stderr, "unknown var %s\n", name);
    exit(1);
    return 0;
}

static int add_var(const char *name, int len, int esize)
{
    int i;
    if (nv >= MAXVAR) die("too many variables");
    i = nv++;
    memset(&var[i], 0, sizeof(var[i]));
    upcase(var[i].name, name);
    var[i].len = len;
    var[i].esize = (unsigned char)esize;
    var[i].v = xcalloc((unsigned int)(len + 1), (unsigned int)esize);
    return i;
}

/* esize is 1 (signed char) only for a bare "PIC 9" OCCURS array - e.g.
 * sieve.cob's FLAGS, OCCURS 8190 TIMES - the one case where storing a full
 * int per element (a few KB, for a large table) burns heap other apps
 * genuinely need. Signed, not unsigned: covers a transient negative value
 * without silently wrapping, at zero extra cost, matching the safety
 * margin a full int already had.
 *
 * Deliberately NOT applied to scalar "PIC 9" fields, even though they're
 * the same declared width: verified against e.cob that scalars like
 * TM/TD/HIGH genuinely carry values outside 0-9 during intermediate
 * arithmetic (a real corruption was reproduced when this was tried) -
 * their byte-packing savings are negligible anyway (a handful of bytes)
 * next to that correctness risk, so scalars always keep the full int.
 *
 * struct Var *vp=&var[vi]: same fix as tests/forint.c's get_sym_val/
 * set_sym_val (see that file's comment and dcc's inliner commit for the
 * background) - var[vi].esize and var[vi].v were each recomputing
 * vi*sizeof(struct Var) from scratch instead of sharing one address, on
 * every variable read/write the whole interpreted program makes. Found by
 * grepping the rest of the suite for the same "array[same index] touched
 * more than once in one static inline body" shape once forint's fix
 * landed - var_get/var_set were the only other real hit. */
static inline int var_get(int vi, int idx)
{
    struct Var *vp = &var[vi];
    if (vp->esize == 1) return ((signed char *)vp->v)[idx];
    return ((int *)vp->v)[idx];
}

static inline void var_set(int vi, int idx, int val)
{
    struct Var *vp = &var[vi];
    if (vp->esize == 1) ((signed char *)vp->v)[idx] = (signed char)val;
    else ((int *)vp->v)[idx] = val;
}

static int find_para_soft(const char *name)
{
    char n[MAXNAME];
    int i;
    upcase(n, name);
    for (i = 0; i < np; i++)
        if (!strcmp(para[i].name, n)) return i;
    return -1;
}

static int find_para(const char *name)
{
    int i;
    i = find_para_soft(name);
    if (i >= 0) return i;
    fprintf(stderr, "unknown paragraph %s\n", name);
    exit(1);
    return 0;
}

static inline int stmt_for_para_i(int p)
{
    return para[p].first;
}

static inline int tpeek(void)
{
    if (tp >= tend) return 0;
    return tc[tp];
}

static inline int tget(void)
{
    if (tp >= tend) return 0;
    return tc[tp++];
}

static inline int acc(int k)
{
    if (tpeek() == k) { tp++; return 1; }
    return 0;
}

static inline void need(int k)
{
    if (!acc(k)) die("syntax");
}

static void emit_tok(int k)
{
    if (ntc >= MAXTCODE) die("token buffer full");
    tc[ntc++] = k;
}

static int add_string(const char *s)
{
    int i;
    if (nstr >= MAXSTR) die("too many strings");
    i = nstr++;
    strs[i] = xstrdup2(s);
    return i;
}

static int keyword_code(const char *s)
{
    if (!strcmp(s, "MOVE")) return KW_MOVE;
    if (!strcmp(s, "TO")) return KW_TO;
    if (!strcmp(s, "COMPUTE")) return KW_COMPUTE;
    if (!strcmp(s, "ADD")) return KW_ADD;
    if (!strcmp(s, "SUBTRACT")) return KW_SUBTRACT;
    if (!strcmp(s, "FROM")) return KW_FROM;
    if (!strcmp(s, "MULTIPLY")) return KW_MULTIPLY;
    if (!strcmp(s, "BY")) return KW_BY;
    if (!strcmp(s, "GIVING")) return KW_GIVING;
    if (!strcmp(s, "DIVIDE")) return KW_DIVIDE;
    if (!strcmp(s, "DISPLAY")) return KW_DISPLAY;
    if (!strcmp(s, "PERFORM")) return KW_PERFORM;
    if (!strcmp(s, "THRU")) return KW_THRU;
    if (!strcmp(s, "TIMES")) return KW_TIMES;
    if (!strcmp(s, "VARYING")) return KW_VARYING;
    if (!strcmp(s, "UNTIL")) return KW_UNTIL;
    if (!strcmp(s, "GO")) return KW_GO;
    if (!strcmp(s, "GOTO")) return KW_GOTO;
    if (!strcmp(s, "IF")) return KW_IF;
    if (!strcmp(s, "ELSE")) return KW_ELSE;
    if (!strcmp(s, "STOP")) return KW_STOP;
    if (!strcmp(s, "EXIT")) return KW_EXIT;
    if (!strcmp(s, "NOT")) return KW_NOT;
    if (!strcmp(s, "AND")) return KW_AND;
    if (!strcmp(s, "OR")) return KW_OR;
    if (!strcmp(s, "MOD")) return KW_MOD;
    if (!strcmp(s, "RUN")) return KW_RUN;
    if (!strcmp(s, "ZERO") || !strcmp(s, "ZEROES")) return TK_NUM;
    return 0;
}

static void tokenize_stmt(int si)
{
    const char *p;
    char word[MAXTOK];
    int i, c, code_, vi, pi;
    stmt[si].ts = ntc;
    p = stmt[si].s;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        c = (unsigned char)*p++;
        if (c == '\'') {
            i = 0;
            while (*p && *p != '\'') {
                if (i < MAXTOK - 1) word[i++] = *p;
                p++;
            }
            if (*p == '\'') p++;
            word[i] = 0;
            emit_tok(TK_STR); emit_tok(add_string(word));
        } else if (c == '-' && isdigit((unsigned char)*p)) {
            long v;
            v = 0;
            while (isdigit((unsigned char)*p)) { v = v * 10 + *p - '0'; p++; }
            emit_tok(TK_NUM); emit_tok((int)(-v));
        } else if (isdigit(c)) {
            long v;
            v = c - '0';
            while (*p && isdigit((unsigned char)*p)) { v = v * 10 + *p - '0'; p++; }
            emit_tok(TK_NUM); emit_tok((int)v);
        } else if (isalpha(c) || c == '_') {
            i = 0;
            word[i++] = (char)toupper(c);
            while (*p && (isalnum((unsigned char)*p) || *p == '-' || *p == '_')) {
                if (i < MAXTOK - 1) word[i++] = (char)toupper((unsigned char)*p);
                p++;
            }
            word[i] = 0;
            code_ = keyword_code(word);
            if (code_ == TK_NUM) { emit_tok(TK_NUM); emit_tok(0); }
            else if (code_) emit_tok(code_);
            else if ((vi = find_var_soft(word)) >= 0) { emit_tok(TK_VAR); emit_tok(vi); }
            else if ((pi = find_para_soft(word)) >= 0) { emit_tok(TK_PARA); emit_tok(pi); }
            else { fprintf(stderr, "unknown word %s\n", word); exit(1); }
        } else {
            emit_tok(c);
        }
    }
    stmt[si].te = ntc;
}

/* ---- bytecode compiler: mirrors the grammar of the evaluator it replaces,
 * emitting instructions instead of computing values. ---- */

/* code_limit starts at 0 and grows on demand (see decode_stmts) instead of
 * pre-sizing to a worst-case ratio of token count: actual bytecode per
 * token varies a lot by statement shape, so a fixed ratio either wastes
 * heap (over-provisioned - measured ~40% unused on ttt.cob) or risks
 * "bytecode full" (under-provisioned). Growing in chunks costs a handful
 * of reallocs, all during compilation, never on the VM's execution path. */
static int emit(int op, int a, int b)
{
    if (cp >= code_limit) {
        code_limit += 128;
        code = (struct Ins *)realloc(code, (unsigned int)code_limit * sizeof(struct Ins));
        if (!code) { fprintf(stderr, "out of memory\n"); exit(1); }
    }
    code[cp].op = (unsigned char)op;
    code[cp].a = a;
    code[cp].b = b;
    return cp++;
}

static void patch(int at, int v)
{
    code[at].a = v;
}

static void compile_expr(void);
static void compile_stmt_seq(void);

static void compile_var_load(void)
{
    int vi;
    if (tget() != TK_VAR) die("variable expected");
    vi = tget();
    if (acc('(')) {
        compile_expr();
        need(')');
        emit(OP_PUSHVAR_IDX, vi, 0);
    } else {
        emit(OP_PUSHVAR_S, vi, 0);
    }
}

static void compile_primary(void)
{
    if (tpeek() == TK_NUM) { tp++; emit(OP_PUSH, tget(), 0); return; }
    if (acc('(')) { compile_expr(); if (tpeek() == ')') tp++; return; }
    if (acc(KW_MOD)) {
        if (tpeek() == '(') tp++;
        compile_expr();
        if (tpeek() == ',') tp++;
        compile_expr();
        if (tpeek() == ')') tp++;
        emit(OP_MOD, 0, 0);
        return;
    }
    if (tpeek() == TK_VAR) { compile_var_load(); return; }
    die("primary");
}

static void compile_factor(void)
{
    if (acc('-')) { compile_factor(); emit(OP_NEG, 0, 0); return; }
    compile_primary();
}

static void compile_term(void)
{
    int op;
    compile_factor();
    while (tpeek() == '*' || tpeek() == '/') {
        op = tget();
        compile_factor();
        emit(op == '*' ? OP_MUL : OP_DIV, 0, 0);
    }
}

static void compile_expr(void)
{
    int op;
    compile_term();
    while (tpeek() == '+' || tpeek() == '-') {
        op = tget();
        compile_term();
        emit(op == '+' ? OP_ADD : OP_SUB, 0, 0);
    }
}

static void compile_rel_one(void)
{
    int notf, op = 0;
    if (tpeek() == '(') tp++;
    compile_expr();
    notf = 0;
    if (acc(KW_NOT)) notf = 1;
    if (tpeek() == '=' || tpeek() == '<' || tpeek() == '>') op = tget();
    else die("relop");    /* die() calls exit(1); this init only silences
                            * host compilers that don't know that (op is
                            * genuinely unreachable-uninitialized here) */
    compile_expr();
    if (tpeek() == ')') tp++;
    if (op == '=') emit(OP_EQ, 0, 0);
    else if (op == '<') emit(OP_LT, 0, 0);
    else emit(OP_GT, 0, 0);
    if (notf) emit(OP_NOT, 0, 0);
}

static void compile_condition(void)
{
    int isand;
    compile_rel_one();
    while (tpeek() == KW_AND || tpeek() == KW_OR) {
        isand = (tpeek() == KW_AND);
        tp++;
        compile_rel_one();
        emit(isand ? OP_AND : OP_OR, 0, 0);
    }
}

/* Compiles "TO/FROM/GIVING <var>" store targets that come AFTER the value
 * has already been compiled (pushed). Canonical stack order for indexed
 * stores is value-then-index (matches every statement type except
 * COMPUTE, whose target precedes its value - see compile_compute). */
static void compile_store_after_value(void)
{
    int vi;
    if (tget() != TK_VAR) die("variable expected");
    vi = tget();
    if (acc('(')) {
        compile_expr();
        need(')');
        emit(OP_STORE_IDX, vi, 0);
    } else {
        emit(OP_STORE_S, vi, 0);
    }
}

static void compile_move(void)
{
    tp++;
    compile_expr();
    need(KW_TO);
    compile_store_after_value();
}

static void compile_compute(void)
{
    int vi, indexed;
    tp++;
    if (tget() != TK_VAR) die("variable expected");
    vi = tget();
    indexed = acc('(');
    if (indexed) {
        compile_expr();
        need(')');
        emit(OP_SAVE_IDX, 0, 0);
    }
    need('=');
    compile_expr();
    if (indexed)
        emit(OP_STORE_IDXT, vi, 0);
    else
        emit(OP_STORE_S, vi, 0);
}

static void compile_add(void)
{
    int vi, indexed;
    tp++;
    compile_expr();
    need(KW_TO);
    if (tget() != TK_VAR) die("variable expected");
    vi = tget();
    indexed = acc('(');
    if (indexed) {
        compile_expr();
        need(')');
        emit(OP_ADD_TO_IDX, vi, 0);
    } else {
        emit(OP_ADD_TO_S, vi, 0);
    }
}

static void compile_subtract(void)
{
    int vi, indexed;
    tp++;
    compile_expr();
    need(KW_FROM);
    if (tget() != TK_VAR) die("variable expected");
    vi = tget();
    indexed = acc('(');
    if (indexed) {
        compile_expr();
        need(')');
        emit(OP_SUB_FROM_IDX, vi, 0);
    } else {
        emit(OP_SUB_FROM_S, vi, 0);
    }
}

static void compile_multiply(void)
{
    tp++;
    compile_expr();
    need(KW_BY);
    compile_expr();
    emit(OP_MUL, 0, 0);
    need(KW_GIVING);
    compile_store_after_value();
}

static void compile_divide(void)
{
    tp++;
    compile_expr();
    need(KW_BY);
    compile_expr();
    emit(OP_DIV, 0, 0);
    need(KW_GIVING);
    compile_store_after_value();
}

static void compile_display(void)
{
    int first, k;
    tp++;
    first = 1;
    while (tp < tend) {
        if (!first) emit(OP_DISPLAY_SPACE, 0, 0);
        first = 0;
        k = tpeek();
        if (k == TK_STR) { tp++; emit(OP_DISPLAY_STR, tget(), 0); }
        else { compile_expr(); emit(OP_DISPLAY_VAL, 0, 0); }
    }
    emit(OP_DISPLAY_NL, 0, 0);
}

static void compile_goto(void)
{
    int p;
    tp++;
    if (tpeek() == KW_TO) tp++;
    if (tpeek() != TK_PARA) die("goto name");
    tp++; p = tget();
    emit(OP_GOTO, p, 0);
}

static void compile_perform(void)
{
    int p, q, vname, mode, times, pfi, jmp, until_start;
    tp++;
    if (tpeek() != TK_PARA) die("perform name");
    tp++; p = tget();
    q = p; times = 1; mode = PM_COUNT; vname = 0; until_start = 0;
    if (acc(KW_THRU)) {
        if (tpeek() != TK_PARA) die("thru name");
        tp++; q = tget();
    }
    if (tpeek() == TK_NUM) {
        tp++; times = tget(); need(KW_TIMES);
    }
    if (acc(KW_VARYING)) {
        mode = PM_VARYING;
        if (tpeek() != TK_VAR) die("vary var");
        tp++; vname = tget();
        need(KW_FROM);
        compile_expr();
        need(KW_BY);
        compile_expr();
        need(KW_UNTIL);

        jmp = emit(OP_JMP, 0, 0);
        until_start = cp;
        compile_condition();
        emit(OP_RETVAL, 0, 0);
        patch(jmp, cp);
    }
    if (npf >= MAXPERFORM) die("too many performs");
    pfi = npf++;
    pf[pfi].p = p; pf[pfi].q = q; pf[pfi].mode = mode;
    pf[pfi].times = times; pf[pfi].vname = vname;
    pf[pfi].until_start = until_start;
    emit(OP_PERFORM, pfi, 0);
}

static void compile_if(void)
{
    int jz, jmp;
    tp++;
    compile_condition();
    jz = emit(OP_JZ, 0, 0);
    compile_stmt_seq();
    if (tpeek() == KW_ELSE) {
        tp++;
        jmp = emit(OP_JMP, 0, 0);
        patch(jz, cp);
        compile_stmt_seq();
        patch(jmp, cp);
    } else {
        patch(jz, cp);
    }
}

static void compile_stmt_seq(void)
{
    int k, stop;
    stop = 0;
    while (tp < tend && !stop) {
        k = tpeek();
        switch (k) {
        case KW_ELSE: return;
        case KW_MOVE: compile_move(); break;
        case KW_COMPUTE: compile_compute(); break;
        case KW_ADD: compile_add(); break;
        case KW_SUBTRACT: compile_subtract(); break;
        case KW_MULTIPLY: compile_multiply(); break;
        case KW_DIVIDE: compile_divide(); break;
        case KW_DISPLAY: compile_display(); break;
        case KW_PERFORM: compile_perform(); break;
        case KW_GO: case KW_GOTO: compile_goto(); stop = 1; break;
        case KW_IF: compile_if(); break;
        case KW_STOP: tp++; emit(OP_STOP, 0, 0); stop = 1; break;
        case KW_EXIT: tp++; emit(OP_EXIT_STMT, 0, 0); stop = 1; break;
        default: tp++; break;
        }
    }
}

static void compile_stmt(int si)
{
    tp = stmt[si].ts;
    tend = stmt[si].te;
    stmt[si].bc = cp;
    compile_stmt_seq();
    emit(OP_STMT_END, 0, 0);
}

/* ---- bytecode VM ---- */

static void exec_range(int start, int end);

static inline void check_idx(int vi, int ix)
{
    if (ix < 0 || ix > var[vi].len + 1) die("bad subscript");
}

/* Combined check_idx+var_get/var_set, sharing &var[vi] and the bounds
 * test once instead of once per separate call - exec_range's
 * OP_PUSHVAR_IDX/OP_STORE_IDX/OP_STORE_IDXT, and run_bc's own
 * OP_PUSHVAR_IDX, all call check_idx then var_get/var_set back-to-back
 * with the same vi (and idx), each independently re-resolving &var[vi]
 * since they're separate inline calls. Same idea as this file's own
 * var_get/var_set fix, one level up the call chain.
 *
 * NOTE: a read-modify-write counterpart (bump_var/check_idx_bump,
 * combining var_get+delta+var_set the same way for the OP_ADD_TO and
 * OP_SUB_FROM opcodes) was tried and abandoned - it miscompiled once
 * called from more than a couple of sites within exec_range (confirmed
 * via bisection: e.cob's digit-by-digit output came out as a constant
 * wrong value regardless of the true digit, isolated to these two
 * functions specifically, reproducible independent of whether the delta
 * argument was a negated expression or a plain variable, and independent
 * of an early-return vs shared-post-if-else-variable rewrite - never
 * fully root-caused in dcc's inliner). The OP_ADD_TO/OP_SUB_FROM cases
 * are left on the original separate var_get()+var_set() calls. */
static inline int check_idx_get(int vi, int idx)
{
    struct Var *vpg = &var[vi];
    if (idx < 0 || idx > vpg->len + 1) die("bad subscript");
    return (vpg->esize == 1) ? ((signed char *)vpg->v)[idx] : ((int *)vpg->v)[idx];
}

static inline void check_idx_set(int vi, int idx, int val)
{
    struct Var *vps = &var[vi];
    if (idx < 0 || idx > vps->len + 1) die("bad subscript");
    if (vps->esize == 1) ((signed char *)vps->v)[idx] = (signed char)val;
    else ((int *)vps->v)[idx] = val;
}

/* run_bc's only caller is exec_range's OP_PERFORM/PM_VARYING case, evaluating
 * a compiled PERFORM ... UNTIL condition block (compile_perform's
 * until_start bytecode: compile_condition() followed by a single OP_RETVAL -
 * see its comment). compile_condition can only ever emit the expression/
 * comparison/logic opcodes below, so this switch is intentionally missing
 * every statement-level opcode (stores, DISPLAY, GOTO, PERFORM, STOP,
 * OP_JMP/OP_JZ, OP_STMT_END/OP_EXIT_STMT) even though exec_range's switch
 * handles all of them - this used to be a full second copy of that switch,
 * which duplicated ~100 bytes of dead cases into the compiled binary for
 * every one of them (measured: enough to push ttt.cob/sieve.cob's nopeep
 * build past its heap budget, since it doesn't reduce the source program's
 * own bytecode/token needs, just the room left over for them). Trimmed to
 * exactly what compile_condition can produce. */
static int run_bc(struct Ins *start)
{
    struct Ins *in;
    int a, b;

    in = start;
    for (;;) {
        switch (in->op) {
        case OP_RETVAL:
            a = vpop();
            return a;
        case OP_PUSH:
            vpush(in->a);
            break;
        case OP_PUSHVAR_S:
            vpush(var_get(in->a, 0));
            break;
        case OP_PUSHVAR_IDX:
            a = vpop();
            vpush(check_idx_get(in->a, a));
            break;
        case OP_NEG:
            a = vpop();
            vpush(-a);
            break;
        case OP_ADD:
            b = vpop(); a = vpop(); vpush(a + b);
            break;
        case OP_SUB:
            b = vpop(); a = vpop(); vpush(a - b);
            break;
        case OP_MUL:
            b = vpop(); a = vpop(); vpush(a * b);
            break;
        case OP_DIV:
            b = vpop(); a = vpop(); vpush(b ? a / b : 0);
            break;
        case OP_MOD:
            b = vpop(); a = vpop(); vpush(b ? a % b : 0);
            break;
        case OP_EQ:
            b = vpop(); a = vpop(); vpush(a == b);
            break;
        case OP_LT:
            b = vpop(); a = vpop(); vpush(a < b);
            break;
        case OP_GT:
            b = vpop(); a = vpop(); vpush(b < a);
            break;
        case OP_NOT:
            a = vpop();
            vpush(!a);
            break;
        case OP_AND:
            b = vpop(); a = vpop(); vpush(a && b);
            break;
        case OP_OR:
            b = vpop(); a = vpop(); vpush(a || b);
            break;
        default:
            die("bad opcode");
        }
        in++;
    }
}

/* Statement-range dispatch, inlined rather than calling run_bc() once per
 * statement. The original shape (exec_range's pc loop calling run_bc() as a
 * separate function for every statement) meant a full Z80 CALL/RET plus
 * frame setup for every COBOL sentence - measured at 140,802 run_bc() calls
 * against just 196 exec_range() calls for e.cob, each call typically doing
 * only 1-4 bytecode ops of actual work before returning. Folding the
 * instruction dispatch directly into this function's statement loop (same
 * case bodies as run_bc, verified identical) keeps every statement inside
 * ITER-ROUTINE/INNER-LOOP-style GOTO loops in one continuous walk with no
 * function-call boundary between sentences. run_bc() itself is untouched
 * and still used for the separate, non-statement PERFORM ... VARYING UNTIL
 * condition block (P->until_start), which needs a real return value. */
static void exec_range(int start, int end)
{
    int pc, a, b;
    struct Ins *in;

    pc = start;
    while (pc <= end && !stopped) {
        jumped = 0;
        in = &code[stmt[pc].bc];
        for (;;) {
            switch (in->op) {
            case OP_STMT_END:
                goto next_stmt;
            case OP_EXIT_STMT:
                goto next_stmt;
            case OP_PUSH:
                vpush(in->a);
                break;
            case OP_PUSHVAR_S:
                vpush(var_get(in->a, 0));
                break;
            case OP_PUSHVAR_IDX:
                a = vpop();
                vpush(check_idx_get(in->a, a));
                break;
            case OP_STORE_S:
                a = vpop();
                var_set(in->a, 0, a);
                break;
            case OP_STORE_IDX:
                a = vpop();
                b = vpop();
                check_idx_set(in->a, a, b);
                break;
            case OP_SAVE_IDX:
                a = vpop();
                idxtmp = a;
                break;
            case OP_STORE_IDXT:
                b = vpop();
                check_idx_set(in->a, idxtmp, b);
                break;
            case OP_ADD_TO_S:
                a = vpop();
                var_set(in->a, 0, var_get(in->a, 0) + a);
                break;
            case OP_ADD_TO_IDX:
                a = vpop();
                b = vpop();
                check_idx(in->a, a);
                var_set(in->a, a, var_get(in->a, a) + b);
                break;
            case OP_SUB_FROM_S:
                a = vpop();
                var_set(in->a, 0, var_get(in->a, 0) - a);
                break;
            case OP_SUB_FROM_IDX:
                a = vpop();
                b = vpop();
                check_idx(in->a, a);
                var_set(in->a, a, var_get(in->a, a) - b);
                break;
            case OP_NEG:
                a = vpop();
                vpush(-a);
                break;
            case OP_ADD:
                b = vpop(); a = vpop(); vpush(a + b);
                break;
            case OP_SUB:
                b = vpop(); a = vpop(); vpush(a - b);
                break;
            case OP_MUL:
                b = vpop(); a = vpop(); vpush(a * b);
                break;
            case OP_DIV:
                b = vpop(); a = vpop(); vpush(b ? a / b : 0);
                break;
            case OP_MOD:
                b = vpop(); a = vpop(); vpush(b ? a % b : 0);
                break;
            case OP_EQ:
                b = vpop(); a = vpop(); vpush(a == b);
                break;
            case OP_LT:
                b = vpop(); a = vpop(); vpush(a < b);
                break;
            case OP_GT:
                b = vpop(); a = vpop(); vpush(b < a);
                break;
            case OP_NOT:
                a = vpop();
                vpush(!a);
                break;
            case OP_AND:
                b = vpop(); a = vpop(); vpush(a && b);
                break;
            case OP_OR:
                b = vpop(); a = vpop(); vpush(a || b);
                break;
            case OP_JMP:
                in = &code[in->a];
                continue;
            case OP_JZ:
                a = vpop();
                if (!a) { in = &code[in->a]; continue; }
                break;
            case OP_STOP:
                stopped = 1;
                break;
            case OP_GOTO:
                jtarget = stmt_for_para_i(in->a);
                jumped = 1;
                break;
            case OP_DISPLAY_STR:
                printf("%s", strs[in->a]);
                break;
            case OP_DISPLAY_VAL:
                printf("%d", vpop());
                break;
            case OP_DISPLAY_SPACE:
                printf(" ");
                break;
            case OP_DISPLAY_NL:
                printf("\n");
                break;
            case OP_PERFORM: {
                struct Perform *P;
                P = &pf[in->a];
                if (P->mode == PM_VARYING) {
                    int byv, fromv;
                    byv = vpop();
                    fromv = vpop();
                    var_set(P->vname, 0, fromv);
                    while (!stopped) {
                        if (run_bc(&code[P->until_start])) break;
                        exec_range(para[P->p].first, para[P->q].last);
                        jumped = 0;
                        var_set(P->vname, 0, var_get(P->vname, 0) + byv);
                    }
                } else {
                    int lim;
                    for (lim = 0; lim < P->times && !stopped; lim++) {
                        exec_range(para[P->p].first, para[P->q].last);
                        jumped = 0;
                    }
                }
                break;
            }
            default:
                die("bad opcode");
            }
            if (stopped || jumped)
                goto next_stmt;
            in++;
        }
    next_stmt:
        if (jumped) pc = jtarget;
        else pc++;
        if (pc < start || pc > end) break;
    }
}

static void add_stmt(const char *s, int p)
{
    if (ns >= MAXSTMT) die("too many statements");
    stmt[ns].s = xstrdup2(s);
    stmt[ns].parno = p;
    stmt[ns].ts = stmt[ns].te = stmt[ns].bc = 0;
    ns++;
}

static void add_para(const char *name)
{
    if (np > 0) para[np - 1].last = ns - 1;
    if (np >= MAXPARA) die("too many paragraphs");
    strcpy(para[np].name, name);
    para[np].first = ns;
    para[np].last = ns - 1;
    np++;
}

static int get_occurs(const char *s)
{
    const char *p;
    p = strstr(s, "OCCURS");
    if (!p) return 0;
    p += 6;
    while (*p && !isdigit((unsigned char)*p)) p++;
    return atoi(p);
}

static int get_pic_esize(const char *s, int len)
{
    const char *p;
    if (len <= 0) return sizeof(int);
    p = strstr(s, " PIC ");
    if (!p) return sizeof(int);
    p += 5;
    while (*p == ' ') p++;
    if (*p != '9') return sizeof(int);
    p++;
    if (*p == '9' || *p == '(') return sizeof(int);
    return 1;
}

static void parse_data_line(char *line)
{
    char *p, *q, name[MAXNAME];
    int lev, len, val;
    trim(line);
    if (!isdigit((unsigned char)line[0])) return;
    lev = atoi(line);
    (void)lev;
    p = line;
    while (*p && !isspace((unsigned char)*p)) p++;
    while (*p && isspace((unsigned char)*p)) p++;
    q = name;
    while (*p && !isspace((unsigned char)*p) && *p != '.') *q++ = *p++;
    *q = 0;
    if (!strstr(line, " PIC ")) return;
    len = get_occurs(line);
    if (len <= 0) len = 0;
    add_var(name, len, get_pic_esize(line, len));
    p = strstr(line, "VALUE");
    if (p) { val = atoi(p + 5); var_set(nv - 1, 0, val); }
}

static int load_file(const char *name)
{
    FILE *f;
    long n, got;
    f = fopen(name, "rb");
    if (!f) { perror(name); return 0; }
    fseek(f, 0L, 2); n = ftell(f); fseek(f, 0L, 0);
    if (n < 0 || n > MAXSRC) { fclose(f); fprintf(stderr, "source too large\n"); return 0; }
    Gst.src = (char *)malloc((unsigned int)n + 1);
    if (!Gst.src) { fclose(f); return 0; }
    got = (long)fread(Gst.src, 1, (unsigned int)n, f); fclose(f);
    if (got != n) return 0;
    while (n > 0 && (unsigned char)Gst.src[n - 1] == 0x1a) n--;
    Gst.src[n] = 0; Gst.slen = n; return 1;
}

static void parse_source(void)
{
    char line[MAXLINE], work[MAXLINE], label[MAXNAME], sent[800];
    char *p;
    int indata, inproc, curp, i, j;
    indata = 0; inproc = 0; curp = -1; sent[0] = 0;
    p = Gst.src;
    while (*p) {
        i = 0;
        while (*p && *p != '\n' && i < MAXLINE - 1) line[i++] = *p++;
        if (*p == '\n') p++;
        line[i] = 0;
        if (line[0] == '*' || (strlen(line) > 6 && line[6] == '*')) continue;
        strcpy(work, line); trim(work);
        if (!work[0]) continue;
        for (j = 0; work[j]; j++) work[j] = (char)toupper((unsigned char)work[j]);
        if (strstr(work, "DATA DIVISION")) { indata = 1; continue; }
        if (strstr(work, "PROCEDURE DIVISION")) { indata = 0; inproc = 1; continue; }
        if (indata) parse_data_line(work);
        if (!inproc) continue;
        if (is_label_line(work, label)) { add_para(label); curp = np - 1; continue; }
        if (curp < 0) { add_para("MAIN"); curp = 0; }
        if (sent[0]) strcat(sent, " ");
        strcat(sent, work);
        if (strchr(work, '.')) {
            char *dot;
            dot = strchr(sent, '.');
            if (dot) *dot = 0;
            trim(sent);
            if (sent[0]) add_stmt(sent, curp);
            sent[0] = 0;
        }
    }
    if (sent[0] && curp >= 0) add_stmt(sent, curp);
    if (np > 0) para[np - 1].last = ns - 1;
}

static void decode_stmts(void)
{
    int i;
    /* Each statement's raw text (stmt[i].s) is only ever read here, by
     * tokenize_stmt - compile_stmt works from the token array (.ts/.te
     * into tc[]), never the raw string. Freeing it immediately after
     * tokenizing (rather than holding every statement's text alive for
     * the rest of the run) matters on a big source like ttt.cob, where
     * the accumulated statement text is several KB of heap that's dead
     * weight right when the code buffer below needs room. */
    for (i = 0; i < ns; i++) {
        tokenize_stmt(i);
        free(stmt[i].s);
        stmt[i].s = NULL;
    }
    code_limit = 0;
    code = NULL;
    for (i = 0; i < ns; i++) compile_stmt(i);
}

static void print_stats(void)
{
    int i, bytes;
    if (!Gst.verbose) return;
    bytes = 0;
    for (i = 0; i < nv; i++) bytes += (var[i].len + 1) * var[i].esize;
    fprintf(stderr, "\nCOBINT usage summary\n");
    fprintf(stderr, "  Variables:  %d / %d\n", nv, MAXVAR);
    fprintf(stderr, "  Paragraphs: %d / %d\n", np, MAXPARA);
    fprintf(stderr, "  Statements: %d / %d\n", ns, MAXSTMT);
    fprintf(stderr, "  Tokens:     %d / %d\n", ntc, MAXTCODE);
    fprintf(stderr, "  Bytecode:   %d / %d\n", cp, code_limit);
    fprintf(stderr, "  Performs:   %d / %d\n", npf, MAXPERFORM);
    fprintf(stderr, "  Strings:    %d / %d\n", nstr, MAXSTR);
    fprintf(stderr, "  Data bytes: %d\n", bytes);
}

int main(int argc, char **argv)
{
    int argi, mainp;
    var = (struct Var *)xcalloc(MAXVAR, sizeof(struct Var));
    para = (struct Para *)xcalloc(MAXPARA, sizeof(struct Para));
    stmt = (struct Stmt *)xcalloc(MAXSTMT, sizeof(struct Stmt));
    tc = (int *)xcalloc(MAXTCODE, sizeof(int));
    strs = (char **)xcalloc(MAXSTR, sizeof(char *));
    pf = (struct Perform *)xcalloc(MAXPERFORM, sizeof(struct Perform));
    argi = 1;
    if (argi < argc && (!strcmp(argv[argi], "-V") || !strcmp(argv[argi], "-v"))) {
        Gst.verbose = 1; argi++;
    }
    if (argi >= argc) { fprintf(stderr, "usage: cobint [-V] file.cob\n"); return 1; }
    if (!load_file(argv[argi])) return 1;
    parse_source();
    /* Gst.src (the whole source file, up to MAXSRC bytes) is only read
     * during parse_source - decode_stmts and everything after work from
     * the already-extracted statement/paragraph tables. Freeing it here
     * matters on a big source like ttt.cob, where it's several KB of
     * heap that's otherwise held dead for the rest of the run. */
    free(Gst.src);
    Gst.src = NULL;
    decode_stmts();
    mainp = find_para("MAIN");
    exec_range(para[mainp].first, ns - 1);
    print_stats();
    return 0;
}
