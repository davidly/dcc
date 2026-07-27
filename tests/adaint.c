/* adaint.c - tiny Ada subset bytecode interpreter for DCC/C89 / CP/M-80.
 * Compile-then-run VM, supports enough Ada for SIEVE.ADA, E.ADA, TTT.ADA.
 * Builtins for Put, New_Line, Command_Line, Str_To_Int are internal.
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
#pragma output CRT_STACK_SIZE = 512
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef ZCC
#include <malloc.h>
#endif

#define MAXSRC 9000L
#define MAXTOK 80
#define MAXNAME 24
#define MAXSYM 96
#define MAXFUNC 24
#define MAXCODE 1800
#define MAXSTR 24
#define MAXMEM 9200
#define MAXSTACK 96
#define MAXFRAME 16
#define MAXLOC 384
#define MAXPARAM 8
#define INTB 2

#define T_EOF 0
#define T_ID 256
#define T_NUM 257
#define T_STR 258
#define T_ASSIGN 259
#define T_NE 260
#define T_LE 261
#define T_GE 262
#define T_DOTDOT 263

#define K_VAR 1
#define K_FUNC 2
#define K_CONST 3

#define OP_HALT 0
#define OP_PUSH 1
#define OP_LDG 2
#define OP_STG 3
#define OP_LDL 4
#define OP_STL 5
#define OP_LDGA 6
#define OP_STGA 7
#define OP_LDLA 8
#define OP_STLA 9
#define OP_LDGB 10
#define OP_STGB 11
#define OP_LDGAB 12
#define OP_STGAB 13
#define OP_LDLB 14
#define OP_STLB 15
#define OP_LDLAB 16
#define OP_STLAB 17
#define OP_ADD 18
#define OP_SUB 19
#define OP_MUL 20
#define OP_DIV 21
#define OP_MOD 22
#define OP_NEG 23
#define OP_NOT 24
#define OP_EQ 25
#define OP_NE 26
#define OP_LT 27
#define OP_LE 28
#define OP_GT 29
#define OP_GE 30
#define OP_AND 31
#define OP_OR 32
#define OP_JMP 33
#define OP_JZ 34
#define OP_CALL 35
#define OP_RET 36
#define OP_POP 37
#define OP_PUTI 38
#define OP_PUTS 39
#define OP_NL 40
#define OP_FORPREP 41
#define OP_FORLOOP 42

struct Ins { unsigned char op; int a; int b; };
struct Sym {
    char name[MAXNAME];
    unsigned char kind;
    unsigned char scope;
    unsigned char esize;
    unsigned char isarr;
    int base;
    int size;
    int val;
    int func;
};
struct Func {
    char name[MAXNAME];
    int entry;
    int nparam;
    int locals;
    unsigned char ret_esize;
    unsigned char pofs[MAXPARAM];
    unsigned char pesz[MAXPARAM];
};
struct Mark { long pos; int tok; int ival; int cp; char text[MAXTOK]; };
struct State {
    char *src;
    long slen;
    long pos;
    int line;
    int tok;
    int ival;
    char text[MAXTOK];
    struct Ins *code;
    struct Sym *sym;
    struct Func *func;
    char **strs;
    unsigned char *gmem;
    unsigned char *floc;
    unsigned char *flp;
    int *st;
    int *stp;
    struct Ins **fret;
    int cp;
    int nsym;
    int nfunc;
    int nstr;
    int gtop;
    int gmem_cap;
    int curfunc;
    int fp;
    int frame_size;
    int main_entry;
    int verbose;
    struct Mark *marks;
    int mark_sp;
};
static struct State *G;

static void parse_expr(void);
static void statement(void);
static void decls_until_begin(void);
static void add_var_name(const char *name, int sc, int esz, int count);

static void die(const char *s)
{
    fprintf(stderr, "adaint:%d: %s near '%s'\n", G ? G->line : 0, s,
            G ? G->text : "");
    exit(1);
}

static void *xcalloc(unsigned int n, unsigned int z)
{
    void *p;
    p = calloc(n, z);
    if (!p) die("oom");
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

static void lower_copy(char *d, const char *s)
{
    int i;
    for (i = 0; s[i] && i < MAXNAME - 1; i++)
        d[i] = (char)tolower((unsigned char)s[i]);
    d[i] = 0;
}

static int emit(int op, int a, int b)
{
    if (G->cp >= MAXCODE) die("code full");
    G->code[G->cp].op = (unsigned char)op;
    G->code[G->cp].a = a;
    G->code[G->cp].b = b;
    return G->cp++;
}

static void patch(int at, int v)
{
    G->code[at].a = v;
}

static inline void pushv(int v)
{
    *G->stp = v;
    G->stp = G->stp + 1;
}

static inline int popv(void)
{
    G->stp = G->stp - 1;
    return *G->stp;
}

static void mark_get(struct Mark *m)
{
    m->pos = G->pos;
    m->tok = G->tok;
    m->ival = G->ival;
    m->cp = G->cp;
    strcpy(m->text, G->text);
}

static void mark_set(struct Mark *m)
{
    G->pos = m->pos;
    G->tok = m->tok;
    G->ival = m->ival;
    G->cp = m->cp;
    strcpy(G->text, m->text);
}

static int isword(const char *s)
{
    return G->tok == T_ID && stricmp(G->text, s) == 0;
}

static void skip_ws(void)
{
    int c;
    for (;;) {
        while (G->pos < G->slen && isspace((unsigned char)G->src[G->pos])) {
            if (G->src[G->pos] == '\n') G->line++;
            G->pos++;
        }
        if (G->pos + 1 < G->slen && G->src[G->pos] == '-' &&
            G->src[G->pos + 1] == '-') {
            while (G->pos < G->slen && G->src[G->pos] != '\n') G->pos++;
            continue;
        }
        c = (G->pos < G->slen) ? (unsigned char)G->src[G->pos] : 0;
        if (c == '@') {
            while (G->pos < G->slen && G->src[G->pos] != '\n') G->pos++;
            continue;
        }
        break;
    }
}

static void next(void)
{
    int c, i;
    long v;

    skip_ws();
    G->text[0] = 0;
    G->tok = T_EOF;
    G->ival = 0;
    if (G->pos >= G->slen) return;
    c = (unsigned char)G->src[G->pos++];
    if (isalpha(c) || c == '_') {
        i = 0;
        while (isalnum(c) || c == '_') {
            if (i < MAXTOK - 1) G->text[i++] = (char)tolower(c);
            if (G->pos >= G->slen) break;
            c = (unsigned char)G->src[G->pos];
            if (isalnum(c) || c == '_') G->pos++;
            else break;
        }
        G->text[i] = 0;
        if (stricmp(G->text, "true") == 0) { G->tok = T_NUM; G->ival = 1; return; }
        if (stricmp(G->text, "false") == 0) { G->tok = T_NUM; G->ival = 0; return; }
        G->tok = T_ID;
        return;
    }
    if (isdigit(c)) {
        v = 0;
        while (isdigit(c)) {
            v = v * 10 + c - '0';
            if (G->pos >= G->slen) break;
            c = (unsigned char)G->src[G->pos++];
        }
        if (!isdigit(c)) G->pos--;
        G->tok = T_NUM;
        G->ival = (int)v;
        sprintf(G->text, "%d", G->ival);
        return;
    }
    if (c == '"') {
        i = 0;
        while (G->pos < G->slen && G->src[G->pos] != '"') {
            c = (unsigned char)G->src[G->pos++];
            if (i < MAXTOK - 1) G->text[i++] = (char)c;
        }
        if (G->pos < G->slen) G->pos++;
        G->text[i] = 0;
        G->tok = T_STR;
        return;
    }
    if (c == ':' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_ASSIGN; strcpy(G->text, ":="); return; }
    if (c == '/' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_NE; strcpy(G->text, "/="); return; }
    if (c == '<' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_LE; strcpy(G->text, "<="); return; }
    if (c == '>' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_GE; strcpy(G->text, ">="); return; }
    if (c == '.' && G->pos < G->slen && G->src[G->pos] == '.')
        { G->pos++; G->tok = T_DOTDOT; strcpy(G->text, ".."); return; }
    G->tok = c;
    G->text[0] = (char)c;
    G->text[1] = 0;
}

static void need(int t)
{
    if (G->tok != t) die("syntax");
    next();
}

static int acc(int t)
{
    if (G->tok == t) { next(); return 1; }
    return 0;
}

static void need_word(const char *s)
{
    if (!isword(s)) die("syntax");
    next();
}

static int acc_word(const char *s)
{
    if (isword(s)) { next(); return 1; }
    return 0;
}

static int add_string(const char *s)
{
    int i;
    if (G->nstr >= MAXSTR) die("too many strings");
    i = G->nstr++;
    G->strs[i] = xstrdup2(s);
    return i;
}

static int find_sym_scope(const char *n, int sc)
{
    char ln[MAXNAME];
    int i;
    lower_copy(ln, n);
    for (i = G->nsym - 1; i >= 0; i--)
        if (G->sym[i].scope == sc && !strcmp(G->sym[i].name, ln)) return i;
    return -1;
}

static int find_sym(const char *n)
{
    int i;
    if (G->curfunc >= 0) {
        i = find_sym_scope(n, G->curfunc + 1);
        if (i >= 0) return i;
    }
    i = find_sym_scope(n, 0);
    if (i >= 0) return i;
    die("undefined identifier");
    return 0;
}

static int add_sym(const char *n, int kind, int sc)
{
    int i;
    if (G->nsym >= MAXSYM) die("sym full");
    i = G->nsym++;
    memset(&G->sym[i], 0, sizeof(struct Sym));
    lower_copy(G->sym[i].name, n);
    G->sym[i].kind = (unsigned char)kind;
    G->sym[i].scope = (unsigned char)sc;
    G->sym[i].esize = INTB;
    return i;
}

static int find_func(const char *n)
{
    char ln[MAXNAME];
    int i;
    lower_copy(ln, n);
    for (i = 0; i < G->nfunc; i++)
        if (!strcmp(G->func[i].name, ln)) return i;
    return -1;
}

static int add_func(const char *n)
{
    int i, si;
    i = find_func(n);
    if (i >= 0) return i;
    if (G->nfunc >= MAXFUNC) die("too many funcs");
    i = G->nfunc++;
    memset(&G->func[i], 0, sizeof(struct Func));
    lower_copy(G->func[i].name, n);
    G->func[i].ret_esize = INTB;
    si = add_sym(n, K_FUNC, 0);
    G->sym[si].func = i;
    return i;
}

/* gmem grows on demand instead of eagerly reserving the full MAXMEM ceiling
 * up front: that ceiling exists for the rare program with a large array
 * (e.g. sieve.ada's Flags, OCCURS-equivalent 8191 elements), but most
 * programs (e.g. ttt.ada) use only a few dozen bytes of global storage -
 * paying the worst case unconditionally left too little heap for anything
 * else. Growing in small chunks costs a handful of reallocs, all at
 * compile time, never on the interpreter's hot execution path. */
