/**
 * @file dccmake.c
 * @brief Drives complete dcc builds of CP/M applications.
 *
 * @par Role
 * Reads dccmake.txt and command-line settings, validates CP/M names, compiles
 * one or more C sources to .MAC, optionally optimizes them, assembles the
 * application and reduced runtime, and links the final .COM plus requested
 * listing, symbol, and debug artifacts.
 *
 * @par Key entry points
 * main() loads configuration; run_build() invokes each pipeline stage.
 *
 * @par Boundary
 * dcc and dccpeep produce application assembly, dccrtlstrip selects runtime
 * blocks, and m80c/M80 plus l80c/L80 assemble and link. This driver configures
 * and invokes those tools but does not implement their transformations.
 */
/* CLOCK_MONOTONIC/clock_gettime (used by now_ms() below) are POSIX, not
 * plain C11 - glibc only declares them if a feature-test macro requesting
 * at least POSIX.1-2001 is defined before the first system header is
 * included. Must come before every #include, including stdio.h. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

/* MSVC's <sys/stat.h> defines the S_IFDIR bitmask but not the POSIX
 * S_ISDIR macro built on top of it - supply the standard fallback. */
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#include <process.h>
#define MKDIR(path) _mkdir(path)
#define CHDIR(path) _chdir(path)
#define GETCWD(buf, size) _getcwd(buf, (int)(size))
#define PATH_SEP '\\'
/* A same-directory tool reference has to use the platform's own separator:
 * cmd.exe (which run_cmd's system() call goes through) does not resolve a
 * POSIX-style "./name" the way a shell does - it fails outright with
 * "'.' is not recognized as an internal or external command" - so this
 * cannot just be a single "./name" literal shared with the non-Windows
 * build. */
#define LOCAL_DCC ".\\dcc"
#define LOCAL_DCCPEEP ".\\dccpeep"
#define LOCAL_DCCRTLSTRIP ".\\dccrtlstrip"
#define LOCAL_M80C ".\\m80c"
#define LOCAL_L80C ".\\l80c"
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0777)
#define CHDIR(path) chdir(path)
#define GETCWD(buf, size) getcwd(buf, size)
#define PATH_SEP '/'
#define LOCAL_DCC "./dcc"
#define LOCAL_DCCPEEP "./dccpeep"
#define LOCAL_DCCRTLSTRIP "./dccrtlstrip"
#define LOCAL_M80C "./m80c"
#define LOCAL_L80C "./l80c"
#endif

#define MAX_ITEMS 128
#define MAX_PATH_LEN 512
#define MAX_NAME_LEN 128
#define MAX_CMD_LEN 32768
#define MAX_LINE_LEN 2048

/* Monotonic wall-clock milliseconds, for the per-phase pipeline timing
 * printed at the end of run_build - run_cmd's children run under system(),
 * whose time isn't reliably reflected in this process's own clock()/CPU
 * time, so this measures wall clock directly instead. */
#ifdef _WIN32
static long long now_ms(void)
{
    return (long long)GetTickCount64();
}
#else
static long long now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#endif

struct Config {
    char inputs[MAX_ITEMS][MAX_PATH_LEN];
    int input_count;
    char output[MAX_NAME_LEN];
    int output_set;
    int floatio;
    int no_floatio;
    int flongio;
    int no_longio;
    int hexio;
    int no_hexio;
    int octio;
    int no_octio;
    int stack_check;
    int no_narrow;
    int debug;
    int debug_lines;
    int stack_bytes;
    int use_emulated_m80;
    int use_emulated_l80;
    char includes[MAX_ITEMS][MAX_PATH_LEN];
    int include_count;
    char dcc_args[MAX_ITEMS][MAX_PATH_LEN];
    int dcc_arg_count;
    int peep;
    int peep_debug;
    int dccpeep_undoc;
    char build_dir[MAX_PATH_LEN];
    char dcc[MAX_PATH_LEN];
    char dccpeep[MAX_PATH_LEN];
    char dccrtlstrip[MAX_PATH_LEN];
    char ntvcm[MAX_PATH_LEN];
    char m80[MAX_PATH_LEN];
    char m80c[MAX_PATH_LEN];
    char l80[MAX_PATH_LEN];
    char l80c[MAX_PATH_LEN];
    char runtime[MAX_PATH_LEN];
};

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    size_t len;
    if (dst_size == 0)
        return;
    if (!src) src = "";
    /* memcpy + manual null terminator, rather than strncpy(dst,src,dst_size-1)
     * (trips gcc's _FORTIFY_SOURCE -Wstringop-truncation) or
     * snprintf(dst,dst_size,"%s",src) (same heuristic, -Wformat-truncation=
     * instead) - gcc doesn't apply either check to a plain memcpy, so this is
     * the same bounded, always-null-terminated copy without the noise. */
    len = strlen(src);
    if (len >= dst_size) len = dst_size - 1;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static void trim(char *s)
{
    char *p;
    char *end;

    p = s;
    while (*p && isspace((unsigned char)*p))
        p++;
    if (p != s)
        memmove(s, p, strlen(p) + 1);

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = 0;
}

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

static void normalize_key(char *dst, size_t dst_size, const char *src)
{
    size_t i;
    size_t j;
    int last_dash;

    j = 0;
    last_dash = 0;
    for (i = 0; src[i] && j + 1 < dst_size; i++) {
        unsigned char c;
        c = (unsigned char)src[i];
        if (c == '_' || c == ' ' || c == '\t') {
            if (!last_dash && j > 0) {
                dst[j++] = '-';
                last_dash = 1;
            }
        } else {
            dst[j++] = (char)tolower(c);
            last_dash = (c == '-');
        }
    }
    while (j > 0 && dst[j - 1] == '-')
        j--;
    dst[j] = 0;
}

static int file_exists(const char *path)
{
    FILE *f;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

static int ensure_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) == 0)
        return S_ISDIR(st.st_mode) != 0;
    if (MKDIR(path) == 0)
        return 1;
    return errno == EEXIST;
}

static int dir_exists(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return S_ISDIR(st.st_mode) != 0;
}

static void path_join(char *dst, size_t dst_size, const char *dir, const char *name)
{
    size_t n;
    copy_text(dst, dst_size, dir);
    n = strlen(dst);
    if (n > 0 && dst[n - 1] != '/' && dst[n - 1] != '\\') {
        if (n + 1 < dst_size) {
            dst[n++] = PATH_SEP;
            dst[n] = 0;
        }
    }
    if (n < dst_size)
        strncat(dst, name, dst_size - n - 1);
}

static const char *base_name(const char *path)
{
    const char *p;
    const char *b;

    b = path;
    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\')
            b = p + 1;
    }
    return b;
}

static void base_no_ext(char *dst, size_t dst_size, const char *path)
{
    const char *b;
    const char *dot;
    size_t n;

    b = base_name(path);
    dot = strrchr(b, '.');
    n = dot ? (size_t)(dot - b) : strlen(b);
    if (n >= dst_size)
        n = dst_size - 1;
    memcpy(dst, b, n);
    dst[n] = 0;
}

static int is_cpm_filename_char(int c)
{
    return isalnum((unsigned char)c) || c == '_' || c == '-' ||
           c == '$' || c == '#' || c == '@';
}

static int validate_cpm_83_name(const char *label, const char *value, int allow_extension)
{
    const char *name;
    const char *dot;
    const char *p;
    int name_len;
    int ext_len;

    name = base_name(value);
    if (!name[0]) {
        fprintf(stderr, "%s is empty; CP/M filenames must use 1 to 8 name characters\n", label);
        return 0;
    }

    dot = strchr(name, '.');
    if (dot && strchr(dot + 1, '.')) {
        fprintf(stderr, "%s '%s' is not a valid CP/M 8.3 filename: too many dots\n", label, value);
        return 0;
    }
    if (dot && !allow_extension) {
        fprintf(stderr, "%s '%s' must be a CP/M 8-character base name without an extension\n", label, value);
        return 0;
    }

    name_len = dot ? (int)(dot - name) : (int)strlen(name);
    ext_len = dot ? (int)strlen(dot + 1) : 0;
    if (name_len < 1 || name_len > 8 || ext_len > 3) {
        fprintf(stderr, "%s '%s' is not a valid CP/M 8.3 filename: name max 8 chars, extension max 3 chars\n", label, value);
        return 0;
    }

    for (p = name; *p && p != dot; p++) {
        if (!is_cpm_filename_char((unsigned char)*p)) {
            fprintf(stderr, "%s '%s' contains invalid CP/M filename character '%c'\n", label, value, *p);
            return 0;
        }
    }
    if (dot) {
        for (p = dot + 1; *p; p++) {
            if (!is_cpm_filename_char((unsigned char)*p)) {
                fprintf(stderr, "%s '%s' contains invalid CP/M filename character '%c'\n", label, value, *p);
                return 0;
            }
        }
    }
    return 1;
}

static void upper_copy(char *dst, size_t dst_size, const char *src)
{
    size_t i;
    for (i = 0; src[i] && i + 1 < dst_size; i++)
        dst[i] = (char)toupper((unsigned char)src[i]);
    dst[i] = 0;
}

