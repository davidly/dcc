/* pint.c - tiny Pascal subset compiler/interpreter for DCC/C89.
 * Supports enough Pascal for E.PAS, SIEVE.PAS, TTT.PAS, and NQ1D.PAS.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXSRC 50000L
#define MAXTOK 64
#define MAXNAME 16
#define MAXSYM 128
#define MAXPROC 16
#define MAXCODE_MAX 2000
#define MAXMEM 8200
#define MAXSTACK 64
#define MAXFRAME 16
#define MAXLOC 16
#define MAXPARAM 8
#define MAXSTR 16
#define MAXVARLIST 16
#define INT_BYTES 2   /* bytes per integer in the VM (Z80: sizeof(int)==2) */

#define K_CONST 1
#define K_VAR   2
#define K_TYPE  3
#define K_PROC  4
#define K_FUNC  5

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
#define OP_ADD 10
#define OP_SUB 11
#define OP_MUL 12
#define OP_DIV 13
#define OP_MOD 14
#define OP_NEG 15
#define OP_EQ 16
#define OP_NE 17
#define OP_LT 18
#define OP_LE 19
#define OP_GT 20
#define OP_GE 21
#define OP_AND 22
#define OP_OR 23
#define OP_NOT 24
#define OP_SHL 25
#define OP_JMP 26
#define OP_JZ 27
#define OP_JNZ 28
#define OP_CALL 29
#define OP_RET 30
#define OP_WRI 31
#define OP_WRS 32
#define OP_WLN 33
#define OP_ODD 34
#define OP_LDGB  35   /* load byte from global scalar */
#define OP_STGB  36   /* store byte to global scalar */
#define OP_LDLB  37   /* load byte from local scalar */
#define OP_STLB  38   /* store byte to local scalar */
#define OP_LDGAB 39   /* load byte from global array */
#define OP_STGAB 40   /* store byte to global array */
#define OP_LDLAB 41   /* load byte from local array */
#define OP_STLAB 42   /* store byte to local array */

struct Ins {
    unsigned char op;
    int a;
    int b;
    /* add filler so array access is faster power of 2. about 1% faster.
        unsigned char f1;
        unsigned char f2;
        unsigned char f3;
    */
};

struct Sym {
    char name[MAXNAME];
    unsigned char kind;
    unsigned char scope;
    unsigned char esize;   /* element byte size: 1 or INT_BYTES */
    int val;
    int size;
    unsigned char isarr;
    int base;
    unsigned char proc;
};

struct Proc {
    char name[MAXNAME];
    int entry;
    unsigned char nparam;
    unsigned char locals;
    unsigned char ret_off;
    unsigned char isfunc;
    unsigned char pofs[MAXPARAM];
    unsigned char pesz[MAXPARAM];  /* parameter element sizes */
    int filler_to_32;
};

static struct Ins *code;
static struct Sym *sym;
static struct Proc *proc;
static struct Ins **fret;
static unsigned char *floc;
static unsigned char *flp;

static unsigned char *gmem;
static int *st;
static int *stp;
static char **strs;

static int cp;
static int code_limit;
static int opt_verbose;
static int nsym;
static int nproc;
static char *src;
static long slen;
static long pos;
static int line_no = 1;
static int tok;
static int ival;
static char text[MAXTOK];
static int gtop;
static int curproc = -1;
static int errcnt;
static int last_esize = INT_BYTES;  /* element byte size from last type_size() */
static int last_isarr = 0;          /* was last type_size() call an array? */
static int nstr;
static int sp;
static int fp;

static void parse_expr(void);
static void statement(void);

static void die(const char *s)
{
    fprintf(stderr, "pint:%d: %s near '%s'\n", line_no, s, text);
    exit(1);
}

static char *xstrdup(const char *s)
{
    char *p;

    p = (char *)malloc(strlen(s) + 1);
    if (!p)
        die("oom");
    strcpy(p, s);
    return p;
}

static int emit(int op, int a, int b)
{
    if (cp >= code_limit)
        die("code full");
    code[cp].op = op;
    code[cp].a = a;
    code[cp].b = b;
    return cp++;
}

static void patch(int at, int v)
{
    code[at].a = v;
}

static int isword(int t, const char *s)
{
    return t == 256 && strcmp(text, s) == 0;
}

static int load_op(int scope)
{
    if (scope)
        return OP_LDL;
    return OP_LDG;
}

