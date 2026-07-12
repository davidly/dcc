/* RELFIX29: emit M80 location-counter records for trailing DS/ORG gaps too. */
/* RELFIX17: add indexed BIT/RES/SET b,(IX/IY+d); prior fixes through RELFIX16. */
/*
* m80clone.c - portable C89, deliberately conservative clone of Microsoft M80
*
* This is a clean-room implementation scaffold aimed at building large CP/M
* Z80/8080 assembly files on modern hosts and emitting LINK-80 compatible REL.
*
* RELFIX5 drop 2026-06-28: fixes LD A,(addr)/LD (addr),A absolute memory forms so they are not misassembled as immediate
* loads; also treats . as the current location counter in expressions.
* Status in this drop:
*   - two-pass assembler, unbounded host memory instead of CP/M 64K tables
*   - M80-style command line: obj,prn=source /Z /I /L /R /H /O /X /M
*   - labels, public labels via FOO::, PUBLIC/ENTRY, EXTRN/EXT, NAME/TITLE
*   - EQU, SET, ORG, ASEG/CSEG/DSEG, DB/DEFB, DW/DEFW, DS/DEFS, END
*   - .RADIX, IF/ELSE/ENDIF/IFE/IFNDEF/IFDEF
*   - 8080 opcodes plus a useful Z80 core: IX/IY indexed loads, CB/ED prefix
*     ops, JR/DJNZ, EXX, LDI/R/D, CPI/R/D, IN/OUT forms, IM, RST, relative tests
*   - PRN listing and SYM symbol listing
*   - REL bitstream writer with module name, public definitions, program size,
*     data bytes, program/data relative words, end-module and end-file items
*
* Not yet complete M80: macro bodies (MACRO/REPT/IRP/IRPC/LOCAL/EXITM), .COMMENT,
* cross-reference .CRF, all listing controls, and some obscure expression forms.
* The structure is intentionally table-free and easy to extend.
*
* Build:
*   cc -std=c89 -Wall -Wextra -O2 -o m80clone m80clone.c
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#define MAXLINE 2048
#define MAXNAME 128
#define MAXARGS 512 /* RELFIX19: long DB/DW argument lists from compiler output */
#define SYMHASH 4099
#define MAXCOND 256
#define T_ABS 0
#define T_CODE 1
#define T_DATA 2
#define T_COMM 3
#define SEG_ABS 0
#define SEG_CODE 1
#define SEG_DATA 2
#define ERRMSG_MAX 256
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned long U32;
typedef struct Expr Expr;
struct Expr {
    long v;
    int type;
    char ext[MAXNAME];
    int undef;
}
;
typedef struct Sym Sym;
struct Sym {
    char *name;
    long value;
    int type;
    int defined;
    int is_public;
    int is_extern;
    int is_set;
    int refs;
    int chain_valid;
    int chain_type;
    long chain_head;
    Sym *next;
}
;
typedef struct Fixup Fixup;
struct Fixup {
    int seg;
    long off;
    int type;
    long val;
    Fixup *next;
}
;
typedef struct DebugLine DebugLine;
struct DebugLine {
    char *file;
    long line;
    long off;
    int seg;
    DebugLine *next;
}
;
typedef struct DebugInfo DebugInfo;
struct DebugInfo {
    int kind;
    char *args;
    long off;
    int seg;
    DebugInfo *next;
}
;
typedef struct ByteVec ByteVec;
struct ByteVec {
    U8 *p;
    U8 *used;
    long n, cap;
}
;
typedef struct Asm Asm;
struct Asm {
    char src[MAXNAME], rel[MAXNAME], prn[MAXNAME], symfile[MAXNAME], dbgfile[MAXNAME], linkfile[MAXNAME];
    int want_rel, want_prn, want_sym, z80, list_hex, init_ds;
    int pass, errors, warnings;
    FILE *fp, *lst;
    long lineno;
    long lc_code, lc_data, lc_abs;
    long end_value;
    int end_type;
    int have_end;
    int seg;
    Sym *syms[SYMHASH];
    Fixup *fixups;
    DebugLine *debug_lines;
    DebugLine *debug_tail;
    DebugInfo *debug_info;
    DebugInfo *debug_info_tail;
    Fixup **fixup_idx_code;
    long fixup_idx_code_cap;
    Fixup **fixup_idx_data;
    long fixup_idx_data_cap;
    ByteVec code, data;
    char modname[MAXNAME];
    int cond_stack[MAXCOND];
    int cond_sp;
    int skipping;
    int radix;
    char last_err[ERRMSG_MAX];
}
;
static char *xstrdup(const char *s) {
    char *p = (char*)malloc(strlen(s)+1);
    if(!p) {
        fprintf(stderr,"out of memory\n");
        exit(2);
    }
    strcpy(p,s);
    return p;
}
static void *xrealloc(void *p, long n) {
    void *q = realloc(p, (size_t)n);
    if(!q) {
        fprintf(stderr,"out of memory\n");
        exit(2);
    }
    return q;
}
static int streqi(const char *a, const char *b) {
    while(*a && *b) {
        if(toupper((unsigned char)*a)!=toupper((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a==0 && *b==0;
}
static void upcase(char *s) {
    int q=0;
    char qc=0;
    while(*s) {
        if(q) {
            if(*s==qc) q=0;
        } else if(*s=='\'' || *s=='\"') {
            q=1;
            qc=*s;
        } else *s=(char)toupper((unsigned char)*s);
        s++;
    }
}
static char *trim(char *s) {
    char *e;
    while(*s && isspace((unsigned char)*s)) s++;
    e=s+strlen(s);
    while(e>s && isspace((unsigned char)e[-1])) *--e=0;
    return s;
}
static int isid0(int c) {
    return isalpha(c)||c=='_'||c=='?'||c=='@'||c=='$'||c=='.';
}
static int isid(int c) {
    return isalnum(c)||c=='_'||c=='?'||c=='@'||c=='$'||c=='.';
}
static unsigned hashname(const char *s) {
    unsigned h=0;
    int i=0;
    while(*s && i<6) {
        h=(h*131u)+(unsigned char)toupper((unsigned char)*s++);
        i++;
    }
    return h%SYMHASH;
}
static void m80sig(char *dst,const char *src) {
    int i=0;
    while(*src && i<6) {
        dst[i++]=(char)toupper((unsigned char)*src++);
    }
    dst[i]=0;
}
static void symkey(char *dst,const char *src) {
    int i=0;
    while(*src && i<MAXNAME-1) {
        dst[i++]=(char)toupper((unsigned char)*src++);
    }
    dst[i]=0;
}
static Sym *sym_find(Asm *a,const char *name,int create) {
    char key[MAXNAME];
    unsigned h;
    Sym *s;
    symkey(key,name);
    h=hashname(key);
    for(s=a->syms[h];s;s=s->next) if(strcmp(s->name,key)==0) return s;
    if(!create) return NULL;
    s=(Sym*)calloc(1,sizeof(Sym));
    if(!s) {
        fprintf(stderr,"oom\n");
        exit(2);
    }
    s->name=xstrdup(key);
    s->type=T_ABS;
    s->next=a->syms[h];
    a->syms[h]=s;
    return s;
}
static long *cur_lc_ptr(Asm *a) {
    if(a->seg==SEG_DATA) return &a->lc_data;
    if(a->seg==SEG_CODE) return &a->lc_code;
    return &a->lc_abs;
}
static int cur_type(Asm *a) {
    if(a->seg==SEG_DATA) return T_DATA;
    if(a->seg==SEG_CODE) return T_CODE;
    return T_ABS;
}
static void vec_need(ByteVec *v,long at,long n) {
    long need=at+n, oldcap;
    if(need<=v->cap) return;
    oldcap=v->cap;
    if(v->cap<1024) v->cap=1024;
    while(v->cap<need) v->cap*=2;
    v->p=(U8*)xrealloc(v->p,v->cap);
    v->used=(U8*)xrealloc(v->used,v->cap);
    memset(v->p+oldcap,0,(size_t)(v->cap-oldcap));
    memset(v->used+oldcap,0,(size_t)(v->cap-oldcap));
}
static void vec_put(ByteVec *v,long at,U8 b) {
    vec_need(v,at,1);
    v->p[at]=b;
    v->used[at]=1;
    if(at>=v->n) v->n=at+1;
}
static void vec_reserve(ByteVec *v,long at,long n) {
    if(n<=0) return;
    vec_need(v,at,n);
    if(at+n>v->n) v->n=at+n;
}
static void seterr(Asm *a,const char *m) {
    if(a->pass==2) {
        a->errors++;
        strncpy(a->last_err,m,ERRMSG_MAX-1);
        a->last_err[ERRMSG_MAX-1]=0;
    }
}
static const char *ps;
static void skipsp(void) {
    while(*ps && isspace((unsigned char)*ps)) ps++;
}
static int parse_expr(Asm *a, Expr *out);
static int local_strncasecmp(const char *a,const char *b,size_t n) {
    size_t i;
    for(i=0;i<n;i++) {
        int ca=toupper((unsigned char)a[i]), cb=toupper((unsigned char)b[i]);
        if(ca!=cb||ca==0||cb==0) return ca-cb;
    }
    return 0;
}
static int match_word(const char *w) {
    const char *p=ps;
    int n=(int)strlen(w);
    skipsp();
    if(local_strncasecmp(ps,w,(size_t)n)==0 && !isid((unsigned char)ps[n])) {
        ps+=n;
        return 1;
    }
    ps=p;
    return 0;
}
static int parse_primary(Asm *a, Expr *e) {
    char buf[MAXNAME];
    int i=0;
    long v=0;
    int base;
    skipsp();
    e->v=0;
    e->type=T_ABS;
    e->undef=0;
    e->ext[0]=0;
    if(*ps=='(') {
        ps++;
        if(!parse_expr(a,e)) return 0;
        skipsp();
        if(*ps==')') ps++;
        else seterr(a,"missing )");
        return 1;
    }
    if(*ps=='$' || (*ps=='.' && !isid((unsigned char)ps[1]))) {
        e->v=*cur_lc_ptr(a);
        e->type=cur_type(a);
        ps++;
        return 1;
    }
    if(*ps=='\'' || *ps=='\"') {
        int q=*ps++;
        if(*ps) {
            v=(unsigned char)*ps++;
            if(*ps && *ps!=q) v=(v<<8)|(unsigned char)*ps++;
        } while(*ps && *ps!=q) ps++;
        if(*ps==q) ps++;
        e->v=v;
        return 1;
    }
    if(isdigit((unsigned char)*ps)) {
        const char *start=ps;
        while(isalnum((unsigned char)*ps)) ps++;
        i=(int)(ps-start);
        if(i>=MAXNAME) i=MAXNAME-1;
        memcpy(buf,start,(size_t)i);
        buf[i]=0;
        base=a->radix;
        i=(int)strlen(buf);
        if(i>0) {
            char c=(char)toupper((unsigned char)buf[i-1]);
            if(c=='H') {
                base=16;
                buf[i-1]=0;
            } else if(c=='B') {
                base=2;
                buf[i-1]=0;
            } else if(c=='O' || c=='Q') {
                base=8;
                buf[i-1]=0;
            } else if(c=='D') {
                base=10;
                buf[i-1]=0;
            }
        }
        v=strtol(buf,NULL,base);
        e->v=v;
        return 1;
    }
    if(isid0((unsigned char)*ps)) {
        Sym *s;
        while(isid((unsigned char)*ps) && i<MAXNAME-1) buf[i++]=(char)toupper((unsigned char)*ps++);
        while(isid((unsigned char)*ps)) ps++;
        buf[i]=0;
        if(streqi(buf,"LOW")) {
            Expr t;
            parse_primary(a,&t);
            e->v=t.v&255;
            return 1;
        }
        if(streqi(buf,"HIGH")) {
            Expr t;
            parse_primary(a,&t);
            e->v=(t.v>>8)&255;
            return 1;
        }
        s=sym_find(a,buf,1);
        s->refs++;
        if(s->defined) {
            e->v=s->value;
            e->type=s->type;
        } else if(s->is_extern) {
            e->v=0;
            e->type=T_ABS;
            e->undef=1;
            strcpy(e->ext,s->name);
        } else {
            e->v=0;
            e->type=T_ABS;
            if(a->pass==2) e->undef=1;
        }
        return 1;
    }
    return 0;
}
static int parse_unary(Asm *a, Expr *e) {
    skipsp();
    if(*ps=='+') {
        ps++;
        return parse_unary(a,e);
    }
    if(*ps=='-') {
        ps++;
        if(parse_unary(a,e)) {
            e->v=-e->v;
            e->type=T_ABS;
            return 1;
        }
        return 0;
    }
    if(*ps=='~') {
        ps++;
        if(parse_unary(a,e)) {
            e->v=(~e->v)&0xffff;
            e->type=T_ABS;
            return 1;
        }
        return 0;
    }
    if(match_word("NOT")) {
        if(parse_unary(a,e)) {
            e->v=!e->v;
            e->type=T_ABS;
            return 1;
        }
        return 0;
    }
    return parse_primary(a,e);
}
static void combine(Expr *l,Expr *r,char op) {
    switch(op) {
        case '*': l->v*=r->v;
        l->type=T_ABS;
        break;
        case '/': if(r->v) l->v/=r->v;
        l->type=T_ABS;
        break;
        case '&': l->v&=r->v;
        l->type=T_ABS;
        break;
        case '+': l->v+=r->v;
        if(l->type==T_ABS) l->type=r->type;
        break;
        case '-': l->v-=r->v;
        if(r->type!=T_ABS) l->type=T_ABS;
        break;
        case '|': l->v|=r->v;
        l->type=T_ABS;
        break;
        case '^': l->v^=r->v;
        l->type=T_ABS;
        break;
    }
}
static int parse_mul(Asm *a,Expr *e) {
    Expr r;
    char op;
    if(!parse_unary(a,e)) return 0;
    for(;;) {
        skipsp();
        op=*ps;
        if(op!='*'&&op!='/'&&op!='&') break;
        ps++;
        if(!parse_unary(a,&r)) break;
        combine(e,&r,op);
    }
    return 1;
}
static int parse_expr(Asm *a, Expr *out) {
    Expr r;
    char op;
    if(!parse_mul(a,out)) return 0;
    for(;;) {
        skipsp();
        op=*ps;
        if(op!='+'&&op!='-'&&op!='|'&&op!='^') break;
        ps++;
        if(!parse_mul(a,&r)) break;
        combine(out,&r,op);
    }
    return 1;
}
static Expr eval(Asm *a,const char *s) {
    Expr e;
    ps=s;
    if(!parse_expr(a,&e)) {
        e.v=0;
        e.type=T_ABS;
        e.undef=1;
    }
    return e;
}
static void emitb(Asm *a,int b) {
    long *lc=cur_lc_ptr(a);
    if(a->pass==2) {
        if(a->seg==SEG_DATA) vec_put(&a->data,*lc,(U8)b);
        else vec_put(&a->code,*lc,(U8)b);
    }
    (*lc)++;
}
static void add_fixup(Asm *a,int seg,long off,int type,long val) {
    Fixup *f;
    Fixup ***idx;
    long *cap;
    if(a->pass!=2) return;
    f=(Fixup*)calloc(1,sizeof(Fixup));
    if(!f) {
        fprintf(stderr,"oom\n");
        exit(2);
    }
    f->seg=seg;
    f->off=off;
    f->type=type;
    f->val=val;
    f->next=a->fixups;
    a->fixups=f;
    /* Direct (seg,off)->Fixup* index alongside the list, so find_fixup can
     * do an O(1) lookup instead of a full list scan per queried offset
     * (that scan, run once per byte of the segment in rel_segment_bytes,
     * used to dominate m80c's runtime on large files). */
    idx = (seg==SEG_DATA) ? &a->fixup_idx_data : &a->fixup_idx_code;
    cap = (seg==SEG_DATA) ? &a->fixup_idx_data_cap : &a->fixup_idx_code_cap;
    if(off>=*cap) {
        long newcap = *cap ? *cap : 1024;
        while(newcap<=off) newcap*=2;
        *idx = (Fixup**)xrealloc(*idx, newcap*(long)sizeof(Fixup*));
        memset(*idx+*cap, 0, (size_t)(newcap-*cap)*sizeof(Fixup*));
        *cap = newcap;
    }
    (*idx)[off]=f;
}
static int cur_seg_id(Asm *a) {
    return a->seg==SEG_DATA ? SEG_DATA : SEG_CODE;
}
static void emitw(Asm *a,Expr e) {
    long off;
    int seg,type;
    Sym *s;
    off=*cur_lc_ptr(a);
    seg=cur_seg_id(a);
    if(a->pass==2 && e.undef && e.ext[0]) {
        s=sym_find(a,e.ext,1);
        emitb(a,(int)(s->chain_valid ? (s->chain_head&255) : 0));
        emitb(a,(int)(s->chain_valid ? ((s->chain_head>>8)&255) : 0));
        if(s->chain_valid) add_fixup(a,seg,off,s->chain_type,s->chain_head);
        s->chain_valid=1;
        s->chain_type=seg==SEG_DATA ? T_DATA : T_CODE;
        s->chain_head=off;
        if(e.v) add_fixup(a,seg,off,T_ABS,e.v);
        /* external plus offset */ return;
    }
    type=e.type;
    emitb(a,(int)(e.v&255));
    emitb(a,(int)((e.v>>8)&255));
    if(a->pass==2 && (type==T_CODE || type==T_DATA || type==T_COMM)) add_fixup(a,seg,off,type,e.v);
}
static int reg8(const char *s) {
    static const char *r[] = {
        "B","C","D","E","H","L","M","A",0
    }
    ;
    int i;
    if(streqi(s,"(HL)")) return 6;
    for(i=0;r[i];i++) if(streqi(s,r[i])) return i;
    return -1;
}
static int rp(const char *s) {
    if(streqi(s,"B")||streqi(s,"BC")) return 0;
    if(streqi(s,"D")||streqi(s,"DE")) return 1;
    if(streqi(s,"H")||streqi(s,"HL")) return 2;
    if(streqi(s,"SP")) return 3;
    return -1;
}
static int rp2(const char *s) {
    if(streqi(s,"B")||streqi(s,"BC")) return 0;
    if(streqi(s,"D")||streqi(s,"DE")) return 1;
    if(streqi(s,"H")||streqi(s,"HL")) return 2;
    if(streqi(s,"PSW")||streqi(s,"AF")) return 3;
    return -1;
}
static int cc(const char *s) {
    static const char *c[]= {
        "NZ","Z","NC","C","PO","PE","P","M",0
    }
    ;
    int i;
    for(i=0;c[i];i++) if(streqi(s,c[i])) return i;
    return -1;
}
static int split_args(char *s,char *argv[],int max) {
    int n=0,q=0,paren=0;
    char qc=0,*p=s,*start=s;
    while(*p) {
        if(q) {
            if(*p==qc) q=0;
        } else if(*p=='\''||*p=='\"') {
            q=1;
            qc=*p;
        } else if(*p=='(') paren++;
        else if(*p==')'&&paren>0) paren--;
        else if(*p==','&&paren==0) {
            *p=0;
            if(n<max) argv[n++]=trim(start);
            start=p+1;
        }
        p++;
    }
    if(*trim(start)||n) {
        if(n<max) argv[n++]=trim(start);
    }
    return n;
}
static int indexed_arg(const char *s,int *prefix,int *disp) {
    char b[MAXNAME];
    int i=0;
    const char *p=s;
    *prefix=0;
    *disp=0;
    while(isspace((unsigned char)*p)) p++;
    if(*p!='(') return 0;
    p++;
    while(*p && *p!=')' && i<MAXNAME-1) b[i++]=(char)toupper((unsigned char)*p++);
    b[i]=0;
    if(*p!=')') return 0;
    if(strncmp(b,"IX",2)==0) {
        *prefix=0xdd;
        *disp=atoi(b+2);
        return 1;
    }
    if(strncmp(b,"IY",2)==0) {
        *prefix=0xfd;
        *disp=atoi(b+2);
        return 1;
    }
    return 0;
}
static int emit_z80_ld(Asm *a,char **av,int n) {
    int r1,r2,rr,pfx,d;
    Expr e;
    if(n!=2) return 0;
    /* Indexed forms must be tested first. */ if(indexed_arg(av[0],&pfx,&d)) {
        r2=reg8(av[1]);
        if(r2>=0 && r2!=6) {
            emitb(a,pfx);
            emitb(a,0x70+r2);
            emitb(a,d);
            return 1;
        }
        e=eval(a,av[1]);
        emitb(a,pfx);
        emitb(a,0x36);
        emitb(a,d);
        emitb(a,(int)e.v);
        return 1;
    }
    if(indexed_arg(av[1],&pfx,&d)) {
        r1=reg8(av[0]);
        if(r1>=0 && r1!=6) {
            emitb(a,pfx);
            emitb(a,0x46+(r1<<3));
            emitb(a,d);
            return 1;
        }
    }
    /* Z80 special register-pair transfers.  These must be recognized before
    the generic 16-bit immediate form, or LD SP,HL is misassembled as
    LD SP,0 (31 00 00), shifting all following labels and producing a
    REL file that L80 rejects.  RELFIX14. */
    if(streqi(av[0],"SP") && streqi(av[1],"HL")) {
        emitb(a,0xf9);
        return 1;
    }
    if(streqi(av[0],"SP") && streqi(av[1],"IX")) {
        emitb(a,0xdd);
        emitb(a,0xf9);
        return 1;
    }
    if(streqi(av[0],"SP") && streqi(av[1],"IY")) {
        emitb(a,0xfd);
        emitb(a,0xf9);
        return 1;
    }
    /* Register forms involving (HL)/M must come before absolute-memory forms. */ r1=reg8(av[0]);
    r2=reg8(av[1]);
    if(r1>=0 && r2>=0) {
        emitb(a,0x40+(r1<<3)+r2);
        return 1;
    }
    /* Z80/8080 one-byte accumulator indirect forms.  These must come before
    absolute-memory LD A,(nn)/LD (nn),A or LD A,(DE) becomes 3A nn nn and
    LD (DE),A becomes 32 nn nn, shifting all following labels.  RELFIX21. */
    if(streqi(av[0],"A") && streqi(av[1],"(BC)")) {
        emitb(a,0x0a);
        return 1;
    }
    if(streqi(av[0],"A") && streqi(av[1],"(DE)")) {
        emitb(a,0x1a);
        return 1;
    }
    if(streqi(av[0],"(BC)") && streqi(av[1],"A")) {
        emitb(a,0x02);
        return 1;
    }
    if(streqi(av[0],"(DE)") && streqi(av[1],"A")) {
        emitb(a,0x12);
        return 1;
    }
    /* Absolute-memory forms must come before generic register-immediate forms.
    Otherwise LD A,(FOO) becomes LD A,LOW FOO (3E nn) instead of 3A nn nn,
    shrinking the program and corrupting the REL relocation stream. */
    if(streqi(av[0],"A") && av[1][0]=='(') {
        e=eval(a,av[1]+1);
        emitb(a,0x3a);
        emitw(a,e);
        return 1;
    }
    if(av[0][0]=='(' && streqi(av[1],"A")) {
        e=eval(a,av[0]+1);
        emitb(a,0x32);
        emitw(a,e);
        return 1;
    }
    if(av[0][0]=='(' && streqi(av[1],"HL")) {
        e=eval(a,av[0]+1);
        emitb(a,0x22);
        emitw(a,e);
        return 1;
    }
    if(streqi(av[0],"HL") && av[1][0]=='(') {
        e=eval(a,av[1]+1);
        emitb(a,0x2a);
        emitw(a,e);
        return 1;
    }
    if(av[0][0]=='(' && (r2=rp(av[1]))>=0) {
        e=eval(a,av[0]+1);
        emitb(a,0xed);
        emitb(a,0x43+(r2<<4));
        emitw(a,e);
        return 1;
    }
    if((rr=rp(av[0]))>=0 && av[1][0]=='(') {
        e=eval(a,av[1]+1);
        emitb(a,0xed);
        emitb(a,0x4b+(rr<<4));
        emitw(a,e);
        return 1;
    }
    if((streqi(av[0],"IX")||streqi(av[0],"IY")) && av[1][0]=='(') {
        int px=streqi(av[0],"IX")?0xdd:0xfd;
        e=eval(a,av[1]+1);
        emitb(a,px);
        emitb(a,0x2a);
        emitw(a,e);
        return 1;
    }
    if(streqi(av[0],"IX")||streqi(av[0],"IY")) {
        int px=streqi(av[0],"IX")?0xdd:0xfd;
        e=eval(a,av[1]);
        emitb(a,px);
        emitb(a,0x21);
        emitw(a,e);
        return 1;
    }
    if(av[0][0]=='(' && (streqi(av[1],"IX")||streqi(av[1],"IY"))) {
        int px=streqi(av[1],"IX")?0xdd:0xfd;
        e=eval(a,av[0]+1);
        emitb(a,px);
        emitb(a,0x22);
        emitw(a,e);
        return 1;
    }
    if(r1>=0) {
        e=eval(a,av[1]);
        emitb(a,0x06+(r1<<3));
        emitb(a,(int)e.v);
        return 1;
    }
    if((r1=rp(av[0]))>=0) {
        e=eval(a,av[1]);
        emitb(a,0x01+(r1<<4));
        emitw(a,e);
        return 1;
    }
    return 0;
}
static int emit_indexed_alu(Asm *a,const char *op,const char *s) {
    int pfx,d,base;
    if(!indexed_arg(s,&pfx,&d)) return 0;
    if(streqi(op,"ADD")||streqi(op,"ADD A")) base=0x80;
    else if(streqi(op,"ADC")||streqi(op,"ADC A")) base=0x88;
    else if(streqi(op,"SUB")) base=0x90;
    else if(streqi(op,"SBC")||streqi(op,"SBC A")) base=0x98;
    else if(streqi(op,"AND")||streqi(op,"ANA")) base=0xa0;
    else if(streqi(op,"XOR")||streqi(op,"XRA")) base=0xa8;
    else if(streqi(op,"OR")||streqi(op,"ORA")) base=0xb0;
    else if(streqi(op,"CP")||streqi(op,"CMP")) base=0xb8;
    else return 0;
    emitb(a,pfx);
    emitb(a,base+6);
    emitb(a,d);
    return 1;
}
static int assemble_op(Asm *a,char *op,char *args) {
    char *av[MAXARGS];
    int n,r,c,rr,pfx,d;
    Expr e;
    n=split_args(args,av,MAXARGS);
#define O1(x) do{emitb(a,(x));return 1;}while(0)
#define O2(x,y) do{emitb(a,(x));emitb(a,(y));return 1;}while(0)
    if(streqi(op,"NOP")) O1(0x00);
    if(streqi(op,"HLT")) O1(0x76);
    if(streqi(op,"HALT")) O1(0x76);
    if(streqi(op,"RET")) {
        if(n==0) O1(0xc9);
        c=cc(av[0]);
        if(c>=0) O1(0xc0+(c<<3));
    }
    if(streqi(op,"EI")) O1(0xfb);
    if(streqi(op,"DI")) O1(0xf3);
    if(streqi(op,"CMA")||streqi(op,"CPL")) O1(0x2f);
    if(streqi(op,"STC")||streqi(op,"SCF")) O1(0x37);
    if(streqi(op,"CMC")||streqi(op,"CCF")) O1(0x3f);
    if(streqi(op,"DAA")) O1(0x27);
    if(streqi(op,"RLC")||streqi(op,"RLCA")) O1(0x07);
    if(streqi(op,"RRC")||streqi(op,"RRCA")) O1(0x0f);
    if(streqi(op,"RAL")||streqi(op,"RLA")) O1(0x17);
    if(streqi(op,"RAR")||streqi(op,"RRA")) O1(0x1f);
    if(streqi(op,"XCHG")||streqi(op,"EXDEHL")) O1(0xeb);
    if(streqi(op,"XTHL")) O1(0xe3);
    if(streqi(op,"SPHL")) O1(0xf9);
    if(streqi(op,"PCHL")||streqi(op,"JP_HL")) O1(0xe9);
    if(streqi(op,"EX") && n==2) {
        if(streqi(av[0],"DE")&&streqi(av[1],"HL")) O1(0xeb);
        if(streqi(av[0],"AF")&&streqi(av[1],"AF'")) O1(0x08);
        if(streqi(av[0],"(SP)")&&streqi(av[1],"HL")) O1(0xe3);
    }
    if(streqi(op,"EXX")) O1(0xd9);
    if(streqi(op,"NEG")) O2(0xed,0x44);
    if(streqi(op,"RETI")) O2(0xed,0x4d);
    if(streqi(op,"RETN")) O2(0xed,0x45);
    if(streqi(op,"LDI")) O2(0xed,0xa0);
    if(streqi(op,"LDIR")) O2(0xed,0xb0);
    if(streqi(op,"LDD")) O2(0xed,0xa8);
    if(streqi(op,"LDDR")) O2(0xed,0xb8);
    if(streqi(op,"CPI")) O2(0xed,0xa1);
    if(streqi(op,"CPIR")) O2(0xed,0xb1);
    if(streqi(op,"CPD")) O2(0xed,0xa9);
    if(streqi(op,"CPDR")) O2(0xed,0xb9);
    if(streqi(op,"LD")||streqi(op,"MOV")||streqi(op,"MVI")||streqi(op,"LXI")) {
        if(streqi(op,"MOV")&&n==2) {
            r=reg8(av[0]);
            c=reg8(av[1]);
            if(r>=0&&c>=0) O1(0x40+(r<<3)+c);
        }
        if(streqi(op,"MVI")&&n==2) {
            r=reg8(av[0]);
            if(r>=0) {
                e=eval(a,av[1]);
                O2(0x06+(r<<3),(int)e.v);
            }
        }
        if(streqi(op,"LXI")&&n==2) {
            r=rp(av[0]);
            if(r>=0) {
                e=eval(a,av[1]);
                emitb(a,0x01+(r<<4));
                emitw(a,e);
                return 1;
            }
        }
        return emit_z80_ld(a,av,n);
    }
    if(streqi(op,"INR")||streqi(op,"INC")) {
        if(n==1) {
            if(streqi(av[0],"IX")||streqi(av[0],"IY")) {
                emitb(a,streqi(av[0],"IX")?0xdd:0xfd);
                emitb(a,0x23);
                return 1;
            }
            r=reg8(av[0]);
            if(r>=0) O1(0x04+(r<<3));
            rr=rp(av[0]);
            if(rr>=0) O1(0x03+(rr<<4));
            if(indexed_arg(av[0],&pfx,&d)) {
                emitb(a,pfx);
                emitb(a,0x34);
                emitb(a,d);
                return 1;
            }
        }
    }
    if(streqi(op,"DCR")||streqi(op,"DEC")) {
        if(n==1) {
            if(streqi(av[0],"IX")||streqi(av[0],"IY")) {
                emitb(a,streqi(av[0],"IX")?0xdd:0xfd);
                emitb(a,0x2b);
                return 1;
            }
            r=reg8(av[0]);
            if(r>=0) O1(0x05+(r<<3));
            rr=rp(av[0]);
            if(rr>=0) O1(0x0b+(rr<<4));
            if(indexed_arg(av[0],&pfx,&d)) {
                emitb(a,pfx);
                emitb(a,0x35);
                emitb(a,d);
                return 1;
            }
        }
    }
    if(streqi(op,"ADD")) {
        if(n==2&&streqi(av[0],"A")&&emit_indexed_alu(a,"ADD",av[1])) return 1;
        if(n==1&&emit_indexed_alu(a,"ADD",av[0])) return 1;
        if(n==2&&(streqi(av[0],"IX")||streqi(av[0],"IY"))) {
            int px=streqi(av[0],"IX")?0xdd:0xfd;
            rr=rp(av[1]);
            if(rr>=0) {
                emitb(a,px);
                emitb(a,0x09+(rr<<4));
                return 1;
            }
        }
        if(n==1) {
            r=reg8(av[0]);
            if(r>=0) O1(0x80+r);
            e=eval(a,av[0]);
            O2(0xc6,(int)e.v);
        }
        if(n==2 && streqi(av[0],"A")) {
            r=reg8(av[1]);
            if(r>=0) O1(0x80+r);
            e=eval(a,av[1]);
            O2(0xc6,(int)e.v);
        }
        if(n==2 && streqi(av[0],"HL")) {
            rr=rp(av[1]);
            if(rr>=0) O1(0x09+(rr<<4));
        }
    }
    if(streqi(op,"ADC")||streqi(op,"ACI")) {
        if(n==2&&streqi(av[0],"A")&&emit_indexed_alu(a,"ADC",av[1])) return 1;
        if(n==1&&emit_indexed_alu(a,"ADC",av[0])) return 1;
        if(n==2&&streqi(av[0],"HL")&&(rr=rp(av[1]))>=0) {
            emitb(a,0xed);
            emitb(a,0x4a+(rr<<4));
            return 1;
        }
        if(n==1||streqi(op,"ACI")) {
            const char *s= n?av[0]:args;
            r=reg8(s);
            if(r>=0) O1(0x88+r);
            e=eval(a,s);
            O2(0xce,(int)e.v);
        }
        if(n==2&&streqi(av[0],"A")) {
            r=reg8(av[1]);
            if(r>=0) O1(0x88+r);
            e=eval(a,av[1]);
            O2(0xce,(int)e.v);
        }
    }
    if(streqi(op,"SBC") && n==2 && streqi(av[0],"HL") && (rr=rp(av[1]))>=0) {
        emitb(a,0xed);
        emitb(a,0x42+(rr<<4));
        return 1;
    }
    if(streqi(op,"SBC") && n==2 && streqi(av[0],"A")) {
        if(emit_indexed_alu(a,"SBC",av[1])) return 1;
        r=reg8(av[1]);
        if(r>=0) O1(0x98+r);
        e=eval(a,av[1]);
        O2(0xde,(int)e.v);
    }
    if(streqi(op,"SUB")||streqi(op,"SUI")) {
        const char *s=n?av[0]:args;
        if(emit_indexed_alu(a,"SUB",s)) return 1;
        r=reg8(s);
        if(r>=0) O1(0x90+r);
        e=eval(a,s);
        O2(0xd6,(int)e.v);
    }
    if(streqi(op,"SBB")||streqi(op,"SBI")) {
        const char *s=n?av[0]:args;
        if(emit_indexed_alu(a,"SBC",s)) return 1;
        r=reg8(s);
        if(r>=0) O1(0x98+r);
        e=eval(a,s);
        O2(0xde,(int)e.v);
    }
    if(streqi(op,"ANA")||streqi(op,"AND")||streqi(op,"ANI")) {
        const char *s=(n==2&&streqi(av[0],"A"))?av[1]:(n?av[0]:args);
        if(emit_indexed_alu(a,"AND",s)) return 1;
        r=reg8(s);
        if(r>=0) O1(0xa0+r);
        e=eval(a,s);
        O2(0xe6,(int)e.v);
    }
    if(streqi(op,"XRA")||streqi(op,"XOR")||streqi(op,"XRI")) {
        const char *s=(n==2&&streqi(av[0],"A"))?av[1]:(n?av[0]:args);
        if(emit_indexed_alu(a,"XOR",s)) return 1;
        r=reg8(s);
        if(r>=0) O1(0xa8+r);
        e=eval(a,s);
        O2(0xee,(int)e.v);
    }
    if(streqi(op,"ORA")||streqi(op,"OR")||streqi(op,"ORI")) {
        const char *s=(n==2&&streqi(av[0],"A"))?av[1]:(n?av[0]:args);
        if(emit_indexed_alu(a,"OR",s)) return 1;
        r=reg8(s);
        if(r>=0) O1(0xb0+r);
        e=eval(a,s);
        O2(0xf6,(int)e.v);
    }
    if(streqi(op,"CMP")||streqi(op,"CP")||streqi(op,"CPI")) {
        const char *s=(n==2&&streqi(av[0],"A"))?av[1]:(n?av[0]:args);
        if(emit_indexed_alu(a,"CP",s)) return 1;
        r=reg8(s);
        if(r>=0) O1(0xb8+r);
        e=eval(a,s);
        O2(0xfe,(int)e.v);
    }
    if(streqi(op,"DAD")) {
        if(n==1 && (rr=rp(av[0]))>=0) O1(0x09+(rr<<4));
    }
    if(streqi(op,"PUSH")) {
        if(n==1 && (streqi(av[0],"IX")||streqi(av[0],"IY"))) {
            emitb(a,streqi(av[0],"IX")?0xdd:0xfd);
            emitb(a,0xe5);
            return 1;
        }
        if(n==1 && (rr=rp2(av[0]))>=0) O1(0xc5+(rr<<4));
    }
    if(streqi(op,"POP")) {
        if(n==1 && (streqi(av[0],"IX")||streqi(av[0],"IY"))) {
            emitb(a,streqi(av[0],"IX")?0xdd:0xfd);
            emitb(a,0xe1);
            return 1;
        }
        if(n==1 && (rr=rp2(av[0]))>=0) O1(0xc1+(rr<<4));
    }
    if(streqi(op,"JMP")||streqi(op,"JP")) {
        if(n==1) {
            char tb[64];
            strcpy(tb,av[0]);
            trim(tb);
            if(streqi(tb,"(HL)")||streqi(tb,"HL")) {
                emitb(a,0xe9);
                return 1;
            }
            if(streqi(tb,"(IX)")||streqi(tb,"IX")) {
                emitb(a,0xdd);
                emitb(a,0xe9);
                return 1;
            }
            if(streqi(tb,"(IY)")||streqi(tb,"IY")) {
                emitb(a,0xfd);
                emitb(a,0xe9);
                return 1;
            }
            e=eval(a,av[0]);
            emitb(a,0xc3);
            emitw(a,e);
            return 1;
        }
        if(n==2 && (c=cc(av[0]))>=0) {
            e=eval(a,av[1]);
            emitb(a,0xc2+(c<<3));
            emitw(a,e);
            return 1;
        }
    }
    if(streqi(op,"CALL")) {
        if(n==1) {
            e=eval(a,av[0]);
            emitb(a,0xcd);
            emitw(a,e);
            return 1;
        }
        if(n==2 && (c=cc(av[0]))>=0) {
            e=eval(a,av[1]);
            emitb(a,0xc4+(c<<3));
            emitw(a,e);
            return 1;
        }
    }
    if(streqi(op,"JR")||streqi(op,"DJNZ")) {
        long here=*cur_lc_ptr(a);
        if(streqi(op,"DJNZ")) {
            e=eval(a,av[0]);
            emitb(a,0x10);
            emitb(a,(int)(e.v-(here+2)));
            return 1;
        }
        if(n==1) {
            e=eval(a,av[0]);
            emitb(a,0x18);
            emitb(a,(int)(e.v-(here+2)));
            return 1;
        }
        if(n==2) {
            int opc=-1;
            if(streqi(av[0],"NZ")) opc=0x20;
            else if(streqi(av[0],"Z")) opc=0x28;
            else if(streqi(av[0],"NC")) opc=0x30;
            else if(streqi(av[0],"C")) opc=0x38;
            if(opc>=0) {
                e=eval(a,av[1]);
                emitb(a,opc);
                emitb(a,(int)(e.v-(here+2)));
                return 1;
            }
        }
    }
    if((streqi(op,"RL") || streqi(op,"RLC") || streqi(op,"RR") || streqi(op,"RRC") ||
        streqi(op,"SLA") || streqi(op,"SRA") || streqi(op,"SRL")) && n==1) {
        int base=-1;
        r=reg8(av[0]);
        if(r>=0) {
            if(streqi(op,"RLC")) base=0x00;
            else if(streqi(op,"RRC")) base=0x08;
            else if(streqi(op,"RL")) base=0x10;
            else if(streqi(op,"RR")) base=0x18;
            else if(streqi(op,"SLA")) base=0x20;
            else if(streqi(op,"SRA")) base=0x28;
            else if(streqi(op,"SRL")) base=0x38;
            emitb(a,0xcb);
            emitb(a,base+r);
            return 1;
        }
    }
    if((streqi(op,"BIT")||streqi(op,"RES")||streqi(op,"SET")) && n==2) {
        int bit=(int)eval(a,av[0]).v;
        int base=streqi(op,"BIT")?0x40:(streqi(op,"RES")?0x80:0xc0);
        r=reg8(av[1]);
        if(r>=0) {
            emitb(a,0xcb);
            emitb(a,base+((bit&7)<<3)+r);
            return 1;
        }
        if(indexed_arg(av[1],&pfx,&d)) {
            emitb(a,pfx);
            emitb(a,0xcb);
            emitb(a,d);
            emitb(a,base+((bit&7)<<3)+6);
            return 1;
        }
    }
    if(streqi(op,"RST")) {
        e=eval(a,av[0]);
        O1(0xc7+((int)e.v&7)*8);
    }
    if(streqi(op,"IN")) {
        if(n==1) {
            e=eval(a,av[0]);
            O2(0xdb,(int)e.v);
        }
        if(n==2 && streqi(av[0],"A")) {
            e=eval(a,av[1]);
            O2(0xdb,(int)e.v);
        }
    }
    if(streqi(op,"OUT")) {
        if(n==1) {
            e=eval(a,av[0]);
            O2(0xd3,(int)e.v);
        }
        if(n==2) {
            e=eval(a,av[0]);
            emitb(a,0xd3);
            emitb(a,(int)e.v);
            return 1;
        }
    }
    if(streqi(op,"IM")) {
        e=eval(a,av[0]);
        if(e.v==0) O2(0xed,0x46);
        if(e.v==1) O2(0xed,0x56);
        if(e.v==2) O2(0xed,0x5e);
    }
    if(streqi(op,"LDA")) {
        e=eval(a,av[0]);
        emitb(a,0x3a);
        emitw(a,e);
        return 1;
    }
    if(streqi(op,"STA")) {
        e=eval(a,av[0]);
        emitb(a,0x32);
        emitw(a,e);
        return 1;
    }
    if(streqi(op,"LHLD")) {
        e=eval(a,av[0]);
        emitb(a,0x2a);
        emitw(a,e);
        return 1;
    }
    if(streqi(op,"SHLD")) {
        e=eval(a,av[0]);
        emitb(a,0x22);
        emitw(a,e);
        return 1;
    }
#undef O1
#undef O2
    return 0;
}
static void define_label(Asm *a,const char *name,int pub) {
    Sym *s=sym_find(a,name,1);
    if(a->pass==1 || s->is_set) {
        s->value=*cur_lc_ptr(a);
        s->type=cur_type(a);
        s->defined=1;
    }
    if(pub) s->is_public=1;
}
static void record_debug_line(Asm *a,const char *orig) {
    const char *p=orig;
    char file[MAXLINE];
    int n=0;
    DebugLine *d;
    while(*p && isspace((unsigned char)*p)) p++;
    if(strncmp(p,";@dcc-line",10)!=0 || !isspace((unsigned char)p[10])) return;
    p+=10;
    while(*p && isspace((unsigned char)*p)) p++;
    if(*p++!='\"') return;
    while(*p && *p!='\"' && n+1<MAXLINE) {
        if(*p=='\\' && p[1]) p++;
        file[n++]=*p++;
    }
    file[n]=0;
    if(*p!='\"') return;
    p++;
    while(*p && isspace((unsigned char)*p)) p++;
    if(!isdigit((unsigned char)*p)) return;
    d=(DebugLine*)calloc(1,sizeof(DebugLine));
    if(!d) { fprintf(stderr,"out of memory\n"); exit(1); }
    d->file=xstrdup(file);
    d->line=strtol(p,NULL,10);
    d->off=*cur_lc_ptr(a);
    d->seg=a->seg;
    if(a->debug_tail) a->debug_tail->next=d;
    else a->debug_lines=d;
    a->debug_tail=d;
}
static void record_debug_info(Asm *a,const char *orig) {
    const char *p=orig;
    const char *args;
    int kind=0;
    DebugInfo *d;
    while(*p && isspace((unsigned char)*p)) p++;
    if(strncmp(p,";@dcc-func-begin",16)==0 && isspace((unsigned char)p[16])) {
        kind=1;
        args=p+16;
    } else if(strncmp(p,";@dcc-func-end",14)==0 && isspace((unsigned char)p[14])) {
        kind=2;
        args=p+14;
    } else if(strncmp(p,";@dcc-var",9)==0 && isspace((unsigned char)p[9])) {
        kind=3;
        args=p+9;
    } else if(strncmp(p,";@dcc-global",12)==0 && isspace((unsigned char)p[12])) {
        kind=4;
        args=p+12;
    } else if(strncmp(p,";@dcc-struct",12)==0 && isspace((unsigned char)p[12])) {
        kind=5;
        args=p+12;
    } else if(strncmp(p,";@dcc-field",11)==0 && isspace((unsigned char)p[11])) {
        kind=6;
        args=p+11;
    } else if(strncmp(p,";@dcc-var-end",13)==0 && isspace((unsigned char)p[13])) {
        kind=7;
        args=p+13;
    } else return;
    while(*args && isspace((unsigned char)*args)) args++;
    d=(DebugInfo*)calloc(1,sizeof(DebugInfo));
    if(!d) { fprintf(stderr,"out of memory\n"); exit(1); }
    d->kind=kind;
    d->args=xstrdup(args);
    d->off=*cur_lc_ptr(a);
    d->seg=a->seg;
    if(a->debug_info_tail) a->debug_info_tail->next=d;
    else a->debug_info=d;
    a->debug_info_tail=d;
}
static int db_arg_is_plain_string(char *s) {
    int q;
    char *p;
    s=trim(s);
    if(*s!='\'' && *s!='\"') return 0;
    q=*s++;
    p=s;
    while(*p && *p!=q) p++;
    if(*p!=q) return 0;
    p++;
    while(*p && isspace((unsigned char)*p)) p++;
    return *p==0;
}
static void do_db(Asm *a,char *args) {
    char *av[MAXARGS];
    int n,i;
    n=split_args(args,av,MAXARGS);
    for(i=0;i<n;i++) {
        char *s=trim(av[i]);
        if(db_arg_is_plain_string(s)) {
            int q=*s++;
            while(*s && *s!=q) emitb(a,(unsigned char)*s++);
        } else {
            Expr e=eval(a,s);
            emitb(a,(int)e.v);
        }
    }
}
static void do_dw(Asm *a,char *args) {
    char *av[MAXARGS];
    int n,i;
    n=split_args(args,av,MAXARGS);
    for(i=0;i<n;i++) {
        Expr e=eval(a,av[i]);
        emitw(a,e);
    }
}
static void reserve_gap(Asm *a,long at,long n) {
    if(a->pass==2 && n>0) {
        if(a->seg==SEG_DATA) vec_reserve(&a->data,at,n);
        else if(a->seg==SEG_CODE) vec_reserve(&a->code,at,n);
    }
}
static void do_ds(Asm *a,char *args) {
    Expr e=eval(a,args);
    long i,oldlc;
    oldlc=*cur_lc_ptr(a);
    if(a->init_ds) for(i=0;i<e.v;i++) emitb(a,0);
    else {
        reserve_gap(a,oldlc,e.v);
        *cur_lc_ptr(a)+=e.v;
    }
}
static void pseudo(Asm *a,char *label,char *op,char *args) {
    char *av[MAXARGS];
    int n,i;
    Expr e;
    Sym *s;
    if(streqi(op,"ASEG")) {
        a->seg=SEG_ABS;
        return;
    }
    if(streqi(op,"CSEG")) {
        a->seg=SEG_CODE;
        return;
    }
    if(streqi(op,"DSEG")) {
        a->seg=SEG_DATA;
        return;
    }
    if(streqi(op,"ORG")) {
        long oldlc=*cur_lc_ptr(a);
        e=eval(a,args);
        if(e.v>oldlc) reserve_gap(a,oldlc,e.v-oldlc);
        *cur_lc_ptr(a)=e.v;
        return;
    }
    if(streqi(op,"DB")||streqi(op,"DEFB")||streqi(op,"DEFM")||streqi(op,"BYTE")) {
        do_db(a,args);
        return;
    }
    if(streqi(op,"DW")||streqi(op,"DEFW")||streqi(op,"WORD")) {
        do_dw(a,args);
        return;
    }
    if(streqi(op,"DS")||streqi(op,"DEFS")||streqi(op,"BLKB")) {
        do_ds(a,args);
        return;
    }
    if(streqi(op,"EQU")||streqi(op,"SET")) {
        if(!*label) {
            seterr(a,"EQU/SET without label");
            return;
        }
        e=eval(a,args);
        s=sym_find(a,label,1);
        if(a->pass==1 || streqi(op,"SET")) {
            s->value=e.v;
            s->type=e.type;
            s->defined=1;
            s->is_set=streqi(op,"SET");
        }
        return;
    }
    if(streqi(op,"PUBLIC")||streqi(op,"ENTRY")) {
        n=split_args(args,av,MAXARGS);
        for(i=0;i<n;i++) {
            s=sym_find(a,av[i],1);
            s->is_public=1;
        }
        return;
    }
    if(streqi(op,"EXTRN")||streqi(op,"EXT")) {
        n=split_args(args,av,MAXARGS);
        for(i=0;i<n;i++) {
            s=sym_find(a,av[i],1);
            s->is_extern=1;
        }
        return;
    }
    if(streqi(op,"NAME")||streqi(op,"TITLE")) {
        n=split_args(args,av,MAXARGS);
        if(n>0) {
            strncpy(a->modname,av[0],MAXNAME-1);
            a->modname[MAXNAME-1]=0;
        }
        return;
    }
    if(streqi(op,".RADIX")||streqi(op,"RADIX")) {
        e=eval(a,args);
        if(e.v>=2 && e.v<=16) a->radix=(int)e.v;
        return;
    }
    if(streqi(op,"END")) {
        if(args && *trim(args)) {
            e=eval(a,args);
            a->end_value=e.v;
            a->end_type=e.type;
            a->have_end=1;
        }
        return;
    }
    seterr(a,"unknown pseudo-op");
}
static void conditional(Asm *a,char *op,char *args) {
    int take=1;
    Expr e;
    Sym *s;
    if(streqi(op,"IF")) {
        e=eval(a,args);
        take=(e.v!=0);
    } else if(streqi(op,"IFE")) {
        e=eval(a,args);
        take=(e.v==0);
    } else if(streqi(op,"IFDEF")) {
        s=sym_find(a,args,0);
        take=(s&&s->defined);
    } else if(streqi(op,"IFNDEF")) {
        s=sym_find(a,args,0);
        take=!(s&&s->defined);
    } else if(streqi(op,"ELSE")) {
        if(a->cond_sp>0) a->cond_stack[a->cond_sp-1]=!a->cond_stack[a->cond_sp-1];
    } else if(streqi(op,"ENDIF")) {
        if(a->cond_sp>0) a->cond_sp--;
    }
    if(streqi(op,"IF")||streqi(op,"IFE")||streqi(op,"IFDEF")||streqi(op,"IFNDEF")) {
        if(a->cond_sp<MAXCOND) a->cond_stack[a->cond_sp++]=take;
    }
    a->skipping=0;
    for(take=0;take<a->cond_sp;take++) if(!a->cond_stack[take]) a->skipping=1;
}
static int is_cond(const char *op) {
    return streqi(op,"IF") || streqi(op,"IFE") || streqi(op,"IFDEF") ||
        streqi(op,"IFNDEF") || streqi(op,"ELSE") || streqi(op,"ENDIF");
}
static int is_pseudo(const char *op) {
    const char *p[]= {
        "ASEG","CSEG","DSEG","ORG","DB","DEFB","DEFM","BYTE",
        "DW","DEFW","WORD","DS","DEFS","BLKB","EQU","SET",
        "PUBLIC","ENTRY","EXTRN","EXT","NAME","TITLE",
        ".RADIX","RADIX","END",0
    }
    ;
    int i;
    for(i=0;p[i];i++) if(streqi(op,p[i])) return 1;
    return 0;
}
static void parse_line(Asm *a,char *line,char *orig) {
    char *p,*q,*op,*args,*label="";
    char labbuf[MAXNAME];
    int pub=0, bol_label;
    a->last_err[0]=0;
    if(a->pass==2) {
        record_debug_line(a,orig);
        record_debug_info(a,orig);
    }
    p=line;
    bol_label=(*p && !isspace((unsigned char)*p));
    q=p;
    while(*q) {
        if(*q==';') {
            *q=0;
            break;
        }
        if(*q=='"' || (*q=='\'' && (q==p || !isalnum((unsigned char)q[-1])))) {
            int qc=*q++;
            while(*q && *q!=qc) q++;
        }
        if(*q) q++;
    }
    p=trim(p);
    if(!*p) goto listed;
    if(isid0((unsigned char)*p)) {
        q=p;
        while(isid((unsigned char)*q)) q++;
        if(*q==':') {
            int l=(int)(q-p);
            if(l>=MAXNAME) l=MAXNAME-1;
            memcpy(labbuf,p,(size_t)l);
            labbuf[l]=0;
            label=labbuf;
            q++;
            if(*q==':') {
                pub=1;
                q++;
            }
            define_label(a,label,pub);
            p=trim(q);
        } else if(*q && isspace((unsigned char)*q)) {
            int l=(int)(q-p);
            char *r=trim(q);
            int force_equ_label = 0;
            if(!bol_label) {
                char *rq = r;
                while(*rq && !isspace((unsigned char)*rq)) rq++;
                if((rq-r)==3 && local_strncasecmp(r,"EQU",3)==0) force_equ_label = 1;
                if((rq-r)==3 && local_strncasecmp(r,"SET",3)==0) force_equ_label = 1;
            }
            if(*r && (bol_label || force_equ_label)) {
                if(l>=MAXNAME) l=MAXNAME-1;
                memcpy(labbuf,p,(size_t)l);
                labbuf[l]=0;
                label=labbuf;
                if(!force_equ_label) define_label(a,label,0);
                p=r;
            }
        }
    }
    if(!*p) goto listed;
    op=p;
    while(*p && !isspace((unsigned char)*p)) p++;
    if(*p) *p++=0;
    args=trim(p);
    if(is_cond(op)) {
        conditional(a,op,args);
        goto listed;
    }
    if(a->skipping) goto listed;
    /* "SET" is ambiguous: label-less "SET b,r" is the Z80 bit-set opcode,
     * while "LABEL SET expr" is the redefinable-symbol pseudo-op. Only the
     * latter has a label by this point, so without one prefer the opcode. */
    if(is_pseudo(op) && !(streqi(op,"SET") && !*label)) pseudo(a,(char*)label,op,args);
    else if(!assemble_op(a,op,args)) {
        if(!*label && (isdigit((unsigned char)op[0]) || op[0]=='$' || op[0]=='\'' || op[0]=='"')) {
            Expr ex=eval(a,op);
            emitb(a,(int)ex.v);
        } else seterr(a,"unknown opcode");
    }
    listed: if(a->pass==2 && a->lst) {
        fprintf(a->lst,"%04lX %5ld  %s", (unsigned long)*cur_lc_ptr(a)&0xffff, a->lineno, orig);
        if(orig[strlen(orig)-1]!='\n') fputc('\n',a->lst);
        if(a->last_err[0]) fprintf(a->lst,"***** %s\n",a->last_err);
    }
}
static int assemble_pass(Asm *a,int pass) {
    char line[MAXLINE],orig[MAXLINE];
    a->pass=pass;
    a->lineno=0;
    a->lc_code=a->lc_data=a->lc_abs=0;
    a->seg=SEG_CODE;
    a->cond_sp=0;
    a->skipping=0;
    a->fp=fopen(a->src,"r");
    if(!a->fp) {
        fprintf(stderr,"cannot open %s\n",a->src);
        return 0;
    }
    if(pass==2 && a->want_prn) {
        a->lst=fopen(a->prn,"w");
        if(!a->lst) {
            fprintf(stderr,"cannot create %s\n",a->prn);
            fclose(a->fp);
            return 0;
        }
    } while(fgets(line,sizeof(line),a->fp)) {
        size_t len;
        a->lineno++;
        strcpy(orig,line);
        len=strlen(line);
        while(len && (line[len-1]=='\n'||line[len-1]=='\r'||line[len-1]==26)) line[--len]=0;
        upcase(line);
        parse_line(a,line,orig);
    }
    fclose(a->fp);
    if(a->lst) {
        fclose(a->lst);
        a->lst=NULL;
    }
    return 1;
}
/* REL bit writer */typedef struct Rel Rel;
struct Rel {
    FILE *f;
    int acc,bits;
}
;
static void rel_bit(Rel *r,int b) {
    r->acc=(r->acc<<1)|(b&1);
    r->bits++;
    if(r->bits==8) {
        fputc(r->acc,r->f);
        r->acc=0;
        r->bits=0;
    }
}
static void rel_bits(Rel *r,unsigned v,int n) {
    int i;
    for(i=n-1;i>=0;i--) rel_bit(r,(v>>i)&1);
}
static void rel_byte(Rel *r,int b) {
    rel_bit(r,0);
    rel_bits(r,(unsigned)b,8);
}
static void rel_value(Rel *r,int type,long v) {
    unsigned x=(unsigned)(v&0xffff);
    rel_bits(r,(unsigned)type,2);
    rel_bits(r,x&0xff,8);
    rel_bits(r,(x>>8)&0xff,8);
}
static void rel_link_value(Rel *r,int type,long v) {
    rel_bit(r,1);
    rel_value(r,type,v);
}
static void rel_name(Rel *r,const char *name) {
    char sig[16];
    int i,n;
    m80sig(sig,name);
    n=(int)strlen(sig);
    if(n>7) n=7;
    rel_bits(r,(unsigned)n,3);
    for(i=0;i<n;i++) rel_bits(r,(unsigned char)sig[i],8);
}
static void rel_special(Rel *r,int code,int hasval,int type,long val,const char *name) {
    rel_bit(r,1);
    rel_bits(r,0,2);
    rel_bits(r,(unsigned)code,4);
    if(hasval) rel_value(r,type,val);
    if(name) rel_name(r,name);
}
static void rel_flush(Rel *r) {
    if(r->bits) {
        r->acc <<= (8-r->bits);
        fputc(r->acc,r->f);
        r->bits=0;
        r->acc=0;
    }
}
static void rel_pad_128(Rel *r) {
    long p,n;
    rel_flush(r);
    p=ftell(r->f);
    if(p<0) return;
    n=(128-(p&127))&127;
    while(n-- > 0) fputc(0,r->f);
}
static Fixup *find_fixup(Asm *a,int seg,long off) {
    Fixup **idx = (seg==SEG_DATA) ? a->fixup_idx_data : a->fixup_idx_code;
    long cap = (seg==SEG_DATA) ? a->fixup_idx_data_cap : a->fixup_idx_code_cap;
    if(off<0 || off>=cap) return NULL;
    return idx[off];
}
static void rel_segment_bytes(Asm *a,Rel *r,int seg,ByteVec *v) {
    long i;
    Fixup *f;
    int type = (seg==SEG_DATA) ? T_DATA : T_CODE;
    for(i=0;i<v->n;i++) {
        if(!v->used || !v->used[i]) {
            long j=i+1,k;
            while(j<v->n && (!v->used || !v->used[j])) j++;
            /* M80 emits LC records for labels that occur inside an
            otherwise un-emitted DS/ORG gap, then another LC record for
            the next emitted byte or, for a trailing DS gap, for the final
            segment size.  L80 tolerates fewer records in some cases, but
            M80-compatible REL output needs these records. */
            for(k=i+1;k<j;k++) {
                int emitted=0,hh;
                Sym *ss;
                for(hh=0;hh<SYMHASH && !emitted;hh++) for(ss=a->syms[hh];ss;ss=ss->next) if(ss->defined &&
                    ss->type==type && ss->value==k) {
                    rel_special(r,11,1,type,k,NULL);
                    emitted=1;
                    break;
                }
            }
            if(j<=v->n) rel_special(r,11,1,type,j,NULL);
            i=j-1;
            continue;
        }
        f=find_fixup(a,seg,i);
        if(f) {
            rel_link_value(r,f->type,f->val);
            i++;
        } else rel_byte(r,v->p[i]);
    }
}
static int rel_entry_rank(const char *name) {
    static const char *ord[]= {
        "_FCB_I","_FILE_","_DO_CO","_ENUME","_QUIET","_MAIN",
        "__MRUN","__STAC","__DATA","__BSSB","__BSSE","__HSTA",
        "__BRK","__HLIM","START","_EXIT","__CONO","__FGET",
        "_READ","__SCMP","_PRINT","__CPM_","_ERRNO","_OPEN",
        "_WRITE","_CLOSE","_UNLIN","_LSEEK","_FDATA",
        "__DMA","__FCBS","_RENAM","_FGETS","_FCLOS","_TMPNA",
        "_TMPFI","__FPS","_REWIN","_STRER","__PF32","_PFLNG",
        "__BUIL","__ARGC","ARGV","_BDOS","__SLEN","__SLF","__CHF","__SDUP",
        "_MALLO","__MCPY","__MSET","__CALL","__MULU","_BSEAR",
        "_QSORT","__MLH","_FREE","__FRCO",0
    }
    ;
    char sig[16];
    int i;
    m80sig(sig,name);
    for(i=0;ord[i];i++) if(strcmp(sig,ord[i])==0) return i;
    return 9999;
}
static int rel_def_rank(const char *name) {
    (void)name;
    return 0;
}
static int symptr_m80_cmp(const void *pa,const void *pb) {
    const Sym *a=*(const Sym * const *)pa;
    const Sym *b=*(const Sym * const *)pb;
    char sa[16],sb[16];
    m80sig(sa,a->name);
    m80sig(sb,b->name);
    if(sa[0]=='_' && sb[0]!='_') return -1;
    if(sa[0]!='_' && sb[0]=='_') return 1;
    return strcmp(sa,sb);
}
static void rel_module_name(char *out,const char *src) {
    const char *p,*slash,*bslash,*dot;
    int i;
    slash=strrchr(src,'/');
    bslash=strrchr(src,'\\');
    p=src;
    if(slash && slash+1>p) p=slash+1;
    if(bslash && bslash+1>p) p=bslash+1;
    dot=strrchr(p,'.');
    if(!dot) dot=p+strlen(p);
    for(i=0; p+i<dot && i<MAXNAME-1; i++) out[i]=(char)toupper((unsigned char)p[i]);
    out[i]=0;
}
static void write_rel(Asm *a) {
    FILE *f;
    Rel r;
    int h,rank;
    Sym *s;
    if(!a->want_rel) return;
    f=fopen(a->rel,"wb");
    if(!f) {
        fprintf(stderr,"cannot create %s\n",a->rel);
        a->errors++;
        return;
    }
    r.f=f;
    r.acc=0;
    r.bits=0;
    {
        char mn[MAXNAME];
        if(a->modname[0]) strcpy(mn,a->modname);
        else rel_module_name(mn,a->src);
        rel_special(&r,2,0,0,0,mn);
    }
    for(rank=0;rank<9999;rank++) for(h=0;h<SYMHASH;h++) for(s=a->syms[h];s;s=s->next) if(s->is_public &&
        rel_entry_rank(s->name)==rank) rel_special(&r,0,0,0,0,s->name);
    for(h=0;h<SYMHASH;h++) for(s=a->syms[h];s;s=s->next) if(s->is_public && rel_entry_rank(s->name)==9999)
        rel_special(&r,0,0,0,0,s->name);
    rel_special(&r,10,1,T_ABS,a->data.n,NULL);
    rel_special(&r,13,1,T_CODE,a->code.n,NULL);
    rel_special(&r,11,1,T_CODE,0,NULL);
    rel_segment_bytes(a,&r,SEG_CODE,&a->code);
    if(a->data.n) {
        rel_special(&r,11,1,T_DATA,0,NULL);
        rel_segment_bytes(a,&r,SEG_DATA,&a->data);
    }
    {
        Sym **arr;
        int cnt=0,idx=0;
        for(h=0;h<SYMHASH;h++) for(s=a->syms[h];s;s=s->next) if((s->is_public && s->defined) || s->chain_valid) cnt++;
        arr=(Sym**)calloc((size_t)(cnt?cnt:1),sizeof(Sym*));
        if(!arr) {
            fprintf(stderr,"oom\n");
            exit(2);
        }
        for(h=0;h<SYMHASH;h++) for(s=a->syms[h];s;s=s->next) if((s->is_public && s->defined) || s->chain_valid)
            arr[idx++]=s;
        qsort(arr,(size_t)cnt,sizeof(Sym*),symptr_m80_cmp);
        for(idx=0;idx<cnt;idx++) {
            s=arr[idx];
            if(s->is_public && s->defined) rel_special(&r,7,1,s->type,s->value,s->name);
            if(s->chain_valid) rel_special(&r,6,1,s->chain_type,s->chain_head,s->name);
        }
        free(arr);
    }
    rel_special(&r,14,1,a->have_end?a->end_type:T_ABS,a->have_end?a->end_value:0,NULL);
    /* RELFIX20: LINK-80 expects the item after END MODULE to start on
    the next byte boundary.  Do not place END FILE immediately after
    the 25-bit END MODULE item in the bit stream. */
    rel_flush(&r);
    rel_special(&r,15,0,0,0,NULL);
    rel_pad_128(&r);
    fclose(f);
}
static void write_sym(Asm *a) {
    FILE *f;
    int h;
    Sym *s;
    if(!a->want_sym) return;
    f=fopen(a->symfile,"w");
    if(!f) {
        fprintf(stderr,"cannot create %s\n",a->symfile);
        return;
    }
    for(h=0;h<SYMHASH;h++) {
        for(s=a->syms[h];s;s=s->next) {
            if(s->defined) {
                fprintf(f,"%-8s %04lX %s%s\n",s->name,s->value&0xffff,
                    s->is_public?"PUBLIC ":"",s->is_extern?"EXTRN":"");
            }
        }
    }
    fclose(f);
}
static const char *debug_segment_name(int type) {
    if(type==T_CODE || type==SEG_CODE) return "code";
    if(type==T_DATA || type==SEG_DATA) return "data";
    return "absolute";
}
static void write_debug(Asm *a) {
    FILE *f;
    DebugLine *d;
    DebugInfo *di;
    Sym *s;
    int h;
    const char *module=a->modname[0]?a->modname:a->src;
    if(!a->debug_lines) {
        remove(a->dbgfile);
        return;
    }
    f=fopen(a->dbgfile,"w");
    if(!f) {
        fprintf(stderr,"cannot create %s\n",a->dbgfile);
        return;
    }
    fprintf(f,"DCCDBG 1\nmodule \"%s\"\nsize code %ld\nsize data %ld\n",module,a->code.n,a->data.n);
    for(d=a->debug_lines;d;d=d->next) {
        const char *p=d->file;
        fprintf(f,"line %s %04lX %ld \"",debug_segment_name(d->seg),d->off&0xffff,d->line);
        while(*p) {
            if(*p=='\\' || *p=='\"') fputc('\\',f);
            fputc(*p++,f);
        }
        fprintf(f,"\"\n");
    }
    for(di=a->debug_info;di;di=di->next) {
        const char *kind;
        if(di->kind==4) {
            char label[MAXNAME];
            Sym *gs;
            if(sscanf(di->args,"\"%127[^\"]\"",label)!=1) continue;
            gs=sym_find(a,label,0);
            if(!gs || !gs->defined) continue;
            fprintf(f,"global %s %04lX %s",debug_segment_name(gs->type),gs->value&0xffff,di->args);
        } else if(di->kind==5 || di->kind==6) {
            fprintf(f,"%s %s",di->kind==5?"struct":"field",di->args);
        } else {
            kind=di->kind==1?"function-begin":(di->kind==2?"function-end":
                 (di->kind==7?"variable-end":"variable"));
            fprintf(f,"%s %s %04lX %s",kind,debug_segment_name(di->seg),di->off&0xffff,di->args);
        }
        if(di->args[0] && di->args[strlen(di->args)-1]!='\n') fputc('\n',f);
    }
    for(h=0;h<SYMHASH;h++) for(s=a->syms[h];s;s=s->next) if(s->defined)
        fprintf(f,"symbol %s %04lX \"%s\"\n",debug_segment_name(s->type),s->value&0xffff,s->name);
    fclose(f);
}
static void write_link_metadata(Asm *a) {
    FILE *f=fopen(a->linkfile,"w");
    if(!f) {
        fprintf(stderr,"cannot create %s\n",a->linkfile);
        return;
    }
    fprintf(f,"DCCLINK 1\nsize code %ld\nsize data %ld\n",a->code.n,a->data.n);
    fclose(f);
}
static void default_ext(char *out,const char *in,const char *ext) {
    char *dot,*slash;
    /* parse_cmd's default_ext(a->src,a->src,".MAC") aliases out==in; a strcpy
     * there is a self-copy (no-op) but macOS's fortified libc traps on any
     * strcpy source/dest overlap, even identical pointers, so skip the copy
     * entirely when there's nothing to do. */
    if(out!=in) strcpy(out,in);
    slash=strrchr(out,'/');
    {
        char *b=strrchr(out,'\\');
        if(!slash || (b&&b>slash)) slash=b;
    }
    dot=strrchr(out,'.');
    if(dot && (!slash || dot>slash)) *dot=0;
    strcat(out,ext);
}
static void uppercase_output_name(char *path) {
    char *p,*slash,*bslash;
    slash=strrchr(path,'/');
    bslash=strrchr(path,'\\');
    p=path;
    if(slash && slash+1>p) p=slash+1;
    if(bslash && bslash+1>p) p=bslash+1;
    while(*p) {
        *p=(char)toupper((unsigned char)*p);
        p++;
    }
}
static void parse_cmd(Asm *a,int argc,char **argv) {
    char cmd[1024],*eq,*comma,*p,*slash,*q;
    int i;
    memset(a,0,sizeof(*a));
    a->z80=1;
    a->list_hex=1;
    a->radix=10;
    if(argc<2) {
        fprintf(stderr,"usage: m80c rel,prn=source /Z /I /L /R /H /O /M\n");
        exit(1);
    }
    cmd[0]=0;
    for(i=1;i<argc;i++) {
        if(i>1) strcat(cmd," ");
        strcat(cmd,argv[i]);
    }
    for(q=cmd;(q=strchr(q,'/'))!=NULL;q++) {
        char sw=(char)toupper((unsigned char)q[1]);
        if(sw=='Z') a->z80=1;
        else if(sw=='I') a->z80=0;
        else if(sw=='L') a->want_prn=1;
        else if(sw=='R') a->want_rel=1;
        else if(sw=='O') {
            a->list_hex=0;
            a->want_rel=1;
        } else if(sw=='H') a->list_hex=1;
        else if(sw=='M') a->init_ds=1;
        else if(sw=='C') a->want_sym=1;
    }
    eq=strchr(cmd,'=');
    if(!eq) {
        fprintf(stderr,"bad M80 command; expected obj,prn=source\n");
        exit(1);
    }
    *eq=0;
    p=eq+1;
    slash=strchr(p,'/');
    if(slash) *slash=0;
    strcpy(a->src,trim(p));
    default_ext(a->src,a->src,".MAC");
    comma=strchr(cmd,',');
    if(comma) {
        *comma=0;
        if(*trim(cmd) && strcmp(trim(cmd),"*")!=0) {
            default_ext(a->rel,trim(cmd),".REL");
            uppercase_output_name(a->rel);
            a->want_rel=1;
        }
        if(*trim(comma+1) && strcmp(trim(comma+1),"*")!=0) {
            default_ext(a->prn,trim(comma+1),".PRN");
            uppercase_output_name(a->prn);
            a->want_prn=1;
        }
    } else {
        if(*trim(cmd) && strcmp(trim(cmd),"*")!=0) {
            default_ext(a->rel,trim(cmd),".REL");
            uppercase_output_name(a->rel);
            a->want_rel=1;
        }
    }
    if(!a->want_prn || !a->prn[0]) {
        default_ext(a->prn,a->src,".PRN");
        uppercase_output_name(a->prn);
    }
    if(!a->want_rel || !a->rel[0]) {
        default_ext(a->rel,a->src,".REL");
        uppercase_output_name(a->rel);
    }
    default_ext(a->symfile,a->src,".SYM");
    uppercase_output_name(a->symfile);
    default_ext(a->dbgfile,a->src,".DBG");
    uppercase_output_name(a->dbgfile);
    default_ext(a->linkfile,a->src,".LNK");
    uppercase_output_name(a->linkfile);
}
int main(int argc,char **argv) {
    Asm a;
    parse_cmd(&a,argc,argv);
    if(!assemble_pass(&a,1)) return 1;
    if(!assemble_pass(&a,2)) return 1;
    write_rel(&a);
    write_sym(&a);
    write_debug(&a);
    write_link_metadata(&a);
    if(a.errors) {
        fprintf(stderr,"%d error(s)\n",a.errors);
        return 1;
    }
    printf("No Fatal error(s)\n");
    return 0;
}
