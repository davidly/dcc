/* bint.c - tiny Microsoft BASIC 4.51-ish integer interpreter for DCC/C89.
 * Supports enough BASIC for sieve.bas, e.bas, and ttt.bas.
 * Revision: smallram-ctrlz-v1.  Tables are right-sized for the loaded program;
 * runtime memory is allocated only after compile, and compile-only buffers
 * are freed before execution to fit a 64K CP/M target.
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

#define MAXSRC 6000L
#define MAXLINE 256
#define MAXPROG 128
#define MAXCODE 800
#define MAXTOK 64
#define MAXSYM 32
#define MAXARRS 8
#define MAXFOR 8
#define MAXGOSUB 16
#define MAXSTACK 32
#define MAXSTR 16
#define MAXMEM 8200
#define MAXPATCH 160
#define INT_BYTES 2

#define OP_HALT 0
#define OP_PUSH 1
#define OP_LDV 2
#define OP_STV 3
#define OP_LDA 4
#define OP_STA 5
#define OP_ADD 6
#define OP_SUB 7
#define OP_MUL 8
#define OP_DIV 9
#define OP_MOD 10
#define OP_NEG 11
#define OP_EQ 12
#define OP_NE 13
#define OP_LT 14
#define OP_LE 15
#define OP_GT 16
#define OP_GE 17
#define OP_AND 18
#define OP_JMP 19
#define OP_JZ 20
#define OP_GOSUB 21
#define OP_RET 22
#define OP_FOR 23
#define OP_NEXT 24
#define OP_PRI 25
#define OP_PRS 26
#define OP_PSP 27
#define OP_PNL 28
#define OP_CLR 29

struct Ins { unsigned char op; int a; int b; };
struct Line { int num; char *txt; int pc; };
struct Sym { char name[12]; int scalar; int base; int size; };
struct Patch { int at; int line; };
struct ForFrame { int var; int limit; int step; struct Ins *pc; };

/* Keep large tables out of BSS.  Only small scalar and pointer globals
 * remain static; the actual arrays are allocated from the CP/M heap.
 * Avoid macro aliases here because DCC's simple macro expander can recurse
 * on replacements that contain the same identifier as a struct field.
 */
static char *src;
static long slen;
static struct Line *lines;
static int nlines;
static struct Ins *code;
static int cp;
static struct Sym *sym;
static int nsym;
static int *mem;
static int mtop;
static int *st;
static int sp;
static struct ForFrame *fstk;
static int fsp;
static struct Ins **gstk;
static int gsp;
static char **strs;
static int nstr;
static struct Patch *patches;
static int npatch;
static int opt_verbose;
static char *lp;
static int tok;
static int ival;
static char *text;

static void expr(void);
static void statement(void);

static void die(const char *s)
{
    fprintf(stderr, "bint: %s", s);
    if (text)
        fprintf(stderr, " near '%s'", text);
    fprintf(stderr, "\n");
    exit(1);
}

static char *xstrdup(const char *s)
{
    char *p;

    p = (char *)malloc(strlen(s) + 1);
    if (!p) die("out of memory");
    strcpy(p, s);
    return p;
}

static void *xcalloc(unsigned int n, unsigned int sz)
{
    void *p;

    p = calloc(n, sz);
    if (!p) die("out of memory");
    return p;
}

static void init_compile_storage(void)
{
    lines = (struct Line *)xcalloc(MAXPROG, sizeof(struct Line));
    code = (struct Ins *)xcalloc(MAXCODE, sizeof(struct Ins));
    sym = (struct Sym *)xcalloc(MAXSYM, sizeof(struct Sym));
    strs = (char **)xcalloc(MAXSTR, sizeof(char *));
    patches = (struct Patch *)xcalloc(MAXPATCH, sizeof(struct Patch));
    text = (char *)xcalloc(MAXTOK, 1);
}

static void init_run_storage(void)
{
    mem = (int *)xcalloc(mtop, sizeof(int));
    st = (int *)xcalloc(MAXSTACK, sizeof(int));
    fstk = (struct ForFrame *)xcalloc(MAXFOR, sizeof(struct ForFrame));
    gstk = (struct Ins **)xcalloc(MAXGOSUB, sizeof(struct Ins *));
}

static void free_compile_only_storage(void)
{
    if (src) { free(src); src = 0; }
    if (lines) { free(lines); lines = 0; }
    if (patches) { free(patches); patches = 0; }
}

