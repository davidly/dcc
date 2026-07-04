/* fint.c - tiny Forth subset compiler/interpreter for DCC/C89 / CP/M-80.
 * fint_inlineprim_safe: inline primitives, but keep original safe return-stack helpers.
 * fint_dcc_align: avoid odd int fields in structs for current dcc.
 * Supports enough Forth for SIEVE.F, e.f, and ttt.f.
 * Integer-only, compact heap-backed state, Ctrl-Z tolerant input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXSRC 8000L
#define MAXTOK 64
#define MAXNAME 18
#define MAXWORDS 80
#define MAXCODE 700
#define MAXSTR 8
#define MAXPATCH 32
#define MAXMEM 8200
#define INITMEM 256
#define MAXSTACK 96
#define MAXRSTACK 48
#define CELL 2

#define W_PRIM 1
#define W_COLON 2
#define W_CONST 3
#define W_VAR 4

#define OP_HALT 0
#define OP_LIT 1
#define OP_CALL 2
#define OP_RET 3
#define OP_JMP 4
#define OP_JZ 5
#define OP_DO 6
#define OP_LOOP 7
#define OP_I 8
#define OP_PSTR 9
#define OP_PRIM 10
#define OP_FETCHA 11
#define OP_STOREA 12
#define OP_CFETCHA 13
#define OP_CSTOREA 14
#define OP_ADDSTOREA 15
#define OP_DUP 16
#define OP_DROP 17
#define OP_SWAP 18
#define OP_OVER 19
#define OP_ROT 20
#define OP_ADD 21
#define OP_SUB 22
#define OP_MUL 23
#define OP_DIVMOD 24
#define OP_MOD 25
#define OP_EQ 26
#define OP_NE 27
#define OP_LT 28
#define OP_GT 29
#define OP_AND 30
#define OP_OR 31
#define OP_INVERT 32
#define OP_FETCH 33
#define OP_STORE 34
#define OP_CFETCH 35
#define OP_CSTORE 36
#define OP_ADDSTORE 37
#define OP_FILL 38
#define OP_DOT 39
#define OP_CR 40
#define OP_EMIT 41

#define P_DUP 1
#define P_DROP 2
#define P_SWAP 3
#define P_OVER 4
#define P_ROT 5
#define P_ADD 6
#define P_SUB 7
#define P_MUL 8
#define P_DIVMOD 9
#define P_MOD 10
#define P_EQ 11
#define P_NE 12
#define P_LT 13
#define P_GT 14
#define P_AND 15
#define P_OR 16
#define P_INVERT 17
#define P_FETCH 18
#define P_STORE 19
#define P_CFETCH 20
#define P_CSTORE 21
#define P_ADDSTORE 22
#define P_FILL 23
#define P_DOT 24
#define P_CR 25
#define P_EMIT 26

struct Ins { int op; int a; };
struct Word { char name[MAXNAME]; int kind; int val; int end; };
struct Ctl { int kind; int a; int b; };

struct State {
    char *s_src;
    long s_slen;
    long s_pos;
    int s_line;
    int s_tok;
    int s_ival;
    char s_text[MAXTOK];
    struct Ins *s_code;
    struct Word *s_words;
    struct Ctl *s_ctl;
    char **s_strs;
    unsigned char *s_mem;
    int *s_st;
    int *s_crs;      /* call return stack: flat int array of saved PCs */
    int *s_lrs_idx;  /* loop index values */
    int *s_lrs_lim;  /* loop limit values */
    int *s_lrs_prv;  /* loop_top link (previous loop frame index) */
    int s_cp;
    int s_nw;
    int s_nc;
    int s_ns;
    int s_mtop;
    int s_mcap;
    int s_sp;
    int s_crp;   /* call return stack pointer */
    int s_lrp;   /* loop stack pointer */
    int s_verbose;
    int s_curword;
    int s_halt;
    int s_loop_top;
};
static struct State *G;