static void lower_copy(char *dst, size_t dst_size, const char *src)
{
    size_t i;
    for (i = 0; src[i] && i + 1 < dst_size; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = 0;
}

static int parse_bool(const char *value, int *out)
{
    if (str_ieq(value, "true") || str_ieq(value, "yes") ||
        str_ieq(value, "on") || !strcmp(value, "1")) {
        *out = 1;
        return 1;
    }
    if (str_ieq(value, "false") || str_ieq(value, "no") ||
        str_ieq(value, "off") || !strcmp(value, "0")) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int parse_int(const char *value, int *out)
{
    char *endp;
    long v;

    v = strtol(value, &endp, 0);
    if (*value == 0 || *endp != 0 || v < 0 || v > 32767)
        return 0;
    *out = (int)v;
    return 1;
}

static int expand_env_macros(const char *label, const char *value, char *out, size_t out_size)
{
    size_t out_pos;
    size_t i;

    if (out_size == 0)
        return 0;

    out_pos = 0;
    for (i = 0; value[i]; i++) {
        if (value[i] == '$' && value[i + 1] == '{') {
            char name[MAX_NAME_LEN];
            const char *env_value;
            size_t name_len;
            size_t j;

            i += 2;
            name_len = 0;
            while (value[i] && value[i] != '}') {
                if (name_len + 1 >= sizeof(name)) {
                    fprintf(stderr, "%s contains an environment macro name that is too long\n", label);
                    return 0;
                }
                name[name_len++] = value[i++];
            }
            if (value[i] != '}') {
                fprintf(stderr, "%s contains an unterminated environment macro\n", label);
                return 0;
            }
            if (name_len == 0) {
                fprintf(stderr, "%s contains an empty environment macro\n", label);
                return 0;
            }
            name[name_len] = 0;
            env_value = getenv(name);
            if (!env_value) {
                fprintf(stderr, "%s references unset environment variable %s\n", label, name);
                return 0;
            }
            for (j = 0; env_value[j]; j++) {
                if (out_pos + 1 >= out_size) {
                    fprintf(stderr, "%s is too long after environment macro expansion\n", label);
                    return 0;
                }
                out[out_pos++] = env_value[j];
            }
        } else {
            if (value[i] == '}') {
                fprintf(stderr, "%s contains an unmatched environment macro terminator\n", label);
                return 0;
            }
            if (out_pos + 1 >= out_size) {
                fprintf(stderr, "%s is too long after environment macro expansion\n", label);
                return 0;
            }
            out[out_pos++] = value[i];
        }
    }
    out[out_pos] = 0;
    return 1;
}

static int add_list(char items[MAX_ITEMS][MAX_PATH_LEN], int *count, const char *value)
{
    char buf[MAX_LINE_LEN];
    char *p;
    char *comma;

    copy_text(buf, sizeof(buf), value);
    p = buf;
    for (;;) {
        comma = strchr(p, ',');
        if (comma)
            *comma = 0;
        trim(p);
        if (*p) {
            if (*count >= MAX_ITEMS) {
                fprintf(stderr, "too many list entries\n");
                return 0;
            }
            copy_text(items[*count], MAX_PATH_LEN, p);
            (*count)++;
        }
        if (!comma)
            break;
        p = comma + 1;
    }
    return 1;
}

static int add_item(char items[MAX_ITEMS][MAX_PATH_LEN], int *count, const char *value)
{
    if (*count >= MAX_ITEMS) {
        fprintf(stderr, "too many list entries\n");
        return 0;
    }
    copy_text(items[*count], MAX_PATH_LEN, value);
    trim(items[*count]);
    if (items[*count][0])
        (*count)++;
    return 1;
}

static int add_whitespace_args(char items[MAX_ITEMS][MAX_PATH_LEN], int *count, const char *value)
{
    char buf[MAX_LINE_LEN];
    char *p;
    char *start;

    if (!value || !*value)
        return 1;
    copy_text(buf, sizeof(buf), value);
    p = buf;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        start = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (*p)
            *p++ = 0;
        if (!add_item(items, count, start))
            return 0;
    }
    return 1;
}

/* Resolve one of the pipeline tool commands (dcc/dccpeep/dccrtlstrip/ntvcm/
 * m80/l80) into `dst`: an explicit ENV_NAME override always wins; otherwise
 * prefer a same-directory build (LOCAL_NAME, e.g. "./dcc") if present, else
 * fall back to a bare command name resolved via PATH.
 *
 * On Windows, a locally-built tool is "./dcc.exe", never bare "./dcc" -
 * file_exists("./dcc") is always false there even when "./dcc.exe" is
 * sitting right next to it, silently falling through past the local build
 * to the bare "dcc" fallback. That fallback then fails outright: cmd.exe's
 * PATH+PATHEXT search (what run_cmd's system() call ultimately goes
 * through) has no reason to look in the current directory unless "." is on
 * PATH, and a freshly-built dcc.exe never is - so the whole build fails
 * with "The system cannot find the path specified." Try the ".exe"-
 * suffixed local path first on Windows so the same-directory build
 * resolves the same way it does everywhere else. This deliberately does
 * NOT touch the final bare-fallback case (PATH search for a name with no
 * local file found) - that already goes through cmd.exe's own correct
 * PATHEXT resolution, which knows the right extension (.exe/.com/.bat) for
 * whatever is actually on PATH; guessing ".exe" there could just as easily
 * break a working non-.exe PATH tool as fix a missing one. */
static void resolve_tool_path(char *dst, size_t dst_size, const char *env_name,
                               const char *local_name, const char *fallback)
{
    const char *v;
#ifdef _WIN32
    char buf[MAX_PATH_LEN];
#endif

    v = getenv(env_name);
    if (v && *v) {
        copy_text(dst, dst_size, v);
        return;
    }

#ifdef _WIN32
    if (local_name) {
        snprintf(buf, sizeof(buf), "%s.exe", local_name);
        if (file_exists(buf)) {
            copy_text(dst, dst_size, buf);
            return;
        }
    }
#endif
    if (local_name && file_exists(local_name)) {
        copy_text(dst, dst_size, local_name);
        return;
    }

    copy_text(dst, dst_size, fallback);
}

/* m80c, unlike dcc/dccpeep/dccrtlstrip, gets run via run_cmd_in_dir (needs
 * cwd = build_dir, same reason ntvcm+M80 does) - so if resolve_tool_path
 * picked the local-build fallback ("./m80c", relative to the ORIGINAL
 * working directory), it would silently resolve to the wrong file once that
 * chdir happens. Rewrite a same-directory relative path to absolute right
 * away; a bare PATH name (no separator) or an explicit env override is left
 * untouched, since those aren't cwd-relative in the first place. */
static void make_local_tool_path_absolute(char *path, size_t path_size)
{
    char cwd[MAX_PATH_LEN];
    char abs[MAX_PATH_LEN];
    int n;
    int has_sep = strchr(path, '/') != NULL || strchr(path, '\\') != NULL;

    if (!has_sep)
        return;
#ifdef _WIN32
    if (path[0] && path[1] == ':')
        return;
#else
    if (path[0] == '/')
        return;
#endif
    if (!GETCWD(cwd, sizeof(cwd)))
        return;
    n = snprintf(abs, sizeof(abs), "%.255s%c%.255s", cwd, PATH_SEP, path);
    if (n < 0 || (size_t)n >= sizeof(abs))
        return;
    copy_text(path, path_size, abs);
}

/* Same env-override-then-local-file-then-fallback resolution as
 * resolve_tool_path, but for a plain data file (DCCRTL.MAC) rather than an
 * executable - no platform-specific ".exe" suffix applies. */
static const char *env_or_default(const char *env_name, const char *local_name, const char *fallback)
{
    const char *v;
    v = getenv(env_name);
    if (v && *v)
        return v;
    if (local_name && file_exists(local_name))
        return local_name;
    return fallback;
}

static void init_config(struct Config *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->floatio = getenv("DCC_FLOATIO") && !strcmp(getenv("DCC_FLOATIO"), "1");
    cfg->no_floatio = getenv("DCC_NO_FLOATIO") && !strcmp(getenv("DCC_NO_FLOATIO"), "1");
    cfg->flongio = getenv("DCC_LONGIO") && !strcmp(getenv("DCC_LONGIO"), "1");
    cfg->no_longio = getenv("DCC_NO_LONGIO") && !strcmp(getenv("DCC_NO_LONGIO"), "1");
    cfg->hexio = getenv("DCC_HEXIO") && !strcmp(getenv("DCC_HEXIO"), "1");
    cfg->no_hexio = getenv("DCC_NO_HEXIO") && !strcmp(getenv("DCC_NO_HEXIO"), "1");
    cfg->octio = getenv("DCC_OCTIO") && !strcmp(getenv("DCC_OCTIO"), "1");
    cfg->no_octio = getenv("DCC_NO_OCTIO") && !strcmp(getenv("DCC_NO_OCTIO"), "1");
    cfg->stack_check = getenv("DCC_FORCE_STACK_CHECK") && !strcmp(getenv("DCC_FORCE_STACK_CHECK"), "1");
    cfg->no_narrow = getenv("DCC_NO_NARROW") && !strcmp(getenv("DCC_NO_NARROW"), "1");
    cfg->debug = getenv("DCC_DEBUG") && !strcmp(getenv("DCC_DEBUG"), "1");
    cfg->debug_lines = getenv("DCC_DEBUG_LINES") && !strcmp(getenv("DCC_DEBUG_LINES"), "1");
    if (cfg->debug_lines)
        cfg->debug = 0;
    cfg->stack_bytes = 512;
    cfg->peep = 1;
    /* Full -g builds skip dccpeep by default. dcc-peep-debug=true opts that
     * conservative codegen into assembly optimization; dcc-debug=lines is the
     * release-identical mode for optimized source breakpoints and stepping. */
    cfg->peep_debug = getenv("DCC_PEEP_DEBUG") && !strcmp(getenv("DCC_PEEP_DEBUG"), "1");
    cfg->dccpeep_undoc = getenv("DCC_ALLOW_UNDOCUMENTED_Z80") && !strcmp(getenv("DCC_ALLOW_UNDOCUMENTED_Z80"), "1");
    /* Native m80c is the default assembler (no Z80 emulation needed); set
     * DCC_USE_EMULATED_M80=1 to fall back to the real M80.COM under ntvcm,
     * e.g. to cross-check output or when m80c hasn't been built locally. */
    cfg->use_emulated_m80 = getenv("DCC_USE_EMULATED_M80") && !strcmp(getenv("DCC_USE_EMULATED_M80"), "1");
    /* Native l80c is the default linker (no Z80 emulation needed, and no
     * CP/M 64K linker-workspace ceiling on large nopeep builds); set
     * DCC_USE_EMULATED_L80=1 to fall back to the real L80.COM under ntvcm. */
    cfg->use_emulated_l80 = getenv("DCC_USE_EMULATED_L80") && !strcmp(getenv("DCC_USE_EMULATED_L80"), "1");
    copy_text(cfg->build_dir, sizeof(cfg->build_dir), "build");
    resolve_tool_path(cfg->dcc, sizeof(cfg->dcc), "DCC", LOCAL_DCC, "dcc");
    resolve_tool_path(cfg->dccpeep, sizeof(cfg->dccpeep), "DCCPEEP", LOCAL_DCCPEEP, "dccpeep");
    resolve_tool_path(cfg->dccrtlstrip, sizeof(cfg->dccrtlstrip), "DCCRTLSTRIP", LOCAL_DCCRTLSTRIP, "dccrtlstrip");
    resolve_tool_path(cfg->ntvcm, sizeof(cfg->ntvcm), "NTVCM", NULL, "ntvcm");
    resolve_tool_path(cfg->m80, sizeof(cfg->m80), "M80", NULL, "m80");
    resolve_tool_path(cfg->m80c, sizeof(cfg->m80c), "M80C", LOCAL_M80C, "m80c");
    make_local_tool_path_absolute(cfg->m80c, sizeof(cfg->m80c));
    resolve_tool_path(cfg->l80, sizeof(cfg->l80), "L80", NULL, "l80");
    resolve_tool_path(cfg->l80c, sizeof(cfg->l80c), "L80C", LOCAL_L80C, "l80c");
    make_local_tool_path_absolute(cfg->l80c, sizeof(cfg->l80c));
    copy_text(cfg->runtime, sizeof(cfg->runtime), env_or_default("DCC_RUNTIME", "DCCRTL.MAC", "DCCRTL.MAC"));
    add_whitespace_args(cfg->dcc_args, &cfg->dcc_arg_count, getenv("DCC_ARGS"));
}

static void promote_debug_compiler_arg(struct Config *cfg)
{
    int i;
    int kept = 0;
    for (i = 0; i < cfg->dcc_arg_count; i++) {
        if (!strcmp(cfg->dcc_args[i], "-g")) {
            if (!cfg->debug_lines)
                cfg->debug = 1;
            continue;
        }
        if (!strcmp(cfg->dcc_args[i], "-gline")) {
            cfg->debug = 0;
            cfg->debug_lines = 1;
            continue;
        }
        if (kept != i)
            copy_text(cfg->dcc_args[kept], sizeof(cfg->dcc_args[kept]), cfg->dcc_args[i]);
        kept++;
    }
    cfg->dcc_arg_count = kept;
}

static int apply_setting(struct Config *cfg, const char *raw_key, const char *value)
{
    char key[MAX_NAME_LEN];
    char expanded[MAX_LINE_LEN];
    int b;
    int n;

    normalize_key(key, sizeof(key), raw_key);
    if (!expand_env_macros(raw_key, value, expanded, sizeof(expanded)))
        return 0;
    value = expanded;
    if (!strcmp(key, "dcc-input")) {
        cfg->input_count = 0;
        return add_list(cfg->inputs, &cfg->input_count, value);
    }
    if (!strcmp(key, "dcc-output")) {
        copy_text(cfg->output, sizeof(cfg->output), value);
        trim(cfg->output);
        cfg->output_set = cfg->output[0] != 0;
        return 1;
    }
    if (!strcmp(key, "dcc-floatio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->floatio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-no-floatio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->no_floatio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-flongio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->flongio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-no-longio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->no_longio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-hexio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->hexio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-no-hexio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->no_hexio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-octio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->octio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-no-octio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->no_octio = b;
        return 1;
    }
    if (!strcmp(key, "dcc-stack-bytes")) {
        if (!parse_int(value, &n)) {
            fprintf(stderr, "invalid stack byte count for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->stack_bytes = n;
        return 1;
    }
    if (!strcmp(key, "dcc-stack-check")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->stack_check = b;
        return 1;
    }
    if (!strcmp(key, "dcc-no-narrow")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->no_narrow = b;
        return 1;
    }
    if (!strcmp(key, "dcc-include-directory") || !strcmp(key, "dcc-include")) {
        cfg->include_count = 0;
        return add_list(cfg->includes, &cfg->include_count, value);
    }
    if (!strcmp(key, "dcc-define") || !strcmp(key, "dcc-defines")) {
        char buf[MAX_LINE_LEN];
        char *p;
        char *comma;

        copy_text(buf, sizeof(buf), value);
        p = buf;
        for (;;) {
            comma = strchr(p, ',');
            if (comma)
                *comma = 0;
            trim(p);
            if (*p) {
                char arg[MAX_PATH_LEN];
                if (strlen(p) + 2 >= sizeof(arg)) {
                    fprintf(stderr, "dcc-define value too long: %s\n", p);
                    return 0;
                }
                snprintf(arg, sizeof(arg), "-D%s", p);
                if (!add_item(cfg->dcc_args, &cfg->dcc_arg_count, arg))
                    return 0;
            }
            if (!comma)
                break;
            p = comma + 1;
        }
        return 1;
    }
    if (!strcmp(key, "dcc-undefine") || !strcmp(key, "dcc-undefines")) {
        char buf[MAX_LINE_LEN];
        char *p;
        char *comma;

        copy_text(buf, sizeof(buf), value);
        p = buf;
        for (;;) {
            comma = strchr(p, ',');
            if (comma)
                *comma = 0;
            trim(p);
            if (*p) {
                char arg[MAX_PATH_LEN];
                if (strlen(p) + 2 >= sizeof(arg)) {
                    fprintf(stderr, "dcc-undefine value too long: %s\n", p);
                    return 0;
                }
                snprintf(arg, sizeof(arg), "-U%s", p);
                if (!add_item(cfg->dcc_args, &cfg->dcc_arg_count, arg))
                    return 0;
            }
            if (!comma)
                break;
            p = comma + 1;
        }
        return 1;
    }
    if (!strcmp(key, "dcc-peep")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->peep = b;
        return 1;
    }
    if (!strcmp(key, "dcc-peep-debug")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->peep_debug = b;
        return 1;
    }
    if (!strcmp(key, "dcc-debug")) {
        if (!strcmp(value, "lines") || !strcmp(value, "line")) {
            cfg->debug = 0;
            cfg->debug_lines = 1;
            return 1;
        }
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid debug mode for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->debug = b;
        cfg->debug_lines = 0;
        return 1;
    }
    if (!strcmp(key, "dcc-allow-undocumented-z80")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->dccpeep_undoc = b;
        return 1;
    }
    if (!strcmp(key, "dcc-build-dir")) {
        copy_text(cfg->build_dir, sizeof(cfg->build_dir), value);
        trim(cfg->build_dir);
        return cfg->build_dir[0] != 0;
    }
    if (!strcmp(key, "dcc-tool")) {
        copy_text(cfg->dcc, sizeof(cfg->dcc), value);
        trim(cfg->dcc);
        return cfg->dcc[0] != 0;
    }
    if (!strcmp(key, "dccpeep-tool")) {
        copy_text(cfg->dccpeep, sizeof(cfg->dccpeep), value);
        trim(cfg->dccpeep);
        return cfg->dccpeep[0] != 0;
    }
    if (!strcmp(key, "dccrtlstrip-tool")) {
        copy_text(cfg->dccrtlstrip, sizeof(cfg->dccrtlstrip), value);
        trim(cfg->dccrtlstrip);
        return cfg->dccrtlstrip[0] != 0;
    }
    if (!strcmp(key, "ntvcm-tool")) {
        copy_text(cfg->ntvcm, sizeof(cfg->ntvcm), value);
        trim(cfg->ntvcm);
        return cfg->ntvcm[0] != 0;
    }
    if (!strcmp(key, "m80-command")) {
        copy_text(cfg->m80, sizeof(cfg->m80), value);
        trim(cfg->m80);
        return cfg->m80[0] != 0;
    }
    if (!strcmp(key, "m80c-tool")) {
        copy_text(cfg->m80c, sizeof(cfg->m80c), value);
        trim(cfg->m80c);
        if (cfg->m80c[0])
            make_local_tool_path_absolute(cfg->m80c, sizeof(cfg->m80c));
        return cfg->m80c[0] != 0;
    }
    if (!strcmp(key, "dcc-use-emulated-m80")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->use_emulated_m80 = b;
        return 1;
    }
    if (!strcmp(key, "l80-command")) {
        copy_text(cfg->l80, sizeof(cfg->l80), value);
        trim(cfg->l80);
        return cfg->l80[0] != 0;
    }
    if (!strcmp(key, "l80c-tool")) {
        copy_text(cfg->l80c, sizeof(cfg->l80c), value);
        trim(cfg->l80c);
        if (cfg->l80c[0])
            make_local_tool_path_absolute(cfg->l80c, sizeof(cfg->l80c));
        return cfg->l80c[0] != 0;
    }
    if (!strcmp(key, "dcc-use-emulated-l80")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->use_emulated_l80 = b;
        return 1;
    }
    if (!strcmp(key, "dcc-runtime")) {
        copy_text(cfg->runtime, sizeof(cfg->runtime), value);
        trim(cfg->runtime);
        return cfg->runtime[0] != 0;
    }

    fprintf(stderr, "unknown dccmake setting: %s\n", raw_key);
    return 0;
}

static int read_config_file(struct Config *cfg, const char *path)
{
    FILE *f;
    char line[MAX_LINE_LEN];
    int lineno;

    f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT)
            return 1;
        fprintf(stderr, "cannot read %s: %s\n", path, strerror(errno));
        return 0;
    }

    lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        char *eq;
        char *comment;

        lineno++;
        line[strcspn(line, "\r\n")] = 0;
        comment = strchr(line, '#');
        if (comment)
            *comment = 0;
        trim(line);
        if (!line[0])
            continue;
        eq = strchr(line, '=');
        if (!eq) {
            fprintf(stderr, "%s:%d: expected key=value\n", path, lineno);
            fclose(f);
            return 0;
        }
        *eq = 0;
        trim(line);
        trim(eq + 1);
        if (!apply_setting(cfg, line, eq + 1)) {
            fclose(f);
            return 0;
        }
    }
    fclose(f);
    return 1;
}

static void print_help(void)
{
    printf("dccmake - build a dcc CP/M application\n");
    printf("usage: dccmake [-h|--help]\n");
    printf("       dccmake [key=value ...] [dcc-style-options]\n");
    printf("       dccmake --dcc-input main.c,module.c --dcc-output app\n");
    printf("\n");
    printf("dccmake reads optional dccmake.txt in the current directory, then applies\n");
    printf("command-line settings as overrides. Command-line settings may be written as\n");
    printf("key=value, --key=value, --key value, or positional .c inputs. Common dcc-style options are also\n");
    printf("accepted and forwarded where they make sense.\n");
    printf("\n");
    printf("build pipeline:\n");
    printf("  dcc each .c file to .MAC; files after the first use -module\n");
    printf("  optionally run dccpeep on generated app .MAC files\n");
    printf("  run dccrtlstrip with the first .MAC as the runtime root\n");
    printf("  assemble all .MAC files and RTLMIN.MAC with native m80c\n");
    printf("  (or ntvcm M80.COM if dcc-use-emulated-m80=true)\n");
    printf("  link RTLMIN plus all app modules with native l80c\n");
    printf("  (or ntvcm L80.COM if dcc-use-emulated-l80=true)\n");
    printf("\n");
    printf("dccmake.txt format:\n");
    printf("  One key=value setting per line. Blank lines are ignored. Text after # is a\n");
    printf("  comment. Comma-separated values may contain spaces around commas.\n");
    printf("  Values may reference environment variables as ${NAME}. Unset or malformed\n");
    printf("  environment-variable references are errors.\n");
    printf("  dcc-input basenames and dcc-output must be valid CP/M 8.3 names.\n");
    printf("\n");
    printf("  example:\n");
    printf("    dcc-input=main.c, module1.c, module2.c\n");
    printf("    dcc-output=main\n");
    printf("    dcc-floatio=false\n");
    printf("    dcc-flongio=false\n");
    printf("    dcc-stack-bytes=512\n");
    printf("    dcc-stack-check=false\n");
    printf("    dcc-no-narrow=false\n");
    printf("    dcc-include-directory=include, ../shared/include\n");
    printf("    dcc-define=DEBUG=1, TRACE\n");
    printf("    dcc-undefine=OLD\n");
    printf("    dcc-peep=true\n");
    printf("\n");
    printf("settings:\n");
    printf("  dcc-input=main.c,module1.c     comma-separated C sources; or pass .c files positionally\n");
    printf("  dcc-output=main                output base name; default first input base\n");
    printf("  dcc-floatio=false|true|1|0     force %%f support on every printf-family call\n");
    printf("                                 (normally auto-detected per call from its own\n");
    printf("                                 literal format string; only needed when a format\n");
    printf("                                 string isn't a compile-time literal)\n");
    printf("  dcc-no-floatio=false|true|1|0  opposite: force every call to NOT support %%f,\n");
    printf("                                 even one whose literal uses it, or the fallback\n");
    printf("                                 for a non-literal format string - use only when\n");
    printf("                                 no call site anywhere needs it\n");
    printf("  dcc-flongio=false|true|1|0     same, but forces long formats (%%ld/%%lu/%%lx)\n");
    printf("  dcc-no-longio=false|true|1|0   dcc-no-floatio, but for long formats\n");
    printf("  dcc-hexio=false|true|1|0       force %%x/%%X support on every call\n");
    printf("  dcc-no-hexio=false|true|1|0    dcc-no-floatio, but for %%x/%%X\n");
    printf("  dcc-octio=false|true|1|0       force %%o support on every call\n");
    printf("  dcc-no-octio=false|true|1|0    dcc-no-floatio, but for %%o\n");
    printf("  dcc-stack-bytes=512            pass -stack bytes to dcc; default 512\n");
    printf("  dcc-stack-check=false|true|1|0 pass -fstack-check to dcc\n");
    printf("  dcc-no-narrow=false|true|1|0   pass -fno-narrow to dcc (disable byte-narrowing passes)\n");
    printf("  dcc-debug=false|true|lines     full debug metadata, or optimized ranged locations;\n");
    printf("                                 both require native m80c. Project 'lines' refines a CLI -g\n");
    printf("  dcc-include-directory=dir,...  include dirs; dcc-include is an alias\n");
    printf("  dcc-define=NAME[=value],...    pass -D values to dcc\n");
    printf("  dcc-undefine=NAME,...          pass -U values to dcc\n");
    printf("  dcc-peep=true|false|1|0        run dccpeep; default true\n");
    printf("                                 ignored with -g unless dcc-peep-debug=true,\n");
    printf("                                 to preserve debug locations by default\n");
    printf("  dcc-peep-debug=false|true|1|0  run dccpeep even with -g; default false.\n");
    printf("                                 Debug markers are retained/remapped, but full -g\n");
    printf("                                 still uses conservative compiler codegen. Use\n");
    printf("                                 dcc-debug=lines for release-identical optimized code\n");
    printf("  dcc-allow-undocumented-z80=false|true|1|0\n");
    printf("                                 pass -fundocumented-z80 to dccpeep; default false\n");
    printf("  dcc-build-dir=build            artifact directory; default build\n");
    printf("  dcc-use-emulated-m80=false|true|1|0\n");
    printf("                                 assemble with real M80.COM under ntvcm instead\n");
    printf("                                 of native m80c; default false\n");
    printf("  dcc-use-emulated-l80=false|true|1|0\n");
    printf("                                 link with real L80.COM under ntvcm instead\n");
    printf("                                 of native l80c; default false. Real L80 runs\n");
    printf("                                 inside ntvcm's emulated 64K CP/M address space,\n");
    printf("                                 so its own symbol/relocation workspace can run\n");
    printf("                                 out of memory on large nopeep builds well before\n");
    printf("                                 the target program itself would not fit - l80c\n");
    printf("                                 has no such ceiling\n");
    printf("\n");
    printf("dcc-style command options:\n");
    printf("  -f, -ffloatio                  same as dcc-floatio=true\n");
    printf("  -fno-floatio                   same as dcc-no-floatio=true\n");
    printf("  -fl, -flongio                  same as dcc-flongio=true\n");
    printf("  -fno-longio                    same as dcc-no-longio=true\n");
    printf("  -fhexio                        same as dcc-hexio=true\n");
    printf("  -fno-hexio                     same as dcc-no-hexio=true\n");
    printf("  -foctio                        same as dcc-octio=true\n");
    printf("  -fno-octio                     same as dcc-no-octio=true\n");
    printf("  -s <bytes>, -stack <bytes>     same as dcc-stack-bytes=<bytes>\n");
    printf("  -fstack-check                  same as dcc-stack-check=true\n");
    printf("  -fno-narrow                    same as dcc-no-narrow=true\n");
    printf("  -g                             same as dcc-debug=true\n");
    printf("  -gline                         same as dcc-debug=lines\n");
    printf("  -femulated-m80                 same as dcc-use-emulated-m80=true\n");
    printf("  -femulated-l80                 same as dcc-use-emulated-l80=true\n");
    printf("  -I <dir>, -Idir                add an include directory\n");
    printf("  -D <name>[=value], -Dname=val  pass a define to dcc\n");
    printf("  -U <name>, -Uname              pass an undefine to dcc\n");
    printf("  -v, --version                  print dccmake version\n");
    printf("  -c, -module                    not needed: dccmake uses -module for inputs\n");
    printf("                                after the first source file\n");
    printf("\n");
    printf("tool settings:\n");
    printf("  dcc-tool=dcc                  dcc compiler command\n");
    printf("  dccpeep-tool=dccpeep          peephole optimizer command\n");
    printf("  dccrtlstrip-tool=dccrtlstrip  runtime stripper command\n");
    printf("  ntvcm-tool=ntvcm              emulator command for M80/L80 (if either is emulated)\n");
    printf("  m80-command=m80               CP/M assembler command passed to ntvcm (emulated M80 only)\n");
    printf("  m80c-tool=m80c                native host assembler command (default, no ntvcm)\n");
    printf("  l80-command=l80               CP/M linker command passed to ntvcm (emulated L80 only)\n");
    printf("  l80c-tool=l80c                native host linker command (default, no ntvcm)\n");
    printf("  dcc-runtime=DCCRTL.MAC        runtime source used by dccrtlstrip\n");
    printf("\n");
    printf("environment macro examples:\n");
    printf("  dccrtlstrip-tool=${DCC_DIR}/dccrtlstrip\n");
    printf("  ntvcm-tool=${NTVCM_DIR}/ntvcm\n");
    printf("  m80-command=${DCC_DIR}/m80.com\n");
    printf("  l80-command=${DCC_DIR}/l80.com\n");
}

static int is_positional_c_source(const char *arg)
{
    size_t len;

    len = strlen(arg);
    return len > 2 && arg[len - 2] == '.' &&
        (arg[len - 1] == 'c' || arg[len - 1] == 'C');
}

static int parse_args(struct Config *cfg, int argc, char **argv)
{
    int i;

    for (i = 1; i < argc; i++) {
        char key[MAX_NAME_LEN];
        char value[MAX_LINE_LEN];
        char *eq;
        const char *arg;

        arg = argv[i];
        if (!strcmp(arg, "--help") || !strcmp(arg, "-h") || !strcmp(arg, "/?")) {
            print_help();
            exit(0);
        }
        if (!strcmp(arg, "-v") || !strcmp(arg, "--version")) {
            printf("dccmake 1.0\n");
            exit(0);
        }
        if (!strcmp(arg, "-f") || !strcmp(arg, "-ffloatio")) {
            cfg->floatio = 1;
            continue;
        }
        if (!strcmp(arg, "-fno-floatio")) {
            cfg->no_floatio = 1;
            continue;
        }
        if (!strcmp(arg, "-fl") || !strcmp(arg, "-flongio")) {
            cfg->flongio = 1;
            continue;
        }
        if (!strcmp(arg, "-fno-longio")) {
            cfg->no_longio = 1;
            continue;
        }
        if (!strcmp(arg, "-fhexio")) {
            cfg->hexio = 1;
            continue;
        }
        if (!strcmp(arg, "-fno-hexio")) {
            cfg->no_hexio = 1;
            continue;
        }
        if (!strcmp(arg, "-foctio")) {
            cfg->octio = 1;
            continue;
        }
        if (!strcmp(arg, "-fno-octio")) {
            cfg->no_octio = 1;
            continue;
        }
        if (!strcmp(arg, "-fstack-check")) {
            cfg->stack_check = 1;
            continue;
        }
        if (!strcmp(arg, "-fno-narrow")) {
            cfg->no_narrow = 1;
            continue;
        }
        if (!strcmp(arg, "-g")) {
            if (!cfg->debug_lines)
                cfg->debug = 1;
            continue;
        }
        if (!strcmp(arg, "-gline")) {
            cfg->debug = 0;
            cfg->debug_lines = 1;
            continue;
        }
        if (!strcmp(arg, "-femulated-m80")) {
            cfg->use_emulated_m80 = 1;
            continue;
        }
        if (!strcmp(arg, "-femulated-l80")) {
            cfg->use_emulated_l80 = 1;
            continue;
        }
        if (!strcmp(arg, "-s") || !strcmp(arg, "-stack")) {
            int n;
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", argv[i]);
                return 0;
            }
            if (!parse_int(argv[++i], &n)) {
                fprintf(stderr, "invalid stack byte count for %s: %s\n", arg, argv[i]);
                return 0;
            }
            cfg->stack_bytes = n;
            continue;
        }
        if (!strncmp(arg, "-stack=", 7)) {
            int n;
            if (!parse_int(arg + 7, &n)) {
                fprintf(stderr, "invalid stack byte count for -stack: %s\n", arg + 7);
                return 0;
            }
            cfg->stack_bytes = n;
            continue;
        }
        if (!strcmp(arg, "-I")) {
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for -I\n");
                return 0;
            }
            if (!add_item(cfg->includes, &cfg->include_count, argv[++i]))
                return 0;
            continue;
        }
        if (!strncmp(arg, "-I", 2) && arg[2]) {
            if (!add_item(cfg->includes, &cfg->include_count, arg + 2))
                return 0;
            continue;
        }
        if (!strcmp(arg, "-D")) {
            char define_arg[MAX_PATH_LEN];
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for -D\n");
                return 0;
            }
            snprintf(define_arg, sizeof(define_arg), "-D%s", argv[++i]);
            if (!add_item(cfg->dcc_args, &cfg->dcc_arg_count, define_arg))
                return 0;
            continue;
        }
        if (!strncmp(arg, "-D", 2) && arg[2]) {
            if (!add_item(cfg->dcc_args, &cfg->dcc_arg_count, arg))
                return 0;
            continue;
        }
        if (!strcmp(arg, "-U")) {
            char undef_arg[MAX_PATH_LEN];
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for -U\n");
                return 0;
            }
            snprintf(undef_arg, sizeof(undef_arg), "-U%s", argv[++i]);
            if (!add_item(cfg->dcc_args, &cfg->dcc_arg_count, undef_arg))
                return 0;
            continue;
        }
        if (!strncmp(arg, "-U", 2) && arg[2]) {
            if (!add_item(cfg->dcc_args, &cfg->dcc_arg_count, arg))
                return 0;
            continue;
        }
        if (!strcmp(arg, "-c") || !strcmp(arg, "-module")) {
            fprintf(stderr, "%s is managed by dccmake; inputs after the first are compiled with -module\n", arg);
            return 0;
        }
        if (!strncmp(arg, "--", 2))
            arg += 2;
        eq = strchr(arg, '=');
        if (eq) {
            size_t n;
            n = (size_t)(eq - arg);
            if (n >= sizeof(key))
                n = sizeof(key) - 1;
            memcpy(key, arg, n);
            key[n] = 0;
            copy_text(value, sizeof(value), eq + 1);
        } else {
            if (is_positional_c_source(arg)) {
                if (!add_item(cfg->inputs, &cfg->input_count, arg))
                    return 0;
                continue;
            }
            if (i + 1 >= argc) {
                fprintf(stderr, "missing value for %s\n", argv[i]);
                return 0;
            }
            copy_text(key, sizeof(key), arg);
            copy_text(value, sizeof(value), argv[++i]);
        }
        trim(key);
        trim(value);
        if (!apply_setting(cfg, key, value))
            return 0;
    }
    return 1;
}

