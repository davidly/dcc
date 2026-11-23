/**
 * @file dcc.c
 * @brief Drives one source translation from command line to M80 assembly.
 *
 * @par Role
 * Parses compiler options, reads and splices source files, expands recursive
 * includes, filters inactive preprocessing branches, initializes compilation
 * state, and sequences translation-unit parsing, MIR finalization, data
 * emission, and deferred extern emission.
 *
 * @par Key entry points
 * main(), preprocess_includes_file(), filter_active_preprocessor_source(), and
 * splice_backslash_newlines().
 *
 * @par Boundary
 * dcc_preproc.c owns token-level preprocessing and lexing; frontend modules
 * own parsing; selected MIR candidates are the only source of production
 * function bodies. This file orchestrates those stages rather than lowering
 * function bodies itself.
 */

/*
 * realpath() is a POSIX extension: under strict ISO C mode, glibc/libc headers
 * don't declare it in <stdlib.h> unless a feature-test macro asks for it
 * (_POSIX_C_SOURCE 200809L alone isn't enough on this glibc; _DEFAULT_SOURCE
 * is). Without a prototype in scope, C89's implicit-int rule assumes
 * realpath returns int, silently truncating/corrupting the real pointer it
 * returns - undefined behavior that happened not to crash under gcc's luck
 * but is a real SEGV under clang. Must be defined before any system header
 * is first included in this translation unit.
 */
#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif

#include "dcc.h"
#include "dcc_mir.h"
#include "dcc_preproc_internal.h"
#include "dcc_ast.h"
#include "dcc_version.h"

#ifdef _WIN32
#include <direct.h>
#endif

