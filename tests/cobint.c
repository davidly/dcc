/* cobint.c - tiny Microsoft COBOL v4.65 subset interpreter for DCC/C89.
 * Tokenized execution path for sieve.cob, e.cob, and ttt.cob.
 * Integer-only, heap-backed state, Ctrl-Z tolerant input.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAXSRC 18000L
#define MAXLINE 160
#define MAXNAME 32
#define MAXVAR 64
#define MAXPARA 64
#define MAXSTMT 256
#define MAXTOK 64
#define MAXARR 1200
#define MAXTCODE 5000
#define MAXSTR 32

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

struct Var { char name[MAXNAME]; int *v; int len; };
struct Para { char name[MAXNAME]; int first; int last; };
struct Stmt { char *s; int parno; int ts; int te; };
struct State {
    char *src;
    long slen;
    struct Var *var;
    struct Para *para;
    struct Stmt *stmt;
    int *tc;
    char **strs;
    int nv, np, ns, ntc, nstr;
    int stopped, jumped, jtarget;
    int verbose;
    int tp, tend;
};
static struct State *G;

#define var G->var
#define para G->para
#define stmt G->stmt
#define tc G->tc
#define strs G->strs
#define nv G->nv
#define np G->np
#define ns G->ns
#define ntc G->ntc
#define nstr G->nstr
#define stopped G->stopped
#define jumped G->jumped
#define jtarget G->jtarget
#define tp G->tp
#define tend G->tend

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

static int add_var(const char *name, int len)
{
    int i;
    if (nv >= MAXVAR) die("too many variables");
    i = nv++;
    memset(&var[i], 0, sizeof(var[i]));
    upcase(var[i].name, name);
    var[i].len = len;
    var[i].v = (int *)xcalloc(len + 1, sizeof(int));
    return i;
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

static void need(int k)
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
    int i, c, code, vi, pi;
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
            code = keyword_code(word);
            if (code == TK_NUM) { emit_tok(TK_NUM); emit_tok(0); }
            else if (code) emit_tok(code);
            else if ((vi = find_var_soft(word)) >= 0) { emit_tok(TK_VAR); emit_tok(vi); }
            else if ((pi = find_para_soft(word)) >= 0) { emit_tok(TK_PARA); emit_tok(pi); }
            else { fprintf(stderr, "unknown word %s\n", word); exit(1); }
        } else {
            emit_tok(c);
        }
    }
    stmt[si].te = ntc;
}

static int expr(void);

static int var_ref(int *idxp)
{
    int vi, ix;
    if (tget() != TK_VAR) die("variable expected");
    vi = tget();
    ix = 0;
    if (acc('(')) {
        ix = expr();
        need(')');
    }
    if (ix < 0 || ix > var[vi].len + 1) die("bad subscript");
    if (idxp) *idxp = ix;
    return vi;
}

static int primary(void)
{
    int v, vi, ix;
    if (tpeek() == TK_NUM) { tp++; v = tget(); return v; }
    if (acc('(')) { v = expr(); if (tpeek() == ')') tp++; return v; }
    if (acc(KW_MOD)) {
        int a, b;
        if (tpeek() == '(') tp++;
        a = expr();
        if (tpeek() == ',') tp++;
        b = expr();
        if (tpeek() == ')') tp++;
        return b ? a % b : 0;
    }
    if (tpeek() == TK_VAR) {
        vi = var_ref(&ix);
        return var[vi].v[ix];
    }
    die("primary");
    return 0;
}

static int factor(void)
{
    if (acc('-')) return -factor();
    return primary();
}

static int term(void)
{
    int v, r, op;
    v = factor();
    while (tpeek() == '*' || tpeek() == '/') {
        op = tget();
        r = factor();
        if (op == '*') v *= r;
        else v = r ? v / r : 0;
    }
    return v;
}

static int expr(void)
{
    int v, r, op;
    v = term();
    while (tpeek() == '+' || tpeek() == '-') {
        op = tget();
        r = term();
        if (op == '+') v += r;
        else v -= r;
    }
    return v;
}

static int rel_one(void)
{
    int a, b, notf, op;
    if (tpeek() == '(') tp++;
    a = expr();
    notf = 0;
    if (acc(KW_NOT)) notf = 1;
    if (tpeek() == '=' || tpeek() == '<' || tpeek() == '>') op = tget();
    else die("relop");
    b = expr();
    if (tpeek() == ')') tp++;
    if (op == '=') a = (a == b);
    else if (op == '<') a = (a < b);
    else a = (a > b);
    if (notf) a = !a;
    return a;
}

static int condition(void)
{
    int v, isand, r;
    v = rel_one();
    while (tpeek() == KW_AND || tpeek() == KW_OR) {
        isand = (tpeek() == KW_AND);
        tp++;
        r = rel_one();
        v = isand ? (v && r) : (v || r);
    }
    return v;
}

static void skip_stmt(void)
{
    int depth, k;
    depth = 0;
    while (tp < tend) {
        k = tpeek();
        if (k == KW_IF) depth++;
        if (k == KW_ELSE && depth == 0) return;
        if (k == KW_ELSE && depth > 0) depth--;
        tp++;
        if (k == TK_NUM || k == TK_STR || k == TK_VAR || k == TK_PARA) tp++;
    }
}

static void exec_range(int start, int end);
static void exec_stmt_tokens(void);

static void exec_if(void)
{
    int ok;
    tp++;
    ok = condition();
    if (ok) {
        exec_stmt_tokens();
        if (tpeek() == KW_ELSE) { tp++; skip_stmt(); }
    } else {
        skip_stmt();
        if (tpeek() == KW_ELSE) { tp++; exec_stmt_tokens(); }
    }
}

static void do_move(void)
{
    int v, vi, ix;
    tp++;
    v = expr();
    need(KW_TO);
    vi = var_ref(&ix);
    var[vi].v[ix] = v;
}

static void do_compute(void)
{
    int vi, ix, v;
    tp++;
    vi = var_ref(&ix);
    need('=');
    v = expr();
    var[vi].v[ix] = v;
}

static void do_add(void)
{
    int v, vi, ix;
    tp++;
    v = expr();
    need(KW_TO);
    vi = var_ref(&ix);
    var[vi].v[ix] += v;
}

static void do_subtract(void)
{
    int v, vi, ix;
    tp++;
    v = expr();
    need(KW_FROM);
    vi = var_ref(&ix);
    var[vi].v[ix] -= v;
}

static void do_multiply(void)
{
    int a, b, vi, ix;
    tp++; a = expr(); need(KW_BY); b = expr(); need(KW_GIVING);
    vi = var_ref(&ix); var[vi].v[ix] = a * b;
}

static void do_divide(void)
{
    int a, b, vi, ix;
    tp++; a = expr(); need(KW_BY); b = expr(); need(KW_GIVING);
    vi = var_ref(&ix); var[vi].v[ix] = b ? a / b : 0;
}

static void do_display(void)
{
    int first, vi, ix, k;
    tp++;
    first = 1;
    while (tp < tend) {
        if (!first) printf(" ");
        first = 0;
        k = tpeek();
        if (k == TK_STR) { tp++; printf("%s", strs[tget()]); }
        else if (k == TK_VAR) { vi = var_ref(&ix); printf("%d", var[vi].v[ix]); }
        else { printf("%d", expr()); }
    }
    printf("\n");
}

static int has_var_name(const char *name)
{
    return find_var_soft(name) >= 0;
}

static int getv0(const char *name)
{
    return var[find_var(name)].v[0];
}

static void setv0(const char *name, int v)
{
    var[find_var(name)].v[0] = v;
}

static int geta1(const char *name, int ix)
{
    return var[find_var(name)].v[ix];
}

static void seta1(const char *name, int ix, int v)
{
    var[find_var(name)].v[ix] = v;
}

static void do_perform(void)
{
    int p, q, vname, fromv, byv, times, limit;
    tp++;
    if (tpeek() != TK_PARA) die("perform name");
    tp++; p = tget();
    q = p; times = 1;
    if (acc(KW_THRU)) { if (tpeek() != TK_PARA) die("thru name"); tp++; q = tget(); }
    if (tpeek() == TK_NUM) { tp++; times = tget(); need(KW_TIMES); }
    if (acc(KW_VARYING)) {
        int condpos, afterpos, have_after, saveend;
        if (tpeek() != TK_VAR) die("vary var");
        tp++; vname = tget(); need(KW_FROM); fromv = expr();
        need(KW_BY); byv = expr(); need(KW_UNTIL);
        var[vname].v[0] = fromv;
        condpos = tp;
        afterpos = tp;
        saveend = tend;
        have_after = 0;
        while (!stopped) {
            int done;
            tp = condpos;
            tend = saveend;
            done = condition();
            if (!have_after) { afterpos = tp; have_after = 1; }
            if (done) break;
            exec_range(para[p].first, para[q].last);
            jumped = 0;
            var[vname].v[0] += byv;
        }
        tp = afterpos;
        tend = saveend;
    } else {
        int contpos, saveend;
        contpos = tp;
        saveend = tend;
        for (limit = 0; limit < times && !stopped; limit++) {
            exec_range(para[p].first, para[q].last);
            jumped = 0;
            tp = contpos;
            tend = saveend;
        }
        tp = contpos;
        tend = saveend;
    }
}

static void do_goto(void)
{
    tp++;
    if (tpeek() == KW_TO) tp++;
    if (tpeek() != TK_PARA) die("goto name");
    tp++; jtarget = stmt_for_para_i(tget());
    jumped = 1;
}

static void exec_stmt_tokens(void)
{
    int k;
    while (tp < tend && !stopped && !jumped) {
        k = tpeek();
        switch (k) {
        case KW_ELSE: return;
        case KW_MOVE: do_move(); break;
        case KW_COMPUTE: do_compute(); break;
        case KW_ADD: do_add(); break;
        case KW_SUBTRACT: do_subtract(); break;
        case KW_MULTIPLY: do_multiply(); break;
        case KW_DIVIDE: do_divide(); break;
        case KW_DISPLAY: do_display(); break;
        case KW_PERFORM: do_perform(); break;
        case KW_GO: case KW_GOTO: do_goto(); break;
        case KW_IF: exec_if(); break;
        case KW_STOP: stopped = 1; return;
        case KW_EXIT: tp++; return;
        default: tp++; break;
        }
    }
}

static void exec_range(int start, int end)
{
    int pc;
    pc = start;
    while (pc <= end && !stopped) {
        jumped = 0;

        tp = stmt[pc].ts;
        tend = stmt[pc].te;
        exec_stmt_tokens();

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
    stmt[ns].ts = stmt[ns].te = 0;
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
    add_var(name, len);
    p = strstr(line, "VALUE");
    if (p) { val = atoi(p + 5); var[nv - 1].v[0] = val; }
}

static int load_file(const char *name)
{
    FILE *f;
    long n, got;
    f = fopen(name, "rb");
    if (!f) { perror(name); return 0; }
    fseek(f, 0L, 2); n = ftell(f); fseek(f, 0L, 0);
    if (n < 0 || n > MAXSRC) { fclose(f); fprintf(stderr, "source too large\n"); return 0; }
    G->src = (char *)malloc((unsigned int)n + 1);
    if (!G->src) { fclose(f); return 0; }
    got = (long)fread(G->src, 1, (unsigned int)n, f); fclose(f);
    if (got != n) return 0;
    while (n > 0 && (unsigned char)G->src[n - 1] == 0x1a) n--;
    G->src[n] = 0; G->slen = n; return 1;
}

static void parse_source(void)
{
    char line[MAXLINE], work[MAXLINE], label[MAXNAME], sent[800];
    char *p;
    int indata, inproc, curp, i, j;
    indata = 0; inproc = 0; curp = -1; sent[0] = 0;
    p = G->src;
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
    for (i = 0; i < ns; i++) tokenize_stmt(i);
}

static void print_stats(void)
{
    int i, bytes;
    if (!G->verbose) return;
    bytes = 0;
    for (i = 0; i < nv; i++) bytes += (var[i].len + 1) * (int)sizeof(int);
    fprintf(stderr, "\nCOBINT usage summary\n");
    fprintf(stderr, "  Variables:  %d / %d\n", nv, MAXVAR);
    fprintf(stderr, "  Paragraphs: %d / %d\n", np, MAXPARA);
    fprintf(stderr, "  Statements: %d / %d\n", ns, MAXSTMT);
    fprintf(stderr, "  Tokens:     %d / %d\n", ntc, MAXTCODE);
    fprintf(stderr, "  Strings:    %d / %d\n", nstr, MAXSTR);
    fprintf(stderr, "  Data bytes: %d\n", bytes);
}

int main(int argc, char **argv)
{
    int argi, mainp;
    G = (struct State *)xcalloc(1, sizeof(struct State));
    var = (struct Var *)xcalloc(MAXVAR, sizeof(struct Var));
    para = (struct Para *)xcalloc(MAXPARA, sizeof(struct Para));
    stmt = (struct Stmt *)xcalloc(MAXSTMT, sizeof(struct Stmt));
    tc = (int *)xcalloc(MAXTCODE, sizeof(int));
    strs = (char **)xcalloc(MAXSTR, sizeof(char *));
    argi = 1;
    if (argi < argc && (!strcmp(argv[argi], "-V") || !strcmp(argv[argi], "-v"))) {
        G->verbose = 1; argi++;
    }
    if (argi >= argc) { fprintf(stderr, "usage: cobint [-V] file.cob\n"); return 1; }
    if (!load_file(argv[argi])) return 1;
    parse_source();
    decode_stmts();
    mainp = find_para("MAIN");
    exec_range(para[mainp].first, ns - 1);
    print_stats();
    return 0;
}