static void cmd_init(char *cmd, size_t cmd_size)
{
    if (cmd_size)
        cmd[0] = 0;
}

static int cmd_append_raw(char *cmd, size_t cmd_size, const char *text)
{
    size_t used;
    size_t need;

    used = strlen(cmd);
    need = strlen(text);
    if (used + need + 1 >= cmd_size) {
        fprintf(stderr, "command line too long\n");
        return 0;
    }
    strcat(cmd, text);
    return 1;
}

static int cmd_arg(char *cmd, size_t cmd_size, const char *arg)
{
    const char *p;

    if (cmd[0] && !cmd_append_raw(cmd, cmd_size, " "))
        return 0;
#ifdef _WIN32
    if (!cmd_append_raw(cmd, cmd_size, "\""))
        return 0;
    for (p = arg; *p; p++) {
        char two[3];
        if (*p == '"') {
            two[0] = '\\';
            two[1] = *p;
            two[2] = 0;
            if (!cmd_append_raw(cmd, cmd_size, two))
                return 0;
        } else {
            two[0] = *p;
            two[1] = 0;
            if (!cmd_append_raw(cmd, cmd_size, two))
                return 0;
        }
    }
    return cmd_append_raw(cmd, cmd_size, "\"");
#else
    if (!cmd_append_raw(cmd, cmd_size, "'"))
        return 0;
    for (p = arg; *p; p++) {
        char two[2];
        if (*p == '\'') {
            if (!cmd_append_raw(cmd, cmd_size, "'\\''"))
                return 0;
        } else {
            two[0] = *p;
            two[1] = 0;
            if (!cmd_append_raw(cmd, cmd_size, two))
                return 0;
        }
    }
    return cmd_append_raw(cmd, cmd_size, "'");
#endif
}

