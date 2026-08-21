/**
 * @file l80c.c
 * @brief Links dcc LINK-80 modules into host-produced CP/M .COM images.
 *
 * @par Role
 * Reads M80-compatible .REL bitstreams, lays out each module's CSEG and DSEG,
 * relocates link values, resolves PUBLIC symbols and EXTRN chains, and writes
 * a padded .COM image plus a linked .SYM map. Optional per-module .SYM files
 * contribute relocated local symbols.
 *
 * @par Key entry points
 * main(), load_rel_file(), load_module_syms(), and sym_define().
 *
 * @par Boundary
 * Complements m80c and replaces emulated L80 for normal dccmake builds,
 * avoiding L80's CP/M workspace ceiling. It accepts the CSEG/DSEG records used
 * by this toolchain and rejects unsupported ASEG, COMMON, or unknown records.
 * Nonstandard program origins receive a CP/M entry jump or relocation
 * bootstrap so the linked image still starts execution at address 0100H.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>

typedef unsigned char U8;
typedef unsigned short U16;

#define SEG_CODE 1
#define SEG_DATA 2

#define T_ABS  0
#define T_CODE 1
#define T_DATA 2
#define T_COMM 3

#define MAXNAME 16
#define MAXMOD 64
#define MAXORIGIN 0x10000L

static const char *g_progname = "l80c";

static void die(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "%s: ", g_progname);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(2);
}
static char *xstrdup(const char *s) {
    char *p = (char *)malloc(strlen(s) + 1);
    if (!p) die("out of memory");
    strcpy(p, s);
    return p;
}
/* MAXNAME-bounded ".REL"/".COM"/".SYM" path builder; avoids snprintf for
 * C89/MSVC portability, matching m80c.c's fixed-buffer + strcpy style. */
static void suffix_path(char *out, size_t outsz, const char *base, const char *suffix) {
    size_t blen = strlen(base);
    size_t slen = strlen(suffix);
    if (blen + slen + 1 > outsz) die("path too long: %s%s", base, suffix);
    strcpy(out, base);
    strcpy(out + blen, suffix);
}
/* Bounded, always-null-terminated copy - like strncpy(dst,src,n-1) plus a
 * manual dst[n-1]=0, or snprintf(dst,n,"%s",src), but neither of those
 * dodges gcc's _FORTIFY_SOURCE truncation heuristics (-Wstringop-truncation
 * and -Wformat-truncation= respectively) when src could be >= n; gcc applies
 * neither check to a plain memcpy. snprintf is also C99, and this file
 * targets C89 like m80c.c. */
static void bounded_copy(char *dst, size_t dst_size, const char *src) {
    size_t len;
    if (dst_size == 0) return;
    len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}
/* Bounded append (memcpy-based, same rationale as bounded_copy above) -
 * dst must already be null-terminated (e.g. via bounded_copy or dst[0]=0). */
static void bounded_append(char *dst, size_t dst_size, const char *src) {
    size_t used, len, room;
    if (dst_size == 0) return;
    used = strlen(dst);
    if (used >= dst_size - 1) return;
    room = dst_size - 1 - used;
    len = strlen(src);
    if (len > room) len = room;
    memcpy(dst + used, src, len);
    dst[used + len] = 0;
}
static int ends_with_com(const char *s) {
    size_t n = strlen(s);
    return n >= 4 &&
        s[n - 4] == '.' &&
        toupper((unsigned char)s[n - 3]) == 'C' &&
        toupper((unsigned char)s[n - 2]) == 'O' &&
        toupper((unsigned char)s[n - 1]) == 'M';
}
static void normalize_output_base(char *name) {
    size_t n = strlen(name);
    if (ends_with_com(name))
        name[n - 4] = 0;
    if (name[0] == 0)
        die("empty output name");
}

/* ---------------------------------------------------------------- */
/* Bit reader - inverse of m80c.c's Rel writer (rel_bit/rel_bits).  */
/* ---------------------------------------------------------------- */
typedef struct {
    const U8 *data;
    long len;
    long bytepos;
    int bitpos;
    const char *modname_for_errors;
} BitIn;

