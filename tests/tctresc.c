/*
 * tctresc.c - regression test for pass_byte_for_counter_to_reg_e tolerating
 * an early-return inside its loop (an internal branch), the same shape
 * tests/tbig.c's check_record has. Guards escape_path_reaches_epilogue_
 * safely/loop_body_escapes_safe_for_offset: the registerized counter's
 * frame slot is only written back once, at the loop's own normal exit,
 * so every early-exit path must be proven to never read that slot while
 * it's stale before this pass may fire at all.
 */
#include <stdio.h>

int find_mismatch(char *p, int base)
{
    int i;

    for (i = 0; i < 32; i++)
        if (p[i] != (char)(base + i))
            return i;
    return -1;
}

int main(void)
{
    char buf[32];
    int i;

    for (i = 0; i < 32; i++)
        buf[i] = (char)(5 + i);
    printf("clean=%d\n", find_mismatch(buf, 5));

    buf[17] = 0;
    printf("dirty=%d\n", find_mismatch(buf, 5));

    return 0;
}
