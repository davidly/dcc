/* cint.c - tiny C subset bytecode interpreter for DCC/C89 / CP/M-80.
 * Compile-then-run VM, modeled after pint.c style.
 * Supports enough C89-ish subset for sieve.c, e.c, and ttt.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXSRC 8000L
#define MAXTOK 80
#define MAXNAME 24
#define MAXSYM 64
#define MAXFUNC 16
#define MAXCODE 800
#define MAXSTR 16
#define MAXMEM 9000
#define MAXSTACK 96
#define MAXFRAME 16
#define MAXLOC 512
#define MAXPARAM 12
#define INTB 2

#define T_EOF 0
#define T_ID 256
#define T_NUM 257
#define T_STR 258
#define T_EQ 259
#define T_NE 260
#define T_LE 261
#define T_GE 262
#define T_ANDAND 263
#define T_OROR 264
#define T_INC 265
#define T_DEC 266
#define T_ADDEQ 267
#define T_SUBEQ 268

#define K_VAR 1
#define K_FUNC 2
#define K_CONST 3

#define SC_GLOBAL 0

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
#define OP_BAND 33
#define OP_JMP 34
#define OP_JZ 35
#define OP_JNZ 36
#define OP_CALL 37
#define OP_CALLI 38
#define OP_RET 39
#define OP_POP 40
#define OP_PRINTF 41

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
struct Mark;
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
    int *fret;
    int cp;
    int nsym;
    int nfunc;
    int nstr;
    int gtop;
    int curfunc;
    int fp;
    int frame_size;
    int main_func;
    int verbose;
    struct Mark *marks;
    int mark_sp;
    char pp_name[64];
    char pp_val[64];
    char pp_id[64];
    char *pp_defn[32];
    char *pp_defv[32];
    int pp_ifact[16];
};
static struct State *G;
static int saved_main_func;

struct Mark { long pos; int tok; int ival; int cp; char text[MAXTOK]; };

static void parse_expr(void);
static void statement(void);
static int is_type_start(void);

static void die(const char *s)
{
    fprintf(stderr, "cint:%d: %s near '%s'\n", G ? G->line : 0, s,
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

static int emit(int op, int a, int b)
{
    if (G->cp >= MAXCODE) die("code full");
    G->code[G->cp].op = (unsigned char)op;
    G->code[G->cp].a = a;
    G->code[G->cp].b = b;
    return G->cp++;
}

static inline void patch(int at, int v)
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
    return G->tok == T_ID && strcmp(G->text, s) == 0;
}

static void skip_ws(void)
{
    while (G->pos < G->slen && isspace((unsigned char)G->src[G->pos])) {
        if (G->src[G->pos] == '\n') G->line++;
        G->pos++;
    }
}

static void next(void)
{
    int c;
    int i;
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
            if (i < MAXTOK - 1) G->text[i++] = (char)c;
            if (G->pos >= G->slen) break;
            c = (unsigned char)G->src[G->pos];
            if (isalnum(c) || c == '_') G->pos++;
            else break;
        }
        G->text[i] = 0;
        if (!strcmp(G->text, "true")) { G->tok = T_NUM; G->ival = 1; return; }
        if (!strcmp(G->text, "false")) { G->tok = T_NUM; G->ival = 0; return; }
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
        while (G->pos < G->slen &&
               (G->src[G->pos] == 'u' || G->src[G->pos] == 'U' ||
                G->src[G->pos] == 'l' || G->src[G->pos] == 'L')) G->pos++;
        G->tok = T_NUM;
        G->ival = (int)v;
        sprintf(G->text, "%d", G->ival);
        return;
    }
    if (c == '"') {
        i = 0;
        while (G->pos < G->slen && G->src[G->pos] != '"') {
            c = (unsigned char)G->src[G->pos++];
            if (c == '\\' && G->pos < G->slen) {
                c = (unsigned char)G->src[G->pos++];
                if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
            }
            if (i < MAXTOK - 1) G->text[i++] = (char)c;
        }
        if (G->pos < G->slen) G->pos++;
        G->text[i] = 0;
        G->tok = T_STR;
        return;
    }
    if (c == '=' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_EQ; strcpy(G->text, "=="); return; }
    if (c == '!' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_NE; strcpy(G->text, "!="); return; }
    if (c == '<' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_LE; strcpy(G->text, "<="); return; }
    if (c == '>' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_GE; strcpy(G->text, ">="); return; }
    if (c == '&' && G->pos < G->slen && G->src[G->pos] == '&')
        { G->pos++; G->tok = T_ANDAND; strcpy(G->text, "&&"); return; }
    if (c == '|' && G->pos < G->slen && G->src[G->pos] == '|')
        { G->pos++; G->tok = T_OROR; strcpy(G->text, "||"); return; }
    if (c == '+' && G->pos < G->slen && G->src[G->pos] == '+')
        { G->pos++; G->tok = T_INC; strcpy(G->text, "++"); return; }
    if (c == '-' && G->pos < G->slen && G->src[G->pos] == '-')
        { G->pos++; G->tok = T_DEC; strcpy(G->text, "--"); return; }
    if (c == '+' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_ADDEQ; strcpy(G->text, "+="); return; }
    if (c == '-' && G->pos < G->slen && G->src[G->pos] == '=')
        { G->pos++; G->tok = T_SUBEQ; strcpy(G->text, "-="); return; }
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
    int i;
    for (i = G->nsym - 1; i >= 0; i--)
        if (G->sym[i].scope == sc && !strcmp(G->sym[i].name, n)) return i;
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
    strncpy(G->sym[i].name, n, MAXNAME - 1);
    G->sym[i].kind = (unsigned char)kind;
    G->sym[i].scope = (unsigned char)sc;
    G->sym[i].esize = INTB;
    return i;
}

static int is_main_name(const char *n)
{
    return n[0] == 'm' && n[1] == 'a' && n[2] == 'i' &&
           n[3] == 'n' && n[4] == 0;
}

static int find_func(const char *n)
{
    int i;
    for (i = 0; i < G->nfunc; i++)
        if (!strcmp(G->func[i].name, n)) return i;
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
    strncpy(G->func[i].name, n, MAXNAME - 1);
    if (is_main_name(n)) G->main_func = i;
    G->func[i].ret_esize = INTB;
    si = add_sym(n, K_FUNC, 0);
    G->sym[si].func = i;
    return i;
}

static int alloc_global(int bytes)
{
    int b;
    b = G->gtop;
    G->gtop += bytes;
    if (G->gtop >= MAXMEM) die("global memory full");
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

static int type_esize(void)
{
    int esz;
    esz = INTB;
    if (isword("extern") || isword("register") || isword("static")) next();
    if (isword("unsigned")) next();
    if (isword("long")) { next(); return INTB; }
    if (isword("int")) { next(); return INTB; }
    if (isword("char")) { next(); return 1; }
    if (isword("void")) { next(); return 0; }
    if (isword("bool")) { next(); return 1; }
    if (isword("uint8_t") || isword("ttt_t")) { next(); return 1; }
    if (isword("uint32_t") || isword("size_t")) { next(); return INTB; }
    if (isword("pfunc_t")) { next(); return INTB; }
    return -1;
}

static int is_type_start(void)
{
    if (G->tok != T_ID) return 0;
    if (isword("typedef")) return 1;
    if (isword("extern")) return 1;
    if (isword("register")) return 1;
    if (isword("static")) return 1;
    if (isword("unsigned")) return 1;
    if (isword("long")) return 1;
    if (isword("int")) return 1;
    if (isword("char")) return 1;
    if (isword("void")) return 1;
    if (isword("bool")) return 1;
    if (isword("uint8_t")) return 1;
    if (isword("uint32_t")) return 1;
    if (isword("size_t")) return 1;
    if (isword("ttt_t")) return 1;
    if (isword("pfunc_t")) return 1;
    return 0;
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

static int parse_const_expr_simple(void)
{
    int v, op;
    if (G->tok != T_NUM) die("constant");
    v = G->ival; next();
    while (G->tok == '+' || G->tok == '-') {
        op = G->tok; next();
        if (G->tok != T_NUM) die("constant");
        if (op == '+') v += G->ival; else v -= G->ival;
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
    if (acc('[')) {
        parse_expr();
        need(']');
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

static void parse_assign(void);

static void primary(void)
{
    char name[MAXNAME];
    int si, fi, argc, arr;

    if (G->tok == T_NUM) { emit(OP_PUSH, G->ival, 0); next(); return; }
    if (G->tok == '(') { next(); parse_expr(); need(')'); return; }
    if (G->tok == T_ID) {
        strcpy(name, G->text);
        next();
        if (acc('(')) {
            if (!strcmp(name, "printf")) {
                int sid, nargs;
                if (G->tok != T_STR) die("printf format");
                sid = add_string(G->text);
                next();
                nargs = 0;
                while (acc(',')) { parse_expr(); nargs++; }
                need(')');
                emit(OP_PRINTF, sid, nargs);
                return;
            }
            if (!strcmp(name, "atoi")) {
                while (G->tok != ')' && G->tok != T_EOF) next();
                need(')'); emit(OP_PUSH, 0, 0); return;
            }
            if (!strcmp(name, "fflush")) {
                while (G->tok != ')' && G->tok != T_EOF) next();
                need(')'); emit(OP_PUSH, 0, 0); return;
            }
            fi = find_func(name);
            if (fi < 0) die("bad function");
            argc = 0;
            if (G->tok != ')') {
                for (;;) { parse_expr(); argc++; if (!acc(',')) break; }
            }
            need(')');
            emit(OP_CALL, fi, argc);
            return;
        }
        si = find_sym(name);
        arr = 0;
        if (acc('[')) {
            parse_expr();
            need(']');
            arr = 1;
        }
        emit_load_lvalue(si, arr);
        if (arr && acc('(')) {
            if (G->tok != ')') die("indirect args");
            need(')');
            emit(OP_CALLI, 0, 0);
            return;
        }
        if (G->tok == T_INC || G->tok == T_DEC) {
            int op;
            op = G->tok;
            next();
            /* Post inc/dec rarely used as value here; leave old value. */
            emit(OP_PUSH, op == T_INC ? 1 : -1, 0);
            emit(OP_ADD, 0, 0);
            /* Cannot duplicate old index cheaply, so post inc only scalar. */
            if (arr) die("array postinc");
            emit_store_lvalue(si, 0);
            emit_load_lvalue(si, 0);
            emit(OP_PUSH, op == T_INC ? 1 : -1, 0);
            emit(OP_SUB, 0, 0);
        }
        return;
    }
    die("primary");
}

