/**
 * @file peep_lines.c
 * @brief Owns dccpeep's mutable line program and assembly file I/O.
 *
 * @par Role
 * Allocates and frees line text, preserves user-assembly text behind opaque
 * placeholders, tracks program versions and edit statistics, provides
 * transactional mutations, and reads and writes normalized assembly files.
 *
 * @par Key entry points
 * peep_context_init(), peep_edit_begin(), replace1(), delete_n(),
 * insert_line(), read_file(), and write_file().
 *
 * @par Boundary
 * Passes must mutate through this API and cannot rewrite opaque user assembly.
 * Parsing, effects, dataflow, and pass scheduling belong to other modules.
 */
#include "dccpeep_internal.h"

char *lines[MAX_LINES];
char *user_asm_original[MAX_LINES];
char *debug_metadata[MAX_LINES];
static char *trailing_debug_metadata;
int nlines;
int input_is_dcc_generated;
PeepContext peep_context;

void peep_context_init(void)
{
    memset(&peep_context, 0, sizeof(peep_context));
    peep_context.lines = lines;
    peep_context.user_asm_original = user_asm_original;
    peep_context.line_count = &nlines;
}

static void peep_edit_discard_snapshot(PeepEditTransaction *transaction)
{
    int i;

    for (i = 0; i < transaction->line_count; ++i) {
        free(transaction->lines[i]);
        free(transaction->user_asm_original[i]);
        free(transaction->debug_metadata[i]);
    }
    free(transaction->lines);
    free(transaction->user_asm_original);
    free(transaction->debug_metadata);
    free(transaction->trailing_debug_metadata);
    memset(transaction, 0, sizeof(*transaction));
}

void peep_edit_begin(PeepEditTransaction *transaction)
{
    int i;

    memset(transaction, 0, sizeof(*transaction));
    transaction->lines = (char **)calloc((size_t)nlines, sizeof(char *));
    transaction->user_asm_original = (char **)calloc(
        (size_t)nlines, sizeof(char *));
    transaction->debug_metadata = (char **)calloc(
        (size_t)nlines, sizeof(char *));
    if (nlines && (!transaction->lines || !transaction->user_asm_original ||
                   !transaction->debug_metadata)) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    transaction->line_count = nlines;
    transaction->version = peep_context.program_version;
    transaction->stats = peep_context.stats;
    transaction->active = 1;
    for (i = 0; i < nlines; ++i) {
        transaction->lines[i] = xstrdup2(lines[i]);
        if (user_asm_original[i])
            transaction->user_asm_original[i] = xstrdup2(user_asm_original[i]);
        if (debug_metadata[i])
            transaction->debug_metadata[i] = xstrdup2(debug_metadata[i]);
    }
    if (trailing_debug_metadata)
        transaction->trailing_debug_metadata = xstrdup2(trailing_debug_metadata);
}

void peep_edit_commit(PeepEditTransaction *transaction)
{
    if (transaction->active)
        peep_edit_discard_snapshot(transaction);
}

void peep_edit_rollback(PeepEditTransaction *transaction)
{
    int i;

    if (!transaction->active)
        return;
    for (i = 0; i < nlines; ++i) {
        free(lines[i]);
        free(user_asm_original[i]);
        free(debug_metadata[i]);
        lines[i] = NULL;
        user_asm_original[i] = NULL;
        debug_metadata[i] = NULL;
    }
    free(trailing_debug_metadata);
    trailing_debug_metadata = NULL;
    nlines = transaction->line_count;
    for (i = 0; i < nlines; ++i) {
        lines[i] = transaction->lines[i];
        user_asm_original[i] = transaction->user_asm_original[i];
        debug_metadata[i] = transaction->debug_metadata[i];
        transaction->lines[i] = NULL;
        transaction->user_asm_original[i] = NULL;
        transaction->debug_metadata[i] = NULL;
    }
    trailing_debug_metadata = transaction->trailing_debug_metadata;
    transaction->trailing_debug_metadata = NULL;
    peep_context.program_version = transaction->version + 1;
    peep_context.stats = transaction->stats;
    peep_edit_discard_snapshot(transaction);
}

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

static char *join_debug_metadata(const char *first, const char *second)
{
    char *joined;
    size_t first_length;
    size_t second_length;

    if (!first || !first[0])
        return second && second[0] ? xstrdup2(second) : NULL;
    if (!second || !second[0])
        return xstrdup2(first);
    first_length = strlen(first);
    second_length = strlen(second);
    joined = (char *)malloc(first_length + second_length + 2);
    if (!joined) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memcpy(joined, first, first_length);
    joined[first_length] = '\n';
    memcpy(joined + first_length + 1, second, second_length + 1);
    return joined;
}

static void append_debug_metadata(char **destination, const char *text)
{
    char *joined;

    if (!text || !text[0])
        return;
    joined = join_debug_metadata(*destination, text);
    free(*destination);
    *destination = joined;
}