static int alloc_global(int bytes)
{
    int b, newtop, newcap;
    b = G->gtop;
    newtop = G->gtop + bytes;
    if (newtop >= MAXMEM) die("global memory full");
    if (newtop > G->gmem_cap) {
        newcap = newtop + 64;
        if (newcap >= MAXMEM) newcap = MAXMEM;
        G->gmem = (unsigned char *)realloc(G->gmem, (unsigned int)newcap);
        if (!G->gmem) die("oom");
        memset(G->gmem + G->gmem_cap, 0, (unsigned int)(newcap - G->gmem_cap));
        G->gmem_cap = newcap;
    }
    G->gtop = newtop;
    return b;
}

static int alloc_local(int bytes)
{
    int b;
    b = G->func[G->curfunc].locals;
    G->func[G->curfunc].locals += bytes;
    if (G->func[G->curfunc].locals >= MAXLOC) die("local memory full");
    if (G->func[G->curfunc].locals > G->frame_size)
        G->frame_size = G->func[G->curfunc].locals;
    return b;
}

static int load_op(int sc, int esz, int arr)
{
    if (arr) {
        if (sc) return esz == 1 ? OP_LDLAB : OP_LDLA;
        return esz == 1 ? OP_LDGAB : OP_LDGA;
    }
    if (sc) return esz == 1 ? OP_LDLB : OP_LDL;
    return esz == 1 ? OP_LDGB : OP_LDG;
}