#ifdef _WIN32
#define RUN_CMD_MAX_ARGV 4096

/* Parses a command string built entirely out of cmd_arg's Windows
 * convention above - every argument wrapped in "..." with any literal "
 * escaped as \" - back into an argv array, dequoting in place inside buf
 * (which the caller owns and must keep alive as long as argv is used).
 * Returns the argument count, or -1 if buf isn't in that exact shape
 * (defensive only: every caller in this file builds `cmd` via cmd_arg, so
 * that should never happen).
 *
 * Known limitation, inherited from cmd_arg's own quoting convention rather
 * than introduced here: an argument ending in a literal trailing backslash
 * (e.g. "path\") is ambiguous - cmd_arg only escapes ", never \, so the
 * built text ends in \" indistinguishable from an escaped quote. No caller
 * in this file can produce that (every path comes from path_join, which
 * always ends in a filename, never a bare separator), so this is untested
 * and unfixed rather than silently mishandling a case that can't occur. */
static int parse_quoted_argv(char *buf, char **argv, int max_argv)
{
    char *p = buf;
    char *w;
    int argc = 0;

    for (;;) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (*p != '"' || argc >= max_argv - 1)
            return -1;
        p++;
        argv[argc++] = p;
        w = p;
        while (*p && *p != '"') {
            if (*p == '\\' && p[1] == '"') {
                *w++ = '"';
                p += 2;
            } else {
                *w++ = *p++;
            }
        }
        if (*p != '"')
            return -1; /* unterminated quote */
        p++;
        *w = 0;
    }
    argv[argc] = NULL;
    return argc;
}

