/*
 * dcc.c - compiler driver and entry point (the program's main translation unit).
 *
 * Ties the pipeline together: reads the input file, resolves #include
 * directives and splices line directives, runs the active-source filtering
 * pass, parses command-line options (-o/-c/-f/-I/-D/-U/...), and contains
 * main(). The include search path (include_dirs/num_include_dirs, capped by
 * MAX_INCLUDE_DIRS) is kept module-local (static) here.
 *
 * MODULE: its own translation unit, linked with the other dcc_*.c modules;
 * all shared declarations come from the umbrella header dcc.h.
 * Source provenance: monolith src/ddc.c lines 17975-18841.
 */

#include "dcc.h"
#include "dcc_ast.h"

void append_mem(char **outp, long *lenp, long *capp, const char *s, long n);

long file_size(FILE *f)
{
    long n;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}


char *splice_backslash_newlines(char *in, long *lenp, const char *filename)
{
    long i;
    long n;
    long run_start;
    char *out;
    long out_len;
    long out_cap;
    int phys_line;
    int spliced_since_sync;

    n = lenp[0];
    out = NULL;
    out_len = 0;
    out_cap = 0;
    phys_line = 1;
    spliced_since_sync = 0;

    i = 0;
    run_start = 0;
    while (i < n) {
        int splice_len = 0;

        if (in[i] == '\\') {
            if (i + 1 < n && in[i + 1] == '\n')
                splice_len = 2;
            else if (i + 2 < n && in[i + 1] == '\r' && in[i + 2] == '\n')
                splice_len = 3;
        }

        if (splice_len) {
            if (i > run_start)
                append_mem(&out, &out_len, &out_cap, in + run_start, i - run_start);
            i += splice_len;
            run_start = i;
            phys_line++;
            spliced_since_sync++;
            continue;
        }

        if (in[i] == '\n') {
            i++;
            append_mem(&out, &out_len, &out_cap, in + run_start, i - run_start);
            run_start = i;
            phys_line++;
            /*
             * Deleting a backslash-newline pair (translation phase 2) drops a
             * physical line from the character stream without dropping it
             * from the file's line numbering, so every diagnostic for the
             * rest of the file would otherwise read low by the number of
             * continued lines already spliced away.  Resync with the same
             * #line directive mechanism already used for #include splicing
             * (see append_line_directive/preprocess_includes_file) right
             * after the first real (non-spliced) newline that follows a
             * spliced run, so line_no matches the original file again.
             */
            if (spliced_since_sync > 0) {
                char buf[768];
                sprintf(buf, "#line %d \"%s\"\n", phys_line, filename);
                append_mem(&out, &out_len, &out_cap, buf, (long)strlen(buf));
                spliced_since_sync = 0;
            }
            continue;
        }

        i++;
    }
    if (i > run_start)
        append_mem(&out, &out_len, &out_cap, in + run_start, i - run_start);

    if (!out) {
        out = (char *)xmalloc(1);
        out[0] = 0;
    }

    free(in);
    lenp[0] = out_len;
    return out;
}

char *read_file(const char *name, long *lenp)
{
    FILE *f;
    long n;
    char *p;
    char errbuf[640];

    f = fopen(name, "rb");
    if (!f) {
        sprintf(errbuf, "cannot open input: %s", name);
        fatal(errbuf);
    }

    n = file_size(f);
    p = (char *)xmalloc((size_t)n + 1);

    if (fread(p, 1, (size_t)n, f) != (size_t)n)
        fatal("cannot read input");

    fclose(f);
    p[n] = 0;
    lenp[0] = n;

    /*
     * C translation phase 2: delete each backslash-newline pair before
     * preprocessing/tokenization.  This enables continued macro definitions,
     * strings, identifiers, and split operators.
     */
    p = splice_backslash_newlines(p, lenp, name);
    return p;
}

#define MAX_INCLUDE_DEPTH 8

static const char *include_dirs[MAX_INCLUDE_DIRS];
static int num_include_dirs;

void add_include_dir(const char *dir)
{
    if (dir == NULL || dir[0] == 0)
        return;
    if (num_include_dirs >= MAX_INCLUDE_DIRS)
        fatal("too many -I include directories");
    include_dirs[num_include_dirs++] = dir;
}

int file_exists(const char *path)
{
    FILE *f;

    f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
}