static int bit_in(BitIn *r) {
    int v;
    if (r->bytepos >= r->len)
        die("unexpected end of REL data in module %s", r->modname_for_errors ? r->modname_for_errors : "?");
    v = (r->data[r->bytepos] >> (7 - r->bitpos)) & 1;
    r->bitpos++;
    if (r->bitpos == 8) {
        r->bitpos = 0;
        r->bytepos++;
    }
    return v;
}
static unsigned bits_in(BitIn *r, int n) {
    unsigned v = 0;
    int i;
    for (i = 0; i < n; i++) v = (unsigned)((v << 1) | (unsigned)bit_in(r));
    return v;
}
static void align_in(BitIn *r) {
    if (r->bitpos != 0) {
        r->bitpos = 0;
        r->bytepos++;
    }
}
static int at_eof(BitIn *r) {
    return r->bytepos >= r->len;
}
static void read_relname(BitIn *r, char *out, int outsz) {
    int n = (int)bits_in(r, 3);
    int i;
    for (i = 0; i < n; i++) {
        int c = (int)bits_in(r, 8);
        if (i < outsz - 1) out[i] = (char)c;
    }
    if (n < outsz) out[n] = 0;
    else out[outsz - 1] = 0;
}

/* ---------------------------------------------------------------- */
/* Per-module data.                                                  */
/* ---------------------------------------------------------------- */
typedef struct Fixup {
    int module;     /* owning module, so relocation knows which base to use */
    int seg;        /* SEG_CODE/SEG_DATA: where the 2-byte slot physically is */
    long off;       /* module-relative offset of that slot */
    int type;       /* T_CODE/T_DATA/T_COMM: which base to add */
    struct Fixup *next;
} Fixup;

typedef struct PublicSym {
    char name[MAXNAME];
    int type;       /* T_CODE/T_DATA/T_ABS */
    long value;     /* module-relative, or absolute for T_ABS */
    int module;
    struct PublicSym *next;
} PublicSym;

typedef struct ChainRef {
    char name[MAXNAME];
    int chain_type; /* T_CODE/T_DATA */
    long chain_head; /* module-relative offset of the last reference */
    int module;
    struct ChainRef *next;
} ChainRef;

/* Local (non-PUBLIC, non-EXTRN) symbols never appear in the .REL at all -
 * the assembler resolves and inlines them at assembly time, so their names
 * never cross the assembler/linker boundary. m80c's -C switch writes them
 * separately, per module, to <MODULE>.SYM (name, module-relative value,
 * and segment - see write_sym()/RELFIX30 in m80c.c) purely as an optional
 * side channel for debugging/profiling; l80c picks it up opportunistically
 * when present and merges relocated entries into its own .SYM, module-
 * qualified ("MODULE:name") since the same local name can recur across
 * modules. Its absence (real M80.COM, or m80c without -C) is not an error -
 * public-symbol linking never depends on it. */
#define MAXSYMNAME 80
typedef struct LocalSym {
    char name[MAXSYMNAME]; /* raw name as read from .SYM, not yet qualified */
    int type;              /* T_CODE/T_DATA/T_ABS */
    long value;            /* module-relative; relocated to absolute in place, later */
    int module;
    struct LocalSym *next;
} LocalSym;
static LocalSym *g_locals = NULL;

typedef struct {
    char name[MAXNAME];
    long cseg_size, dseg_size;
    U8 *code, *data;
    long cseg_base, dseg_base; /* filled in during layout */
    int has_start;
    int start_type;
    long start_value;
} Module;

static Module g_modules[MAXMOD];
static int g_nmodules = 0;
static Fixup *g_fixups = NULL;
static PublicSym *g_publics = NULL;
static ChainRef *g_chains = NULL;

/* Fixups are tagged with the module currently being loaded, so relocation
 * later knows which module's segment base to add. */
static int g_loading_module = -1;
static void add_fixup(int seg, long off, int type) {
    Fixup *f = (Fixup *)calloc(1, sizeof(Fixup));
    if (!f) die("out of memory");
    f->module = g_loading_module;
    f->seg = seg;
    f->off = off;
    f->type = type;
    f->next = g_fixups;
    g_fixups = f;
}

static void ensure_room(long size, long offset, long width,
                        const char *what, const char *modname) {
    if (offset < 0 || width < 0 || offset > size || width > size - offset)
        die("module %s: %s range %ld..%ld out of bounds "
            "(segment size %ld)", modname, what, offset,
            offset + width, size);
}

