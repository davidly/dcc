#ifndef TMFSTR_H
#define TMFSTR_H

struct MfRecord {
    int count;
    long total;
    char tag;
};

struct MfRecord mf_make_record(int count, long total, char tag);
void mf_adjust_record(struct MfRecord *record, int delta);
int mf_consume_record(struct MfRecord record);

#endif
