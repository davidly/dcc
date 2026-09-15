#define main direnum_embedded_control
#include "direnum3.c"
#undef main

static int oracle_step(int checksum, int value)
{
    return ((checksum % 800) * 33 + value) % 30000;
}

int main(void)
{
    int ok;
    int checksum = 17;

    ok = enumerate("Q7??????.D??");
    checksum = oracle_step(checksum, ok);
    checksum = oracle_step(checksum, initialize_calls);
    checksum = oracle_step(checksum, find_first_calls);
    checksum = oracle_step(checksum, find_next_calls);
    checksum = oracle_step(checksum, size_bdos_calls);
    checksum = oracle_step(checksum, duplicate_calls);
    checksum = oracle_step(checksum, sort_calls);
    checksum = oracle_step(checksum, search_calls);
    checksum = oracle_step(checksum, print_calls);
    checksum = oracle_step(checksum, size_calls);
    checksum = oracle_step(checksum, free_calls);
    printf(
        "directory enumeration checks=%d checksum=%d\n",
        ok, checksum);
    if (!ok || checksum != 4524)
        return 1;
    return 0;
}