static int emit(int op, int a, int b)
{
    if (cp >= MAXCODE) die("code full");
    code[cp].op = (unsigned char)op;
    code[cp].a = a;
    code[cp].b = b;
    return cp++;
}

static int add_string(const char *s)
{
    if (nstr >= MAXSTR) die("string table full");
    strs[nstr] = xstrdup(s);
    return nstr++;
}

static int same(const char *a, const char *b) { return strcmp(a, b) == 0; }

static void skipws(void)
{
    while (*lp && *lp != 26 && isspace((unsigned char)*lp)) lp++;
}

static void next(void)
{
    int c, i;
    skipws();
    tok = 0; ival = 0; text[0] = 0;
    c = (unsigned char)*lp;
    if (!c || c == 26) return;
    lp++;
    if (isalpha(c)) {
        i = 0;
        do {
            if (i < MAXTOK - 1) text[i++] = (char)toupper(c);
            c = (unsigned char)*lp;
            if (isalnum(c) || c == '$' || c == '%') lp++; else break;
        } while (1);
        text[i] = 0; tok = 256; return;
    }
    if (isdigit(c)) {
        long v;

        v = c - '0';
        while (isdigit((unsigned char)*lp)) {
            v = v * 10 + *lp - '0';
            lp++;
        }
        ival = (int)v; sprintf(text, "%d", ival); tok = 257; return;
    }
    if (c == '"') {
        i = 0;
        while (*lp && *lp != '"') {
            if (i < MAXTOK - 1) text[i++] = *lp;
            lp++;
        }
        if (*lp == '"') lp++;
        text[i] = 0; tok = 258; return;
    }
    if (c == '<' && *lp == '=') { lp++; tok = 300; strcpy(text, "<="); return; }
    if (c == '>' && *lp == '=') { lp++; tok = 301; strcpy(text, ">="); return; }
    if (c == '<' && *lp == '>') { lp++; tok = 302; strcpy(text, "<>"); return; }
    if (c == '\\') { tok = 303; strcpy(text, "\\"); return; }
    tok = c; text[0] = (char)c; text[1] = 0;
}

static int acc(int t, const char *s)
{
    if (tok == t && (!s || same(text, s))) { next(); return 1; }
    return 0;
}

static void need(int t, const char *s)
{
    if (!acc(t, s)) die("syntax");
}

static int find_line(int n)
{
    int i;
    for (i = 0; i < nlines; i++) if (lines[i].num == n) return i;
    return -1;
}

static void add_patch(int at, int line)
{
    if (npatch >= MAXPATCH) die("too many line refs");
    patches[npatch].at = at; patches[npatch].line = line; npatch++;
}

static int sym_find(const char *name)
{
    int i;
    for (i = 0; i < nsym; i++) if (same(sym[i].name, name)) return i;
    if (nsym >= MAXSYM) die("symbol table full");
    strncpy(sym[nsym].name, name, sizeof(sym[nsym].name) - 1);
    sym[nsym].scalar = mtop++;
    sym[nsym].base = -1;
    sym[nsym].size = 0;
    if (mtop >= MAXMEM) die("memory full");
    return nsym++;
}

static int isvarname(void) { return tok == 256 && !same(text, "REM"); }

static void factor(void)
{
    char name[MAXTOK];
    int si;
    if (tok == 257) { emit(OP_PUSH, ival, 0); next(); return; }
    if (tok == '(') { next(); expr(); need(')', 0); return; }
    if (tok == '-') { next(); factor(); emit(OP_NEG, 0, 0); return; }
    if (isvarname()) {
        strcpy(name, text); next(); si = sym_find(name);
        if (acc('(', 0) || acc('[', 0)) {
            expr(); if (tok == ')') next(); else need(']', 0);
            emit(OP_LDA, si, 0);
        } else emit(OP_LDV, si, 0);
        return;
    }
    die("factor");
}

static void term(void)
{
    int op;
    factor();
    for (;;) {
        if (tok == '*') { op = OP_MUL; next(); }
        else if (tok == '/') { op = OP_DIV; next(); }
        else if (tok == 303) { op = OP_DIV; next(); }
        else if (tok == 256 && same(text, "MOD")) { op = OP_MOD; next(); }
        else break;
        factor(); emit(op, 0, 0);
    }
}

static void sum(void)
{
    int op;
    term();
    for (;;) {
        if (tok == '+') { op = OP_ADD; next(); }
        else if (tok == '-') { op = OP_SUB; next(); }
        else break;
        term(); emit(op, 0, 0);
    }
}