static int store_op(int sc, int esz, int arr)
{
    if (arr) {
        if (sc) return esz == 1 ? OP_STLAB : OP_STLA;
        return esz == 1 ? OP_STGAB : OP_STGA;
    }
    if (sc) return esz == 1 ? OP_STLB : OP_STL;
    return esz == 1 ? OP_STGB : OP_STG;
}

static int parse_const_expr(void)
{
    int v, op;
    if (G->tok == '-') { next(); return -parse_const_expr(); }
    if (G->tok == T_NUM) { v = G->ival; next(); }
    else if (G->tok == T_ID) { v = G->sym[find_sym(G->text)].val; next(); }
    else die("constant");
    while (G->tok == '+' || G->tok == '-') {
        op = G->tok; next();
        if (G->tok == T_NUM) { if (op == '+') v += G->ival; else v -= G->ival; }
        else if (G->tok == T_ID) {
            if (op == '+') v += G->sym[find_sym(G->text)].val;
            else v -= G->sym[find_sym(G->text)].val;
        } else die("constant");
        next();
    }
    return v;
}

static void lvalue(int *sip, int *arrp)
{
    int si;
    char name[MAXNAME];
    if (G->tok != T_ID) die("lvalue");
    strcpy(name, G->text);
    next();
    si = find_sym(name);
    *arrp = 0;
    if (acc('(')) {
        parse_expr();
        need(')');
        *arrp = 1;
    }
    *sip = si;
}

