/* forint_fast.c - tiny FORTRAN IV subset interpreter for DCC/C89 / CP/M-80.
* Faster predecoded version for sieve.for, e.for, and ttt.for.
* Integer/logical only. Ctrl-Z tolerant input. BSS kept tiny via heap state.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define MAXSRC 7000L
#define MAXLINE 160
#define MAXSTMT 160
#define MAXSYM 64
/* 24, not the more obvious 16: struct Sym's other four fields (kind, type,
 * base, size) add 8 bytes, so name[24] makes sizeof(struct Sym) exactly 32
 * - a power of 2. get_sym_val/set_sym_val compute &g_syms[si] on every
 * variable read/write in the whole interpreted program (25%+8% of total
 * runtime per profiling); with the previous name[16] (sizeof 24, not a
 * power of 2), that address needed a 5-instruction shift/add sequence for
 * si*24, versus 5 plain doublings for si*32 - fewer, cheaper instructions,
 * and no wasted padding field, just a slightly more generous (than
 * standard FORTRAN's own 6-character limit) identifier length. */
#define MAXNAME 24
#define MAXCALL 16
#define MAXDO 16
#define MAXMEM 9000
#define MAXEXPR 160
#define MAXETOK 512
#define INITMEM 128
#define CELL 2
#define TYPE_I1 1
#define TYPE_I2 2
#define K_SCALAR 1
#define K_ARRAY 2
#define OP_SKIP 0
#define OP_STOP 1
#define OP_RETURN 2
#define OP_WRITE 3
#define OP_CALL 4
#define OP_DO 5
#define OP_CONTINUE 6
#define OP_IF 7
#define OP_GOTO 8
#define OP_CGOTO 9
#define OP_ASSIGN 10
#define ACT_NONE 0
#define ACT_GOTO 1
#define ACT_RETURN 2
#define ACT_ASSIGN 3
#define EO_CONST 1
#define EO_LOAD 2
#define EO_LOADA 3
#define EO_ADD 4
#define EO_SUB 5
#define EO_MUL 6
#define EO_DIV 7
#define EO_MOD 8
#define EO_NEG 9
#define EO_EQ 10
#define EO_NE 11
#define EO_LT 12
#define EO_LE 13
#define EO_GT 14
#define EO_GE 15
#define EO_AND 16
#define EO_OR 17
struct Stmt
{
    int label;
    char *text;
    int unit;
    int op;
    struct Stmt *target;      /* resolved GOTO/CALL/DO target (NULL = unresolved/bad) */
    int target_label;         /* OP_DO only: raw label text, staged for decode_stmts' 2nd pass */
    int fmt;
    int sym;
    char *a;
    char *b;
    char *c;
    int ae;
    int be;
    int ce;
    int act;
    struct Stmt *act_target;  /* resolved IF-GOTO target */
    int act_target_label;     /* IF+ACT_GOTO only: raw label text, staged for the 2nd pass */
    int act_sym;
    char *act_idx;
    char *act_rhs;
    int act_idx_e;
    int act_rhs_e;
    int ntargets;
    struct Stmt *targets[10];
}
;
struct Sym
{
    char name[MAXNAME];
    int kind;
    int type;
    int base;
    int size;
}
;
struct Call
{
    struct Stmt *pc;
}
;
struct DoEnt
{
    struct Stmt *label;       /* the loop's terminating CONTINUE statement */
    struct Stmt *pc_after;    /* resume point: the statement right after DO */
    int sym;
    int endv;
    int step;
}
;
struct Expr
{
    int start;
    int len;
}
;
struct ETok
{
    int op;
    int a;
}
;
/* Former struct State's fields, now plain globals: there was only ever one
 * instance (allocated once in init_state, never reassigned), so every
 * reference through it paid a pointer fetch (find where G lives) plus a
 * field-offset add (find where in *G the field is) before even touching the
 * value - two indirections for what's really just a fixed set of variables,
 * each with a compile-time-known address once it's a real global. */