/* ---------------------------------------------------------------- */
/* Load one .REL file into a Module. General item dispatcher: any    */
/* item may appear in any order after the module-name header, except */
/* end-module/end-file at the very end - not overfit to the exact    */
/* sequence m80c happens to emit.                                    */
/* ---------------------------------------------------------------- */
static void load_rel_file(const char *path) {
    FILE *f;
    long len;
    U8 *data;
    BitIn r;
    Module *m;
    int have_cseg_size = 0, have_dseg_size = 0;
    long cur_code_lc = 0, cur_data_lc = 0;
    int cur_seg = SEG_CODE;
    int done = 0;

    if (g_nmodules >= MAXMOD) die("too many modules (max %d)", MAXMOD);
    m = &g_modules[g_nmodules];
    memset(m, 0, sizeof(*m));

    f = fopen(path, "rb");
    if (!f) die("cannot open %s", path);
    if (fseek(f, 0, SEEK_END) != 0) die("cannot seek %s", path);
    len = ftell(f);
    if (len < 0) die("cannot tell %s", path);
    rewind(f);
    data = (U8 *)malloc((size_t)(len > 0 ? len : 1));
    if (!data) die("out of memory reading %s", path);
    if (len > 0 && fread(data, 1, (size_t)len, f) != (size_t)len) die("short read on %s", path);
    fclose(f);

    r.data = data;
    r.len = len;
    r.bytepos = 0;
    r.bitpos = 0;
    r.modname_for_errors = path;

    /* First item must be the module-name special (code 2). */
    {
        int lead = bit_in(&r);
        int t2, code;
        if (lead != 1) die("%s: does not start with a module-name item", path);
        t2 = (int)bits_in(&r, 2);
        code = (int)bits_in(&r, 4);
        if (t2 != 0 || code != 2) die("%s: does not start with a module-name item", path);
        read_relname(&r, m->name, sizeof(m->name));
        r.modname_for_errors = m->name;
    }

    g_loading_module = g_nmodules;

    while (!done) {
        int lead;
        if (at_eof(&r)) die("module %s: truncated (missing end-file item)", m->name);
        lead = bit_in(&r);
        if (lead == 0) {
            int b = (int)bits_in(&r, 8);
            if (cur_seg == SEG_CODE) {
                ensure_room(m->cseg_size, cur_code_lc, 1,
                            "code", m->name);
                m->code[cur_code_lc++] = (U8)b;
            } else {
                ensure_room(m->dseg_size, cur_data_lc, 1,
                            "data", m->name);
                m->data[cur_data_lc++] = (U8)b;
            }
            continue;
        }
        {
            int t2 = (int)bits_in(&r, 2);
            if (t2 != 0) {
                /* top-level link value: type is t2 (1=CODE,2=DATA,3=COMM) */
                long lo = (long)bits_in(&r, 8);
                long hi = (long)bits_in(&r, 8);
                long val = lo | (hi << 8);
                int seg = cur_seg;
                long off = (seg == SEG_CODE) ? cur_code_lc : cur_data_lc;
                U8 *arr;
                ensure_room(seg == SEG_CODE ? m->cseg_size : m->dseg_size,
                            off, 2, "link", m->name);
                arr = (seg == SEG_CODE) ? m->code : m->data;
                arr[off] = (U8)(val & 0xff);
                arr[off + 1] = (U8)((val >> 8) & 0xff);
                add_fixup(seg, off, t2);
                if (seg == SEG_CODE) cur_code_lc += 2; else cur_data_lc += 2;
                continue;
            }
            {
                int code = (int)bits_in(&r, 4);
                int hasval = 0, valtype = 0;
                long val = 0;
                char name[MAXNAME];
                int has_name = 0;

                switch (code) {
                    case 0:  hasval = 0; has_name = 1; break; /* entry symbol preview */
                    case 2:  die("module %s: unexpected second module-name item", m->name); break;
                    case 6:  hasval = 1; has_name = 1; break; /* extern chain head */
                    case 7:  hasval = 1; has_name = 1; break; /* define public */
                    case 10: hasval = 1; has_name = 0; break; /* dseg size */
                    case 11: hasval = 1; has_name = 0; break; /* set LC */
                    case 13: hasval = 1; has_name = 0; break; /* cseg size */
                    case 14: hasval = 1; has_name = 0; break; /* end module */
                    case 15: hasval = 0; has_name = 0; break; /* end file */
                    default:
                        die("module %s: unsupported REL item code %d (COMMON/ASEG or an unrecognized "
                            "record type - not produced by this toolchain's real usage; refusing to "
                            "guess rather than risk a silent miscompile)", m->name, code);
                }
                if (hasval) {
                    valtype = (int)bits_in(&r, 2);
                    {
                        long lo = (long)bits_in(&r, 8);
                        long hi = (long)bits_in(&r, 8);
                        val = lo | (hi << 8);
                    }
                }
                if (has_name) read_relname(&r, name, sizeof(name));

                switch (code) {
                    case 0:
                        /* informational only; the authoritative (type,value) comes via code 7 */
                        break;
                    case 10:
                        if (have_dseg_size) die("module %s: duplicate dseg-size item", m->name);
                        m->dseg_size = val;
                        m->data = (U8 *)calloc((size_t)(val > 0 ? val : 1), 1);
                        if (!m->data) die("out of memory allocating data segment for %s", m->name);
                        have_dseg_size = 1;
                        break;
                    case 13:
                        if (have_cseg_size) die("module %s: duplicate cseg-size item", m->name);
                        m->cseg_size = val;
                        m->code = (U8 *)calloc((size_t)(val > 0 ? val : 1), 1);
                        if (!m->code) die("out of memory allocating code segment for %s", m->name);
                        have_cseg_size = 1;
                        break;
                    case 11:
                        if (!have_cseg_size || !have_dseg_size)
                            die("module %s: set-LC item before segment sizes were declared", m->name);
                        if (valtype == T_CODE) {
                            ensure_room(m->cseg_size, val, 0,
                                        "code LC", m->name);
                            cur_seg = SEG_CODE;
                            cur_code_lc = val;
                        } else if (valtype == T_DATA) {
                            ensure_room(m->dseg_size, val, 0,
                                        "data LC", m->name);
                            cur_seg = SEG_DATA;
                            cur_data_lc = val;
                        } else {
                            die("module %s: set-LC item with unsupported segment type %d", m->name, valtype);
                        }
                        break;
                    case 7: {
                        PublicSym *p = (PublicSym *)calloc(1, sizeof(PublicSym));
                        if (!p) die("out of memory");
                        bounded_copy(p->name, sizeof(p->name), name);
                        p->type = valtype;
                        p->value = val;
                        p->module = g_loading_module;
                        p->next = g_publics;
                        g_publics = p;
                        break;
                    }
                    case 6: {
                        ChainRef *c = (ChainRef *)calloc(1, sizeof(ChainRef));
                        if (!c) die("out of memory");
                        if (valtype != T_CODE && valtype != T_DATA)
                            die("module %s: extern chain %s has unsupported segment type %d", m->name, name, valtype);
                        bounded_copy(c->name, sizeof(c->name), name);
                        c->chain_type = valtype;
                        c->chain_head = val;
                        c->module = g_loading_module;
                        c->next = g_chains;
                        g_chains = c;
                        break;
                    }
                    case 14:
                        if (valtype != T_ABS && valtype != T_CODE &&
                            valtype != T_DATA)
                            die("module %s: start address has unsupported "
                                "segment type %d", m->name, valtype);
                        if (valtype != T_ABS || val != 0) {
                            m->has_start = 1;
                            m->start_type = valtype;
                            m->start_value = val;
                        }
                        align_in(&r); /* RELFIX20: end-module is followed by a byte-aligned end-file */
                        break;
                    case 15:
                        done = 1;
                        break;
                    default:
                        break;
                }
            }
        }
    }

    if (!have_cseg_size) die("module %s: missing code-segment-size item", m->name);
    if (!have_dseg_size) die("module %s: missing data-segment-size item", m->name);

    free(data);
    g_nmodules++;
}