static void emit_load_lvalue(int si, int arr)
{
    emit(load_op(G->sym[si].scope, G->sym[si].esize, arr), G->sym[si].base, 0);
}

static void emit_store_lvalue(int si, int arr)
{
    emit(store_op(G->sym[si].scope, G->sym[si].esize, arr), G->sym[si].base, 0);
}

static void primary(void)
{
    char name[MAXNAME];
    int si, fi, argc, arr;

    if (G->tok == T_NUM) { emit(OP_PUSH, G->ival, 0); next(); return; }
    if (G->tok == '(') { next(); parse_expr(); need(')'); return; }
    if (G->tok == T_ID) {
        strcpy(name, G->text);
        next();
        si = find_sym_scope(name, G->curfunc >= 0 ? G->curfunc + 1 : 0);
        if (si < 0) si = find_sym_scope(name, 0);
        if (si >= 0 && G->sym[si].kind != K_FUNC) {
            if (G->sym[si].kind == K_CONST) { emit(OP_PUSH, G->sym[si].val, 0); return; }
            arr = 0;
            if (acc('(')) { parse_expr(); need(')'); arr = 1; }
            emit_load_lvalue(si, arr);
            return;
        }
        if (acc('(')) {
            fi = find_func(name);
            if (fi >= 0) {
                argc = 0;
                if (G->tok != ')') {
                    for (;;) { parse_expr(); argc++; if (!acc(',')) break; }
                }
                need(')');
                emit(OP_CALL, fi, argc);
                return;
            }
            if (stricmp(name, "command_line") == 0) {
                if (G->tok != ')') die("command line args");
                need(')'); emit(OP_PUSH, 0, 0); return;
            }
            if (stricmp(name, "str_to_int") == 0) {
                if (G->tok != ')') { parse_expr(); emit(OP_POP, 0, 0); }
                need(')'); emit(OP_PUSH, 0, 0); return;
            }
            die("bad function");
        }
        si = find_sym(name);
        if (G->sym[si].kind == K_CONST) { emit(OP_PUSH, G->sym[si].val, 0); return; }
        emit_load_lvalue(si, 0);
        return;
    }
    die("primary");
}

static void unary(void)
{
    if (G->tok == '-') { next(); unary(); emit(OP_NEG, 0, 0); return; }
    if (acc_word("not")) { unary(); emit(OP_NOT, 0, 0); return; }
    primary();
}

static void mul_expr(void)
{
    int op;
    unary();
    for (;;) {
        if (G->tok == '*') op = OP_MUL;
        else if (G->tok == '/') op = OP_DIV;
        else if (isword("rem")) op = OP_MOD;
        else break;
        next(); unary(); emit(op, 0, 0);
    }
}

static void add_expr(void)
{
    int op;
    mul_expr();
    for (;;) {
        if (G->tok == '+') op = OP_ADD;
        else if (G->tok == '-') op = OP_SUB;
        else break;
        next(); mul_expr(); emit(op, 0, 0);
    }
}

static void rel_expr(void)
{
    int op;
    add_expr();
    for (;;) {
        if (G->tok == '=') op = OP_EQ;
        else if (G->tok == T_NE) op = OP_NE;
        else if (G->tok == '<') op = OP_LT;
        else if (G->tok == '>') op = OP_GT;
        else if (G->tok == T_LE) op = OP_LE;
        else if (G->tok == T_GE) op = OP_GE;
        else break;
        next(); add_expr(); emit(op, 0, 0);
    }
}

static void and_expr(void)
{
    rel_expr();
    while (acc_word("and")) { rel_expr(); emit(OP_AND, 0, 0); }
}

static void parse_expr(void)
{
    and_expr();
    while (acc_word("or")) { and_expr(); emit(OP_OR, 0, 0); }
}

static void skip_optional_name(void)
{
    if (G->tok == T_ID) next();
}

static void parse_put_call(void)
{
    if (G->tok == T_STR) {
        emit(OP_PUTS, add_string(G->text), 0);
        next();
        return;
    }
    parse_expr();
    emit(OP_PUTI, 0, 0);
}

static void simple_call_or_assign(void)
{
    char name[MAXNAME];
    int si, arr, fi, argc;

    if (G->tok != T_ID) die("statement");
    strcpy(name, G->text);
    next();
    if (stricmp(name, "put") == 0) {
        need('('); parse_put_call(); need(')'); need(';'); return;
    }
    if (stricmp(name, "new_line") == 0) {
        if (acc('(')) need(')');
        emit(OP_NL, 0, 0); need(';'); return;
    }
    if (G->tok == '(') {
        fi = find_func(name);
        if (fi >= 0) {
            next(); argc = 0;
            if (G->tok != ')') {
                for (;;) { parse_expr(); argc++; if (!acc(',')) break; }
            }
            need(')'); emit(OP_CALL, fi, argc); emit(OP_POP, 0, 0); need(';'); return;
        }
    }
    si = find_sym(name);
    arr = 0;
    if (acc('(')) { parse_expr(); need(')'); arr = 1; }
    need(T_ASSIGN);
    parse_expr();
    emit_store_lvalue(si, arr);
    need(';');
}

