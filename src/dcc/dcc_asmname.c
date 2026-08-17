/**
 * @file dcc_asmname.c
 * @brief Maps C identifiers and runtime calls to safe M80 symbol names.
 *
 * @par Role
 * Applies reserved-name and short-significance mangling, recognizes fixed
 * runtime labels and internal publics, selects printf-family variants, caches
 * assigned names, and diagnoses collisions among emitted PUBLIC symbols.
 *
 * @par Key entry points
 * asm_name_for(), asm_name_for_runtime(), asm_name_for_pf_call(),
 * asm_name_must_mangle(), and asm_name_check_public_collision().
 *
 * @par Boundary
 * Symbol ownership and EXTRN tracking belong to dcc_symbols.c; callers that
 * emit declarations decide when a mapped name becomes PUBLIC.
 */

#include "dcc.h"

static int asm_prefix_eq_ci(const char *a, const char *b, int n)
{
    int i;
    for (i = 0; i < n; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
        if (a[i] == 0 || b[i] == 0)
            return a[i] == b[i];
    }
    return 1;
}

/* Write "_" + src, truncated to fit dst[dstsz], into dst. A bounded copy
 * instead of sprintf(dst, "_%s", src): with sprintf, gcc's -Wformat-overflow
 * cannot always prove src (other->name, a char[64] field) fits once this
 * function's caller is partially inlined, and warns with a nonsensical huge
 * bound estimate. */
static void asm_name_prefix_underscore(char *dst, size_t dstsz, const char *src)
{
    size_t len;

    len = strlen(src);
    if (len > dstsz - 2)
        len = dstsz - 2;
    dst[0] = '_';
    memcpy(dst + 1, src, len);
    dst[1 + len] = 0;
}

int asm_name_must_mangle(const char *cname)
{
    struct Sym *s;
    static const char *runtime_names[] = {
        "printf", "fprintf", "sprintf", "vprintf", "vfprintf", "vsprintf",
        "snprintf", "vsnprintf",
        NULL
    };
    int i;

    if (strncmp(cname, "fake_", 5) == 0)
        return 1;

    for (i = 0; runtime_names[i] != NULL; ++i) {
        if (!strcmp(cname, runtime_names[i]))
            return 0;
        if (asm_prefix_eq_ci(cname, runtime_names[i], 5))
            return 1;
    }

    /*
     * M80/L80 have short external-name significance, so file-scope static
     * helpers such as cb_is_zero/cb_is_one and compute_crc/compute_crc_fb can
     * collide if emitted as their C names.  Static names have internal linkage:
     * give them generated assembler names and do not make them PUBLIC.
     */
    s = find_global(cname);
    if (s && s->is_static)
        return 1;

    if (s && s->is_defined) {
        char aname[66];
        asm_name_prefix_underscore(aname, sizeof(aname), cname);
        for (i = 0; i < nglobals; ++i) {
            struct Sym *other = &globals[i];
            char oname[66];

            if (other == s || other->is_static)
                continue;
            if (!other->is_defined)
                continue;
            if (other->storage != SC_FUNC && other->storage != SC_GLOBAL &&
                other->storage != SC_EXTERN)
                continue;
            asm_name_prefix_underscore(oname, sizeof(oname), other->name);
            if (asm_prefix_eq_ci(aname, oname, 6))
                return 1;
        }
    }

    return 0;
}

int asm_name_is_internal_public(const char *cname)
{
    /*
     * These labels are emitted by every dcc translation unit around the
     * uninitialized global data block.  Let C code refer to them by their
     * real assembler names, without adding another leading underscore and
     * without emitting EXTRN for them.
     *
     * Usage from C:
     *     extern char __bssb;
     *     extern char __bsse;
     *     extern char __hstart;
     *     p = &__bssb;
     */
    return !strcmp(cname, "__bssb") ||
           !strcmp(cname, "__bsse") ||
           !strcmp(cname, "__hstart") ||
           !strcmp(cname, "__data_end");
}


