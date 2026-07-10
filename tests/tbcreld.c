/*
 * tbcreld.c - regression test for BC's opportunistic reload-on-demand
 * (regalloc_buffer_finalize, dcc_func.c): pack_stamp's pointer parameter
 * `b` qualifies for BC residency (>=2 references, read-only, no address
 * taken), but the 32-bit shift/OR long arithmetic that combines its four
 * indexed bytes parks a scratch value via push bc/pop bc in between uses -
 * exactly tests/tbig.c's get_stamp shape. Since `b` is read-only, its
 * original incoming parameter slot never changes, so instead of declining
 * BC residency outright the moment anything else touches bc, a fresh
 * reload from that always-correct slot is inserted right before the next
 * BC use - this test exists to catch a regression in that specific
 * mechanism (as opposed to tests/tbcreg.c's simpler declined-vs-kept
 * cases, which never exercise reload at all).
 */
#include <stdio.h>

long pack_stamp(char *b)
{
    long v;

    v  = ((long)b[0] & 0xffL) << 24;
    v |= ((long)b[1] & 0xffL) << 16;
    v |= ((long)b[2] & 0xffL) << 8;
    v |= (long)b[3] & 0xffL;
    return v;
}

int main(void)
{
    char buf[4];

    buf[0] = 0x12;
    buf[1] = 0x34;
    buf[2] = 0x56;
    buf[3] = 0x78;
    printf("stamp=%ld\n", pack_stamp(buf));
    return 0;
}