void append_mem(char **outp, long *lenp, long *capp, const char *s, long n);


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
    int line_begin;
    int in_directive;
    int in_line_comment;
    char quote;

    n = lenp[0];
    out = NULL;
    out_len = 0;
    out_cap = 0;
    phys_line = 1;
    spliced_since_sync = 0;
    line_begin = 1;
    in_directive = 0;
    in_line_comment = 0;
    quote = 0;

    i = 0;
    run_start = 0;
    while (i < n) {
        long splice_len = 0;

        if (in[i] == '\\') {
            /* gcc and clang also continue a line whose backslash is
             * followed only by blanks, usually an accident of an editor. */
            long k = i + 1;
            while (k < n && (in[k] == ' ' || in[k] == '\t'))
                k++;
            if (k < n && in[k] == '\n')
                splice_len = k + 1 - i;
            else if (k + 1 < n && in[k] == '\r' && in[k + 1] == '\n')
                splice_len = k + 2 - i;
            if (splice_len && k > i + 1)
                warn_at(filename, phys_line, "backslash and newline separated by space");
        }

        /* A directive ends at its line's end, so a block comment that
         * starts on a directive line and runs over later lines is replaced
         * here by one space, as translation phase 3 does, keeping the line
         * numbering with the same #line resync as a splice. */
        if (!splice_len && in_directive && !quote && !in_line_comment &&
            in[i] == '/' && i + 1 < n && in[i + 1] == '*') {
            long k = i + 2;
            int lines = 0;
            while (k + 1 < n && !(in[k] == '*' && in[k + 1] == '/')) {
                if (in[k] == '\n')
                    lines++;
                k++;
            }
            if (k + 1 < n && lines > 0) {
                if (i > run_start)
                    append_mem(&out, &out_len, &out_cap, in + run_start, i - run_start);
                append_mem(&out, &out_len, &out_cap, " ", 1);
                i = k + 2;
                run_start = i;
                phys_line += lines;
                spliced_since_sync += lines;
                continue;
            }
            if (k + 1 < n) {
                i = k + 2;
                continue;
            }
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
            line_begin = 1;
            in_directive = 0;
            in_line_comment = 0;
            quote = 0;
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

        if (line_begin && in[i] == '#')
            in_directive = 1;
        if (in[i] != ' ' && in[i] != '\t' && in[i] != '\r' && in[i] != '\f')
            line_begin = 0;
        if (in_directive && !in_line_comment) {
            if (quote) {
                if (in[i] == '\\' && i + 1 < n && in[i + 1] != '\n') {
                    i += 2;
                    continue;
                }
                if (in[i] == quote)
                    quote = 0;
            } else if (in[i] == '"' || in[i] == '\'') {
                quote = in[i];
            } else if (in[i] == '/' && i + 1 < n && in[i + 1] == '/') {
                in_line_comment = 1;
            }
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

/* Bytes an editor may leave that are not C: drop a UTF-8 byte-order mark,
 * end a line at a lone CR (classic Mac line endings) and treat NUL as a
 * space, as clang and gcc do, rather than compiling a truncated file. */
static void normalize_source_bytes(char *p, long *lenp, const char *name)
{
    long n = lenp[0];
    long i;
    long o = 0;
    int line = 1;
    int warned_nul = 0;

    i = 0;
    if (n >= 3 && (unsigned char)p[0] == 0xef && (unsigned char)p[1] == 0xbb &&
        (unsigned char)p[2] == 0xbf)
        i = 3;
    for (; i < n; ++i) {
        char c = p[i];
        if (c == '\r' && (i + 1 >= n || p[i + 1] != '\n'))
            c = '\n';
        if (c == 0) {
            if (!warned_nul) {
                warn_at(name, line, "null character in source treated as a space");
                warned_nul = 1;
            }
            c = ' ';
        }
        if (c == '\n')
            line++;
        p[o++] = c;
    }
    p[o] = 0;
    lenp[0] = o;
}

char *read_file(const char *name, long *lenp)
{
    FILE *f;
    long n;
    char *p;
    char errbuf[640];

    f = fopen(name, "rb");
    if (f) {
        (void)getc(f);
        if (ferror(f)) {
            fclose(f);
            f = NULL;
        }
    }
    if (!f) {
        sprintf(errbuf, "cannot open input: %s", name);
        fatal(errbuf);
    }

    p = dcc_read_stream_text(f, &n, "cannot read input");

    fclose(f);
    normalize_source_bytes(p, &n, name);
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
#define MAX_PRAGMA_ONCE_FILES 256

static const char *include_dirs[MAX_INCLUDE_DIRS];
static int num_include_dirs;
static char pragma_once_files[MAX_PRAGMA_ONCE_FILES][512];
static int num_pragma_once_files;

void add_include_dir(const char *dir)
{
    if (dir == NULL || dir[0] == 0)
        return;
    if (num_include_dirs >= MAX_INCLUDE_DIRS)
        fatal("too many -I include directories");
    include_dirs[num_include_dirs++] = dir;
}

/* Whether path names a file that can be read: fopen() succeeds on a
 * directory on some hosts, and reading it then fails. */
static int include_file_readable(const char *path)
{
    FILE *f = fopen(path, "rb");
    int ok;

    if (!f)
        return 0;
    (void)getc(f);
    ok = !ferror(f);
    fclose(f);
    return ok;
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

void canonical_include_path(const char *name, char *out, int outsz)
{
    char *p;

    /*
     * Let the platform routine allocate the buffer: realpath() may write up to
     * PATH_MAX bytes (4096 on Linux), so a fixed on-stack buffer risks an
     * overflow.  Both realpath(x, NULL) (POSIX.1-2008 / glibc / macOS) and
     * _fullpath(NULL, x, 0) (MSVC) malloc a right-sized result for us.
     */
#ifdef _WIN32
    p = _fullpath(NULL, name, 0);
#else
    p = realpath(name, NULL);
#endif

    if (!p) {
        /* Not resolvable (e.g. file does not exist): fall back to the raw
         * name; read_file() will report the real error later. */
        if ((int)strlen(name) >= outsz)
            fatal("include path too long");
        strcpy(out, name);
        return;
    }

    if ((int)strlen(p) >= outsz) {
        free(p);
        fatal("include path too long");
    }

    strcpy(out, p);
    free(p);
}

int find_pragma_once_file(const char *path)
{
    int i;

    for (i = 0; i < num_pragma_once_files; ++i) {
        if (!strcmp(pragma_once_files[i], path))
            return i;
    }
    return -1;
}

void mark_pragma_once_file(const char *path)
{
    if (find_pragma_once_file(path) >= 0)
        return;

    if (num_pragma_once_files >= MAX_PRAGMA_ONCE_FILES)
        fatal("too many #pragma once files");

    strcpy(pragma_once_files[num_pragma_once_files++], path);
}

/* Track #if/#ifdef/#ifndef/#elif/#else/#endif nesting during include splicing so
 * that `#pragma once` (and recursion into #include) can be gated on the active
 * region.  Returns 1 if the line is a conditional directive - the caller still
 * emits its verbatim text so the later active-source filter pass sees it - or 0
 * otherwise. */
int include_cond_update(const char *line, long n,
                        int *astk, int *btk, int *selse,
                        int *spp, int *activep)
{
    const char *s = line;
    const char *e = line + n;
    char word[16];
    int wi;
    int sp = *spp;
    int active = *activep;

    while (s < e && (*s == ' ' || *s == '\t'))
        s++;
    if (s >= e || *s != '#')
        return 0;
    s++;
    while (s < e && (*s == ' ' || *s == '\t'))
        s++;
    wi = 0;
    while (s < e && is_ident_char((unsigned char)*s) && wi < (int)sizeof(word) - 1)
        word[wi++] = *s++;
    word[wi] = 0;

    if (!strcmp(word, "ifdef") || !strcmp(word, "ifndef")) {
        char name[64];
        int ni;
        int cond;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;
        ni = 0;
        while (s < e && is_ident_char((unsigned char)*s) && ni < (int)sizeof(name) - 1)
            name[ni++] = *s++;
        name[ni] = 0;
        cond = (name[0] && find_define(name) >= 0);
        if (!strcmp(word, "ifndef"))
            cond = !cond;
        if (sp < MAX_IFSTACK) {
            astk[sp] = active;
            btk[sp] = (active && cond) ? 1 : 0;
            selse[sp] = 0;
            active = active && cond;
            sp++;
        }
    } else if (!strcmp(word, "if")) {
        char expr[512];
        int ei = 0;
        int cond;
        while (s < e && ei < (int)sizeof(expr) - 1)
            expr[ei++] = *s++;
        expr[ei] = 0;
        strip_macro_replacement_comments(expr);
        cond = active && pp_eval_simple_expr(expr);
        if (sp < MAX_IFSTACK) {
            astk[sp] = active;
            btk[sp] = (active && cond) ? 1 : 0;
            selse[sp] = 0;
            active = active && cond;
            sp++;
        }
    } else if (!strcmp(word, "elif")) {
        if (sp > 0) {
            int i = sp - 1;
            int parent = astk[i];
            if (selse[i] || btk[i]) {
                active = 0;
            } else {
                char expr[512];
                int ei = 0;
                int cond;
                while (s < e && ei < (int)sizeof(expr) - 1)
                    expr[ei++] = *s++;
                expr[ei] = 0;
                strip_macro_replacement_comments(expr);
                cond = parent && pp_eval_simple_expr(expr);
                active = parent && cond;
                if (active)
                    btk[i] = 1;
            }
        }
    } else if (!strcmp(word, "else")) {
        if (sp > 0) {
            int i = sp - 1;
            int parent = astk[i];
            if (!selse[i]) {
                active = parent && !btk[i];
                btk[i] = 1;
                selse[i] = 1;
            } else {
                active = 0;
            }
        }
    } else if (!strcmp(word, "endif")) {
        if (sp > 0) {
            sp--;
            active = astk[sp];
        }
    } else {
        return 0;
    }

    *spp = sp;
    *activep = active;
    return 1;
}

int include_scan_macro_directive(const char *line, long n, int active)
{
    const char *s = line;
    const char *e = line + n;
    char word[16];
    int wi;

    while (s < e && (*s == ' ' || *s == '\t'))
        s++;
    if (s >= e || *s != '#')
        return 0;
    s++;
    while (s < e && (*s == ' ' || *s == '\t'))
        s++;

    wi = 0;
    while (s < e && is_ident_char((unsigned char)*s) && wi < (int)sizeof(word) - 1)
        word[wi++] = *s++;
    word[wi] = 0;

    if (!strcmp(word, "undef")) {
        char name[64];
        int ni;

        if (!active)
            return 1;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;
        ni = 0;
        while (s < e && is_ident_char((unsigned char)*s) && ni < (int)sizeof(name) - 1)
            name[ni++] = *s++;
        name[ni] = 0;
        if (name[0])
            remove_define(name);
        return 1;
    }

    if (!strcmp(word, "define")) {
        char name[64];
        char val[MAX_MACRO_TEXT];
        char params[8][32];
        int nargs;
        int ni;
        int vi;

        if (!active)
            return 1;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;
        ni = 0;
        while (s < e && is_ident_char((unsigned char)*s) && ni < (int)sizeof(name) - 1)
            name[ni++] = *s++;
        name[ni] = 0;
        if (!name[0])
            return 1;

        memset(params, 0, sizeof(params));
        nargs = 0;
        if (s < e && *s == '(') {
            s++;
            while (s < e && *s != ')') {
                char pname[32];
                int pi;
                while (s < e && (*s == ' ' || *s == '\t'))
                    s++;
                if (s >= e || *s == ')')
                    break;

                if (s + 2 < e && s[0] == '.' && s[1] == '.' && s[2] == '.') {
                    s += 3;
                    if (nargs < 8) {
                        strcpy(params[nargs], "__VA_ARGS__");
                        nargs++;
                    }
                    while (s < e && (*s == ' ' || *s == '\t'))
                        s++;
                    if (s < e && *s == ',')
                        s++;
                    continue;
                }

                pi = 0;
                while (s < e && is_ident_char((unsigned char)*s) && pi < 31)
                    pname[pi++] = *s++;
                pname[pi] = 0;
                if (pname[0] && nargs < 8) {
                    strcpy(params[nargs], pname);
                    nargs++;
                }
                while (s < e && (*s == ' ' || *s == '\t'))
                    s++;
                if (s < e && *s == ',') {
                    s++;
                } else if (s < e && *s != ')') {
                    s++;
                }
            }
            if (s < e && *s == ')')
                s++;
            while (s < e && (*s == ' ' || *s == '\t'))
                s++;
            vi = 0;
            while (s < e && vi < (int)sizeof(val) - 1)
                val[vi++] = *s++;
            while (vi > 0 && (val[vi - 1] == ' ' || val[vi - 1] == '\t' || val[vi - 1] == '\r'))
                vi--;
            val[vi] = 0;
            strip_macro_replacement_comments(val);
            add_define_ex(name, val, 1, nargs, params);
        } else {
            while (s < e && (*s == ' ' || *s == '\t'))
                s++;
            vi = 0;
            while (s < e && vi < (int)sizeof(val) - 1)
                val[vi++] = *s++;
            while (vi > 0 && (val[vi - 1] == ' ' || val[vi - 1] == '\t' || val[vi - 1] == '\r'))
                vi--;
            val[vi] = 0;
            strip_macro_replacement_comments(val);
            add_define(name, val);
        }
        return 1;
    }

    return 0;
}

int try_parse_pragma_once(const char *line, long n)
{
    long i;

    i = 0;
    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i >= n || line[i] != '#')
        return 0;
    i++;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i + 6 > n || memcmp(line + i, "pragma", 6) != 0)
        return 0;
    i += 6;

    if (i >= n || (line[i] != ' ' && line[i] != '\t'))
        return 0;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i + 4 > n || memcmp(line + i, "once", 4) != 0)
        return 0;
    i += 4;

    while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r'))
        i++;

    if (i == n)
        return 1;

    /* Tolerate a trailing comment after the directive (line or block form). */
    if (i + 1 < n && line[i] == '/' && (line[i + 1] == '/' || line[i + 1] == '*'))
        return 1;

    return 0;
}

/* Set by try_parse_include when the name is followed by more than blanks
 * and comments, for the caller to warn about in active code. */
static int include_extra_tokens;
static const char *pp_skip_blank(const char *s, const char *e);

int try_parse_include(const char *line, long n, const char *file, int src_line,
                              char *name, int namesz, int *is_system, int active)
{
    long i;
    int j;
    char endch;
    const char *text;
    long tn;
    char *expanded = NULL;
    int status;

    i = 0;
    include_extra_tokens = 0;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i >= n || line[i] != '#')
        return 0;

    i++;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    if (i + 7 > n || memcmp(line + i, "include", 7) != 0 ||
        (i + 7 < n && is_ident_char((unsigned char)line[i + 7])))
        return 0;

    /* Discard the directive without expanding an inactive operand. */
    if (!active)
        return -1;

    i += 7;

    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;

    text = line + i;
    tn = n - i;
    /* `#include NAME`: the macro's expansion must be "file" or <file>. */
    if (i < n && is_ident_start((unsigned char)line[i])) {
        char *raw = (char *)xmalloc((size_t)tn + 1);
        memcpy(raw, line + i, (size_t)tn);
        raw[tn] = 0;
        strip_macro_replacement_comments(raw);
        expanded = (char *)xmalloc(MAX_MACRO_TEXT);
        macro_expand_argument_text(raw, expanded, MAX_MACRO_TEXT, 0);
        free(raw);
        text = expanded;
        while (*text == ' ' || *text == '\t')
            text++;
        tn = (long)strlen(text);
    }

    i = 0;
    if (i >= tn || (text[i] != '"' && text[i] != '<')) {
        status = report_include_error(file, src_line, "expected \"FILENAME\" or <FILENAME>");
        goto done;
    }

    if (text[i] == '<') {
        endch = '>';
        if (is_system) *is_system = 1;
    } else {
        endch = '"';
        if (is_system) *is_system = 0;
    }

    i++;
    j = 0;

    while (i < tn && text[i] != endch && text[i] != '\r') {
        if (j + 1 >= namesz) {
            status = report_include_error(file, src_line, "include name too long");
            goto done;
        }

        name[j++] = text[i++];
    }

    if (i >= tn || text[i] != endch) {
        status = report_include_error(file, src_line, "unterminated include name");
        goto done;
    }

    name[j] = 0;
    /* `<>` would name the include directory itself. */
    while (j > 0 && (name[j - 1] == ' ' || name[j - 1] == '\t'))
        --j;
    if (j == 0) {
        status = report_include_error(file, src_line, "expected \"FILENAME\" or <FILENAME>");
        goto done;
    }
    include_extra_tokens = pp_skip_blank(text + i + 1, text + tn) < text + tn;
    status = 1;
done:
    free(expanded);
    return status;
}

void append_line_directive(char **outp, long *lenp, long *capp,
                                  int line, const char *name)
{
    char buf[768];

    sprintf(buf, "#line %d \"%s\"\n", line, name);
    append_mem(outp, lenp, capp, buf, (long)strlen(buf));
}

/* preprocess_includes_file's #include/#pragma once/#if splicing pass runs
 * before real tokenization, on raw lines, and each of its detector functions
 * (include_cond_update, include_scan_macro_directive, try_parse_pragma_once,
 * try_parse_include) just checks whether a line starts with '#' - they have
 * no notion of still being inside a slash-star block comment opened on an
 * earlier line, so e.g. a comment that happens to mention "#include" at the
 * start of one of its lines was misparsed as a real directive and could
 * fail with a bogus "expected FILENAME" error.
 *
 * This scans one line, carrying *in_comment across calls (one call per
 * line, in order), and returns the state the line STARTED in so the caller
 * can skip the directive checks for a line that opens inside an
 * already-open comment. Skips over string/char literals so a comment
 * delimiter inside one (e.g. in a printf format string) doesn't corrupt the
 * tracked state; a line (double-slash) comment is confined to its own line
 * and never affects this multi-line state either way. */
int line_starts_inside_comment(const char *line, long n, int *in_comment)
{
    long i = 0;
    int was_in_comment = *in_comment;

    while (i < n) {
        if (*in_comment) {
            if (line[i] == '*' && i + 1 < n && line[i + 1] == '/') {
                *in_comment = 0;
                i += 2;
            } else {
                i++;
            }
            continue;
        }
        if (line[i] == '/' && i + 1 < n && line[i + 1] == '*') {
            *in_comment = 1;
            i += 2;
            continue;
        }
        if (line[i] == '/' && i + 1 < n && line[i + 1] == '/')
            break;
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            i++;
            while (i < n && line[i] != quote) {
                if (line[i] == '\\' && i + 1 < n)
                    i += 2;
                else
                    i++;
            }
            if (i < n)
                i++;
            continue;
        }
        i++;
    }
    return was_in_comment;
}

