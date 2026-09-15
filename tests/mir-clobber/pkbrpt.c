#include <stdio.h>

#ifdef PKBRPT_RENAMED_PACK
#define pkbrpt_pack pkbrpt_fold
#endif

#ifdef PKBRPT_VOLATILE_BUFFER
#define PKBRPT_VOLATILE volatile
#else
#define PKBRPT_VOLATILE
#endif

#ifdef PKBRPT_WORD_BUFFER
typedef unsigned int pkbrpt_element;
#else
typedef unsigned char pkbrpt_element;
#endif

long pkbrpt_pack(PKBRPT_VOLATILE pkbrpt_element *bytes)
{
    return ((long)bytes[0] << 24) |
           ((long)bytes[1] << 16) |
           ((long)bytes[2] << 8) |
           (long)bytes[3];
}

int main(void)
{
#ifdef PKBRPT_VLA_BUFFER
    int count = 4;
    pkbrpt_element bytes[count];
#else
    PKBRPT_VOLATILE pkbrpt_element bytes[4];
#endif

    bytes[0] = 0x12;
    bytes[1] = 0xa5;
    bytes[2] = 0x5a;
    bytes[3] = 0xe7;
    printf("packed byte report value=%ld\n", pkbrpt_pack(bytes));
    return 0;
}