void append_mem(char **outp, long *lenp, long *capp,
                       const char *s, long n)
{
    char *p;
    long newcap;

    if (*lenp + n + 1 > *capp) {
        newcap = *capp ? *capp : 4096;
        while (*lenp + n + 1 > newcap)
            newcap *= 2;

        p = (char *)realloc(*outp, (size_t)newcap);
        if (!p) fatal("out of memory");

        *outp = p;
        *capp = newcap;
    }

    memcpy(*outp + *lenp, s, (size_t)n);
    *lenp += n;
    (*outp)[*lenp] = 0;
}

int path_is_absolute(const char *p)
{
    if (!p || !p[0])
        return 0;

    /* Unix, CP/M-ish, and Windows rooted paths. */
    if (p[0] == '/' || p[0] == '\\')
        return 1;

    /* Windows drive-rooted paths, e.g. C:\foo or C:/foo.  A bare "C:foo"
     * is drive-relative, so leave it searchable like an ordinary relative
     * path. */
    if (isalpha((unsigned char)p[0]) && p[1] == ':' &&
        (p[2] == '/' || p[2] == '\\'))
        return 1;

    return 0;
}

const char *path_last_sep(const char *p)
{
    const char *slash;
    const char *backslash;

    if (!p)
        return NULL;

    slash = strrchr(p, '/');
    backslash = strrchr(p, '\\');

    if (!slash)
        return backslash;
    if (!backslash)
        return slash;
    return slash > backslash ? slash : backslash;
}

int path_needs_sep(const char *dir)
{
    int len;

    if (!dir || !dir[0])
        return 0;

    len = (int)strlen(dir);
    return dir[len - 1] != '/' && dir[len - 1] != '\\';
}

void make_include_path(const char *base, const char *inc,
                              char *out, int outsz)
{
    int i;

    /* First try the name as given (current directory or absolute path). */
    strncpy(out, inc, (size_t)outsz - 1);
    out[outsz - 1] = 0;
    if (file_exists(out))
        return;

    /* Then try relative to the directory of the including file, so that
     * headers next to the source file are found without requiring -I.
     * Accept both Unix '/' and DOS/Windows '\\' separators.  ma.bat passes
     * source names like tests\a1.c; looking only for '/' misses that source
     * directory and makes #include "m6502.h" fall back to the current
     * working directory. */
    if (base && !path_is_absolute(inc)) {
        const char *slash = path_last_sep(base);
        if (slash) {
            int dirlen = (int)(slash - base) + 1;
            if (dirlen + (int)strlen(inc) < outsz) {
                strncpy(out, base, (size_t)dirlen);
                out[dirlen] = 0;
                strcat(out, inc);
                if (file_exists(out))
                    return;
            }
        }
    }

    /* Then search each -I directory in command-line order; use the first
     * that contains the header.  Absolute include names are left untouched. */
    if (!path_is_absolute(inc)) {
        for (i = 0; i < num_include_dirs; ++i) {
            const char *dir = include_dirs[i];
            int len = (int)strlen(dir);
            int need_slash = path_needs_sep(dir);

            if (len + (need_slash ? 1 : 0) + (int)strlen(inc) >= outsz)
                continue;

            strcpy(out, dir);
            if (need_slash)
                strcat(out, "/");
            strcat(out, inc);

            if (file_exists(out))
                return;
        }
    }

    /* Not found anywhere: fall back to the bare name so the caller's
     * existing handling (silent drop for <system>, fatal for "user") runs. */
    strncpy(out, inc, (size_t)outsz - 1);
    out[outsz - 1] = 0;
}

int report_include_error(const char *file, int line, const char *msg)
{
    dcc_error_at(file ? file : "<input>", line, -1, msg, NULL);
    return -1;
}

int try_parse_include(const char *line, long n, const char *file, int src_line,
                              char *name, int namesz, int *is_system)
{
    long i;
    int j;
    char endch;

    i = 0;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i >= n || line[i] != '#')
        return 0;

    i++;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i + 7 > n || memcmp(line + i, "include", 7) != 0)
        return 0;

    i += 7;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i >= n || (line[i] != '"' && line[i] != '<'))
        return report_include_error(file, src_line, "expected \"FILENAME\" or <FILENAME>");

    if (line[i] == '<') {
        endch = '>';
        if (is_system) *is_system = 1;
    } else {
        endch = '"';
        if (is_system) *is_system = 0;
    }

    i++;
    j = 0;

    while (i < n && line[i] != endch) {
        if (j + 1 >= namesz)
            return report_include_error(file, src_line, "include name too long");

        name[j++] = line[i++];
    }

    if (i >= n || line[i] != endch)
        return report_include_error(file, src_line, "unterminated include name");

    name[j] = 0;
    return 1;
}