static void relation(void)
{
    int op;

    op = 0;
    sum();
    if (tok == '=') { op = OP_EQ; next(); }
    else if (tok == '<') { op = OP_LT; next(); }
    else if (tok == '>') { op = OP_GT; next(); }
    else if (tok == 300) { op = OP_LE; next(); }
    else if (tok == 301) { op = OP_GE; next(); }
    else if (tok == 302) { op = OP_NE; next(); }
    if (op) { sum(); emit(op, 0, 0); }
}

static void expr(void)
{
    relation();
    while (tok == 256 && same(text, "AND")) {
        next(); relation(); emit(OP_AND, 0, 0);
    }
}

static void assignment_name(const char *name)
{
    int si;
    int isarr;

    si = sym_find(name);
    isarr = 0;
    if (acc('(', 0) || acc('[', 0)) {
        isarr = 1; expr(); if (tok == ')') next(); else need(']', 0);
    }
    need('=', 0);
    expr();
    emit(isarr ? OP_STA : OP_STV, si, 0);
}

static void dim_stmt(void)
{
    char name[MAXTOK];
    int si, n;
    next();
    for (;;) {
        if (tok != 256) die("dim name");
        strcpy(name, text); next(); si = sym_find(name);
        need('(', 0); if (tok != 257) die("dim size"); n = ival + 1; next(); need(')', 0);
        if (sym[si].base < 0) {
            sym[si].base = mtop; sym[si].size = n; mtop += n;
            if (mtop >= MAXMEM) die("memory full");
        }
        if (!acc(',', 0)) break;
    }
}

static void goto_line_op(int op)
{
    int at, ln;
    next();
    if (tok != 257) die("line number");
    ln = ival; next();
    at = emit(op, 0, 0); add_patch(at, ln);
}

static void print_stmt(void)
{
    int nonew;

    nonew = 0;
    next();
    if (tok == 256 && same(text, "USING")) {
        next(); if (tok == 258) next(); if (tok == ';' || tok == ',') next();
    }
    while (tok && tok != ':') {
        if (tok == 258) { emit(OP_PRS, add_string(text), 0); nonew = 0; next(); }
        else if (tok == ';') { nonew = 1; next(); continue; }
        else if (tok == ',') { emit(OP_PSP, 0, 0); nonew = 0; next(); continue; }
        else { expr(); emit(OP_PRI, 0, 0); nonew = 0; }
        if (tok == ';') { nonew = 1; next(); }
        else if (tok == ',') { emit(OP_PSP, 0, 0); nonew = 0; next(); }
        else break;
    }
    if (!nonew) emit(OP_PNL, 0, 0);
}

static void if_stmt(void)
{
    int jz, jmpend, at, ln;
    next(); expr();
    if (tok == 256 && same(text, "THEN")) next();
    if (tok == 256 && same(text, "GOTO")) { next(); }
    if (tok == 257) { ln = ival; next(); at = emit(OP_JZ, cp + 2, 0); at = emit(OP_JMP, 0, 0); add_patch(at, ln); return; }
    jz = emit(OP_JZ, 0, 0);
    while (tok && !(tok == 256 && same(text, "ELSE"))) {
        statement();
        if (tok == ':') next(); else if (!(tok == 256 && same(text, "ELSE"))) break;
    }
    if (tok == 256 && same(text, "ELSE")) {
        jmpend = emit(OP_JMP, 0, 0);
        code[jz].a = cp;
        next();
        while (tok) {
            statement();
            if (tok == ':') next(); else break;
        }
        code[jmpend].a = cp;
    } else {
        code[jz].a = cp;
    }
}

static void for_stmt(void)
{
    char name[MAXTOK];
    int si;
    next();
    if (tok != 256) die("for var");
    strcpy(name, text); next(); si = sym_find(name);
    need('=', 0); expr(); emit(OP_STV, si, 0);
    if (!(tok == 256 && same(text, "TO"))) die("to expected");
    next(); expr(); emit(OP_FOR, si, 1);
}

static void next_stmt(void)
{
    int si;

    si = -1;
    next();
    if (tok == 256) { si = sym_find(text); next(); }
    emit(OP_NEXT, si, 0);
}

