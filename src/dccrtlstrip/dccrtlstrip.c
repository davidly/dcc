/*
 * dccrtlstrip.c - conservative pre-link reducer for dccrtl.mac
 *
 * Usage:
 *   dccrtlstrip [-k symbol ...] -r dccrtl.mac -o rtlmin.mac app.mac [app2.mac ...]
 *
 * This version treats PUBLIC directives as runtime block boundaries, not
 * arbitrary labels.  That is important for routines like _printf whose body
 * contains private labels/data such as __pf_run, pf_sink, etc.  Splitting on
 * every label can keep only the entry stub and strip the real body.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINES   60000
#define MAX_BLOCKS   4096
#define MAX_SYMS    20000
#define MAX_ROOTS   20000
#define MAX_LINE      512
#define MAX_SCAN_SYMS 60000
#define SCAN_HASH_SIZE 65521

struct Line {
    char *s;
};

struct Block {
    int start;
    int end;
    int keep;
    int scanned;    /* has add_refs_from_line already run over [start,end)?
                      * a line's refs never change, so this need happen once. */
    int dep;        /* block to keep when this block is kept, or -1 */
    char name[128];
};

struct SymMap {
    char name[128];
    int block;
};

static struct Line lines[MAX_LINES];
static int nlines;

static struct Block blocks[MAX_BLOCKS];
static int nblocks;

static struct SymMap syms[MAX_SYMS];
static int nsyms;

/* Comprehensive name->block index covering every label and PUBLIC-mentioned
 * name in every already-classified block, built once by build_scan_index()
 * so find_sym_block's not-in-syms[] case is an O(1) lookup instead of the
 * O(nlines) rescan find_symbol_block_by_scan used to do on every call. */
static struct SymMap scan_syms[MAX_SCAN_SYMS];
static int scan_next[MAX_SCAN_SYMS];
static int scan_buckets[SCAN_HASH_SIZE];
static int n_scan_syms;

static char roots[MAX_ROOTS][128];
static int nroots;

static char *xstrdup2(const char *s)
{
    char *p = (char *)malloc(strlen(s) + 1);
    if (!p) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    strcpy(p, s);
    return p;
}

static void rtrim(char *s)
{
    int n = (int)strlen(s);
    while (n > 0 && (s[n-1] == '\n' || s[n-1] == '\r' ||
                     s[n-1] == ' ' || s[n-1] == '\t'))
        s[--n] = 0;
}

static const char *skipws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static void strip_comment_copy(const char *in, char *out, int outsz)
{
    int i = 0;
    while (*in && *in != ';' && i < outsz - 1)
        out[i++] = *in++;
    out[i] = 0;
    rtrim(out);
}

static int is_ident_start(int c)
{
    return isalpha((unsigned char)c) || c == '_' || c == '?' || c == '.';
}

static int is_ident_char2(int c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '?' || c == '.' || c == '$';
}

static int parse_ident_token(const char **pp, char *out)
{
    const char *p = skipws(*pp);
    int i = 0;

    if (!is_ident_start((unsigned char)*p))
        return 0;

    while (is_ident_char2((unsigned char)*p) && i < 127)
        out[i++] = *p++;
    out[i] = 0;
    *pp = p;
    return i > 0;
}

static int is_number_token(const char *s)
{
    if (*s == 0) return 0;
    if (*s == '-' || *s == '+') s++;
    if (*s == 0) return 0;
    if (*s == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        if (!isxdigit((unsigned char)*s)) return 0;
        while (isxdigit((unsigned char)*s)) s++;
    } else {
        if (!isdigit((unsigned char)*s)) return 0;
        while (isdigit((unsigned char)*s)) s++;
    }
    return *s == 0 || *s == 'h' || *s == 'H';
}

static int is_register_name(const char *s)
{
    static const char *regs[] = {
        "a","b","c","d","e","h","l","af","bc","de","hl","sp","ix","iy",
        "nz","z","nc","pe","po","p","m","c", NULL
    };
    int i;
    for (i = 0; regs[i]; ++i)
        if (!strcmp(s, regs[i]))
            return 1;
    return 0;
}