/* The #include being expanded, for the nesting-depth diagnostic. */
static const char *include_site_file;
static int include_site_line;

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
    char once_path[512];
    int if_active[MAX_IFSTACK];
    int if_taken[MAX_IFSTACK];
    int if_seen_else[MAX_IFSTACK];
    int if_sp;
    int active;
    int in_comment;

    if (depth > MAX_INCLUDE_DEPTH) {
        char diag[96];
        sprintf(diag, "#include nested too deeply (dcc allows %d levels)", MAX_INCLUDE_DEPTH);
        report_include_error(include_site_file, include_site_line, diag);
        exit(1);
    }

    canonical_include_path(name, once_path, sizeof(once_path));
    if (find_pragma_once_file(once_path) >= 0) {
        out = (char *)xmalloc(1);
        out[0] = 0;
        out_len[0] = 0;
        return out;
    }

    raw = read_file(name, &raw_len);

    out = NULL;
    out_len2 = 0;
    out_cap = 0;
    src_line = 1;
    if_sp = 0;
    active = 1;
    in_comment = 0;

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
            int line_opened_in_comment = line_starts_inside_comment(
                raw + line_start, line_end - line_start, &in_comment);

            if (line_opened_in_comment) {
                /* This line's own leading '#' (if any) is inside a block
                 * comment opened on an earlier line - not a real directive. */
                include_status = 0;
            } else if (include_cond_update(raw + line_start, line_end - line_start,
                                    if_active, if_taken, if_seen_else,
                                    &if_sp, &active)) {
                /* Conditional directive: keep it verbatim so the later
                 * active-source filter pass still balances #if/#endif. */
                include_status = 0;
            } else if (include_scan_macro_directive(raw + line_start,
                                                    line_end - line_start,
                                                    active)) {
                /* Keep the directive in the stream; this mutation is only for
                 * include-splice conditionals and is restored before filtering. */
                include_status = 0;
            } else if (try_parse_pragma_once(raw + line_start, line_end - line_start)) {
                if (active) {
                    mark_pragma_once_file(once_path);
                    include_status = -1;
                } else {
                    /* Dead-code pragma (e.g. inside #if 0): do not honor it;
                     * leave the line for the filter pass to drop. */
                    include_status = 0;
                }
            } else {
                include_status = try_parse_include(raw + line_start,
                                                   line_end - line_start,
                                                   name,
                                                   src_line,
                                                   incname,
                                                   sizeof(incname),
                                                   &is_system, active);
                if (include_status > 0 && include_extra_tokens)
                    warn_at(name, src_line, "extra tokens at end of #include directive");
                if (include_status > 0) {
                    include_site_file = name;
                    include_site_line = src_line;
                }
            }
            if (include_status > 0) {
                if (is_system) {
                    /* For system includes (<foo.h>), try the local directory
                     * first.  If a local file is found, include it just like a
                     * user include; otherwise silently drop the directive. */
                    make_include_path(name, incname, incpath, sizeof(incpath));
                    {
                        if (include_file_readable(incpath)) {
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
                    {
                        if (!include_file_readable(incpath)) {
                            /* A quoted user include that cannot be found is a
                             * hard error.  Report it against the including file
                             * and line (rather than letting read_file() abort
                             * with a generic "cannot open input") so the
                             * diagnostic points at the offending #include. */
                            char diag[320];
                            sprintf(diag, "cannot open include file '%s'", incname);
                            report_include_error(name, src_line, diag);
                            exit(1);
                        }
                    }
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
static void pp_directive_error(long line_start, const char *msg)
{
    char filebuf[256];
    int lno;

    source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
    dcc_error_at(filebuf, lno, line_start, msg, NULL);
}

static void pp_directive_warning(long line_start, const char *msg)
{
    char filebuf[256];
    int lno;

    source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
    warn_at(filebuf, lno, msg);
}

/* Report a malformed #if or #elif expression; expr is its text. */
static void report_pp_expr_error(long line_start, const char *directive, const char *expr)
{
    char msg[200];
    const char *p = expr;

    while (*p && isspace((unsigned char)*p))
        p++;
    if (!*p) {
        sprintf(msg, "#%s with no expression", directive);
        pp_directive_error(line_start, msg);
    } else {
        pp_directive_error(line_start, pp_eval_expr_error());
    }
}

/* Skip blanks and comments within one directive line. */
static const char *pp_skip_blank(const char *s, const char *e)
{
    for (;;) {
        while (s < e && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\f' || *s == '\v'))
            s++;
        if (s + 1 < e && s[0] == '/' && s[1] == '/')
            return e;
        if (s + 1 < e && s[0] == '/' && s[1] == '*') {
            const char *c = s + 2;
            while (c + 1 < e && !(c[0] == '*' && c[1] == '/'))
                c++;
            if (c + 1 >= e)
                return e;
            s = c + 2;
            continue;
        }
        return s;
    }
}

/* Warn about anything but blanks and comments after a directive's operands,
 * such as `#endif FOO` or `#include "x.h";`. */
static void pp_check_extra_tokens(const char *s, const char *e, long line_start,
                                  const char *directive)
{
    if (pp_skip_blank(s, e) < e) {
        char msg[80];
        sprintf(msg, "extra tokens at end of #%s directive", directive);
        pp_directive_warning(line_start, msg);
    }
}

/* The macro name a #define, #undef, #ifdef or #ifndef needs. Returns the
 * position after it, or NULL once it has reported a missing or bad name. */
static const char *pp_check_macro_name(const char *s, const char *e, long line_start,
                                       char *name, int namesz)
{
    int ni = 0;

    s = pp_skip_blank(s, e);
    if (s >= e) {
        pp_directive_error(line_start, "macro name missing");
        return NULL;
    }
    if (!is_ident_start((unsigned char)*s)) {
        pp_directive_error(line_start, "macro name must be an identifier");
        return NULL;
    }
    while (s < e && is_ident_char((unsigned char)*s)) {
        if (ni < namesz - 1)
            name[ni++] = *s;
        s++;
    }
    name[ni] = 0;
    return s;
}

/* Collapse a replacement list's comments and white-space runs to single
 * spaces, so redefinitions can be compared by spelling. */
static void pp_normalize_body(const char *in, char *out, int outsz)
{
    char *buf = (char *)xmalloc(MAX_MACRO_TEXT);
    const char *p;
    int oi = 0;

    strncpy(buf, in, MAX_MACRO_TEXT - 1);
    buf[MAX_MACRO_TEXT - 1] = 0;
    strip_macro_replacement_comments(buf);
    for (p = buf; *p && oi < outsz - 1; p++) {
        if (isspace((unsigned char)*p)) {
            if (oi > 0 && out[oi - 1] != ' ')
                out[oi++] = ' ';
        } else {
            out[oi++] = *p;
        }
    }
    while (oi > 0 && out[oi - 1] == ' ')
        oi--;
    out[oi] = 0;
    free(buf);
}

static int pp_param_list_error(long line_start, const char *msg)
{
    pp_directive_error(line_start, msg);
    return 0;
}

/* Diagnose a malformed #define before the filter records it: the macro
 * name, the parameter list, '#' and '##' in the replacement list, and a
 * redefinition that changes the macro. s is just past "define". Returns 0
 * when the definition is unusable and must not be recorded. */
static int pp_check_define(const char *s, const char *e, long line_start)
{
    char name[64];
    char params[MAX_MACRO_ARGS][32];
    char *body;
    char *old_body;
    int nparams = 0;
    int is_func = 0;
    int variadic = 0;
    int ok = 1;
    int bi;
    int di;
    const char *b;

    s = pp_check_macro_name(s, e, line_start, name, sizeof(name));
    if (!s)
        return 0;
    if (!strcmp(name, "defined"))
        return pp_param_list_error(line_start, "'defined' cannot be used as a macro name");
    if (s < e && *s == '(') {
        is_func = 1;
        s = pp_skip_blank(s + 1, e);
        if (s < e && *s == ')') {
            s++;
        } else {
            for (;;) {
                char param[32];
                int pi = 0;
                int i;

                s = pp_skip_blank(s, e);
                if (s + 2 < e && s[0] == '.' && s[1] == '.' && s[2] == '.') {
                    variadic = 1;
                    s += 3;
                } else if (s >= e) {
                    return pp_param_list_error(line_start, "missing ')' in macro parameter list");
                } else if (!is_ident_start((unsigned char)*s)) {
                    return pp_param_list_error(line_start,
                        "expected parameter name in macro parameter list");
                } else {
                    while (s < e && is_ident_char((unsigned char)*s)) {
                        if (pi < (int)sizeof(param) - 1)
                            param[pi++] = *s;
                        s++;
                    }
                    param[pi] = 0;
                    for (i = 0; i < nparams; ++i) {
                        if (!strcmp(params[i], param)) {
                            char msg[96];
                            sprintf(msg, "duplicate macro parameter name '%s'", param);
                            return pp_param_list_error(line_start, msg);
                        }
                    }
                    if (nparams >= MAX_MACRO_ARGS) {
                        char msg[96];
                        sprintf(msg, "too many macro parameters (dcc allows at most %d)",
                                MAX_MACRO_ARGS);
                        return pp_param_list_error(line_start, msg);
                    }
                    strcpy(params[nparams++], param);
                    s = pp_skip_blank(s, e);
                    /* GNU `name...` names the variadic parameter. */
                    if (s + 2 < e && s[0] == '.' && s[1] == '.' && s[2] == '.') {
                        variadic = 1;
                        s += 3;
                    }
                }
                s = pp_skip_blank(s, e);
                if (variadic) {
                    if (s >= e || *s != ')')
                        return pp_param_list_error(line_start,
                            "missing ')' after '...' in macro parameter list");
                    s++;
                    break;
                }
                if (s < e && *s == ',') {
                    s++;
                    continue;
                }
                if (s < e && *s == ')') {
                    s++;
                    break;
                }
                return pp_param_list_error(line_start, s >= e
                    ? "missing ')' in macro parameter list"
                    : "expected ',' or ')' in macro parameter list");
            }
        }
        if (nparams + variadic > MAX_MACRO_ARGS) {
            char msg[96];
            sprintf(msg, "too many macro parameters (dcc allows at most %d)", MAX_MACRO_ARGS);
            return pp_param_list_error(line_start, msg);
        }
    }

    body = (char *)xmalloc(MAX_MACRO_TEXT);
    old_body = (char *)xmalloc(MAX_MACRO_TEXT);
    bi = 0;
    while (s < e && bi < MAX_MACRO_TEXT - 1)
        body[bi++] = *s++;
    body[bi] = 0;
    while (s < e && (*s == ' ' || *s == '\t' || *s == '\r'))
        s++;
    if (s < e && !(s + 1 < e && *s == '/' && (s[1] == '/' || s[1] == '*'))) {
        char msg[96];
        sprintf(msg, "macro replacement list too long (dcc allows %d characters)",
                MAX_MACRO_TEXT - 1);
        pp_directive_error(line_start, msg);
        free(body);
        free(old_body);
        return 0;
    }
    pp_normalize_body(body, body, MAX_MACRO_TEXT);

    bi = (int)strlen(body);
    if ((bi >= 2 && body[0] == '#' && body[1] == '#') ||
        (bi >= 2 && body[bi - 1] == '#' && body[bi - 2] == '#')) {
        pp_directive_error(line_start,
            "'##' cannot appear at either end of a macro replacement list");
        ok = 0;
    }
    /* In a function-like macro, '#' stringizes the parameter after it. */
    for (b = body; ok && is_func && *b; ) {
        if (*b == '"' || *b == '\'') {
            char q = *b++;
            while (*b && *b != q) {
                if (*b == '\\' && b[1])
                    b++;
                b++;
            }
            if (*b)
                b++;
        } else if (*b == '#' && b[1] == '#') {
            b += 2;
        } else if (*b == '#') {
            char ident[32];
            int ii = 0;
            int i;
            int named = 0;

            b++;
            while (*b == ' ')
                b++;
            while (is_ident_char((unsigned char)*b)) {
                if (ii < (int)sizeof(ident) - 1)
                    ident[ii++] = *b;
                b++;
            }
            ident[ii] = 0;
            for (i = 0; i < nparams; ++i)
                if (!strcmp(params[i], ident))
                    named = 1;
            if (variadic && !strcmp(ident, "__VA_ARGS__"))
                named = 1;
            if (!named) {
                pp_directive_error(line_start, "'#' is not followed by a macro parameter");
                ok = 0;
            }
        } else {
            b++;
        }
    }

    di = ok ? find_define(name) : -1;
    if (di >= 0) {
        int same = defs[di].is_func == is_func;
        if (same && is_func) {
            int i;
            same = defs[di].nargs == nparams + variadic;
            for (i = 0; same && i < nparams; ++i)
                if (strcmp(defs[di].params[i], params[i]) != 0)
                    same = 0;
        }
        if (same) {
            pp_normalize_body(defs[di].value, old_body, MAX_MACRO_TEXT);
            same = strcmp(old_body, body) == 0;
        }
        if (!same) {
            char msg[96];
            sprintf(msg, "'%s' macro redefined", name);
            pp_directive_warning(line_start, msg);
        }
    }
    free(body);
    free(old_body);
    return ok;
}

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
    long if_open_ofs[MAX_IFSTACK];
    int sp;
    int active;
    int in_asm;
    long asm_open_ofs = 0;
    int logical_line;
    int in_comment;

    out = NULL;
    out_len = 0;
    out_cap = 0;
    p = 0;
    sp = 0;
    active = 1;
    in_asm = 0;
    logical_line = 1;
    in_comment = 0;

    while (p < src_len) {
        const char *s;
        const char *e;
        char word[32];
        int is_directive;
        int is_linemarker;
        int next_logical_line;
        int line_opened_in_comment;

        line_start = p;
        while (p < src_len && src[p] != '\n')
            p++;
        line_end = p;
        if (p < src_len && src[p] == '\n')
            p++;
        g_lex.line_no = logical_line;
        next_logical_line = logical_line + 1;

        line_opened_in_comment = line_starts_inside_comment(
            src + line_start, line_end - line_start, &in_comment);

        s = src + line_start;
        e = src + line_end;
        while (s < e && (*s == ' ' || *s == '\t'))
            s++;

        /* A line whose leading '#' (if any) is inside a block comment opened
         * on an earlier line is not a real directive - see
         * line_starts_inside_comment's comment for the motivating bug. */
        is_directive = (!line_opened_in_comment) && (s < e && *s == '#');

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

        /* `# 42 "file"`, the linemarker cpp writes, is #line in GNU form. */
        is_linemarker = s < e && isdigit((unsigned char)*s);
        if (is_linemarker) {
            strcpy(word, "line");
        } else {
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
                const char *msg = NULL;
                int ei;
                long lno;

                while (s < e && (*s == ' ' || *s == '\t'))
                    s++;
                ei = 0;
                while (s < e && ei < (int)sizeof(line_expr) - 1)
                    line_expr[ei++] = *s++;
                line_expr[ei] = 0;
                strip_macro_replacement_comments(line_expr);
                macro_expand_argument_text(line_expr, expanded, sizeof(expanded), 0);
                lp = expanded;
                while (*lp && isspace((unsigned char)*lp))
                    lp++;
                lno = 0;
                if (!isdigit((unsigned char)*lp))
                    msg = "#line directive requires a positive integer argument";
                while (!msg && isdigit((unsigned char)*lp)) {
                    int digit = *lp++ - '0';
                    /* Test before multiplying: a host long may be 32 bits. */
                    if (lno > (2147483647L - digit) / 10)
                        msg = "line number out of range in #line directive";
                    else
                        lno = lno * 10 + digit;
                }
                if (!msg && *lp && !isspace((unsigned char)*lp))
                    msg = "#line directive requires a positive integer argument";
                while (!msg && *lp && isspace((unsigned char)*lp))
                    lp++;
                if (!msg && *lp) {
                    if (*lp != '"') {
                        msg = "invalid filename for #line directive";
                    } else {
                        lp++;
                        while (*lp && *lp != '"') {
                            if (*lp == '\\' && lp[1])
                                lp++;
                            lp++;
                        }
                        if (*lp != '"')
                            msg = "invalid filename for #line directive";
                    }
                }
                if (msg) {
                    pp_directive_error(line_start, msg);
                    append_mem(&out, &out_len, &out_cap, "\n", 1);
                } else {
                    /* The source-location table reads literal numbers.
                     * Preserve the expanded operands for later lexer passes. */
                    next_logical_line = (int)lno;
                    append_mem(&out, &out_len, &out_cap, "#line ", 6);
                    append_mem(&out, &out_len, &out_cap, expanded, (long)strlen(expanded));
                    append_mem(&out, &out_len, &out_cap, "\n", 1);
                }
            }
            goto next_filter_line;
        }

        if (!strcmp(word, "ifdef") || !strcmp(word, "ifndef")) {
            char name[64];
            int ni;
            int cond;
            if (active) {
                const char *after = pp_check_macro_name(s, e, line_start, name, sizeof(name));
                if (after)
                    pp_check_extra_tokens(after, e, line_start, word);
            }
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
            if_open_ofs[sp] = line_start;
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
            g_lex.line_no = logical_line;
            cond = active && pp_eval_simple_expr(expr);
            if (active && pp_eval_expr_malformed())
                report_pp_expr_error(line_start, "if", expr);
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
            if_open_ofs[sp] = line_start;
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
                if (seen_else[i]) {
                    /* An #elif once the #else slot at this level is already
                     * used is a structural error in the directive text (the
                     * #else must be the final branch), exactly like the
                     * "#else after #else" case below. Reported unconditionally
                     * rather than gated on `active`. */
                    char filebuf[256];
                    int lno;
                    source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                    dcc_error_at(filebuf, lno, line_start, "#elif after #else", NULL);
                    active = 0;
                } else if (branch_taken[i]) {
                    active = 0;
                } else {
                    while (s < e && (*s == ' ' || *s == '\t')) s++;
                    ei = 0;
                    while (s < e && ei < (int)sizeof(expr) - 1)
                        expr[ei++] = *s++;
                    expr[ei] = 0;
                    strip_macro_replacement_comments(expr);
                    g_lex.line_no = logical_line;
                    cond = parent && pp_eval_simple_expr(expr);
                    if (parent && pp_eval_expr_malformed())
                        report_pp_expr_error(line_start, "elif", expr);
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
                if (parent)
                    pp_check_extra_tokens(s, e, line_start, "else");
                if (!seen_else[i]) {
                    active = parent && !branch_taken[i];
                    branch_taken[i] = 1;
                    seen_else[i] = 1;
                } else {
                    /* A second #else at this nesting level - previously
                     * silently deactivated output with no diagnostic,
                     * which let a missing #endif between two #else's for
                     * the same #if pass through unnoticed: the second
                     * #else reads as the enclosing level's own #else to a
                     * human, but this #if's #else slot was already used by
                     * the first one. Reported unconditionally (not gated on
                     * `active`, unlike the "no matching #if" cases below) -
                     * this is a structural nesting error in the directive
                     * text itself, not a property of which branch happens
                     * to be live, so it's just as real when the enclosing
                     * branch is the one currently skipped. */
                    char filebuf[256];
                    int lno;
                    source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                    dcc_error_at(filebuf, lno, line_start, "#else after #else", NULL);
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
                if (active)
                    pp_check_extra_tokens(s, e, line_start, "endif");
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
            const char *after = pp_check_macro_name(s, e, line_start, name, sizeof(name));
            if (!after) {
                append_mem(&out, &out_len, &out_cap, "\n", 1);
                goto next_filter_line;
            }
            pp_check_extra_tokens(after, e, line_start, "undef");
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

            if (!pp_check_define(s, e, line_start)) {
                /* Diagnosed; leave it out so the lexer does not record a
                 * half-parsed macro. */
                append_mem(&out, &out_len, &out_cap, "\n", 1);
                goto next_filter_line;
            }

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
                        if (nargs < MAX_MACRO_ARGS) {
                            strcpy(params[nargs], "__VA_ARGS__");
                            nargs++;
                        }
                        while (s < e && (*s == ' ' || *s == '\t')) s++;
                        if (s < e && *s == ',') s++;
                        continue;
                    }

                    {
                        char param[32];
                        pi = 0;
                        while (s < e && is_ident_char((unsigned char)*s)) {
                            if (pi < 31)
                                param[pi++] = *s;
                            s++;
                        }
                        param[pi] = 0;
                        if (param[0] && nargs < MAX_MACRO_ARGS)
                            strcpy(params[nargs++], param);
                    }
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
                add_define_ex(name, val, 1, nargs, params);
            } else if (name[0]) {
                while (s < e && (*s == ' ' || *s == '\t')) s++;
                vi = 0;
                while (s < e && vi < (int)sizeof(val) - 1)
                    val[vi++] = *s++;
                while (vi > 0 && (val[vi - 1] == ' ' || val[vi - 1] == '\t' || val[vi - 1] == '\r'))
                    vi--;
                val[vi] = 0;
                add_define(name, val);
            }
            /* Keep active #define in the filtered source so macro scope is
             * applied in normal C source order.  The add_define above is only
             * for evaluating later conditionals during this filtering pass.
             */
            append_mem(&out, &out_len, &out_cap, src + line_start, p - line_start);
            goto next_filter_line;
        }

        if (!strcmp(word, "asm")) {
            if (active) {
                in_asm = 1;
                asm_open_ofs = line_start;
            }
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
        } else if (active) {
            /* A lone # is the null directive; `#(1, 2);` is not one. */
            while (s < e && (*s == ' ' || *s == '\t' || *s == '\r'))
                s++;
            if (s < e && !(*s == '/' && s + 1 < e && (s[1] == '/' || s[1] == '*'))) {
                char filebuf[256];
                int lno;
                char msg[64];
                source_location_at(line_start, filebuf, sizeof(filebuf), &lno);
                sprintf(msg, "'#%c' is not a valid preprocessor directive", *s);
                dcc_error_at(filebuf, lno, line_start, msg, NULL);
            }
        }
        append_mem(&out, &out_len, &out_cap, "\n", 1);

next_filter_line:
        logical_line = next_logical_line;
    }

    /* Any #if/#ifdef/#ifndef still open at end of file never got a matching
     * #endif. Report only the outermost still-open level (nested opens are
     * very likely a consequence of it, not independent problems) at the
     * point where the file actually ran out - once that one is fixed,
     * reprocessing may well reveal or resolve the rest, the same way fixing
     * the first error in a cascade usually does. */
    if (in_asm) {
        char openbuf[256];
        char eofbuf[256];
        char msg[320];
        int openline;
        int eofline;

        source_location_at(asm_open_ofs, openbuf, sizeof(openbuf), &openline);
        source_location_at(p > 0 ? p - 1 : 0, eofbuf, sizeof(eofbuf), &eofline);
        sprintf(msg, "#asm with no matching #endasm (opened at %s:%d)", openbuf, openline);
        dcc_error_at(eofbuf, eofline, p > 0 ? p - 1 : 0, msg, NULL);
    }

    if (sp > 0) {
        char openbuf[256];
        char eofbuf[256];
        int openline;
        int eofline;

        source_location_at(if_open_ofs[0], openbuf, sizeof(openbuf), &openline);
        /* The last character: the end itself can map past the last line. */
        source_location_at(p > 0 ? p - 1 : 0, eofbuf, sizeof(eofbuf), &eofline);
        {
            char msg[320];
            sprintf(msg, "#if with no matching #endif (opened at %s:%d)", openbuf, openline);
            dcc_error_at(eofbuf, eofline, p, msg, NULL);
        }
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

FILE *bank_api_file;

void print_version(void)
{
    printf("dcc (DCC C89->Z80 compiler) %s\n", DCC_BUILD_VERSION);
}

void usage(void)
{
    fprintf(stderr, "usage: dcc [-c|-module] [-f|-ffloatio|-fno-floatio] [-fl|-flongio|-fno-longio] [-fhexio|-fno-hexio] [-foctio|-fno-octio] [-fstack-check] [-fno-narrow] [-v] [-h] [-s|-stack bytes] [-Idir] [-Dname[=value]] [-Uname] input.c -o output.mac\n");
    exit(1);
}

void print_help(void)
{
    print_version();
    printf("usage: dcc [options] input.c -o output.mac\n");
    printf("\n");
    printf("options:\n");
    printf("  -o <file>        write M80 assembly to <file> ('-' for stdout)\n");
    printf("  -c, -module      emit a linkable helper module (not a final program)\n");
    printf("  -bank-api <file> emit resolved public function type/definition metadata\n");
    printf("  -f, -ffloatio    force every printf-family call to support %%f, even\n");
    printf("                   ones whose literal format string doesn't use it\n");
    printf("                   (normally auto-detected per call; only needed for a\n");
    printf("                   format string that isn't a compile-time literal)\n");
    printf("  -fno-floatio     opposite: force every call to NOT support %%f, even\n");
    printf("                   a literal that uses it, or the conservative fallback\n");
    printf("                   for a non-literal format string - use only when you\n");
    printf("                   know no call site anywhere needs it, to shrink the\n");
    printf("                   fallback's cost\n");
    printf("  -fl, -flongio    same, but forces long formats (%%ld/%%lu/%%lx/%%lX/%%ls)\n");
    printf("  -fno-longio      -fno-floatio, but for long formats\n");
    printf("  -fhexio          force every call to support %%x/%%X\n");
    printf("  -fno-hexio       -fno-floatio, but for %%x/%%X\n");
    printf("  -foctio          force every call to support %%o\n");
    printf("  -fno-octio       -fno-floatio, but for %%o\n");
    printf("  -s, -stack <bytes>   reserve <bytes> for the C stack (default 512)\n");
    printf("  -g               emit source-level debug annotations\n");
    printf("  -gline           emit optimized debug annotations and variable locations\n");
    printf("  -fstack-check    abort gracefully if the stack overflows its reserve\n");
    printf("  -fno-narrow      disable every int-array/scalar/for-counter byte-narrowing pass\n");
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
    const char *bank_api_path = NULL;

    input_name = NULL;
    output_name = NULL;
    opt_module = 0;
    opt_stack_size = 512;
    opt_stack_check = 0;
    opt_no_narrow = 0;
    opt_debug = 0;
    opt_debug_lines = 0;
    g_main_has_args = 0;
    max_function_local_bytes = 0;

    add_define("_DCC_", "1");
    /* Common implementation macro used by GCC/SDCC-compatible sources.
     * This target always has eight-bit bytes (the same value exposed as
     * CHAR_BIT by limits.h), including during preprocessing before headers
     * have been included. */
    add_define("__CHAR_BIT__", "8");
    /* Underlying type used by the target's size_t typedef.  Exposing the
     * conventional implementation macro lets freestanding GCC/SDCC-derived
     * sources declare library interfaces without including stddef.h. */
    add_define("__SIZE_TYPE__", "unsigned int");
    add_define("__PTRDIFF_TYPE__", "int");
    add_define("__builtin_offsetof", "__offsetof");
    /* GCC's __extension__ only suppresses pedantic diagnostics for the next
     * construct.  dcc has no pedantic mode, so its compatible meaning is an
     * empty token sequence. */
    add_define("__extension__", "");
    ast_build_init();

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-ffloatio") || !strcmp(argv[i], "-f")) {
            opt_floatio = 1;
        } else if (!strcmp(argv[i], "-fno-floatio")) {
            opt_floatio = -1;
        } else if (!strcmp(argv[i], "-flongio") || !strcmp(argv[i], "-fl")) {
            opt_longio = 1;
        } else if (!strcmp(argv[i], "-fno-longio")) {
            opt_longio = -1;
        } else if (!strcmp(argv[i], "-fhexio")) {
            opt_hexio = 1;
        } else if (!strcmp(argv[i], "-fno-hexio")) {
            opt_hexio = -1;
        } else if (!strcmp(argv[i], "-foctio")) {
            opt_octio = 1;
        } else if (!strcmp(argv[i], "-fno-octio")) {
            opt_octio = -1;
        } else if (!strcmp(argv[i], "-fstack-check")) {
            opt_stack_check = 1;
        } else if (!strcmp(argv[i], "-fno-narrow")) {
            opt_no_narrow = 1;
        } else if (!strcmp(argv[i], "-g")) {
            opt_debug = 1;
            opt_debug_lines = 0;
        } else if (!strcmp(argv[i], "-gline")) {
            opt_debug = 0;
            opt_debug_lines = 1;
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
        } else if (!strcmp(argv[i], "-bank-api")) {
            if (++i >= argc) usage();
            bank_api_path = argv[i];
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

    {
        int saved_ndefs;
        struct Def *saved_defs;

        /* Include splicing tracks active #define/#undef directives so that
         * #pragma once under #if/#ifdef sees source-order macro state. Restore
         * the table afterward: the expanded source still contains those
         * directives, and the active-source filter must replay them normally. */
        saved_defs = (struct Def *)xmalloc(sizeof(defs));
        saved_ndefs = ndefs;
        memcpy(saved_defs, defs, sizeof(defs));

        src = preprocess_includes_file(input_name, 0, &src_len);
        g_src_generation++;

        ndefs = saved_ndefs;
        memcpy(defs, saved_defs, sizeof(defs));
        free(saved_defs);
    }
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
        g_src_generation++;
    }
    /* Function-like macros are now left in the filtered source and processed
     * by the normal lexer-level preprocessor in source order.  Do not pre-scan
     * them globally; that breaks C macro scoping/order.
     */

    /* Whole-file lexical scan of which identifiers are ever written to /
     * have their address taken, and in which function each write occurs -
     * see dcc_global_scan.c. Runs once, over the same finalised `src`
     * buffer the real pass will tokenise, then fully rewinds lexer and
     * macro-table state so the real pass starts exactly as if this had
     * never happened. Must run before posi/line_no are set for the real
     * pass below (it sets and restores them itself). */
    scan_global_write_info();

    g_lex.posi = 0;
    g_lex.tok_start_pos = 0;
    g_lex.line_no = 1;
    g_lex.tok_line = 1;
    pp_reset_asm_dedupe();
    g_emit_sink.purpose = EMIT_SINK_FINAL;

    if (!strcmp(output_name, "-")) {
        g_emit_sink.stream = stdout;
    } else {
        /* Keep assembly bytes identical across hosts; text mode on Windows
         * otherwise expands every LF and invalidates strict campaign hashes. */
        g_emit_sink.stream = fopen(output_name, "wb");
        if (!g_emit_sink.stream) fatal("cannot open output");
    }
    fprintf(g_emit_sink.stream, "; built with dcc %s\n", DCC_BUILD_VERSION);

    add_typedef_name("FILE", TYPE_INT, 0);

    /* stdout/stderr/stdin are runtime data objects, not functions.
     * They are predeclared lazily in parse_translation_unit() as
     * SC_EXTERN so reference bookkeeping emits EXTRN when they are
     * actually referenced.  Do not pre-add them here as SC_FUNC,
     * or add_global() preserves the wrong storage class and M80 sees
     * ld hl,_stdout without a preceding EXTRN. */
    add_define("NULL", "0");
    if (find_define("EOF") < 0)
        add_define("EOF", "-1");

    if (bank_api_path) {
        bank_api_file = fopen(bank_api_path, "w");
        if (!bank_api_file) fatal("cannot open bank API metadata");
        fprintf(bank_api_file, "DCCBANK 2\n");
    }
    parse_translation_unit();
    if (bank_api_file) {
        bank_api_records();
        if (fclose(bank_api_file)) fatal("cannot write bank API metadata");
        bank_api_file = NULL;
    }
    emit_needed_deferred_bodies();
    mir_finish_translation_unit();
    emit_data();
    emit_deferred_extrns();
    emit("\n\tend\n");

    if (g_emit_sink.stream != stdout)
        fclose(g_emit_sink.stream);

    if (warnings) {
        fprintf(stderr, "dcc: %d warning(s)\n", warnings);
    }

    if (errors) {
        fprintf(stderr, "dcc: %d error(s)\n", errors);
        return 1;
    }

    return 0;
}