static void block_until_end(void)
{
    while (!isword("end") && G->tok != T_EOF) statement();
}

static void if_stmt(void)
{
    int jz, jm;
    next();
    if (acc('(')) { parse_expr(); need(')'); }
    else parse_expr();
    need_word("then");
    jz = emit(OP_JZ, 0, 0);
    while (!isword("else") && !(isword("end")) && G->tok != T_EOF) statement();
    if (isword("else")) {
        jm = emit(OP_JMP, 0, 0);
        patch(jz, G->cp);
        next();
        while (!isword("end") && G->tok != T_EOF) statement();
        patch(jm, G->cp);
    } else patch(jz, G->cp);
    need_word("end"); need_word("if"); need(';');
}

static void while_stmt(void)
{
    int top, jz;
    next();
    top = G->cp;
    parse_expr();
    need_word("loop");
    jz = emit(OP_JZ, 0, 0);
    while (!isword("end") && G->tok != T_EOF) statement();
    need_word("end"); need_word("loop"); skip_optional_name(); need(';');
    emit(OP_JMP, top, 0);
    patch(jz, G->cp);
}

static void for_stmt(void)
{
    char name[MAXNAME];
    int si, top, jz, start, endv;
    next();
    if (G->tok != T_ID) die("for var");
    strcpy(name, G->text); next();
    si = find_sym_scope(name, G->curfunc >= 0 ? G->curfunc + 1 : 0);
    if (si < 0) si = find_sym_scope(name, 0);
    if (si < 0) {
        add_var_name(name, G->curfunc >= 0 ? G->curfunc + 1 : 0, INTB, 0);
        si = find_sym(name);
    }
    need_word("in");
    parse_expr();
    emit_store_lvalue(si, 0);
    need(T_DOTDOT);
    parse_expr();
    endv = alloc_local(INTB);
    emit(OP_STL, endv, 0);
    need_word("loop");
    top = G->cp;
    emit_load_lvalue(si, 0);
    emit(OP_LDL, endv, 0);
    emit(OP_LE, 0, 0);
    jz = emit(OP_JZ, 0, 0);
    while (!isword("end") && G->tok != T_EOF) statement();
    need_word("end"); need_word("loop"); skip_optional_name(); need(';');
    emit_load_lvalue(si, 0);
    emit(OP_PUSH, 1, 0);
    emit(OP_ADD, 0, 0);
    emit_store_lvalue(si, 0);
    emit(OP_JMP, top, 0);
    patch(jz, G->cp);
    (void)start;
}

static void return_stmt(void)
{
    next();
    parse_expr();
    need(';');
    emit(OP_RET, G->curfunc, 0);
}

static void case_stmt(void)
{
    int tmp, endj[12], ne[12], nend, val, jz;
    nend = 0;
    next();
    parse_expr();
    tmp = alloc_local(INTB);
    emit(OP_STL, tmp, 0);
    need_word("is");
    while (isword("when")) {
        next();
        val = parse_const_expr();
        need('=');
        if (G->tok == '>') next();
        emit(OP_LDL, tmp, 0); emit(OP_PUSH, val, 0); emit(OP_EQ, 0, 0);
        jz = emit(OP_JZ, 0, 0);
        statement();
        endj[nend] = emit(OP_JMP, 0, 0);
        ne[nend++] = jz;
        patch(jz, G->cp);
    }
    need_word("end"); need_word("case"); need(';');
    while (nend--) patch(endj[nend], G->cp);
    (void)ne;
}

static void statement(void)
{
    if (G->tok == T_ID) {
        struct Mark *m;
        if (G->mark_sp >= 4) die("mark stack");
        m = G->marks + G->mark_sp++;
        mark_get(m);
        next();
        if (G->tok == ':') { next(); G->mark_sp--; statement(); return; }
        mark_set(m);
        G->mark_sp--;
    }
    if (isword("if")) { if_stmt(); return; }
    if (isword("while")) { while_stmt(); return; }
    if (isword("for")) { for_stmt(); return; }
    if (isword("return")) { return_stmt(); return; }
    if (isword("case")) { case_stmt(); return; }
    if (isword("begin")) { next(); block_until_end(); need_word("end"); need(';'); return; }
    if (acc(';')) return;
    simple_call_or_assign();
}

static int type_esize(void)
{
    if (isword("integer")) { next(); return INTB; }
    if (isword("boolean")) { next(); return 1; }
    return INTB;
}