void append_line_directive(char **outp, long *lenp, long *capp,
                                  int line, const char *name)
{
    char buf[768];

    sprintf(buf, "#line %d \"%s\"\n", line, name);
    append_mem(outp, lenp, capp, buf, (long)strlen(buf));
}

char *preprocess_includes_file(const char *name, int depth, long *out_len)
{
    char *raw;
    char *out;
    char incname[256];
    char incpath[512];
    char *incsrc;
    long raw_len;
    long inc_len;
    long out_len2;
    long out_cap;
    long p;
    long line_start;
    long line_end;
    int src_line;

    if (depth > MAX_INCLUDE_DEPTH)
        fatal("too many nested includes");

    raw = read_file(name, &raw_len);

    out = NULL;
    out_len2 = 0;
    out_cap = 0;
    src_line = 1;

    append_line_directive(&out, &out_len2, &out_cap, 1, name);

    p = 0;
    while (p < raw_len) {
        line_start = p;

        while (p < raw_len && raw[p] != '\n')
            p++;

        line_end = p;
        if (p < raw_len && raw[p] == '\n')
            p++;

        {
            int is_system = 0;
            int include_status;
            include_status = try_parse_include(raw + line_start,
                                               line_end - line_start,
                                               name,
                                               src_line,
                                               incname,
                                               sizeof(incname),
                                               &is_system);
            if (include_status > 0) {
                if (is_system) {
                    /* For system includes (<foo.h>), try the local directory
                     * first.  If a local file is found, include it just like a
                     * user include; otherwise silently drop the directive. */
                    make_include_path(name, incname, incpath, sizeof(incpath));
                    {
                        FILE *probe = fopen(incpath, "rb");
                        if (probe) {
                            fclose(probe);
                            incsrc = preprocess_includes_file(incpath, depth + 1, &inc_len);
                            append_mem(&out, &out_len2, &out_cap, incsrc, inc_len);
                            append_mem(&out, &out_len2, &out_cap, "\n", 1);
                            append_line_directive(&out, &out_len2, &out_cap, src_line + 1, name);
                            free(incsrc);
                        } else {
                            append_line_directive(&out, &out_len2, &out_cap, src_line + 1, name);
                        }
                        /* else: not found locally — silently ignore */
                    }
                } else {
                    make_include_path(name, incname, incpath, sizeof(incpath));
                    incsrc = preprocess_includes_file(incpath, depth + 1, &inc_len);
                    append_mem(&out, &out_len2, &out_cap, incsrc, inc_len);
                    append_mem(&out, &out_len2, &out_cap, "\n", 1);
                    append_line_directive(&out, &out_len2, &out_cap, src_line + 1, name);
                    free(incsrc);
                }
            } else if (include_status == 0) {
                append_mem(&out, &out_len2, &out_cap,
                           raw + line_start,
                           p - line_start);
            } else {
                append_mem(&out, &out_len2, &out_cap, "\n", 1);
            }
        } /* end try_parse_include block */
        src_line++;
    } /* end while lines */

    free(raw);

    if (!out) {
        out = (char *)xmalloc(1);
        out[0] = 0;
    }

    out_len[0] = out_len2;
    return out;
}


/* Pre-scan active function-like macro definitions after include expansion.
 * The normal tokenizer also handles #define, but include guards and conditional
 * state in expanded headers can otherwise make a function-like macro such as
 * assert(e) unavailable by the time uses are parsed.  This pass deliberately
 * records only function-like macros, not object-like include guards such as
 * _ASSERT_H, so it does not change later conditional parsing of header bodies.
 */
/* Reduce preprocessor conditionals after include expansion.
 *
 * DCC's lexer-level preprocessor is intentionally small, but handling
 * conditional blocks only while tokenising can lose declarations in included
 * headers when a macro from the same active conditional block is pre-scanned.
 * This pass walks the already-expanded source once, honors #if/#ifdef/#ifndef,
 * #else/#elif/#endif, records active #define/#undef directives, and emits only
 * active non-directive source lines (plus #line directives).  That makes code
 * and macros in an active include block visible consistently: an assert.h block
 * can both define assert(e) and define __assert_fail().
 */
