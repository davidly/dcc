#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSYM 32
#define MAXMEM 64

struct Sym {
    char name[12];
#ifdef SYMBOL_UNSIGNED_FIELDS
    unsigned int scalar;
    unsigned int base;
    unsigned int size;
#else
    int scalar;
    int base;
    int size;
#endif
};

static struct Sym records[MAXSYM];
#ifdef SYMBOL_COMPARE_MUTATES_GLOBALS
static struct Sym alternate_records[MAXSYM];
static int compare_calls;
#endif
#ifdef SYMBOL_VOLATILE_TABLE
static struct Sym * volatile sym;
#else
static struct Sym *sym;
#endif
#ifdef SYMBOL_UNSIGNED_COUNT
static unsigned int nsym;
#elif defined(SYMBOL_CAPACITY_CONTROL)
static int nsym = MAXSYM;
#elif defined(SYMBOL_COMPARE_MUTATES_GLOBALS)
static int nsym = 2;
#else
static int nsym;
#endif
#ifdef SYMBOL_UNSIGNED_MEMORY_TOP
static unsigned int mtop;
#elif defined(SYMBOL_MEMORY_CONTROL)
static int mtop = MAXMEM - 1;
#else
static int mtop;
#endif

static void die(const char *message)
{
    printf("error=%s\n", message);
    exit(1);
}

static int same(const char *left, const char *right)
{
#ifdef SYMBOL_COMPARE_MUTATES_GLOBALS
    if (compare_calls++ == 0)
        sym = alternate_records;
#endif
    return strcmp(left, right) == 0;
}

static int sym_find(const char *name)
{
#ifdef SYMBOL_UNSIGNED_INDEX
    unsigned int i;
#else
    int i;
#endif

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

#ifdef SYMBOL_COMPARE_MUTATES_GLOBALS
    strcpy(records[0].name, "OLD0");
    strcpy(records[1].name, "OLD1");
    strcpy(alternate_records[0].name, "ALT0");
    strcpy(alternate_records[1].name, "TARGET");
    sym = records;
    alpha = sym_find("TARGET");
    printf("mutation=%d nsym=%d active=%s,%s\n",
        alpha, (int)nsym, sym[0].name, sym[1].name);
    return 0;
#endif
    sym = records;
#ifdef SYMBOL_CAPACITY_CONTROL
    return sym_find("FULL");
#endif
#ifdef SYMBOL_MEMORY_CONTROL
    return sym_find("LAST");
#endif
#ifdef SYMBOL_LONG_NAME
    alpha = sym_find("ABCDEFGHIJKLMNO");
    printf("long=%d nsym=%d mtop=%d name=%s\n",
        alpha, (int)nsym, (int)mtop, sym[alpha].name);
    return 0;
#endif
    alpha = sym_find("ALPHA");
    beta = sym_find("BETA");
    alpha_again = sym_find("ALPHA");
    printf("ids=%d,%d,%d nsym=%d mtop=%d "
           "a=%d/%d/%d b=%d/%d/%d names=%s,%s\n",
        alpha, beta, alpha_again, (int)nsym, (int)mtop,
        sym[alpha].scalar, sym[alpha].base, sym[alpha].size,
        sym[beta].scalar, sym[beta].base, sym[beta].size,
        sym[alpha].name, sym[beta].name);
    return 0;
}