static void add_var_name(const char *name, int sc, int esz, int count)
{
    int si, bytes;
    si = add_sym(name, K_VAR, sc);
    G->sym[si].esize = (unsigned char)esz;
    G->sym[si].isarr = (unsigned char)(count > 0);
    G->sym[si].size = count;
    bytes = count > 0 ? (count + 1) * esz : esz;
    G->sym[si].base = sc ? alloc_local(bytes) : alloc_global(bytes);
}

static void var_or_const_decl(int sc)
{
    char names[8][MAXNAME];
    int nn, i, esz, lo, hi, count, val;

    nn = 0;
    if (G->tok != T_ID) die("decl name");
    for (;;) {
        strcpy(names[nn++], G->text); next();
        if (!acc(',')) break;
        if (nn >= 8) die("too many names");
        if (G->tok != T_ID) die("decl name");
    }
    need(':');
    if (acc_word("constant")) {
        need(T_ASSIGN);
        val = parse_const_expr();
        for (i = 0; i < nn; i++) {
            int si;
            si = add_sym(names[i], K_CONST, sc);
            G->sym[si].val = val;
        }
        need(';');
        return;
    }
    count = 0;
    esz = INTB;
    if (acc_word("array")) {
        need('('); lo = parse_const_expr(); need(T_DOTDOT); hi = parse_const_expr();
        need(')'); need_word("of"); esz = type_esize(); count = hi;
        (void)lo;
    } else {
        esz = type_esize();
    }
    for (i = 0; i < nn; i++) add_var_name(names[i], sc, esz, count);
    need(';');
}

static void parse_params(int fi)
{
    char name[MAXNAME];
    int p_esz, si, sc;
    sc = fi + 1;
    if (!acc('(')) return;
    if (G->tok != ')') {
        for (;;) {
            if (G->tok != T_ID) die("param name");
            strcpy(name, G->text); next();
            need(':');
            if (acc_word("in")) ;
            p_esz = type_esize();
            si = add_sym(name, K_VAR, sc);
            G->sym[si].esize = (unsigned char)p_esz;
            G->sym[si].base = alloc_local(p_esz);
            if (G->func[fi].nparam < MAXPARAM) {
                G->func[fi].pofs[G->func[fi].nparam] = (unsigned char)G->sym[si].base;
                G->func[fi].pesz[G->func[fi].nparam] = (unsigned char)p_esz;
                G->func[fi].nparam++;
            }
            if (!acc(';')) break;
        }
    }
    need(')');
}

static void parse_subprogram(int isfunc)
{
    char name[MAXNAME];
    int fi, old;
    next();
    if (G->tok != T_ID) die("subprogram name");
    strcpy(name, G->text); next();
    fi = add_func(name);
    old = G->curfunc;
    G->curfunc = fi;
    parse_params(fi);
    if (isfunc) { need_word("return"); G->func[fi].ret_esize = (unsigned char)type_esize(); }
    need_word("is");
    decls_until_begin();
    G->func[fi].entry = G->cp;
    need_word("begin");
    block_until_end();
    need_word("end"); skip_optional_name(); need(';');
    emit(OP_PUSH, 0, 0);
    emit(OP_RET, fi, 0);
    G->curfunc = old;
}

static void decls_until_begin(void)
{
    while (!isword("begin") && G->tok != T_EOF) {
        if (isword("use") || isword("with") || isword("pragma")) {
            while (G->tok != ';' && G->tok != T_EOF) next();
            if (G->tok == ';') next();
            continue;
        }
        if (isword("function")) { parse_subprogram(1); continue; }
        if (isword("procedure")) { parse_subprogram(0); continue; }
        if (G->tok == T_ID) { var_or_const_decl(G->curfunc >= 0 ? G->curfunc + 1 : 0); continue; }
        next();
    }
}

static void parse_package(void)
{
    while (G->tok != T_EOF && !isword("package")) next();
    if (isword("package")) {
        next();
        if (isword("body")) next();
        if (G->tok == T_ID) next();
        need_word("is");
    }
    G->curfunc = -1;
    decls_until_begin();
    G->main_entry = G->cp;
    need_word("begin");
    block_until_end();
    need_word("end"); skip_optional_name(); acc(';');
    emit(OP_HALT, 0, 0);
}

/* Split into word/byte variants instead of a single function taking a
 * runtime esz parameter: esz is always a compile-time literal (1 or INTB)
 * at every real call site, but a reproducible dcc quirk means having both
 * mem_get and mem_set inlined together in the same function (as run()
 * does, many times over) defeats constant-folding of esz through the
 * substituted parameter for *both* of them - each call ends up paying a
 * genuine runtime __mulu for idx * esz instead of the compile-time-
 * constant shift it should fold to. Baking the size into which function
 * is called instead of passing it sidesteps the interaction entirely
 * (verified: neither function alone triggers it, only using both
 * together in one function does) - see the identical fix in cint.c. */