char *filter_active_preprocessor_source(long *lenp)
{
    char *out;
    long out_len;
    long out_cap;
    long p;
    long line_start;
    long line_end;
    int active_stack[MAX_IFSTACK];
    int branch_taken[MAX_IFSTACK];
    int seen_else[MAX_IFSTACK];
    int sp;
    int active;
    int in_asm;
    int logical_line;

    out = NULL;
    out_len = 0;
    out_cap = 0;
    p = 0;
    sp = 0;
    active = 1;
    in_asm = 0;
    logical_line = 1;

    while (p < src_len) {
        const char *s;
        const char *e;
        char word[32];
        int is_directive;
        int next_logical_line;

        line_start = p;
        while (p < src_len && src[p] != '\n')
            p++;
        line_end = p;
        if (p < src_len && src[p] == '\n')
            p++;
        line_no = logical_line;
        next_logical_line = logical_line + 1;

        s = src + line_start;
        e = src + line_end;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;

        is_directive = (s < e && *s == '#');

        /* Inside a #asm block: intercept all lines. */
        if (in_asm) {
            if (is_directive) {
                const char *ss = s + 1;
                char ww[32]; int wwi = 0;
                while (ss < e && (*ss == ' ' || *ss == '\t')) ss++;
                while (ss < e && is_ident_char((unsigned char)*ss) && wwi < 31)
                    ww[wwi++] = *ss++;
                ww[wwi] = 0;
                if (!strcmp(ww, "endasm")) {
                    in_asm = 0;
                    append_mem(&out, &out_len, &out_cap, "\n", 1);
                    goto next_filter_line;
                }
            }
            /* Pass asm content to tokenizer as a pseudo-directive.
             * Use SOH (\001) as separator to preserve all leading whitespace. */
            if (active) {
                static const char pfx[] = "#__asm_line\001";
                append_mem(&out, &out_len, &out_cap, pfx, (long)(sizeof(pfx) - 1));
                append_mem(&out, &out_len, &out_cap, src + line_start, p - line_start);
            } else {
                append_mem(&out, &out_len, &out_cap, "\n", 1);
            }
            goto next_filter_line;
        }

        if (!is_directive) {
            if (active)
                append_mem(&out, &out_len, &out_cap, src + line_start, p - line_start);
            else
                append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        s++;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;

        {
            int wi;
            wi = 0;
            while (s < e && is_ident_char((unsigned char)*s) && wi < (int)sizeof(word) - 1)
                word[wi++] = *s++;
            word[wi] = 0;
        }

        /* '##' at directive position: the token-paste operator is only valid
         * inside a macro replacement list.  Diagnose and skip the line. */
        if (word[0] == 0 && s < e && *s == '#') {
            char filebuf[256];
            int lno;
            source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
            dcc_error_at(filebuf, lno, line_start, "'##' is not a valid preprocessor directive", NULL);
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "line")) {
            if (active) {
                char line_expr[MAX_MACRO_TEXT];
                char expanded[MAX_MACRO_TEXT];
                const char *lp;
                int ei;
                int lno;

                while (s < e && (*s == ' ' || *s == '\t'))
                    s++;
                ei = 0;
                while (s < e && ei < (int)sizeof(line_expr) - 1)
                    line_expr[ei++] = *s++;
                line_expr[ei] = 0;
                macro_expand_argument_text(line_expr, expanded, sizeof(expanded), 0);
                lp = expanded;
                while (*lp && isspace((unsigned char)*lp))
                    lp++;
                lno = 0;
                while (isdigit((unsigned char)*lp))
                    lno = lno * 10 + *lp++ - '0';
                if (lno > 0)
                    next_logical_line = lno;
                append_mem(&out, &out_len, &out_cap, src + line_start, p - line_start);
            }
            goto next_filter_line;
        }

        if (!strcmp(word, "ifdef") || !strcmp(word, "ifndef")) {
            char name[64];
            int ni;
            int cond;
            while (s < e && (*s == ' ' || *s == '\t')) s++;
            ni = 0;
            while (s < e && is_ident_char((unsigned char)*s) && ni < 63)
                name[ni++] = *s++;
            name[ni] = 0;
            if (sp >= MAX_IFSTACK) {
                char filebuf[256];
                int lno;
                source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                dcc_error_at(filebuf, lno, line_start, "too many nested #if", NULL);
                append_mem(&out, &out_len, &out_cap, "\n", 1);
                goto next_filter_line;
            }
            cond = (name[0] && find_define(name) >= 0);
            if (!strcmp(word, "ifndef"))
                cond = !cond;
            active_stack[sp] = active;
            branch_taken[sp] = (active && cond) ? 1 : 0;
            seen_else[sp] = 0;
            active = active && cond;
            sp++;
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "if")) {
            char expr[512];
            int ei;
            int cond;
            while (s < e && (*s == ' ' || *s == '\t')) s++;
            ei = 0;
            while (s < e && ei < (int)sizeof(expr) - 1)
                expr[ei++] = *s++;
            expr[ei] = 0;
            strip_macro_replacement_comments(expr);
            line_no = logical_line;
            cond = pp_eval_simple_expr(expr);
            if (sp >= MAX_IFSTACK) {
                char filebuf[256];
                int lno;
                source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                dcc_error_at(filebuf, lno, line_start, "too many nested #if", NULL);
                append_mem(&out, &out_len, &out_cap, "\n", 1);
                goto next_filter_line;
            }
            active_stack[sp] = active;
            branch_taken[sp] = (active && cond) ? 1 : 0;
            seen_else[sp] = 0;
            active = active && cond;
            sp++;
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "elif")) {
            if (sp > 0) {
                int i;
                int parent;
                int cond;
                char expr[512];
                int ei;
                i = sp - 1;
                parent = active_stack[i];
                if (seen_else[i] || branch_taken[i]) {
                    active = 0;
                } else {
                    while (s < e && (*s == ' ' || *s == '\t')) s++;
                    ei = 0;
                    while (s < e && ei < (int)sizeof(expr) - 1)
                        expr[ei++] = *s++;
                    expr[ei] = 0;
                    strip_macro_replacement_comments(expr);
                        line_no = logical_line;
                    cond = pp_eval_simple_expr(expr);
                    active = parent && cond;
                    if (active)
                        branch_taken[i] = 1;
                }
            } else if (active) {
                char filebuf[256];
                int lno;
                source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                dcc_error_at(filebuf, lno, line_start, "#elif without matching #if", NULL);
            }
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "else")) {
            if (sp > 0) {
                int i;
                int parent;
                i = sp - 1;
                parent = active_stack[i];
                if (!seen_else[i]) {
                    active = parent && !branch_taken[i];
                    branch_taken[i] = 1;
                    seen_else[i] = 1;
                } else {
                    active = 0;
                }
            } else if (active) {
                char filebuf[256];
                int lno;
                source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                dcc_error_at(filebuf, lno, line_start, "#else without matching #if", NULL);
            }
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "endif")) {
            if (sp > 0) {
                sp--;
                active = active_stack[sp];
            } else if (active) {
                char filebuf[256];
                int lno;
                source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                dcc_error_at(filebuf, lno, line_start, "#endif without matching #if", NULL);
            }
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!active) {
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "error")) {
            char msg[256];
            char filebuf[256];
            int lno;
            int mi;

            while (s < e && (*s == ' ' || *s == '\t')) s++;
            mi = 0;
            while (s < e && mi < (int)sizeof(msg) - 1)
                msg[mi++] = *s++;
            while (mi > 0 && (msg[mi - 1] == ' ' || msg[mi - 1] == '\t' || msg[mi - 1] == '\r'))
                mi--;
            msg[mi] = 0;

            source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
            {
                char diag[280];
                sprintf(diag, "#error %s", msg);
                dcc_error_at(filebuf, lno, line_start, diag, NULL);
            }
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "undef")) {
            char name[64];
            int ni;
            while (s < e && (*s == ' ' || *s == '\t')) s++;
            ni = 0;
            while (s < e && is_ident_char((unsigned char)*s) && ni < 63)
                name[ni++] = *s++;
            name[ni] = 0;
            if (name[0])
                remove_define(name);
            /* Keep active #undef in the filtered source so the normal
             * lexer-level preprocessor sees it at the correct source order.
             * The mutation above is only for this filtering pass.
             */
            append_mem(&out, &out_len, &out_cap, src + line_start, p - line_start);
            goto next_filter_line;
        }

        if (!strcmp(word, "define")) {
            char name[64];
            char val[MAX_MACRO_TEXT];
            int ni;
            int vi;

            while (s < e && (*s == ' ' || *s == '\t')) s++;
            ni = 0;
            while (s < e && is_ident_char((unsigned char)*s) && ni < 63)
                name[ni++] = *s++;
            name[ni] = 0;

            if (name[0] && s < e && *s == '(') {
                char params[8][32];
                int nargs;
                int pi;
                memset(params, 0, sizeof(params));
                nargs = 0;
                s++;
                while (s < e && *s != ')') {
                    while (s < e && (*s == ' ' || *s == '\t')) s++;
                    if (s >= e || *s == ')') break;

                    /* C99 variadic marker `...`: nothing else in this loop
                     * consumes a '.', so without this it would spin on it
                     * forever (see the matching fix and comment in
                     * dcc_preproc.c's #define parameter-list parser, which
                     * this one duplicates for the #if/#ifdef pre-scan). */
                    if (s + 2 < e && s[0] == '.' && s[1] == '.' && s[2] == '.') {
                        s += 3;
                        if (nargs < 7) {
                            strcpy(params[nargs], "__VA_ARGS__");
                            nargs++;
                        }
                        while (s < e && (*s == ' ' || *s == '\t')) s++;
                        if (s < e && *s == ',') s++;
                        continue;
                    }

                    pi = 0;
                    while (s < e && is_ident_char((unsigned char)*s) && pi < 31)
                        params[nargs][pi++] = *s++;
                    params[nargs][pi] = 0;
                    if (params[nargs][0] && nargs < 7)
                        nargs++;
                    while (s < e && (*s == ' ' || *s == '\t')) s++;
                    if (s < e && *s == ',') {
                        s++;
                    } else if (s < e && *s != ')') {
                        /* Consume any stray character (notably the '.' of a
                         * C99 '...' variadic parameter list) so this scan
                         * always makes forward progress. Without this the
                         * pass spins forever on '...' because nothing else
                         * advances the cursor. Variadic macros are not
                         * otherwise implemented. */
                        s++;
                    }
                }
                if (s < e && *s == ')') s++;
                while (s < e && (*s == ' ' || *s == '\t')) s++;
                vi = 0;
                while (s < e && vi < (int)sizeof(val) - 1)
                    val[vi++] = *s++;
                while (vi > 0 && (val[vi - 1] == ' ' || val[vi - 1] == '\t' || val[vi - 1] == '\r'))
                    vi--;
                val[vi] = 0;
                add_define_ex(name, val[0] ? val : "1", 1, nargs, params);
            } else if (name[0]) {
                while (s < e && (*s == ' ' || *s == '\t')) s++;
                vi = 0;
                while (s < e && vi < (int)sizeof(val) - 1)
                    val[vi++] = *s++;
                while (vi > 0 && (val[vi - 1] == ' ' || val[vi - 1] == '\t' || val[vi - 1] == '\r'))
                    vi--;
                val[vi] = 0;
                add_define(name, val[0] ? val : "1");
            }
            /* Keep active #define in the filtered source so macro scope is
             * applied in normal C source order.  The add_define above is only
             * for evaluating later conditionals during this filtering pass.
             */
            append_mem(&out, &out_len, &out_cap, src + line_start, p - line_start);
            goto next_filter_line;
        }

        if (!strcmp(word, "asm")) {
            if (active)
                in_asm = 1;
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "endasm")) {
            /* #endasm without matching #asm - ignore */
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "warning")) {
            if (active) {
                char msg[256];
                char filebuf[256];
                int lno;
                int mi;
                while (s < e && (*s == ' ' || *s == '\t')) s++;
                mi = 0;
                while (s < e && mi < (int)sizeof(msg) - 1)
                    msg[mi++] = *s++;
                while (mi > 0 && (msg[mi-1] == ' ' || msg[mi-1] == '\t' || msg[mi-1] == '\r'))
                    mi--;
                msg[mi] = 0;
                source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                fprintf(stderr, "%s:%d: warning: #warning %s\n", filebuf, lno, msg);
            }
            append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        if (!strcmp(word, "pragma")) {
            if (active)
                append_mem(&out, &out_len, &out_cap, src + line_start, p - line_start);
            else
                append_mem(&out, &out_len, &out_cap, "\n", 1);
            goto next_filter_line;
        }

        /* Unknown directives in inactive code are silently dropped.
         * In active code, the null directive is silently ignored; anything
         * else is a hard error. */
        if (active && word[0] != 0) {
            char filebuf[256];
            int lno;
            char msg[96];
            source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
            sprintf(msg, "unknown preprocessor directive '#%s'", word);
            dcc_error_at(filebuf, lno, line_start, msg, NULL);
        }
        append_mem(&out, &out_len, &out_cap, "\n", 1);

next_filter_line:
        logical_line = next_logical_line;
    }

    if (!out) {
        out = (char *)xmalloc(1);
        out[0] = 0;
    }

    lenp[0] = out_len;
    return out;
}

