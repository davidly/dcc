/**
 * @file dcc_app_strip.c
 * @brief Removes unreachable functions and objects from app .MAC modules.
 *
 * @par Role
 * Loads all dcc-generated application modules, builds a conservative symbol
 * graph from structural LTO markers and identifier references, marks blocks
 * reachable from whole-program roots, and atomically rewrites each module.
 *
 * @par Key entry points
 * dcc_strip_app_files().
 *
 * @par Boundary
 * The compiler owns marker emission and dccmake owns whole-program policy.
 * This module changes assembly text before m80c; it does not parse REL files
 * or alter runtime stripping.
 */
#include "dcc_app_strip.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MARK_BEGIN ";@dcc.lto begin "
#define MARK_END   ";@dcc.lto end "
#define MAX_SYMBOL 128

struct AppLine {
    char *text;
    int block;
};

struct AppFile {
    const char *path;
    char *temp_path;
    char *backup_path;
    struct AppLine *lines;
    int line_count;
    int line_capacity;
};

struct AppBlock {
    int file;
    int start;
    int end;
    int keep;
    int scanned;
    char symbol[MAX_SYMBOL];
};

struct AppExtern {
    int file;
    int needed;
    char symbol[MAX_SYMBOL];
};

struct AppLabel {
    int file;
    int block;
    char symbol[MAX_SYMBOL];
};

struct AppPublic {
    int block;
    char symbol[MAX_SYMBOL];
};

struct AppGraph {
    struct AppFile *files;
    int file_count;
    struct AppBlock *blocks;
    int block_count;
    int block_capacity;
    struct AppExtern *externs;
    int extern_count;
    int extern_capacity;
    struct AppLabel *labels;
    int label_count;
    int label_capacity;
    struct AppPublic *publics;
    int public_count;
    int public_capacity;
};

static void *xrealloc(void *p, size_t size)
{
    void *result;

    result = realloc(p, size);
    if (result == NULL) {
        fprintf(stderr, "dccrtlstrip: out of memory\n");
        exit(1);
    }
    return result;
}

static char *xstrdup_app(const char *s)
{
    char *result;

    result = (char *)malloc(strlen(s) + 1);
    if (result == NULL) {
        fprintf(stderr, "dccrtlstrip: out of memory\n");
        exit(1);
    }
    strcpy(result, s);
    return result;
}

static int char_ieq(int a, int b)
{
    return tolower((unsigned char)a) == tolower((unsigned char)b);
}

