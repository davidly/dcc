/* peep_analyze.c - shared register and function-bound safety analysis. */
#include "dccpeep_internal.h"

#define MAX_LOCAL_FUNC_LABELS 8192
static char local_func_labels[MAX_LOCAL_FUNC_LABELS][128];
static int n_local_func_labels;

int line_clobbers_bc(const char *line)
{
    char clean[MAX_LINE];
    const char *p;

    strip_peep_comment_lower_copy(clean, line);

    if ((strncmp(clean, "rst", 3) == 0 &&
         (clean[3] == ' ' || clean[3] == '\t')) ||
        strncmp(clean, "djnz", 4) == 0 || strcmp(clean, "exx") == 0 ||
        strcmp(clean, "ldi") == 0 || strcmp(clean, "ldd") == 0 ||
        strcmp(clean, "cpi") == 0 || strcmp(clean, "cpd") == 0 ||
        strcmp(clean, "ini") == 0 || strcmp(clean, "ind") == 0 ||
        strcmp(clean, "outi") == 0 || strcmp(clean, "outd") == 0 ||
        strcmp(clean, "ldir") == 0 || strcmp(clean, "lddr") == 0 ||
        strcmp(clean, "cpir") == 0 || strcmp(clean, "cpdr") == 0 ||
        strcmp(clean, "inir") == 0 || strcmp(clean, "indr") == 0 ||
        strcmp(clean, "otir") == 0 || strcmp(clean, "otdr") == 0)
        return 1;

    if (strncmp(clean, "call", 4) == 0 &&
        (clean[4] == ' ' || clean[4] == '\t') &&
        strcmp(clean, "call __stchk") != 0)
        return 1;

    /* These use the carry flag, not register C. Conditional calls are not
     * exempt: when taken, they are ordinary ABI-clobbering calls. */
    if (!strncmp(clean, "jp c,", 5) || !strncmp(clean, "jr c,", 5) ||
        !strcmp(clean, "ret c"))
        return 0;

    p = clean;
    while (*p) {
        if (isalnum((unsigned char)*p) || *p == '_') {
            const char *start = p;
            int n = 0;
            while (*p && (isalnum((unsigned char)*p) || *p == '_')) { p++; n++; }
            if (n == 1 && (*start == 'b' || *start == 'c'))
                return 1;
            if (n == 2 && start[0] == 'b' && start[1] == 'c')
                return 1;
        } else {
            p++;
        }
    }
    return 0;
}

int line_could_use_bc(const char *line)
{
    char clean[MAX_LINE];

    if (line_clobbers_bc(line))
        return 1;

    strip_peep_comment_copy(clean, line);
    return strncmp(clean, "push ", 5) == 0 || strncmp(clean, "pop ", 4) == 0;
}

void find_function_bounds(int from, int *func_start, int *func_end)
{
    peep_indexed_function_bounds(from, 0, func_start, func_end);
}

void find_function_bounds_any(int from, int *func_start, int *func_end)
{
    peep_indexed_function_bounds(from, 1, func_start, func_end);
}

void scan_local_func_labels(void)
{
    int i;
    char name[128];
    int n;

    n_local_func_labels = 0;
    for (i = 0; i + 1 < nlines; ++i) {
        /* "; static function " is 18 characters, not 19 - an off-by-one
         * here meant this branch never matched (strncmp saw the real
         * function name's first character where the literal's implicit
         * NUL was, at n=19), so scan_local_func_labels only ever recorded
         * genuinely `public` functions, never `static` ones. That silently
         * defeated the whole cross-function IY-collision check for calls
         * between static functions - confirmed as the root cause of
         * tests/too.c's corrupted output under -fundocumented-z80:
         * gallery_init (static) calls hall_init (static) calls
         * exhibit_init (static), and all three independently claimed IYL
         * for their own loop, each stomping the others' live value. */
        if (strncmp(lines[i], "public ", 7) != 0 &&
            strncmp(lines[i], "; static function ", 18) != 0)
            continue;
        if (!starts_label(lines[i + 1]))
            continue;

        strncpy(name, lines[i + 1], sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        n = (int)strlen(name);
        if (n > 0 && name[n - 1] == ':')
            name[n - 1] = 0;

        if (n_local_func_labels < MAX_LOCAL_FUNC_LABELS)
            strcpy(local_func_labels[n_local_func_labels++], name);
    }
}

int is_local_func_label(const char *name)
{
    int i;
    for (i = 0; i < n_local_func_labels; ++i)
        if (!strcmp(local_func_labels[i], name))
            return 1;
    return 0;
}

int line_touches_reg_pair(const char *s, const char *lo, const char *hi,
                                 const char *pair)
{
    static const char *implicit_pair_mnemonics[] = {
        "djnz ", "ldir", "lddr", "cpir", "cpdr",
        "otir", "otdr", "inir", "indr",
        "ldi", "ldd", "cpi", "cpd", "ini", "ind", "outi", "outd",
        NULL
    };
    const char *p;
    char tok[16];
    char paren[8];
    int ti;
    int i;

    for (i = 0; implicit_pair_mnemonics[i] != NULL; ++i)
        if (strncmp(s, implicit_pair_mnemonics[i], strlen(implicit_pair_mnemonics[i])) == 0)
            return 1;

    sprintf(paren, "(%s)", pair);
    if (strstr(s, paren) != NULL)
        return 1;

    p = s;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            ti = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && ti < 15)
                tok[ti++] = *p++;
            tok[ti] = 0;
            if (strcmp(tok, lo) == 0 || strcmp(tok, hi) == 0 || strcmp(tok, pair) == 0)
                return 1;
        } else {
            p++;
        }
    }
    return 0;
}

int line_touches_bc(const char *s)
{
    return line_touches_reg_pair(s, "b", "c", "bc");
}

int line_touches_de(const char *s)
{
    return line_touches_reg_pair(s, "d", "e", "de");
}

int line_touches_hl(const char *s)
{
    return line_touches_reg_pair(s, "l", "h", "hl");
}

int line_touches_a(const char *s)
{
    char tmp[MAX_LINE];
    const char *p;
    char tok[16];
    int ti;

    strip_peep_comment_copy(tmp, s);
    p = tmp;
    while (*p) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            ti = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && ti < 15)
                tok[ti++] = *p++;
            tok[ti] = 0;
            if (strcmp(tok, "a") == 0 || strcmp(tok, "af") == 0)
                return 1;
        } else {
            p++;
        }
    }
    return 0;
}

