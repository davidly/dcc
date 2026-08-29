#include "tmfstr.h"

int mf_consume_record(struct MfRecord record)
{
    return record.count + (int)(record.total - 70000L) + record.tag;
}