void usage(void);

int is_macro_name_text(const char *s, int n)
{
    int i;

    if (n <= 0)
        return 0;

    if (!is_ident_start((unsigned char)s[0]))
        return 0;

    for (i = 1; i < n; ++i) {
        if (!is_ident_char((unsigned char)s[i]))
            return 0;
    }

    return 1;
}

void add_cmdline_define(const char *arg)
{
    char name[64];
    char value[MAX_MACRO_TEXT];
    const char *eqp;
    int namelen;

    if (!arg || !arg[0])
        usage();

    eqp = strchr(arg, '=');
    if (eqp) {
        namelen = (int)(eqp - arg);
    } else {
        namelen = (int)strlen(arg);
    }

    if (namelen <= 0 || namelen >= (int)sizeof(name))
        usage();

    if (!is_macro_name_text(arg, namelen))
        usage();

    memcpy(name, arg, (size_t)namelen);
    name[namelen] = 0;

    if (eqp) {
        strncpy(value, eqp + 1, sizeof(value) - 1);
        value[sizeof(value) - 1] = 0;
    } else {
        strcpy(value, "1");
    }

    add_define(name, value);
}

#define DCC_VERSION "dcc (DCC C89->Z80 compiler) 1.0"

void print_version(void)
{
    printf("%s\n", DCC_VERSION);
}