static int store_op(int scope)
{
    if (scope)
        return OP_STL;
    return OP_STG;
}

static int loada_op(int scope)
{
    if (scope)
        return OP_LDLA;
    return OP_LDGA;
}

static int storea_op(int scope)
{
    if (scope)
        return OP_STLA;
    return OP_STGA;
}

static int load_op_e(int scope, int esize)
{
    if (esize == 1)
        return scope ? OP_LDLB : OP_LDGB;
    return scope ? OP_LDL : OP_LDG;
}

static int store_op_e(int scope, int esize)
{
    if (esize == 1)
        return scope ? OP_STLB : OP_STGB;
    return scope ? OP_STL : OP_STG;
}

static int loada_op_e(int scope, int esize)
{
    if (esize == 1)
        return scope ? OP_LDLAB : OP_LDGAB;
    return scope ? OP_LDLA : OP_LDGA;
}

static int storea_op_e(int scope, int esize)
{
    if (esize == 1)
        return scope ? OP_STLAB : OP_STGAB;
    return scope ? OP_STLA : OP_STGA;
}

static int func_kind(int isfunc)
{
    if (isfunc)
        return K_FUNC;
    return K_PROC;
}

static int alloc_temp(void)
{
    int off;
    if (curproc >= 0) {
        off = proc[curproc].locals;
        proc[curproc].locals += INT_BYTES;
        return off;
    }
    off = gtop;
    gtop += INT_BYTES;
    return off;
}

static void skip_brace_comment(void)
{
    int nest;

    pos++;
    nest = 1;
    while (pos < slen && nest) {
        if (src[pos] == '\n')
            line_no++;
        if (src[pos] == '{')
            nest++;
        else if (src[pos] == '}')
            nest--;
        pos++;
    }
}

static void skip_star_comment(void)
{
    pos += 2;
    while (pos + 1 < slen && !(src[pos] == '*' && src[pos + 1] == ')')) {
        if (src[pos] == '\n')
            line_no++;
        pos++;
    }
    if (pos + 1 < slen)
        pos += 2;
}

static void skipws(void)
{
    for (;;) {
        while (pos < slen && isspace((unsigned char)src[pos])) {
            if (src[pos] == '\n')
                line_no++;
            pos++;
        }
        if (pos < slen && src[pos] == '{') {
            skip_brace_comment();
            continue;
        }
        if (pos + 1 < slen && src[pos] == '(' && src[pos + 1] == '*') {
            skip_star_comment();
            continue;
        }
        break;
    }
}

static void scan_ident(int c)
{
    int i;

    i = 0;
    do {
        if (i < MAXNAME - 1)
            text[i++] = (char)tolower(c);
        if (pos >= slen)
            break;
        c = (unsigned char)src[pos];
        if (isalnum(c) || c == '_')
            pos++;
        else
            break;
    } while (1);
    text[i] = 0;
    tok = 256;
}

static void scan_number(int c)
{
    long v;

    v = 0;
    while (isdigit(c)) {
        v = v * 10 + c - '0';
        if (pos >= slen)
            break;
        c = (unsigned char)src[pos++];
    }
    if (pos <= slen && !isdigit(c))
        pos--;
    ival = (int)v;
    tok = 257;
    sprintf(text, "%d", ival);
}

static void scan_string(void)
{
    int i;

    i = 0;
    while (pos < slen && src[pos] != '\'') {
        if (i < MAXTOK - 1)
            text[i++] = src[pos];
        pos++;
    }
    if (pos < slen)
        pos++;
    text[i] = 0;
    tok = 258;
}

static void next(void)
{
    int c;

    skipws();
    text[0] = 0;
    tok = 0;
    ival = 0;
    if (pos >= slen) {
        tok = 0;
        return;
    }

    c = (unsigned char)src[pos++];
    if (isalpha(c) || c == '_') {
        scan_ident(c);
        return;
    }
    if (isdigit(c)) {
        scan_number(c);
        return;
    }
    if (c == '\'') {
        scan_string();
        return;
    }
    if (c == ':' && pos < slen && src[pos] == '=') {
        pos++;
        tok = 300;
        strcpy(text, ":=");
        return;
    }
    if (c == '<' && pos < slen && src[pos] == '>') {
        pos++;
        tok = 301;
        strcpy(text, "<>");
        return;
    }
    if (c == '<' && pos < slen && src[pos] == '=') {
        pos++;
        tok = 302;
        strcpy(text, "<=");
        return;
    }
    if (c == '>' && pos < slen && src[pos] == '=') {
        pos++;
        tok = 303;
        strcpy(text, ">=");
        return;
    }
    if (c == '.' && pos < slen && src[pos] == '.') {
        pos++;
        tok = 304;
        strcpy(text, "..");
        return;
    }
    tok = c;
    text[0] = (char)c;
    text[1] = 0;
}

