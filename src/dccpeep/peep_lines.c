/* peep_lines.c - dccpeep line storage, mutation, and file I/O. */
#include "dccpeep_internal.h"

char *lines[MAX_LINES];
char *user_asm_original[MAX_LINES];
int nlines;
int input_is_dcc_generated;

char *xstrdup2(const char *s)
{
    char *p;
    p = (char *)malloc(strlen(s) + 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(p, s);
    return p;
}

static void trim(char *s)
{
    int i;
    int j;
    int n;

    n = (int)strlen(s);
    while (n > 0 &&
           (s[n - 1] == '\n' || s[n - 1] == '\r' ||
            s[n - 1] == ' '  || s[n - 1] == '\t'))
        s[--n] = 0;

    i = 0;
    while (s[i] == ' ' || s[i] == '\t')
        i++;

    if (i) {
        j = 0;
        while (s[i])
            s[j++] = s[i++];
        s[j] = 0;
    }
}

int eq(int i, const char *s)
{
    char buf[MAX_LINE];
    char *semi;
    int n;

    if (i < 0 || i >= nlines)
        return 0;

    strncpy(buf, lines[i], sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;

    semi = strchr(buf, ';');
    if (semi)
        *semi = 0;

    n = (int)strlen(buf);
    while (n > 0 && (buf[n - 1] == ' ' || buf[n - 1] == '\t'))
        buf[--n] = 0;

    return strcmp(buf, s) == 0;
}

int starts_label(const char *s)
{
    int n;
    n = (int)strlen(s);
    return n > 0 && s[n - 1] == ':';
}

int is_blank_or_comment(const char *s)
{
    return s[0] == 0 || s[0] == ';';
}


void strip_peep_comment_copy(char *dst, const char *src)
{
    int i;
    int n;

    i = 0;
    while (src[i] && src[i] != ';' && i < MAX_LINE - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;

    n = (int)strlen(dst);
    while (n > 0 && (dst[n - 1] == ' ' || dst[n - 1] == '\t')) {
        dst[n - 1] = 0;
        n--;
    }
}

void strip_peep_comment_lower_copy(char *dst, const char *src)
{
    char *p;

    strip_peep_comment_copy(dst, src);
    for (p = dst; *p; ++p)
        *p = (char)tolower((unsigned char)*p);
}

void replace1(int i, const char *s)
{
    char *p;

    if (user_asm_original[i] != NULL) {
        fprintf(stderr, "internal error: attempted to rewrite user assembly\n");
        exit(1);
    }

    /*
     * Be careful when callers pass lines[i] as the replacement text.
     * The old version freed lines[i] before duplicating s, which is a
     * use-after-free if s == lines[i].  That happened in the signed_le_zero
     * peephole and produced platform-dependent garbage on Linux while often
     * appearing to work on Windows.
     */
    p = xstrdup2(s);
    free(lines[i]);
    lines[i] = p;
}

static char *make_tagged_line(const char *s, const char *tag)
{
    char *buf;
    size_t n;

    /*
     * Optimized lines can already be close to MAX_LINE bytes.  A fixed
     * snprintf buffer is safe at runtime but triggers -Wformat-truncation
     * under fortified libc because the diagnostic correctly sees that the
     * tag may not fit.  Allocate the exact size instead.
     */
    n = strlen(s) + strlen(tag) + strlen(" ; peep: ") + 1;
    buf = (char *)malloc(n);
    if (!buf) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(buf, s);
    strcat(buf, " ; peep: ");
    strcat(buf, tag);
    return buf;
}

void replace1_tagged(int i, const char *s, const char *tag)
{
    char *buf;

    buf = make_tagged_line(s, tag);
    replace1(i, buf);
    free(buf);
}

void delete_n(int i, int count)
{
    int j;

    for (j = 0; j < count; j++) {
        if (user_asm_original[i + j] != NULL) {
            fprintf(stderr, "internal error: attempted to delete user assembly\n");
            exit(1);
        }
        free(lines[i + j]);
    }

    for (j = i; j + count < nlines; j++) {
        lines[j] = lines[j + count];
        user_asm_original[j] = user_asm_original[j + count];
    }

    nlines -= count;
    for (j = nlines; j < nlines + count; ++j) {
        lines[j] = NULL;
        user_asm_original[j] = NULL;
    }
}

void insert_line(int i, const char *s)
{
    int j;

    if (nlines >= MAX_LINES) {
        fprintf(stderr, "too many lines\n");
        exit(1);
    }

    for (j = nlines; j > i; j--) {
        lines[j] = lines[j - 1];
        user_asm_original[j] = user_asm_original[j - 1];
    }

    lines[i] = xstrdup2(s);
    user_asm_original[i] = NULL;
    nlines++;
}

void insert_line_tagged(int i, const char *s, const char *tag)
{
    char *buf;

    buf = make_tagged_line(s, tag);
    insert_line(i, buf);
    free(buf);
}

static int read_physical_line(FILE *f, char **bufp, size_t *capp)
{
    size_t len;
    int c;
    char *buf;

    len = 0;
    buf = *bufp;
    while ((c = fgetc(f)) != EOF) {
        if (len + 1 >= *capp) {
            size_t newcap = (*capp == 0) ? 512 : *capp * 2;
            char *grown = (char *)realloc(buf, newcap);
            if (grown == NULL) {
                free(buf);
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            buf = grown;
            *bufp = buf;
            *capp = newcap;
        }
        if (c == '\n')
            break;
        buf[len++] = (char)c;
    }
    if (c == EOF && len == 0)
        return 0;
    buf[len] = 0;
    return 1;
}

void read_file(const char *name)
{
    FILE *f;
    char *buf;
    size_t cap;
    int user_asm_depth;

    f = fopen(name, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", name);
        exit(1);
    }

    buf = NULL;
    cap = 0;
    user_asm_depth = 0;
    while (read_physical_line(f, &buf, &cap)) {
        char placeholder[64];

        trim(buf);
        if (strcmp(buf, "; dcc stage-1d output") == 0)
            input_is_dcc_generated = 1;
        if (nlines >= MAX_LINES) {
            fprintf(stderr, "too many lines\n");
            exit(1);
        }
        if (strcmp(buf, "; dcc user asm begin") == 0) {
            lines[nlines] = xstrdup2(buf);
            user_asm_original[nlines++] = NULL;
            user_asm_depth++;
        } else if (strcmp(buf, "; dcc user asm end") == 0) {
            if (user_asm_depth > 0)
                user_asm_depth--;
            lines[nlines] = xstrdup2(buf);
            user_asm_original[nlines++] = NULL;
        } else if (user_asm_depth > 0) {
            sprintf(placeholder, "__dcc_user_asm_%d:", nlines);
            lines[nlines] = xstrdup2(placeholder);
            user_asm_original[nlines++] = xstrdup2(buf);
        } else {
            lines[nlines] = xstrdup2(buf);
            user_asm_original[nlines++] = NULL;
        }
    }

    if (ferror(f)) {
        fprintf(stderr, "cannot read %s\n", name);
        free(buf);
        fclose(f);
        exit(1);
    }
    free(buf);
    fclose(f);
}

void write_file(const char *name)
{
    FILE *f;
    int i;

    f = fopen(name, "w");
    if (!f) {
        fprintf(stderr, "cannot create %s\n", name);
        exit(1);
    }

    for (i = 0; i < nlines; i++) {
        const char *line = user_asm_original[i] != NULL
            ? user_asm_original[i] : lines[i];
        if (line[0] == 0)
            fprintf(f, "\n");
        else if (starts_label(line) || line[0] == ';')
            fprintf(f, "%s\n", line);
        else
            fprintf(f, "\t%s\n", line);
    }

    if (ferror(f) || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", name);
        exit(1);
    }
}
