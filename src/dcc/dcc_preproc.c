/*
 * dcc_preproc.c - preprocessor, macro engine, and lexer.
 *
 * Handles #define/#undef/#if/#ifdef directives, object- and function-like
 * macro expansion (including # stringize and ## paste), the recursive-descent
 * #if constant-expression evaluator, and the main tokenizer next_token() that
 * feeds the parser. The #if-expression cursor globals (pp_expr_p/pp_expr_depth)
 * are kept module-local here rather than in dcc_state.c.
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 692-2871.
 */

#include "dcc.h"

#define MACRO_PLACEMARKER '\002'

#define MAX_MACRO_PUSH 64

struct MacroPushEntry {
    char name[64];
    int was_defined;
    struct Def def;
};

static struct MacroPushEntry macro_push_stack[MAX_MACRO_PUSH];
static int nmacro_push_stack;

int find_define(const char *name)
{
    int i;
    for (i = ndefs - 1; i >= 0; --i)
        if (!strcmp(defs[i].name, name)) return i;
    return -1;
}

void add_define_ex(const char *name, const char *value, int is_func, int nargs, char params[8][32])
{
    int i;
    int j;

    i = find_define(name);
    if (i < 0) {
        if (ndefs >= MAX_DEFINES) fatal("too many defines");
        i = ndefs++;
        memset(&defs[i], 0, sizeof(defs[i]));
        strncpy(defs[i].name, name, sizeof(defs[i].name) - 1);
    }

    defs[i].is_func = is_func;
    defs[i].nargs = nargs;
    for (j = 0; j < 8; ++j)
        defs[i].params[j][0] = 0;
    for (j = 0; j < nargs && j < 8; ++j) {
        strncpy(defs[i].params[j], params[j], sizeof(defs[i].params[j]) - 1);
        defs[i].params[j][sizeof(defs[i].params[j]) - 1] = 0;
    }

    strncpy(defs[i].value, value, sizeof(defs[i].value) - 1);
    defs[i].value[sizeof(defs[i].value) - 1] = 0;
}

void add_define(const char *name, const char *value)
{
    char dummy[8][32];
    memset(dummy, 0, sizeof(dummy));
    add_define_ex(name, value, 0, 0, dummy);
}

static const char *pp_expr_p;
static int pp_expr_depth;

void pp_expr_skip_ws(void)
{
    while (*pp_expr_p && isspace((unsigned char)*pp_expr_p))
        pp_expr_p++;
}

int pp_expr_match_word(const char *w)
{
    int n;
    n = (int)strlen(w);
    pp_expr_skip_ws();
    if (strncmp(pp_expr_p, w, n) != 0)
        return 0;
    if (is_ident_char((unsigned char)pp_expr_p[n]))
        return 0;
    pp_expr_p += n;
    return 1;
}

long pp_expr_number(void)
{
    unsigned long v;
    int base;

    pp_expr_skip_ws();

    v = 0;
    base = 10;
    if (pp_expr_p[0] == '0') {
        if (pp_expr_p[1] == 'x' || pp_expr_p[1] == 'X') {
            base = 16;
            pp_expr_p += 2;
        } else {
            base = 8;
            pp_expr_p++;
        }
    }

    if (base == 16) {
        while (isxdigit((unsigned char)*pp_expr_p)) {
            v *= 16;
            if (*pp_expr_p >= '0' && *pp_expr_p <= '9') v += *pp_expr_p - '0';
            else if (*pp_expr_p >= 'a' && *pp_expr_p <= 'f') v += *pp_expr_p - 'a' + 10;
            else v += *pp_expr_p - 'A' + 10;
            pp_expr_p++;
        }
    } else {
        while (*pp_expr_p >= '0' && *pp_expr_p <= (base == 8 ? '7' : '9')) {
            v = v * (unsigned long)base + (unsigned long)(*pp_expr_p - '0');
            pp_expr_p++;
        }
    }

    while (*pp_expr_p == 'u' || *pp_expr_p == 'U' ||
           *pp_expr_p == 'l' || *pp_expr_p == 'L')
        pp_expr_p++;

    return (long)v;
}

long pp_expr_charlit(void)
{
    int c;
    long v;

    pp_expr_skip_ws();
    if (*pp_expr_p != '\'')
        return 0;
    pp_expr_p++;

    if (*pp_expr_p == '\\') {
        pp_expr_p++;
        c = (unsigned char)*pp_expr_p;
        if (c == 'n') v = '\n';
        else if (c == 'r') v = '\r';
        else if (c == 't') v = '\t';
        else if (c == '0') v = 0;
        else v = c;
        if (*pp_expr_p)
            pp_expr_p++;
    } else {
        v = (unsigned char)*pp_expr_p;
        if (*pp_expr_p)
            pp_expr_p++;
    }

    while (*pp_expr_p && *pp_expr_p != '\'')
        pp_expr_p++;
    if (*pp_expr_p == '\'')
        pp_expr_p++;

    return v;
}

long pp_expr_primary(void);
long pp_expr_unary(void);
long pp_expr_mul(void);
long pp_expr_add(void);
long pp_expr_shift(void);
long pp_expr_rel(void);
long pp_expr_eq(void);
long pp_expr_bitand(void);
long pp_expr_bitxor(void);
long pp_expr_bitor(void);
long pp_expr_andand(void);
long pp_expr_oror(void);
long pp_expr_cond(void);

long pp_expr_defined(void)
{
    char name[64];
    int i;

    if (!pp_expr_match_word("defined"))
        return 0;

    pp_expr_skip_ws();
    if (*pp_expr_p == '(') {
        pp_expr_p++;
        pp_expr_skip_ws();
        i = 0;
        while (is_ident_char((unsigned char)*pp_expr_p) && i < 63)
            name[i++] = *pp_expr_p++;
        name[i] = 0;
        pp_expr_skip_ws();
        if (*pp_expr_p == ')')
            pp_expr_p++;
    } else {
        i = 0;
        while (is_ident_char((unsigned char)*pp_expr_p) && i < 63)
            name[i++] = *pp_expr_p++;
        name[i] = 0;
    }

    return name[0] && find_define(name) >= 0;
}

long pp_expr_primary(void)
{
    char name[64];
    int i;
    long v;

    pp_expr_skip_ws();

    if (!strncmp(pp_expr_p, "defined", 7) &&
        !is_ident_char((unsigned char)pp_expr_p[7]))
        return pp_expr_defined();

    if (*pp_expr_p == '(') {
        pp_expr_p++;
        v = pp_expr_cond();
        pp_expr_skip_ws();
        if (*pp_expr_p == ')')
            pp_expr_p++;
        return v;
    }

    if (*pp_expr_p == '\'')
        return pp_expr_charlit();

    if (isdigit((unsigned char)*pp_expr_p))
        return pp_expr_number();

    if (is_ident_start((unsigned char)*pp_expr_p)) {
        int di;
        const char *savep;

        i = 0;
        while (is_ident_char((unsigned char)*pp_expr_p) && i < 63)
            name[i++] = *pp_expr_p++;
        name[i] = 0;

        if (!strcmp(name, "__LINE__"))
            return line_no;

        di = find_define(name);
        if (di >= 0 && !defs[di].is_func && pp_expr_depth < 16) {
            savep = pp_expr_p;
            pp_expr_p = defs[di].value;
            pp_expr_depth++;
            v = pp_expr_cond();
            pp_expr_depth--;
            pp_expr_p = savep;
            return v;
        }

        return 0;
    }

    return 0;
}

long pp_expr_unary(void)
{
    pp_expr_skip_ws();
    if (*pp_expr_p == '!') {
        pp_expr_p++;
        return !pp_expr_unary();
    }
    if (*pp_expr_p == '~') {
        pp_expr_p++;
        return ~pp_expr_unary();
    }
    if (*pp_expr_p == '+') {
        pp_expr_p++;
        return pp_expr_unary();
    }
    if (*pp_expr_p == '-') {
        pp_expr_p++;
        return -pp_expr_unary();
    }
    return pp_expr_primary();
}