/* Best-effort: m80c's -C switch writes <MODULE>.SYM alongside the .REL with
 * every defined symbol (public and local), each tagged with its segment
 * (RELFIX30). Missing file is not an error - most builds don't pass -C, and
 * real M80.COM never produces one - it just means no local symbols are
 * available for this module. Each line is:
 *   NAME     ADDR TYPE     [PUBLIC ][EXTRN]
 * Only lines with neither trailing tag (i.e. exactly 3 tokens) are local. */
static int line_type_code(const char *typestr) {
    if (strcmp(typestr, "code") == 0) return T_CODE;
    if (strcmp(typestr, "data") == 0) return T_DATA;
    return T_ABS;
}
static void load_module_syms(int module_index, const char *base_name) {
    char path[MAXNAME + 8];
    FILE *f;
    char line[256];

    suffix_path(path, sizeof(path), base_name, ".SYM");
    f = fopen(path, "r");
    if (!f) {
        suffix_path(path, sizeof(path), base_name, ".sym");
        f = fopen(path, "r");
    }
    if (!f) return;

    while (fgets(line, sizeof(line), f)) {
        char name[MAXSYMNAME], typestr[16], tag[16];
        unsigned long uval;
        int n = sscanf(line, "%79s %lx %15s %15s", name, &uval, typestr, tag);
        if (n == 3) {
            LocalSym *ls = (LocalSym *)calloc(1, sizeof(LocalSym));
            if (!ls) die("out of memory");
            bounded_copy(ls->name, sizeof(ls->name), name);
            ls->type = line_type_code(typestr);
            ls->value = (long)(uval & 0xffff);
            ls->module = module_index;
            ls->next = g_locals;
            g_locals = ls;
        }
    }
    fclose(f);
}