static void unary(void)
{
    int op, si, arr;
    if (G->tok == '-') { next(); unary(); emit(OP_NEG, 0, 0); return; }
    if (G->tok == '!') { next(); unary(); emit(OP_NOT, 0, 0); return; }
    if (G->tok == T_INC || G->tok == T_DEC) {
        op = G->tok; next();
        lvalue(&si, &arr);
        if (arr) die("array preinc");
        emit_load_lvalue(si, 0);
        emit(OP_PUSH, op == T_INC ? 1 : -1, 0);
        emit(OP_ADD, 0, 0);
        emit_store_lvalue(si, 0);
        emit_load_lvalue(si, 0);
        return;
    }
    primary();
}

static void mul_expr(void)
{
    int op;
    unary();
    for (;;) {
        if (G->tok == '*') op = OP_MUL;
        else if (G->tok == '/') op = OP_DIV;
        else if (G->tok == '%') op = OP_MOD;
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
        if (G->tok == '<') op = OP_LT;
        else if (G->tok == '>') op = OP_GT;
        else if (G->tok == T_LE) op = OP_LE;
        else if (G->tok == T_GE) op = OP_GE;
        else break;
        next(); add_expr(); emit(op, 0, 0);
    }
}

static void eq_expr(void)
{
    int op;
    rel_expr();
    for (;;) {
        if (G->tok == T_EQ) op = OP_EQ;
        else if (G->tok == T_NE) op = OP_NE;
        else break;
        next(); rel_expr(); emit(op, 0, 0);
    }
}