static void write_debug_metadata(FILE *file, const char *metadata)
{
    if (!metadata || !metadata[0])
        return;
    fputs(metadata, file);
    fputc('\n', file);
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
    const char *semi;
    int n;

    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == ';')
        return 0;  /* a prose comment ending in ':' is not a label */

    /* A label may carry a trailing inline comment on the same physical
     * line ("stchk_pb:      ; print byte..."); only the code before the
     * comment determines whether this is a label, not the raw line's own
     * last character. */
    semi = strchr(s, ';');
    n = semi ? (int)(semi - s) : (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        --n;

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
    peep_context.program_version++;
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
    char *removed_metadata = NULL;

    for (j = 0; j < count; j++) {
        if (user_asm_original[i + j] != NULL) {
            fprintf(stderr, "internal error: attempted to delete user assembly\n");
            exit(1);
        }
        append_debug_metadata(&removed_metadata, debug_metadata[i + j]);
        free(lines[i + j]);
        free(debug_metadata[i + j]);
    }

    memmove(&lines[i], &lines[i + count],
            (size_t)(nlines - i - count) * sizeof(lines[0]));
    memmove(&user_asm_original[i], &user_asm_original[i + count],
            (size_t)(nlines - i - count) * sizeof(user_asm_original[0]));
    memmove(&debug_metadata[i], &debug_metadata[i + count],
            (size_t)(nlines - i - count) * sizeof(debug_metadata[0]));

    nlines -= count;
    if (removed_metadata) {
        if (i < nlines) {
            char *joined = join_debug_metadata(removed_metadata, debug_metadata[i]);
            free(debug_metadata[i]);
            debug_metadata[i] = joined;
        } else {
            append_debug_metadata(&trailing_debug_metadata, removed_metadata);
        }
        free(removed_metadata);
    }
    peep_context.stats.lines_deleted += (unsigned long)count;
    peep_context.program_version++;
    for (j = nlines; j < nlines + count; ++j) {
        lines[j] = NULL;
        user_asm_original[j] = NULL;
        debug_metadata[j] = NULL;
    }
}

void insert_line(int i, const char *s)
{
    if (nlines >= MAX_LINES) {
        fprintf(stderr, "too many lines\n");
        exit(1);
    }

    memmove(&lines[i + 1], &lines[i],
            (size_t)(nlines - i) * sizeof(lines[0]));
    memmove(&user_asm_original[i + 1], &user_asm_original[i],
            (size_t)(nlines - i) * sizeof(user_asm_original[0]));
        memmove(&debug_metadata[i + 1], &debug_metadata[i],
            (size_t)(nlines - i) * sizeof(debug_metadata[0]));

    lines[i] = xstrdup2(s);
    user_asm_original[i] = NULL;
        debug_metadata[i] = NULL;
    nlines++;
    peep_context.stats.lines_inserted++;
    peep_context.program_version++;
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
    char *pending_debug_metadata;

    f = fopen(name, "r");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", name);
        exit(1);
    }

    buf = NULL;
    cap = 0;
    user_asm_depth = 0;
    pending_debug_metadata = NULL;
    while (read_physical_line(f, &buf, &cap)) {
        char placeholder[64];

        trim(buf);
        if (strcmp(buf, "; dcc stage-1d output") == 0)
            input_is_dcc_generated = 1;
        if (user_asm_depth == 0 && strncmp(buf, ";@dcc-", 6) == 0) {
            append_debug_metadata(&pending_debug_metadata, buf);
            continue;
        }
        if (nlines >= MAX_LINES) {
            fprintf(stderr, "too many lines\n");
            exit(1);
        }
        if (strcmp(buf, "; dcc user asm begin") == 0) {
            lines[nlines] = xstrdup2(buf);
            user_asm_original[nlines] = NULL;
            debug_metadata[nlines++] = pending_debug_metadata;
            pending_debug_metadata = NULL;
            user_asm_depth++;
        } else if (strcmp(buf, "; dcc user asm end") == 0) {
            if (user_asm_depth > 0)
                user_asm_depth--;
            lines[nlines] = xstrdup2(buf);
            user_asm_original[nlines] = NULL;
            debug_metadata[nlines++] = pending_debug_metadata;
            pending_debug_metadata = NULL;
        } else if (user_asm_depth > 0) {
            sprintf(placeholder, "__dcc_user_asm_%d:", nlines);
            lines[nlines] = xstrdup2(placeholder);
            user_asm_original[nlines] = xstrdup2(buf);
            debug_metadata[nlines++] = pending_debug_metadata;
            pending_debug_metadata = NULL;
        } else {
            lines[nlines] = xstrdup2(buf);
            user_asm_original[nlines] = NULL;
            debug_metadata[nlines++] = pending_debug_metadata;
            pending_debug_metadata = NULL;
        }
    }
    append_debug_metadata(&trailing_debug_metadata, pending_debug_metadata);
    free(pending_debug_metadata);

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
        write_debug_metadata(f, debug_metadata[i]);
        if (line[0] == 0)
            fprintf(f, "\n");
        else if (starts_label(line) || line[0] == ';')
            fprintf(f, "%s\n", line);
        else
            fprintf(f, "\t%s\n", line);
    }
            write_debug_metadata(f, trailing_debug_metadata);

    if (ferror(f) || fclose(f) != 0) {
        fprintf(stderr, "cannot write %s\n", name);
        exit(1);
    }
}
