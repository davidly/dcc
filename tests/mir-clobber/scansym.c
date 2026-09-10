#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSYM 32
#define MAXMEM 64

struct Sym {
    char name[12];
    int scalar;
    int base;
    int size;
};

static struct Sym records[MAXSYM];
static struct Sym *sym;
static int nsym;
static int mtop;

static void die(const char *message)
{
    printf("error=%s\n", message);
    exit(1);
}

static int same(const char *left, const char *right)
{
    return strcmp(left, right) == 0;
}

static int sym_find(const char *name)
{
    int i;

    for (i = 0; i < nsym; i++)
        if (same(sym[i].name, name))
            return i;
    if (nsym >= MAXSYM)
        die("symbol table full");
#ifdef COPY_SHORT_NAME
    strncpy(sym[nsym].name, name, sizeof(sym[nsym].name) - 2);
#else
    strncpy(sym[nsym].name, name, sizeof(sym[nsym].name) - 1);
#endif
    sym[nsym].scalar = mtop++;
    sym[nsym].base = -1;
    sym[nsym].size = 0;
    if (mtop >= MAXMEM)
        die("memory full");
    return nsym++;
}

int main(void)
{
    int alpha;
    int beta;
    int alpha_again;

    sym = records;
    alpha = sym_find("ALPHA");
    beta = sym_find("BETA");
    alpha_again = sym_find("ALPHA");
    printf("ids=%d,%d,%d nsym=%d mtop=%d "
           "a=%d/%d/%d b=%d/%d/%d names=%s,%s\n",
        alpha, beta, alpha_again, nsym, mtop,
        sym[alpha].scalar, sym[alpha].base, sym[alpha].size,
        sym[beta].scalar, sym[beta].base, sym[beta].size,
        sym[alpha].name, sym[beta].name);
    return 0;
}
