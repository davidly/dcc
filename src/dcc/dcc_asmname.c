/*
 * dcc_asmname.c - C identifier -> M80 assembler symbol mapping.
 *
 * Decides how each C name is spelled in the emitted assembly: when a name must
 * be mangled (to dodge M80's 6-significant-char publics and reserved words),
 * when it maps to a fixed runtime-library entry point, and caches the
 * allocated assembler names in the shared asm_names[] table.
 *
 * MODULE: its own translation unit. Uses struct AsmName / MAX_ASM_NAMES and the
 * asm_names[] table; all shared declarations come from the umbrella header dcc.h.
 * Source provenance: monolith src/ddc.c lines 207-336.
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

int asm_name_must_mangle(const char *cname)
{
    struct Sym *s;
    static const char *runtime_names[] = {
        "printf", "fprintf", "sprintf", "vprintf", "vfprintf", "vsprintf",
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
    return s && s->is_static;
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
    if (!strcmp(cname, "iscntrl")) return "__cct";
    if (!strcmp(cname, "ispunct")) return "__cpu";
    if (!strcmp(cname, "toupper")) return "__ctu";
    if (!strcmp(cname, "tolower")) return "__ctl";

    if (!strcmp(cname, "strlen"))  return "__slen";
    if (!strcmp(cname, "strcpy"))  return "__scpy";
    if (!strcmp(cname, "strcmp"))  return "__scmp";
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

const char *asm_name_for(const char *cname)
{
    int i;

    if (!strcmp(cname, "printf")) {
        if (opt_floatio && opt_longio) return "_pflio";
        if (opt_floatio)               return "_pffio";
        if (opt_longio)                return "_pflng";
    }

    if (opt_longio) {
        if (!strcmp(cname, "sprintf"))  return "_splng";
        if (!strcmp(cname, "fprintf"))  return "_fplng";
        if (!strcmp(cname, "vprintf"))  return "_vplng";
        if (!strcmp(cname, "vsprintf")) return "_vslng";
        if (!strcmp(cname, "vfprintf")) return "_vflng";
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
