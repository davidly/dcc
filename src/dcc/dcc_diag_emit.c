/*
 * dcc_diag_emit.c - diagnostics, allocation, and low-level emit primitives.
 *
 * The compiler's "plumbing": fatal()/error_here() error reporting,
 * source_location_at() for #line-aware positions, xmalloc/xstrdup2, label
 * allocation, the emit()/emit_label()/emit_jp_label() assembly-output
 * primitives, and the raw source character readers (peekc/getc_src).
 *
 * MODULE: compiled as its own translation unit; shared declarations are in dcc.h.
 * Source provenance: monolith src/ddc.c lines 495-691.
 */

#include "dcc.h"

void dcc_copy_str(char *dst, size_t dstsz, const char *src)
{
    size_t i;

    if (!dst || dstsz == 0)
        return;
    if (!src)
        src = "";

    i = 0;
    while (i + 1 < dstsz && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

void fatal(const char *msg)
{
    fprintf(stderr, "dcc: fatal: %s\n", msg);
    exit(1);
}

static int dcc_msg_has(const char *msg, const char *needle)
{
    return msg && needle && strstr(msg, needle) != NULL;
}

const char *dcc_diag_code_for_message(const char *msg)
{
    if (dcc_msg_has(msg, "undeclared identifier")) return "DCC-E0201";
    if (dcc_msg_has(msg, "expected \"FILENAME\" or <FILENAME>")) return "DCC-E0301";
    if (dcc_msg_has(msg, "include name too long")) return "DCC-E0302";
    if (dcc_msg_has(msg, "unterminated include name")) return "DCC-E0303";
    if (dcc_msg_has(msg, "cannot open include file")) return "DCC-E0304";
    if (dcc_msg_has(msg, "not a valid preprocessor directive")) return "DCC-E0310";
    if (dcc_msg_has(msg, "unknown preprocessor directive")) return "DCC-E0310";
    if (dcc_msg_has(msg, "too many nested #if")) return "DCC-E0311";
    if (dcc_msg_has(msg, "#error")) return "DCC-E0312";
    if (dcc_msg_has(msg, "#elif without matching #if")) return "DCC-E0313";
    if (dcc_msg_has(msg, "#else without matching #if")) return "DCC-E0314";
    if (dcc_msg_has(msg, "#endif without matching #if")) return "DCC-E0315";
    if (dcc_msg_has(msg, "#else after #else")) return "DCC-E0316";
    if (dcc_msg_has(msg, "#if with no matching #endif")) return "DCC-E0317";
    if (dcc_msg_has(msg, "too many arguments provided to function-like macro invocation")) return "DCC-E0320";
    if (dcc_msg_has(msg, "too few arguments provided to function-like macro invocation")) return "DCC-E0321";
    if (dcc_msg_has(msg, "macro argument too long in function-like macro invocation")) return "DCC-E0322";
    if (dcc_msg_has(msg, "constant integer expression expected")) return "DCC-E0401";
    if (dcc_msg_has(msg, "integer constant expression out of range")) return "DCC-E0401";
    if (dcc_msg_has(msg, "division by zero in constant expression")) return "DCC-E0402";
    if (dcc_msg_has(msg, "expected an expression")) return "DCC-E0403";
    if (dcc_msg_has(msg, "static assertion failed")) return "DCC-E0404";
    if (dcc_msg_has(msg, "static assertion")) return "DCC-E0405";
    if (dcc_msg_has(msg, "expected a field designator")) return "DCC-E0501";
    if (dcc_msg_has(msg, "unknown field initializer designator")) return "DCC-E0502";
    if (dcc_msg_has(msg, "field name expected in offsetof")) return "DCC-E0503";
    if (dcc_msg_has(msg, "unknown field in offsetof")) return "DCC-E0504";
    if (dcc_msg_has(msg, "offsetof needs struct/union type")) return "DCC-E0505";
    if (dcc_msg_has(msg, "__offsetof expected")) return "DCC-E0507";
    if (dcc_msg_has(msg, "nested offsetof field is not struct/union")) return "DCC-E0508";
    if (dcc_msg_has(msg, "unknown struct field")) return "DCC-E0509";
    if (dcc_msg_has(msg, "field name expected")) return "DCC-E0506";
    if (dcc_msg_has(msg, "bitfield type must be int or unsigned int")) return "DCC-E0510";
    if (dcc_msg_has(msg, "invalid bitfield width")) return "DCC-E0511";
    if (dcc_msg_has(msg, "duplicate enum constant")) return "DCC-E0520";
    if (dcc_msg_has(msg, "enum constant name expected")) return "DCC-E0521";
    if (dcc_msg_has(msg, "enumerator value is not representable as 16-bit int")) return "DCC-E0522";
    if (dcc_msg_has(msg, "struct/union name or '{' expected")) return "DCC-E0530";
    if (dcc_msg_has(msg, "type expected")) return "DCC-E0531";
    if (dcc_msg_has(msg, "multiple storage classes in declaration")) return "DCC-E0540";
    if (dcc_msg_has(msg, "variable length arrays are not supported")) return "DCC-E0601";
    if (dcc_msg_has(msg, "variable inner dimensions in variable-length arrays are not supported")) return "DCC-E0601";
    if (dcc_msg_has(msg, "subscripted value is not an array or pointer")) return "DCC-E0602";
    if (dcc_msg_has(msg, "too many array dimensions")) return "DCC-E0603";
    if (dcc_msg_has(msg, "invalid array bound for 16-bit target")) return "DCC-E0604";
    if (dcc_msg_has(msg, "object size exceeds 16-bit address space")) return "DCC-E0605";
    if (dcc_msg_has(msg, "break statement outside loop or switch")) return "DCC-E0701";
    if (dcc_msg_has(msg, "continue statement outside loop")) return "DCC-E0702";
    if (dcc_msg_has(msg, "case label outside switch")) return "DCC-E0703";
    if (dcc_msg_has(msg, "default label outside switch")) return "DCC-E0704";
    if (dcc_msg_has(msg, "duplicate goto label")) return "DCC-E0705";
    if (dcc_msg_has(msg, "undefined goto label")) return "DCC-E0706";
    if (dcc_msg_has(msg, "parameter declaration name expected")) return "DCC-E0801";
    if (dcc_msg_has(msg, "parameter name expected")) return "DCC-E0802";
    if (dcc_msg_has(msg, "old-style parameter declaration does not match parameter list")) return "DCC-E0803";
    if (dcc_msg_has(msg, "redefinition of")) return "DCC-E0804";
    if (dcc_msg_has(msg, "too few arguments to function call")) return "DCC-E0805";
    if (dcc_msg_has(msg, "too many arguments to function call")) return "DCC-E0806";
    if (dcc_msg_has(msg, "identifier expected after & in initializer")) return "DCC-E0901";
    if (dcc_msg_has(msg, "constant initializer expected")) return "DCC-E0902";
    if (dcc_msg_has(msg, "string initializer too long")) return "DCC-E0903";
    if (dcc_msg_has(msg, "too many initializer elements")) return "DCC-E0904";
    if (dcc_msg_has(msg, "struct initializer list expected")) return "DCC-E0905";
    if (dcc_msg_has(msg, "array initializer list expected")) return "DCC-E0906";
    if (dcc_msg_has(msg, "compound literal initializer expected")) return "DCC-E0907";
    if (dcc_msg_has(msg, "numeric constant expected after sign")) return "DCC-E0908";
    if (dcc_msg_has(msg, "negative initializer offset")) return "DCC-E0909";
    if (dcc_msg_has(msg, "initializer designator overlaps address constant")) return "DCC-E0910";
    if (dcc_msg_has(msg, "too many union initializer elements")) return "DCC-E0911";
    if (dcc_msg_has(msg, "float initializer must be constant")) return "DCC-E0912";
    if (dcc_msg_has(msg, "negative array initializer designator")) return "DCC-E0913";
    if (dcc_msg_has(msg, "array initializer designator out of range")) return "DCC-E0916";
    if (dcc_msg_has(msg, "wide string cannot initialize char array")) return "DCC-E0914";
    if (dcc_msg_has(msg, "bitfield initializer must be constant integer")) return "DCC-E0915";
    if (dcc_msg_has(msg, "incompatible integer to pointer assignment")) return "DCC-E0920";
    if (dcc_msg_has(msg, "unsupported sizeof expression")) return "DCC-E1001";
    if (dcc_msg_has(msg, "unsupported")) return "DCC-E1002";
    if (dcc_msg_has(msg, "malformed")) return "DCC-E1003";
    if (dcc_msg_has(msg, "string literal too long")) return "DCC-E1004";
    if (dcc_msg_has(msg, "'(' expected after sizeof in constant expression")) return "DCC-E1005";
    if (dcc_msg_has(msg, "external declaration expected")) return "DCC-E1101";
    if (dcc_msg_has(msg, "expected ';'")) return "DCC-E1102";
    if (dcc_msg_has(msg, "expected '}'")) return "DCC-E1103";
    if (dcc_msg_has(msg, "identifier expected")) return "DCC-E1104";
    if (dcc_msg_has(msg, "expected ')'")) return "DCC-E1105";
    if (dcc_msg_has(msg, "expected ']'")) return "DCC-E1106";
    if (dcc_msg_has(msg, "expected '='")) return "DCC-E1107";
    if (dcc_msg_has(msg, "double is not supported")) return "DCC-E1201";
    if (dcc_msg_has(msg, "long long is not supported")) return "DCC-E1202";
    if (dcc_msg_has(msg, "64-bit integer types are not supported")) return "DCC-E1203";
    return "DCC-E0001";
}

static void dcc_print_source_caret(long ofs)
{
    long start;
    long end;
    long p;
    long caret;

    if (!src || src_len <= 0 || ofs < 0 || ofs > src_len)
        return;

    start = ofs;
    while (start > 0 && src[start - 1] != '\n' && src[start - 1] != '\r')
        start--;
    end = ofs;
    while (end < src_len && src[end] != '\n' && src[end] != '\r')
        end++;
    if (end <= start)
        return;

    fprintf(stderr, "    ");
    for (p = start; p < end; ++p)
        fputc((unsigned char)src[p], stderr);
    fprintf(stderr, "\n    ");
    caret = ofs - start;
    for (p = 0; p < caret; ++p)
        fputc(src[start + p] == '\t' ? '\t' : ' ', stderr);
    fprintf(stderr, "^\n");
}

void init_predefined_macro_texts(void)
{
    time_t now;
    struct tm *tmv;
    static const char *mons[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    now = time(NULL);
    tmv = localtime(&now);
    if (tmv) {
        sprintf(predefined_date_text, "%s %2d %04d",
                mons[tmv->tm_mon], tmv->tm_mday, tmv->tm_year + 1900);
        sprintf(predefined_time_text, "%02d:%02d:%02d",
                tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
    } else {
        strcpy(predefined_date_text, "Jan  1 1970");
        strcpy(predefined_time_text, "00:00:00");
    }
}

/* source_location_at is called once per token (next_token, dcc_preproc.c),
 * so a #line-directive scan restarting from byte 0 of the source buffer on
 * every call is O(tokens * average position) - quadratic in file size, and
 * the dominant cost of compiling a large source (profiled: >95% of dcc's
 * own runtime on tests/cobint.c, the largest generated .mac in the suite).
 *
 * Fix: precompute a table with one entry per source line (offset -> the
 * effective line number/filename after applying any #line directive up to
 * and including that line), built by a single linear scan, and answer each
 * query with a binary search over it - O(nlines) once plus O(log nlines)
 * per call, instead of O(ofs) every call.
 *
 * `src` isn't static for the whole compile, though: replace_source_range
 * (dcc_preproc.c) rewrites it in place for every macro expansion, and
 * dcc_global_scan.c's whole-file pre-pass saves/restores it around its own
 * scan. A naive "cache the last position and resume forward" scheme was
 * tried first and measured almost no improvement, because that isn't
 * occasional - the compiler's speculative-parse machinery (narrowing,
 * inlining, register allocation, the frame-sizing pre-pass) constantly
 * saves lexer state and re-lexes from an earlier position, so >90% of
 * calls turned out to be "rewinds" that a forward-only cache can't help.
 * A table keyed to the buffer's actual content order doesn't care what
 * order it's queried in, so it isn't defeated by that access pattern - it
 * only needs to know when to rebuild. g_src_generation (bumped at every
 * one of those `src` reassignments) is the invalidation signal: if it
 * doesn't match the generation the table was built for, rebuild before
 * answering. */
struct SrcLineEntry {
    long ofs;
    int line;
    char file[256];
};

static struct SrcLineEntry *g_srcline_table;
static long g_srcline_count;
static long g_srcline_built_for_generation = -1;

static void build_srcline_table(void)
{
    long p, line_start, line_end;
    long i;
    int line;
    char curfile[256];

    free(g_srcline_table);
    g_srcline_count = 1;
    for (i = 0; i < src_len; ++i)
        if (src[i] == '\n')
            g_srcline_count++;
    g_srcline_table = (struct SrcLineEntry *)xmalloc((size_t)g_srcline_count * sizeof(struct SrcLineEntry));

    strncpy(curfile, input_name ? input_name : "<input>", sizeof(curfile) - 1);
    curfile[sizeof(curfile) - 1] = 0;
    line = 1;
    g_srcline_count = 0;

    p = 0;
    for (;;) {
        int j;

        line_start = p;
        while (p < src_len && src[p] != '\n')
            p++;
        line_end = p;

        j = (int)line_start;
        while (j < line_end && (src[j] == ' ' || src[j] == '\t'))
            j++;

        if (j + 5 <= line_end && src[j] == '#' &&
            src[j + 1] == 'l' && src[j + 2] == 'i' &&
            src[j + 3] == 'n' && src[j + 4] == 'e' &&
            (j + 5 == line_end || src[j + 5] == ' ' || src[j + 5] == '\t')) {
            int n, qi;

            j += 5;
            while (j < line_end && (src[j] == ' ' || src[j] == '\t'))
                j++;
            n = 0;
            while (j < line_end && src[j] >= '0' && src[j] <= '9') {
                n = n * 10 + src[j] - '0';
                j++;
            }
            if (n > 0)
                line = n - 1;

            while (j < line_end && (src[j] == ' ' || src[j] == '\t'))
                j++;
            if (j < line_end && src[j] == '"') {
                j++;
                qi = 0;
                while (j < line_end && src[j] != '"' && qi < (int)sizeof(curfile) - 1)
                    curfile[qi++] = src[j++];
                curfile[qi] = 0;
            }
        }

        g_srcline_table[g_srcline_count].ofs = line_start;
        g_srcline_table[g_srcline_count].line = line;
        strcpy(g_srcline_table[g_srcline_count].file, curfile);
        g_srcline_count++;

        if (p >= src_len)
            break;
        line++;
        p++; /* skip the newline */
    }

    g_srcline_built_for_generation = g_src_generation;
}

void source_location_at(long ofs, char *filebuf, int filebufsz, int *linep)
{
    long lo, hi, mid, best;

    if (ofs < 0)
        ofs = 0;
    if (ofs > src_len)
        ofs = src_len;

    if (g_srcline_built_for_generation != g_src_generation)
        build_srcline_table();

    lo = 0;
    hi = g_srcline_count - 1;
    best = 0;
    while (lo <= hi) {
        mid = (lo + hi) / 2;
        if (g_srcline_table[mid].ofs <= ofs) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }

    linep[0] = g_srcline_table[best].line;
    if (filebufsz > 0) {
        strncpy(filebuf, g_srcline_table[best].file, (size_t)filebufsz - 1);
        filebuf[filebufsz - 1] = 0;
    }
}

void dcc_error_at(const char *file, int line, long ofs, const char *msg, const char *near_text)
{
    const char *fn;
    const char *code;

    /* Counted unconditionally, even while suppressed below - this is the
     * only signal try_speculative_bc_regalloc_function_body (dcc_func.c) has
     * that a genuine error occurred during a speculatively-generated,
     * possibly-discarded function body: asm_suppress_depth prevents this
     * function from printing or bumping the real `errors` counter (so a
     * discarded attempt's error isn't shown to the user prematurely, before
     * the real fallback pass re-encounters and correctly reports it exactly
     * once), but silently committing a speculative attempt that hit a real
     * error would ship broken code with NO diagnostic at all. Comparing this
     * counter before/after a suppressed attempt lets that caller force a
     * decline whenever it increased, regardless of what the regalloc-
     * specific safety checks concluded. */
    g_diag_error_count++;

    /* asm_suppress_depth marks source text being parsed for its structural
     * side effects only (dead code kept in sync for frame layout, a real
     * inline-asm block, or - see record_inline_function_if_simple/
     * record_narrow_return_expr_if_simple in dcc_func.c - a throwaway
     * speculative re-parse of a function body before its own locals are
     * declared for this pass) - never for a diagnostic a user should see, so
     * a type/syntax complaint raised while it's set is a false positive of
     * the speculative context, not a real error in the program. */
    if (asm_suppress_depth > 0)
        return;

    fn = file && file[0] ? file : (input_name ? input_name : "<input>");
    code = dcc_diag_code_for_message(msg);
    if (near_text && near_text[0])
        fprintf(stderr, "%s:%d: error %s: %s near '%s'\n", fn, line, code, msg, near_text);
    else
        fprintf(stderr, "%s:%d: error %s: %s\n", fn, line, code, msg);
    dcc_print_source_caret(ofs);
    errors++;
    if (errors > 40) fatal("too many errors");
}

void error_here(const char *msg)
{
    const char *fn;

    fn = tok.file[0] ? tok.file : (input_name ? input_name : "<input>");
    dcc_error_at(fn, tok_line, tok_start_pos, msg, tok.text);
}

int warnings = 0;

/* Non-fatal diagnostic: doesn't touch `errors` or exit(), so a warning never
 * changes whether the compile succeeds. Suppressed under asm_suppress_depth
 * for the same reason dcc_error_at is (see its comment) - a speculative,
 * possibly-discarded codegen attempt (narrowing, inline-candidate scanning,
 * the no-ix/bc-regalloc speculative function-body attempts in dcc_func.c)
 * must not print a diagnostic about code that might never actually ship;
 * only the real, kept compilation of a given function runs outside any
 * suppressed region.
 *
 * Also suppressed once a real error has already been reported anywhere in
 * the compile: a missing/malformed return expression already produces its
 * own error at the return statement itself (e.g. "unsupported return
 * expression"), and the control-flow analysis this feeds (does the
 * function's body provably return a value on every path) is only
 * meaningful for a function that actually parsed - once something upstream
 * is already broken, "control reaches end of non-void function" is
 * cascaded noise on top of a diagnostic the user already has, not a
 * separate real finding. */
void warn_at(const char *file, int line, const char *msg)
{
    const char *fn;

    if (asm_suppress_depth > 0 || errors > 0)
        return;
    fn = file && file[0] ? file : (input_name ? input_name : "<input>");
    fprintf(stderr, "%s:%d: warning: %s\n", fn, line, msg);
    warnings++;
}

void *xmalloc(size_t n)
{
    void *p;
    p = malloc(n);
    if (!p) fatal("out of memory");
    return p;
}

char *xstrdup2(const char *s)
{
    char *p;
    p = (char *)xmalloc(strlen(s) + 1);
    strcpy(p, s);
    return p;
}

int new_label(void)
{
    return ++label_id;
}

void flush_pending_asm(void)
{
    /* Never flush into a suppressed/speculative context (asm_suppress_depth>0,
     * e.g. the inline-candidate double-scan in dcc_func.c, or a dead-code
     * replay in ast_gen_stmt's AST_COMPOUND case): that output either goes to
     * a throwaway buffer or is discarded outright if the speculative attempt
     * is abandoned, silently losing content that was legitimately buffered
     * earlier during a real (non-suppressed) pass. Leave pending_asm_len
     * untouched so the content survives to the next real flush point instead
     * of being written once into a doomed buffer and considered "done". */
    if (asm_suppress_depth > 0)
        return;
    if (pending_asm_len > 0 && outf) {
        fwrite(pending_asm_buf, 1, (size_t)pending_asm_len, outf);
        pending_asm_len = 0;
    }
}

void emit(const char *s);

void emit_ld_de_const(long v)
{
    if (!scan_mode)
        fprintf(outf, "\tld de,%ld\n", v & 0xffffL);
}

void emit_add_const_to_hl(long v)
{
    v &= 0xffffL;
    if (v == 0)
        return;
    emit_ld_de_const(v);
    emit("\tadd hl,de\n");
}

void emit(const char *s)
{
    if (!scan_mode)
        fputs(s, outf);
}

void emit_label(int n)
{
    if (!scan_mode)
        fprintf(outf, "L%d:\n", n);
}

void emit_jp_label(const char *op, int n)
{
    if (!scan_mode)
        fprintf(outf, "\t%s L%d\n", op, n);
}

int is_ident_start(int c)
{
    return isalpha((unsigned char)c) || c == '_';
}

int is_ident_char(int c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static int trigraph_xlat(int third)
{
    switch (third) {
        case '=':  return '#';
        case '/':  return '\\';
        case '\'': return '^';
        case '(':  return '[';
        case ')':  return ']';
        case '!':  return '|';
        case '<':  return '{';
        case '>':  return '}';
        case '-':  return '~';
        default:   return 0;
    }
}

int peekc(void)
{
    int t;
    if (posi >= src_len) return 0;
    if ((unsigned char)src[posi] == '?' && posi + 2 < src_len &&
            (unsigned char)src[posi + 1] == '?' &&
            (t = trigraph_xlat((unsigned char)src[posi + 2])) != 0)
        return t;
    return (unsigned char)src[posi];
}

int getc_src(void)
{
    int c, t;
    if (posi >= src_len) return 0;
    c = (unsigned char)src[posi++];
    if (c == '\n') { line_no++; return c; }
    if (c == '?' && posi + 1 < src_len && (unsigned char)src[posi] == '?' &&
            (t = trigraph_xlat((unsigned char)src[posi + 1])) != 0) {
        posi += 2;
        return t;
    }
    return c;
}

int define_number_value(const char *name, long *out, int depth);
void strip_macro_replacement_comments(char *s);