void usage(void)
{
    fprintf(stderr, "usage: dcc [-c|-module] [-f|-ffloatio] [-fl|-flongio] [-v] [-s|-stack bytes] [-Idir] [-Dname[=value]] [-Uname] input.c -o output.mac\n");
    exit(1);
}

void print_help(void)
{
    printf("%s\n", DCC_VERSION);
    printf("usage: dcc [options] input.c -o output.mac\n");
    printf("\n");
    printf("options:\n");
    printf("  -o <file>        write M80 assembly to <file> ('-' for stdout)\n");
    printf("  -c, -module      emit a linkable helper module (not a final program)\n");
    printf("  -f, -ffloatio    enable %%f formatting for printf\n");
    printf("  -fl, -flongio    enable long printf formats (%%ld/%%lu/%%lx/%%lX/%%ls)\n");
    printf("  -s, -stack <bytes>   reserve <bytes> for the C stack (default 512)\n");
    printf("  -fstack-check    abort gracefully if the stack overflows its reserve\n");
    printf("  -I<dir>          add <dir> to the include search path\n");
    printf("  -D<name>[=val]   define a preprocessor macro\n");
    printf("  -U<name>         undefine a preprocessor macro\n");
    printf("  -v, --version    print version and exit\n");
    printf("  -h, --help       print this help and exit\n");
    exit(0);
}