const char *asm_name_for_runtime(const char *cname)
{
    /*
     * LINK-80/M80 style tools only preserve a short prefix of external
     * names.  Keep ordinary C source and headers standard, but map known RTL
     * entry points to deliberately short, collision-free assembler names.
     * The returned names are final assembler labels, not C names to which
     * another underscore should be prepended.
     */
    if (!strcmp(cname, "memcpy"))  return "__mcpy";
    if (!strcmp(cname, "memcmp"))  return "__mcmp";
    if (!strcmp(cname, "memchr"))  return "__mchr";
    if (!strcmp(cname, "memmove")) return "__mmov";
    if (!strcmp(cname, "memset"))  return "__mset";

    if (!strcmp(cname, "realloc")) return "__real";

    if (!strcmp(cname, "isalpha")) return "__caa";
    if (!strcmp(cname, "isalnum")) return "__can";
    if (!strcmp(cname, "isspace")) return "__csp";
    if (!strcmp(cname, "isdigit")) return "__cdg";
    if (!strcmp(cname, "isupper")) return "__cup";
    if (!strcmp(cname, "islower")) return "__clo";
    if (!strcmp(cname, "isxdigit")) return "__cxd";
    if (!strcmp(cname, "isprint")) return "__cpr";
    if (!strcmp(cname, "isgraph")) return "__cgr";
    if (!strcmp(cname, "iscntrl")) return "__cct";
    if (!strcmp(cname, "ispunct")) return "__cpu";
    if (!strcmp(cname, "toupper")) return "__ctu";
    if (!strcmp(cname, "tolower")) return "__ctl";

    if (!strcmp(cname, "strlen"))  return "__slen";
    if (!strcmp(cname, "strcpy"))  return "__scpy";
    if (!strcmp(cname, "strcmp"))  return "__scmp";
    if (!strcmp(cname, "stricmp")) return "__sicm";
    if (!strcmp(cname, "strchr"))  return "__schr";
    if (!strcmp(cname, "strcat"))  return "__scat";
    if (!strcmp(cname, "strrchr")) return "__srch";
    if (!strcmp(cname, "strstr"))  return "__sstr";
    if (!strcmp(cname, "strcoll")) return "__scol";
    if (!strcmp(cname, "strcspn")) return "__scsp";
    if (!strcmp(cname, "strpbrk")) return "__spbr";
    if (!strcmp(cname, "strspn"))  return "__sspn";
    if (!strcmp(cname, "strdup"))  return "__sdup";
    if (!strcmp(cname, "strncpy")) return "__ncpy";
    if (!strcmp(cname, "strncmp")) return "__ncmp";
    if (!strcmp(cname, "strncat")) return "__ncat";
    if (!strcmp(cname, "strtok"))     return "__stok";
    if (!strcmp(cname, "strtol"))     return "__stol";
    if (!strcmp(cname, "strtoul"))    return "__stou";
    if (!strcmp(cname, "localtime"))  return "__ltim";
    if (!strcmp(cname, "localeconv")) return "__lcnv";

    if (!strcmp(cname, "putchar")) return "__pchr";
    if (!strcmp(cname, "putc"))    return "__putc";
    if (!strcmp(cname, "fputc"))   return "__fpc";
    if (!strcmp(cname, "fputs"))   return "__fps";
    if (!strcmp(cname, "getc"))    return "__getc";
    if (!strcmp(cname, "fgetc"))   return "__fgetc";
    if (!strcmp(cname, "getchar")) return "__gchr";
    if (!strcmp(cname, "kbhit"))   return "__kbht";
    if (!strcmp(cname, "getch"))   return "__gtch";

    return NULL;
}

/* printf-family functions share one set of hook-installing runtime entry
 * points. Historically -ffloatio/-flongio were whole-program flags that
 * remapped every call the same way; asm_name_for_pf_call (below) instead
 * lets each call site pick its own variant based on what its own format
 * string literal actually uses, so a program mixing a %f-free sprintf() in
 * one file with a %.2f sprintf() in another links only what each call
 * actually needs. -ffloatio/-flongio still work as a blanket override (see
 * asm_name_for): passing one forces every call, literal or not, onto the
 * matching variant. */
