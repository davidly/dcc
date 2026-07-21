#include <stdio.h>
#include <stdint.h>
#include <ctype.h>

/* Parallel coverage for the pihex "block argument" parser and the 16-bit
 * block/chunk boundary arithmetic.  pihex.c itself is a benchmark app that is
 * always run with no arguments, so those argument-parsing and top-of-range
 * wrap paths are exercised here in a dedicated unit test instead of being
 * baked into the benchmark.  Baselined against the host compiler. */

/* Strict decimal parse of a block count/offset expressed in units of 128:
 *   - leading whitespace is skipped;
 *   - a single optional leading '+' is allowed, '-' is rejected;
 *   - at least one digit is required;
 *   - the value is accumulated in a 16-bit unsigned with an explicit bound so
 *     it can never exceed 511 (block 512 is the exclusive end of the range);
 *   - trailing whitespace is skipped and any other trailing text is rejected.
 * Returns 1 and writes *out on success, 0 on rejection. */
static int parse_block_arg(const char *text, uint16_t *out)
{
    const char *p = text;
    uint16_t value;

    while (isspace((unsigned char)*p))
        p++;
    if (*p == '-')
        return 0;
    if (*p == '+')
        p++;
    if (!isdigit((unsigned char)*p))
        return 0;

    value = 0;
    while (isdigit((unsigned char)*p)) {
        uint16_t digit = (uint16_t)(*p - '0');
        if (value > 51 || (value == 51 && digit > 1))
            return 0;
        value = (uint16_t)(value * 10 + digit);
        p++;
    }
    while (isspace((unsigned char)*p))
        p++;
    if (*p != '\0')
        return 0;

    *out = value;
    return 1;
}

/* Visit every digit index in [off128*128, off128*128 + cnt128*128) using
 * 32-index chunks, returning the number of indices visited.  The exclusive
 * end may be 65536 (block 512), one past the largest 16-bit value, so the
 * final chunk (indices 65504..65535) is handled with a do/while that never
 * forms the wrapping bound 65504 + 32 == 65536 == 0.  This mirrors the exact
 * shape pihex needs and asserts it visits each index once with no wrap. */
static uint16_t visit_indices(uint16_t off128, uint16_t cnt128)
{
    const uint16_t chunkSize = 32;
    uint16_t startingOffset = (uint16_t)(off128 * 128u);
    uint16_t countGenerated = (uint16_t)(cnt128 * 128u);
    uint16_t startInChunks = (uint16_t)(startingOffset / chunkSize);
    uint16_t limitInChunks = (uint16_t)(startInChunks + countGenerated / chunkSize);
    uint16_t normalLimitInChunks = (limitInChunks == 2048u) ? 2047u : limitInChunks;
    uint16_t visited = 0;
    uint16_t i;

    for (i = startInChunks; i < normalLimitInChunks; i++) {
        uint16_t start = (uint16_t)(i * chunkSize);
        uint16_t d;
        for (d = start; d < start + chunkSize; d++)
            visited++;
    }

    if (limitInChunks == 2048u) {
        uint16_t d = 65504u;
        do {
            visited++;
        } while (d++ != 65535u);
    }

    return visited;
}

int main(void)
{
    uint16_t v = 0;

    printf("tpihexb:");

    /* Parser: accepted and rejected forms (rejection prints -1). */
    printf(" p0=%d", parse_block_arg("0", &v) ? (int)v : -1);
    printf(" p511=%d", parse_block_arg("511", &v) ? (int)v : -1);
    printf(" p512=%d", parse_block_arg("512", &v) ? (int)v : -1);
    printf(" pplus=%d", parse_block_arg("+42", &v) ? (int)v : -1);
    printf(" pws=%d", parse_block_arg("  12  ", &v) ? (int)v : -1);
    printf(" pneg=%d", parse_block_arg("-1", &v) ? (int)v : -1);
    printf(" pjunk=%d", parse_block_arg("12x", &v) ? (int)v : -1);
    printf(" pempty=%d", parse_block_arg("", &v) ? (int)v : -1);
    printf(" pbig=%d", parse_block_arg("65535", &v) ? (int)v : -1);

    /* Boundary arithmetic: each case must visit exactly count*128 indices. */
    printf(" v0_1=%u", visit_indices(0, 1));
    printf(" v0_4=%u", visit_indices(0, 4));
    printf(" v511_1=%u", visit_indices(511, 1));
    printf(" v1_511=%u", visit_indices(1, 511));
    printf(" v0_511=%u", visit_indices(0, 511));

    printf("\n");
    return 0;
}