int main(int argc, char **argv)
{
    int i;

    input_name = NULL;
    output_name = NULL;
    opt_module = 0;
    opt_stack_size = 512;
    opt_stack_check = 0;
    max_function_local_bytes = 0;

    add_define("_DCC_", "1");
    ast_build_init();

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-ffloatio") || !strcmp(argv[i], "-f")) {
            opt_floatio = 1;
        } else if (!strcmp(argv[i], "-flongio") || !strcmp(argv[i], "-fl")) {
            opt_longio = 1;
        } else if (!strcmp(argv[i], "-fstack-check")) {
            opt_stack_check = 1;
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            print_version();
            return 0;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            print_help();
        } else if (!strcmp(argv[i], "-c") || !strcmp(argv[i], "-module")) {
            opt_module = 1;
        } else if (!strcmp(argv[i], "-stack") || !strcmp(argv[i], "--stack") || !strcmp(argv[i], "-s")) {
            char *endp;
            long v;
            if (++i >= argc) usage();
            v = strtol(argv[i], &endp, 0);
            if (*endp != 0 || v < 0 || v > 32767)
                usage();
            opt_stack_size = (int)v;
        } else if (!strncmp(argv[i], "-stack=", 7)) {
            char *endp;
            long v;
            v = strtol(argv[i] + 7, &endp, 0);
            if (*endp != 0 || v < 0 || v > 32767)
                usage();
            opt_stack_size = (int)v;
        } else if (!strncmp(argv[i], "--stack=", 8)) {
            char *endp;
            long v;
            v = strtol(argv[i] + 8, &endp, 0);
            if (*endp != 0 || v < 0 || v > 32767)
                usage();
            opt_stack_size = (int)v;
        } else if (!strncmp(argv[i], "-s=", 3)) {
            char *endp;
            long v;
            v = strtol(argv[i] + 3, &endp, 0);
            if (*endp != 0 || v < 0 || v > 32767)
                usage();
            opt_stack_size = (int)v;
        } else if (!strcmp(argv[i], "-D")) {
            if (++i >= argc) usage();
            add_cmdline_define(argv[i]);
        } else if (!strncmp(argv[i], "-D", 2)) {
            add_cmdline_define(argv[i] + 2);
        } else if (!strcmp(argv[i], "-U")) {
            if (++i >= argc) usage();
            remove_define(argv[i]);
        } else if (!strncmp(argv[i], "-U", 2)) {
            remove_define(argv[i] + 2);
        } else if (!strcmp(argv[i], "-I")) {
            if (++i >= argc) usage();
            add_include_dir(argv[i]);
        } else if (!strncmp(argv[i], "-I", 2)) {
            add_include_dir(argv[i] + 2);
        } else if (!strcmp(argv[i], "-o")) {
            if (++i >= argc) usage();
            output_name = argv[i];
        } else if (argv[i][0] == '-') {
            /* ignored for now */
        } else {
            input_name = argv[i];
        }
    }

    if (!input_name) usage();
    if (!output_name) output_name = "out.mac";

    init_predefined_macro_texts();

    strncpy(current_file_name, input_name, sizeof(current_file_name) - 1);
    current_file_name[sizeof(current_file_name) - 1] = 0;

    src = preprocess_includes_file(input_name, 0, &src_len);
    {
        char *filtered_src;
        long filtered_len;
        int saved_ndefs;
        struct Def *saved_defs;

        /* filter_active_preprocessor_source() uses the define table to
         * evaluate #if/#ifdef while reducing inactive source.  Do not let
         * definitions discovered later in the file leak into the real parse
         * before their source-order #define is reached; otherwise a later
         * '#define n 20' can rewrite 'int n' in an earlier included header.
         *
         * Keep this snapshot off the C host stack.  struct Def can be large
         * when macro replacement buffers are widened, and putting
         * MAX_DEFINES copies on the stack can overflow MSVC's default stack
         * before main() really starts.
         */
        saved_defs = (struct Def *)xmalloc(sizeof(defs));
        saved_ndefs = ndefs;
        memcpy(saved_defs, defs, sizeof(defs));

        filtered_src = filter_active_preprocessor_source(&filtered_len);

        ndefs = saved_ndefs;
        memcpy(defs, saved_defs, sizeof(defs));
        free(saved_defs);

        free(src);
        src = filtered_src;
        src_len = filtered_len;
    }
    /* Function-like macros are now left in the filtered source and processed
     * by the normal lexer-level preprocessor in source order.  Do not pre-scan
     * them globally; that breaks C macro scoping/order.
     */
    posi = 0;
    tok_start_pos = 0;
    line_no = 1;
    tok_line = 1;

    if (!strcmp(output_name, "-")) {
        outf = stdout;
    } else {
        outf = fopen(output_name, "w");
        if (!outf) fatal("cannot open output");
    }

    add_typedef_name("FILE", TYPE_INT, 0);

    /* stdout/stderr/stdin are runtime data objects, not functions.
     * They are predeclared lazily in parse_translation_unit() as
     * SC_EXTERN so emit_load_sym_addr() emits EXTRN when they are
     * actually referenced.  Do not pre-add them here as SC_FUNC,
     * or add_global() preserves the wrong storage class and M80 sees
     * ld hl,_stdout without a preceding EXTRN. */
    add_define("NULL", "0");
    if (find_define("EOF") < 0)
        add_define("EOF", "-1");

    parse_translation_unit();
    emit_needed_inline_bodies();
    emit_data();
    emit_deferred_extrns();
    emit("\n\tend\n");

    if (outf != stdout)
        fclose(outf);

    if (errors) {
        fprintf(stderr, "dcc: %d error(s)\n", errors);
        return 1;
    }

    return 0;
}