static void band_expr(void)
{
    eq_expr();
    while (G->tok == '&') { next(); eq_expr(); emit(OP_BAND, 0, 0); }
}

static void and_expr(void)
{
    band_expr();
    while (G->tok == T_ANDAND) { next(); band_expr(); emit(OP_AND, 0, 0); }
}

static void or_expr(void)
{
    and_expr();
    while (G->tok == T_OROR) { next(); and_expr(); emit(OP_OR, 0, 0); }
}

static int try_assignment(void)
{
    struct Mark *m;
    int si, arr, op;
    if (G->mark_sp >= 8) die("mark stack");
    m = G->marks + G->mark_sp++;
    mark_get(m);
    if (G->tok != T_ID) { G->mark_sp--; return 0; }
    next();
    if (G->tok == '(') { mark_set(m); G->mark_sp--; return 0; }
    mark_set(m);
    lvalue(&si, &arr);
    if (G->tok == '=' || G->tok == T_ADDEQ || G->tok == T_SUBEQ) {
        op = G->tok; next();
        if (op == T_ADDEQ || op == T_SUBEQ) {
            if (arr) die("array +=");
            emit_load_lvalue(si, 0);
            parse_assign();
            emit(op == T_ADDEQ ? OP_ADD : OP_SUB, 0, 0);
        } else {
            parse_assign();
        }
        emit_store_lvalue(si, arr);
        emit_load_lvalue(si, 0);
        G->mark_sp--;
        return 1;
    }
    mark_set(m);
    G->mark_sp--;
    return 0;
}