/* Bypasses cmd.exe entirely via _spawnvp (-> CreateProcess directly), the
 * one difference between this and the system()-based POSIX path below.
 * system() on Windows always shells out through "cmd.exe /c <cmd>", which
 * scripts/bench-pwsh-overhead.ps1 measured at ~7ms of pure shell-launch
 * overhead per call on native Windows (vs sub-1ms for an equivalent spawn
 * on Linux/macOS) - multiplied by the 6 run_cmd/run_cmd_in_dir calls in a
 * single app build, across a few hundred apps in the full suite, that's
 * tens of seconds of overhead with no relation to the actual work being
 * done. _spawnvp needs a plain argv array rather than a shell command
 * line, so parse_quoted_argv above recovers one from the exact quoting
 * convention cmd_arg always uses - every caller of run_cmd in this file
 * builds `cmd` that way, so this never needs to handle arbitrary shell
 * syntax (no `&&`, pipes, or redirection - run_cmd_in_dir above already
 * sidesteps needing any of that for the one caller that used to want it). */
static int run_cmd(const char *cmd)
{
    char *buf;
    char *argv[RUN_CMD_MAX_ARGV];
    int argc;
    intptr_t rc;

    printf("+ %s\n", cmd);
    buf = _strdup(cmd);
    if (!buf) {
        fprintf(stderr, "out of memory building command line\n");
        return 0;
    }
    argc = parse_quoted_argv(buf, argv, RUN_CMD_MAX_ARGV);
    if (argc <= 0) {
        fprintf(stderr, "command failed: %s\n", cmd);
        free(buf);
        return 0;
    }
    rc = _spawnvp(_P_WAIT, argv[0], (const char * const *)argv);
    free(buf);
    if (rc != 0) {
        if (rc < 0)
            fprintf(stderr, "command failed: %s: %s\n", cmd, strerror(errno));
        else
            fprintf(stderr, "command failed: %s\n", cmd);
        return 0;
    }
    return 1;
}
#else
static int run_cmd(const char *cmd)
{
    int rc;
    printf("+ %s\n", cmd);
    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "command failed: %s\n", cmd);
        return 0;
    }
    return 1;
}
#endif

/* Run `inner_cmd` with the process's working directory temporarily switched
 * to `dir`, restoring the original directory afterward regardless of the
 * command's outcome. This used to be done as a single compound shell
 * command ("cd /d dir && inner_cmd" on Windows, "cd dir && inner_cmd"
 * elsewhere) passed straight to run_cmd, but on Windows that shape doesn't
 * benefit from run_cmd's own quote-wrapping fix: once a command contains
 * both a shell operator (&&) and several individually-quoted arguments,
 * cmd.exe's /c quote-stripping mangles it regardless of how the whole
 * string is wrapped (confirmed empirically - wrapping the entire compound
 * command in an extra pair of quotes does not fix it the way it fixes a
 * plain multi-argument command). Actually changing directory sidesteps that
 * shell-quoting fragility entirely: run_cmd only ever sees a plain
 * program-plus-arguments command, the one shape it's already correct for. */