static inline int mem_get_word(int base, int idx, unsigned char *m)
{
    return (short)(m[base + idx * INTB] | (m[base + idx * INTB + 1] << 8));
}

static inline int mem_get_byte(int base, int idx, unsigned char *m)
{
    return m[base + idx];
}

static inline void mem_set_word(int base, int idx, unsigned char *m, int v)
{
    m[base + idx * INTB] = (unsigned char)(v & 255);
    m[base + idx * INTB + 1] = (unsigned char)((v >> 8) & 255);
}

static inline void mem_set_byte(int base, int idx, unsigned char *m, int v)
{
    m[base + idx] = (unsigned char)v;
}

static void call_func(int fi, struct Ins *retpc, int argc)
{
    int i, v;
    struct Func *fnp = &G->func[fi];
    if (G->fp + 1 >= MAXFRAME) die("frame full");
    G->fp++;
    G->flp = G->floc + G->fp * G->frame_size;
    memset(G->flp, 0, (unsigned int)G->frame_size);
    G->fret[G->fp] = retpc;
    for (i = argc - 1; i >= 0; i--) {
        v = popv();
        if (i < fnp->nparam) {
            if (fnp->pesz[i] == 1)
                mem_set_byte(fnp->pofs[i], 0, G->flp, v);
            else
                mem_set_word(fnp->pofs[i], 0, G->flp, v);
        }
    }
}

static void run(void)
{
    int a, b, v, argc, i;
    struct Ins *in;
    G->stp = G->st;
    G->fp = 0;
    G->flp = G->floc;
    memset(G->floc, 0, (unsigned int)(MAXFRAME * G->frame_size));
    in = &G->code[G->main_entry];
    for (;;) {
        switch (in->op) {
        case OP_HALT: return;
        case OP_PUSH: pushv(in->a); break;
        case OP_LDG: pushv(mem_get_word(in->a, 0, G->gmem)); break;
        case OP_STG: mem_set_word(in->a, 0, G->gmem, popv()); break;
        case OP_LDL: pushv(mem_get_word(in->a, 0, G->flp)); break;
        case OP_STL: mem_set_word(in->a, 0, G->flp, popv()); break;
        case OP_LDGB: pushv(mem_get_byte(in->a, 0, G->gmem)); break;
        case OP_STGB: mem_set_byte(in->a, 0, G->gmem, popv()); break;
        case OP_LDLB: pushv(mem_get_byte(in->a, 0, G->flp)); break;
        case OP_STLB: mem_set_byte(in->a, 0, G->flp, popv()); break;
        case OP_LDGA: a=popv(); pushv(mem_get_word(in->a, a, G->gmem)); break;
        case OP_STGA: v=popv(); a=popv(); mem_set_word(in->a, a, G->gmem, v); break;
        case OP_LDLA: a=popv(); pushv(mem_get_word(in->a, a, G->flp)); break;
        case OP_STLA: v=popv(); a=popv(); mem_set_word(in->a, a, G->flp, v); break;
        case OP_LDGAB: a=popv(); pushv(mem_get_byte(in->a, a, G->gmem)); break;
        case OP_STGAB: v=popv(); a=popv(); mem_set_byte(in->a, a, G->gmem, v); break;
        case OP_LDLAB: a=popv(); pushv(mem_get_byte(in->a, a, G->flp)); break;
        case OP_STLAB: v=popv(); a=popv(); mem_set_byte(in->a, a, G->flp, v); break;
        case OP_ADD: b=popv(); a=popv(); pushv(a+b); break;
        case OP_SUB: b=popv(); a=popv(); pushv(a-b); break;
        case OP_MUL: b=popv(); a=popv(); pushv(a*b); break;
        case OP_DIV: b=popv(); a=popv(); pushv(b?a/b:0); break;
        case OP_MOD: b=popv(); a=popv(); pushv(b?a%b:0); break;
        case OP_NEG: a=popv(); pushv(-a); break;
        case OP_NOT: a=popv(); pushv(!a); break;
        case OP_EQ: b=popv(); a=popv(); pushv(a==b); break;
        case OP_NE: b=popv(); a=popv(); pushv(a!=b); break;
        case OP_LT: b=popv(); a=popv(); pushv(a<b); break;
        case OP_LE: b=popv(); a=popv(); pushv(a<=b); break;
        case OP_GT: b=popv(); a=popv(); pushv(a>b); break;
        case OP_GE: b=popv(); a=popv(); pushv(a>=b); break;
        case OP_AND: b=popv(); a=popv(); pushv(a&&b); break;
        case OP_OR: b=popv(); a=popv(); pushv(a||b); break;
        case OP_JMP: in = &G->code[in->a]; continue;
        case OP_JZ: a=popv(); if(!a) { in = &G->code[in->a]; continue; } break;
        case OP_CALL:
            call_func(in->a, in + 1, in->b); in = &G->code[G->func[in->a].entry]; continue;
        case OP_RET:
            v=popv(); in=G->fret[G->fp]; G->fp--;
            G->flp = G->floc + G->fp * G->frame_size; pushv(v); continue;
        case OP_POP: (void)popv(); break;
        case OP_PUTI: printf("%d", popv()); break;
        case OP_PUTS: printf("%s", G->strs[in->a]); break;
        case OP_NL: printf("\n"); break;
        default: die("bad op");
        }
        (void)argc; (void)i;
        in++;
    }
}

