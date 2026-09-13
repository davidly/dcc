#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define COUNT_OF(x) (sizeof(x) / sizeof((x)[0]))

#pragma pack(push, 1)
struct PackedRecord
{
    uint8_t ui8;
#ifdef PR20_INTERIOR_PADDING
    uint8_t padding;
#endif
    uint16_t ui16;
    uint32_t ui32;
    int8_t i8;
    int16_t i16;
    int32_t i32;
};

#ifdef PR20_VOLATILE_RECORDS
#define PR20_RECORD_QUAL volatile
#else
#define PR20_RECORD_QUAL
#endif

static struct PackedRecord before_records[10];
PR20_RECORD_QUAL struct PackedRecord records[20];
static struct PackedRecord after_records[10];
static int failures;
int dump_arguments_ok;

static void check_record(
    const PR20_RECORD_QUAL struct PackedRecord *record, unsigned int index)
{
    if (record->ui8 != (uint8_t)index ||
        record->ui16 != (uint16_t)index * 2 ||
        record->ui32 != (uint32_t)index * 4 ||
        record->i8 != -(int8_t)index ||
        record->i16 != -(int16_t)index * 2 ||
        record->i32 != -(int32_t)index * 4)
        ++failures;
}

void VerifyBinaryData(void)
{
    uint32_t hash = 2166136261UL;
    size_t index;

    if (!dump_arguments_ok)
        ++failures;
    for (index = 0; index < COUNT_OF(before_records); ++index) {
        const uint8_t *bytes = (const uint8_t *)&before_records[index];
        size_t byte;

        for (byte = 0; byte < sizeof(before_records[index]); ++byte) {
            if (bytes[byte] != 0)
                ++failures;
            hash = (hash ^ bytes[byte]) * 16777619UL;
        }
    }
    for (index = 0; index < COUNT_OF(records); ++index) {
        const PR20_RECORD_QUAL uint8_t *bytes =
            (const PR20_RECORD_QUAL uint8_t *)&records[index];
        size_t byte;

        check_record(&records[index], (unsigned int)index);
        for (byte = 0; byte < sizeof(records[index]); ++byte)
            hash = (hash ^ bytes[byte]) * 16777619UL;
    }
    for (index = 0; index < COUNT_OF(after_records); ++index) {
        const uint8_t *bytes = (const uint8_t *)&after_records[index];
        size_t byte;

        for (byte = 0; byte < sizeof(after_records[index]); ++byte) {
            if (bytes[byte] != 0)
                ++failures;
            hash = (hash ^ bytes[byte]) * 16777619UL;
        }
    }
    printf(
        "PR6 oracle failures=%d hash=%lu "
        "first=%u,%u,%lu,%d,%d,%ld "
        "last=%u,%u,%lu,%d,%d,%ld "
        "guards=%u,%u,%u,%u\n",
        failures, (unsigned long)hash,
        records[0].ui8, records[0].ui16,
        (unsigned long)records[0].ui32,
        records[0].i8, records[0].i16, (long)records[0].i32,
        records[19].ui8, records[19].ui16,
        (unsigned long)records[19].ui32,
        records[19].i8, records[19].i16, (long)records[19].i32,
        ((uint8_t *)before_records)[0],
        ((uint8_t *)before_records)[sizeof(before_records) - 1],
        ((uint8_t *)after_records)[0],
        ((uint8_t *)after_records)[sizeof(after_records) - 1]);
}

#ifdef PR6_DUMP_FASTCALL
extern void __fastcall ShowBinaryData(
    uint8_t *data, size_t length, size_t indent);

#asm
        public  _ShowBinaryData
_ShowBinaryData:
        push    hl
        ld      a,b
        or      a
        jp      nz,pr6_bad_args
        ld      a,c
        cp      4
        jp      nz,pr6_bad_args
        ld      a,d
        cp      1
        jp      nz,pr6_bad_args
        ld      a,e
        cp      24
        jp      nz,pr6_bad_args
        pop     hl
        ld      de,_records
        or      a
        sbc     hl,de
        jp      nz,pr6_bad
        ld      hl,1
        ld      (_dump_arguments_ok),hl
        jp      _VerifyBinaryData
pr6_bad_args:
        pop     hl
pr6_bad:
        ld      hl,0
        ld      (_dump_arguments_ok),hl
        jp      _VerifyBinaryData
#endasm
#else
void ShowBinaryData(uint8_t *data, size_t length, size_t indent)
{
    dump_arguments_ok =
        data == (uint8_t *)records &&
        length == sizeof(records) &&
        indent == 4;
    VerifyBinaryData();
}
#endif

void test_many()
{
    memset(before_records, 0, sizeof(before_records));
    memset(after_records, 0, sizeof(after_records));

    for (size_t i = 0; i < COUNT_OF(records); i++)
    {
        PR20_RECORD_QUAL struct PackedRecord *m = &records[i];
        m->ui8 = (uint8_t)i;
        m->ui16 = (uint16_t)i * 2;
        m->ui32 = (uint32_t)i * 4;
        m->i8 = -(int8_t)i;
        m->i16 = -(int16_t)i * 2;
        m->i32 = -(int32_t)i * 4;
    }

    memset(before_records, 0, sizeof(before_records));
    memset(after_records, 0, sizeof(after_records));

    for (size_t i = 0; i < COUNT_OF(records); i++)
    {
        PR20_RECORD_QUAL struct PackedRecord *m = &records[i];

        if (m->ui8 != i)
            printf("error: i %u, ui8 is %lu, not %lu\n",
                   (int)i, (uint32_t)m->ui8, (uint32_t)i);
        if (m->ui16 != (uint16_t)i * 2)
            printf("error: i %u, ui16 is %lu, not %lu\n",
                   (int)i, (uint32_t)m->ui16, (uint32_t)i * 2);
        if (m->ui32 != (uint32_t)i * 4)
            printf("error: i %u, ui32 is %lu, not %lu\n",
                   (int)i, (uint32_t)m->ui32, (uint32_t)i * 4);

        if (m->i8 != -(int8_t)i)
            printf("error: i %u, i8 is %ld, not %ld\n",
                   (int)i, (int32_t)m->i8, -(int32_t)i);
        if (m->i16 != -(int16_t)i * 2)
            printf("error: i %u, i16 is %ld, not %ld\n",
                   (int)i, (int32_t)m->ui16, -(int32_t)i * 2);
        if (m->i32 != -(int32_t)i * 4)
            printf("error: i %u, i32 is %ld, not %ld\n",
                   (int)i, (int32_t)m->ui32, -(int32_t)i * 4);
    }

    ShowBinaryData((uint8_t *)records, sizeof(records), 4);
}

int main(void)
{
    memset(before_records, 0xa5, sizeof(before_records));
    memset(records, 0x3c, sizeof(records));
    memset(after_records, 0x5a, sizeof(after_records));
    test_many();
    return failures != 0;
}
