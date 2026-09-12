#include <stdio.h>
#include <stdlib.h>

#define MAXTOK 64

struct Ins { int op; };
struct Sym { int value; };
struct Func { int entry; };
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

static void die(const char *s)
{
#ifdef MIR_CLOBBER_FATAL_EXTRA
    printf("fatal extra control\n");
#endif
    fprintf(stderr, "adaint:%d: %s near '%s'\n", G ? G->line : 0, s,
            G ? G->text : "");
    exit(1);
}

int main(void)
{
    die("boom");
    return 0;
}