static const struct {
    const char *cname;
    const char *plain;  /* neither flag needed for this call */
    const char *lng;    /* long support only */
    const char *fio;    /* float support only */
    const char *flio;   /* both */
} pf_family[] = {
    { "printf",     "_printf",    "_pflng", "_pffio", "_pflio" },
    { "sprintf",    "_sprintf",   "_splng", "_spfio", "_splio" },
    { "fprintf",    "_fprintf",   "_fplng", "_fpfio", "_fplio" },
    { "vprintf",    "_vprintf",   "_vplng", "_vpfio", "_vplio" },
    { "vsprintf",   "_vsprintf",  "_vslng", "_vsfio", "_vslio" },
    { "vfprintf",   "_vfprintf",  "_vflng", "_vffio", "_vflio" },
    { "snprintf",   "_snprintf",  "_snlng", "_snfio", "_snlio" },
    { "vsnprintf",  "_vsnprintf", "_vnlng", "_vnfio", "_vnlio" },
};

static const char *pf_family_lookup(const char *cname, int floatio, int longio)
{
    int i;

    for (i = 0; i < (int)(sizeof(pf_family) / sizeof(pf_family[0])); ++i) {
        if (strcmp(cname, pf_family[i].cname) != 0)
            continue;
        if (floatio && longio) return pf_family[i].flio;
        if (floatio)           return pf_family[i].fio;
        if (longio)            return pf_family[i].lng;
        return pf_family[i].plain;
    }
    return NULL;
}

/* Index of the format-string argument for a printf-family call (0-based,
 * matching source order), or -1 if cname isn't one of these functions. */
int asm_printf_family_fmt_arg_index(const char *cname)
{
    if (!strcmp(cname, "printf"))    return 0;
    if (!strcmp(cname, "vprintf"))   return 0;
    if (!strcmp(cname, "sprintf"))   return 1;
    if (!strcmp(cname, "fprintf"))   return 1;
    if (!strcmp(cname, "vsprintf"))  return 1;
    if (!strcmp(cname, "vfprintf"))  return 1;
    if (!strcmp(cname, "snprintf"))  return 2;
    if (!strcmp(cname, "vsnprintf")) return 2;
    return -1;
}

/* Scan a decoded (escapes already resolved) format-string literal for `%f`,
 * `%l`, `%x`/`%X`, and `%o` conversions, mirroring DCCRTL.MAC's own
 * pf_pct_next/pf_pct_done_width parsing closely enough to avoid false
 * negatives: skip flags/width/precision/'z', then check for a length
 * modifier 'l' and the conversion character itself. Only ever sets flags to
 * 1, never clears an already-true value, so callers can seed needs_*  from a
 * blanket override and let this add to it. Scanning stops at the first NUL,
 * matching how the runtime itself would process the string - any content
 * dcc stored past an embedded '\0' escape is unreachable at runtime anyway.
 *
 * needs_hex/needs_octal are only set for the *plain* (no 'l' prefix)
 * conversions: %lx/%lX print via pf_build_hex32 inside the already-gated
 * -flongio path (pf_lng), which is entirely separate code from pf_xu/pf_xl/
 * pf_build_hex16 - so a long-hex call only needs needs_long, not needs_hex.
 * There is no %lo in this runtime at all (pf_lng has no 'o' case). */
void asm_scan_format_specifiers(const char *s, int *needs_float, int *needs_long,
                                 int *needs_hex, int *needs_octal)
{
    size_t i;
    size_t len;

    len = strlen(s);
    i = 0;
    while (i < len) {
        if (s[i] != '%') {
            ++i;
            continue;
        }
        ++i;
        if (i >= len)
            break;
        if (s[i] == '%') {
            ++i;
            continue;
        }
        while (i < len && (s[i] == '-' || s[i] == '+' || s[i] == ' ' ||
                           s[i] == '#' || s[i] == '0'))
            ++i;
        while (i < len && s[i] >= '0' && s[i] <= '9')
            ++i;
        if (i < len && s[i] == '.') {
            ++i;
            while (i < len && s[i] >= '0' && s[i] <= '9')
                ++i;
        }
        if (i < len && s[i] == 'z')
            ++i;
        if (i < len && s[i] == 'l') {
            *needs_long = 1;
            ++i;
        } else if (i < len && (s[i] == 'x' || s[i] == 'X')) {
            *needs_hex = 1;
        } else if (i < len && s[i] == 'o') {
            *needs_octal = 1;
        }
        if (i < len && s[i] == 'f')
            *needs_float = 1;
        if (i < len)
            ++i;
    }
}