static void statement(void)
{
    char name[MAXTOK];
    if (tok == 0) return;
    if (tok == 256 && same(text, "REM")) { tok = 0; return; }
    if (tok == 256 && same(text, "DIM")) { dim_stmt(); return; }
    if (tok == 256 && same(text, "PRINT")) { print_stmt(); return; }
    if (tok == 256 && same(text, "IF")) { if_stmt(); return; }
    if (tok == 256 && same(text, "GOTO")) { goto_line_op(OP_JMP); return; }
    if (tok == 256 && same(text, "GOSUB")) { goto_line_op(OP_GOSUB); return; }
    if (tok == 256 && same(text, "RETURN")) { next(); emit(OP_RET, 0, 0); return; }
    if (tok == 256 && same(text, "FOR")) { for_stmt(); return; }
    if (tok == 256 && same(text, "NEXT")) { next_stmt(); return; }
    if (tok == 256 && (same(text, "END") || same(text, "SYSTEM"))) { next(); emit(OP_HALT, 0, 0); return; }
    if (tok == 256 && same(text, "CLS")) { next(); emit(OP_CLR, 0, 0); return; }
    if (tok == 256) { strcpy(name, text); next(); assignment_name(name); return; }
    die("statement");
}

static void compile_line(char *s)
{
    lp = s; next();
    while (tok) {
        statement();
        if (tok == ':') next(); else break;
    }
}

static void resolve_patches(void)
{
    int i, li;
    for (i = 0; i < npatch; i++) {
        li = find_line(patches[i].line);
        if (li < 0) die("bad line reference");
        code[patches[i].at].a = lines[li].pc;
    }
}

static inline void pushv(int v) { st[sp++] = v; }
static inline int popv(void) { return st[--sp]; }

static void run(void)
{
    int a, b, v, idx, si;
    struct Ins *in;

    in = code;
    for (;;) {
        switch (in->op) {
        case OP_HALT: return;
        case OP_PUSH: pushv(in->a); break;
        case OP_LDV: pushv(mem[sym[in->a].scalar]); break;
        case OP_STV: mem[sym[in->a].scalar] = popv(); break;
        case OP_LDA: {
            struct Sym *symp = &sym[in->a];
            idx = popv();
            if (symp->base < 0 || idx < 0 || idx >= symp->size) pushv(0);
            else pushv(mem[symp->base + idx]);
            break;
        }
        case OP_STA: {
            struct Sym *symp = &sym[in->a];
            v = popv(); idx = popv();
            if (symp->base >= 0 && idx >= 0 && idx < symp->size) mem[symp->base + idx] = v;
            break;
        }
        case OP_ADD: b=popv(); a=popv(); pushv(a+b); break;
        case OP_SUB: b=popv(); a=popv(); pushv(a-b); break;
        case OP_MUL: b=popv(); a=popv(); pushv(a*b); break;
        case OP_DIV: b=popv(); a=popv(); pushv(b ? a/b : 0); break;
        case OP_MOD: b=popv(); a=popv(); pushv(b ? a%b : 0); break;
        case OP_NEG: a=popv(); pushv(-a); break;
        case OP_EQ: b=popv(); a=popv(); pushv(a==b); break;
        case OP_NE: b=popv(); a=popv(); pushv(a!=b); break;
        case OP_LT: b=popv(); a=popv(); pushv(a<b); break;
        case OP_LE: b=popv(); a=popv(); pushv(a<=b); break;
        case OP_GT: b=popv(); a=popv(); pushv(a>b); break;
        case OP_GE: b=popv(); a=popv(); pushv(a>=b); break;
        case OP_AND: b=popv(); a=popv(); pushv(a & b); break;
        case OP_JMP: in = &code[in->a]; continue;
        case OP_JZ: a=popv(); if (!a) { in = &code[in->a]; continue; } break;
        case OP_GOSUB:
            if (gsp >= MAXGOSUB) die("gosub stack full");
            gstk[gsp++] = in + 1; in = &code[in->a]; continue;
        case OP_RET:
            if (gsp <= 0) die("return stack empty");
            in = gstk[--gsp]; continue;
        case OP_FOR: {
            struct ForFrame *fp;
            if (fsp >= MAXFOR) die("for stack full");
            fp = &fstk[fsp];
            fp->var = in->a; fp->limit = popv(); fp->step = in->b; fp->pc = in + 1; fsp++;
            break;
        }
        case OP_NEXT: {
            struct ForFrame *fp;
            struct Sym *vsp;
            int newval;
            if (fsp <= 0) die("next without for");
            si = fsp - 1;
            fp = &fstk[si];
            vsp = &sym[fp->var];
            newval = (mem[vsp->scalar] += fp->step);
            if (newval <= fp->limit) { in = fp->pc; continue; }
            fsp--;
            break;
        }
        case OP_PRI: printf("%d", popv()); break;
        case OP_PRS: printf("%s", strs[in->a]); break;
        case OP_PSP: printf(" "); break;
        case OP_PNL: printf("\n"); break;
        case OP_CLR: break;
        default: die("bad opcode");
        }
        in++;
    }
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
    fseek(f, 0L, 2);
    n = ftell(f);
    fseek(f, 0L, 0);
    if (n < 0 || n > MAXSRC) {
        fclose(f);
        fprintf(stderr, "%s: source too large\n", name);
        return 0;
    }
    src = (char *)malloc((unsigned int)n + 2);
    if (!src) {
        fclose(f);
        return 0;
    }
    got = (long)fread(src, 1, (unsigned int)n, f);
    fclose(f);
    if (got != n) {
        free(src);
        return 0;
    }
    while (n > 0 && src[n - 1] == 26)
        n--;
    src[n] = '\n';
    src[n + 1] = 0;
    slen = n;
    return 1;
}