static void parse_assign(void)
{
    if (!try_assignment()) or_expr();
}

static void parse_expr(void)
{
    parse_assign();
}

static void expr_stmt(void)
{
    if (G->tok != ';') { parse_expr(); emit(OP_POP, 0, 0); }
    need(';');
}

static void block(void)
{
    need('{');
    while (G->tok != '}') statement();
    need('}');
}

static void if_stmt(void)
{
    int jz, jm;
    next(); need('('); parse_expr(); need(')');
    jz = emit(OP_JZ, 0, 0);
    statement();
    if (isword("else")) {
        jm = emit(OP_JMP, 0, 0);
        patch(jz, G->cp);
        next(); statement();
        patch(jm, G->cp);
    } else patch(jz, G->cp);
}

static void while_stmt(void)
{
    int top, jz;
    next(); need('('); top = G->cp; parse_expr(); need(')');
    jz = emit(OP_JZ, 0, 0);
    statement(); emit(OP_JMP, top, 0); patch(jz, G->cp);
}

static void for_stmt(void)
{
    int top, jz, jm, incp;
    next(); need('(');
    if (G->tok != ';') { parse_expr(); emit(OP_POP, 0, 0); }
    need(';');
    top = G->cp;
    if (G->tok != ';') parse_expr(); else emit(OP_PUSH, 1, 0);
    need(';');
    jz = emit(OP_JZ, 0, 0);
    jm = emit(OP_JMP, 0, 0);
    incp = G->cp;
    if (G->tok != ')') { parse_expr(); emit(OP_POP, 0, 0); }
    need(')');
    emit(OP_JMP, top, 0);
    patch(jm, G->cp);
    statement();
    emit(OP_JMP, incp, 0);
    patch(jz, G->cp);
}

static void return_stmt(void)
{
    next();
    if (G->tok != ';') parse_expr(); else emit(OP_PUSH, 0, 0);
    need(';');
    emit(OP_RET, G->curfunc, 0);
}