static int ci_strncmp(const char *a, const char *b, int n)
{
    int ca, cb;
    while (n-- > 0) {
        ca = tolower((unsigned char)*a++);
        cb = tolower((unsigned char)*b++);
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
    return 0;
}

static void add_root(const char *name)
{
    int i;
    if (!name || !name[0]) return;
    if (is_number_token(name) || is_register_name(name)) return;

    for (i = 0; i < nroots; ++i)
        if (!strcmp(roots[i], name))
            return;
    if (nroots >= MAX_ROOTS) {
        fprintf(stderr, "too many roots\n");
        exit(1);
    }
    strncpy(roots[nroots], name, sizeof(roots[nroots]) - 1);
    roots[nroots][sizeof(roots[nroots]) - 1] = 0;
    nroots++;
}

static void add_sym(const char *name, int block)
{
    int i;
    if (!name || !name[0]) return;
    for (i = 0; i < nsyms; ++i) {
        if (!strcmp(syms[i].name, name)) {
            /* Prefer the earliest block that actually owns the PUBLIC. */
            if (syms[i].block < 0)
                syms[i].block = block;
            return;
        }
    }
    if (nsyms >= MAX_SYMS) {
        fprintf(stderr, "too many symbols\n");
        exit(1);
    }
    strncpy(syms[nsyms].name, name, sizeof(syms[nsyms].name) - 1);
    syms[nsyms].name[sizeof(syms[nsyms].name) - 1] = 0;
    syms[nsyms].block = block;
    nsyms++;
}

static int parse_label(const char *line, char *lab);

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static int line_label_is(const char *line, const char *name)
{
    char lab[128];
    return parse_label(line, lab) && str_ieq(lab, name);
}

static unsigned scan_hash(const char *name)
{
    unsigned h = 0;
    while (*name) {
        h = h * 131u + (unsigned char)tolower((unsigned char)*name);
        name++;
    }
    return h % SCAN_HASH_SIZE;
}

static void scan_index_insert(const char *name, int block)
{
    unsigned h;
    int i;

    if (!name || !name[0])
        return;
    h = scan_hash(name);
    for (i = scan_buckets[h]; i >= 0; i = scan_next[i]) {
        if (str_ieq(scan_syms[i].name, name))
            return; /* first occurrence wins, matching the original
                      * block-then-line scan order below */
    }
    if (n_scan_syms >= MAX_SCAN_SYMS) {
        fprintf(stderr, "too many symbols\n");
        exit(1);
    }
    strncpy(scan_syms[n_scan_syms].name, name, sizeof(scan_syms[n_scan_syms].name) - 1);
    scan_syms[n_scan_syms].name[sizeof(scan_syms[n_scan_syms].name) - 1] = 0;
    scan_syms[n_scan_syms].block = block;
    scan_next[n_scan_syms] = scan_buckets[h];
    scan_buckets[h] = n_scan_syms;
    n_scan_syms++;
}

static int scan_index_lookup(const char *name)
{
    unsigned h = scan_hash(name);
    int i;

    for (i = scan_buckets[h]; i >= 0; i = scan_next[i]) {
        if (str_ieq(scan_syms[i].name, name))
            return scan_syms[i].block;
    }
    return -1;
}

/* Builds, once, the same name->block mapping find_symbol_block_by_scan used
 * to recompute from scratch on every call: every label (parse_label) and
 * every PUBLIC-mentioned name in every already-classified block. This is
 * deliberately independent of the symbol table built by build_blocks(), so
 * an app EXTRN can still keep a runtime block even if PUBLIC parsing missed
 * that symbol or case differs after M80/L80 folding. Blocks/lines are
 * walked in the same order the old per-query scan used, and
 * scan_index_insert keeps only the first occurrence of a name, so lookups
 * are identical to what that scan would have returned - just O(1) instead
 * of O(nlines) per query. */
static void build_scan_index(void)
{
    int b, i;

    for (i = 0; i < SCAN_HASH_SIZE; ++i)
        scan_buckets[i] = -1;
    n_scan_syms = 0;

    for (b = 0; b < nblocks; ++b) {
        for (i = blocks[b].start; i < blocks[b].end; ++i) {
            const char *line = lines[i].s;
            char clean[MAX_LINE];
            const char *p;
            char sym[128];
            char lab[128];

            if (parse_label(line, lab))
                scan_index_insert(lab, b);

            strip_comment_copy(line, clean, sizeof(clean));
            p = skipws(clean);
            if (ci_strncmp(p, "public", 6) != 0 ||
                !(p[6] == ' ' || p[6] == '\t'))
                continue;
            p += 6;
            for (;;) {
                p = skipws(p);
                if (!parse_ident_token(&p, sym))
                    break;
                scan_index_insert(sym, b);
                p = skipws(p);
                if (*p == ',') {
                    p++;
                    continue;
                }
                break;
            }
        }
    }
}

static int find_sym_block(const char *name)
{
    int i;
    for (i = 0; i < nsyms; ++i)
        if (!strcmp(syms[i].name, name) || str_ieq(syms[i].name, name))
            return syms[i].block;
    return scan_index_lookup(name);
}

static int parse_label(const char *line, char *lab)
{
    const char *p;
    int i;

    p = skipws(line);
    if (!is_ident_start((unsigned char)*p))
        return 0;

    i = 0;
    while (is_ident_char2((unsigned char)*p) && i < 127)
        lab[i++] = *p++;
    lab[i] = 0;

    p = skipws(p);
    return *p == ':';
}

static void collect_publics_from_line(const char *line, int block)
{
    char clean[MAX_LINE];
    const char *p;
    char sym[128];

    strip_comment_copy(line, clean, sizeof(clean));
    p = skipws(clean);
    if (ci_strncmp(p, "public", 6) != 0 ||
        !(p[6] == ' ' || p[6] == '\t'))
        return;
    p += 6;

    for (;;) {
        p = skipws(p);
        if (!parse_ident_token(&p, sym))
            break;
        add_sym(sym, block);
        p = skipws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        break;
    }
}

static int is_public_line(const char *line)
{
    char clean[MAX_LINE];
    const char *p;
    strip_comment_copy(line, clean, sizeof(clean));
    p = skipws(clean);
    return ci_strncmp(p, "public", 6) == 0 && (p[6] == ' ' || p[6] == '\t');
}

static void read_runtime(const char *fn)
{
    FILE *f;
    char buf[MAX_LINE];

    f = fopen(fn, "r");
    if (!f) {
        perror(fn);
        exit(1);
    }

    while (fgets(buf, sizeof(buf), f)) {
        if (nlines >= MAX_LINES) {
            fprintf(stderr, "too many runtime lines\n");
            exit(1);
        }
        rtrim(buf);
        lines[nlines++].s = xstrdup2(buf);
    }
    fclose(f);
}

/* Build blocks using PUBLIC directives as boundaries.  Lines before the
 * first PUBLIC are kept as a preamble.  Each PUBLIC block includes its PUBLIC
 * directive and all following private labels/data until the next PUBLIC.
 */
static int collect_public_names_from_range(int first, int last, char names[][128], int maxnames)
{
    int i;
    int n;
    char clean[MAX_LINE];
    const char *p;
    char sym[128];

    n = 0;
    for (i = first; i < last; ++i) {
        strip_comment_copy(lines[i].s, clean, sizeof(clean));
        p = skipws(clean);
        if (ci_strncmp(p, "public", 6) != 0 ||
            !(p[6] == ' ' || p[6] == '\t'))
            continue;
        p += 6;
        for (;;) {
            p = skipws(p);
            if (!parse_ident_token(&p, sym))
                break;
            if (n < maxnames) {
                strncpy(names[n], sym, 127);
                names[n][127] = 0;
                n++;
            }
            p = skipws(p);
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
    }
    return n;
}

static int name_in_list(const char *name, char names[][128], int n)
{
    int i;
    for (i = 0; i < n; ++i)
        if (str_ieq(name, names[i]))
            return 1;
    return 0;
}

static int find_public_label_in_range(const char *name, int first, int last)
{
    int i;
    for (i = first; i < last; ++i) {
        if (line_label_is(lines[i].s, name))
            return i;
    }
    return -1;
}

static void sort_ints(int *v, int n)
{
    int i, j;
    for (i = 0; i < n; ++i) {
        for (j = i + 1; j < n; ++j) {
            if (v[j] < v[i]) {
                int t = v[i];
                v[i] = v[j];
                v[j] = t;
            }
        }
    }
}

static void start_new_block_at(int i, int *curp)
{
    int cur;

    if (*curp >= 0)
        blocks[*curp].end = i;

    if (nblocks >= MAX_BLOCKS) {
        fprintf(stderr, "too many blocks\n");
        exit(1);
    }

    cur = nblocks++;
    memset(&blocks[cur], 0, sizeof(blocks[cur]));
    blocks[cur].dep = -1;
    blocks[cur].start = i;
    blocks[cur].end = nlines;
    sprintf(blocks[cur].name, "block%d", cur);
    *curp = cur;
}

/* Is `line` inert as far as splitting label starts is concerned - blank,
 * a comment, a PUBLIC directive, or a bare "label:" with nothing else on
 * the line? Used to detect a run of alias labels that share one
 * fallthrough body further down (e.g. DCCRTL.MAC's time stubs:
 * _asctime:/_ctime:/_gmtime:/__ltim: all immediately precede the single
 * "ld hl,0 / ret" body they share). A label line with code after the
 * colon, or an EQU/data line, is real content and returns 0. */
static int line_is_bare_label_or_inert(const char *line)
{
    char clean[MAX_LINE];
    char lab[128];
    const char *p;
    const char *colon;

    if (is_public_line(line))
        return 1;

    strip_comment_copy(line, clean, sizeof(clean));
    p = skipws(clean);
    if (*p == 0)
        return 1;

    if (!parse_label(clean, lab))
        return 0;

    colon = strchr(clean, ':');
    if (!colon)
        return 0;
    return *skipws(colon + 1) == 0;
}

/* Do the lines in [a, b) contain nothing but alias labels/comments/blanks -
 * i.e. no line here is itself the start of that label's real body? */
static int range_is_alias_only(int a, int b)
{
    int i;
    for (i = a; i < b; ++i)
        if (!line_is_bare_label_or_inert(lines[i].s))
            return 0;
    return 1;
}

static void build_blocks(void)
{
    int i;
    int run_start;
    char names[128][128];

    nblocks = 0;

    i = 0;
    while (i < nlines) {
        int run_end;
        int next_run;
        int npublic;
        int starts[128];
        int nstarts;
        int prelude_block;
        int bi;
        int j;

        if (!is_public_line(lines[i].s)) {
            i++;
            continue;
        }

        /*
         * A run of consecutive PUBLIC directives often declares several entry
         * points before any code/EQU lines.  The old stripper made the whole
         * run and all following routines one block, so using _open kept _read,
         * _write, _lseek, stdio, etc.  Instead:
         *
         *   - make a small prelude block for the PUBLIC run and shared EQU/data
         *     before the first public label;
         *   - split at the actual public labels found later in the run's body;
         *   - make each split block depend on the prelude, so the PUBLIC lines
         *     and shared constants are preserved when any entry is kept.
         */
        run_start = i;
        run_end = i;
        while (run_end < nlines && is_public_line(lines[run_end].s))
            run_end++;

        next_run = run_end;
        while (next_run < nlines && !is_public_line(lines[next_run].s))
            next_run++;

        npublic = collect_public_names_from_range(run_start, run_end, names, 128);

        nstarts = 0;
        for (j = 0; j < npublic; ++j) {
            int li;
            li = find_public_label_in_range(names[j], run_end, next_run);
            if (li >= 0 && nstarts < 128)
                starts[nstarts++] = li;
        }
        sort_ints(starts, nstarts);

        /*
         * Merge a run of alias labels that share one fallthrough body with
         * no real code of their own (e.g. DCCRTL.MAC's time stubs -
         * _asctime:/_ctime:/_gmtime:/__ltim: all immediately precede the
         * single "ld hl,0 / ret" body they share) back into one block.
         * Without this, splitting at every label boundary gives every
         * alias but the last an EMPTY block - falling through into
         * whatever the stripper decided to emit next - since the actual
         * body only lives in the range belonging to the final label.
         */
        if (nstarts > 1) {
            int merged[128];
            int nmerged;
            int k;

            nmerged = 0;
            j = 0;
            while (j < nstarts) {
                int group_start = starts[j];
                while (j + 1 < nstarts &&
                       range_is_alias_only(starts[j], starts[j + 1]))
                    j++;
                if (nmerged < 128)
                    merged[nmerged++] = group_start;
                j++;
            }
            for (k = 0; k < nmerged; ++k)
                starts[k] = merged[k];
            nstarts = nmerged;
        }

        if (nstarts <= 1) {
            bi = -1;
            start_new_block_at(run_start, &bi);
            blocks[bi].end = next_run;
            for (j = run_start; j < next_run; ++j) {
                char lab[128];
                collect_publics_from_line(lines[j].s, bi);
                if (parse_label(lines[j].s, lab))
                    add_sym(lab, bi);
            }
            i = next_run;
            continue;
        }

        /* Prelude: PUBLIC run plus lines before the first public label. */
        prelude_block = -1;
        start_new_block_at(run_start, &prelude_block);
        blocks[prelude_block].end = starts[0];
        strcpy(blocks[prelude_block].name, "public_prelude");
        for (j = run_start; j < starts[0]; ++j) {
            char lab[128];
            if (parse_label(lines[j].s, lab))
                add_sym(lab, prelude_block);
        }

        for (j = 0; j < nstarts; ++j) {
            int k;
            int block_start;
            int block_end;

            block_start = starts[j];
            block_end = (j + 1 < nstarts) ? starts[j + 1] : next_run;

            bi = -1;
            start_new_block_at(block_start, &bi);
            blocks[bi].end = block_end;
            blocks[bi].dep = prelude_block;

            for (k = block_start; k < block_end; ++k) {
                char lab[128];
                if (parse_label(lines[k].s, lab)) {
                    add_sym(lab, bi);
                    if (name_in_list(lab, names, npublic))
                        add_sym(lab, bi);
                }
                collect_publics_from_line(lines[k].s, bi);
            }
        }

        /*
         * PUBLIC symbols whose labels were not found are mapped to the prelude.
         * This is safer than losing them, and usually means they are data or
         * aliases near the declaration run.
         */
        for (j = 0; j < npublic; ++j) {
            if (find_public_label_in_range(names[j], run_end, next_run) < 0)
                add_sym(names[j], prelude_block);
        }

        i = next_run;
    }
}


static void add_refs_from_line(const char *line)
{
    char clean[MAX_LINE];
    char op[64], sym[128];
    const char *p;
    int i;

    strip_comment_copy(line, clean, sizeof(clean));
    p = skipws(clean);
    if (*p == 0)
        return;

    /* Skip labels at start. */
    if (parse_label(p, sym)) {
        char *colon = strchr(clean, ':');
        if (!colon) return;
        p = skipws(colon + 1);
        if (*p == 0)
            return;
    }

    if (!parse_ident_token(&p, op))
        return;

    for (i = 0; op[i]; ++i)
        op[i] = (char)tolower((unsigned char)op[i]);

    if (!strcmp(op, "extrn") || !strcmp(op, "public")) {
        for (;;) {
            p = skipws(p);
            if (!parse_ident_token(&p, sym))
                break;
            if (!strcmp(op, "extrn"))
                add_root(sym);
            p = skipws(p);
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        return;
    }

    if (!strcmp(op, "call") || !strcmp(op, "jp") || !strcmp(op, "jr")) {
        /* jp z,label has condition first. */
        p = skipws(p);
        if (parse_ident_token(&p, sym)) {
            p = skipws(p);
            if (*p == ',') {
                p++;
                if (parse_ident_token(&p, sym))
                    add_root(sym);
            } else {
                add_root(sym);
            }
        }
        return;
    }

    if (!strcmp(op, "dw")) {
        for (;;) {
            p = skipws(p);
            if (parse_ident_token(&p, sym))
                add_root(sym);
            else {
                while (*p && *p != ',') p++;
            }
            p = skipws(p);
            if (*p == ',') {
                p++;
                continue;
            }
            break;
        }
        return;
    }

    if (!strcmp(op, "ld")) {
        /* catch ld hl,sym / ld de,sym / ld (addr),hl-ish symbol uses */
        const char *q = strchr(p, ',');
        if (q) {
            q++;
            q = skipws(q);
            /* ld hl,(sym) / ld a,(sym) loads from memory at sym: the symbol
             * is a reference even though it is wrapped in parentheses. */
            if (*q == '(')
                q++;
            if (parse_ident_token(&q, sym))
                add_root(sym);
        }
        /* Also catch ld (sym),hl */
        p = skipws(p);
        if (*p == '(') {
            p++;
            if (parse_ident_token(&p, sym))
                add_root(sym);
        }
        return;
    }
}


static int symbol_mentioned_in_line(const char *line, const char *sym)
{
    char clean[MAX_LINE];
    const char *p;
    int n;

    strip_comment_copy(line, clean, sizeof(clean));
    n = (int)strlen(sym);
    if (n <= 0)
        return 0;

    p = clean;
    while ((p = strstr(p, sym)) != NULL) {
        int before_ok;
        int after_ok;
        before_ok = (p == clean) || !is_ident_char2((unsigned char)p[-1]);
        after_ok = !is_ident_char2((unsigned char)p[n]);
        if (before_ok && after_ok)
            return 1;
        p++;
    }
    return 0;
}

static void add_known_runtime_refs_from_line(const char *line)
{
    int i;

    /*
     * Fallback root scan: after the runtime has been parsed, every PUBLIC and
     * internal label is known.  Some compiler output uses symbols in forms not
     * covered by the small opcode parser above, or may contain extra spacing /
     * macro-like constructs.  If an app line mentions an exact runtime symbol,
     * keep that symbol's owning block.  This is intentionally conservative and
     * fixes cases such as the float-printf entry _pffio being emitted by dcc but
     * omitted from rtlmin.mac.
     */
    for (i = 0; i < nsyms; ++i) {
        if (symbol_mentioned_in_line(line, syms[i].name))
            add_root(syms[i].name);
    }
}

static void scan_app(const char *fn)
{
    FILE *f;
    char buf[MAX_LINE];

    f = fopen(fn, "r");
    if (!f) {
        perror(fn);
        exit(1);
    }
    while (fgets(buf, sizeof(buf), f)) {
        rtrim(buf);
        add_refs_from_line(buf);
        add_known_runtime_refs_from_line(buf);
    }
    fclose(f);
}

static int keep_block_for_symbol(const char *name)
{
    int b = find_sym_block(name);
    int changed;

    changed = 0;
    if (b >= 0 && !blocks[b].keep) {
        blocks[b].keep = 1;
        changed = 1;
    }
    if (b >= 0 && blocks[b].dep >= 0 && !blocks[blocks[b].dep].keep) {
        blocks[blocks[b].dep].keep = 1;
        changed = 1;
    }
    return changed;
}

static void mark_reachable(void)
{
    int changed;
    int i, b, pass_start, pass_end;
    int roots_done;

    add_root("start");
    roots_done = 0;

    /* Worklist fixed point: a root's block mapping and a line's extracted
     * refs are both pure functions of static data (the symbol table/blocks
     * built once in build_blocks, and the line text), so re-resolving a root
     * or re-scanning a block already processed in an earlier pass can only
     * ever repeat the same answer. Track how far each has progressed so
     * every root is resolved once and every kept block's lines are scanned
     * for refs once, rather than the whole accumulated roots[]/blocks[] on
     * every pass - this was previously O(passes * (nroots + kept lines)),
     * dominated by find_sym_block's O(nlines) not-found fallback scan
     * repeating for the same roots pass after pass (profiled: >80% of
     * dccrtlstrip's runtime on a large app like cobint/tchess). */
    do {
        changed = 0;

        for (i = roots_done; i < nroots; ++i)
            if (keep_block_for_symbol(roots[i]))
                changed = 1;
        roots_done = nroots;

        for (b = 0; b < nblocks; ++b) {
            if (!blocks[b].keep)
                continue;
            if (blocks[b].dep >= 0 && !blocks[blocks[b].dep].keep) {
                blocks[blocks[b].dep].keep = 1;
                changed = 1;
            }
            if (blocks[b].scanned)
                continue;
            blocks[b].scanned = 1;

            pass_start = blocks[b].start;
            pass_end = blocks[b].end;
            for (i = pass_start; i < pass_end; ++i) {
                int before = nroots;
                add_refs_from_line(lines[i].s);
                if (nroots != before)
                    changed = 1;
            }
        }
    } while (changed);
}


static int public_symbol_is_kept(const char *name)
{
    int b;

    b = find_sym_block(name);
    return b >= 0 && blocks[b].keep;
}

static void write_filtered_public_line(FILE *f, const char *line)
{
    char clean[MAX_LINE];
    const char *p;
    char sym[128];
    int wrote;
    char indent[64];
    int ii;

    strip_comment_copy(line, clean, sizeof(clean));
    p = skipws(clean);
    if (ci_strncmp(p, "public", 6) != 0 ||
        !(p[6] == ' ' || p[6] == '\t')) {
        fprintf(f, "%s\n", line);
        return;
    }

    /*
     * Finer splitting can keep a PUBLIC prelude while stripping some bodies
     * declared in that prelude.  M80 then prints "U public _foo" for each
     * PUBLIC whose label is absent.  Re-emit only PUBLIC symbols whose owning
     * block is kept.
     */
    ii = 0;
    while (line[ii] == ' ' || line[ii] == '\t') {
        if (ii < (int)sizeof(indent) - 1)
            indent[ii] = line[ii];
        ii++;
    }
    if (ii >= (int)sizeof(indent))
        ii = (int)sizeof(indent) - 1;
    indent[ii] = 0;

    p += 6;
    wrote = 0;

    for (;;) {
        p = skipws(p);
        if (!parse_ident_token(&p, sym))
            break;

        if (public_symbol_is_kept(sym)) {
            if (!wrote) {
                fprintf(f, "%spublic  %s", indent, sym);
                wrote = 1;
            } else {
                fprintf(f, ",%s", sym);
            }
        }

        p = skipws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        break;
    }

    if (wrote)
        fprintf(f, "\n");
}

static void write_output(const char *fn)
{
    FILE *f;
    int i, b;
    int first_block_start;

    f = fopen(fn, "w");
    if (!f) {
        perror(fn);
        exit(1);
    }

    fprintf(f, "; generated by dccrtlstrip - conservative reduced runtime\n");

    first_block_start = nlines;
    for (b = 0; b < nblocks; ++b) {
        if (blocks[b].start < first_block_start)
            first_block_start = blocks[b].start;
    }

    /* Preamble before first PUBLIC: org/equ/comments/etc. */
    for (i = 0; i < first_block_start; ++i)
        write_filtered_public_line(f, lines[i].s);

    for (b = 0; b < nblocks; ++b) {
        if (!blocks[b].keep)
            continue;
        for (i = blocks[b].start; i < blocks[b].end; ++i)
            write_filtered_public_line(f, lines[i].s);
    }

    /* If the original file had a standalone END outside any kept block,
     * preserve it.  If not, M80 can still assemble but warns; better to add
     * END START for CP/M runtime modules.
     */
    fprintf(f, "\n\tend\tstart\n");
    fclose(f);
}

static void usage(void)
{
    fprintf(stderr, "usage: dccrtlstrip [-k symbol ...] -r dccrtl.mac -o rtlmin.mac app.mac [app2.mac ...]\n");
    exit(1);
}

int main(int argc, char **argv)
{
    const char *rt = NULL;
    const char *out = NULL;
    int i;
    int saw_app = 0;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-r") && i + 1 < argc) {
            rt = argv[++i];
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out = argv[++i];
        } else if (!strcmp(argv[i], "-k") && i + 1 < argc) {
            /* explicit root; added after runtime symbol table is built */
            i++;
        } else if (!strcmp(argv[i], "-root") && i + 1 < argc) {
            /* alias for -k */
            i++;
        } else if (argv[i][0] == '-') {
            usage();
        } else {
            /* apps scanned after runtime symbols are known below */
        }
    }

    if (!rt || !out)
        usage();

    read_runtime(rt);
    build_blocks();
    build_scan_index();

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "-r") || !strcmp(argv[i], "-o")) {
            i++;
            continue;
        }
        if ((!strcmp(argv[i], "-k") || !strcmp(argv[i], "-root")) && i + 1 < argc) {
            add_root(argv[++i]);
            continue;
        }
        if (argv[i][0] == '-')
            continue;
        scan_app(argv[i]);
        saw_app = 1;
    }

    if (!saw_app)
        usage();

    mark_reachable();
    write_output(out);
    return 0;
}
