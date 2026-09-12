#include <stdio.h>
#include <stdlib.h>

struct Stmt {
    int label;
    char *text;
    int unit;
    int op;
    struct Stmt *target;
    int target_label;
    int fmt;
    int sym;
    char *a;
    char *b;
    char *c;
    int ae;
    int be;
    int ce;
    int act;
    struct Stmt *act_target;
    int act_target_label;
    int act_sym;
    char *act_idx;
    char *act_rhs;
    int act_idx_e;
    int act_rhs_e;
    int ntargets;
    struct Stmt *targets[10];
};

static struct Stmt *g_stmts;
static int g_ns;
static struct Stmt *g_pc;

static _Noreturn void die(const char *message)
{
#ifdef MIR_CLOBBER_FORTRAN_TEMP
    const char *text = "";
    if (g_stmts && g_pc >= g_stmts && g_pc < g_stmts + g_ns)
        text = g_pc->text;
    fprintf(stderr, "forint:%s near pc=%d '%s'\n", message,
            (g_stmts && g_pc) ? (int)(g_pc - g_stmts) : -1,
            text);
#else
    fprintf(
#ifdef MIR_CLOBBER_FORTRAN_STDOUT
            stdout,
#else
            stderr,
#endif
            "forint:%s near pc=%d '%s'\n", message,
#ifdef MIR_CLOBBER_FORTRAN_REVERSE
            (g_stmts && g_pc) ? (int)(g_stmts - g_pc) : -1,
#else
            (g_stmts && g_pc) ? (int)(g_pc - g_stmts) : -1,
#endif
#ifdef MIR_CLOBBER_FORTRAN_RANGE
            (g_stmts && g_pc > g_stmts && g_pc < g_stmts + g_ns)
#else
            (g_stmts && g_pc >= g_stmts && g_pc < g_stmts + g_ns)
#endif
                ? g_pc->text : "");
#endif
#ifdef MIR_CLOBBER_FORTRAN_EXIT
    exit(2);
#else
    exit(1);
#endif
}

int main(void)
{
    static struct Stmt statements[2];

    statements[1].text = "LINE";
    g_stmts = statements;
    g_ns = 2;
    g_pc = statements + 1;
    die("boom");
}