long pp_expr_mul(void)
{
    long v;
    long r;

    v = pp_expr_unary();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '*') {
            pp_expr_p++;
            v = v * pp_expr_unary();
        } else if (*pp_expr_p == '/') {
            pp_expr_p++;
            r = pp_expr_unary();
            v = r ? (v / r) : 0;
        } else if (*pp_expr_p == '%') {
            pp_expr_p++;
            r = pp_expr_unary();
            v = r ? (v % r) : 0;
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_add(void)
{
    long v;

    v = pp_expr_mul();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '+') {
            pp_expr_p++;
            v = v + pp_expr_mul();
        } else if (*pp_expr_p == '-') {
            pp_expr_p++;
            v = v - pp_expr_mul();
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_shift(void)
{
    long v;
    long r;

    v = pp_expr_add();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '<' && pp_expr_p[1] == '<') {
            pp_expr_p += 2;
            r = pp_expr_add();
            if (r < 0 || r >= 32)
                v = 0;
            else
                v = v << (int)r;
        } else if (pp_expr_p[0] == '>' && pp_expr_p[1] == '>') {
            pp_expr_p += 2;
            r = pp_expr_add();
            if (r < 0 || r >= 32)
                v = 0;
            else
                v = v >> (int)r;
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_rel(void)
{
    long v;
    long r;

    v = pp_expr_shift();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '<' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_shift();
            v = (v <= r);
        } else if (pp_expr_p[0] == '>' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_shift();
            v = (v >= r);
        } else if (*pp_expr_p == '<') {
            pp_expr_p++;
            r = pp_expr_shift();
            v = (v < r);
        } else if (*pp_expr_p == '>') {
            pp_expr_p++;
            r = pp_expr_shift();
            v = (v > r);
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_eq(void)
{
    long v;
    long r;

    v = pp_expr_rel();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '=' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_rel();
            v = (v == r);
        } else if (pp_expr_p[0] == '!' && pp_expr_p[1] == '=') {
            pp_expr_p += 2;
            r = pp_expr_rel();
            v = (v != r);
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_bitand(void)
{
    long v;

    v = pp_expr_eq();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '&' && pp_expr_p[1] != '&') {
            pp_expr_p++;
            v = v & pp_expr_eq();
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_bitxor(void)
{
    long v;

    v = pp_expr_bitand();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '^') {
            pp_expr_p++;
            v = v ^ pp_expr_bitand();
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_bitor(void)
{
    long v;

    v = pp_expr_bitxor();
    for (;;) {
        pp_expr_skip_ws();
        if (*pp_expr_p == '|' && pp_expr_p[1] != '|') {
            pp_expr_p++;
            v = v | pp_expr_bitxor();
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_andand(void)
{
    long v;
    v = pp_expr_bitor();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '&' && pp_expr_p[1] == '&') {
            pp_expr_p += 2;
            v = (pp_expr_bitor() && v);
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_oror(void)
{
    long v;
    v = pp_expr_andand();
    for (;;) {
        pp_expr_skip_ws();
        if (pp_expr_p[0] == '|' && pp_expr_p[1] == '|') {
            pp_expr_p += 2;
            v = (pp_expr_andand() || v);
        } else {
            break;
        }
    }
    return v;
}

long pp_expr_cond(void)
{
    long v;
    long t;
    long f;

    v = pp_expr_oror();
    pp_expr_skip_ws();
    if (*pp_expr_p == '?') {
        pp_expr_p++;
        t = pp_expr_cond();
        pp_expr_skip_ws();
        if (*pp_expr_p == ':')
            pp_expr_p++;
        f = pp_expr_cond();
        v = v ? t : f;
    }
    return v;
}

int pp_eval_simple_expr(const char *s)
{
    pp_expr_p = s;
    pp_expr_depth = 0;
    return pp_expr_cond() != 0;
}

void remove_define(const char *name)
{
    int i;
    int j;

    i = find_define(name);
    if (i < 0)
        return;

    for (j = i; j + 1 < ndefs; ++j)
        defs[j] = defs[j + 1];
    ndefs--;
}

static int parse_pragma_macro_name(const char *line, const char *op, char *name, int namesz)
{
    int oi;

    while (*line && isspace((unsigned char)*line))
        line++;
    while (*op) {
        if (*line++ != *op++)
            return 0;
    }
    while (*line && isspace((unsigned char)*line))
        line++;
    if (*line++ != '(')
        return 0;
    while (*line && isspace((unsigned char)*line))
        line++;
    if (*line++ != '"')
        return 0;

    oi = 0;
    while (*line && *line != '"' && oi < namesz - 1)
        name[oi++] = *line++;
    name[oi] = 0;
    if (*line != '"')
        return 0;
    line++;
    while (*line && isspace((unsigned char)*line))
        line++;
    return name[0] && *line == ')';
}

static void pp_push_macro(const char *name)
{
    int di;
    size_t namelen;
    struct MacroPushEntry *e;

    if (nmacro_push_stack >= MAX_MACRO_PUSH)
        fatal("too many pushed macros");

    e = &macro_push_stack[nmacro_push_stack++];
    memset(e, 0, sizeof(*e));
    namelen = strlen(name);
    if (namelen > sizeof(e->name) - 1)
        namelen = sizeof(e->name) - 1;
    memcpy(e->name, name, namelen);

    di = find_define(name);
    if (di >= 0) {
        e->was_defined = 1;
        e->def = defs[di];
    }
}

static void pp_pop_macro(const char *name)
{
    int si;
    int di;
    int j;
    struct MacroPushEntry saved;

    for (si = nmacro_push_stack - 1; si >= 0; --si) {
        if (!strcmp(macro_push_stack[si].name, name))
            break;
    }
    if (si < 0)
        return;

    saved = macro_push_stack[si];
    for (j = si; j + 1 < nmacro_push_stack; ++j)
        macro_push_stack[j] = macro_push_stack[j + 1];
    nmacro_push_stack--;

    if (!saved.was_defined) {
        remove_define(name);
        return;
    }

    di = find_define(name);
    if (di >= 0) {
        defs[di] = saved.def;
    } else {
        if (ndefs >= MAX_DEFINES)
            fatal("too many defines");
        defs[ndefs++] = saved.def;
    }
}

static void handle_pragma_line(const char *line)
{
    char name[64];

    if (parse_pragma_macro_name(line, "push_macro", name, sizeof(name))) {
        pp_push_macro(name);
        return;
    }
    if (parse_pragma_macro_name(line, "pop_macro", name, sizeof(name))) {
        pp_pop_macro(name);
        return;
    }
}

void pp_recompute_active(void)
{
    if (if_sp <= 0) {
        pp_active = 1;
    } else {
        pp_active = if_parent_active[if_sp - 1] && if_this_active[if_sp - 1];
    }
}

void macro_expand_argument_text(const char *in, char *out, int outsz, int depth);

void parse_preprocessor_line(void)
{
    char word[32];
    char name[64];
    char val[MAX_MACRO_TEXT];
    int i, c;
    int parent;
    int cond;

    while (isspace((unsigned char)peekc()) && peekc() != '\n') getc_src();

    i = 0;
    while (is_ident_char(peekc()) && i < (int)sizeof(word) - 1)
        word[i++] = (char)getc_src();
    word[i] = 0;

    /* A '#' where a directive keyword is expected means '##' was written at
     * the start of a line.  '##' is the token-paste operator and is only
     * valid inside a macro replacement list; at directive position it is
     * always a hard error, even inside an inactive #if block. */
    if (word[0] == 0 && peekc() == '#') {
        fprintf(stderr, "%s:%d: error: '##' is not a valid preprocessor directive\n",
                current_file_name[0] ? current_file_name : (input_name ? input_name : "<input>"),
                line_no);
        errors++;
        if (errors > 40) fatal("too many errors");
        while (peekc() && peekc() != '\n') getc_src();
        return;
    }

    if (!strcmp(word, "if")) {
        i = 0;
        while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
            val[i++] = (char)getc_src();
        val[i] = 0;
        strip_macro_replacement_comments(val);

        if (if_sp >= MAX_IFSTACK) {
            fprintf(stderr, "%s:%d: error: too many nested #if\n",
                current_file_name[0] ? current_file_name : (input_name ? input_name : "<input>"),
                line_no);
            errors++;
            if (errors > 40) fatal("too many errors");
            while (peekc() && peekc() != '\n') getc_src();
            return;
        }

        parent = pp_active;
        cond = pp_eval_simple_expr(val);

        if_parent_active[if_sp] = parent;
        if_this_active[if_sp] = cond ? 1 : 0;
        if_seen_else[if_sp] = 0;
        if_branch_taken[if_sp] = cond ? 1 : 0;
        if_sp++;
        pp_recompute_active();
    } else if (!strcmp(word, "ifdef") || !strcmp(word, "ifndef")) {
        while (isspace((unsigned char)peekc()) && peekc() != '\n') getc_src();

        i = 0;
        while (is_ident_char(peekc()) && i < (int)sizeof(name) - 1)
            name[i++] = (char)getc_src();
        name[i] = 0;

        if (if_sp >= MAX_IFSTACK) {
            fprintf(stderr, "%s:%d: error: too many nested #if\n",
                current_file_name[0] ? current_file_name : (input_name ? input_name : "<input>"),
                line_no);
            errors++;
            if (errors > 40) fatal("too many errors");
            while (peekc() && peekc() != '\n') getc_src();
            return;
        }

        parent = pp_active;
        cond = find_define(name) >= 0;
        if (!strcmp(word, "ifndef")) cond = !cond;

        if_parent_active[if_sp] = parent;
        if_this_active[if_sp] = cond ? 1 : 0;
        if_seen_else[if_sp] = 0;
        if_branch_taken[if_sp] = cond ? 1 : 0;
        if_sp++;
        pp_recompute_active();
    } else if (!strcmp(word, "elif")) {
        if (if_sp <= 0) {
            /* ignore unmatched #elif for now */
        } else {
            i = if_sp - 1;
            if (if_seen_else[i]) {
                if_this_active[i] = 0;
            } else {
                i = 0;
                while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
                    val[i++] = (char)getc_src();
                val[i] = 0;
                strip_macro_replacement_comments(val);
                cond = pp_eval_simple_expr(val);
                i = if_sp - 1;
                if (!if_branch_taken[i] && cond) {
                    if_this_active[i] = 1;
                    if_branch_taken[i] = 1;
                } else {
                    if_this_active[i] = 0;
                }
            }
            pp_recompute_active();
        }
    } else if (!strcmp(word, "else")) {
        if (if_sp <= 0) {
            /* ignore unmatched #else for now */
        } else {
            i = if_sp - 1;
            if (!if_seen_else[i]) {
                if_this_active[i] = if_branch_taken[i] ? 0 : 1;
                if_branch_taken[i] = 1;
                if_seen_else[i] = 1;
            }
            pp_recompute_active();
        }
    } else if (!strcmp(word, "endif")) {
        if (if_sp > 0)
            if_sp--;
        pp_recompute_active();
    } else if (!strcmp(word, "line")) {
        int lno;
        int qi;
        char expanded[MAX_MACRO_TEXT];
        const char *lp;

        while (isspace((unsigned char)peekc()) && peekc() != '\n')
            getc_src();

        i = 0;
        while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
            val[i++] = (char)getc_src();
        val[i] = 0;
        macro_expand_argument_text(val, expanded, sizeof(expanded), 0);

        lp = expanded;
        while (*lp && isspace((unsigned char)*lp))
            lp++;

        lno = 0;
        while (isdigit((unsigned char)*lp))
            lno = lno * 10 + *lp++ - '0';

        if (lno > 0)
            line_no = lno - 1;

        while (*lp && isspace((unsigned char)*lp))
            lp++;

        if (*lp == '"') {
            lp++;
            qi = 0;
            while (*lp && *lp != '"' && *lp != '\n' &&
                   qi < (int)sizeof(current_file_name) - 1)
                current_file_name[qi++] = *lp++;
            current_file_name[qi] = 0;
        }
    } else if (!strcmp(word, "include")) {
        /* #include directives are expanded before tokenisation by
         * preprocess_includes_file(); any that survive to here (e.g. inside
         * an inactive #ifdef block) are silently skipped. */
    } else if (!strcmp(word, "undef")) {
        if (pp_active) {
            while (isspace((unsigned char)peekc()) && peekc() != '\n') getc_src();
            i = 0;
            while (is_ident_char(peekc()) && i < (int)sizeof(name) - 1)
                name[i++] = (char)getc_src();
            name[i] = 0;
            if (name[0]) remove_define(name);
        }
    } else if (!strcmp(word, "error")) {
        if (pp_active) {
            i = 0;
            while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
                val[i++] = (char)getc_src();
            val[i] = 0;
            fprintf(stderr, "%s:%d: error: #error %s\n",
                    current_file_name[0] ? current_file_name : (input_name ? input_name : "<input>"),
                    line_no, val);
            errors++;
            if (errors > 40) fatal("too many errors");
        }
    } else if (!strcmp(word, "define")) {
        if (pp_active) {
            while (isspace((unsigned char)peekc()) && peekc() != '\n') getc_src();

            i = 0;
            while (is_ident_char(peekc()) && i < (int)sizeof(name) - 1)
                name[i++] = (char)getc_src();
            name[i] = 0;

            /* Function-like macro: the '(' must immediately follow the name.
             * This is enough for forms such as:
             *     #define _countof( X ) ( sizeof( X ) / sizeof( X[0] ) )
             */
            if (peekc() == '(') {
                char params[8][32];
                int nargs;
                int pi;
                int pp;

                memset(params, 0, sizeof(params));
                nargs = 0;
                getc_src();

                while (peekc() && peekc() != ')' && peekc() != '\n') {
                    while (isspace((unsigned char)peekc()) && peekc() != '\n')
                        getc_src();
                    if (peekc() == ')')
                        break;

                    pp = 0;
                    while (is_ident_char(peekc()) && pp < 31)
                        params[nargs][pp++] = (char)getc_src();
                    params[nargs][pp] = 0;
                    if (params[nargs][0] && nargs < 7)
                        nargs++;

                    while (isspace((unsigned char)peekc()) && peekc() != '\n')
                        getc_src();
                    if (peekc() == ',')
                        getc_src();
                }
                if (peekc() == ')')
                    getc_src();

                while (isspace((unsigned char)peekc()) && peekc() != '\n') getc_src();

                i = 0;
                while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
                    val[i++] = (char)getc_src();
                val[i] = 0;
                strip_macro_replacement_comments(val);

                if (name[0]) add_define_ex(name, val[0] ? val : "1", 1, nargs, params);
                (void)pi;
            } else {
                while (isspace((unsigned char)peekc()) && peekc() != '\n') getc_src();

                i = 0;
                while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
                    val[i++] = (char)getc_src();
                val[i] = 0;
                strip_macro_replacement_comments(val);

                if (name[0]) add_define(name, val[0] ? val : "1");
            }
        }
    } else if (!strcmp(word, "__asm_line")) {
        /* asm content smuggled from the filter pass as a pseudo-directive.
         * SOH (\001) separates the keyword from the original line content
         * so that all leading whitespace in the asm line is preserved.
         * Buffer into pending_asm_buf rather than writing directly: the
         * tokenizer may re-visit this position during scan_function_body()
         * pre-passes (which save/restore posi).  flush_pending_asm() is
         * called from emit_function_epilogue() and emit_data() to emit the
         * content exactly once at the correct output position. */
        char line[512]; int li = 0; int ch;
        if (peekc() == '\001') getc_src();
        while ((ch = peekc()) && ch != '\n') {
            if (li < (int)sizeof(line) - 1) line[li++] = (char)ch;
            getc_src();
        }
        line[li] = 0;
        if (!scan_mode && asm_suppress_depth == 0) {
            if (pending_asm_len + li + 2 < (int)sizeof(pending_asm_buf)) {
                memcpy(pending_asm_buf + pending_asm_len, line, (size_t)li);
                pending_asm_len += li;
                pending_asm_buf[pending_asm_len++] = '\n';
            }
            /* If this line is "public _name", mark the C symbol as defined
             * so DCC won't emit a redundant extrn for it at end of file. */
            {
                const char *p = line;
                char cname[64]; int ci;
                while (*p == ' ' || *p == '\t') p++;
                if (strncmp(p, "public", 6) == 0 &&
                    (p[6] == ' ' || p[6] == '\t')) {
                    p += 7;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == '_') {
                        p++;
                        ci = 0;
                        while (is_ident_char((unsigned char)*p) && ci < 63)
                            cname[ci++] = *p++;
                        cname[ci] = 0;
                        if (cname[0]) {
                            struct Sym *s = find_global(cname);
                            if (s) {
                                s->is_defined = 1;
                                s->needs_extrn = 0;
                            }
                        }
                    }
                }
            }
        }
        return;
    } else if (!strcmp(word, "warning")) {
        if (pp_active) {
            i = 0;
            while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
                val[i++] = (char)getc_src();
            val[i] = 0;
            fprintf(stderr, "%s:%d: warning: #warning %s\n",
                    current_file_name[0] ? current_file_name : (input_name ? input_name : "<input>"),
                    line_no, val);
        }
    } else if (!strcmp(word, "pragma")) {
        if (pp_active) {
            i = 0;
            while ((c = peekc()) != 0 && c != '\n' && i < (int)sizeof(val) - 1)
                val[i++] = (char)getc_src();
            val[i] = 0;
            handle_pragma_line(val);
        }
    } else if (word[0] == 0) {
        /* null directive (#) is silently ignored */
    } else {
        /* Any other unrecognised directive is an error when active.
         * In an inactive block it is silently skipped so that unknown
         * directives in dead code do not cascade into spurious errors. */
        if (pp_active) {
            fprintf(stderr, "%s:%d: error: unknown preprocessor directive '#%s'\n",
                    current_file_name[0] ? current_file_name : (input_name ? input_name : "<input>"),
                    line_no, word);
            errors++;
            if (errors > 40) fatal("too many errors");
        }
    }

    while (peekc() && peekc() != '\n') getc_src();
}

void skip_ws_and_comments(void)
{
    int c;

    for (;;) {
        c = peekc();

        while (isspace((unsigned char)c)) {
            getc_src();
            c = peekc();
        }

        if (c == '/' && posi + 1 < src_len && src[posi + 1] == '/') {
            while (peekc() && peekc() != '\n') getc_src();
            continue;
        }

        if (c == '/' && posi + 1 < src_len && src[posi + 1] == '*') {
            posi += 2;
            while (peekc()) {
                if (peekc() == '*' && posi + 1 < src_len && src[posi + 1] == '/') {
                    posi += 2;
                    break;
                }
                getc_src();
            }
            continue;
        }

        if (c == '#') {
            getc_src();
            parse_preprocessor_line();
            continue;
        }

        if (!pp_active) {
            while (peekc() && peekc() != '\n')
                getc_src();
            continue;
        }

        break;
    }
}

int keyword_kind(const char *s)
{
    if (!strcmp(s, "int")) return TOK_INT;
    if (!strcmp(s, "short")) return TOK_SHORT;
    if (!strcmp(s, "long")) return TOK_LONG;
    if (!strcmp(s, "float")) return TOK_FLOAT;
    if (!strcmp(s, "_Bool")) return TOK_BOOL;
    if (!strcmp(s, "char")) return TOK_CHAR;
    if (!strcmp(s, "void")) return TOK_VOID;
    if (!strcmp(s, "unsigned")) return TOK_UNSIGNED;
    if (!strcmp(s, "signed")) return TOK_SIGNED;
    if (!strcmp(s, "extern")) return TOK_EXTERN;
    if (!strcmp(s, "static")) return TOK_STATIC;
    if (!strcmp(s, "register")) return TOK_REGISTER;
    if (!strcmp(s, "auto")) return TOK_AUTO;
    if (!strcmp(s, "const")) return TOK_CONST;
    if (!strcmp(s, "volatile")) return TOK_VOLATILE;
    if (!strcmp(s, "if")) return TOK_IF;
    if (!strcmp(s, "else")) return TOK_ELSE;
    if (!strcmp(s, "while")) return TOK_WHILE;
    if (!strcmp(s, "for")) return TOK_FOR;
    if (!strcmp(s, "return")) return TOK_RETURN;
    if (!strcmp(s, "break")) return TOK_BREAK;
    if (!strcmp(s, "continue")) return TOK_CONTINUE;
    if (!strcmp(s, "sizeof")) return TOK_SIZEOF;
    if (!strcmp(s, "switch")) return TOK_SWITCH;
    if (!strcmp(s, "case")) return TOK_CASE;
    if (!strcmp(s, "default")) return TOK_DEFAULT;
    if (!strcmp(s, "typedef")) return TOK_TYPEDEF;
    if (!strcmp(s, "struct")) return TOK_STRUCT;
    if (!strcmp(s, "union")) return TOK_UNION;
    if (!strcmp(s, "enum"))  return TOK_ENUM;
    if (!strcmp(s, "goto"))  return TOK_GOTO;
    if (!strcmp(s, "do")) return TOK_DO;
    if (!strcmp(s, "inline")) return TOK_INLINE;
    return TOK_ID;
}

static void skip_gnu_attribute(void)
{
    int depth;
    int c;

    while (isspace((unsigned char)peekc()))
        getc_src();
    if (peekc() != '(')
        return;

    depth = 0;
    while ((c = getc_src()) != 0) {
        if (c == '"') {
            while (peekc() && peekc() != '"') {
                if (peekc() == '\\') {
                    getc_src();
                    if (peekc()) getc_src();
                } else {
                    getc_src();
                }
            }
            if (peekc() == '"') getc_src();
            continue;
        }
        if (c == '\'') {
            while (peekc() && peekc() != '\'') {
                if (peekc() == '\\') {
                    getc_src();
                    if (peekc()) getc_src();
                } else {
                    getc_src();
                }
            }
            if (peekc() == '\'') getc_src();
            continue;
        }
        if (c == '(')
            depth++;
        else if (c == ')') {
            depth--;
            if (depth <= 0)
                break;
        }
    }
}

int read_escape(void)
{
    int c;
    int v;
    c = getc_src();
    if (c == 'n') return '\n';
    if (c == 'r') return '\r';
    if (c == 't') return '\t';
    if (c == 'b') return '\b';
    if (c == 'f') return '\f';
    if (c == 'v') return '\v';
    if (c == 'a') return 7;
    if (c == '?') return '?';
    if (c == '\\') return '\\';
    if (c == '\'') return '\'';
    if (c == '"') return '"';
    if (c >= '0' && c <= '7') {
        v = c - '0';
        if (peekc() >= '0' && peekc() <= '7') {
            v = v * 8 + (getc_src() - '0');
            if (peekc() >= '0' && peekc() <= '7')
                v = v * 8 + (getc_src() - '0');
        }
        return v & 255;
    }
    if (c == 'x') {
        v = 0;
        while (isxdigit((unsigned char)peekc())) {
            int d = getc_src();
            if (d >= '0' && d <= '9') v = v * 16 + (d - '0');
            else if (d >= 'a' && d <= 'f') v = v * 16 + (d - 'a' + 10);
            else v = v * 16 + (d - 'A' + 10);
        }
        return v & 255;
    }
    return c;
}

long parse_number_string(const char *s)
{
    long v;
    int i;
    int neg;

    v = 0;
    i = 0;
    neg = 0;

    if (s[i] == '-') {
        neg = 1;
        i++;
    }

    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        while (isxdigit((unsigned char)s[i])) {
            v *= 16;
            if (s[i] >= '0' && s[i] <= '9') v += s[i] - '0';
            else if (s[i] >= 'a' && s[i] <= 'f') v += s[i] - 'a' + 10;
            else v += s[i] - 'A' + 10;
            i++;
        }
    } else {
        while (isdigit((unsigned char)s[i])) {
            v = v * 10 + s[i] - '0';
            i++;
        }
    }

    while (s[i] == 'u' || s[i] == 'U' || s[i] == 'l' || s[i] == 'L')
        i++;

    if (neg) v = -v;
    return v & 0xffffL;
}


unsigned long parse_float_literal_bits(const char *text)
{
    /* Stage-1 float support stores 32-bit IEEE single constants as opaque
     * data.  The host compiler is expected to use IEEE-754 float, which is
     * true for the supported Windows/Linux/macOS build hosts. */
    union {
        float f;
        unsigned char b[4];
    } u;
    double d;
    unsigned long v;

    d = atof(text);
    u.f = (float)d;
    v = ((unsigned long)u.b[0]) |
        ((unsigned long)u.b[1] << 8) |
        ((unsigned long)u.b[2] << 16) |
        ((unsigned long)u.b[3] << 24);
    return v;
}


int parse_escape_string_char(const char **ps)
{
    int c;

    c = (unsigned char)**ps;
    if (c == 0)
        return 0;
    *ps = *ps + 1;

    if (c != '\\')
        return c;

    c = (unsigned char)**ps;
    if (c == 0)
        return '\\';
    *ps = *ps + 1;

    if (c == 'n') return '\n';
    if (c == 'r') return '\r';
    if (c == 't') return '\t';
    if (c == 'b') return '\b';
    if (c == 'f') return '\f';
    if (c == 'v') return '\v';
    if (c == 'a') return 7;
    if (c == '\\') return '\\';
    if (c == '\'') return '\'';
    if (c == '"') return '"';

    if (c >= '0' && c <= '7') {
        int v;
        int n;
        v = c - '0';
        n = 1;
        while (n < 3 && **ps >= '0' && **ps <= '7') {
            v = v * 8 + (**ps - '0');
            *ps = *ps + 1;
            n++;
        }
        return v & 255;
    }

    return c;
}

int parse_charlit_string_value(const char *s, long *out)
{
    const char *p;
    int c;

    p = s;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != '\'')
        return 0;
    p++;

    c = parse_escape_string_char(&p);

    if (*p != '\'')
        return 0;
    p++;

    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != 0)
        return 0;

    *out = c & 255;
    return 1;
}


/* Macro names appearing inside their own replacement text must not be
 * expanded again.  The real C preprocessor tracks disabled macro tokens;
 * dcc uses textual reinsertion, so keep a small set of source ranges whose
 * tokens came from one macro replacement and suppress only that same macro
 * name while scanning the range.  This fixes cases such as:
 *     #define words G->words
 * where the member name in the replacement is intentionally the macro name.
 */
#define MAX_DISABLED_MACRO_RANGES 64
static long disabled_macro_start[MAX_DISABLED_MACRO_RANGES];
static long disabled_macro_end[MAX_DISABLED_MACRO_RANGES];
static char disabled_macro_name[MAX_DISABLED_MACRO_RANGES][64];
static int ndisabled_macro_ranges;

static int macro_disabled_here(const char *name, long at)
{
    int i;

    for (i = ndisabled_macro_ranges - 1; i >= 0; --i) {
        if (at >= disabled_macro_start[i] && at < disabled_macro_end[i] &&
            !strcmp(disabled_macro_name[i], name)) {
            return 1;
        }
    }
    return 0;
}

static void add_disabled_macro_range(long start, long end, const char *name)
{
    int i;

    if (!name || !name[0] || end <= start)
        return;

    if (ndisabled_macro_ranges >= MAX_DISABLED_MACRO_RANGES) {
        for (i = 1; i < ndisabled_macro_ranges; ++i) {
            disabled_macro_start[i - 1] = disabled_macro_start[i];
            disabled_macro_end[i - 1] = disabled_macro_end[i];
            strcpy(disabled_macro_name[i - 1], disabled_macro_name[i]);
        }
        ndisabled_macro_ranges--;
    }

    i = ndisabled_macro_ranges++;
    disabled_macro_start[i] = start;
    disabled_macro_end[i] = end;
    strncpy(disabled_macro_name[i], name, sizeof(disabled_macro_name[i]) - 1);
    disabled_macro_name[i][sizeof(disabled_macro_name[i]) - 1] = 0;
}

void replace_source_range(long start, long end, const char *text)
{
    long n;
    long rest;
    char *nsrc;

    if (start < 0) start = 0;
    if (end < start) end = start;
    if (end > src_len) end = src_len;

    n = (long)strlen(text);
    rest = src_len - end;
    nsrc = (char *)xmalloc((size_t)(start + n + rest + 1));
    memcpy(nsrc, src, (size_t)start);
    memcpy(nsrc + start, text, (size_t)n);
    memcpy(nsrc + start + n, src + end, (size_t)rest);
    nsrc[start + n + rest] = 0;
    src = nsrc;
    src_len = start + n + rest;
    posi = start;
}

static void replace_source_range_disabled(long start, long end, const char *text, const char *macro_name)
{
    long n;

    n = (long)strlen(text);
    replace_source_range(start, end, text);
    add_disabled_macro_range(start, start + n, macro_name);
}

void trim_arg(char *s)
{
    int i;
    int j;
    int n;

    i = 0;
    while (s[i] && isspace((unsigned char)s[i]))
        i++;

    if (i > 0) {
        j = 0;
        while (s[i])
            s[j++] = s[i++];
        s[j] = 0;
    }

    n = (int)strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = 0;
        n--;
    }
}

void strip_macro_replacement_comments(char *s)
{
    int i;
    int o;
    int quote;
    int c;

    /*
     * #define replacement text ends at newline, but comments are replaced by
     * whitespace before macro replacement is stored.  Do not strip comment
     * markers that occur inside string or character literals.
     */
    i = 0;
    o = 0;
    quote = 0;

    while (s[i]) {
        c = (unsigned char)s[i];

        if (quote) {
            s[o++] = s[i++];
            if (c == '\\' && s[i]) {
                s[o++] = s[i++];
                continue;
            }
            if (c == quote)
                quote = 0;
            continue;
        }

        if (c == '"' || c == '\'') {
            quote = c;
            s[o++] = s[i++];
            continue;
        }

        if (c == '/' && s[i + 1] == '*') {
            i += 2;
            while (s[i] && !(s[i] == '*' && s[i + 1] == '/'))
                i++;
            if (s[i])
                i += 2;
            if (o > 0 && !isspace((unsigned char)s[o - 1]))
                s[o++] = ' ';
            continue;
        }

        if (c == '/' && s[i + 1] == '/') {
            if (o > 0 && !isspace((unsigned char)s[o - 1]))
                s[o++] = ' ';
            break;
        }

        s[o++] = s[i++];
    }

    s[o] = 0;
    trim_arg(s);
}

static int macro_call_args_too_many;

int read_macro_call_args(char args[8][128], int *nargs)
{
    int c;
    int depth;
    int ai;
    int ap;

    while (isspace((unsigned char)peekc()))
        getc_src();

    if (peekc() != '(')
        return 0;

    getc_src();
    ai = 0;
    ap = 0;
    depth = 0;
    macro_call_args_too_many = 0;
    memset(args, 0, 8 * 128);

    for (;;) {
        c = getc_src();
        if (c == 0)
            return 0;

        if (c == '"') {
            if (ap < 127) args[ai][ap++] = (char)c;
            while ((c = getc_src()) != 0) {
                if (ap < 127) args[ai][ap++] = (char)c;
                if (c == '\\') {
                    c = getc_src();
                    if (c == 0) return 0;
                    if (ap < 127) args[ai][ap++] = (char)c;
                } else if (c == '"') {
                    break;
                }
            }
            continue;
        }

        if (c == '\'') {
            if (ap < 127) args[ai][ap++] = (char)c;
            while ((c = getc_src()) != 0) {
                if (ap < 127) args[ai][ap++] = (char)c;
                if (c == '\\') {
                    c = getc_src();
                    if (c == 0) return 0;
                    if (ap < 127) args[ai][ap++] = (char)c;
                } else if (c == '\'') {
                    break;
                }
            }
            continue;
        }

        if (c == '(' || c == '[' || c == '{') {
            depth++;
            if (ap < 127) args[ai][ap++] = (char)c;
            continue;
        }

        if (c == ')' && depth == 0) {
            args[ai][ap] = 0;
            trim_arg(args[ai]);
            ai++;
            break;
        }

        if (c == ')' || c == ']' || c == '}') {
            depth--;
            if (ap < 127) args[ai][ap++] = (char)c;
            continue;
        }

        if (c == ',' && depth == 0) {
            args[ai][ap] = 0;
            trim_arg(args[ai]);
            ai++;
            if (ai >= 8) {
                macro_call_args_too_many = 1;
                while ((c = getc_src()) != 0) {
                    if (c == '(' || c == '[' || c == '{')
                        depth++;
                    else if (c == ')' && depth == 0)
                        break;
                    else if ((c == ')' || c == ']' || c == '}') && depth > 0)
                        depth--;
                }
                *nargs = ai + 1;
                return 1;
            }
            ap = 0;
            continue;
        }

        if (ap < 127)
            args[ai][ap++] = (char)c;
    }

    if (ai == 1 && args[0][0] == 0)
        ai = 0;
    *nargs = ai;
    return 1;
}


void append_macro_string_literal(const char *arg, char *out, int *oip, int outsz)
{
    int oi;
    const char *p;
    int last_space;

    oi = oip[0];
    if (oi < outsz - 1)
        out[oi++] = '"';

    p = arg;
    while (*p && isspace((unsigned char)*p))
        p++;

    last_space = 0;
    while (*p && oi < outsz - 1) {
        unsigned char c;

        c = (unsigned char)*p++;

        if (isspace(c)) {
            last_space = 1;
            continue;
        }

        if (last_space) {
            if (oi < outsz - 1)
                out[oi++] = ' ';
            last_space = 0;
        }

        if (c == '"' || c == '\\') {
            if (oi < outsz - 1)
                out[oi++] = '\\';
            if (oi < outsz - 1)
                out[oi++] = (char)c;
        } else if (c == '\n' || c == '\r') {
            if (oi < outsz - 1)
                out[oi++] = ' ';
        } else {
            out[oi++] = (char)c;
        }
    }

    if (oi < outsz - 1)
        out[oi++] = '"';

    oip[0] = oi;
}

int macro_param_index(int di, const char *ident)
{
    int j;
    for (j = 0; j < defs[di].nargs; ++j) {
        if (!strcmp(ident, defs[di].params[j]))
            return j;
    }
    return -1;
}


void expand_function_macro(int di, char args[8][128], char *out, int outsz);

int read_macro_call_args_text(const char **pp, char args[8][128], int *nargs)
{
    const char *p;
    int c;
    int depth;
    int ai;
    int ap;

    p = *pp;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != '(')
        return 0;

    p++;
    ai = 0;
    ap = 0;
    depth = 0;
    memset(args, 0, 8 * 128);

    for (;;) {
        c = (unsigned char)*p++;
        if (c == 0)
            return 0;

        if (c == '"') {
            if (ap < 127) args[ai][ap++] = (char)c;
            while ((c = (unsigned char)*p++) != 0) {
                if (ap < 127) args[ai][ap++] = (char)c;
                if (c == '\\') {
                    c = (unsigned char)*p++;
                    if (c == 0) return 0;
                    if (ap < 127) args[ai][ap++] = (char)c;
                } else if (c == '"') {
                    break;
                }
            }
            continue;
        }

        if (c == '\'') {
            if (ap < 127) args[ai][ap++] = (char)c;
            while ((c = (unsigned char)*p++) != 0) {
                if (ap < 127) args[ai][ap++] = (char)c;
                if (c == '\\') {
                    c = (unsigned char)*p++;
                    if (c == 0) return 0;
                    if (ap < 127) args[ai][ap++] = (char)c;
                } else if (c == '\'') {
                    break;
                }
            }
            continue;
        }

        if (c == '(' || c == '[' || c == '{') {
            depth++;
            if (ap < 127) args[ai][ap++] = (char)c;
            continue;
        }

        if (c == ')' && depth == 0) {
            args[ai][ap] = 0;
            trim_arg(args[ai]);
            ai++;
            break;
        }

        if (c == ')' || c == ']' || c == '}') {
            depth--;
            if (ap < 127) args[ai][ap++] = (char)c;
            continue;
        }

        if (c == ',' && depth == 0) {
            args[ai][ap] = 0;
            trim_arg(args[ai]);
            ai++;
            if (ai >= 8)
                fatal("too many macro arguments");
            ap = 0;
            continue;
        }

        if (ap < 127)
            args[ai][ap++] = (char)c;
    }

    if (ai == 1 && args[0][0] == 0)
        ai = 0;

    *nargs = ai;
    *pp = p;
    return 1;
}

void macro_expand_argument_text(const char *in, char *out, int outsz, int depth)
{
    int oi;
    const char *p;

    if (depth > 8) {
        size_t n = strlen(in);
        if (n >= (size_t)outsz) n = (size_t)outsz - 1;
        memcpy(out, in, n);
        out[n] = '\0';
        return;
    }

    oi = 0;
    p = in;
    while (*p && oi < outsz - 1) {
        if (*p == '"') {
            out[oi++] = *p++;
            while (*p && oi < outsz - 1) {
                out[oi++] = *p;
                if (*p == '\\' && p[1] && oi < outsz - 1) {
                    p++;
                    out[oi++] = *p++;
                    continue;
                }
                if (*p++ == '"')
                    break;
            }
            continue;
        }

        if (*p == '\'') {
            out[oi++] = *p++;
            while (*p && oi < outsz - 1) {
                out[oi++] = *p;
                if (*p == '\\' && p[1] && oi < outsz - 1) {
                    p++;
                    out[oi++] = *p++;
                    continue;
                }
                if (*p++ == '\'')
                    break;
            }
            continue;
        }

        if (is_ident_start((unsigned char)*p)) {
            char ident[64];
            int ii;
            int di;

            ii = 0;
            while (is_ident_char((unsigned char)*p) && ii < 63)
                ident[ii++] = *p++;
            ident[ii] = 0;

            if (!strcmp(ident, "__LINE__")) {
                char numbuf[32];
                sprintf(numbuf, "%d", line_no);
                for (ii = 0; numbuf[ii] && oi < outsz - 1; ++ii)
                    out[oi++] = numbuf[ii];
                continue;
            }
            if (!strcmp(ident, "__FILE__")) {
                char filebuf[320];
                const char *fp0;
                int fj;
                fp0 = tok.file[0] ? tok.file : (input_name ? input_name : "<input>");
                fj = 0;
                filebuf[fj++] = '"';
                while (*fp0 && fj < (int)sizeof(filebuf) - 2) {
                    if (*fp0 == '\\' || *fp0 == '"')
                        filebuf[fj++] = '\\';
                    filebuf[fj++] = *fp0++;
                }
                filebuf[fj++] = '"';
                filebuf[fj] = 0;
                for (ii = 0; filebuf[ii] && oi < outsz - 1; ++ii)
                    out[oi++] = filebuf[ii];
                continue;
            }

            di = find_define(ident);
            if (di >= 0) {
                if (defs[di].is_func) {
                    const char *after_ident;
                    char args[8][128];
                    int nargs;

                    after_ident = p;
                    if (read_macro_call_args_text(&after_ident, args, &nargs)) {
                        char tmp[MAX_MACRO_TEXT];
                        char tmp2[MAX_MACRO_TEXT];
                        if (nargs != defs[di].nargs)
                            fatal("wrong number of macro arguments");
                        expand_function_macro(di, args, tmp, sizeof(tmp));
                        macro_expand_argument_text(tmp, tmp2, sizeof(tmp2), depth + 1);
                        for (ii = 0; tmp2[ii] && oi < outsz - 1; ++ii)
                            out[oi++] = tmp2[ii];
                        p = after_ident;
                        continue;
                    }
                } else {
                    char tmp[MAX_MACRO_TEXT];
                    macro_expand_argument_text(defs[di].value, tmp, sizeof(tmp), depth + 1);
                    for (ii = 0; tmp[ii] && oi < outsz - 1; ++ii)
                        out[oi++] = tmp[ii];
                    continue;
                }
            }

            for (ii = 0; ident[ii] && oi < outsz - 1; ++ii)
                out[oi++] = ident[ii];
        } else {
            out[oi++] = *p++;
        }
    }
    out[oi] = 0;
}

void paste_tokens_in_text(char *s)
{
    char tmp[MAX_MACRO_TEXT];
    int i;
    int o;
    int had_placemarker;

    i = 0;
    o = 0;

    while (s[i] && o < (int)sizeof(tmp) - 1) {
        if (s[i] == MACRO_PLACEMARKER) {
            i++;
            continue;
        }

        if (s[i] == '#' && s[i + 1] == '#') {
            while (o > 0 && isspace((unsigned char)tmp[o - 1]))
                --o;
            if (o > 0 && tmp[o - 1] == MACRO_PLACEMARKER)
                --o;
            i += 2;
            while (s[i] && isspace((unsigned char)s[i]))
                ++i;
            had_placemarker = 0;
            if (s[i] == MACRO_PLACEMARKER) {
                had_placemarker = 1;
                i++;
                while (s[i] && isspace((unsigned char)s[i]))
                    ++i;
            }
            if (had_placemarker && o > 0 && s[i] &&
                !isspace((unsigned char)tmp[o - 1]) &&
                !isspace((unsigned char)s[i]))
                tmp[o++] = ' ';
            continue;
        }

        tmp[o++] = s[i++];
    }

    tmp[o] = 0;
    strcpy(s, tmp);
}

int replacement_param_raw_context(const char *start, const char *param_start, const char *param_end)
{
    const char *p;

    /* Used with # before parameter? */
    p = param_start;
    while (p > start && (p[-1] == ' ' || p[-1] == '\t'))
        --p;
    if (p > start && p[-1] == '#') {
        if (!(p - 1 > start && p[-2] == '#'))
            return 1;
    }

    /* Adjacent to ## before parameter? */
    if (p - 2 >= start && p[-1] == '#' && p[-2] == '#')
        return 1;

    /* Adjacent to ## after parameter? */
    p = param_end;
    while (*p == ' ' || *p == '\t')
        ++p;
    if (p[0] == '#' && p[1] == '#')
        return 1;

    return 0;
}

void expand_function_macro(int di, char args[8][128], char *out, int outsz)
{
    const char *v;
    int oi;
    int i;
    int j;
    char ident[64];
    int matched;
    char expanded_args[8][MAX_MACRO_TEXT];

    for (i = 0; i < 8; ++i)
        expanded_args[i][0] = 0;
    for (i = 0; i < defs[di].nargs && i < 8; ++i)
        macro_expand_argument_text(args[i], expanded_args[i], sizeof(expanded_args[i]), 0);

    v = defs[di].value;
    oi = 0;

    while (*v && oi < outsz - 1) {
        /*
         * C89 macro stringification:
         *     #define S(x) #x
         *     S(a + b)  ->  "a + b"
         *
         * Stringification uses the raw, unexpanded argument.
         */
        if (*v == '#') {
            const char *q;

            if (v[1] == '#') {
                out[oi++] = *v++;
                out[oi++] = *v++;
                continue;
            }

            q = v + 1;
            while (*q == ' ' || *q == '\t')
                q++;

            if (is_ident_start((unsigned char)*q)) {
                i = 0;
                while (is_ident_char((unsigned char)*q) && i < 63) {
                    ident[i++] = *q;
                    q++;
                }
                ident[i] = 0;

                matched = macro_param_index(di, ident);
                if (matched >= 0) {
                    append_macro_string_literal(args[matched], out, &oi, outsz);
                    v = q;
                    continue;
                }
            }

            out[oi++] = *v++;
            continue;
        }

        if (is_ident_start((unsigned char)*v)) {
            const char *ident_start;
            const char *ident_end;

            ident_start = v;
            i = 0;
            while (is_ident_char((unsigned char)*v) && i < 63) {
                ident[i++] = *v;
                v++;
            }
            ident_end = v;
            ident[i] = 0;

            matched = macro_param_index(di, ident);

            if (matched >= 0) {
                const char *a;
                if (replacement_param_raw_context(defs[di].value, ident_start, ident_end))
                    a = args[matched];
                else
                    a = expanded_args[matched];

                if (*a == 0 && replacement_param_raw_context(defs[di].value, ident_start, ident_end)) {
                    if (oi < outsz - 1)
                        out[oi++] = MACRO_PLACEMARKER;
                } else {
                    while (*a && oi < outsz - 1)
                        out[oi++] = *a++;
                }
            } else {
                for (j = 0; ident[j] && oi < outsz - 1; ++j)
                    out[oi++] = ident[j];
            }
        } else {
            out[oi++] = *v++;
        }
    }

    out[oi] = 0;
    paste_tokens_in_text(out);
}



int macro_value_is_float_literal(const char *s)
{
    const char *p;
    int saw_digit;
    int saw_float;
    int c;

    p = s;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p == '+' || *p == '-')
        p++;

    saw_digit = 0;
    saw_float = 0;

    while (isdigit((unsigned char)*p)) {
        saw_digit = 1;
        p++;
    }

    if (*p == '.') {
        saw_float = 1;
        p++;
        while (isdigit((unsigned char)*p)) {
            saw_digit = 1;
            p++;
        }
    }

    if ((*p == 'e' || *p == 'E') && saw_digit) {
        saw_float = 1;
        p++;
        if (*p == '+' || *p == '-')
            p++;
        if (!isdigit((unsigned char)*p))
            return 0;
        while (isdigit((unsigned char)*p))
            p++;
    }

    if (!saw_digit || !saw_float)
        return 0;

    c = (unsigned char)*p;
    if (c == 'f' || c == 'F' || c == 'l' || c == 'L')
        p++;

    while (*p && isspace((unsigned char)*p))
        p++;

    return *p == 0;
}


int macro_number_should_expand_textually(const char *s)
{
    const char *p;
    unsigned long v;
    int saw_digit;
    int is_nondecimal;

    p = s;
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p == '+' || *p == '-')
        p++;

    saw_digit = 0;
    is_nondecimal = 0;
    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        is_nondecimal = 1;
        p += 2;
        while (isxdigit((unsigned char)*p)) {
            saw_digit = 1;
            p++;
        }
    } else if (p[0] == '0' && isdigit((unsigned char)p[1])) {
        is_nondecimal = 1; /* octal */
        while (isdigit((unsigned char)*p)) {
            saw_digit = 1;
            p++;
        }
    } else {
        while (isdigit((unsigned char)*p)) {
            saw_digit = 1;
            p++;
        }
    }

    if (!saw_digit)
        return 0;

    /*
     * Any explicit long suffix must remain text so the normal integer
     * literal lexer preserves long type.  Also keep values above 16 bits
     * textual; otherwise define_number_value() truncates object-like numeric
     * macros to the target int width.  That broke LONG_MAX/LONG_MIN.
     *
     * Also expand non-decimal (hex/octal) constants with values in
     * 0x8000..0xFFFF textually so the normal lexer sets g_tok_unsigned_suffix,
     * giving them type 'unsigned int' per C89.  Without this, the fast-path
     * through define_number_value() leaves g_tok_unsigned_suffix unset and
     * the constant is sign-extended to long instead of zero-extended, causing
     * comparisons like (long_var > 0xBDFF) to compare against -16897 instead
     * of 48639.
     */
    if (*p == 'l' || *p == 'L')
        return 1;
    if (*p == 'u' || *p == 'U') {
        p++;
        if (*p == 'l' || *p == 'L')
            return 1;
    }

    v = strtoul(s, NULL, 0);
    return v > 0xffffUL || (is_nondecimal && v > 32767UL);
}

static int macro_value_is_integer_literal(const char *s)
{
    char *endp;

    while (*s && isspace((unsigned char)*s))
        s++;

    if (!isdigit((unsigned char)*s) &&
        !((s[0] == '-' || s[0] == '+') && isdigit((unsigned char)s[1])))
        return 0;

    (void)strtoul(s, &endp, 0);

    while (*endp == 'u' || *endp == 'U' || *endp == 'l' || *endp == 'L')
        endp++;

    while (*endp && isspace((unsigned char)*endp))
        endp++;

    return *endp == 0;
}

int define_number_value(const char *name, long *out, int depth)
{
    int di;
    const char *v;
    char nested[64];
    int i;

    if (depth > 16) {
        return 0;
    }

    di = find_define(name);
    if (di < 0) {
        return 0;
    }
    if (defs[di].is_func) {
        return 0;
    }

    v = defs[di].value;

    while (v[0]) {
        if (!isspace((unsigned char)v[0])) {
            break;
        }
        v = v + 1;
    }

    if (macro_value_is_integer_literal(v)) {
        out[0] = parse_number_string(v);
        return 1;
    }

    if (v[0] == '\'') {
        return parse_charlit_string_value(v, out);
    }

    if (is_ident_start((unsigned char)v[0])) {
        i = 0;
        while (is_ident_char((unsigned char)v[0]) && i < 63) {
            nested[i] = v[0];
            i = i + 1;
            v = v + 1;
        }
        nested[i] = 0;

        while (v[0]) {
            if (!isspace((unsigned char)v[0])) {
                break;
            }
            v = v + 1;
        }

        if (v[0] == 0) {
            return define_number_value(nested, out, depth + 1);
        }
    }

    return 0;
}

void next_token(void)
{
    int c, d, i, di;
    long start_line;

    skip_ws_and_comments();
    memset(&tok, 0, sizeof(tok));

    start_line = line_no;
    c = getc_src();
    tok_start_pos = posi - 1;
    source_location_at(tok_start_pos, tok.file, sizeof(tok.file), &tok_line);
    (void)start_line;

    if (!c) {
        tok.kind = TOK_EOF;
        strcpy(tok.text, "<eof>");
        return;
    }

    if (c == 'L' && peekc() == '\'') {
        getc_src();     /* consume opening quote */
        if (peekc() == '\\') {
            getc_src();
            tok.val = read_escape();
        } else {
            tok.val = getc_src();
        }
        if (peekc() == '\'') getc_src();
        tok.kind = TOK_NUM;
        sprintf(tok.text, "%ld", tok.val & 0xffffL);
        return;
    }

    if (c == 'L' && peekc() == '"') {
        getc_src();     /* consume opening quote */
        i = 0;
        while (peekc() && peekc() != '"' && i < MAX_TOK_TEXT - 1) {
            if (peekc() == '\\') {
                getc_src();
                tok.text[i++] = (char)read_escape();
            } else {
                tok.text[i++] = (char)getc_src();
            }
        }
        tok.text[i] = 0;
        /* Drain an over-long wide literal up to the closing quote (see the
         * narrow-string case below) so the lexer stays synchronized. */
        if (peekc() && peekc() != '"') {
            error_here("string literal too long");
            while (peekc() && peekc() != '"') {
                if (peekc() == '\\') {
                    getc_src();
                    read_escape();
                } else {
                    getc_src();
                }
            }
        }
        if (peekc() == '"') getc_src();
        tok.kind = TOK_WSTR;
        return;
    }

    if (is_ident_start(c)) {
        i = 0;
        tok.text[i++] = (char)c;
        while (is_ident_char(peekc()) && i < MAX_TOK_TEXT - 1)
            tok.text[i++] = (char)getc_src();
        tok.text[i] = 0;

        if (!strcmp(tok.text, "__attribute__")) {
            skip_gnu_attribute();
            next_token();
            return;
        }

        /* C89 predefined macros.  These are handled by the lexer so
         * __FILE__ and __LINE__ reflect the logical source location after
         * include/#line processing. */
        if (!strcmp(tok.text, "__DATE__")) {
            tok.kind = TOK_STR;
            strncpy(tok.text, predefined_date_text, sizeof(tok.text) - 1);
            tok.text[sizeof(tok.text) - 1] = 0;
            return;
        }
        if (!strcmp(tok.text, "__TIME__")) {
            tok.kind = TOK_STR;
            strncpy(tok.text, predefined_time_text, sizeof(tok.text) - 1);
            tok.text[sizeof(tok.text) - 1] = 0;
            return;
        }
        if (!strcmp(tok.text, "__FILE__")) {
            tok.kind = TOK_STR;
            strncpy(tok.text, tok.file, sizeof(tok.text) - 1);
            tok.text[sizeof(tok.text) - 1] = 0;
            return;
        }
        if (!strcmp(tok.text, "__LINE__")) {
            tok.kind = TOK_NUM;
            tok.val = tok_line;
            sprintf(tok.text, "%d", tok_line);
            return;
        }
        if (!strcmp(tok.text, "__STDC__")) {
            tok.kind = TOK_NUM;
            tok.val = 1;
            strcpy(tok.text, "1");
            return;
        }

        di = find_define(tok.text);
        if (di >= 0 && macro_disabled_here(tok.text, tok_start_pos))
            di = -1;
        if (di >= 0) {
            long dv;
            const char *rv;
            int ri;

            if (defs[di].is_func) {
                long save_pos;
                char args[8][128];
                int nargs;
                char expbuf[512];

                save_pos = posi;
                if (read_macro_call_args(args, &nargs)) {
                    if (nargs != defs[di].nargs) {
                        error_here(macro_call_args_too_many ?
                                   "too many arguments provided to function-like macro invocation" :
                                   "too few arguments provided to function-like macro invocation");
                        replace_source_range(tok_start_pos, posi, "0");
                        next_token();
                        return;
                    }
                    expand_function_macro(di, args, expbuf, sizeof(expbuf));
                    replace_source_range(tok_start_pos, posi, expbuf);
                    next_token();
                    return;
                }
                posi = save_pos;
                tok.kind = TOK_ID;
            } else {
                const char *rv_base;
                rv = defs[di].value;
                while (*rv && isspace((unsigned char)*rv))
                    rv++;
                rv_base = rv;

                if (macro_value_is_float_literal(rv)) {
                    replace_source_range(tok_start_pos, posi, rv);
                    next_token();
                    return;
                }

                if (macro_number_should_expand_textually(rv)) {
                    replace_source_range(tok_start_pos, posi, rv);
                    next_token();
                    return;
                }

                if (define_number_value(tok.text, &dv, 0)) {
                    tok.kind = TOK_NUM;
                    tok.val = dv;
                    sprintf(tok.text, "%ld", dv);
                    return;
                }

                if (is_ident_start((unsigned char)*rv)) {
                    const char *rv0;
                    rv0 = rv;
                    ri = 0;
                    while (is_ident_char((unsigned char)*rv) && ri < 63) {
                        rv++;
                        ri++;
                    }
                    while (*rv && isspace((unsigned char)*rv))
                        rv++;
                    if (*rv == 0) {
                        /*
                         * Expand object-like aliases textually too.  This lets
                         * chained macros and keyword-like macros go back through
                         * the normal lexer instead of becoming a dead identifier.
                         */
                        replace_source_range_disabled(tok_start_pos, posi, rv0, defs[di].name);
                        next_token();
                        return;
                    }
                }

                /*
                 * General object-like macro replacement.  Older dcc only
                 * handled numeric macros and single identifiers, so macros such
                 * as:
                 *
                 *     #define BUF_SIZE ( sizeof( buf ) )
                 *     #define FLAG     ( O_CREAT | O_RDWR )
                 *
                 * were left as undefined identifiers.  Reinsert the replacement
                 * text and lex it normally.
                 */
                replace_source_range_disabled(tok_start_pos, posi, rv_base, defs[di].name);
                next_token();
                return;
            }
        } else {
            tok.kind = keyword_kind(tok.text);
        }
        return;
    }

    if (isdigit(c)) {
        unsigned long v;
        int non_decimal_literal;
        v = 0;
        non_decimal_literal = 0;
        if (c == '0' && (peekc() == 'x' || peekc() == 'X')) {
            non_decimal_literal = 1;
            getc_src();
            while (isxdigit(peekc())) {
                c = getc_src();
                v *= 16UL;
                if (c >= '0' && c <= '9') v += (unsigned long)(c - '0');
                else if (c >= 'a' && c <= 'f') v += (unsigned long)(c - 'a' + 10);
                else v += (unsigned long)(c - 'A' + 10);
                v &= 0xffffffffUL;
            }
        } else {
            int saw_float;
            v = (unsigned long)(c - '0');
            while (isdigit(peekc())) {
                v = v * 10UL + (unsigned long)(getc_src() - '0');
                v &= 0xffffffffUL;
            }

            saw_float = 0;
            if (peekc() == '.') {
                saw_float = 1;
                while (peekc() == '.' || isdigit(peekc())) getc_src();
            }
            if (peekc() == 'e' || peekc() == 'E') {
                saw_float = 1;
                getc_src();
                if (peekc() == '+' || peekc() == '-') getc_src();
                while (isdigit(peekc())) getc_src();
            }
            if (saw_float) {
                int flen;
                while (peekc() == 'f' || peekc() == 'F' ||
                       peekc() == 'l' || peekc() == 'L')
                    getc_src();
                tok.kind = TOK_FLOATLIT;
                flen = (int)(posi - tok_start_pos);
                if (flen >= MAX_TOK_TEXT) flen = MAX_TOK_TEXT - 1;
                memcpy(tok.text, src + tok_start_pos, (size_t)flen);
                tok.text[flen] = 0;
                tok.val = (long)(parse_float_literal_bits(tok.text) & 0xffffffffUL);
                return;
            }
        }

        g_tok_long_suffix = 0;
        g_tok_unsigned_suffix = 0;
        while (peekc() == 'u' || peekc() == 'U' ||
               peekc() == 'l' || peekc() == 'L') {
            int c2 = getc_src();
            if (c2 == 'l' || c2 == 'L') g_tok_long_suffix = 1;
            if (c2 == 'u' || c2 == 'U') g_tok_unsigned_suffix = 1;
        }

        /*
         * With DCC's 16-bit int, unsuffixed hexadecimal/octal constants use
         * the C89 non-decimal sequence: int, unsigned int, long, unsigned long.
         * Thus 0xffff is unsigned int, not signed int, and must zero-extend
         * when assigned to uint32_t/unsigned long.
         *
         * Decimal 65535 is still long, because decimal constants do not try
         * unsigned int before long in C89.
         */
        /* Choose the target C89 integer literal type.  DCC has 16-bit int
         * and 32-bit long.  Keep only two flags because the rest of the
         * compiler represents literal type as TYPE_INT/TYPE_LONG plus
         * TYPE_UNSIGNED.
         *
         * Unsuffixed decimal:     int, long, unsigned long
         * Unsuffixed hex/octal:   int, unsigned int, long, unsigned long
         * U suffix:               unsigned int, unsigned long
         * L suffix:               long, unsigned long
         * UL/LU suffix:           unsigned long
         */
        if (g_tok_unsigned_suffix) {
            if (g_tok_long_suffix || v > 0xffffUL)
                g_tok_long_suffix = 1;
        } else if (g_tok_long_suffix) {
            if (v > 0x7fffffffUL)
                g_tok_unsigned_suffix = 1;
        } else if (non_decimal_literal) {
            if (v <= 32767UL) {
                /* int */
            } else if (v <= 0xffffUL) {
                g_tok_unsigned_suffix = 1;
            } else if (v <= 0x7fffffffUL) {
                g_tok_long_suffix = 1;
            } else {
                g_tok_long_suffix = 1;
                g_tok_unsigned_suffix = 1;
            }
        } else {
            if (v <= 32767UL) {
                /* int */
            } else if (v <= 0x7fffffffUL) {
                g_tok_long_suffix = 1;
            } else {
                g_tok_long_suffix = 1;
                g_tok_unsigned_suffix = 1;
            }
        }

        tok.kind = TOK_NUM;
        tok.val = (long)(v & 0xffffffffUL);
        {
            int flen;
            flen = (int)(posi - tok_start_pos);
            if (flen >= MAX_TOK_TEXT) flen = MAX_TOK_TEXT - 1;
            memcpy(tok.text, src + tok_start_pos, (size_t)flen);
            tok.text[flen] = 0;
        }
        return;
    }

    if (c == '"') {
        i = 0;
        while (peekc() && peekc() != '"' && i < MAX_TOK_TEXT - 1) {
            if (peekc() == '\\') {
                getc_src();
                tok.text[i++] = (char)read_escape();
            } else {
                tok.text[i++] = (char)getc_src();
            }
        }
        tok.text[i] = 0;
        /*
         * Over-long literal: drain the remainder up to the closing quote so
         * the lexer stays in sync.  Previously the scan stopped here, leaving
         * the unread tail (including the closing ") to be relexed as stray
         * tokens, which silently derailed the parser instead of diagnosing.
         */
        if (peekc() && peekc() != '"') {
            error_here("string literal too long");
            while (peekc() && peekc() != '"') {
                if (peekc() == '\\') {
                    getc_src();
                    read_escape();
                } else {
                    getc_src();
                }
            }
        }
        if (peekc() == '"') getc_src();
        tok.kind = TOK_STR;
        return;
    }

    if (c == '\'') {
        if (peekc() == '\\') {
            getc_src();
            tok.val = read_escape();
        } else {
            tok.val = getc_src();
        }
        if (peekc() == '\'') getc_src();
        tok.kind = TOK_CHARLIT;
        strcpy(tok.text, "charlit");
        return;
    }

    d = peekc();

    if (c == '.' && d == '.' && posi + 1 < src_len && src[posi + 1] == '.') {
        getc_src();
        getc_src();
        tok.kind = TOK_ELLIPSIS;
        strcpy(tok.text, "...");
        return;
    }

    if (c == '.' && isdigit((unsigned char)d)) {
        int flen;
        while (isdigit(peekc())) getc_src();
        if (peekc() == 'e' || peekc() == 'E') {
            getc_src();
            if (peekc() == '+' || peekc() == '-') getc_src();
            while (isdigit(peekc())) getc_src();
        }
        while (peekc() == 'f' || peekc() == 'F' ||
               peekc() == 'l' || peekc() == 'L')
            getc_src();
        tok.kind = TOK_FLOATLIT;
        flen = (int)(posi - tok_start_pos);
        if (flen >= MAX_TOK_TEXT) flen = MAX_TOK_TEXT - 1;
        memcpy(tok.text, src + tok_start_pos, (size_t)flen);
        tok.text[flen] = 0;
        tok.val = (long)(parse_float_literal_bits(tok.text) & 0xffffffffUL);
        return;
    }

    if (c == '=') {
        if (d == '=') { getc_src(); tok.kind = TOK_EQ; strcpy(tok.text, "=="); return; }
    } else if (c == '!') {
        if (d == '=') { getc_src(); tok.kind = TOK_NE; strcpy(tok.text, "!="); return; }
    } else if (c == '<') {
        if (d == '=') { getc_src(); tok.kind = TOK_LE; strcpy(tok.text, "<="); return; }
        if (d == '<') { getc_src(); if (peekc() == '=') { getc_src(); tok.kind = TOK_SHLEQ; strcpy(tok.text, "<<="); return; } tok.kind = TOK_SHL; strcpy(tok.text, "<<"); return; }
        if (d == '%') { getc_src(); tok.kind = '{'; strcpy(tok.text, "<%"); return; }
        if (d == ':') { getc_src(); tok.kind = '['; strcpy(tok.text, "<:"); return; }
    } else if (c == '>') {
        if (d == '=') { getc_src(); tok.kind = TOK_GE; strcpy(tok.text, ">="); return; }
        if (d == '>') { getc_src(); if (peekc() == '=') { getc_src(); tok.kind = TOK_SHREQ; strcpy(tok.text, ">>="); return; } tok.kind = TOK_SHR; strcpy(tok.text, ">>"); return; }
    } else if (c == '&') {
        if (d == '&') { getc_src(); tok.kind = TOK_ANDAND; strcpy(tok.text, "&&"); return; }
        if (d == '=') { getc_src(); tok.kind = TOK_ANDEQ; strcpy(tok.text, "&="); return; }
    } else if (c == '|') {
        if (d == '|') { getc_src(); tok.kind = TOK_OROR; strcpy(tok.text, "||"); return; }
        if (d == '=') { getc_src(); tok.kind = TOK_OREQ; strcpy(tok.text, "|="); return; }
    } else if (c == '+') {
        if (d == '+') { getc_src(); tok.kind = TOK_INC; strcpy(tok.text, "++"); return; }
        if (d == '=') { getc_src(); tok.kind = TOK_ADDEQ; strcpy(tok.text, "+="); return; }
    } else if (c == '-') {
        if (d == '-') { getc_src(); tok.kind = TOK_DEC; strcpy(tok.text, "--"); return; }
        if (d == '=') { getc_src(); tok.kind = TOK_SUBEQ; strcpy(tok.text, "-="); return; }
        if (d == '>') { getc_src(); tok.kind = TOK_ARROW; strcpy(tok.text, "->"); return; }
    } else if (c == '*') {
        if (d == '=') { getc_src(); tok.kind = TOK_MULEQ; strcpy(tok.text, "*="); return; }
    } else if (c == '/') {
        if (d == '=') { getc_src(); tok.kind = TOK_DIVEQ; strcpy(tok.text, "/="); return; }
    } else if (c == '%') {
        if (d == '=') { getc_src(); tok.kind = TOK_MODEQ; strcpy(tok.text, "%="); return; }
        if (d == '>') { getc_src(); tok.kind = '}'; strcpy(tok.text, "%>"); return; }
    } else if (c == ':') {
        if (d == '>') { getc_src(); tok.kind = ']'; strcpy(tok.text, ":>"); return; }
    } else if (c == '^') {
        if (d == '=') { getc_src(); tok.kind = TOK_XOREQ; strcpy(tok.text, "^="); return; }
    }

    tok.kind = c;
    tok.text[0] = (char)c;
    tok.text[1] = 0;
}

int accept(int k)
{
    if (tok.kind == k) {
        next_token();
        return 1;
    }
    return 0;
}

static const char *expected_token_name(int k, char *buf)
{
    switch (k) {
        case TOK_EOF:      return "end of file";
        case TOK_ID:       return "identifier";
        case TOK_NUM:      return "number";
        case TOK_STR:      return "string literal";
        case TOK_CHARLIT:  return "character constant";
        case TOK_INT:      return "int";
        case TOK_CHAR:     return "char";
        case TOK_VOID:     return "void";
        case TOK_UNSIGNED: return "unsigned";
        case TOK_SIGNED:   return "signed";
        case TOK_EXTERN:   return "extern";
        case TOK_STATIC:   return "static";
        case TOK_CONST:    return "const";
        case TOK_IF:       return "if";
        case TOK_ELSE:     return "else";
        case TOK_WHILE:    return "while";
        case TOK_FOR:      return "for";
        case TOK_RETURN:   return "return";
        case TOK_BREAK:    return "break";
        case TOK_CONTINUE: return "continue";
        case TOK_SIZEOF:   return "sizeof";
        case TOK_TYPEDEF:  return "typedef";
        case TOK_STRUCT:   return "struct";
        case TOK_UNION:    return "union";
        case TOK_ENUM:     return "enum";
        case TOK_SWITCH:   return "switch";
        case TOK_CASE:     return "case";
        case TOK_DEFAULT:  return "default";
        case TOK_LONG:     return "long";
        case TOK_FLOAT:    return "float";
        case TOK_BOOL:     return "_Bool";
        case TOK_EQ:       return "==";
        case TOK_NE:       return "!=";
        case TOK_LE:       return "<=";
        case TOK_GE:       return ">=";
        case TOK_ANDAND:   return "&&";
        case TOK_OROR:     return "||";
        case TOK_SHL:      return "<<";
        case TOK_SHR:      return ">>";
        case TOK_INC:      return "++";
        case TOK_DEC:      return "--";
        case TOK_ARROW:    return "->";
        case TOK_ELLIPSIS: return "...";
        default:
            if (k > 0 && k < 128 && isprint((unsigned char)k)) {
                sprintf(buf, "'%c'", k);
                return buf;
            }
            sprintf(buf, "token %d", k);
            return buf;
    }
}

void expect(int k)
{
    if (tok.kind != k) {
        char namebuf[32];
        char msg[80];

        sprintf(msg, "expected %s", expected_token_name(k, namebuf));
        error_here(msg);
        return;
    }
    next_token();
}