/* ---------------------------------------------------------------- */
/* Linking: layout, symbol table, relocation, EXTRN chain walking.  */
/* ---------------------------------------------------------------- */
typedef struct SymEntry {
    char name[MAXNAME];
    long value;
    int defmodule;
    struct SymEntry *next;
} SymEntry;

#define SYMHASH 2039
static SymEntry *g_symtab[SYMHASH];

static unsigned symhash(const char *s) {
    unsigned h = 0;
    while (*s) h = h * 131u + (unsigned char)*s++;
    return h % SYMHASH;
}
static SymEntry *sym_lookup(const char *name) {
    SymEntry *s;
    for (s = g_symtab[symhash(name)]; s; s = s->next)
        if (strcmp(s->name, name) == 0) return s;
    return NULL;
}
static void sym_define(const char *name, long value, int module) {
    unsigned h = symhash(name);
    SymEntry *s = sym_lookup(name);
    if (s) die("multiply defined global: %s (module %s and module %s)",
               name, g_modules[s->defmodule].name, g_modules[module].name);
    s = (SymEntry *)calloc(1, sizeof(SymEntry));
    if (!s) die("out of memory");
    bounded_copy(s->name, sizeof(s->name), name);
    s->value = value;
    s->defmodule = module;
    s->next = g_symtab[h];
    g_symtab[h] = s;
}