/* Per-call-site resolution: needs_float/needs_long are the caller's already-
 * combined decision (override flag OR'd with whatever asm_scan_format_
 * specifiers found in this call's own literal, OR 1/1 if the format wasn't
 * a compile-time-visible literal at all). Falls back to asm_name_for for
 * any cname that isn't part of the printf family. */
const char *asm_name_for_pf_call(const char *cname, int needs_float, int needs_long)
{
    const char *r = pf_family_lookup(cname, needs_float, needs_long);
    return r ? r : asm_name_for(cname);
}

const char *asm_name_for(const char *cname)
{
    int i;

    {
        /* opt_floatio/opt_longio are tristate (-1/0/1); pf_family_lookup
         * wants plain booleans, and a bare -1 is truthy in C, so normalize
         * before passing it in. */
        const char *r = pf_family_lookup(cname, opt_floatio > 0, opt_longio > 0);
        if (r)
            return r;
    }

    {
        const char *rtlname;
        rtlname = asm_name_for_runtime(cname);
        if (rtlname)
            return rtlname;
    }

    if (asm_name_is_internal_public(cname))
        return cname;

    for (i = 0; i < nasm_names; ++i)
        if (!strcmp(asm_names[i].cname, cname))
            return asm_names[i].aname;

    if (nasm_names >= MAX_ASM_NAMES)
        fatal("too many assembler symbol names");

    memset(&asm_names[nasm_names], 0, sizeof(asm_names[nasm_names]));
    strncpy(asm_names[nasm_names].cname, cname,
            sizeof(asm_names[nasm_names].cname) - 1);

    if (asm_name_must_mangle(cname)) {
        sprintf(asm_names[nasm_names].aname, "_Z%04d", nasm_names + 1);
    } else {
        sprintf(asm_names[nasm_names].aname, "_%s", cname);
    }

    return asm_names[nasm_names++].aname;
}

void asm_name_check_public_collision(const char *cname)
{
    /*
     * M80/L80 only honor the first 6 significant characters of a PUBLIC
     * symbol, so two distinct external-linkage C names that agree there
     * (e.g. xatanf/xatan2f -> _XATAN) look fine in the emitted .MAC but
     * silently collide at link time ("%Mult. Def. Global _XATAN"). Only
     * names actually emitted with a `public` directive are at risk (plain
     * BSS globals in a single-module build are EQU aliases, never public,
     * so they are exempt), which is why this is a separate opt-in check
     * called from each `public %s` emission site rather than folded into
     * every asm_name_for() lookup.
     */
    static char seen_cname[MAX_ASM_NAMES][64];
    static char seen_aname[MAX_ASM_NAMES][66];
    static int nseen;
    const char *aname;
    int i;

    aname = asm_name_for(cname);

    for (i = 0; i < nseen; ++i) {
        if (!strcmp(seen_cname[i], cname))
            return;
        if (asm_prefix_eq_ci(seen_aname[i], aname, 6)) {
            char errbuf[320];
            char cname1[64], cname2[64];
            strncpy(cname1, seen_cname[i], sizeof(cname1) - 1);
            cname1[sizeof(cname1) - 1] = 0;
            strncpy(cname2, cname, sizeof(cname2) - 1);
            cname2[sizeof(cname2) - 1] = 0;
            sprintf(errbuf,
                    "global names '%s' and '%s' are not distinguishable "
                    "in M80's 6 significant character public symbols "
                    "(both become '%.6s'); rename one",
                    cname1, cname2, aname);
            fatal(errbuf);
        }
    }

    if (nseen < MAX_ASM_NAMES) {
        strncpy(seen_cname[nseen], cname, sizeof(seen_cname[nseen]) - 1);
        seen_cname[nseen][sizeof(seen_cname[nseen]) - 1] = 0;
        strncpy(seen_aname[nseen], aname, sizeof(seen_aname[nseen]) - 1);
        seen_aname[nseen][sizeof(seen_aname[nseen]) - 1] = 0;
        nseen++;
    }
}