static int str_ieq_app(const char *a, const char *b)
{
    while (*a && *b) {
        if (!char_ieq(*a, *b))
            return 0;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

static const char *skip_space_app(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
        ++p;
    return p;
}

static int is_symbol_start(int c)
{
    return isalpha((unsigned char)c) || c == '_' || c == '?' || c == '@';
}

static int is_symbol_char(int c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '?' ||
           c == '@' || c == '$';
}

static int parse_marker(
    const char *line, const char *prefix, char *symbol, int symbol_size)
{
    const char *p;
    int n;

    p = skip_space_app(line);
    if (strncmp(p, prefix, strlen(prefix)) != 0)
        return 0;
    p += strlen(prefix);
    p = skip_space_app(p);
    if (!is_symbol_start(*p))
        return -1;
    n = 0;
    while (is_symbol_char(*p)) {
        if (n + 1 >= symbol_size)
            return -1;
        symbol[n++] = *p++;
    }
    symbol[n] = 0;
    p = skip_space_app(p);
    return *p == 0 || *p == ';' ? 1 : -1;
}

static int append_public(
    struct AppGraph *graph, int block, const char *symbol)
{
    struct AppPublic *entry;
    int i;

    for (i = 0; i < graph->public_count; ++i)
        if (graph->publics[i].block == block &&
            str_ieq_app(graph->publics[i].symbol, symbol))
            return 1;
    if (graph->public_count >= graph->public_capacity) {
        int next_capacity;

        next_capacity = graph->public_capacity == 0 ? 128 :
                        graph->public_capacity * 2;
        graph->publics = (struct AppPublic *)xrealloc(
            graph->publics,
            (size_t)next_capacity * sizeof(*graph->publics));
        graph->public_capacity = next_capacity;
    }
    entry = &graph->publics[graph->public_count++];
    memset(entry, 0, sizeof(*entry));
    entry->block = block;
    strncpy(entry->symbol, symbol, sizeof(entry->symbol) - 1);
    entry->symbol[sizeof(entry->symbol) - 1] = 0;
    return 1;
}

static void collect_publics_from_line(
    struct AppGraph *graph, int block, const char *line)
{
    const char *p;
    char symbol[MAX_SYMBOL];
    int n;

    p = skip_space_app(line);
    if (strncmp(p, "public", 6) != 0 ||
        is_symbol_char((unsigned char)p[6]))
        return;
    p += 6;
    for (;;) {
        p = skip_space_app(p);
        if (!is_symbol_start(*p))
            return;
        n = 0;
        while (is_symbol_char(*p)) {
            if (n + 1 < (int)sizeof(symbol))
                symbol[n++] = *p;
            ++p;
        }
        symbol[n] = 0;
        append_public(graph, block, symbol);
        p = skip_space_app(p);
        if (*p != ',')
            return;
        ++p;
    }
}

static int parse_extern_symbol(
    const char *line, char *symbol, int symbol_size)
{
    const char *p;
    int n;

    p = skip_space_app(line);
    if (strncmp(p, "extrn", 5) != 0 ||
        is_symbol_char((unsigned char)p[5]))
        return 0;
    p = skip_space_app(p + 5);
    if (!is_symbol_start(*p))
        return 0;
    n = 0;
    while (is_symbol_char(*p)) {
        if (n + 1 >= symbol_size)
            return 0;
        symbol[n++] = *p++;
    }
    symbol[n] = 0;
    return 1;
}

static int parse_label_symbol(
    const char *line, char *symbol, int symbol_size)
{
    const char *p;
    int n;

    p = skip_space_app(line);
    if (!is_symbol_start(*p))
        return 0;
    n = 0;
    while (is_symbol_char(*p)) {
        if (n + 1 >= symbol_size)
            return 0;
        symbol[n++] = *p++;
    }
    symbol[n] = 0;
    p = skip_space_app(p);
    return *p == ':';
}

static int append_label(
    struct AppGraph *graph, int file_index, int block, const char *symbol)
{
    struct AppLabel *label;
    int i;

    for (i = 0; i < graph->label_count; ++i)
        if (graph->labels[i].file == file_index &&
            str_ieq_app(graph->labels[i].symbol, symbol))
            return graph->labels[i].block == block;
    if (graph->label_count >= graph->label_capacity) {
        int next_capacity;

        next_capacity = graph->label_capacity == 0 ? 256 :
                        graph->label_capacity * 2;
        graph->labels = (struct AppLabel *)xrealloc(
            graph->labels,
            (size_t)next_capacity * sizeof(*graph->labels));
        graph->label_capacity = next_capacity;
    }
    label = &graph->labels[graph->label_count++];
    memset(label, 0, sizeof(*label));
    label->file = file_index;
    label->block = block;
    strncpy(label->symbol, symbol, sizeof(label->symbol) - 1);
    label->symbol[sizeof(label->symbol) - 1] = 0;
    return 1;
}

static int append_extern(
    struct AppGraph *graph, int file_index, const char *symbol)
{
    struct AppExtern *external;
    int i;

    for (i = 0; i < graph->extern_count; ++i)
        if (graph->externs[i].file == file_index &&
            str_ieq_app(graph->externs[i].symbol, symbol))
            return 1;
    if (graph->extern_count >= graph->extern_capacity) {
        int next_capacity;

        next_capacity = graph->extern_capacity == 0 ? 128 :
                        graph->extern_capacity * 2;
        graph->externs = (struct AppExtern *)xrealloc(
            graph->externs,
            (size_t)next_capacity * sizeof(*graph->externs));
        graph->extern_capacity = next_capacity;
    }
    external = &graph->externs[graph->extern_count++];
    memset(external, 0, sizeof(*external));
    external->file = file_index;
    strncpy(external->symbol, symbol, sizeof(external->symbol) - 1);
    external->symbol[sizeof(external->symbol) - 1] = 0;
    return 1;
}

static int append_line(struct AppFile *file, const char *text)
{
    if (file->line_count >= file->line_capacity) {
        int next_capacity;

        next_capacity = file->line_capacity == 0 ? 256 :
                        file->line_capacity * 2;
        file->lines = (struct AppLine *)xrealloc(
            file->lines,
            (size_t)next_capacity * sizeof(*file->lines));
        file->line_capacity = next_capacity;
    }
    file->lines[file->line_count].text = xstrdup_app(text);
    file->lines[file->line_count].block = -1;
    ++file->line_count;
    return 1;
}

static int read_app_file(struct AppFile *file)
{
    FILE *in;
    char buffer[1024];

    in = fopen(file->path, "rb");
    if (in == NULL) {
        perror(file->path);
        return 0;
    }
    while (fgets(buffer, sizeof(buffer), in) != NULL) {
        if (strchr(buffer, '\n') == NULL && !feof(in)) {
            fprintf(stderr, "dccrtlstrip: %s: assembly line too long\n",
                    file->path);
            fclose(in);
            return 0;
        }
        append_line(file, buffer);
    }
    if (ferror(in)) {
        perror(file->path);
        fclose(in);
        return 0;
    }
    fclose(in);
    return 1;
}

static struct AppBlock *append_block(struct AppGraph *graph)
{
    struct AppBlock *block;

    if (graph->block_count >= graph->block_capacity) {
        int next_capacity;

        next_capacity = graph->block_capacity == 0 ? 128 :
                        graph->block_capacity * 2;
        graph->blocks = (struct AppBlock *)xrealloc(
            graph->blocks,
            (size_t)next_capacity * sizeof(*graph->blocks));
        graph->block_capacity = next_capacity;
    }
    block = &graph->blocks[graph->block_count++];
    memset(block, 0, sizeof(*block));
    return block;
}

static int build_blocks_for_file(struct AppGraph *graph, int file_index)
{
    struct AppFile *file;
    struct AppBlock *open_block;
    char symbol[MAX_SYMBOL];
    int open_index;
    int i;
    int result;

    file = &graph->files[file_index];
    open_block = NULL;
    open_index = -1;
    for (i = 0; i < file->line_count; ++i) {
        if (parse_extern_symbol(
                file->lines[i].text, symbol, sizeof(symbol)))
            append_extern(graph, file_index, symbol);
        result = parse_marker(
            file->lines[i].text, MARK_BEGIN, symbol, sizeof(symbol));
        if (result < 0) {
            fprintf(stderr, "dccrtlstrip: %s:%d: malformed LTO marker\n",
                    file->path, i + 1);
            return 0;
        }
        if (result > 0) {
            if (open_block != NULL) {
                fprintf(stderr,
                        "dccrtlstrip: %s:%d: nested LTO block %s\n",
                        file->path, i + 1, symbol);
                return 0;
            }
            open_block = append_block(graph);
            open_index = graph->block_count - 1;
            open_block->file = file_index;
            open_block->start = i;
            strncpy(open_block->symbol, symbol,
                    sizeof(open_block->symbol) - 1);
            open_block->symbol[sizeof(open_block->symbol) - 1] = 0;
        }
        if (open_block != NULL) {
            file->lines[i].block = open_index;
            if (parse_label_symbol(
                    file->lines[i].text, symbol, sizeof(symbol)) &&
                !append_label(graph, file_index, open_index, symbol)) {
                fprintf(stderr,
                        "dccrtlstrip: %s:%d: duplicate LTO label %s\n",
                        file->path, i + 1, symbol);
                return 0;
            }
            collect_publics_from_line(
                graph, open_index, file->lines[i].text);
        }
        result = parse_marker(
            file->lines[i].text, MARK_END, symbol, sizeof(symbol));
        if (result < 0) {
            fprintf(stderr, "dccrtlstrip: %s:%d: malformed LTO marker\n",
                    file->path, i + 1);
            return 0;
        }
        if (result > 0) {
            if (open_block == NULL ||
                !str_ieq_app(open_block->symbol, symbol)) {
                fprintf(stderr,
                        "dccrtlstrip: %s:%d: unmatched LTO end %s\n",
                        file->path, i + 1, symbol);
                return 0;
            }
            open_block->end = i + 1;
            open_block = NULL;
            open_index = -1;
        }
    }
    if (open_block != NULL) {
        fprintf(stderr, "dccrtlstrip: %s: unterminated LTO block %s\n",
                file->path, open_block->symbol);
        return 0;
    }
    return 1;
}

static int find_local_block(
    const struct AppGraph *graph, int file_index, const char *symbol)
{
    int i;

    for (i = 0; i < graph->block_count; ++i)
        if (graph->blocks[i].file == file_index &&
            str_ieq_app(graph->blocks[i].symbol, symbol))
            return i;
    for (i = 0; i < graph->label_count; ++i)
        if (graph->labels[i].file == file_index &&
            str_ieq_app(graph->labels[i].symbol, symbol))
            return graph->labels[i].block;
    return -1;
}

static int find_public_block(
    const struct AppGraph *graph, const char *symbol)
{
    int found;
    int i;

    found = -1;
    for (i = 0; i < graph->public_count; ++i) {
        if (!str_ieq_app(graph->publics[i].symbol, symbol))
            continue;
        if (found >= 0) {
            fprintf(stderr,
                    "dccrtlstrip: duplicate public LTO symbol %s\n", symbol);
            return -2;
        }
        found = graph->publics[i].block;
    }
    return found;
}

static int keep_symbol(
    struct AppGraph *graph, int from_file, const char *symbol)
{
    int block;

    block = find_local_block(graph, from_file, symbol);
    if (block < 0)
        block = find_public_block(graph, symbol);
    if (block == -2)
        return -1;
    if (block >= 0 && !graph->blocks[block].keep) {
        graph->blocks[block].keep = 1;
        return 1;
    }
    return 0;
}

static void mark_needed_extern(
    struct AppGraph *graph, int file_index, const char *symbol)
{
    int i;

    for (i = 0; i < graph->extern_count; ++i)
        if (graph->externs[i].file == file_index &&
            str_ieq_app(graph->externs[i].symbol, symbol))
            graph->externs[i].needed = 1;
}

static int scan_reference_line(
    struct AppGraph *graph, int from_file, const char *line)
{
    const char *p;
    const char *first;
    char symbol[MAX_SYMBOL];
    int changed;
    int n;
    int result;

    changed = 0;
    p = skip_space_app(line);
    first = p;
    while (is_symbol_char(*p))
        ++p;
    if ((p - first == 5 &&
         strncmp(first, "extrn", 5) == 0) ||
        (p - first == 6 &&
         strncmp(first, "public", 6) == 0))
        return 0;
    p = line;
    while (*p && *p != ';') {
        if (!is_symbol_start(*p)) {
            ++p;
            continue;
        }
        n = 0;
        while (is_symbol_char(*p)) {
            if (n + 1 < (int)sizeof(symbol))
                symbol[n++] = *p;
            ++p;
        }
        symbol[n] = 0;
        mark_needed_extern(graph, from_file, symbol);
        result = keep_symbol(graph, from_file, symbol);
        if (result < 0)
            return -1;
        if (result > 0)
            changed = 1;
    }
    return changed;
}

static int mark_reachable_apps(
    struct AppGraph *graph, int root_count, char **roots)
{
    int changed;
    int result;
    int b;
    int f;
    int i;

    for (i = 0; i < root_count; ++i) {
        result = keep_symbol(graph, 0, roots[i]);
        if (result < 0)
            return 0;
    }

    for (f = 0; f < graph->file_count; ++f) {
        for (i = 0; i < graph->files[f].line_count; ++i) {
            if (graph->files[f].lines[i].block >= 0)
                continue;
            result = scan_reference_line(
                graph, f, graph->files[f].lines[i].text);
            if (result < 0)
                return 0;
        }
    }

    do {
        changed = 0;
        for (b = 0; b < graph->block_count; ++b) {
            struct AppBlock *block;

            block = &graph->blocks[b];
            if (!block->keep || block->scanned)
                continue;
            block->scanned = 1;
            for (i = block->start; i < block->end; ++i) {
                result = scan_reference_line(
                    graph, block->file,
                    graph->files[block->file].lines[i].text);
                if (result < 0)
                    return 0;
                if (result > 0)
                    changed = 1;
            }
        }
    } while (changed);
    return 1;
}

static int write_temp_files(struct AppGraph *graph)
{
    int f;

    for (f = 0; f < graph->file_count; ++f) {
        struct AppFile *file;
        FILE *out;
        size_t path_len;
        int i;

        file = &graph->files[f];
        path_len = strlen(file->path);
        file->temp_path = (char *)malloc(path_len + 16);
        if (file->temp_path == NULL) {
            fprintf(stderr, "dccrtlstrip: out of memory\n");
            return 0;
        }
        sprintf(file->temp_path, "%s.lto.tmp", file->path);
        out = fopen(file->temp_path, "wb");
        if (out == NULL) {
            perror(file->temp_path);
            return 0;
        }
        for (i = 0; i < file->line_count; ++i) {
            int block;
            char external_symbol[MAX_SYMBOL];

            block = file->lines[i].block;
            if (parse_extern_symbol(
                    file->lines[i].text, external_symbol,
                    sizeof(external_symbol))) {
                int external;
                int needed;

                needed = 0;
                for (external = 0;
                     external < graph->extern_count; ++external) {
                    if (graph->externs[external].file == f &&
                        graph->externs[external].needed &&
                        str_ieq_app(
                            graph->externs[external].symbol,
                            external_symbol)) {
                        needed = 1;
                        break;
                    }
                }
                if (!needed)
                    continue;
            }
            if (block >= 0 && !graph->blocks[block].keep) {
                if (!parse_extern_symbol(
                        file->lines[i].text, external_symbol,
                        sizeof(external_symbol)))
                    continue;
            }
            if (fputs(file->lines[i].text, out) == EOF) {
                perror(file->temp_path);
                fclose(out);
                return 0;
            }
        }
        if (fclose(out) != 0) {
            perror(file->temp_path);
            return 0;
        }
    }
    return 1;
}

static int install_temp_files(struct AppGraph *graph)
{
    int f;
    int restored;

    for (f = 0; f < graph->file_count; ++f) {
        struct AppFile *file;
        size_t path_len;
        int j;

        file = &graph->files[f];
        path_len = strlen(file->path);
        file->backup_path = (char *)malloc(path_len + 16);
        if (file->backup_path == NULL) {
            fprintf(stderr, "dccrtlstrip: out of memory\n");
            for (j = 0; j < f; ++j)
                rename(graph->files[j].backup_path,
                       graph->files[j].path);
            return 0;
        }
        sprintf(file->backup_path, "%s.lto.bak", file->path);
        remove(file->backup_path);
        if (rename(file->path, file->backup_path) != 0) {
            perror(file->path);
            for (j = 0; j < f; ++j)
                rename(graph->files[j].backup_path,
                       graph->files[j].path);
            return 0;
        }
    }
    for (f = 0; f < graph->file_count; ++f) {
        if (rename(graph->files[f].temp_path, graph->files[f].path) != 0) {
            int j;

            perror(graph->files[f].path);
            for (j = 0; j < f; ++j)
                remove(graph->files[j].path);
            restored = 1;
            for (j = 0; j < graph->file_count; ++j)
                if (rename(graph->files[j].backup_path,
                           graph->files[j].path) != 0) {
                    perror(graph->files[j].backup_path);
                    restored = 0;
                }
            if (!restored)
                fprintf(stderr,
                        "dccrtlstrip: restore failed; original .lto.bak files retained\n");
            return 0;
        }
    }
    for (f = 0; f < graph->file_count; ++f)
        remove(graph->files[f].backup_path);
    return 1;
}

static void free_graph(struct AppGraph *graph)
{
    int f;
    int i;

    for (f = 0; f < graph->file_count; ++f) {
        for (i = 0; i < graph->files[f].line_count; ++i)
            free(graph->files[f].lines[i].text);
        free(graph->files[f].lines);
        if (graph->files[f].temp_path != NULL) {
            remove(graph->files[f].temp_path);
            free(graph->files[f].temp_path);
        }
        free(graph->files[f].backup_path);
    }
    free(graph->files);
    free(graph->blocks);
    free(graph->externs);
    free(graph->labels);
    free(graph->publics);
}

int dcc_strip_app_files(
    int file_count, char **paths, int root_count, char **roots)
{
    struct AppGraph graph;
    int kept;
    int f;
    int i;
    int ok;

    memset(&graph, 0, sizeof(graph));
    graph.file_count = file_count;
    graph.files = (struct AppFile *)calloc(
        (size_t)file_count, sizeof(*graph.files));
    if (graph.files == NULL) {
        fprintf(stderr, "dccrtlstrip: out of memory\n");
        return 0;
    }

    ok = 1;
    for (f = 0; f < file_count && ok; ++f) {
        graph.files[f].path = paths[f];
        ok = read_app_file(&graph.files[f]) &&
             build_blocks_for_file(&graph, f);
    }
    if (ok)
        ok = mark_reachable_apps(&graph, root_count, roots);
    if (ok)
        ok = write_temp_files(&graph);
    if (ok)
        ok = install_temp_files(&graph);

    kept = 0;
    for (i = 0; i < graph.block_count; ++i)
        if (graph.blocks[i].keep)
            ++kept;
    if (ok)
        fprintf(stderr,
                "dccrtlstrip: app LTO kept %d/%d blocks, removed %d\n",
                kept, graph.block_count, graph.block_count - kept);
    free_graph(&graph);
    return ok;
}