static char *preprocess(char *in)
{
    char *out;
    int i, o, c;
    out = (char *)malloc(strlen(in) + 1);
    if (!out) die("oom");
    i = 0; o = 0;
    while (in[i]) {
        if (in[i] == '-' && in[i + 1] == '-') {
            while (in[i] && in[i] != '\n') i++;
            if (in[i] == '\n') out[o++] = in[i++];
            continue;
        }
        if (in[i] == '@') {
            while (in[i] && in[i] != '\n') i++;
            if (in[i] == '\n') out[o++] = in[i++];
            continue;
        }
        c = (unsigned char)in[i++];
        if (c != 0x1a) out[o++] = (char)c;
    }
    out[o] = 0;
    return out;
}

static int load_file(const char *name)
{
    FILE *f; long n, got; char *raw;
    f = fopen(name, "rb");
    if (!f) { perror(name); return 0; }
    fseek(f, 0L, 2); n = ftell(f); fseek(f, 0L, 0);
    if (n < 0 || n > MAXSRC) { fclose(f); fprintf(stderr, "source too large\n"); return 0; }
    raw = (char *)malloc((size_t)n + 1); if (!raw) return 0;
    got = (long)fread(raw, 1, (size_t)n, f); fclose(f);
    if (got != n) return 0;
    raw[n] = 0;
    G->src = preprocess(raw); free(raw);
    G->slen = (long)strlen(G->src);
    return 1;
}

static void init_state(void)
{
    G = (struct State *)calloc(1, sizeof(struct State));
    if (!G) { fprintf(stderr, "oom\n"); exit(1); }
    G->line = 1;
    G->curfunc = -1;
    G->frame_size = 2;
    G->marks = (struct Mark *)xcalloc(4, sizeof(struct Mark));
}

static void init_compile_storage(void)
{
    G->code = (struct Ins *)xcalloc(MAXCODE, sizeof(struct Ins));
    G->sym = (struct Sym *)xcalloc(MAXSYM, sizeof(struct Sym));
    G->func = (struct Func *)xcalloc(MAXFUNC, sizeof(struct Func));
    G->strs = (char **)xcalloc(MAXSTR, sizeof(char *));
}

static void init_run_storage(void)
{
    G->st = (int *)xcalloc(MAXSTACK, sizeof(int));
    G->fret = (struct Ins **)xcalloc(MAXFRAME, sizeof(struct Ins *));
    G->floc = (unsigned char *)xcalloc((unsigned int)(MAXFRAME * G->frame_size), 1);
}

int main(int argc, char **argv)
{
    int argi;
    init_state();
    argi = 1;
    if (argi < argc && (!strcmp(argv[argi], "-V") || !strcmp(argv[argi], "-v"))) {
        G->verbose = 1; argi++;
    }
    if (argi >= argc) { fprintf(stderr, "usage: adaint [-V] file.ada\n"); return 1; }
    if (!load_file(argv[argi])) return 1;
    init_compile_storage();
    next();
    parse_package();
    if (G->frame_size < 2) G->frame_size = 2;
    init_run_storage();
    run();
    if (G->verbose) {
        fprintf(stderr, "\nADAINT usage summary\n");
        fprintf(stderr, "  Source bytes: %ld / %ld\n", G->slen, MAXSRC);
        fprintf(stderr, "  Code:         %d / %d\n", G->cp, MAXCODE);
        fprintf(stderr, "  Symbols:      %d / %d\n", G->nsym, MAXSYM);
        fprintf(stderr, "  Functions:    %d / %d\n", G->nfunc, MAXFUNC);
        fprintf(stderr, "  Data bytes:   %d / %d\n", G->gtop, MAXMEM);
        fprintf(stderr, "  Frame bytes:  %d\n", G->frame_size);
    }
    return 0;
}