static void local_decl(void)
{
    int esz, si, count, bytes, sc;
    char name[MAXNAME];
    esz = type_esize();
    if (esz < 0) die("type");
    for (;;) {
        while (acc('*')) ;
        if (G->tok != T_ID) die("decl name");
        strcpy(name, G->text); next();
        count = 0;
        if (acc('[')) { count = parse_const_expr_simple(); need(']'); }
        si = add_sym(name, K_VAR, G->curfunc + 1);
        G->sym[si].esize = (unsigned char)(esz ? esz : INTB);
        G->sym[si].isarr = (unsigned char)(count > 0);
        G->sym[si].size = count;
        bytes = count > 0 ? count * G->sym[si].esize : G->sym[si].esize;
        G->sym[si].base = alloc_local(bytes);
        sc = G->sym[si].scope;
        if (acc('=')) { parse_expr(); emit_store_lvalue(si, 0); }
        if (!acc(',')) break;
    }
    need(';');
}

static void statement(void)
{
    if (G->tok == '{') { block(); return; }
    if (is_type_start()) { local_decl(); return; }
    if (isword("if")) { if_stmt(); return; }
    if (isword("while")) { while_stmt(); return; }
    if (isword("for")) { for_stmt(); return; }
    if (isword("return")) { return_stmt(); return; }
    if (acc(';')) return;
    expr_stmt();
}

static void skip_typedef(void)
{
    while (G->tok != ';' && G->tok != T_EOF) next();
    if (G->tok == ';') next();
}

static void global_decl_or_func(void)
{
    int esz, si, count, bytes, fi, isfunc, nparam, p_esz;
    char name[MAXNAME], pname[MAXNAME];
    esz = type_esize();
    if (esz < 0) die("global type");
    while (acc('*')) ;
    if (G->tok != T_ID) die("global name");
    strcpy(name, G->text); next();
    if (acc('(')) {
        fi = add_func(name);
        nparam = 0;
        G->curfunc = fi;
        if (G->tok != ')') {
            for (;;) {
                p_esz = type_esize();
                if (p_esz < 0) die("param type");
                while (acc('*')) ;
                if (G->tok == T_ID) { strcpy(pname, G->text); next(); }
                else strcpy(pname, "p");
                if (acc('[')) { while (G->tok != ']') next(); need(']'); }
                si = add_sym(pname, K_VAR, fi + 1);
                G->sym[si].esize = (unsigned char)(p_esz ? p_esz : INTB);
                G->sym[si].base = alloc_local(G->sym[si].esize);
                if (nparam < MAXPARAM) {
                    G->func[fi].pofs[nparam] = (unsigned char)G->sym[si].base;
                    G->func[fi].pesz[nparam] = G->sym[si].esize;
                    nparam++;
                }
                if (!acc(',')) break;
            }
        }
        need(')');
        G->func[fi].nparam = nparam;
        G->func[fi].ret_esize = (unsigned char)(esz ? esz : INTB);
        if (acc(';')) { G->curfunc = -1; return; }
        G->func[fi].entry = G->cp;
        if (is_main_name(name)) G->main_func = fi;
        block();
        emit(OP_PUSH, 0, 0);
        emit(OP_RET, fi, 0);
        G->curfunc = -1;
        return;
    }
    for (;;) {
        count = 0;
        if (acc('[')) { count = parse_const_expr_simple(); need(']'); }
        si = add_sym(name, K_VAR, 0);
        G->sym[si].esize = (unsigned char)(esz ? esz : INTB);
        G->sym[si].isarr = (unsigned char)(count > 0);
        G->sym[si].size = count;
        bytes = count > 0 ? count * G->sym[si].esize : G->sym[si].esize;
        G->sym[si].base = alloc_global(bytes);
        if (acc('=')) {
            if (acc('{')) {
                int idx, f;
                idx = 0;
                while (!acc('}')) {
                    if (G->tok == T_ID && find_func(G->text) >= 0) {
                        f = find_func(G->text); next();
                        G->gmem[G->sym[si].base + idx * INTB] = f & 255;
                        G->gmem[G->sym[si].base + idx * INTB + 1] = f >> 8;
                    } else if (G->tok == T_NUM) {
                        G->gmem[G->sym[si].base + idx * G->sym[si].esize] =
                            (unsigned char)G->ival;
                        next();
                    } else next();
                    idx++;
                    acc(',');
                }
            } else if (G->tok == T_NUM) {
                G->gmem[G->sym[si].base] = (unsigned char)(G->ival & 255);
                G->gmem[G->sym[si].base + 1] = (unsigned char)(G->ival >> 8);
                next();
            }
        }
        if (!acc(',')) break;
        if (G->tok != T_ID) die("global name");
        strcpy(name, G->text); next();
    }
    need(';');
}