static int run_cmd_in_dir(const char *dir, const char *inner_cmd)
{
    char saved_cwd[MAX_PATH_LEN];
    int ok;

    if (!GETCWD(saved_cwd, sizeof(saved_cwd))) {
        fprintf(stderr, "cannot determine current directory\n");
        return 0;
    }
    if (CHDIR(dir) != 0) {
        fprintf(stderr, "cannot cd to %s: %s\n", dir, strerror(errno));
        return 0;
    }
    ok = run_cmd(inner_cmd);
    if (CHDIR(saved_cwd) != 0) {
        fprintf(stderr, "cannot cd back to %s: %s\n", saved_cwd, strerror(errno));
        return 0;
    }
    return ok;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in;
    FILE *out;
    char buf[65536];
    size_t n;

    in = fopen(src, "rb");
    if (!in) {
        fprintf(stderr, "cannot open %s: %s\n", src, strerror(errno));
        return 0;
    }
    out = fopen(dst, "wb");
    if (!out) {
        fprintf(stderr, "cannot write %s: %s\n", dst, strerror(errno));
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fprintf(stderr, "write failed for %s\n", dst);
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    fclose(out);
    return 1;
}

static int same_file(const char *a, const char *b)
{
    struct stat sa;
    struct stat sb;

    if (!strcmp(a, b))
        return 1;
    if (stat(a, &sa) != 0 || stat(b, &sb) != 0)
        return 0;
    return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
}

static int to_crlf(const char *path)
{
    FILE *in;
    FILE *out;
    char tmp[MAX_PATH_LEN];
    /* Block I/O instead of fgetc/fputc per byte: on this file (a
     * full RTL-source copy up to ~600KB, redone for every app built)
     * per-call stdio overhead - especially Win32 CRT/filesystem
     * latency - dominated dccmake's unaccounted "other" time bucket.
     * outbuf is 2x inbuf since every byte could be a lone '\n'
     * (worst case one '\r' inserted per input byte). */
    unsigned char inbuf[65536];
    unsigned char outbuf[2 * sizeof(inbuf)];
    size_t n;
    int prev;

    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "cannot open %s: %s\n", path, strerror(errno));
        return 0;
    }
    out = fopen(tmp, "wb");
    if (!out) {
        fprintf(stderr, "cannot write %s: %s\n", tmp, strerror(errno));
        fclose(in);
        return 0;
    }
    prev = 0;
    while ((n = fread(inbuf, 1, sizeof(inbuf), in)) > 0) {
        size_t i;
        size_t outlen = 0;
        for (i = 0; i < n; i++) {
            unsigned char c = inbuf[i];
            if (c == '\n' && prev != '\r')
                outbuf[outlen++] = '\r';
            outbuf[outlen++] = c;
            prev = c;
        }
        if (fwrite(outbuf, 1, outlen, out) != outlen) {
            fprintf(stderr, "write failed for %s\n", tmp);
            fclose(in);
            fclose(out);
            return 0;
        }
    }
    fclose(in);
    if (fclose(out) != 0) {
        fprintf(stderr, "write failed for %s\n", tmp);
        return 0;
    }
    if (remove(path) != 0 || rename(tmp, path) != 0) {
        fprintf(stderr, "cannot replace %s\n", path);
        return 0;
    }
    return 1;
}

/* Scan an M80 listing (.PRN) file for its "N Fatal error(s)" summary line.
 * M80 does not fail the process when it reports errors, and even lets L80
 * link straight past an undefined external - so file-existence and exit-
 * code checks alone can miss a build with real assembler errors. Confirmed
 * by a real user hitting exactly this: a dccrtlstrip block-splitting bug
 * left pf_hex_store_nz/pf_hforce declared public but never defined, M80
 * printed "10 Fatal error(s)" for tests/adaint.c, and the pipeline still
 * produced a .COM anyway. Returns 1 (clean) if the listing has no such
 * line, or if the count on it is 0; 0 (fatal errors present) otherwise,
 * after printing the offending line so the cause is visible. Missing file
 * (e.g. the assembly step itself already failed and never wrote one) is
 * not this function's concern - callers check file_exists separately. */
static int check_no_fatal_errors(const char *prn_path)
{
    FILE *f;
    char line[MAX_LINE_LEN];
    int ok;

    f = fopen(prn_path, "r");
    if (!f)
        return 1;

    ok = 1;
    while (fgets(line, sizeof(line), f)) {
        char *p;
        char *endptr;
        long count;

        trim(line);
        p = line;
        if (!isdigit((unsigned char)*p))
            continue;
        count = strtol(p, &endptr, 10);
        if (endptr == p || count <= 0)
            continue;
        while (*endptr == ' ')
            endptr++;
        if (strncmp(endptr, "Fatal error", 11) == 0) {
            fprintf(stderr, "assembler reported errors in %s: %s\n", prn_path, line);
            ok = 0;
        }
    }
    fclose(f);
    return ok;
}

static int read_link_sizes(const char *path, long *code_size, long *data_size)
{
    FILE *f;
    char line[MAX_LINE_LEN];

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot read link metadata: %s\n", path);
        return 0;
    }
    *code_size = -1;
    *data_size = -1;
    while (fgets(line, sizeof(line), f)) {
        (void)sscanf(line, "size code %ld", code_size);
        (void)sscanf(line, "size data %ld", data_size);
    }
    fclose(f);
    if (*code_size >= 0 && *data_size >= 0)
        return 1;
    fprintf(stderr, "missing segment size in link metadata: %s\n", path);
    return 0;
}

static int remap_debug_type(int type, int struct_id_base)
{
    if (type & 128)
        return (type & 255) | (((type >> 8) + struct_id_base) << 8);
    return type;
}

static int append_absolute_debug(FILE *out, const char *path, long code_base, long data_base,
                                 int *next_struct_id)
{
    FILE *in;
    char line[MAX_LINE_LEN];
    int struct_id_base = *next_struct_id - 1;
    int max_struct_id = 0;

    in = fopen(path, "r");
    if (!in) {
        fprintf(stderr, "cannot read debug metadata: %s\n", path);
        return 0;
    }
    while (fgets(line, sizeof(line), in)) {
        unsigned long offset;
        long source_line;
        char *quoted;
        char first_name[128];
        char second_name[128];
        int type;
        int id;
        int size;
        int is_union;
        int consumed;
        if (sscanf(line, "line code %lx %ld", &offset, &source_line) == 2) {
            quoted = strchr(line, '"');
            if (!quoted) {
                fclose(in);
                fprintf(stderr, "malformed debug line in %s\n", path);
                return 0;
            }
            fprintf(out, "line %04lX %ld %s", (code_base + (long)offset) & 0xffffL,
                    source_line, quoted);
        } else if (sscanf(line, "symbol code %lx", &offset) == 1) {
            quoted = strchr(line, '"');
            if (quoted)
                fprintf(out, "symbol %04lX %s", (code_base + (long)offset) & 0xffffL, quoted);
        } else if (sscanf(line, "function-begin code %lx", &offset) == 1) {
            quoted = strchr(line, '"');
            if (quoted)
                fprintf(out, "function-begin %04lX %s", (code_base + (long)offset) & 0xffffL, quoted);
        } else if (sscanf(line, "function-end code %lx", &offset) == 1) {
            quoted = strchr(line, '"');
            if (quoted)
                fprintf(out, "function-end %04lX %s", (code_base + (long)offset) & 0xffffL, quoted);
        } else if (sscanf(line, "variable code %lx \"%127[^\"]\" \"%127[^\"]\" %d %n",
                          &offset, first_name, second_name, &type, &consumed) == 4) {
            fprintf(out, "variable %04lX \"%s\" \"%s\" %d %s",
                    (code_base + (long)offset) & 0xffffL, first_name, second_name,
                    remap_debug_type(type, struct_id_base), line + consumed);
        } else if (sscanf(line, "variable-end code %lx", &offset) == 1) {
            quoted = strchr(line, '"');
            if (quoted)
                fprintf(out, "variable-end %04lX %s", (code_base + (long)offset) & 0xffffL, quoted);
        } else if (sscanf(line, "location code %lx", &offset) == 1) {
            quoted = strchr(line, '"');
            if (quoted)
                fprintf(out, "location %04lX %s", (code_base + (long)offset) & 0xffffL, quoted);
        } else if (sscanf(line, "global data %lx \"%127[^\"]\" \"%127[^\"]\" %d %n",
                          &offset, first_name, second_name, &type, &consumed) == 4) {
            fprintf(out, "global %04lX \"%s\" \"%s\" %d %s",
                    (data_base + (long)offset) & 0xffffL, first_name, second_name,
                    remap_debug_type(type, struct_id_base), line + consumed);
        } else if (sscanf(line, "global code %lx \"%127[^\"]\" \"%127[^\"]\" %d %n",
                          &offset, first_name, second_name, &type, &consumed) == 4) {
            fprintf(out, "global %04lX \"%s\" \"%s\" %d %s",
                    (code_base + (long)offset) & 0xffffL, first_name, second_name,
                    remap_debug_type(type, struct_id_base), line + consumed);
        } else if (sscanf(line, "struct %d %d %d \"%127[^\"]\"", &id, &size,
                          &is_union, first_name) == 4) {
            fprintf(out, "struct %d %d %d \"%s\"\n", id + struct_id_base, size,
                    is_union, first_name);
            if (id > max_struct_id)
                max_struct_id = id;
        } else if (sscanf(line, "field %d \"%127[^\"]\" %d %n", &id, first_name,
                          &type, &consumed) == 3) {
            fprintf(out, "field %d \"%s\" %d %s", id + struct_id_base, first_name,
                    remap_debug_type(type, struct_id_base), line + consumed);
        }
    }
    fclose(in);
    *next_struct_id += max_struct_id;
    return 1;
}