static void split_lines(void)
{
    char *p, *e, *q;
    int ln;

    p = src;
    while (*p && *p != 26) {
        while (*p == '\r' || *p == '\n') p++;
        if (!*p || *p == 26) break;
        ln = atoi(p);
        while (isdigit((unsigned char)*p)) p++;
        while (*p == ' ' || *p == '\t') p++;
        e = p; while (*e && *e != 26 && *e != '\r' && *e != '\n') e++;
        q = (char *)malloc((unsigned int)(e - p) + 1); if (!q) die("out of memory");
        memcpy(q, p, (unsigned int)(e - p)); q[e - p] = 0;
        if (nlines >= MAXPROG) die("too many lines");
        lines[nlines].num = ln; lines[nlines].txt = q; lines[nlines].pc = -1; nlines++;
        p = e;
    }
    {
        int i, j;
        struct Line t;
        for (i = 1; i < nlines; i++) {
            t = lines[i];
            j = i - 1;
            while (j >= 0 && lines[j].num > t.num) {
                lines[j + 1] = lines[j];
                j--;
            }
            lines[j + 1] = t;
        }
    }
}

static void compile_prog(void)
{
    int i;
    for (i = 0; i < nlines; i++) { lines[i].pc = cp; compile_line(lines[i].txt); }
    emit(OP_HALT, 0, 0);
    resolve_patches();
}

static void print_stats(void)
{
    int i, arrbytes;

    arrbytes = 0;
    if (!opt_verbose) return;
    for (i = 0; i < nsym; i++) if (sym[i].base >= 0) arrbytes += sym[i].size * INT_BYTES;
    fprintf(stderr, "\nBINT usage summary\n");
    fprintf(stderr, "  Source bytes:          %u / %u\n", (unsigned)slen, (unsigned)MAXSRC);
    fprintf(stderr, "  BASIC lines:           %d / %d\n", nlines, MAXPROG);
    fprintf(stderr, "  P-code instructions:   %d / %d\n", cp, MAXCODE);
    fprintf(stderr, "  Symbols:               %d / %d\n", nsym, MAXSYM);
    fprintf(stderr, "  BASIC strings:         %d / %d\n", nstr, MAXSTR);
    fprintf(stderr, "  VM memory cells:       %d allocated (%d bytes)\n", mtop, mtop * INT_BYTES);
    fprintf(stderr, "  Array bytes:           %d\n", arrbytes);
    fprintf(stderr, "  Eval stack cells:      %d\n", MAXSTACK);
    fprintf(stderr, "  FOR stack frames:      %d\n", MAXFOR);
    fprintf(stderr, "  GOSUB stack frames:    %d\n", MAXGOSUB);
}

int main(int argc, char **argv)
{
    int argi;
    int want_verbose;
    char *fname;

    fname = 0;
    want_verbose = 0;
    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "-V") == 0 || strcmp(argv[argi], "-v") == 0)
            want_verbose = 1;
        else
            fname = argv[argi];
    }
    if (!fname) { fprintf(stderr, "usage: bint [-V] file.bas\n"); return 1; }
    init_compile_storage();
    opt_verbose = want_verbose;
    if (!load_file(fname)) return 1;
    split_lines();
    compile_prog();
    free_compile_only_storage();
    init_run_storage();
    run();
    print_stats();
    return 0;
}