static void program(void)
{
    G->main_func = -1;
    while (G->tok != T_EOF) {
        if (isword("typedef")) { skip_typedef(); continue; }
        if (is_type_start()) global_decl_or_func();
        else next();
    }
    if (G->main_func < 0) {
        int mi;
        for (mi = 0; mi < G->nfunc; mi++)
            if (is_main_name(G->func[mi].name)) G->main_func = mi;
    }
}

static int mem_get(int base, int esz, int idx, unsigned char *m)
{
    int a, v;
    a = base + idx * esz;
    if (esz == 1) return m[a];
    v = m[a] | (m[a + 1] << 8);
    return (short)v;
}

static void mem_set(int base, int esz, int idx, unsigned char *m, int v)
{
    int a;
    a = base + idx * esz;
    if (esz == 1) { m[a] = (unsigned char)v; return; }
    m[a] = (unsigned char)(v & 255);
    m[a + 1] = (unsigned char)((v >> 8) & 255);
}

static void call_func(int fi, int retpc, int argc)
{
    int i, v;
    if (G->fp + 1 >= MAXFRAME) die("frame full");
    G->fp++;
    G->flp = G->floc + G->fp * G->frame_size;
    memset(G->flp, 0, (unsigned int)G->frame_size);
    G->fret[G->fp] = retpc;
    for (i = argc - 1; i >= 0; i--) {
        v = popv();
        if (i < G->func[fi].nparam)
            mem_set(G->func[fi].pofs[i], G->func[fi].pesz[i], 0, G->flp, v);
    }
}

static void print_fmt(const char *fmt, int nargs, int *vals)
{
    int i, vi, c;
    vi = 0;
    for (i = 0; fmt[i]; i++) {
        c = fmt[i];
        if (c == '%' && fmt[i + 1]) {
            i++;
            while (fmt[i] == 'l' || fmt[i] == 'u' || fmt[i] == 'd') {
                if (fmt[i] == 'u' || fmt[i] == 'd') break;
                i++;
            }
            if (vi < nargs) printf("%d", vals[vi++]);
        } else putchar(c);
    }
}