static void need(int t, const char *s)
{
    if (tok != t || (s && strcmp(text, s)))
        die("syntax");
    next();
}

static int acc(int t, const char *s)
{
    if (tok == t && (!s || strcmp(text, s) == 0)) {
        next();
        return 1;
    }
    return 0;
}

static int find_scope(const char *n, int sc)
{
    int i;

    for (i = nsym - 1; i >= 0; i--)
        if (sym[i].scope == sc && strcmp(sym[i].name, n) == 0)
            return i;
    return -1;
}

static int find_sym(const char *n)
{
    int i;

    if (curproc >= 0) {
        i = find_scope(n, curproc + 1);
        if (i >= 0)
            return i;
    }
    i = find_scope(n, 0);
    if (i >= 0)
        return i;
    die("undefined identifier");
    return 0;
}

static int add_sym(const char *n, int k, int sc)
{
    int i;

    if (nsym >= MAXSYM)
        die("sym full");
    i = nsym++;
    memset(&sym[i], 0, sizeof(sym[i]));
    strncpy(sym[i].name, n, MAXNAME - 1);
    sym[i].kind = k;
    sym[i].scope = sc;
    sym[i].size = INT_BYTES;
    sym[i].esize = INT_BYTES;
    return i;
}

static int find_proc(const char *n)
{
    int i;

    for (i = 0; i < nproc; i++)
        if (strcmp(proc[i].name, n) == 0)
            return i;
    return -1;
}

static int parse_const_value(void)
{
    int sign;
    int v;

    sign = 1;
    if (tok == '-') {
        sign = -1;
        next();
    }
    if (tok == 257) {
        v = ival;
        next();
        return sign * v;
    }
    if (tok == 256) {
        v = sym[find_sym(text)].val;
        next();
        return sign * v;
    }
    die("constant expected");
    return 0;
}

static int type_size(void)
{
    int lo, hi, sid, esz, n;

    if (isword(tok, "integer")) {
        next();
        last_esize = INT_BYTES; last_isarr = 0;
        return INT_BYTES;
    }
    if (isword(tok, "boolean") || isword(tok, "byte")) {
        next();
        last_esize = 1; last_isarr = 0;
        return 1;
    }
    sid = find_scope(text, 0);
    if (sid >= 0 && sym[sid].kind == K_TYPE) {
        next();
        last_esize = sym[sid].esize;
        last_isarr = sym[sid].isarr;
        return sym[sid].size;
    }
    if (isword(tok, "array")) {
        next();
        need('[', 0);
        lo = parse_const_value();
        need(304, "..");
        hi = parse_const_value();
        need(']', 0);
        need(256, "of");
        n = hi - lo + 1;
        type_size();
        esz = last_esize;
        last_esize = esz;
        last_isarr = 1;
        return n * esz;
    }
    die("type expected");
    return INT_BYTES;
}

static void call_builtin(const char *name)
{
    int id;

    if (strcmp(name, "write") == 0 || strcmp(name, "writeln") == 0) {
        if (acc('(', 0)) {
            if (tok != ')') {
                for (;;) {
                    if (tok == 258) {
                        id = nstr++;
                        strs[id] = xstrdup(text);
                        next();
                        emit(OP_WRS, id, 0);
                    } else {
                        parse_expr();
                        emit(OP_WRI, 0, 0);
                    }
                    if (!acc(',', 0))
                        break;
                }
            }
            need(')', 0);
        }
        if (strcmp(name, "writeln") == 0)
            emit(OP_WLN, 0, 0);
        return;
    }
    die("unknown builtin");
}

static void parse_call_name(const char *name)
{
    int pi;
    int argc;

    pi = find_proc(name);
    argc = 0;
    if (pi < 0) {
        call_builtin(name);
        return;
    }
    if (acc('(', 0)) {
        if (tok != ')') {
            for (;;) {
                parse_expr();
                argc++;
                if (!acc(',', 0))
                    break;
            }
        }
        need(')', 0);
    }
    if (argc != proc[pi].nparam)
        die("argument count");
    emit(OP_CALL, pi, 0);
}