static int finalize_debug_file(const char *output_path, const char *rtl_link,
                               char debug_paths[MAX_ITEMS][MAX_PATH_LEN],
                               char link_paths[MAX_ITEMS][MAX_PATH_LEN], int count)
{
    char tmp[MAX_PATH_LEN];
    FILE *out;
    long code_base;
    long data_base;
    long rtl_code_size;
    long rtl_data_size;
    long code_sizes[MAX_ITEMS];
    long data_sizes[MAX_ITEMS];
    int i;
    int next_struct_id = 1;

    if (snprintf(tmp, sizeof(tmp), "%s.tmp", output_path) < 0 || strlen(tmp) >= sizeof(tmp))
        return 0;
    if (!read_link_sizes(rtl_link, &rtl_code_size, &rtl_data_size))
        return 0;
    data_base = 0x100L + rtl_code_size;
    for (i = 0; i < count; i++) {
        if (!read_link_sizes(link_paths[i], &code_sizes[i], &data_sizes[i]))
            return 0;
        data_base += code_sizes[i];
    }
    data_base += rtl_data_size;
    code_base = 0x100L + rtl_code_size;
    out = fopen(tmp, "w");
    if (!out) {
        fprintf(stderr, "cannot create final debug metadata: %s\n", tmp);
        return 0;
    }
    fprintf(out, "DCCDBG 2\n");
    for (i = 1; i < count; i++) {
        if (!append_absolute_debug(out, debug_paths[i], code_base, data_base, &next_struct_id)) {
            fclose(out);
            remove(tmp);
            return 0;
        }
        code_base += code_sizes[i];
        data_base += data_sizes[i];
    }
    if (count > 0 &&
        !append_absolute_debug(out, debug_paths[0], code_base, data_base, &next_struct_id)) {
        fclose(out);
        remove(tmp);
        return 0;
    }
    if (fclose(out) != 0 || remove(output_path) != 0 || rename(tmp, output_path) != 0) {
        fprintf(stderr, "cannot replace final debug metadata: %s\n", output_path);
        remove(tmp);
        return 0;
    }
    return 1;
}

static int maybe_copy_tool(const char *name, const char *build_dir)
{
    char dst[MAX_PATH_LEN];
    if (!file_exists(name))
        return 1;
    path_join(dst, sizeof(dst), build_dir, name);
    return copy_file(name, dst);
}

static int build_dcc_command(struct Config *cfg, int index, const char *input,
                             const char *mac_path, char *cmd, size_t cmd_size)
{
    int i;
    char stack_buf[32];

    cmd_init(cmd, cmd_size);
    if (!cmd_arg(cmd, cmd_size, cfg->dcc)) return 0;
    if (index > 0 && !cmd_arg(cmd, cmd_size, "-module")) return 0;
    if (cfg->stack_check && !cmd_arg(cmd, cmd_size, "-fstack-check")) return 0;
    if (cfg->no_narrow && !cmd_arg(cmd, cmd_size, "-fno-narrow")) return 0;
    if (cfg->debug && !cmd_arg(cmd, cmd_size, "-g")) return 0;
    if (cfg->debug_lines && !cmd_arg(cmd, cmd_size, "-gline")) return 0;
    if (!cmd_arg(cmd, cmd_size, "-stack")) return 0;
    snprintf(stack_buf, sizeof(stack_buf), "%d", cfg->stack_bytes);
    if (!cmd_arg(cmd, cmd_size, stack_buf)) return 0;
    if (cfg->floatio && !cmd_arg(cmd, cmd_size, "-ffloatio")) return 0;
    if (cfg->no_floatio && !cmd_arg(cmd, cmd_size, "-fno-floatio")) return 0;
    if (cfg->flongio && !cmd_arg(cmd, cmd_size, "-flongio")) return 0;
    if (cfg->no_longio && !cmd_arg(cmd, cmd_size, "-fno-longio")) return 0;
    if (cfg->hexio && !cmd_arg(cmd, cmd_size, "-fhexio")) return 0;
    if (cfg->no_hexio && !cmd_arg(cmd, cmd_size, "-fno-hexio")) return 0;
    if (cfg->octio && !cmd_arg(cmd, cmd_size, "-foctio")) return 0;
    if (cfg->no_octio && !cmd_arg(cmd, cmd_size, "-fno-octio")) return 0;
    for (i = 0; i < cfg->include_count; i++) {
        if (!cmd_arg(cmd, cmd_size, "-I")) return 0;
        if (!cmd_arg(cmd, cmd_size, cfg->includes[i])) return 0;
    }
    for (i = 0; i < cfg->dcc_arg_count; i++) {
        if (!cmd_arg(cmd, cmd_size, cfg->dcc_args[i])) return 0;
    }
    if (!cmd_arg(cmd, cmd_size, input)) return 0;
    if (!cmd_arg(cmd, cmd_size, "-o")) return 0;
    if (!cmd_arg(cmd, cmd_size, mac_path)) return 0;
    return 1;
}

/* Builds the M80-style "obj,prn=source /X /O /Z /L" assembly command, either
 * for native m80c (default: runs directly, no emulator) or, when
 * cfg->use_emulated_m80 is set, for the real M80.COM under ntvcm (as
 * before). mac_arg is the M80-style "=NAME.MAC" source argument. */
static int build_m80_command(struct Config *cfg, const char *mac_arg,
                              char *cmd, size_t cmd_size)
{
    cmd_init(cmd, cmd_size);
    if (cfg->use_emulated_m80) {
        if (!cmd_arg(cmd, cmd_size, cfg->ntvcm)) return 0;
        if (!cmd_arg(cmd, cmd_size, cfg->m80)) return 0;
    } else {
        if (!cmd_arg(cmd, cmd_size, cfg->m80c)) return 0;
    }
    if (!cmd_arg(cmd, cmd_size, mac_arg)) return 0;
    if (!cmd_arg(cmd, cmd_size, "/X")) return 0;
    if (!cmd_arg(cmd, cmd_size, "/O")) return 0;
    if (!cmd_arg(cmd, cmd_size, "/Z")) return 0;
    if (!cmd_arg(cmd, cmd_size, "/L")) return 0;
    /* /C: per-module <NAME>.SYM with every symbol, public and local, each
     * tagged with its segment (m80c-only; real M80.COM doesn't get this).
     * l80c picks these up opportunistically to enrich its own .SYM with
     * local symbols, which never appear in the .REL at all. */
    if (!cfg->use_emulated_m80 && !cmd_arg(cmd, cmd_size, "/C")) return 0;
    return 1;
}

static int add_default_include(struct Config *cfg)
{
    if (cfg->include_count >= MAX_ITEMS)
        return 1;
    if (file_exists("stdio.h")) {
        copy_text(cfg->includes[cfg->include_count++], MAX_PATH_LEN, ".");
    }
    return 1;
}

