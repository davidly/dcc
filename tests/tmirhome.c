/*
 * tmirhome.c - homed MIR emitter regressions.
 *
 * Small functions keep their values in registers. Each case here once
 * produced wrong code or an unlinkable program from that emitter.
 */
#include <stdio.h>
#include <math.h>

struct Record {
    int fields[9];
};

static struct Record source_records[4];
static struct Record target_records[4];
static int failures;

static void check(const char *name, int ok)
{
    if (!ok) {
        printf("FAIL %s\n", name);
        ++failures;
    }
}

/* Both addresses are computed values; the source lands in HL, where the
 * destination address used to be materialized over it. */
static void copy_indexed(int to, int from)
{
    target_records[to] = source_records[from];
}

static int copy_indexed_and_read(int to, int from, int field)
{
    target_records[to] = source_records[from];
    return target_records[to].fields[field];
}

/* A 32-bit load from an external object must not use symbol+2, which the
 * linker cannot resolve. */
static float load_external_float(void)
{
    return NAN;
}

static int float_is_nan(float value)
{
    return value != value;
}

static void check_aggregate_copies(void)
{
    int record;
    int field;

    for (record = 0; record < 4; ++record)
        for (field = 0; field < 9; ++field)
            source_records[record].fields[field] =
                record * 100 + field * 3 + 1;
    copy_indexed(1, 2);
    check("indexed copy first field", target_records[1].fields[0] == 201);
    check("indexed copy last field", target_records[1].fields[8] == 225);
    check("indexed copy leaves neighbour",
          target_records[0].fields[0] == 0 &&
          target_records[2].fields[0] == 0);
    check("indexed copy returns field",
          copy_indexed_and_read(3, 1, 4) == 113);
    check("indexed copy keeps source",
          source_records[1].fields[4] == 113 &&
          source_records[2].fields[8] == 225);
}

static void check_external_loads(void)
{
    float value = load_external_float();

    check("external float is nan", float_is_nan(value));
    check("ordinary float is not nan", !float_is_nan(1.5f));
}

int main(void)
{
    check_aggregate_copies();
    check_external_loads();
    if (failures == 0)
        printf("tmirhome passed with great success\n");
    return failures != 0;
}
