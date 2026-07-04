/*
 * dccmake - small build driver for the dcc CP/M pipeline.
 *
 * Pipeline:
 *   dcc -> optional dccpeep -> ntvcm M80 -> dccrtlstrip -> ntvcm M80 -> ntvcm L80
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(path) _mkdir(path)
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define MKDIR(path) mkdir(path, 0777)
#define PATH_SEP '/'
#endif

#define MAX_ITEMS 128
#define MAX_PATH_LEN 512
#define MAX_NAME_LEN 128
#define MAX_CMD_LEN 32768
#define MAX_LINE_LEN 2048

struct Config {
    char inputs[MAX_ITEMS][MAX_PATH_LEN];
    int input_count;
    char output[MAX_NAME_LEN];
    int output_set;
    int floatio;
    int flongio;
    int stack_check;
    int stack_bytes;
    char includes[MAX_ITEMS][MAX_PATH_LEN];
    int include_count;
    char dcc_args[MAX_ITEMS][MAX_PATH_LEN];
    int dcc_arg_count;
    int peep;
    char build_dir[MAX_PATH_LEN];
    char dcc[MAX_PATH_LEN];
    char dccpeep[MAX_PATH_LEN];
    char dccrtlstrip[MAX_PATH_LEN];
    char ntvcm[MAX_PATH_LEN];
    char m80[MAX_PATH_LEN];
    char l80[MAX_PATH_LEN];
    char runtime[MAX_PATH_LEN];
};

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
        return;
    strncpy(dst, src ? src : "", dst_size - 1);
    dst[dst_size - 1] = 0;
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
        return (st.st_mode & S_IFDIR) != 0;
    if (MKDIR(path) == 0)
        return 1;
    return errno == EEXIST;
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
    cfg->flongio = getenv("DCC_LONGIO") && !strcmp(getenv("DCC_LONGIO"), "1");
    cfg->stack_check = getenv("DCC_FORCE_STACK_CHECK") && !strcmp(getenv("DCC_FORCE_STACK_CHECK"), "1");
    cfg->stack_bytes = 512;
    cfg->peep = 1;
    copy_text(cfg->build_dir, sizeof(cfg->build_dir), "build");
    copy_text(cfg->dcc, sizeof(cfg->dcc), env_or_default("DCC", "./dcc", "dcc"));
    copy_text(cfg->dccpeep, sizeof(cfg->dccpeep), env_or_default("DCCPEEP", "./dccpeep", "dccpeep"));
    copy_text(cfg->dccrtlstrip, sizeof(cfg->dccrtlstrip), env_or_default("DCCRTLSTRIP", "./dccrtlstrip", "dccrtlstrip"));
    copy_text(cfg->ntvcm, sizeof(cfg->ntvcm), env_or_default("NTVCM", NULL, "ntvcm"));
    copy_text(cfg->m80, sizeof(cfg->m80), env_or_default("M80", NULL, "m80"));
    copy_text(cfg->l80, sizeof(cfg->l80), env_or_default("L80", NULL, "l80"));
    copy_text(cfg->runtime, sizeof(cfg->runtime), env_or_default("DCC_RUNTIME", "DCCRTL.MAC", "DCCRTL.MAC"));
    add_whitespace_args(cfg->dcc_args, &cfg->dcc_arg_count, getenv("DCC_ARGS"));
}

static int apply_setting(struct Config *cfg, const char *raw_key, const char *value)
{
    char key[MAX_NAME_LEN];
    int b;
    int n;

    normalize_key(key, sizeof(key), raw_key);
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
    if (!strcmp(key, "dcc-flongio")) {
        if (!parse_bool(value, &b)) {
            fprintf(stderr, "invalid boolean for %s: %s\n", raw_key, value);
            return 0;
        }
        cfg->flongio = b;
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
    if (!strcmp(key, "l80-command")) {
        copy_text(cfg->l80, sizeof(cfg->l80), value);
        trim(cfg->l80);
        return cfg->l80[0] != 0;
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
    printf("  assemble all .MAC files and RTLMIN.MAC with ntvcm M80\n");
    printf("  link RTLMIN plus all app modules with ntvcm L80\n");
    printf("\n");
    printf("dccmake.txt format:\n");
    printf("  One key=value setting per line. Blank lines are ignored. Text after # is a\n");
    printf("  comment. Comma-separated values may contain spaces around commas.\n");
    printf("  dcc-input basenames and dcc-output must be valid CP/M 8.3 names.\n");
    printf("\n");
    printf("  example:\n");
    printf("    dcc-input=main.c, module1.c, module2.c\n");
    printf("    dcc-output=main\n");
    printf("    dcc-floatio=false\n");
    printf("    dcc-flongio=false\n");
    printf("    dcc-stack-bytes=512\n");
    printf("    dcc-stack-check=false\n");
    printf("    dcc-include-directory=include, ../shared/include\n");
    printf("    dcc-define=DEBUG=1, TRACE\n");
    printf("    dcc-undefine=OLD\n");
    printf("    dcc-peep=true\n");
    printf("\n");
    printf("settings:\n");
    printf("  dcc-input=main.c,module1.c     comma-separated C sources; or pass .c files positionally\n");
    printf("  dcc-output=main                output base name; default first input base\n");
    printf("  dcc-floatio=false|true|1|0     pass -ffloatio to dcc and keep _pffio\n");
    printf("  dcc-flongio=false|true|1|0     pass -flongio to dcc and keep _pflng\n");
    printf("  dcc-stack-bytes=512            pass -stack bytes to dcc; default 512\n");
    printf("  dcc-stack-check=false|true|1|0 pass -fstack-check to dcc\n");
    printf("  dcc-include-directory=dir,...  include dirs; dcc-include is an alias\n");
    printf("  dcc-define=NAME[=value],...    pass -D values to dcc\n");
    printf("  dcc-undefine=NAME,...          pass -U values to dcc\n");
    printf("  dcc-peep=true|false|1|0        run dccpeep; default true\n");
    printf("  dcc-build-dir=build            artifact directory; default build\n");
    printf("\n");
    printf("dcc-style command options:\n");
    printf("  -f, -ffloatio                  same as dcc-floatio=true\n");
    printf("  -fl, -flongio                  same as dcc-flongio=true\n");
    printf("  -s <bytes>, -stack <bytes>     same as dcc-stack-bytes=<bytes>\n");
    printf("  -fstack-check                  same as dcc-stack-check=true\n");
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
    printf("  ntvcm-tool=ntvcm              emulator command for M80/L80\n");
    printf("  m80-command=m80               CP/M assembler command passed to ntvcm\n");
    printf("  l80-command=l80               CP/M linker command passed to ntvcm\n");
    printf("  dcc-runtime=DCCRTL.MAC        runtime source used by dccrtlstrip\n");
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
        if (!strcmp(arg, "-fl") || !strcmp(arg, "-flongio")) {
            cfg->flongio = 1;
            continue;
        }
        if (!strcmp(arg, "-fstack-check")) {
            cfg->stack_check = 1;
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

static int build_cd_command(char *dst, size_t dst_size, const char *dir, const char *inner)
{
    cmd_init(dst, dst_size);
#ifdef _WIN32
    if (!cmd_append_raw(dst, dst_size, "cd /d")) return 0;
#else
    if (!cmd_append_raw(dst, dst_size, "cd")) return 0;
#endif
    if (!cmd_arg(dst, dst_size, dir)) return 0;
    if (!cmd_append_raw(dst, dst_size, " && ")) return 0;
    return cmd_append_raw(dst, dst_size, inner);
}

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

static int copy_file(const char *src, const char *dst)
{
    FILE *in;
    FILE *out;
    char buf[8192];
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
    int c;
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
    while ((c = fgetc(in)) != EOF) {
        if (c == '\n' && prev != '\r')
            fputc('\r', out);
        fputc(c, out);
        prev = c;
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
    if (!cmd_arg(cmd, cmd_size, "-stack")) return 0;
    snprintf(stack_buf, sizeof(stack_buf), "%d", cfg->stack_bytes);
    if (!cmd_arg(cmd, cmd_size, stack_buf)) return 0;
    if (cfg->floatio && !cmd_arg(cmd, cmd_size, "-ffloatio")) return 0;
    if (cfg->flongio && !cmd_arg(cmd, cmd_size, "-flongio")) return 0;
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
    char prn[MAX_PATH_LEN];
    char cmd[MAX_CMD_LEN];
    char tmp[MAX_PATH_LEN];
    char rtl_src[MAX_PATH_LEN];
    char rtl_min[MAX_PATH_LEN];
    char rtl_rel[MAX_PATH_LEN];
    char app_com[MAX_PATH_LEN];
    char lower_com[MAX_PATH_LEN];
    char output_upper[MAX_NAME_LEN];
    char output_lower[MAX_NAME_LEN];
    char link_arg[MAX_CMD_LEN];
    char shell_cmd[MAX_CMD_LEN];
    int i;

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
        snprintf(tmp, sizeof(tmp), "%s.MAC", uppers[i]);
        path_join(macs[i], sizeof(macs[i]), cfg->build_dir, tmp);
        snprintf(tmp, sizeof(tmp), "%s.REL", uppers[i]);
        path_join(rels[i], sizeof(rels[i]), cfg->build_dir, tmp);
        snprintf(tmp, sizeof(tmp), "%s.PRN", uppers[i]);
        path_join(prn, sizeof(prn), cfg->build_dir, tmp);
        remove(macs[i]);
        remove(rels[i]);
        remove(prn);
    }
    path_join(rtl_src, sizeof(rtl_src), cfg->build_dir, "DCCRTL.MAC");
    path_join(rtl_min, sizeof(rtl_min), cfg->build_dir, "RTLMIN.MAC");
    path_join(rtl_rel, sizeof(rtl_rel), cfg->build_dir, "RTLMIN.REL");
    snprintf(tmp, sizeof(tmp), "%s.COM", output_upper);
    path_join(app_com, sizeof(app_com), cfg->build_dir, tmp);
    snprintf(tmp, sizeof(tmp), "%s.com", output_lower);
    path_join(lower_com, sizeof(lower_com), cfg->build_dir, tmp);
    remove(rtl_src);
    remove(rtl_min);
    remove(rtl_rel);
    remove(app_com);
    remove(lower_com);

    for (i = 0; i < cfg->input_count; i++) {
        if (!build_dcc_command(cfg, i, cfg->inputs[i], macs[i], cmd, sizeof(cmd)))
            return 0;
        if (!run_cmd(cmd) || !file_exists(macs[i]))
            return 0;
        if (cfg->peep) {
            snprintf(tmp, sizeof(tmp), "%s%c_PEEPOUT_%d.MAC", cfg->build_dir, PATH_SEP, i);
            cmd_init(cmd, sizeof(cmd));
            if (!cmd_arg(cmd, sizeof(cmd), cfg->dccpeep)) return 0;
            if (!cmd_arg(cmd, sizeof(cmd), macs[i])) return 0;
            if (!cmd_arg(cmd, sizeof(cmd), tmp)) return 0;
            if (!run_cmd(cmd) || !file_exists(tmp))
                return 0;
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
        cmd_init(cmd, sizeof(cmd));
        if (!cmd_arg(cmd, sizeof(cmd), cfg->ntvcm)) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), cfg->m80)) return 0;
        snprintf(tmp, sizeof(tmp), "=%s.MAC", uppers[i]);
        if (!cmd_arg(cmd, sizeof(cmd), tmp)) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), "/X")) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), "/O")) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), "/Z")) return 0;
        if (!cmd_arg(cmd, sizeof(cmd), "/L")) return 0;
        if (!build_cd_command(shell_cmd, sizeof(shell_cmd), cfg->build_dir, cmd))
            return 0;
        if (!run_cmd(shell_cmd) || !file_exists(rels[i])) {
            fprintf(stderr, "assembly failed: %s was not produced\n", rels[i]);
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
    if (!cmd_arg(cmd, sizeof(cmd), macs[0])) return 0;
    if (!run_cmd(cmd) || !file_exists(rtl_min) || !to_crlf(rtl_min))
        return 0;

    cmd_init(cmd, sizeof(cmd));
    if (!cmd_arg(cmd, sizeof(cmd), cfg->ntvcm)) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), cfg->m80)) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), "=RTLMIN.MAC")) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), "/X")) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), "/O")) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), "/Z")) return 0;
    if (!build_cd_command(shell_cmd, sizeof(shell_cmd), cfg->build_dir, cmd))
        return 0;
    if (!run_cmd(shell_cmd) || !file_exists(rtl_rel)) {
        fprintf(stderr, "runtime assembly failed: %s was not produced\n", rtl_rel);
        return 0;
    }

    copy_text(link_arg, sizeof(link_arg), "/P:100,RTLMIN");
    for (i = 0; i < cfg->input_count; i++) {
        if (!cmd_append_raw(link_arg, sizeof(link_arg), ",")) return 0;
        if (!cmd_append_raw(link_arg, sizeof(link_arg), uppers[i])) return 0;
    }
    if (!cmd_append_raw(link_arg, sizeof(link_arg), ",")) return 0;
    if (!cmd_append_raw(link_arg, sizeof(link_arg), output_upper)) return 0;
    if (!cmd_append_raw(link_arg, sizeof(link_arg), "/N/E")) return 0;

    cmd_init(cmd, sizeof(cmd));
    if (!cmd_arg(cmd, sizeof(cmd), cfg->ntvcm)) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), cfg->l80)) return 0;
    if (!cmd_arg(cmd, sizeof(cmd), link_arg)) return 0;
    if (!build_cd_command(shell_cmd, sizeof(shell_cmd), cfg->build_dir, cmd))
        return 0;
    if (!run_cmd(shell_cmd) || !file_exists(app_com)) {
        fprintf(stderr, "link failed: %s was not produced\n", app_com);
        return 0;
    }

    if (strcmp(output_lower, output_upper) != 0 && !same_file(app_com, lower_com))
        copy_file(app_com, lower_com);

    printf("dccmake: built %s\n", app_com);
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
    return run_build(&cfg) ? 0 : 1;
}