static int run_build(struct Config *cfg)
{
    char names[MAX_ITEMS][MAX_NAME_LEN];
    char uppers[MAX_ITEMS][MAX_NAME_LEN];
    char macs[MAX_ITEMS][MAX_PATH_LEN];
    char rels[MAX_ITEMS][MAX_PATH_LEN];
    char prns[MAX_ITEMS][MAX_PATH_LEN];
    char dbgs[MAX_ITEMS][MAX_PATH_LEN];
    char links[MAX_ITEMS][MAX_PATH_LEN];
    char rtl_prn[MAX_PATH_LEN];
    char rtl_link[MAX_PATH_LEN];
    char cmd[MAX_CMD_LEN];
    char tmp[MAX_PATH_LEN];
    char rtl_src[MAX_PATH_LEN];
    char rtl_min[MAX_PATH_LEN];
    char rtl_rel[MAX_PATH_LEN];
    char app_com[MAX_PATH_LEN];
    char app_dbg[MAX_PATH_LEN];
    char lower_com[MAX_PATH_LEN];
    char output_upper[MAX_NAME_LEN];
    char output_lower[MAX_NAME_LEN];
    char link_arg[MAX_CMD_LEN];
    int i;
    long long t_start, t0;
    long long ms_dcc = 0, ms_peep = 0, ms_asm = 0, ms_rtlstrip = 0, ms_link = 0;

    t_start = now_ms();

    if (cfg->input_count <= 0) {
        fprintf(stderr, "dcc-input is required; set it in dccmake.txt or pass dcc-input=main.c\n");
        fprintf(stderr, "run dccmake -h for the command-line and dccmake.txt formats\n");
        return 0;
    }
    if (!cfg->output_set) {
        base_no_ext(cfg->output, sizeof(cfg->output), cfg->inputs[0]);
        cfg->output_set = 1;
    }
    if (!validate_cpm_83_name("dcc-output", cfg->output, 0))
        return 0;
    if ((cfg->debug || cfg->debug_lines) && cfg->use_emulated_m80) {
        fprintf(stderr, "dcc debug metadata requires native m80c; disable dcc-use-emulated-m80\n");
        return 0;
    }
    for (i = 0; i < cfg->input_count; i++) {
        char input_label[64];
        char module_name[MAX_NAME_LEN];

        snprintf(input_label, sizeof(input_label), "dcc-input[%d]", i + 1);
        if (!validate_cpm_83_name(input_label, cfg->inputs[i], 1))
            return 0;
        if (i > 0) {
            base_no_ext(module_name, sizeof(module_name), cfg->inputs[i]);
            if (!validate_cpm_83_name("module output name", module_name, 0))
                return 0;
        }
    }
    if (!ensure_dir(cfg->build_dir)) {
        fprintf(stderr, "cannot create build directory: %s\n", cfg->build_dir);
        return 0;
    }
    if (!file_exists(cfg->runtime)) {
        fprintf(stderr, "runtime not found: %s\n", cfg->runtime);
        return 0;
    }
    for (i = 0; i < cfg->input_count; i++) {
        if (!file_exists(cfg->inputs[i])) {
            fprintf(stderr, "input not found: %s\n", cfg->inputs[i]);
            return 0;
        }
    }
    for (i = 0; i < cfg->include_count; i++) {
        if (!dir_exists(cfg->includes[i])) {
            fprintf(stderr, "include directory not found or not a directory: %s\n", cfg->includes[i]);
            return 0;
        }
    }

    if (cfg->debug && cfg->peep && cfg->peep_debug) {
        fprintf(stderr,
                "warning: dcc-peep-debug=true - dccpeep will run on this -g build.\n"
                "         Debug markers are retained/remapped, but -g still uses\n"
                "         conservative compiler codegen. Use dcc-debug=lines for\n"
                "         release-identical optimized code with line/function tables.\n");
    }

    add_default_include(cfg);
    maybe_copy_tool("m80.com", cfg->build_dir);
    maybe_copy_tool("l80.com", cfg->build_dir);

    upper_copy(output_upper, sizeof(output_upper), cfg->output);
    lower_copy(output_lower, sizeof(output_lower), cfg->output);
    for (i = 0; i < cfg->input_count; i++) {
        if (i == 0)
            copy_text(names[i], sizeof(names[i]), cfg->output);
        else
            base_no_ext(names[i], sizeof(names[i]), cfg->inputs[i]);
        upper_copy(uppers[i], sizeof(uppers[i]), names[i]);
        /* uppers[i] is char[MAX_NAME_LEN] (128); the explicit .127s
         * precision (MAX_NAME_LEN-1) lets gcc prove the result always fits
         * tmp's 512 bytes, since it otherwise can't see the bound through
         * the array index and assumes an arbitrary-length string
         * (-Wformat-truncation false positive). */
        snprintf(tmp, sizeof(tmp), "%.127s.MAC", uppers[i]);
        path_join(macs[i], sizeof(macs[i]), cfg->build_dir, tmp);
        snprintf(tmp, sizeof(tmp), "%.127s.REL", uppers[i]);
        path_join(rels[i], sizeof(rels[i]), cfg->build_dir, tmp);
        snprintf(tmp, sizeof(tmp), "%.127s.PRN", uppers[i]);
        path_join(prns[i], sizeof(prns[i]), cfg->build_dir, tmp);
        snprintf(tmp, sizeof(tmp), "%.127s.DBG", uppers[i]);
        path_join(dbgs[i], sizeof(dbgs[i]), cfg->build_dir, tmp);
        snprintf(tmp, sizeof(tmp), "%.127s.LNK", uppers[i]);
        path_join(links[i], sizeof(links[i]), cfg->build_dir, tmp);
        remove(macs[i]);
        remove(rels[i]);
        remove(prns[i]);
        remove(dbgs[i]);
        remove(links[i]);
    }
    path_join(rtl_src, sizeof(rtl_src), cfg->build_dir, "DCCRTL.MAC");
    path_join(rtl_min, sizeof(rtl_min), cfg->build_dir, "RTLMIN.MAC");
    path_join(rtl_rel, sizeof(rtl_rel), cfg->build_dir, "RTLMIN.REL");
    path_join(rtl_prn, sizeof(rtl_prn), cfg->build_dir, "RTLMIN.PRN");
    path_join(rtl_link, sizeof(rtl_link), cfg->build_dir, "RTLMIN.LNK");
    snprintf(tmp, sizeof(tmp), "%.127s.COM", output_upper);
    path_join(app_com, sizeof(app_com), cfg->build_dir, tmp);
    snprintf(tmp, sizeof(tmp), "%.127s.DBG", output_upper);
    path_join(app_dbg, sizeof(app_dbg), cfg->build_dir, tmp);
    snprintf(tmp, sizeof(tmp), "%.127s.com", output_lower);
    path_join(lower_com, sizeof(lower_com), cfg->build_dir, tmp);
    remove(rtl_src);
    remove(rtl_min);
    remove(rtl_rel);
    remove(rtl_prn);
    remove(rtl_link);
    remove(app_com);
    remove(lower_com);

    for (i = 0; i < cfg->input_count; i++) {
        if (!build_dcc_command(cfg, i, cfg->inputs[i], macs[i], cmd, sizeof(cmd)))
            return 0;
        t0 = now_ms();
        if (!run_cmd(cmd) || !file_exists(macs[i]))
            return 0;
        ms_dcc += now_ms() - t0;
        if (cfg->peep && (!cfg->debug || cfg->peep_debug)) {
            int tmp_n = snprintf(tmp, sizeof(tmp), "%s%c_PEEPOUT_%d.MAC", cfg->build_dir, PATH_SEP, i);
            if (tmp_n < 0 || (size_t)tmp_n >= sizeof(tmp)) {
                fprintf(stderr, "build path too long: %s\n", cfg->build_dir);
                return 0;
            }
            cmd_init(cmd, sizeof(cmd));
            if (!cmd_arg(cmd, sizeof(cmd), cfg->dccpeep)) return 0;
            if (cfg->dccpeep_undoc) {
                if (!cmd_arg(cmd, sizeof(cmd), "-fundocumented-z80")) return 0;
            }
            if (!cmd_arg(cmd, sizeof(cmd), macs[i])) return 0;
            if (!cmd_arg(cmd, sizeof(cmd), tmp)) return 0;
            t0 = now_ms();
            if (!run_cmd(cmd) || !file_exists(tmp))
                return 0;
            ms_peep += now_ms() - t0;
            remove(macs[i]);
            if (rename(tmp, macs[i]) != 0) {
                fprintf(stderr, "cannot replace %s with optimized output\n", macs[i]);
                return 0;
            }
        }
        if (!to_crlf(macs[i]))
            return 0;
    }

    for (i = 0; i < cfg->input_count; i++) {
        snprintf(tmp, sizeof(tmp), "=%.127s.MAC", uppers[i]);
        if (!build_m80_command(cfg, tmp, cmd, sizeof(cmd)))
            return 0;
        t0 = now_ms();
        if (!run_cmd_in_dir(cfg->build_dir, cmd) || !file_exists(rels[i])) {
            fprintf(stderr, "assembly failed: %s was not produced\n", rels[i]);
            return 0;
        }
        ms_asm += now_ms() - t0;
        if (!check_no_fatal_errors(prns[i]))
            return 0;
        if ((cfg->debug || cfg->debug_lines) &&
            (!file_exists(dbgs[i]) || !file_exists(links[i]))) {
            fprintf(stderr, "debug metadata assembly failed for %s\n", macs[i]);
            return 0;
        }
    }

    if (!copy_file(cfg->runtime, rtl_src) || !to_crlf(rtl_src))
        return 0;

    cmd_init(cmd, sizeof(cmd));
    if (!cmd_arg(cmd, sizeof(cmd), cfg->dccrtlstrip)) return 0;
    if (cfg->floatio) {
        if (!cmd_arg(cmd, sizeof(cmd), "-k")) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), "_pffio")) return 0;
    }
    if (cfg->flongio) {
        if (!cmd_arg(cmd, sizeof(cmd), "-k")) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), "_pflng")) return 0;
    }
    if (!cmd_arg(cmd, sizeof(cmd), "-r")) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), rtl_src)) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), "-o")) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), rtl_min)) return 0;
    for (i = 0; i < cfg->input_count; i++) {
        if (!cmd_arg(cmd, sizeof(cmd), macs[i])) return 0;
    }
    t0 = now_ms();
    if (!run_cmd(cmd) || !file_exists(rtl_min) || !to_crlf(rtl_min))
        return 0;
    ms_rtlstrip += now_ms() - t0;

    if (!build_m80_command(cfg, "=RTLMIN.MAC", cmd, sizeof(cmd)))
        return 0;
    t0 = now_ms();
    if (!run_cmd_in_dir(cfg->build_dir, cmd) || !file_exists(rtl_rel)) {
        fprintf(stderr, "runtime assembly failed: %s was not produced\n", rtl_rel);
        return 0;
    }
    ms_asm += now_ms() - t0;
    if (!check_no_fatal_errors(rtl_prn))
        return 0;
    if ((cfg->debug || cfg->debug_lines) && !file_exists(rtl_link)) {
        fprintf(stderr, "runtime link metadata was not produced: %s\n", rtl_link);
        return 0;
    }

    copy_text(link_arg, sizeof(link_arg), "/P:100,RTLMIN");
    for (i = 1; i < cfg->input_count; i++) {
        if (!cmd_append_raw(link_arg, sizeof(link_arg), ",")) return 0;
        if (!cmd_append_raw(link_arg, sizeof(link_arg), uppers[i])) return 0;
    }
    if (!cmd_append_raw(link_arg, sizeof(link_arg), ",")) return 0;
    if (!cmd_append_raw(link_arg, sizeof(link_arg), uppers[0])) return 0;
    if (!cmd_append_raw(link_arg, sizeof(link_arg), ",")) return 0;
    if (!cmd_append_raw(link_arg, sizeof(link_arg), output_upper)) return 0;
    if (!cmd_append_raw(link_arg, sizeof(link_arg), "/N/E/Y")) return 0;

    cmd_init(cmd, sizeof(cmd));
    if (cfg->use_emulated_l80) {
        if (!cmd_arg(cmd, sizeof(cmd), cfg->ntvcm)) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), cfg->l80)) return 0;
    } else {
        if (!cmd_arg(cmd, sizeof(cmd), cfg->l80c)) return 0;
    }
    if (!cmd_arg(cmd, sizeof(cmd), link_arg)) return 0;
    t0 = now_ms();
    if (!run_cmd_in_dir(cfg->build_dir, cmd) || !file_exists(app_com)) {
        fprintf(stderr, "link failed: %s was not produced\n", app_com);
        return 0;
    }
    ms_link += now_ms() - t0;

    if ((cfg->debug || cfg->debug_lines) &&
        !finalize_debug_file(app_dbg, rtl_link, dbgs, links, cfg->input_count))
        return 0;

    if (strcmp(output_lower, output_upper) != 0 && !same_file(app_com, lower_com))
        copy_file(app_com, lower_com);

    printf("dccmake: built %s\n", app_com);
    {
        long long total = now_ms() - t_start;
        long long other = total - (ms_dcc + ms_peep + ms_asm + ms_rtlstrip + ms_link);
        if (other < 0) other = 0;
        printf("dccmake: timing dcc=%lldms peep=%lldms asm=%lldms(%s) rtlstrip=%lldms link=%lldms(%s) other=%lldms total=%lldms\n",
               ms_dcc, ms_peep, ms_asm, cfg->use_emulated_m80 ? "emulated" : "native",
               ms_rtlstrip, ms_link, cfg->use_emulated_l80 ? "emulated" : "native", other, total);
    }
    return 1;
}

int main(int argc, char **argv)
{
    struct Config cfg;

    init_config(&cfg);
    if (!read_config_file(&cfg, "dccmake.txt"))
        return 1;
    if (!parse_args(&cfg, argc, argv))
        return 1;
    promote_debug_compiler_arg(&cfg);
    return run_build(&cfg) ? 0 : 1;
}