static void run(void)
{
    int pc, a, b, v, fi, argc, vals[8], i;
    struct Ins *in;
    G->stp = G->st;
    G->fp = 0;
    G->flp = G->floc;
    memset(G->floc, 0, (unsigned int)(MAXFRAME * G->frame_size));
    if (saved_main_func < 0) die("no main");
    G->main_func = saved_main_func;
    if (G->func[G->main_func].nparam >= 2) { pushv(1); pushv(0); argc = 2; }
    else argc = 0;
    call_func(G->main_func, -1, argc);
    pc = G->func[G->main_func].entry;
    for (;;) {
        in = &G->code[pc++];
        switch (in->op) {
        case OP_HALT: return;
        case OP_PUSH: pushv(in->a); break;
        case OP_LDG: pushv(mem_get(in->a, INTB, 0, G->gmem)); break;
        case OP_STG: mem_set(in->a, INTB, 0, G->gmem, popv()); break;
        case OP_LDL: pushv(mem_get(in->a, INTB, 0, G->flp)); break;
        case OP_STL: mem_set(in->a, INTB, 0, G->flp, popv()); break;
        case OP_LDGB: pushv(mem_get(in->a, 1, 0, G->gmem)); break;
        case OP_STGB: mem_set(in->a, 1, 0, G->gmem, popv()); break;
        case OP_LDLB: pushv(mem_get(in->a, 1, 0, G->flp)); break;
        case OP_STLB: mem_set(in->a, 1, 0, G->flp, popv()); break;
        case OP_LDGA: a = popv(); pushv(mem_get(in->a, INTB, a, G->gmem)); break;
        case OP_STGA: v = popv(); a = popv(); mem_set(in->a, INTB, a, G->gmem, v); break;
        case OP_LDLA: a = popv(); pushv(mem_get(in->a, INTB, a, G->flp)); break;
        case OP_STLA: v = popv(); a = popv(); mem_set(in->a, INTB, a, G->flp, v); break;
        case OP_LDGAB: a = popv(); pushv(mem_get(in->a, 1, a, G->gmem)); break;
        case OP_STGAB: v = popv(); a = popv(); mem_set(in->a, 1, a, G->gmem, v); break;
        case OP_LDLAB: a = popv(); pushv(mem_get(in->a, 1, a, G->flp)); break;
        case OP_STLAB: v = popv(); a = popv(); mem_set(in->a, 1, a, G->flp, v); break;
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
        case OP_BAND: b=popv(); a=popv(); pushv(a&b); break;
        case OP_JMP: pc = in->a; break;
        case OP_JZ: a=popv(); if(!a) pc=in->a; break;
        case OP_JNZ: a=popv(); if(a) pc=in->a; break;
        case OP_CALL: call_func(in->a, pc, in->b); pc = G->func[in->a].entry; break;
        case OP_CALLI: fi = popv(); call_func(fi, pc, 0); pc = G->func[fi].entry; break;
        case OP_RET:
            v = popv(); pc = G->fret[G->fp]; G->fp--;
            if (pc < 0) return;
            G->flp = G->floc + G->fp * G->frame_size; pushv(v); break;
        case OP_POP: (void)popv(); break;
        case OP_PRINTF:
            for (i = in->b - 1; i >= 0; i--) vals[i] = popv();
            print_fmt(G->strs[in->a], in->b, vals); pushv(0); break;
        default: die("bad op");
        }
    }
}

