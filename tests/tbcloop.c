/*
 * tbcloop.c - regression for two bugs in the speculative BC register
 * candidate rewrite pass (regalloc_buffer_finalize, dcc_func.c), both found
 * via tests/tlongidx.c hanging/producing garbage once its sole pointer
 * parameter qualified for BC residency (find_bc_regalloc_candidate):
 *
 * 1. A `long`-typed loop index used as a postfix-incremented array
 *    subscript (`in[i++]`) makes gen_post_update_from_addr save an address
 *    in BC as scratch and restore it with "ld h,b"/"ld l,c" - textually the
 *    same two instructions (in the opposite order) as emit_load_sym_value_
 *    direct's REG_BC read pair ("ld l,c"/"ld h,b"). The rewrite pass
 *    conflated the two, splicing a spurious parameter reload into the
 *    middle of the address restore.
 *
 * 2. Even after fixing that, the rewrite pass's bc-trust tracking is a
 *    single linear text scan with no notion of control flow: a loop header
 *    label reached by both fall-through and a backward jump is only
 *    visited once in file order, so a reload correctly inserted before the
 *    loop's first (textual) encounter of a value-read did not carry over to
 *    the second and later runtime iterations reaching that same label via
 *    the back-edge - each of them then reused the wrong operands.
 *
 * Both count_long_index and copy_long_index below need several loop
 * iterations (not just one or two) to actually exercise the back-edge
 * repeatedly, matching what caught bug 2 in tests/tlongidx.c.
 */
#include <stdio.h>
#include <string.h>

static char srcbuf[64];
static char outbuf[64];

static long count_long_index(const char *in)
{
    long i;
    int c;

    i = 0;
    while (in[i]) {
        c = (unsigned char)in[i++];
        (void)c;
    }
    return i;
}

static void copy_long_index(const char *in)
{
    long i;
    long o;
    int c;

    i = 0;
    o = 0;
    while (in[i]) {
        c = (unsigned char)in[i++];
        outbuf[o++] = (char)c;
    }
    outbuf[o] = 0;
}

static int checks = 0, failures = 0;

static void ck_long(long got, long want, const char *label)
{
    checks++;
    if (got != want) {
        failures++;
        printf("FAIL %s: got %ld want %ld\n", label, got, want);
    }
}

static void ck_str(const char *got, const char *want, const char *label)
{
    checks++;
    if (strcmp(got, want) != 0) {
        failures++;
        printf("FAIL %s: got '%s' want '%s'\n", label, got, want);
    }
}

int main(void)
{
    strcpy(srcbuf, "the quick brown fox jumps over the lazy dog");
    ck_long(count_long_index(srcbuf), (long)strlen(srcbuf), "count1");

    copy_long_index(srcbuf);
    ck_str(outbuf, srcbuf, "copy1");

    /* A second, differently-sized call on the same functions - guards
     * against a fix that only happens to work for one specific length. */
    strcpy(srcbuf, "abc");
    ck_long(count_long_index(srcbuf), 3, "count2");
    copy_long_index(srcbuf);
    ck_str(outbuf, "abc", "copy2");

    printf("checks=%d failures=%d\n", checks, failures);
    printf("RESULT: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures ? 1 : 0;
}