int main(int argc, char **argv) {
    long origin = 0x100;
    char *modlist[MAXMOD];
    int nmodlist = 0;
    char outname[512];
    int have_outname = 0;
    int i;
    long running;
    long total_len, output_len, start_address;
    U8 *image;
    PublicSym *p;
    Fixup *mf;
    ChainRef *c;
    FILE *outf;
    char outpath[512];
    char sympath[512];
    int verbose = 0;

    if (argc > 0) g_progname = argv[0];
    outname[0] = 0;

    if (argc < 2) {
        fprintf(stderr, "usage: %s [/P:origin,]module1,module2,...,moduleN[/switches] [-o out.COM] [-v]\n", g_progname);
        return 2;
    }

    /* Classic L80-style single-token command line, e.g.:
       /P:100,RTLMIN,A1,A1/N/E/Y
       Comma-separated tokens; a token starting with '/' is a leading
       switch (only /P: is meaningful here); otherwise a module name,
       optionally followed by /switch/switch... which are parsed and
       ignored (L80's /N /E /Y etc. affect its interactive console
       behavior, not link correctness). A module repeated later in the
       list (as real L80 command lines do, to attach trailing switches)
       is treated as the same module - not linked twice. */
    {
        char *cmdline = xstrdup(argv[1]);
        char *tok;
        int argi;
        tok = strtok(cmdline, ",");
        while (tok) {
            if (tok[0] == '/') {
                if ((tok[1] == 'P' || tok[1] == 'p') && tok[2] == ':') {
                    char *end;
                    errno = 0;
                    origin = strtol(tok + 3, &end, 16);
                    if (tok[3] == 0 || *end != 0 || errno == ERANGE)
                        die("invalid origin: %s", tok + 3);
                }
                /* other leading switches: ignored */
            } else {
                char *slash = strchr(tok, '/');
                char name[MAXNAME];
                int n;
                if (slash) n = (int)(slash - tok); else n = (int)strlen(tok);
                if (n >= MAXNAME) n = MAXNAME - 1;
                memset(name, 0, sizeof(name));
                for (i = 0; i < n; i++) name[i] = (char)toupper((unsigned char)tok[i]);
                {
                    int dup = 0, k;
                    for (k = 0; k < nmodlist; k++) if (strcmp(modlist[k], name) == 0) { dup = 1; break; }
                    if (!dup) {
                        if (nmodlist >= MAXMOD) die("too many modules on command line");
                        modlist[nmodlist] = xstrdup(name);
                        nmodlist++;
                    }
                }
            }
            tok = strtok(NULL, ",");
        }
        free(cmdline);

        for (argi = 2; argi < argc; argi++) {
            if (strcmp(argv[argi], "-o") == 0 && argi + 1 < argc) {
                bounded_copy(outname, sizeof(outname), argv[++argi]);
                have_outname = 1;
            } else if (strcmp(argv[argi], "-v") == 0) {
                verbose = 1;
            }
        }
    }

    if (nmodlist == 0) die("no modules given");
    if (origin < 0 || origin >= MAXORIGIN)
        die("origin out of range");

    if (!have_outname) {
        bounded_copy(outname, sizeof(outname), modlist[nmodlist - 1]);
    }
    normalize_output_base(outname);

    /* Load every module named on the command line, in order. Module names
       are matched to files by appending .REL (trying both the given case
       and lowercase). Real L80 command lines conventionally repeat the
       last module's name with switches attached just to carry those
       switches (e.g. "...,A1,A1/N/E/Y") - harmless here since it was
       already deduplicated above. dccmake's multi-module command lines go
       further: the trailing name+switches token can be a pure output-name
       marker that is *not* one of the actual linked modules at all. So a
       missing .REL is only a hard error unless it's the last token in the
       list, in which case it's treated as an output-name-only marker
       instead of a module. */
    for (i = 0; i < nmodlist; i++) {
        char path[MAXNAME + 8];
        FILE *probe;
        suffix_path(path, sizeof(path), modlist[i], ".REL");
        probe = fopen(path, "rb");
        if (!probe) {
            suffix_path(path, sizeof(path), modlist[i], ".rel");
            probe = fopen(path, "rb");
        }
        if (!probe) {
            if (i == nmodlist - 1) {
                if (verbose)
                    fprintf(stderr, "%s: no %s.REL - treating as an output-name marker, not a module\n",
                            g_progname, modlist[i]);
                continue;
            }
            die("cannot find %s.REL", modlist[i]);
        }
        fclose(probe);
        if (verbose) fprintf(stderr, "%s: loading %s\n", g_progname, path);
        load_rel_file(path);
        load_module_syms(g_nmodules - 1, modlist[i]);
    }

    /* Layout: all CSEGs concatenated in module order, then all DSEGs
       concatenated in module order (standard LINK-80 behavior). */
    running = origin;
    for (i = 0; i < g_nmodules; i++) {
        g_modules[i].cseg_base = running;
        running += g_modules[i].cseg_size;
    }
    for (i = 0; i < g_nmodules; i++) {
        g_modules[i].dseg_base = running;
        running += g_modules[i].dseg_size;
    }
    total_len = running - origin;
    if (total_len <= 0) die("empty program");
    if (origin + total_len > MAXORIGIN) die("program too large: extends past 0xFFFF");

    start_address = origin;
    for (i = 0; i < g_nmodules; i++) {
        Module *m = &g_modules[i];
        if (!m->has_start) continue;
        if (m->start_type == T_CODE)
            start_address = m->cseg_base + m->start_value;
        else if (m->start_type == T_DATA)
            start_address = m->dseg_base + m->start_value;
        else
            start_address = m->start_value;
    }
    if (start_address < 0 || start_address >= MAXORIGIN)
        die("start address out of range");

    image = (U8 *)calloc((size_t)total_len, 1);
    if (!image) die("out of memory allocating %ld-byte image", total_len);
    for (i = 0; i < g_nmodules; i++) {
        Module *m = &g_modules[i];
        if (m->cseg_size) memcpy(image + (m->cseg_base - origin), m->code, (size_t)m->cseg_size);
        if (m->dseg_size) memcpy(image + (m->dseg_base - origin), m->data, (size_t)m->dseg_size);
    }

    /* Global symbol table: module-relative public values become absolute. */
    for (p = g_publics; p; p = p->next) {
        long absval;
        Module *m = &g_modules[p->module];
        if (p->type == T_CODE) absval = p->value + m->cseg_base;
        else if (p->type == T_DATA) absval = p->value + m->dseg_base;
        else absval = p->value; /* T_ABS: already absolute (EQU constant etc.) */
        sym_define(p->name, absval & 0xffff, p->module);
    }

    /* Local symbols (from optional per-module .SYM, -C only): relocate the
       same way, in place - value becomes absolute. Not entered into the
       global symbol table (g_symtab): they're display-only, never a valid
       EXTRN resolution target (their names never appear in any .REL). */
    {
        LocalSym *ls;
        for (ls = g_locals; ls; ls = ls->next) {
            Module *m = &g_modules[ls->module];
            if (ls->type == T_CODE) ls->value = (ls->value + m->cseg_base) & 0xffff;
            else if (ls->type == T_DATA) ls->value = (ls->value + m->dseg_base) & 0xffff;
            /* T_ABS already absolute */
        }
    }

    /* Generic relocation: every recorded link-value slot gets its owning
       module's segment base added, in place, inside the combined image.
       This uniformly relocates both ordinary intra-module references and
       EXTRN chain-link placeholders (which are written as ordinary
       CODE/DATA link values pointing at another same-module offset) - no
       special-casing needed between the two. */
    for (mf = g_fixups; mf; mf = mf->next) {
        Module *m = &g_modules[mf->module];
        long base;
        long slot;
        U16 v;
        if (mf->type == T_CODE) base = m->cseg_base;
        else if (mf->type == T_DATA) base = m->dseg_base;
        else die("module %s: COMMON blocks are not supported by l80c", m->name);
        slot = ((mf->seg == SEG_CODE) ? m->cseg_base : m->dseg_base) - origin + mf->off;
        if (slot < 0 || slot + 1 >= total_len) die("module %s: relocation slot out of range", m->name);
        v = (U16)(image[slot] | (image[slot + 1] << 8));
        v = (U16)(v + base);
        image[slot] = (U8)(v & 0xff);
        image[slot + 1] = (U8)((v >> 8) & 0xff);
    }

    /* EXTRN chains: walk each module's backward-linked reference list,
       patching every slot with the resolved absolute address. After the
       relocation pass above, a non-terminal slot's stored value is
       already the absolute address of the previous reference (since that
       slot was relocated like any other CODE/DATA link value); a stored
       0 means "first reference in this chain" (never fixed up, so it's
       still the literal 0 written at load time), and terminates the walk -
       0 is never a valid CP/M code/data target. */
    for (c = g_chains; c; c = c->next) {
        SymEntry *sym = sym_lookup(c->name);
        Module *m = &g_modules[c->module];
        long addr;
        if (!sym)
            die("undefined symbol: %s (referenced from module %s)", c->name, m->name);
        addr = (c->chain_type == T_CODE ? m->cseg_base : m->dseg_base) + c->chain_head;
        for (;;) {
            long slot = addr - origin;
            U16 old;
            if (slot < 0 || slot + 1 >= total_len)
                die("module %s: extern chain for %s runs outside the image", m->name, c->name);
            old = (U16)(image[slot] | (image[slot + 1] << 8));
            image[slot] = (U8)(sym->value & 0xff);
            image[slot + 1] = (U8)((sym->value >> 8) & 0xff);
            if (old == 0) break;
            addr = old;
        }
    }

    /* Emit a CP/M-loadable image. At or above 0100H, file offsets map
       directly to memory and a leading JP reaches origins above the three-byte
       entry slot. Below 0100H, a compact loader after the payload copies it
       down before transferring control. */
    suffix_path(outpath, sizeof(outpath), outname, ".COM");
    outf = fopen(outpath, "wb");
    if (!outf) die("cannot create %s", outpath);
    if (origin >= 0x100) {
        long prefix = origin - 0x100;
        if (prefix >= 3) {
            if (fputc(0xc3, outf) == EOF ||
                fputc((int)(start_address & 0xff), outf) == EOF ||
                fputc((int)((start_address >> 8) & 0xff), outf) == EOF)
                die("short write to %s", outpath);
            prefix -= 3;
        }
        while (prefix-- > 0)
            if (fputc(0, outf) == EOF)
                die("short write to %s", outpath);
        if (fwrite(image, 1, (size_t)total_len, outf) !=
            (size_t)total_len)
            die("short write to %s", outpath);
        output_len = origin - 0x100 + total_len;
    } else {
        enum { BOOTSTRAP_SIZE = 22 };
        long source = 0x103;
        long loader = source + total_len;
        long loop = loader + 9;
        U8 bootstrap[BOOTSTRAP_SIZE];

        output_len = 3 + total_len + BOOTSTRAP_SIZE;
        if (output_len > 0xff00)
            die("program too large for a CP/M relocation bootstrap");

        bootstrap[0] = 0x21; /* LD HL,source */
        bootstrap[1] = (U8)(source & 0xff);
        bootstrap[2] = (U8)((source >> 8) & 0xff);
        bootstrap[3] = 0x11; /* LD DE,origin */
        bootstrap[4] = (U8)(origin & 0xff);
        bootstrap[5] = (U8)((origin >> 8) & 0xff);
        bootstrap[6] = 0x01; /* LD BC,length */
        bootstrap[7] = (U8)(total_len & 0xff);
        bootstrap[8] = (U8)((total_len >> 8) & 0xff);
        bootstrap[9] = 0x7e; /* copy: LD A,(HL) */
        bootstrap[10] = 0x12; /* LD (DE),A */
        bootstrap[11] = 0x23; /* INC HL */
        bootstrap[12] = 0x13; /* INC DE */
        bootstrap[13] = 0x0b; /* DEC BC */
        bootstrap[14] = 0x78; /* LD A,B */
        bootstrap[15] = 0xb1; /* OR C */
        bootstrap[16] = 0xc2; /* JP NZ,copy */
        bootstrap[17] = (U8)(loop & 0xff);
        bootstrap[18] = (U8)((loop >> 8) & 0xff);
        bootstrap[19] = 0xc3; /* JP start */
        bootstrap[20] = (U8)(start_address & 0xff);
        bootstrap[21] = (U8)((start_address >> 8) & 0xff);

        if (fputc(0xc3, outf) == EOF ||
            fputc((int)(loader & 0xff), outf) == EOF ||
            fputc((int)((loader >> 8) & 0xff), outf) == EOF ||
            fwrite(image, 1, (size_t)total_len, outf) !=
                (size_t)total_len ||
            fwrite(bootstrap, 1, BOOTSTRAP_SIZE, outf) != BOOTSTRAP_SIZE)
            die("short write to %s", outpath);
    }
    {
        /* CP/M files are made of 128-byte records; pad with zeros to the
           next boundary (ntvcm and real CP/M both load a non-padded image
           fine, but a zero-padded tail is the conventional, disk-friendly
           shape and avoids surprises in tools that assume it). */
        long pad = (128 - (output_len % 128)) % 128;
        while (pad-- > 0)
            if (fputc(0, outf) == EOF)
                die("short write to %s", outpath);
    }
    if (fclose(outf) != 0)
        die("cannot finish writing %s", outpath);

    /* .SYM: final linked symbol table, ADDR<TAB>NAME pairs, matching the
       format dccprof.py already parses from real L80's output - now with
       local symbols merged in too when a module's .SYM was found (-C),
       module-qualified ("MODULE:name") since a local name can recur across
       modules. dccprof.py's regex-based parser (looking for 4 hex digits
       then a non-space token) accepts these unchanged - the colon is just
       part of an opaque symbol name to it. */
    suffix_path(sympath, sizeof(sympath), outname, ".SYM");
    outf = fopen(sympath, "w");
    if (outf) {
        typedef struct { char name[MAXSYMNAME + MAXNAME]; long value; } SymOut;
        SymOut *all;
        int n = 0, cap = 256, h;
        SymEntry *s;
        LocalSym *ls;
        all = (SymOut *)malloc(sizeof(SymOut) * (size_t)cap);
        if (all) {
            for (h = 0; h < SYMHASH; h++)
                for (s = g_symtab[h]; s; s = s->next) {
                    if (n >= cap) {
                        cap *= 2;
                        all = (SymOut *)realloc(all, sizeof(SymOut) * (size_t)cap);
                        if (!all) break;
                    }
                    bounded_copy(all[n].name, sizeof(all[n].name), s->name);
                    all[n].value = s->value;
                    n++;
                }
        }
        for (ls = g_locals; all && ls; ls = ls->next) {
            if (n >= cap) {
                cap *= 2;
                all = (SymOut *)realloc(all, sizeof(SymOut) * (size_t)cap);
                if (!all) break;
            }
            bounded_copy(all[n].name, sizeof(all[n].name), g_modules[ls->module].name);
            bounded_append(all[n].name, sizeof(all[n].name), ":");
            bounded_append(all[n].name, sizeof(all[n].name), ls->name);
            all[n].value = ls->value;
            n++;
        }
        if (all) {
            int a, b, col = 0;
            for (a = 0; a < n; a++)
                for (b = a + 1; b < n; b++)
                    if (strcmp(all[a].name, all[b].name) > 0) {
                        SymOut t = all[a]; all[a] = all[b]; all[b] = t;
                    }
            for (a = 0; a < n; a++) {
                fprintf(outf, "%04lX %-6s", all[a].value & 0xffff, all[a].name);
                col++;
                if (col == 4) { fputc('\n', outf); col = 0; }
                else fputc('\t', outf);
            }
            if (col) fputc('\n', outf);
            free(all);
        }
        fclose(outf);
    }

    if (verbose) {
        fprintf(stderr, "%s: %d module(s), origin %04lX, size %04lX (%ld bytes), wrote %s\n",
                g_progname, g_nmodules, origin, total_len, total_len, outpath);
    }

    return 0;
}
