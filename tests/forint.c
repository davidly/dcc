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
#define MAXNAME 16
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
    int target;
    int fmt;
    int sym;
    char *a;
    char *b;
    char *c;
    int ae;
    int be;
    int ce;
    int act;
    int act_target;
    int act_sym;
    char *act_idx;
    char *act_rhs;
    int act_idx_e;
    int act_rhs_e;
    int ntargets;
    int targets[10];
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
    int pc;
}
;
struct DoEnt
{
    int label;
    int pc_after;
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
struct State
{
    char *src;
    long slen;
    struct Stmt *stmts;
    struct Sym *syms;
    struct Call *calls;
    struct DoEnt *dos;
    struct Expr *exprs;
    struct ETok *etoks;
    unsigned char *mem;
    int ns, nsy, ncall, ndo, mtop, mcap;
    int ne, netok;
    int pc, halted, verbose;
    const char *ep;
    const char *cep;
}
;
static struct State *G;
static void die(const char *s)
{
    fprintf(stderr, "forint:%s near pc=%d '%s'\n", s, G ? G->pc : -1,
    (G && G->pc >= 0 && G->pc < G->ns) ? G->stmts[G->pc].text : "");
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
    for(i=G->nsy-1;i>=0;i--) if(!strcmp(G->syms[i].name,n)) return i;
    return -1;
}

static void grow_mem(int need)
{
    unsigned char *p;
    int ncap;

    if(need <= G->mcap) return;
    if(need > MAXMEM) die("data memory full");
    ncap = G->mcap;
    while(ncap < need) {
        if(ncap < 1024) ncap += 256;
        else ncap += 1024;
    }
    if(ncap > MAXMEM) ncap = MAXMEM;
    p = (unsigned char*)realloc(G->mem, (unsigned int)ncap);
    if(!p) die("oom");
    memset(p + G->mcap, 0, (unsigned int)(ncap - G->mcap));
    G->mem = p;
    G->mcap = ncap;
}
static int add_sym(const char *name,int kind,int type,int count)
{
    int i,bytes;
    if(G->nsy>=MAXSYM) die("too many symbols");
    i=G->nsy++;
    memset(&G->syms[i],0,sizeof(struct Sym));
    upcase(G->syms[i].name,name);
    G->syms[i].kind=kind;
    G->syms[i].type=type;
    G->syms[i].size=count;
    bytes=(count+1)*(type==TYPE_I1?1:CELL);
    grow_mem(G->mtop + bytes);
    G->syms[i].base=G->mtop;
    G->mtop+=bytes;
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
    if(a<0||a+1>=G->mcap)die("bad cell");
    return(short)(G->mem[a]|(G->mem[a+1]<<8));
}
static inline void set_cell(int a,int v)
{
    if(a<0||a+1>=G->mcap)die("bad cell");
    G->mem[a]=(unsigned char)(v&255);
    G->mem[a+1]=(unsigned char)((v>>8)&255);
}
static int get_sym_val(int si,int idx)
{
    struct Sym *s;
    int a,v;
    s=&G->syms[si];
    if(s->kind==K_SCALAR) idx=0;
    if(idx<0||idx>s->size) die("array index");
    if(s->type==TYPE_I1)
    {
        a=s->base+idx;
        v=G->mem[a];
        if(v>=128)v-=256;
        return v;
    }
    return cell_at(s->base+idx*CELL);
}
static void set_sym_val(int si,int idx,int v)
{
    struct Sym *s;
    int a;
    s=&G->syms[si];
    if(s->kind==K_SCALAR) idx=0;
    if(idx<0||idx>s->size) die("array index");
    if(s->type==TYPE_I1)
    {
        G->mem[s->base+idx]=(unsigned char)v;
        return;
    }
    a=s->base+idx*CELL;
    set_cell(a,v);
}
static void eskip(void)
{
    while(*G->ep && isspace((unsigned char)*G->ep))G->ep++;
}
static int expr(void);
static int primary(void)
{
    char name[MAXNAME];
    int i,v,si,idx;
    eskip();
    if(*G->ep=='(')
    {
        G->ep++;
        v=expr();
        eskip();
        if(*G->ep==')')G->ep++;
        return v;
    }
    if(*G->ep=='-'||*G->ep=='+')
    {
        int neg;
        neg=(*G->ep=='-');
        G->ep++;
        v=primary();
        return neg?-v:v;
    }
    if(*G->ep=='.')
    {
        if(!strncmp(G->ep,".TRUE.",6))
        {
            G->ep+=6;
            return 1;
        }
        if(!strncmp(G->ep,".FALSE.",7))
        {
            G->ep+=7;
            return 0;
        }
    }
    if(isdigit((unsigned char)*G->ep))
    {
        v=0;
        while(isdigit((unsigned char)*G->ep))v=v*10+*G->ep++-'0';
        return v;
    }
    if(isalpha((unsigned char)*G->ep))
    {
        i=0;
        while((isalnum((unsigned char)*G->ep) || *G->ep == '_') &&
              i < MAXNAME - 1) {
            name[i++] = (char)toupper((unsigned char)*G->ep++);
        }
        name[i]=0;
        if(!strcmp(name,"MOD"))
        {
            eskip();
            if(*G->ep=='(')G->ep++;
            v=expr();
            eskip();
            if(*G->ep==',')G->ep++;
            idx=expr();
            eskip();
            if(*G->ep==')')G->ep++;
            return idx?v%idx:0;
        }
        si=ensure_sym(name);
        eskip();
        if(*G->ep=='(')
        {
            G->ep++;
            idx=expr();
            eskip();
            if(*G->ep==')')G->ep++;
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
        if(*G->ep=='*')
        {
            G->ep++;
            v*=primary();
        }
        else if(*G->ep=='/')
        {
            G->ep++;
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
        if(*G->ep=='+')
        {
            G->ep++;
            v+=term();
        }
        else if(*G->ep=='-')
        {
            G->ep++;
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
        if(!strncmp(G->ep,".EQ.",4))
        {
            G->ep+=4;
            r=arith();
            v=(v==r);
        }
        else if(!strncmp(G->ep,".NE.",4))
        {
            G->ep+=4;
            r=arith();
            v=(v!=r);
        }
        else if(!strncmp(G->ep,".LT.",4))
        {
            G->ep+=4;
            r=arith();
            v=(v<r);
        }
        else if(!strncmp(G->ep,".LE.",4))
        {
            G->ep+=4;
            r=arith();
            v=(v<=r);
        }
        else if(!strncmp(G->ep,".GT.",4))
        {
            G->ep+=4;
            r=arith();
            v=(v>r);
        }
        else if(!strncmp(G->ep,".GE.",4))
        {
            G->ep+=4;
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
        if(!strncmp(G->ep,".AND.",5))
        {
            G->ep+=5;
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
        if(!strncmp(G->ep,".OR.",4))
        {
            G->ep+=4;
            v=(land()||v);
        }
        else break;
    }
    return v;
}
static int eval_str(const char *s)
{
    G->ep=s;
    return expr();
}

static int eemit(int op,int a)
{
    int i;
    if(G->netok>=MAXETOK)die("expr token full");
    i=G->netok++;
    G->etoks[i].op=op;
    G->etoks[i].a=a;
    return i;
}
static void cskip(void)
{
    while(*G->cep&&isspace((unsigned char)*G->cep))G->cep++;
}
static void cexpr(void);
static void cprimary(void)
{
    char name[MAXNAME];
    int i,v,si;
    cskip();
    if(*G->cep=='(')
    {
        G->cep++;
        cexpr();
        cskip();
        if(*G->cep==')')G->cep++;
        return;
    }
    if(*G->cep=='-'||*G->cep=='+')
    {
        i=(*G->cep=='-');
        G->cep++;
        cprimary();
        if(i)eemit(EO_NEG,0);
        return;
    }
    if(*G->cep=='.')
    {
        if(!strncmp(G->cep,".TRUE.",6))
        {
            G->cep+=6;
            eemit(EO_CONST,1);
            return;
        }
        if(!strncmp(G->cep,".FALSE.",7))
        {
            G->cep+=7;
            eemit(EO_CONST,0);
            return;
        }
    }
    if(isdigit((unsigned char)*G->cep))
    {
        v=0;
        while(isdigit((unsigned char)*G->cep))v=v*10+*G->cep++-'0';
        eemit(EO_CONST,v);
        return;
    }
    if(isalpha((unsigned char)*G->cep))
    {
        i=0;
        while((isalnum((unsigned char)*G->cep)||*G->cep=='_')&&
              i<MAXNAME-1)
        {
            name[i++]=(char)toupper((unsigned char)*G->cep++);
        }
        name[i]=0;
        if(!strcmp(name,"MOD"))
        {
            cskip();
            if(*G->cep=='(')G->cep++;
            cexpr();
            cskip();
            if(*G->cep==',')G->cep++;
            cexpr();
            cskip();
            if(*G->cep==')')G->cep++;
            eemit(EO_MOD,0);
            return;
        }
        si=ensure_sym(name);
        cskip();
        if(*G->cep=='(')
        {
            G->cep++;
            cexpr();
            cskip();
            if(*G->cep==')')G->cep++;
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
        if(*G->cep=='*')
        {
            G->cep++;
            cprimary();
            eemit(EO_MUL,0);
        }
        else if(*G->cep=='/')
        {
            G->cep++;
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
        if(*G->cep=='+')
        {
            G->cep++;
            cprimary();
            cterm();
            eemit(EO_ADD,0);
        }
        else if(*G->cep=='-')
        {
            G->cep++;
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
        if(!strncmp(G->cep,".EQ.",4))
        {
            G->cep+=4; carith(); eemit(EO_EQ,0);
        }
        else if(!strncmp(G->cep,".NE.",4))
        {
            G->cep+=4; carith(); eemit(EO_NE,0);
        }
        else if(!strncmp(G->cep,".LT.",4))
        {
            G->cep+=4; carith(); eemit(EO_LT,0);
        }
        else if(!strncmp(G->cep,".LE.",4))
        {
            G->cep+=4; carith(); eemit(EO_LE,0);
        }
        else if(!strncmp(G->cep,".GT.",4))
        {
            G->cep+=4; carith(); eemit(EO_GT,0);
        }
        else if(!strncmp(G->cep,".GE.",4))
        {
            G->cep+=4; carith(); eemit(EO_GE,0);
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
        if(!strncmp(G->cep,".AND.",5))
        {
            G->cep+=5;
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
        if(!strncmp(G->cep,".OR.",4))
        {
            G->cep+=4;
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
    if(G->ne>=MAXEXPR)die("too many exprs");
    i=G->ne++;
    start=G->netok;
    G->cep=s;
    cexpr();
    G->exprs[i].start=start;
    G->exprs[i].len=G->netok-start;
    return i;
}
static int eval_e(int ei)
{
    int stack[24];
    int sp,i,end,op,a,b;
    struct ETok *t;
    if(ei<0)return 0;
    sp=0;
    i=G->exprs[ei].start;
    end=i+G->exprs[ei].len;
    while(i<end)
    {
        t=&G->etoks[i++];
        op=t->op;
        if(op==EO_CONST)stack[sp++]=t->a;
        else if(op==EO_LOAD)stack[sp++]=get_sym_val(t->a,0);
        else if(op==EO_LOADA)
        {
            a=stack[--sp];
            stack[sp++]=get_sym_val(t->a,a);
        }
        else if(op==EO_NEG)stack[sp-1]=-stack[sp-1];
        else
        {
            b=stack[--sp];
            a=stack[--sp];
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
                default: die("bad expr op");
            }
            stack[sp++]=a;
        }
    }
    return sp?stack[sp-1]:0;
}
static int find_label_in_unit(int lab,int unit)
{
    int i;
    for(i=0;i<G->ns;i++)if(G->stmts[i].label==lab&&G->stmts[i].unit==unit)return i;
    for(i=0;i<G->ns;i++)if(G->stmts[i].label==lab)return i;
    return -1;
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
static int find_sub(const char *name)
{
    char n[MAXNAME],w[MAXNAME];
    int i;
    const char*p;
    upcase(n,name);
    for(i=0;i<G->ns;i++)if(starts(G->stmts[i].text,"SUBROUTINE"))
    {
        p=G->stmts[i].text+10;
        while(*p&&isspace((unsigned char)*p))p++;
        upcase(w,p);
        if(!strcmp(w,n))return i+1;
    }
    return -1;
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
    for(i=0;i<G->ns;i++)
    {
        strcpy(buf,G->stmts[i].text);
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
        st->act_target=parse_goto_label(q);
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
    for(i=0;i<G->ns;i++)
    {
        st=&G->stmts[i];
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
            st->target=atoi(p);
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
    for(i=0;i<G->ns;i++)
    {
        st=&G->stmts[i];
        if(st->op==OP_DO)st->target=find_label_in_unit(st->target,st->unit);
        if(st->op == OP_IF && st->act == ACT_GOTO) {
            st->act_target = find_label_in_unit(st->act_target, st->unit);
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
    if(G->ncall<=0)
    {
        G->halted=1;
        return;
    }
    G->pc=G->calls[--G->ncall].pc;
}
static void exec_stmt(void)
{
    struct Stmt *st;
    struct DoEnt *d;
    int v,idx;
    st=&G->stmts[G->pc];
    switch(st->op)
    {
        case OP_SKIP: G->pc++;
        return;
        case OP_STOP: G->halted=1;
        return;
        case OP_RETURN: do_return();
        return;
        case OP_WRITE: write_pre(st);
        G->pc++;
        return;
        case OP_CALL: if(st->target<0)die("bad call");
        if(G->ncall>=MAXCALL)die("call stack");
        G->calls[G->ncall++].pc=G->pc+1;
        G->pc=st->target;
        return;
        case OP_DO: if(G->ndo>=MAXDO)die("do stack");
        set_sym_val(st->sym,0,eval_e(st->ae));
        d=&G->dos[G->ndo++];
        d->label=st->target;
        d->pc_after=G->pc+1;
        d->sym=st->sym;
        d->endv=eval_e(st->be);
        d->step=eval_e(st->ce);
        G->pc++;
        return;
        case OP_CONTINUE: if(G->ndo>0 && G->dos[G->ndo-1].label==G->pc)
        {
            d=&G->dos[G->ndo-1];
            v=get_sym_val(d->sym,0)+d->step;
            set_sym_val(d->sym,0,v);
            if((d->step>=0&&v<=d->endv)||(d->step<0&&v>=d->endv))
            {
                G->pc=d->pc_after;
                return;
            }
            G->ndo--;
        }
        G->pc++;
        return;
        case OP_IF: if(eval_e(st->ae))
        {
            if(st->act==ACT_GOTO)
            {
                if(st->act_target<0)die("bad label");
                G->pc=st->act_target;
                return;
            }
            if(st->act==ACT_RETURN)
            {
                do_return();
                return;
            }
            if(st->act==ACT_ASSIGN)
            {
                assign_pre(st->act_sym,st->act_idx_e,st->act_rhs_e);
            }
        }
        G->pc++;
        return;
        case OP_GOTO: if(st->target<0)die("bad label");
        G->pc=st->target;
        return;
        case OP_CGOTO: idx=eval_e(st->ae);
        if(idx>=1&&idx<=st->ntargets)
        {
            G->pc=st->targets[idx-1];
            return;
        }
        G->pc++;
        return;
        case OP_ASSIGN: assign_pre(st->sym,st->ae,st->be);
        G->pc++;
        return;
    }
    G->pc++;
}
static void run_prog(void)
{
    G->pc=0;
    while(!G->halted&&G->pc>=0&&G->pc<G->ns)exec_stmt();
}
static void add_stmt(int label,char*s,int unit)
{
    if(G->ns>=MAXSTMT)die("too many statements");
    G->stmts[G->ns].label=label;
    G->stmts[G->ns].text=xstrdup2(s);
    G->stmts[G->ns].unit=unit;
    G->stmts[G->ns].ae=-1;
    G->stmts[G->ns].be=-1;
    G->stmts[G->ns].ce=-1;
    G->stmts[G->ns].act_idx_e=-1;
    G->stmts[G->ns].act_rhs_e=-1;
    G->ns++;
}
static void parse_source(void)
{
    long p;
    char line[MAXLINE],body[MAXLINE];
    int li,c,label,unit;
    p=0;
    unit=0;
    while(p<G->slen)
    {
        li=0;
        while(p<G->slen&&G->src[p]!='\n'&&li<MAXLINE-1)
        {
            c=(unsigned char)G->src[p++];
            if(c==0x1a)
            {
                p=G->slen;
                break;
            }
            if(c!='\r')line[li++]=(char)c;
        }
        while(p<G->slen&&G->src[p]!='\n')p++;
        if(p<G->slen&&G->src[p]=='\n')p++;
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
    G->src=(char*)malloc((unsigned int)n+1);
    if(!G->src)
    {
        fclose(f);
        return 0;
    }
    got=(long)fread(G->src,1,(unsigned int)n,f);
    fclose(f);
    if(got!=n)return 0;
    while(n>0&&(unsigned char)G->src[n-1]==0x1a)n--;
    G->src[n]=0;
    G->slen=n;
    return 1;
}
static void init_state(void)
{
    G=(struct State*)calloc(1,sizeof(struct State));
    if(!G)
    {
        fprintf(stderr,"out of memory\n");
        exit(1);
    }
    G->stmts=(struct Stmt*)xcalloc(MAXSTMT,sizeof(struct Stmt));
    G->syms=(struct Sym*)xcalloc(MAXSYM,sizeof(struct Sym));
    G->calls=(struct Call*)xcalloc(MAXCALL,sizeof(struct Call));
    G->dos=(struct DoEnt*)xcalloc(MAXDO,sizeof(struct DoEnt));
    G->exprs=(struct Expr*)xcalloc(MAXEXPR,sizeof(struct Expr));
    G->etoks=(struct ETok*)xcalloc(MAXETOK,sizeof(struct ETok));
    G->mem=(unsigned char*)xcalloc(INITMEM,1);
    G->mcap=INITMEM;
}
static void print_stats(void)
{
    if(!G->verbose)return;
    fprintf(stderr,"\nFORINT usage summary\n");
    fprintf(stderr,"  Source bytes:    %ld / %ld\n",G->slen,MAXSRC);
    fprintf(stderr,"  Statements:      %d / %d\n",G->ns,MAXSTMT);
    fprintf(stderr,"  Symbols:         %d / %d\n",G->nsy,MAXSYM);
    fprintf(stderr,"  Data bytes:      %d used, %d allocated, %d max\n",
            G->mtop,G->mcap,MAXMEM);
    fprintf(stderr,"  Expressions:     %d / %d\n",G->ne,MAXEXPR);
    fprintf(stderr,"  Expr tokens:     %d / %d\n",G->netok,MAXETOK);
}
int main(int argc,char**argv)
{
    int argi;
    init_state();
    argi=1;
    if(argi<argc&&(!strcmp(argv[argi],"-V")||!strcmp(argv[argi],"-v")))
    {
        G->verbose=1;
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
