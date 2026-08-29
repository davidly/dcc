#include "tmfstr.h"

struct MfRecord mf_make_record(int count, long total, char tag)
{
    struct MfRecord record;

    record.count = count;
    record.total = total;
    record.tag = tag;
    return record;
}

void mf_adjust_record(struct MfRecord *record, int delta)
{
    record->count += delta;
    record->total += delta;
    record->tag++;
}
