/* Regression for a stack parameter whose PHI-planning slot is reused in IY. */
#include <stdio.h>
#include <string.h>

#define TEST_BYTES 32768U
#define LOCAL_BYTES 16384U
static unsigned char buffer[TEST_BYTES];

typedef struct {
    unsigned long write_ms;
    unsigned long read_ms;
    unsigned int chunk;
} timings;

static unsigned int watches;

static unsigned char expected_byte(unsigned int offset)
{
    return (unsigned char)((offset & 255U) ^ (offset >> 8) ^ 0x5AU);
}

/* Keep MEMPERF's calls and error paths without requiring timer hardware. */
static void start_watch(void)
{
    ++watches;
}

static unsigned long elapsed_ms(void)
{
    return watches;
}

static int benchmark_local(unsigned int chunk_bytes, timings *result)
{
    unsigned char *store = buffer;
    unsigned char *io = buffer + LOCAL_BYTES;
    unsigned int i, offset, pass, local_chunk;

    /* The loops reuse a PHI-planning slot in IY. The incoming parameter
     * must still be read from its stack home before either clamp arm. */
    local_chunk = chunk_bytes > LOCAL_BYTES ? LOCAL_BYTES : chunk_bytes;
    for (i = 0; i < LOCAL_BYTES; ++i)
    {
        io[i] = expected_byte(i);
        store[i] = (unsigned char)~expected_byte(i);
    }
    start_watch();
    for (pass = 0; pass < 2; ++pass)
        for (offset = 0; offset < LOCAL_BYTES; offset += local_chunk)
            memcpy(store + offset, io + offset, local_chunk);
    result->write_ms = elapsed_ms();
    if (result->write_ms == 0xFFFFFFFFUL)
    {
        puts("FAIL: local RAM write stopwatch read.");
        return 0;
    }
    for (i = 0; i < LOCAL_BYTES; ++i)
        if (store[i] != expected_byte(i))
        {
            printf("FAIL: local RAM write verification at offset %u\n", i);
            return 0;
        }

    for (i = 0; i < LOCAL_BYTES; ++i)
        io[i] = (unsigned char)~expected_byte(i);
    start_watch();
    for (pass = 0; pass < 2; ++pass)
        for (offset = 0; offset < LOCAL_BYTES; offset += local_chunk)
            memcpy(io + offset, store + offset, local_chunk);
    result->read_ms = elapsed_ms();
    if (result->read_ms == 0xFFFFFFFFUL)
    {
        puts("FAIL: local RAM read stopwatch read.");
        return 0;
    }
    for (i = 0; i < LOCAL_BYTES; ++i)
        if (io[i] != expected_byte(i))
        {
            printf("FAIL: local RAM read verification at offset %u\n", i);
            return 0;
        }
    result->chunk = local_chunk;
    return 1;
}

int main(void)
{
    timings result;
    unsigned int failures = 0;
    if (!benchmark_local(32768U, &result) || result.chunk != 16384U)
        ++failures;
    if (!benchmark_local(256U, &result) || result.chunk != 256U)
        ++failures;
    if (!benchmark_local(16384U, &result) || result.chunk != 16384U)
        ++failures;
    if (!benchmark_local(65535U, &result) || result.chunk != 16384U)
        ++failures;
    if (!benchmark_local(128U, &result) || result.chunk != 128U)
        ++failures;
    printf("ternary argument clamp: %u failures\n", failures);
    return failures != 0;
}