static char *preprocess(char *in)
{
    char *out;
    int i, o;
    int c, active, ifsp;
    int ndefs, j;
    char *name;
    char *val;
    char *id;
    char **defn;
    char **defv;
    int *ifact;

    name = G->pp_name;
    val = G->pp_val;
    id = G->pp_id;
    defn = G->pp_defn;
    defv = G->pp_defv;
    ifact = G->pp_ifact;

    out = (char *)malloc((unsigned int)strlen(in) + 1);
    if (!out) die("oom");
    for (j = 0; j < 32; j++) {
        defn[j] = 0;
        defv[j] = 0;
    }

    i = 0;
    o = 0;
    active = 1;
    ifsp = 0;
    ndefs = 0;
    while (in[i]) {
        if (in[i] == '/' && in[i + 1] == '*') {
            i += 2;
            if (active) out[o++] = ' ';
            while (in[i] && !(in[i] == '*' && in[i + 1] == '/')) {
                if (in[i] == '\n' && active) out[o++] = '\n';
                i++;
            }
            if (in[i]) i += 2;
            continue;
        }
        if (in[i] == '/' && in[i + 1] == '/') {
            i += 2;
            while (in[i] && in[i] != '\n') i++;
            continue;
        }
        if (in[i] == '#') {
            i++;
            while (in[i] == ' ' || in[i] == '\t') i++;
            j = 0;
            while (isalpha((unsigned char)in[i]) && j < 63)
                name[j++] = in[i++];
            name[j] = 0;
            while (in[i] == ' ' || in[i] == '\t') i++;
            if (!strcmp(name, "define")) {
                j = 0;
                while ((isalnum((unsigned char)in[i]) || in[i] == '_') && j < 63)
                    name[j++] = in[i++];
                name[j] = 0;
                while (in[i] == ' ' || in[i] == '\t') i++;
                j = 0;
                while (in[i] && in[i] != '\n' && j < 63)
                    val[j++] = in[i++];
                val[j] = 0;
                while (j > 0 && isspace((unsigned char)val[j - 1]))
                    val[--j] = 0;
                if (ndefs < 32) {
                    defn[ndefs] = xstrdup2(name);
                    defv[ndefs] = xstrdup2(val);
                    ndefs++;
                }
            } else if (!strcmp(name, "if")) {
                j = 0;
                while (in[i] && in[i] != '\n' && j < 63)
                    val[j++] = in[i++];
                val[j] = 0;
                if (ifsp < 16) ifact[ifsp++] = active;
                active = active && (strstr(val, "true") || strstr(val, "1"));
            } else if (!strcmp(name, "else")) {
                if (ifsp > 0) active = ifact[ifsp - 1] && !active;
            } else if (!strcmp(name, "endif")) {
                if (ifsp > 0) active = ifact[--ifsp];
            }
            while (in[i] && in[i] != '\n') i++;
            if (in[i] == '\n') {
                if (active) out[o++] = '\n';
                i++;
            }
            continue;
        }
        if (!active) {
            if (in[i] == '\n') { out[o++] = '\n'; }
            i++;
            continue;
        }
        if (isalpha((unsigned char)in[i]) || in[i] == '_') {
            int k;
            const char *rp;
            j = 0;
            while (isalnum((unsigned char)in[i]) || in[i] == '_') {
                if (j < 63) id[j++] = in[i];
                i++;
            }
            id[j] = 0;
            for (k = 0; k < ndefs; k++) {
                if (!strcmp(defn[k], id)) break;
            }
            if (k < ndefs && (isdigit((unsigned char)defv[k][0]) ||
                              !strcmp(defv[k], "true") ||
                              !strcmp(defv[k], "false"))) {
                rp = defv[k];
                if (!strcmp(rp, "true")) rp = "1";
                if (!strcmp(rp, "false")) rp = "0";
                while (*rp) out[o++] = *rp++;
            } else {
                for (k = 0; id[k]; k++) out[o++] = id[k];
            }
            continue;
        }
        c = (unsigned char)in[i++];
        if (c != 0x1a) out[o++] = (char)c;
    }
    out[o] = 0;
    for (j = 0; j < ndefs; j++) {
        free(defn[j]);
        free(defv[j]);
    }
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
    saved_main_func = -1;
    G->marks = (struct Mark *)calloc(8, sizeof(struct Mark));
    if (!G->marks) { fprintf(stderr, "oom\n"); exit(1); }
    G->curfunc = -1;
    G->frame_size = 2;
}

static void init_compile_storage(void)
{
    G->code = (struct Ins *)xcalloc(MAXCODE, sizeof(struct Ins));
    G->sym = (struct Sym *)xcalloc(MAXSYM, sizeof(struct Sym));
    G->func = (struct Func *)xcalloc(MAXFUNC, sizeof(struct Func));
    G->strs = (char **)xcalloc(MAXSTR, sizeof(char *));
    G->gmem = (unsigned char *)xcalloc(MAXMEM, 1);
}

static void init_run_storage(void)
{
    G->st = (int *)xcalloc(MAXSTACK, sizeof(int));
    G->fret = (int *)xcalloc(MAXFRAME, sizeof(int));
    G->floc = (unsigned char *)xcalloc((unsigned int)(MAXFRAME * G->frame_size), 1);
}

int main(int argc, char **argv)
{
    int argi;
    init_state();
    argi = 1;
    if (argi < argc && !strcmp(argv[argi], "-V")) { G->verbose = 1; argi++; }
    if (argi >= argc) { fprintf(stderr, "usage: cint [-V] file.c\n"); return 1; }
    if (!load_file(argv[argi])) return 1;
    init_compile_storage();
    next();
    program();
    saved_main_func = G->main_func;
    /* Keep source and symbols until exit; CP/M heap/free pressure is low
       here and this avoids possible allocator corruption before run(). */
    if (G->frame_size < 2) G->frame_size = 2;
    init_run_storage();
    run();
    return 0;
}
