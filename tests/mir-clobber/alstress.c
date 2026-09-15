#include <stdio.h>
#include <stdlib.h>

static unsigned int fill_calls;
static unsigned int check_calls;
static unsigned int zero_check_calls;
static unsigned int slot_checksum;

#ifdef ALLOCSTRESS_ALTERNATE_PATTERN
#define STRESS_OFFSET 12
#else
#define STRESS_OFFSET 11
#endif

#ifdef ALLOCSTRESS_VOLATILE_SLOTS
static unsigned char * volatile slots[24];
static volatile unsigned int sizes[24];
#else
static unsigned char *slots[24];
static unsigned int sizes[24];
#endif
static unsigned int seed;

static void fail(const char *message)
{
    printf("FAIL %s\n", message);
    exit(1);
}

static unsigned int rnd(void)
{
    seed = (unsigned int)(seed * 25173U + 13849U);
    return seed;
}

static unsigned char patt(int slot, unsigned int offset)
{
    return (unsigned char)((slot * 37 + offset * 13 + 91) & 255U);
}

static void fill(unsigned char *pointer, unsigned int size, int slot)
{
    unsigned int index;

    ++fill_calls;
    slot_checksum = (unsigned int)(slot_checksum + (unsigned int)slot);
    for (index = 0; index < size; ++index)
        pointer[index] = patt(slot, index);
}

static void check(unsigned char *pointer, unsigned int size, int slot,
                  const char *message)
{
    unsigned int index;

    ++check_calls;
    slot_checksum = (unsigned int)(slot_checksum + (unsigned int)slot);
    if (pointer == 0)
        fail("null check pointer");
    for (index = 0; index < size; ++index) {
        if (pointer[index] != patt(slot, index))
            fail(message);
    }
}

static void zcheck(unsigned char *pointer, unsigned int size,
                   const char *message)
{
    unsigned int index;

    ++zero_check_calls;
    if (pointer == 0)
        fail("null zero pointer");
    for (index = 0; index < size; ++index) {
        if (pointer[index] != 0)
            fail(message);
    }
}

#ifdef ALLOCSTRESS_RENAMED
#define allocator_stress_fixture renamed_allocator_stress_fixture
#endif

static void allocator_stress_fixture(void)
{
    int i;
    int idx;
    int op;
    unsigned int n;
    unsigned int old;
    unsigned int keep;
    unsigned char *p;

    seed = 0xACE1U;
    for (i = 0; i < 24; i++) {
        slots[i] = 0;
        sizes[i] = 0;
    }

    for (i = 0; i < 420; i++) {
        idx = (int)(rnd() % 24U);
        if (slots[idx] != 0) {
            check(slots[idx], sizes[idx], idx + STRESS_OFFSET,
                  "stress contents changed");
            op = (int)(rnd() % 4U);
            if (op == 0) {
                old = sizes[idx];
                n = (rnd() % 220U) + 1U;
                p = (unsigned char *)realloc(slots[idx], n);
                if (p == 0)
                    fail("stress realloc returned null");
                keep = old < n ? old : n;
                check(p, keep, idx + STRESS_OFFSET,
                      "stress realloc contents changed");
                slots[idx] = p;
                sizes[idx] = n;
                fill(slots[idx], sizes[idx], idx + STRESS_OFFSET);
            }
            else {
                free(slots[idx]);
                slots[idx] = 0;
                sizes[idx] = 0;
            }
        }
        else {
            n = (rnd() % 220U) + 1U;
            if ((rnd() & 1U) != 0) {
                p = (unsigned char *)calloc(n, 1U);
                if (p == 0)
                    fail("stress calloc returned null");
                zcheck(p, n, "stress calloc not zero");
            }
            else {
                p = (unsigned char *)malloc(n);
                if (p == 0)
                    fail("stress malloc returned null");
            }
            slots[idx] = p;
            sizes[idx] = n;
            fill(slots[idx], sizes[idx], idx + STRESS_OFFSET);
        }
    }

    for (i = 0; i < 24; i += 2) {
        if (slots[i] != 0) {
            check(slots[i], sizes[i], i + STRESS_OFFSET,
                  "stress even final changed");
            free(slots[i]);
            slots[i] = 0;
        }
    }
    for (i = 1; i < 24; i += 2) {
        if (slots[i] != 0) {
            check(slots[i], sizes[i], i + STRESS_OFFSET,
                  "stress odd final changed");
            free(slots[i]);
            slots[i] = 0;
        }
    }

    p = (unsigned char *)malloc(12000U);
    if (p == 0)
        fail("stress final large malloc failed");
    fill(p, 12000U, 35);
    check(p, 12000U, 35, "stress final large changed");
    free(p);
}

static unsigned int oracle_rnd(unsigned int *state)
{
    *state = (unsigned int)(*state * 25173U + 13849U);
    return *state;
}

static int verify_oracle(void)
{
    unsigned char occupied[24];
    unsigned int state;
    unsigned int expected_fill;
    unsigned int expected_check;
    unsigned int expected_zero;
    unsigned int expected_checksum;
    int index;
    int iteration;
    int operation;

    for (index = 0; index < 24; ++index)
        occupied[index] = 0;
    state = 0xACE1U;
    expected_fill = 1;
    expected_check = 1;
    expected_zero = 0;
    expected_checksum = 70;

    for (iteration = 0; iteration < 420; ++iteration) {
        index = (int)(oracle_rnd(&state) % 24U);
        if (occupied[index]) {
            ++expected_check;
            expected_checksum = (unsigned int)(
                expected_checksum + index + STRESS_OFFSET);
            operation = (int)(oracle_rnd(&state) % 4U);
            if (operation == 0) {
                (void)oracle_rnd(&state);
                ++expected_check;
                ++expected_fill;
                expected_checksum = (unsigned int)(
                    expected_checksum +
                    2 * (index + STRESS_OFFSET));
            }
            else {
                occupied[index] = 0;
            }
        }
        else {
            (void)oracle_rnd(&state);
            if ((oracle_rnd(&state) & 1U) != 0) {
                ++expected_zero;
            }
            occupied[index] = 1;
            ++expected_fill;
            expected_checksum = (unsigned int)(
                expected_checksum + index + STRESS_OFFSET);
        }
    }
    for (index = 0; index < 24; ++index) {
        if (occupied[index]) {
            ++expected_check;
            expected_checksum = (unsigned int)(
                expected_checksum + index + STRESS_OFFSET);
        }
    }

    return seed == state &&
           fill_calls == expected_fill &&
           check_calls == expected_check &&
           zero_check_calls == expected_zero &&
           slot_checksum == expected_checksum;
}

int main(void)
{
    allocator_stress_fixture();
    if (!verify_oracle()) {
        printf("allocator stress oracle failed\n");
        return 1;
    }
    printf("allocator stress failures=0 helpers=%u,%u,%u seed=%u checksum=%u\n",
           fill_calls, check_calls, zero_check_calls, seed, slot_checksum);
    return 0;
}
