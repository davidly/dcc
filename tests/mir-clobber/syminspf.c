#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSYM 128
#define MAXNAME 16

struct Sym {
    char name[MAXNAME];
    unsigned char kind;
    unsigned char scope;
    unsigned char esize;
    int val;
    int size;
    unsigned char isarr;
    int base;
    unsigned char proc;
};

static struct Sym *sym;
static int nsym;

static void die(const char *message)
{
    printf("error=%s\n", message);
    exit(1);
}

static int sym_add(const char *name, int kind, int scope)
{
    int index;

    if (nsym >= MAXSYM)
        die("symbol table full");
    index = nsym++;
    memset(&sym[index], 0, sizeof(sym[index]));
    strncpy(sym[index].name, name, MAXNAME - 1);
    sym[index].kind = kind;
    sym[index].scope = scope;
    sym[index].size = 2;
    sym[index].esize = 2;
    return index;
}

static int sym_find(const char *name)
{
    int index;

    for (index = 0; index < nsym; ++index)
        if (strcmp(sym[index].name, name) == 0)
            return index;
    return -1;
}

static int sym_intern(const char *name, int kind, int scope)
{
    int index;

    index = sym_find(name);
    if (index >= 0)
        return index;
    return sym_add(name, kind, scope);
}

static int record_zero(const struct Sym *record)
{
    const unsigned char *bytes;
    int index;

    bytes = (const unsigned char *)record;
    for (index = 0; index < (int)sizeof(*record); ++index)
        if (bytes[index] != 0)
            return 0;
    return 1;
}

int main(void)
{
    int first;
    int existing;
    int added;
    int again;
    int final;
    int failures;
    int count_after_first;
    int count_after_existing;
    int count_after_added;
    int count_after_again;
    int stride;
    int kind_offset;
    int scope_offset;
    int esize_offset;
    int val_offset;
    int size_offset;
    int isarr_offset;
    int base_offset;
    int proc_offset;
    int untouched;

    failures = 0;
    sym = (struct Sym *)calloc(MAXSYM, sizeof(struct Sym));
    if (sym == NULL)
        die("allocation failed");
    untouched = record_zero(&sym[3]);

    first = sym_add("OLD", 5, 7);
    count_after_first = nsym;
    existing = sym_intern("OLD", 99, 101);
    count_after_existing = nsym;
    added = sym_intern("NEW-SYMBOL-1234", 258, 259);
    count_after_added = nsym;
    again = sym_intern("NEW-SYMBOL-1234", 77, 88);
    count_after_again = nsym;
    final = sym_add("THIRD", -1, 256);

    stride = (int)((char *)&sym[1] - (char *)&sym[0]);
    kind_offset = (int)((char *)&sym[0].kind - (char *)&sym[0]);
    scope_offset = (int)((char *)&sym[0].scope - (char *)&sym[0]);
    esize_offset = (int)((char *)&sym[0].esize - (char *)&sym[0]);
    val_offset = (int)((char *)&sym[0].val - (char *)&sym[0]);
    size_offset = (int)((char *)&sym[0].size - (char *)&sym[0]);
    isarr_offset = (int)((char *)&sym[0].isarr - (char *)&sym[0]);
    base_offset = (int)((char *)&sym[0].base - (char *)&sym[0]);
    proc_offset = (int)((char *)&sym[0].proc - (char *)&sym[0]);

    if (!untouched || first != 0 || existing != 0 ||
        added != 1 || again != 1 || final != 2 ||
        count_after_first != 1 || count_after_existing != 1 ||
        count_after_added != 2 || count_after_again != 2 || nsym != 3)
        ++failures;
    if (stride != 27 || (int)sizeof(struct Sym) != 27 ||
        kind_offset != 16 || scope_offset != 17 ||
        esize_offset != 18 || val_offset != 19 ||
        size_offset != 21 || isarr_offset != 23 ||
        base_offset != 24 || proc_offset != 26)
        ++failures;
    if (strcmp(sym[0].name, "OLD") != 0 ||
        sym[0].kind != 5 || sym[0].scope != 7 ||
        sym[0].esize != 2 || sym[0].val != 0 ||
        sym[0].size != 2 || sym[0].isarr != 0 ||
        sym[0].base != 0 || sym[0].proc != 0)
        ++failures;
    if (strcmp(sym[1].name, "NEW-SYMBOL-1234") != 0 ||
        sym[1].name[15] != 0 ||
        sym[1].kind != 2 || sym[1].scope != 3 ||
        sym[1].esize != 2 || sym[1].val != 0 ||
        sym[1].size != 2 || sym[1].isarr != 0 ||
        sym[1].base != 0 || sym[1].proc != 0)
        ++failures;
    if (strcmp(sym[2].name, "THIRD") != 0 ||
        sym[2].kind != 255 || sym[2].scope != 0 ||
        sym[2].esize != 2 || sym[2].val != 0 ||
        sym[2].size != 2 || sym[2].isarr != 0 ||
        sym[2].base != 0 || sym[2].proc != 0 ||
        !record_zero(&sym[3]))
        ++failures;

    printf("ids=%d,%d,%d,%d,%d counts=%d,%d,%d,%d,%d "
           "layout=%d/%d,%d,%d,%d,%d,%d,%d,%d,%d "
           "alloc=%d old=%s/%u/%u new=%s/%u/%u final=%s/%u/%u "
           "zero=%d failures=%d\n",
        first, existing, added, again, final,
        count_after_first, count_after_existing, count_after_added,
        count_after_again, nsym,
        (int)sizeof(struct Sym), stride, kind_offset, scope_offset,
        esize_offset, val_offset, size_offset, isarr_offset,
        base_offset, proc_offset,
        sym != NULL, sym[0].name, (unsigned)sym[0].kind,
        (unsigned)sym[0].scope, sym[1].name,
        (unsigned)sym[1].kind, (unsigned)sym[1].scope,
        sym[2].name, (unsigned)sym[2].kind,
        (unsigned)sym[2].scope, record_zero(&sym[3]), failures);
    free(sym);
    return failures != 0;
}