static void factor_call_or_var(const char *name)
{
    int si;
    int pi;
    int argc;

    if (acc('(', 0)) {
        pi = find_proc(name);
        argc = 0;
        if (pi < 0)
            die("bad function");
        if (tok != ')') {
            for (;;) {
                parse_expr();
                argc++;
                if (!acc(',', 0))
                    break;
            }
        }
        need(')', 0);
        if (argc != proc[pi].nparam)
            die("argument count");
        emit(OP_CALL, pi, 0);
        return;
    }

    si = find_sym(name);
    if (sym[si].kind == K_CONST) {
        emit(OP_PUSH, sym[si].val, 0);
        return;
    }
    if (sym[si].kind == K_PROC || sym[si].kind == K_FUNC) {
        emit(OP_CALL, sym[si].proc, 0);
        return;
    }
    if (acc('[', 0)) {
        parse_expr();
        need(']', 0);
        emit(loada_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
    } else {
        emit(load_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
    }
}

static void factor(void)
{
    char name[32];

    if (tok == 257) {
        emit(OP_PUSH, ival, 0);
        next();
        return;
    }
    if (tok == '(') {
        next();
        parse_expr();
        need(')', 0);
        return;
    }
    if (tok == '-') {
        next();
        factor();
        emit(OP_NEG, 0, 0);
        return;
    }
    if (isword(tok, "not")) {
        next();
        factor();
        emit(OP_NOT, 0, 0);
        return;
    }
    if (isword(tok, "odd")) {
        next();
        need('(', 0);
        parse_expr();
        need(')', 0);
        emit(OP_ODD, 0, 0);
        return;
    }
    if (tok == 256) {
        strcpy(name, text);
        next();
        factor_call_or_var(name);
        return;
    }
    die("factor");
}

static void term(void)
{
    int op;

    factor();
    for (;;) {
        if (tok == '*') {
            op = OP_MUL;
            next();
        } else if (isword(tok, "div")) {
            op = OP_DIV;
            next();
        } else if (isword(tok, "mod")) {
            op = OP_MOD;
            next();
        } else if (isword(tok, "and")) {
            op = OP_AND;
            next();
        } else {
            break;
        }
        factor();
        emit(op, 0, 0);
    }
}

static void simple(void)
{
    int op;

    term();
    for (;;) {
        if (tok == '+') {
            op = OP_ADD;
            next();
        } else if (tok == '-') {
            op = OP_SUB;
            next();
        } else if (isword(tok, "or")) {
            op = OP_OR;
            next();
        } else if (isword(tok, "shl")) {
            op = OP_SHL;
            next();
        } else {
            break;
        }
        term();
        emit(op, 0, 0);
    }
}

static void parse_expr(void)
{
    int op;

    op = 0;
    simple();
    if (tok == '=') {
        op = OP_EQ;
        next();
    } else if (tok == 301) {
        op = OP_NE;
        next();
    } else if (tok == '<') {
        op = OP_LT;
        next();
    } else if (tok == 302) {
        op = OP_LE;
        next();
    } else if (tok == '>') {
        op = OP_GT;
        next();
    } else if (tok == 303) {
        op = OP_GE;
        next();
    }
    if (op) {
        simple();
        emit(op, 0, 0);
    }
}

static void block_stmt(void)
{
    need(256, "begin");
    while (!isword(tok, "end")) {
        statement();
        acc(';', 0);
    }
    need(256, "end");
}

static void assign_or_call(void)
{
    char name[32];
    int si;
    int isarr;

    isarr = 0;
    strcpy(name, text);
    next();
    if (!(tok == 300 || tok == '[')) {
        parse_call_name(name);
        return;
    }

    si = find_sym(name);
    if (sym[si].kind == K_FUNC && sym[si].proc == curproc)
        si = find_scope(name, curproc + 1);
    if (acc('[', 0)) {
        isarr = 1;
        parse_expr();
        need(']', 0);
    }
    need(300, ":=");
    parse_expr();
    if (isarr)
        emit(storea_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
    else
        emit(store_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
}

static void if_stmt(void)
{
    int jz;
    int jmp;

    next();
    parse_expr();
    need(256, "then");
    jz = emit(OP_JZ, 0, 0);
    statement();
    if (isword(tok, "else")) {
        jmp = emit(OP_JMP, 0, 0);
        patch(jz, cp);
        next();
        statement();
        patch(jmp, cp);
    } else {
        patch(jz, cp);
    }
}

static void while_stmt(void)
{
    int top;
    int jz;

    next();
    top = cp;
    parse_expr();
    need(256, "do");
    jz = emit(OP_JZ, 0, 0);
    statement();
    emit(OP_JMP, top, 0);
    patch(jz, cp);
}

static void repeat_stmt(void)
{
    int top;

    next();
    top = cp;
    while (!isword(tok, "until")) {
        statement();
        acc(';', 0);
    }
    next();
    parse_expr();
    emit(OP_JZ, top, 0);
}

static void for_stmt(void)
{
    char vn[32];
    int si;
    int top;
    int jz;
    int limit;

    next();
    if (tok != 256)
        die("for var");
    strcpy(vn, text);
    next();
    si = find_sym(vn);
    need(300, ":=");
    parse_expr();
    emit(store_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
    need(256, "to");
    parse_expr();
    limit = alloc_temp();
    emit(store_op(sym[si].scope), limit, 0);
    need(256, "do");
    top = cp;
    emit(load_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
    emit(load_op(sym[si].scope), limit, 0);
    emit(OP_LE, 0, 0);
    jz = emit(OP_JZ, 0, 0);
    statement();
    emit(load_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
    emit(OP_PUSH, 1, 0);
    emit(OP_ADD, 0, 0);
    emit(store_op_e(sym[si].scope, sym[si].esize), sym[si].base, 0);
    emit(OP_JMP, top, 0);
    patch(jz, cp);
}

static void case_stmt(void)
{
    int endj[64];
    int ne[64];
    int nend;
    int vtmp;
    int sc;
    int scope;

    nend = 0;
    next();
    parse_expr();
    vtmp = alloc_temp();
    if (curproc >= 0)
        scope = 1;
    else
        scope = 0;
    emit(store_op(scope), vtmp, 0);
    need(256, "of");
    while (!isword(tok, "end")) {
        sc = parse_const_value();
        need(':', 0);
        emit(load_op(scope), vtmp, 0);
        emit(OP_PUSH, sc, 0);
        emit(OP_EQ, 0, 0);
        ne[nend] = emit(OP_JZ, 0, 0);
        statement();
        endj[nend] = emit(OP_JMP, 0, 0);
        patch(ne[nend], cp);
        nend++;
        acc(';', 0);
    }
    next();
    while (nend--)
        patch(endj[nend], cp);
}

static void statement(void)
{
    if (tok == 256) {
        if (isword(tok, "begin"))
            block_stmt();
        else if (isword(tok, "if"))
            if_stmt();
        else if (isword(tok, "while"))
            while_stmt();
        else if (isword(tok, "repeat"))
            repeat_stmt();
        else if (isword(tok, "for"))
            for_stmt();
        else if (isword(tok, "case"))
            case_stmt();
        else
            assign_or_call();
    }
}

static int is_section(void)
{
    return isword(tok, "const") || isword(tok, "type") || isword(tok, "var") ||
           isword(tok, "procedure") || isword(tok, "function") ||
           isword(tok, "begin") || isword(tok, "end");
}

static void consts(void)
{
    char n[MAXNAME];
    int si;

    if (!isword(tok, "const"))
        return;
    next();
    while (tok == 256 && !is_section()) {
        strcpy(n, text);
        next();
        need('=', 0);
        si = add_sym(n, K_CONST, 0);
        sym[si].val = parse_const_value();
        need(';', 0);
    }
}

static void types(void)
{
    char n[MAXNAME];
    int si;

    if (!isword(tok, "type"))
        return;
    next();
    while (tok == 256 && !is_section()) {
        strcpy(n, text);
        next();
        need('=', 0);
        si = add_sym(n, K_TYPE, 0);
        sym[si].size = type_size();
        sym[si].esize = last_esize;
        sym[si].isarr = last_isarr;
        need(';', 0);
    }
}

static void add_var_name(const char *name, int sc, int sz)
{
    int si;

    si = add_sym(name, K_VAR, sc);
    sym[si].size = sz;
    sym[si].esize = last_esize;
    sym[si].isarr = last_isarr;
    if (sc) {
        sym[si].base = proc[curproc].locals;
        proc[curproc].locals += sz;
    } else {
        sym[si].base = gtop;
        gtop += sz;
        if (gtop >= MAXMEM * INT_BYTES)
            die("global memory full");
    }
}

static void vars(int sc)
{
    char names[MAXVARLIST][MAXNAME];
    int nn;
    int i;
    int sz;

    if (!isword(tok, "var"))
        return;
    next();
    while (tok == 256 && !is_section()) {
        nn = 0;
        for (;;) {
            if (nn >= MAXVARLIST)
                die("too many names");
            strcpy(names[nn++], text);
            next();
            if (!acc(',', 0))
                break;
        }
        need(':', 0);
        sz = type_size();
        need(';', 0);
        for (i = 0; i < nn; i++)
            add_var_name(names[i], sc, sz);
    }
}

static void parse_params(int pi, int sc)
{
    char pn[MAXNAME];
    int si, esz;

    if (acc('(', 0)) {
        if (tok != ')') {
            for (;;) {
                if (tok != 256)
                    die("param");
                strcpy(pn, text);
                next();
                need(':', 0);
                type_size();
                esz = last_esize;
                si = add_sym(pn, K_VAR, sc);
                sym[si].esize = esz;
                sym[si].base = proc[pi].locals;
                if (proc[pi].nparam >= MAXPARAM)
                    die("too many params");
                proc[pi].pofs[proc[pi].nparam] = proc[pi].locals;
                proc[pi].pesz[proc[pi].nparam] = esz;
                proc[pi].nparam++;
                proc[pi].locals += esz;
                if (!acc(';', 0))
                    break;
            }
        }
        need(')', 0);
    }
}

static void subprog(int isfunc)
{
    char n[MAXNAME];
    int pi;
    int old;
    int si;
    int sc;
    int ret_esz;

    ret_esz = INT_BYTES;
    next();
    if (tok != 256)
        die("proc name");
    strcpy(n, text);
    next();
    if (nproc >= MAXPROC)
        die("too many procedures");
    pi = nproc++;
    memset(&proc[pi], 0, sizeof(proc[pi]));
    strcpy(proc[pi].name, n);
    proc[pi].isfunc = isfunc;
    si = add_sym(n, func_kind(isfunc), 0);
    sym[si].proc = pi;
    old = curproc;
    curproc = pi;
    sc = pi + 1;
    parse_params(pi, sc);
    if (isfunc) {
        need(':', 0);
        type_size();
        ret_esz = last_esize;
        si = add_sym(n, K_VAR, sc);
        sym[si].esize = ret_esz;
        sym[si].base = proc[pi].locals;
        proc[pi].ret_off = proc[pi].locals;
        proc[pi].locals += ret_esz;
    }
    need(';', 0);
    consts();
    types();
    vars(sc);
    proc[pi].entry = cp;
    block_stmt();
    if (isfunc)
        emit(ret_esz == 1 ? OP_LDLB : OP_LDL, proc[pi].ret_off, 0);
    emit(OP_RET, pi, 0);
    need(';', 0);
    curproc = old;
}

static void program(void)
{
    int mainjmp;

    add_sym("true", K_CONST, 0);
    sym[nsym - 1].val = 1;
    add_sym("false", K_CONST, 0);
    sym[nsym - 1].val = 0;
    need(256, "program");
    if (tok == 256)
        next();
    if (acc('(', 0)) {
        while (!acc(')', 0))
            next();
    }
    need(';', 0);
    consts();
    types();
    vars(0);
    mainjmp = emit(OP_JMP, 0, 0);
    while (isword(tok, "procedure") || isword(tok, "function"))
        subprog(isword(tok, "function"));
    vars(0);
    patch(mainjmp, cp);
    block_stmt();
    emit(OP_HALT, 0, 0);
    if (tok == '.')
        next();
}

static inline int popv(void)
{
    return *(stp = stp - 1);
}

static inline void pushv(int v)
{
    *stp++ = v;
}

static void call_proc(int pi, struct Ins *retaddr)
{
    int i;

    if (fp + 1 >= MAXFRAME) {
        fprintf(stderr, "frame overflow\n");
        exit(1);
    }
    fp++;
    flp += MAXLOC * INT_BYTES;
    memset(flp, 0, MAXLOC * INT_BYTES);
    fret[fp] = retaddr;
    for (i = proc[pi].nparam - 1; i >= 0; i--)
        if (proc[pi].pesz[i] == 1)
            flp[proc[pi].pofs[i]] = (unsigned char)popv();
        else
            *(short *)(flp + proc[pi].pofs[i]) = (short)popv();
}

static void run(void)
{
    int a;
    int b;
    int v;
    int pi;
    struct Ins *in;

    in = code;
    fp = 0;
    sp = 0;
    stp = st;
    flp = floc;
    memset(fret, 0, sizeof(struct Ins *) * MAXFRAME);
    memset(floc, 0, MAXFRAME * MAXLOC * INT_BYTES);
    for (;;) {
        switch (in->op) {
        case OP_HALT:
            return;
        case OP_PUSH:
            pushv(in->a);
            break;
        case OP_LDG:
            pushv(*(short *)(gmem + in->a));
            break;
        case OP_STG:
            *(short *)(gmem + in->a) = (short)popv();
            break;
        case OP_LDL:
            pushv(*(short *)(flp + in->a));
            break;
        case OP_STL:
            *(short *)(flp + in->a) = (short)popv();
            break;
        case OP_LDGA:
            a = popv();
            pushv(*(short *)(gmem + in->a + a * INT_BYTES));
            break;
        case OP_STGA:
            v = popv();
            a = popv();
            *(short *)(gmem + in->a + a * INT_BYTES) = (short)v;
            break;
        case OP_LDLA:
            a = popv();
            pushv(*(short *)(flp + in->a + a * INT_BYTES));
            break;
        case OP_STLA:
            v = popv();
            a = popv();
            *(short *)(flp + in->a + a * INT_BYTES) = (short)v;
            break;
        case OP_LDGB:
            pushv(gmem[in->a]);
            break;
        case OP_STGB:
            gmem[in->a] = (unsigned char)popv();
            break;
        case OP_LDLB:
            pushv(flp[in->a]);
            break;
        case OP_STLB:
            flp[in->a] = (unsigned char)popv();
            break;
        case OP_LDGAB:
            a = popv();
            pushv(gmem[in->a + a]);
            break;
        case OP_STGAB:
            v = popv();
            a = popv();
            gmem[in->a + a] = (unsigned char)v;
            break;
        case OP_LDLAB:
            a = popv();
            pushv(flp[in->a + a]);
            break;
        case OP_STLAB:
            v = popv();
            a = popv();
            flp[in->a + a] = (unsigned char)v;
            break;
        case OP_ADD:
            b = popv();
            a = popv();
            pushv(a + b);
            break;
        case OP_SUB:
            b = popv();
            a = popv();
            pushv(a - b);
            break;
        case OP_MUL:
            b = popv();
            a = popv();
            pushv(a * b);
            break;
        case OP_DIV:
            b = popv();
            a = popv();
            if (b)
                pushv(a / b);
            else
                pushv(0);
            break;
        case OP_MOD:
            b = popv();
            a = popv();
            if (b)
                pushv(a % b);
            else
                pushv(0);
            break;
        case OP_NEG:
            a = popv();
            pushv(-a);
            break;
        case OP_EQ:
            b = popv();
            a = popv();
            pushv(a == b);
            break;
        case OP_NE:
            b = popv();
            a = popv();
            pushv(a != b);
            break;
        case OP_LT:
            b = popv();
            a = popv();
            pushv(a < b);
            break;
        case OP_LE:
            b = popv();
            a = popv();
            pushv(a <= b);
            break;
        case OP_GT:
            b = popv();
            a = popv();
            pushv(a > b);
            break;
        case OP_GE:
            b = popv();
            a = popv();
            pushv(a >= b);
            break;
        case OP_AND:
            b = popv();
            a = popv();
            pushv(a && b);
            break;
        case OP_OR:
            b = popv();
            a = popv();
            pushv(a || b);
            break;
        case OP_NOT:
            a = popv();
            pushv(!a);
            break;
        case OP_SHL:
            b = popv();
            a = popv();
            pushv(a << b);
            break;
        case OP_JMP:
            in = &code[in->a];
            continue;
        case OP_JZ:
            a = popv();
            if (!a) {
                in = &code[in->a];
                continue;
            }
            break;
        case OP_JNZ:
            a = popv();
            if (a) {
                in = &code[in->a];
                continue;
            }
            break;
        case OP_CALL:
            pi = in->a;
            call_proc(pi, in + 1);
            in = &code[proc[pi].entry];
            continue;
        case OP_RET:
            pi = in->a;
            v = 0;
            if (proc[pi].isfunc)
                v = popv();
            in = fret[fp];
            fp--;
            flp -= MAXLOC * INT_BYTES;
            if (proc[pi].isfunc)
                pushv(v);
            continue;
        case OP_WRI:
            printf("%d", popv());
            break;
        case OP_WRS:
            printf("%s", strs[in->a]);
            break;
        case OP_WLN:
            printf("\n");
            break;
        case OP_ODD:
            a = popv();
            pushv(a & 1);
            break;
        default:
            fprintf(stderr, "bad op %d\n", in->op);
            exit(1);
        }
        in++;
    }
}

static void *xcalloc(size_t n, size_t sz)
{
    void *p;

    p = calloc(n, sz);
    if (!p)
        die("oom");
    return p;
}

static int calc_code_limit(void)
{
    long n;

    n = slen / 8 + 128;
    if (n < 128)
        n = 128;
    if (n > MAXCODE_MAX)
        n = MAXCODE_MAX;
    return (int)n;
}

static void init_compile_storage(void)
{
    code_limit = calc_code_limit();
    code = (struct Ins *)xcalloc(code_limit, sizeof(struct Ins));
    sym = (struct Sym *)xcalloc(MAXSYM, sizeof(struct Sym));
    proc = (struct Proc *)xcalloc(MAXPROC, sizeof(struct Proc));
    strs = (char **)xcalloc(MAXSTR, sizeof(char *));
}

static void init_run_storage(void)
{
    fret = (struct Ins **)xcalloc(MAXFRAME, sizeof(struct Ins *));
    floc = (unsigned char *)xcalloc(MAXFRAME * MAXLOC * INT_BYTES, 1);
    gmem = (unsigned char *)xcalloc(MAXMEM * INT_BYTES, 1);
    st = (int *)xcalloc(MAXSTACK, sizeof(int));
    stp = st;
}

static void free_compile_storage(void)
{
    free(src);
    src = 0;
    free(sym);
    sym = 0;
}

static void print_stats(void)
{
    if (!opt_verbose)
        return;

    fprintf(stderr, "\nPINT usage summary\n");
    fprintf(stderr, "  Source bytes:             %ld / %ld\n", slen, MAXSRC);
    fprintf(stderr, "  Bytecode instructions:    %d / %d\n", cp, code_limit);
    fprintf(stderr, "  Symbols:                  %d / %d\n", nsym, MAXSYM);
    fprintf(stderr, "  Procedures/functions:     %d / %d\n", nproc, MAXPROC);
    fprintf(stderr, "  Pascal strings:           %d / %d\n", nstr, MAXSTR);
    fprintf(stderr, "  Global memory bytes used: %d / %d\n", gtop, MAXMEM * INT_BYTES);
    fprintf(stderr, "  VM stack cells limit:     %d\n", MAXSTACK);
    fprintf(stderr, "  VM call frames limit:     %d\n", MAXFRAME);
    fprintf(stderr, "  VM locals per frame:      %d\n", MAXLOC);
    fprintf(stderr, "  VM local cells allocated: %d\n", MAXFRAME * MAXLOC);
}

static int load_file(const char *name)
{
    FILE *f;
    long n;
    long got;

    f = fopen(name, "rb");
    if (!f) {
        perror(name);
        return 0;
    }

    if (fseek(f, 0L, SEEK_END) != 0) {
        fclose(f);
        perror(name);
        return 0;
    }
    n = ftell(f);
    if (n < 0 || n > MAXSRC) {
        fclose(f);
        fprintf(stderr, "%s: source too large\n", name);
        return 0;
    }
    if (fseek(f, 0L, SEEK_SET) != 0) {
        fclose(f);
        perror(name);
        return 0;
    }

    src = (char *)malloc((size_t)n + 1);
    if (!src) {
        perror("out of ram");
        return 1;
    }
    got = (long)fread(src, 1, (size_t)n, f);
    fclose(f);
    if (got != n) {
        fprintf(stderr, "%s: read error\n", name);
        free(src);
        src = 0;
        return 0;
    }
    src[n] = 0;
    slen = n;
    return 1;
}

int main(int argc, char **argv)
{
    int argi;

    argi = 1;
    if (argi < argc && strcmp(argv[argi], "-V") == 0) {
        opt_verbose = 1;
        argi++;
    }
    if (argi >= argc) {
        fprintf(stderr, "usage: pint [-v] file.pas\n");
        return 1;
    }
    if (!load_file(argv[argi]))
        return 1;
    init_compile_storage();
    next();
    program();
    free_compile_storage();
    if (errcnt)
        return 1;
    init_run_storage();
    run();
    print_stats();
    return 0;
}