static char *g_src;
static long g_slen;
static struct Stmt *g_stmts;
static struct Sym *g_syms;
static struct Call *g_calls;
static struct DoEnt *g_dos;
static struct Expr *g_exprs;
static struct ETok *g_etoks;
static unsigned char *g_mem;
static int g_ns, g_nsy, g_ncall, g_ndo, g_mtop, g_mcap;
static int g_ne, g_netok;
static struct Stmt *g_pc;
static int g_halted, g_verbose;
static const char *g_ep;
static const char *g_cep;
static _Noreturn void die(const char *s)
{
    fprintf(stderr, "forint:%s near pc=%d '%s'\n", s,
        (g_stmts && g_pc) ? (int)(g_pc - g_stmts) : -1,
        (g_stmts && g_pc >= g_stmts && g_pc < g_stmts + g_ns) ? g_pc->text : "");
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
static void trim(char *s)
{
    int i,j,n;
    i=0;
    while(s[i] && isspace((unsigned char)s[i])) i++;
    if(i)
    {
        j=0;
        while(s[i]) s[j++]=s[i++];
        s[j]=0;
    }
    n=(int)strlen(s);
    while(n>0 && isspace((unsigned char)s[n-1])) s[--n]=0;
}
static int starts(const char *s, const char *p)
{
    while(*p)
    {
        if(toupper((unsigned char)*s)!=*p) return 0;
        s++;
        p++;
    }
    return 1;
}
static void upcase(char *d, const char *s)
{
    int i;
    for(i=0; s[i] && i<MAXNAME-1; i++) d[i]=(char)toupper((unsigned char)s[i]);
    d[i]=0;
}
static int find_sym(const char *name)
{
    char n[MAXNAME];
    int i;
    upcase(n,name);
    for(i=g_nsy-1;i>=0;i--) if(!strcmp(g_syms[i].name,n)) return i;
    return -1;
}

static void grow_mem(int need)
{
    unsigned char *p;
    int ncap;

    if(need <= g_mcap) return;
    if(need > MAXMEM) die("data memory full");
    ncap = g_mcap;
    while(ncap < need) {
        if(ncap < 1024) ncap += 256;
        else ncap += 1024;
    }
    if(ncap > MAXMEM) ncap = MAXMEM;
    p = (unsigned char*)realloc(g_mem, (unsigned int)ncap);
    if(!p) die("oom");
    memset(p + g_mcap, 0, (unsigned int)(ncap - g_mcap));
    g_mem = p;
    g_mcap = ncap;
}
static int add_sym(const char *name,int kind,int type,int count)
{
    int i,bytes;
    if(g_nsy>=MAXSYM) die("too many symbols");
    i=g_nsy++;
    memset(&g_syms[i],0,sizeof(struct Sym));
    upcase(g_syms[i].name,name);
    g_syms[i].kind=kind;
    g_syms[i].type=type;
    g_syms[i].size=count;
    bytes=(count+1)*(type==TYPE_I1?1:CELL);
    grow_mem(g_mtop + bytes);
    g_syms[i].base=g_mtop;
    g_mtop+=bytes;
    return i;
}
static int ensure_sym(const char *name)
{
    int i;
    i=find_sym(name);
    return i>=0?i:add_sym(name,K_SCALAR,TYPE_I2,0);
}
static inline int cell_at(int a)
{
    if(a<0||a+1>=g_mcap)die("bad cell");
    return(short)(g_mem[a]|(g_mem[a+1]<<8));
}
static inline void set_cell(int a,int v)
{
    if(a<0||a+1>=g_mcap)die("bad cell");
    g_mem[a]=(unsigned char)(v&255);
    g_mem[a+1]=(unsigned char)((v>>8)&255);
}
/* si's array-index resolution, factored out so both get/set_sym_val can
 * reach it without a parameter reassignment, which would make it
 * ineligible for dcc's inliner (see dcc_func.c's AST_ASSIGN handling, and
 * commit 00b7ef6's crash fix for that). A scalar symbol always resolves to
 * index 0 regardless of what idx the caller passed.
 *
 * Takes the already-computed &g_syms[si] rather than si itself: get/
 * set_sym_val below capture that address into their own local once (see
 * their comment) specifically so every field read of that one symbol -
 * .kind here included - shares it instead of each re-deriving
 * si*sizeof(struct Sym) from scratch. Since this call is itself inlined
 * into get/set_sym_val's already-substituted body, "s" here resolves to
 * whichever #itmpN slot the caller's own local was materialized into for
 * that call site - ordinary nested inline expansion, nothing this
 * particular parameter needs to know about. */
static inline int resolve_idx(struct Sym *s,int idx)
{
    return (s->kind==K_SCALAR) ? 0 : idx;
}
/* Bounds checking (and the die() call it used) deliberately dropped here,
 * matching every other interpreter in this suite (e.g. pint.c's
 * pushv/popv): the bytecode these functions read is only ever produced by
 * this same program's own compile pass, never untrusted input, so the
 * check was pure overhead on a hot path.
 *
 * struct Sym *s=&g_syms[si] here used to cost these two functions their
 * dcc-inliner eligibility outright (a local declaration was one of two
 * things - the other being a parameter reassignment, see resolve_idx above
 * - that made a static inline body too complex to fold into a caller):
 * profiling showed .type and .base each recomputing g_syms[si]'s address
 * from scratch (si*sizeof(struct Sym), a 5-instruction shift/add sequence)
 * instead of sharing the one this local now caches - the single hottest
 * code region in forint's whole execution, since get/set_sym_val run on
 * nearly every VM instruction. dcc's inliner was extended (one leading,
 * provably-single-assignment local declaration, materialized once per call
 * site the same way a multiply-used parameter already was) specifically to
 * let this file take this shape without losing inlining - see
 * dcc_func.c's try_scan_inline_local_decl for the compiler-side half of
 * this change. */
static inline int get_sym_val(int si,int idx)
{
    struct Sym *s=&g_syms[si];
    return (s->type==TYPE_I1)
        ? (signed char)g_mem[s->base+resolve_idx(s,idx)]
        : cell_at(s->base+resolve_idx(s,idx)*CELL);
}
static inline void set_sym_val(int si,int idx,int v)
{
    struct Sym *s=&g_syms[si];
    if(s->type==TYPE_I1)
        g_mem[s->base+resolve_idx(s,idx)]=(unsigned char)v;
    else
        set_cell(s->base+resolve_idx(s,idx)*CELL,v);
}
static void eskip(void)
{
    while(*g_ep && isspace((unsigned char)*g_ep))g_ep++;
}
static int expr(void);
static int primary(void)
{
    char name[MAXNAME];
    int i,v,si,idx;
    eskip();
    if(*g_ep=='(')
    {
        g_ep++;
        v=expr();
        eskip();
        if(*g_ep==')')g_ep++;
        return v;
    }
    if(*g_ep=='-'||*g_ep=='+')
    {
        int neg;
        neg=(*g_ep=='-');
        g_ep++;
        v=primary();
        return neg?-v:v;
    }
    if(*g_ep=='.')
    {
        if(!strncmp(g_ep,".TRUE.",6))
        {
            g_ep+=6;
            return 1;
        }
        if(!strncmp(g_ep,".FALSE.",7))
        {
            g_ep+=7;
            return 0;
        }
    }
    if(isdigit((unsigned char)*g_ep))
    {
        v=0;
        while(isdigit((unsigned char)*g_ep))v=v*10+*g_ep++-'0';
        return v;
    }
    if(isalpha((unsigned char)*g_ep))
    {
        i=0;
        while((isalnum((unsigned char)*g_ep) || *g_ep == '_') &&
              i < MAXNAME - 1) {
            name[i++] = (char)toupper((unsigned char)*g_ep++);
        }
        name[i]=0;
        if(!strcmp(name,"MOD"))
        {
            eskip();
            if(*g_ep=='(')g_ep++;
            v=expr();
            eskip();
            if(*g_ep==',')g_ep++;
            idx=expr();
            eskip();
            if(*g_ep==')')g_ep++;
            return idx?v%idx:0;
        }
        si=ensure_sym(name);
        eskip();
        if(*g_ep=='(')
        {
            g_ep++;
            idx=expr();
            eskip();
            if(*g_ep==')')g_ep++;
            return get_sym_val(si,idx);
        }
        return get_sym_val(si,0);
    }
    die("bad expr");
    return 0;
}
static int term(void)
{
    int v,r;
    v=primary();
    for(;;)
    {
        eskip();
        if(*g_ep=='*')
        {
            g_ep++;
            v*=primary();
        }
        else if(*g_ep=='/')
        {
            g_ep++;
            r=primary();
            v=r?v/r:0;
        }
        else break;
    }
    return v;
}
static int arith(void)
{
    int v;
    v=term();
    for(;;)
    {
        eskip();
        if(*g_ep=='+')
        {
            g_ep++;
            v+=term();
        }
        else if(*g_ep=='-')
        {
            g_ep++;
            v-=term();
        }
        else break;
    }
    return v;
}
static int rel(void)
{
    int v,r;
    v=arith();
    for(;;)
    {
        eskip();
        if(!strncmp(g_ep,".EQ.",4))
        {
            g_ep+=4;
            r=arith();
            v=(v==r);
        }
        else if(!strncmp(g_ep,".NE.",4))
        {
            g_ep+=4;
            r=arith();
            v=(v!=r);
        }
        else if(!strncmp(g_ep,".LT.",4))
        {
            g_ep+=4;
            r=arith();
            v=(v<r);
        }
        else if(!strncmp(g_ep,".LE.",4))
        {
            g_ep+=4;
            r=arith();
            v=(v<=r);
        }
        else if(!strncmp(g_ep,".GT.",4))
        {
            g_ep+=4;
            r=arith();
            v=(v>r);
        }
        else if(!strncmp(g_ep,".GE.",4))
        {
            g_ep+=4;
            r=arith();
            v=(v>=r);
        }
        else break;
    }
    return v;
}
static int land(void)
{
    int v;
    v=rel();
    for(;;)
    {
        eskip();
        if(!strncmp(g_ep,".AND.",5))
        {
            g_ep+=5;
            v=(rel()&&v);
        }
        else break;
    }
    return v;
}
static int expr(void)
{
    int v;
    v=land();
    for(;;)
    {
        eskip();
        if(!strncmp(g_ep,".OR.",4))
        {
            g_ep+=4;
            v=(land()||v);
        }
        else break;
    }
    return v;
}
static int eval_str(const char *s)
{
    g_ep=s;
    return expr();
}

static int eemit(int op,int a)
{
    int i;
    if(g_netok>=MAXETOK)die("expr token full");
    i=g_netok++;
    g_etoks[i].op=op;
    g_etoks[i].a=a;
    return i;
}
static void cskip(void)
{
    while(*g_cep&&isspace((unsigned char)*g_cep))g_cep++;
}
static void cexpr(void);
static void cprimary(void)
{
    char name[MAXNAME];
    int i,v,si;
    cskip();
    if(*g_cep=='(')
    {
        g_cep++;
        cexpr();
        cskip();
        if(*g_cep==')')g_cep++;
        return;
    }
    if(*g_cep=='-'||*g_cep=='+')
    {
        i=(*g_cep=='-');
        g_cep++;
        cprimary();
        if(i)eemit(EO_NEG,0);
        return;
    }
    if(*g_cep=='.')
    {
        if(!strncmp(g_cep,".TRUE.",6))
        {
            g_cep+=6;
            eemit(EO_CONST,1);
            return;
        }
        if(!strncmp(g_cep,".FALSE.",7))
        {
            g_cep+=7;
            eemit(EO_CONST,0);
            return;
        }
    }
    if(isdigit((unsigned char)*g_cep))
    {
        v=0;
        while(isdigit((unsigned char)*g_cep))v=v*10+*g_cep++-'0';
        eemit(EO_CONST,v);
        return;
    }
    if(isalpha((unsigned char)*g_cep))
    {
        i=0;
        while((isalnum((unsigned char)*g_cep)||*g_cep=='_')&&
              i<MAXNAME-1)
        {
            name[i++]=(char)toupper((unsigned char)*g_cep++);
        }
        name[i]=0;
        if(!strcmp(name,"MOD"))
        {
            cskip();
            if(*g_cep=='(')g_cep++;
            cexpr();
            cskip();
            if(*g_cep==',')g_cep++;
            cexpr();
            cskip();
            if(*g_cep==')')g_cep++;
            eemit(EO_MOD,0);
            return;
        }
        si=ensure_sym(name);
        cskip();
        if(*g_cep=='(')
        {
            g_cep++;
            cexpr();
            cskip();
            if(*g_cep==')')g_cep++;
            eemit(EO_LOADA,si);
        }
        else
        {
            eemit(EO_LOAD,si);
        }
        return;
    }
    die("bad compiled expr");
}
static void cterm(void)
{
    for(;;)
    {
        cskip();
        if(*g_cep=='*')
        {
            g_cep++;
            cprimary();
            eemit(EO_MUL,0);
        }
        else if(*g_cep=='/')
        {
            g_cep++;
            cprimary();
            eemit(EO_DIV,0);
        }
        else return;
    }
}
static void carith(void)
{
    cprimary();
    cterm();
    for(;;)
    {
        cskip();
        if(*g_cep=='+')
        {
            g_cep++;
            cprimary();
            cterm();
            eemit(EO_ADD,0);
        }
        else if(*g_cep=='-')
        {
            g_cep++;
            cprimary();
            cterm();
            eemit(EO_SUB,0);
        }
        else return;
    }
}
static void crel(void)
{
    carith();
    for(;;)
    {
        cskip();
        if(!strncmp(g_cep,".EQ.",4))
        {
            g_cep+=4; carith(); eemit(EO_EQ,0);
        }
        else if(!strncmp(g_cep,".NE.",4))
        {
            g_cep+=4; carith(); eemit(EO_NE,0);
        }
        else if(!strncmp(g_cep,".LT.",4))
        {
            g_cep+=4; carith(); eemit(EO_LT,0);
        }
        else if(!strncmp(g_cep,".LE.",4))
        {
            g_cep+=4; carith(); eemit(EO_LE,0);
        }
        else if(!strncmp(g_cep,".GT.",4))
        {
            g_cep+=4; carith(); eemit(EO_GT,0);
        }
        else if(!strncmp(g_cep,".GE.",4))
        {
            g_cep+=4; carith(); eemit(EO_GE,0);
        }
        else return;
    }
}
static void cand(void)
{
    crel();
    for(;;)
    {
        cskip();
        if(!strncmp(g_cep,".AND.",5))
        {
            g_cep+=5;
            crel();
            eemit(EO_AND,0);
        }
        else return;
    }
}
static void cexpr(void)
{
    cand();
    for(;;)
    {
        cskip();
        if(!strncmp(g_cep,".OR.",4))
        {
            g_cep+=4;
            cand();
            eemit(EO_OR,0);
        }
        else return;
    }
}
static int compile_expr_str(const char *s)
{
    int i,start;
    if(!s)return -1;
    if(g_ne>=MAXEXPR)die("too many exprs");
    i=g_ne++;
    start=g_netok;
    g_cep=s;
    cexpr();
    g_exprs[i].start=start;
    g_exprs[i].len=g_netok-start;
    return i;
}
/* eval_e is a small stack-machine bytecode interpreter (g_etoks[], compiled
 * once by cexpr()/friends) - the same shape as the bytecode dispatch loops
 * in pint/cint/bint/adaint/fint/cobint and forint's own run_prog, all
 * already fixed earlier this session to carry the "current position" as a
 * pointer instead of an array index. This one was missed: `t=&g_etoks[i++];`
 * recomputed i's address from scratch (base + i*4) every single token, and
 * the VM operand stack (stack[]/sp) had the identical "int index,
 * recompute the address every push/pop" shape. Both are now pointers,
 * advanced/pushed/popped directly - no address recomputation on the
 * common path. This is the single hottest function in forint by a wide
 * margin (54% of total runtime per profiling), so this loop's shape
 * matters more here than anywhere else in the interpreter. */
static int eval_e(int ei)
{
    int stack[24];
    int *sp;
    int op,a,b;
    struct ETok *t;
    struct ETok *tend;
    if(ei<0)return 0;
    sp=stack;
    t=&g_etoks[g_exprs[ei].start];
    tend=t+g_exprs[ei].len;
    while(t<tend)
    {
        op=t->op;
        if(op==EO_CONST)*sp++=t->a;
        else if(op==EO_LOAD)*sp++=get_sym_val(t->a,0);
        else if(op==EO_LOADA)
        {
            a=*--sp;
            *sp++=get_sym_val(t->a,a);
        }
        else if(op==EO_NEG)sp[-1]=-sp[-1];
        else
        {
            b=*--sp;
            a=*--sp;
            switch(op)
            {
                case EO_ADD: a=a+b; break;
                case EO_SUB: a=a-b; break;
                case EO_MUL: a=a*b; break;
                case EO_DIV: a=b?a/b:0; break;
                case EO_MOD: a=b?a%b:0; break;
                case EO_EQ: a=(a==b); break;
                case EO_NE: a=(a!=b); break;
                case EO_LT: a=(a<b); break;
                case EO_LE: a=(a<=b); break;
                case EO_GT: a=(a>b); break;
                case EO_GE: a=(a>=b); break;
                case EO_AND: a=(a&&b); break;
                case EO_OR: a=(a||b); break;
            }
            *sp++=a;
        }
        t++;
    }
    return sp>stack?sp[-1]:0;
}
/* Returns the resolved statement, or NULL if lab isn't found in this
 * program (used directly as a runtime "no target" sentinel by every
 * caller, matching the old -1 index's role). */
static struct Stmt *find_label_in_unit(int lab,int unit)
{
    int i;
    for(i=0;i<g_ns;i++)if(g_stmts[i].label==lab&&g_stmts[i].unit==unit)return &g_stmts[i];
    for(i=0;i<g_ns;i++)if(g_stmts[i].label==lab)return &g_stmts[i];
    return NULL;
}
static int parse_goto_label(char *s)
{
    char*p;
    p=s;
    while(*p&&!isdigit((unsigned char)*p))p++;
    return atoi(p);
}
static void parse_lhs(char *lhs,int *symp,char **idxp)
{
    char name[MAXNAME];
    char *p,*q;
    int i;
    p=lhs;
    trim(p);
    i=0;
    while((isalnum((unsigned char)*p) || *p == '_') &&
          i < MAXNAME - 1) {
        name[i++] = (char)toupper((unsigned char)*p++);
    }
    name[i]=0;
    *symp=ensure_sym(name);
    *idxp=0;
    while(*p&&isspace((unsigned char)*p))p++;
    if(*p=='(')
    {
        p++;
        q=strchr(p,')');
        if(q)*q=0;
        *idxp=xstrdup2(p);
    }
}
static void decode_assignment_fields(struct Stmt *st,char *s,int ifpart)
{
    char *eq;
    eq=strchr(s,'=');
    if(!eq) die("assignment expected");
    *eq=0;
    if(ifpart)
    {
        parse_lhs(s,&st->act_sym,&st->act_idx);
        st->act_rhs=xstrdup2(eq+1);
        st->act_idx_e=st->act_idx?compile_expr_str(st->act_idx):-1;
        st->act_rhs_e=compile_expr_str(st->act_rhs);
        st->act=ACT_ASSIGN;
    }
    else
    {
        parse_lhs(s,&st->sym,&st->a);
        st->b=xstrdup2(eq+1);
        st->ae=st->a?compile_expr_str(st->a):-1;
        st->be=compile_expr_str(st->b);
        st->op=OP_ASSIGN;
    }
}
/* Returns the subroutine's first executable statement, or NULL if not
 * found (see find_label_in_unit). */
static struct Stmt *find_sub(const char *name)
{
    char n[MAXNAME],w[MAXNAME];
    int i;
    const char*p;
    upcase(n,name);
    for(i=0;i<g_ns;i++)if(starts(g_stmts[i].text,"SUBROUTINE"))
    {
        p=g_stmts[i].text+10;
        while(*p&&isspace((unsigned char)*p))p++;
        upcase(w,p);
        if(!strcmp(w,n))return &g_stmts[i+1];
    }
    return NULL;
}
static void parse_decl(char *s,int type)
{
    char name[MAXNAME];
    char*p;
    int i,n,kind,count;
    p=s;
    while(*p&&!isspace((unsigned char)*p))p++;
    for(;;)
    {
        while(*p&&(isspace((unsigned char)*p)||*p==','))p++;
        if(!*p)break;
        i=0;
        while((isalnum((unsigned char)*p) || *p == '_') &&
          i < MAXNAME - 1) {
        name[i++] = (char)toupper((unsigned char)*p++);
    }
        name[i]=0;
        if(!name[0])break;
        kind=K_SCALAR;
        count=0;
        while(*p&&isspace((unsigned char)*p))p++;
        if(*p=='(')
        {
            p++;
            while(*p&&isspace((unsigned char)*p))p++;
            n=0;
            while(isdigit((unsigned char)*p))n=n*10+*p++-'0';
            count=n;
            kind=K_ARRAY;
            while(*p&&*p!=')')p++;
            if(*p==')')p++;
        }
        if(find_sym(name)<0)add_sym(name,kind,type,count);
        while(*p&&*p!=',')p++;
    }
}
static void parse_decls(void)
{
    int i;
    char buf[MAXLINE];
    for(i=0;i<g_ns;i++)
    {
        strcpy(buf,g_stmts[i].text);
        trim(buf);
        if(starts(buf,"INTEGER*1"))parse_decl(buf,TYPE_I1);
        else if(starts(buf,"INTEGER*2"))parse_decl(buf,TYPE_I2);
        else if(starts(buf,"LOGICAL"))parse_decl(buf,TYPE_I1);
    }
}
static void decode_action(struct Stmt *st,char *q)
{
    trim(q);
    st->act=ACT_NONE;
    if(starts(q,"GOTO")||starts(q,"GO TO"))
    {
        st->act=ACT_GOTO;
        st->act_target_label=parse_goto_label(q);
        return;
    }
    if(starts(q,"RETURN"))
    {
        st->act=ACT_RETURN;
        return;
    }
    if(strchr(q,'='))
    {
        decode_assignment_fields(st,q,1);
        return;
    }
}
static void decode_stmts(void)
{
    int i,lab,nl;
    char buf[MAXLINE],*s,*p,*q,*r;
    struct Stmt *st;
    for(i=0;i<g_ns;i++)
    {
        st=&g_stmts[i];
        strcpy(buf,st->text);
        trim(buf);
        s=buf;
        st->op=OP_SKIP;
        if(!*s || starts(s,"PROGRAM") || starts(s,"SUBROUTINE") ||
           starts(s,"COMMON") || starts(s,"INTEGER") ||
           starts(s,"LOGICAL") || starts(s,"FORMAT")) {
            continue;
        }
        if(starts(s,"CONTINUE"))
        {
            st->op=OP_CONTINUE;
            continue;
        }
        if(starts(s,"END"))
        {
            st->op=OP_SKIP;
            continue;
        }
        if(starts(s,"STOP"))
        {
            st->op=OP_STOP;
            continue;
        }
        if(starts(s,"RETURN"))
        {
            st->op=OP_RETURN;
            continue;
        }
        if(starts(s,"WRITE"))
        {
            st->op=OP_WRITE;
            p=strchr(s,',');
            if(p)
            {
                p++;
                while(*p&&isspace((unsigned char)*p))p++;
                st->fmt=atoi(p);
                q=strchr(p,')');
                if(q)
                {
                    q++;
                    while(*q&&(isspace((unsigned char)*q)||*q==','))q++;
                    if(*q) {
                        st->a=xstrdup2(q);
                        st->ae=compile_expr_str(st->a);
                    }
                }
            }
            continue;
        }
        if(starts(s,"CALL"))
        {
            st->op=OP_CALL;
            p=s+4;
            while(*p&&isspace((unsigned char)*p))p++;
            st->target=find_sub(p);
            continue;
        }
        if(starts(s,"DO"))
        {
            st->op=OP_DO;
            p=s+2;
            while(*p&&isspace((unsigned char)*p))p++;
            st->target_label=atoi(p);
            while(*p&&!isspace((unsigned char)*p))p++;
            while(*p&&isspace((unsigned char)*p))p++;
            q=strchr(p,'=');
            if(!q)die("bad DO");
            *q=0;
            trim(p);
            st->sym=ensure_sym(p);
            q++;
            p=strchr(q,',');
            if(!p)die("bad DO");
            *p=0;
            st->a=xstrdup2(q);
            q=p+1;
            p=strchr(q,',');
            if(p)
            {
                *p=0;
                st->b=xstrdup2(q);
                st->c=xstrdup2(p+1);
            }
            else
            {
                st->b=xstrdup2(q);
                st->c=xstrdup2("1");
            }
            st->ae=compile_expr_str(st->a);
            st->be=compile_expr_str(st->b);
            st->ce=compile_expr_str(st->c);
            continue;
        }
        if(starts(s,"IF"))
        {
            st->op=OP_IF;
            p=strchr(s,'(');
            q=strrchr(s,')');
            if(!p||!q||q<=p)die("bad IF");
            *q=0;
            st->a=xstrdup2(p+1);
            st->ae=compile_expr_str(st->a);
            q++;
            decode_action(st,q);
            continue;
        }
        if(starts(s,"GOTO")||starts(s,"GO TO"))
        {
            p=strchr(s,'(');
            if(p)
            {
                st->op=OP_CGOTO;
                nl=0;
                p++;
                while(*p&&*p!=')'&&nl<10)
                {
                    while(*p&&!isdigit((unsigned char)*p))p++;
                    if(isdigit((unsigned char)*p))
                    {
                        lab=atoi(p);
                        st->targets[nl++]=find_label_in_unit(lab,st->unit);
                    }
                    while(isdigit((unsigned char)*p))p++;
                }
                st->ntargets=nl;
                q=strchr(s,')');
                if(!q)die("bad computed goto");
                q++;
                while(*q&&(*q==','||isspace((unsigned char)*q)))q++;
                st->a=xstrdup2(q);
                st->ae=compile_expr_str(st->a);
            }
            else
            {
                st->op=OP_GOTO;
                lab=parse_goto_label(s);
                st->target=find_label_in_unit(lab,st->unit);
            }
            continue;
        }
        if(strchr(s,'='))
        {
            decode_assignment_fields(st,s,0);
            continue;
        }
    }
    for(i=0;i<g_ns;i++)
    {
        st=&g_stmts[i];
        if(st->op==OP_DO)st->target=find_label_in_unit(st->target_label,st->unit);
        if(st->op == OP_IF && st->act == ACT_GOTO) {
            st->act_target = find_label_in_unit(st->act_target_label, st->unit);
        }
    }
}
static void assign_pre(int si,int idxe,int rhse)
{
    int idx,val;
    idx=idxe>=0?eval_e(idxe):0;
    val=eval_e(rhse);
    set_sym_val(si,idx,val);
}
static void write_pre(struct Stmt *st)
{
    int v;
    v=st->ae>=0?eval_e(st->ae):0;
    if(st->fmt==2000)printf("%1d",v);
    else if(st->fmt==2001)printf("%2d",v);
    else printf("%6d\n",v);
}
static void do_return(void)
{
    if(g_ncall<=0)
    {
        g_halted=1;
        return;
    }
    g_pc=g_calls[--g_ncall].pc;
}
/* run_prog merges what used to be a separate exec_stmt(), called from a
 * one-line while loop, into the loop body directly: exec_stmt was always
 * too big for dcc's inliner (a switch with locals - dcc only inlines a
 * single expression or a simple if-chain, never anything with
 * declarations), so every statement execution paid a real call/ret plus
 * exec_stmt's own prologue/epilogue for its 4 locals. There was exactly
 * one call site, so merging the bodies is exactly what a real inliner
 * would have done here anyway, with no code-size tradeoff since nothing
 * gets duplicated.
 *
 * g_pc is now a struct Stmt* (was: an int index into g_stmts[]) - struct
 * Stmt is 60+ bytes, not a power of 2, so `&g_stmts[g_pc]` recomputed a
 * genuine multiply-by-sizeof(struct Stmt) on every single statement
 * dispatch. Advancing via `g_pc++` on the common (non-branching) path is
 * a compile-time-constant pointer bump instead - the same fix already
 * applied to pint/cint/bint/adaint/fint's bytecode dispatch loops earlier
 * this session. Every GOTO/CALL/DO/computed-GOTO target, and the DO-loop
 * bookkeeping (Call.pc, DoEnt.label/pc_after) that used to store a
 * statement index, is now resolved to a pointer instead: for GOTO/CALL/
 * computed-GOTO targets that happens once at decode_stmts() parse time,
 * entirely off the runtime path; for the DO-loop fields it happens once
 * per loop entry and is then reused, unconverted, on every iteration
 * (previously a repeated multiply on every single pass through the
 * loop's CONTINUE statement). */
static void run_prog(void)
{
    struct Stmt *st;
    struct Stmt *stmts_end;
    struct DoEnt *d;
    int v,idx;

    g_pc=g_stmts;
    stmts_end=g_stmts+g_ns;
    while(!g_halted && g_pc>=g_stmts && g_pc<stmts_end)
    {
        st=g_pc;
        switch(st->op)
        {
            case OP_SKIP: g_pc++;
            continue;
            case OP_STOP: g_halted=1;
            continue;
            case OP_RETURN: do_return();
            continue;
            case OP_WRITE: write_pre(st);
            g_pc++;
            continue;
            case OP_CALL: if(st->target==NULL)die("bad call");
            if(g_ncall>=MAXCALL)die("call stack");
            g_calls[g_ncall++].pc=g_pc+1;
            g_pc=st->target;
            continue;
            case OP_DO: if(g_ndo>=MAXDO)die("do stack");
            set_sym_val(st->sym,0,eval_e(st->ae));
            d=&g_dos[g_ndo++];
            d->label=st->target;
            d->pc_after=g_pc+1;
            d->sym=st->sym;
            d->endv=eval_e(st->be);
            d->step=eval_e(st->ce);
            g_pc++;
            continue;
            case OP_CONTINUE: if(g_ndo>0 && g_dos[g_ndo-1].label==g_pc)
            {
                d=&g_dos[g_ndo-1];
                v=get_sym_val(d->sym,0)+d->step;
                set_sym_val(d->sym,0,v);
                if((d->step>=0&&v<=d->endv)||(d->step<0&&v>=d->endv))
                {
                    g_pc=d->pc_after;
                    continue;
                }
                g_ndo--;
            }
            g_pc++;
            continue;
            case OP_IF: if(eval_e(st->ae))
            {
                if(st->act==ACT_GOTO)
                {
                    if(st->act_target==NULL)die("bad label");
                    g_pc=st->act_target;
                    continue;
                }
                if(st->act==ACT_RETURN)
                {
                    do_return();
                    continue;
                }
                if(st->act==ACT_ASSIGN)
                {
                    assign_pre(st->act_sym,st->act_idx_e,st->act_rhs_e);
                }
            }
            g_pc++;
            continue;
            case OP_GOTO: if(st->target==NULL)die("bad label");
            g_pc=st->target;
            continue;
            case OP_CGOTO: idx=eval_e(st->ae);
            if(idx>=1&&idx<=st->ntargets)
            {
                if(st->targets[idx-1]==NULL)die("bad label");
                g_pc=st->targets[idx-1];
                continue;
            }
            g_pc++;
            continue;
            case OP_ASSIGN: assign_pre(st->sym,st->ae,st->be);
            g_pc++;
            continue;
        }
        g_pc++;
    }
}
static void add_stmt(int label,char*s,int unit)
{
    if(g_ns>=MAXSTMT)die("too many statements");
    g_stmts[g_ns].label=label;
    g_stmts[g_ns].text=xstrdup2(s);
    g_stmts[g_ns].unit=unit;
    g_stmts[g_ns].ae=-1;
    g_stmts[g_ns].be=-1;
    g_stmts[g_ns].ce=-1;
    g_stmts[g_ns].act_idx_e=-1;
    g_stmts[g_ns].act_rhs_e=-1;
    g_ns++;
}
static void parse_source(void)
{
    long p;
    char line[MAXLINE],body[MAXLINE];
    int li,c,label,unit;
    p=0;
    unit=0;
    while(p<g_slen)
    {
        li=0;
        while(p<g_slen&&g_src[p]!='\n'&&li<MAXLINE-1)
        {
            c=(unsigned char)g_src[p++];
            if(c==0x1a)
            {
                p=g_slen;
                break;
            }
            if(c!='\r')line[li++]=(char)c;
        }
        while(p<g_slen&&g_src[p]!='\n')p++;
        if(p<g_slen&&g_src[p]=='\n')p++;
        line[li]=0;
        if(line[0]=='C'||line[0]=='c'||line[0]=='#')continue;
        trim(line);
        if(!line[0])continue;
        label=0;
        li=0;
        while(isdigit((unsigned char)line[li]))
        {
            label=label*10+line[li]-'0';
            li++;
        }
        while(line[li]&&isspace((unsigned char)line[li]))li++;
        strcpy(body,line+li);
        trim(body);
        if(starts(body,"SUBROUTINE"))unit++;
        add_stmt(label,body,unit);
    }
}
static int load_file(const char*name)
{
    FILE*f;
    long n,got;
    f=fopen(name,"rb");
    if(!f)
    {
        perror(name);
        return 0;
    }
    if(fseek(f,0L,2)!=0)
    {
        fclose(f);
        return 0;
    }
    n=ftell(f);
    if(n<0||n>MAXSRC)
    {
        fclose(f);
        fprintf(stderr,"%s: source too large\n",name);
        return 0;
    }
    if(fseek(f,0L,0)!=0)
    {
        fclose(f);
        return 0;
    }
    g_src=(char*)malloc((unsigned int)n+1);
    if(!g_src)
    {
        fclose(f);
        return 0;
    }
    got=(long)fread(g_src,1,(unsigned int)n,f);
    fclose(f);
    if(got!=n)return 0;
    while(n>0&&(unsigned char)g_src[n-1]==0x1a)n--;
    g_src[n]=0;
    g_slen=n;
    return 1;
}
static void init_state(void)
{
    g_stmts=(struct Stmt*)xcalloc(MAXSTMT,sizeof(struct Stmt));
    g_syms=(struct Sym*)xcalloc(MAXSYM,sizeof(struct Sym));
    g_calls=(struct Call*)xcalloc(MAXCALL,sizeof(struct Call));
    g_dos=(struct DoEnt*)xcalloc(MAXDO,sizeof(struct DoEnt));
    g_exprs=(struct Expr*)xcalloc(MAXEXPR,sizeof(struct Expr));
    g_etoks=(struct ETok*)xcalloc(MAXETOK,sizeof(struct ETok));
    g_mem=(unsigned char*)xcalloc(INITMEM,1);
    g_mcap=INITMEM;
}
static void print_stats(void)
{
    if(!g_verbose)return;
    fprintf(stderr,"\nFORINT usage summary\n");
    fprintf(stderr,"  Source bytes:    %ld / %ld\n",g_slen,MAXSRC);
    fprintf(stderr,"  Statements:      %d / %d\n",g_ns,MAXSTMT);
    fprintf(stderr,"  Symbols:         %d / %d\n",g_nsy,MAXSYM);
    fprintf(stderr,"  Data bytes:      %d used, %d allocated, %d max\n",
            g_mtop,g_mcap,MAXMEM);
    fprintf(stderr,"  Expressions:     %d / %d\n",g_ne,MAXEXPR);
    fprintf(stderr,"  Expr tokens:     %d / %d\n",g_netok,MAXETOK);
}
int main(int argc,char**argv)
{
    int argi;
    init_state();
    argi=1;
    if(argi<argc&&(!strcmp(argv[argi],"-V")||!strcmp(argv[argi],"-v")))
    {
        g_verbose=1;
        argi++;
    }
    if(argi>=argc)
    {
        fprintf(stderr,"usage: forint [-V] file.for\n");
        return 1;
    }
    if(!load_file(argv[argi]))return 1;
    parse_source();
    parse_decls();
    decode_stmts();
    run_prog();
    print_stats();
    return 0;
}