#define src G->s_src
#define slen G->s_slen
#define pos G->s_pos
#define line_no G->s_line
#define tok G->s_tok
#define ival G->s_ival
#define text G->s_text
#define code G->s_code
#define words G->s_words
#define ctl G->s_ctl
#define strs G->s_strs
#define mem G->s_mem
#define st G->s_st
#define crs G->s_crs
#define lrs_idx G->s_lrs_idx
#define lrs_lim G->s_lrs_lim
#define lrs_prv G->s_lrs_prv
#define cp G->s_cp
#define nw G->s_nw
#define nc G->s_nc
#define ns G->s_ns
#define mtop G->s_mtop
#define mcap G->s_mcap
#define sp G->s_sp
#define crp G->s_crp
#define lrp G->s_lrp
#define opt_verbose G->s_verbose
#define curword G->s_curword
#define halted G->s_halt
#define loop_top G->s_loop_top

static void die(const char *s)
{
    fprintf(stderr, "fint:%d: %s near '%s'\n", line_no, s, text);
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

static int emit(int op, int a)
{
    struct Ins *ip;
    if (cp >= MAXCODE) die("code full");
    ip = code + cp;
    ip->op = op;
    ip->a = a;
    return cp++;
}

static void patch(int at, int v)
{
    struct Ins *ip;
    ip = code + at;
    ip->a = v;
}

static int last_is_lit(void)
{
    struct Ins *ip;
    if (cp <= 0)
        return 0;
    ip = code + cp - 1;
    return ip->op == OP_LIT;
}

static void fold_last_lit_to(int op)
{
    struct Ins *ip;
    ip = code + cp - 1;
    ip->op = op;
}

static int op_has_local_target(int op)
{
    return op == OP_JMP || op == OP_JZ || op == OP_DO || op == OP_LOOP;
}

static int can_inline_word(int wi)
{
    int i, start, end, len, op;
    struct Ins *ip;

    if ((words + wi)->kind != W_COLON)
        return 0;
    start = (words + wi)->val;
    end = (words + wi)->end;
    if (end <= start)
        return 0;
    len = end - start;
    if (len > 12)
        return 0;
    if ((code + end - 1)->op != OP_RET)
        return 0;
    for (i = start; i < end - 1; ++i) {
        ip = code + i;
        op = ip->op;
        if (op == OP_RET || op_has_local_target(op) || op == OP_PSTR)
            return 0;
    }
    return 1;
}

static int inline_word(int wi)
{
    int i, start, end;

    if (!can_inline_word(wi))
        return 0;
    start = (words + wi)->val;
    end = (words + wi)->end;
    for (i = start; i < end - 1; ++i) {
        emit((code + i)->op, (code + i)->a);
    }
    return 1;
}

static inline void push(int v) { st[sp++] = v; }
static inline int pop(void) { return st[--sp]; }
static inline int peek(void) { return st[sp - 1]; }

static void grow_mem(int need)
{
    unsigned char *p;
    int ncap;

    if (need <= mcap)
        return;
    if (need > MAXMEM)
        die("memory full");

    ncap = mcap;
    while (ncap < need) {
        if (ncap < 1024)
            ncap += 256;
        else
            ncap += 1024;
    }
    if (ncap > MAXMEM)
        ncap = MAXMEM;

    p = (unsigned char *)realloc(mem, (unsigned int)ncap);
    if (!p)
        die("oom");
    memset(p + mcap, 0, (unsigned int)(ncap - mcap));
    mem = p;
    mcap = ncap;
}

static inline int cell_at(int a)
{
    if (a < 0 || a + 1 >= mcap) die("bad address");
    return (short)(mem[a] | (mem[a + 1] << 8));
}

static inline void set_cell(int a, int v)
{
    if (a < 0 || a + 1 >= mcap) die("bad address");
    mem[a] = (unsigned char)(v & 255);
    mem[a + 1] = (unsigned char)((v >> 8) & 255);
}

static void upper(char *d, const char *s)
{
    int i;
    for (i = 0; s[i] && i < MAXNAME - 1; i++) d[i] = (char)toupper((unsigned char)s[i]);
    d[i] = 0;
}

static int find_word(const char *s)
{
    char n[MAXNAME];
    int i;
    upper(n, s);
    for (i = nw - 1; i >= 0; i--)
        if (!strcmp((words + i)->name, n)) return i;
    return -1;
}

static int add_word(const char *name, int kind, int val)
{
    int i;
    struct Word *wp;
    if (nw >= MAXWORDS) die("too many words");
    i = nw++;
    wp = words + i;
    memset(wp, 0, sizeof(struct Word));
    upper(wp->name, name);
    wp->kind = kind;
    wp->val = val;
    return i;
}

static void add_prim(const char *n, int p) { add_word(n, W_PRIM, p); }

static void init_prims(void)
{
    add_prim("DUP", P_DUP); add_prim("DROP", P_DROP); add_prim("SWAP", P_SWAP);
    add_prim("OVER", P_OVER); add_prim("ROT", P_ROT); add_prim("+", P_ADD);
    add_prim("-", P_SUB); add_prim("*", P_MUL); add_prim("/MOD", P_DIVMOD);
    add_prim("MOD", P_MOD); add_prim("=", P_EQ); add_prim("<>", P_NE);
    add_prim("<", P_LT); add_prim(">", P_GT); add_prim("AND", P_AND);
    add_prim("OR", P_OR); add_prim("INVERT", P_INVERT); add_prim("@", P_FETCH);
    add_prim("!", P_STORE); add_prim("C@", P_CFETCH); add_prim("C!", P_CSTORE);
    add_prim("+!", P_ADDSTORE); add_prim("FILL", P_FILL); add_prim(".", P_DOT);
    add_prim("CR", P_CR); add_prim("EMIT", P_EMIT);
}

static void skip_comment(void)
{
    int c;
    while (pos < slen) {
        c = (unsigned char)src[pos++];
        if (c == '\n') line_no++;
        if (c == ')') break;
        if (c == 0x1a) { pos = slen; break; }
    }
}

static void next(void)
{
    int c, i, sign;
    for (;;) {
        while (pos < slen) {
            c = (unsigned char)src[pos];
            if (c == 0x1a) { pos = slen; break; }
            if (!isspace(c)) break;
            if (c == '\n') line_no++;
            pos++;
        }
        if (pos < slen && src[pos] == '(') { pos++; skip_comment(); continue; }
        break;
    }
    text[0] = 0; tok = 0; ival = 0;
    if (pos >= slen) return;
    c = (unsigned char)src[pos++];
    if (c == '.' && pos < slen && src[pos] == '"') {
        pos++;
        i = 0;
        while (pos < slen && src[pos] != '"') {
            c = (unsigned char)src[pos++];
            if (c == 0x1a) break;
            if (i < MAXTOK - 1) text[i++] = (char)c;
        }
        if (pos < slen && src[pos] == '"') pos++;
        text[i] = 0; tok = 258; return;
    }
    i = 0;
    text[i++] = (char)c;
    while (pos < slen) {
        c = (unsigned char)src[pos];
        if (c == 0x1a || isspace(c) || c == '(') break;
        if (i < MAXTOK - 1) text[i++] = (char)c;
        pos++;
    }
    text[i] = 0;
    sign = 1; i = 0;
    if (text[0] == '-' && text[1]) { sign = -1; i = 1; }
    if (isdigit((unsigned char)text[i])) {
        long v;
        v = 0;
        while (isdigit((unsigned char)text[i])) v = v * 10 + text[i++] - '0';
        if (text[i] == 0) { ival = (int)(v * sign); tok = 257; return; }
    }
    tok = 256;
}

static void need_name(char *out)
{
    next();
    if (tok != 256) die("name expected");
    strcpy(out, text);
}

static int add_string(const char *s)
{
    int i;
    if (ns >= MAXSTR) die("too many strings");
    i = ns++;
    strs[i] = xstrdup2(s);
    return i;
}

static void prim(int p)
{
    int a,b,c,n;
    switch (p) {
    case P_DUP: push(peek()); break;
    case P_DROP: (void)pop(); break;
    case P_SWAP: a=pop(); b=pop(); push(a); push(b); break;
    case P_OVER: if (sp < 2) die("stack empty"); push(st[sp-2]); break;
    case P_ROT: if (sp < 3) die("stack empty"); c=pop(); b=pop(); a=pop(); push(b); push(c); push(a); break;
    case P_ADD: b=pop(); a=pop(); push(a+b); break;
    case P_SUB: b=pop(); a=pop(); push(a-b); break;
    case P_MUL: b=pop(); a=pop(); push(a*b); break;
    case P_DIVMOD: b=pop(); a=pop(); if (b) { push(a % b); push(a / b); } else { push(0); push(0); } break;
    case P_MOD: b=pop(); a=pop(); push(b ? a % b : 0); break;
    case P_EQ: b=pop(); a=pop(); push(a==b); break;
    case P_NE: b=pop(); a=pop(); push(a!=b); break;
    case P_LT: b=pop(); a=pop(); push(a<b); break;
    case P_GT: b=pop(); a=pop(); push(a>b); break;
    case P_AND: b=pop(); a=pop(); push(a & b); break;
    case P_OR: b=pop(); a=pop(); push(a | b); break;
    case P_INVERT: a=pop(); push(!a); break;
    case P_FETCH: a=pop(); push(cell_at(a)); break;
    case P_STORE: a=pop(); b=pop(); set_cell(a,b); break;
    case P_CFETCH: a=pop(); if (a<0 || a>=mcap) die("bad address"); push(mem[a]); break;
    case P_CSTORE: a=pop(); b=pop(); if (a<0 || a>=mcap) die("bad address"); mem[a]=(unsigned char)b; break;
    case P_ADDSTORE: a=pop(); b=pop(); set_cell(a, cell_at(a)+b); break;
    case P_FILL: c=pop(); n=pop(); a=pop(); if(a<0||n<0||a+n>mcap) die("bad fill"); memset(mem+a, c & 255, (unsigned int)n); break;
    case P_DOT: printf("%d ", pop()); break;
    case P_CR: printf("\n"); break;
    case P_EMIT: putchar(pop()); break;
    default: die("bad primitive");
    }
}


static void run_at(int pc)
{
    struct Ins *in;
    struct Ins *lcode;
    unsigned char *lmem;
    int *lst;
    int *lcrs;
    int *llrs_idx;
    int *llrs_lim;
    int *llrs_prv;
    int lsp, lcrp, llrp, lltop, lmcap;
    int a, b;

    lcode   = code;
    lmem    = mem;
    lst     = st;
    lcrs    = crs;
    llrs_idx = lrs_idx;
    llrs_lim = lrs_lim;
    llrs_prv = lrs_prv;
    lsp     = sp;
    lcrp    = crp;
    llrp    = lrp;
    lltop   = loop_top;
    lmcap   = mcap;

    for (;;) {
        in = lcode + pc++;
        switch (in->op) {
        case OP_HALT:
            sp = lsp; crp = lcrp; lrp = llrp; loop_top = lltop;
            return;
        case OP_LIT: lst[lsp++] = in->a; break;
        case OP_PRIM:
            sp = lsp; crp = lcrp; lrp = llrp; loop_top = lltop;
            prim(in->a);
            lsp = sp; lcrp = crp; llrp = lrp; lltop = loop_top;
            break;
        case OP_FETCHA:
            { int _a = in->a; lst[lsp++] = (int)(short)(lmem[_a] | (lmem[_a+1] << 8)); }
            break;
        case OP_STOREA:
            { int _a = in->a; int _v = lst[--lsp]; lmem[_a] = (unsigned char)_v; lmem[_a+1] = (unsigned char)(_v >> 8); }
            break;
        case OP_CFETCHA:
            lst[lsp++] = lmem[in->a];
            break;
        case OP_CSTOREA:
            lmem[in->a] = (unsigned char)lst[--lsp];
            break;
        case OP_ADDSTOREA:
            { int _a = in->a; int _v = (int)(short)(lmem[_a] | (lmem[_a+1] << 8)) + lst[--lsp]; lmem[_a] = (unsigned char)_v; lmem[_a+1] = (unsigned char)(_v >> 8); }
            break;
        case OP_CALL:
            if (lcrp >= MAXRSTACK) die("return stack full");
            lcrs[lcrp++] = pc;
            pc = in->a;
            break;
        case OP_RET:
            if (lcrp <= 0) {
                sp = lsp; crp = lcrp; lrp = llrp; loop_top = lltop;
                return;
            }
            pc = lcrs[--lcrp];
            break;
        case OP_JMP: pc = in->a; break;
        case OP_JZ: { int _v = lst[--lsp]; if (!_v) pc = in->a; } break;
        case OP_DO:
            a = lst[--lsp]; b = lst[--lsp];
            if (llrp >= MAXRSTACK) die("loop stack full");
            llrs_idx[llrp] = a;
            llrs_lim[llrp] = b;
            llrs_prv[llrp] = lltop;
            lltop = llrp++;
            break;
        case OP_LOOP:
            { int _r = llrp - 1; int _i = llrs_idx[_r] + 1; llrs_idx[_r] = _i;
              if (_i < llrs_lim[_r]) pc = in->a;
              else { lltop = llrs_prv[_r]; llrp = _r; } }
            break;
        case OP_I: lst[lsp++] = llrs_idx[lltop]; break;
        case OP_DUP: lst[lsp] = lst[lsp-1]; lsp++; break;
        case OP_DROP: --lsp; break;
        case OP_SWAP: { int _t = lst[lsp-1]; lst[lsp-1] = lst[lsp-2]; lst[lsp-2] = _t; } break;
        case OP_OVER: lst[lsp] = lst[lsp-2]; lsp++; break;
        case OP_ROT:  { int _t = lst[lsp-3]; lst[lsp-3] = lst[lsp-2]; lst[lsp-2] = lst[lsp-1]; lst[lsp-1] = _t; } break;
        case OP_ADD:  { int _t = lst[--lsp]; lst[lsp-1] += _t; } break;
        case OP_SUB:  { int _t = lst[--lsp]; lst[lsp-1] -= _t; } break;
        case OP_MUL:  { int _t = lst[--lsp]; lst[lsp-1] *= _t; } break;
        case OP_DIVMOD: b=lst[--lsp]; a=lst[--lsp]; if (b) { lst[lsp++]=a%b; lst[lsp++]=a/b; } else { lst[lsp++]=0; lst[lsp++]=0; } break;
        case OP_MOD:  b=lst[--lsp]; a=lst[--lsp]; lst[lsp++]=(b ? a%b : 0); break;
        case OP_EQ:   { int _t = lst[--lsp]; lst[lsp-1] = (lst[lsp-1] == _t); } break;
        case OP_NE:   { int _t = lst[--lsp]; lst[lsp-1] = (lst[lsp-1] != _t); } break;
        case OP_LT:   { int _t = lst[--lsp]; lst[lsp-1] = (lst[lsp-1] <  _t); } break;
        case OP_GT:   { int _t = lst[--lsp]; lst[lsp-1] = (lst[lsp-1] >  _t); } break;
        case OP_AND:  { int _t = lst[--lsp]; lst[lsp-1] &= _t; } break;
        case OP_OR:   { int _t = lst[--lsp]; lst[lsp-1] |= _t; } break;
        case OP_INVERT: lst[lsp-1] = !lst[lsp-1]; break;
        case OP_FETCH:
            { int _a = lst[lsp-1]; if (_a < 0 || _a+1 >= lmcap) die("bad address");
              lst[lsp-1] = (int)(short)(lmem[_a] | (lmem[_a+1] << 8)); }
            break;
        case OP_STORE:
            a = lst[--lsp]; if (a < 0 || a+1 >= lmcap) die("bad address");
            b = lst[--lsp]; lmem[a] = (unsigned char)b; lmem[a+1] = (unsigned char)(b >> 8);
            break;
        case OP_CFETCH:
            { int _a = lst[lsp-1]; if (_a < 0 || _a >= lmcap) die("bad address");
              lst[lsp-1] = lmem[_a]; }
            break;
        case OP_CSTORE:
            a = lst[--lsp]; if (a < 0 || a >= lmcap) die("bad address");
            lmem[a] = (unsigned char)lst[--lsp];
            break;
        case OP_ADDSTORE:
            a = lst[--lsp]; if (a < 0 || a+1 >= lmcap) die("bad address");
            b = lst[--lsp];
            { int _v = (int)(short)(lmem[a] | (lmem[a+1] << 8)) + b; lmem[a] = (unsigned char)_v; lmem[a+1] = (unsigned char)(_v >> 8); }
            break;
        case OP_FILL:
            { int c, n; c=lst[--lsp]; n=lst[--lsp]; a=lst[--lsp];
              if(a<0||n<0||a+n>lmcap) die("bad fill"); memset(lmem+a, c & 255, (unsigned int)n); }
            break;
        case OP_DOT:  printf("%d ", lst[--lsp]); break;
        case OP_CR:   printf("\n"); break;
        case OP_EMIT: putchar(lst[--lsp]); break;
        case OP_PSTR: printf("%s", strs[in->a]); break;
        default: die("bad op");
        }
    }
}

static void exec_word(int wi)
{
    switch ((words + wi)->kind) {
    case W_PRIM: prim((words + wi)->val); break;
    case W_CONST: push((words + wi)->val); break;
    case W_VAR: push((words + wi)->val); break;
    case W_COLON: run_at((words + wi)->val); break;
    }
}

static void compile_word(int wi)
{
    int p;

    if ((words + wi)->kind == W_PRIM) {
        p = (words + wi)->val;

        /*
         * Peephole the most common Forth variable forms.  In ttt.f almost
         * every state access is written as "name @", "name !",
         * "name C@", "name C!", or "name +!".  Variables and
         * address constants compile as OP_LIT address, so fold that prior
         * literal into a direct memory opcode.  This preserves stack order:
         *   value addr !   -> prior literal is addr; value remains on stack.
         *   value addr C!  -> same.
         *   delta addr +!  -> same.
         */
        if (last_is_lit()) {
            if (p == P_FETCH) { fold_last_lit_to(OP_FETCHA); return; }
            if (p == P_STORE) { fold_last_lit_to(OP_STOREA); return; }
            if (p == P_CFETCH) { fold_last_lit_to(OP_CFETCHA); return; }
            if (p == P_CSTORE) { fold_last_lit_to(OP_CSTOREA); return; }
            if (p == P_ADDSTORE) { fold_last_lit_to(OP_ADDSTOREA); return; }
        }
        emit(OP_DUP + p - 1, 0);
    } else if ((words + wi)->kind == W_CONST || (words + wi)->kind == W_VAR) {
        emit(OP_LIT, (words + wi)->val);
    } else {
        if (!inline_word(wi))
            emit(OP_CALL, (words + wi)->val);
    }
}

static void ctl_push(int kind, int a, int b)
{
    struct Ctl *cp0;
    if (nc >= MAXPATCH) die("control stack full");
    cp0 = ctl + nc;
    cp0->kind = kind;
    cp0->a = a;
    cp0->b = b;
    nc++;
}

static struct Ctl ctl_pop(int kind)
{
    struct Ctl c;
    if (nc <= 0) die("control stack empty");
    c = ctl[--nc];
    if (c.kind != kind) die("control mismatch");
    return c;
}

static void compile_definition(const char *name)
{
    int wi, w, j;
    struct Ctl c;
    struct Word *cwp;
    wi = add_word(name, W_COLON, cp);
    cwp = words + wi;
    curword = wi;
    for (;;) {
        next();
        if (tok == 0) die("missing ;");
        if (tok == 258) { emit(OP_PSTR, add_string(text)); continue; }
        if (tok == 257) { emit(OP_LIT, ival); continue; }
        if (!strcmp(text, ";")) { emit(OP_RET, 0); cwp->end = cp; break; }
        if (!strcmp(text, "IF")) { j = emit(OP_JZ, 0); ctl_push(1, j, 0); continue; }
        if (!strcmp(text, "ELSE")) { c = ctl_pop(1); j = emit(OP_JMP, 0); patch(c.a, cp); ctl_push(2, j, 0); continue; }
        if (!strcmp(text, "THEN")) { if (nc && (ctl + nc - 1)->kind == 2) c=ctl_pop(2); else c=ctl_pop(1); patch(c.a, cp); continue; }
        if (!strcmp(text, "BEGIN")) { ctl_push(3, cp, 0); continue; }
        if (!strcmp(text, "WHILE")) { c = ctl_pop(3); j = emit(OP_JZ, 0); ctl_push(4, c.a, j); continue; }
        if (!strcmp(text, "REPEAT")) { c = ctl_pop(4); emit(OP_JMP, c.a); patch(c.b, cp); continue; }
        if (!strcmp(text, "DO")) { emit(OP_DO, 0); ctl_push(5, cp, 0); continue; }
        if (!strcmp(text, "LOOP")) { c = ctl_pop(5); emit(OP_LOOP, c.a); continue; }
        if (!strcmp(text, "I")) { emit(OP_I, 0); continue; }
        if (!strcmp(text, "EXIT")) { emit(OP_RET, 0); continue; }
        if (!strcmp(text, "RECURSE")) { emit(OP_CALL, (words + curword)->val); continue; }
        w = find_word(text);
        if (w < 0) die("unknown word");
        compile_word(w);
    }
    curword = -1;
}

static void top_level(void)
{
    char name[MAXTOK];
    int w, v, base, n;
    next();
    while (tok != 0 && !halted) {
        if (tok == 258) { printf("%s", text); next(); continue; }
        if (tok == 257) { push(ival); next(); continue; }
        if (!strcmp(text, "BYE")) break;
        if (!strcmp(text, ":")) { need_name(name); compile_definition(name); next(); continue; }
        if (!strcmp(text, "CONSTANT")) { need_name(name); v = pop(); add_word(name, W_CONST, v); next(); continue; }
        if (!strcmp(text, "VARIABLE")) {
            need_name(name); base = mtop; grow_mem(base + CELL); add_word(name, W_VAR, base); set_cell(base, 0); mtop += CELL; next(); continue;
        }
        if (!strcmp(text, "ALLOT")) { n = pop(); if (n < 0) die("memory full"); grow_mem(mtop + n); mtop += n; next(); continue; }
        if (!strcmp(text, "I")) { if (loop_top < 0) die("I outside loop"); push(lrs_idx[loop_top]); next(); continue; }
        w = find_word(text);
        if (w < 0) die("unknown word");
        exec_word(w);
        next();
    }
}

static int load_file(const char *name)
{
    FILE *f;
    long n, got;
    f = fopen(name, "rb");
    if (!f) { perror(name); return 0; }
    if (fseek(f, 0L, 2) != 0) { fclose(f); return 0; }
    n = ftell(f);
    if (n < 0 || n > MAXSRC) { fclose(f); fprintf(stderr, "%s: source too large\n", name); return 0; }
    if (fseek(f, 0L, 0) != 0) { fclose(f); return 0; }
    src = (char *)malloc((unsigned int)n + 1);
    if (!src) { fclose(f); return 0; }
    got = (long)fread(src, 1, (unsigned int)n, f);
    fclose(f);
    if (got != n) { free(src); src=0; return 0; }
    while (n > 0 && (unsigned char)src[n-1] == 0x1a) n--;
    src[n] = 0; slen = n; return 1;
}

static void init_state(void)
{
    G = (struct State *)calloc(1, sizeof(struct State));
    if (!G) { fprintf(stderr, "out of memory\n"); exit(1); }
    G->s_line = 1;
    G->s_curword = -1;
    G->s_loop_top = -1;
    code = (struct Ins *)xcalloc(MAXCODE, sizeof(struct Ins));
    words = (struct Word *)xcalloc(MAXWORDS, sizeof(struct Word));
    ctl = (struct Ctl *)xcalloc(MAXPATCH, sizeof(struct Ctl));
    strs = (char **)xcalloc(MAXSTR, sizeof(char *));
    mem = (unsigned char *)xcalloc(INITMEM, 1);
    mcap = INITMEM;
    st = (int *)xcalloc(MAXSTACK, sizeof(int));
    crs = (int *)xcalloc(MAXRSTACK, sizeof(int));
    lrs_idx = (int *)xcalloc(MAXRSTACK, sizeof(int));
    lrs_lim = (int *)xcalloc(MAXRSTACK, sizeof(int));
    lrs_prv = (int *)xcalloc(MAXRSTACK, sizeof(int));
    init_prims();
}

static void print_stats(void)
{
    if (!opt_verbose) return;
    fprintf(stderr, "\nFINT usage summary\n");
    fprintf(stderr, "  Source bytes:          %ld / %ld\n", slen, MAXSRC);
    fprintf(stderr, "  Bytecode instructions: %d / %d\n", cp, MAXCODE);
    fprintf(stderr, "  Words:                 %d / %d\n", nw, MAXWORDS);
    fprintf(stderr, "  Strings:               %d / %d\n", ns, MAXSTR);
    fprintf(stderr, "  Data memory bytes:     %d used, %d allocated, %d max\n", mtop, mcap, MAXMEM);
    fprintf(stderr, "  Data stack max:        %d\n", MAXSTACK);
    fprintf(stderr, "  Call return stack max: %d\n", MAXRSTACK);
    fprintf(stderr, "  Loop stack max:        %d\n", MAXRSTACK);
}

int main(int argc, char **argv)
{
    int argi;
    init_state();
    argi = 1;
    if (argi < argc && (!strcmp(argv[argi], "-V") || !strcmp(argv[argi], "-v"))) { opt_verbose=1; argi++; }
    if (argi >= argc) { fprintf(stderr, "usage: fint [-V] file.f\n"); return 1; }
    if (!load_file(argv[argi])) return 1;
    top_level();
    print_stats();
    return 0;
}
