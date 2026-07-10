/* validates some printf behaviors */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>

// most of this fails with dccrtl, but it's good to know where things stand.

#if 0
void cppreference() // from https://en.cppreference.com/w/c/io/fprintf
{
    const char* s = "Hello";
    printf("Strings:\n"); // same as puts("Strings");
    printf(" padding:\n");
    printf("\t[%10s]\n", s);
    printf("\t[%-10s]\n", s);
    printf("\t[%*s]\n", 10, s);
    printf(" truncating:\n");
    printf("\t%.4s\n", s);
    printf("\t%.*s\n", 3, s);

    printf("Characters:\t%c %%\n", 'A');

    printf("Integers:\n");
    printf("\tDecimal:\t%i %d %.6i %i %.0i %+i %i\n",
                         1, 2,   3, 0,   0,  4,-4);
    printf("\tHexadecimal:\t%x %x %X %#x\n", 5, 10, 10, 6);
    printf("\tOctal:\t\t%o %#o %#o\n", 10, 10, 4);

    printf("Floating-point:\n");
    printf("\tRounding:\t%f %.0f %.32f\n", 1.5, 1.5, 1.3);
    printf("\tPadding:\t%05.2f %.2f %5.2f\n", 1.5, 1.5, 1.5);
    printf("\tScientific:\t%E %e\n", 1.5, 1.5);
    printf("\tHexadecimal:\t%a %A\n", 1.5, 1.5);
    printf("\tSpecial values:\t0/0=%g 1/0=%g\n", 0.0 / 0.0, 1.0 / 0.0);

    printf("Fixed-width types:\n");
    printf("\tLargest 32-bit value is %" PRIu32 " or %#" PRIx32 "\n",
                                     UINT32_MAX,     UINT32_MAX );
}
#endif

int main()
{
    /* %d signed decimal */
    printf("%d\n", 0);
    printf("%d\n", 1);
    printf("%d\n", -1);
    printf("%d\n", 12345);
    printf("%d\n", -12345);
    printf("%d\n", 32767);
    printf("%d\n", -32767);

    /* %u unsigned decimal */
    printf("%u\n", 0);
    printf("%u\n", 1);
    printf("%u\n", 12345);
    printf("%u\n", 32767);

    /* %x hex -- minimal width, lowercase a-f (C89) */
    printf("%x\n", 0);
    printf("%x\n", 255);       /* ff */
    printf("%x\n", 291);       /* 123 */
    printf("%x\n", 2748);      /* abc */
    printf("%x\n", 32767);     /* 7fff */

    /* %c character */
    printf("%c\n", 65);        /* A */
    printf("%c\n", 97);        /* a */
    printf("%c\n", 48);        /* 0 */

    /* %s string */
    printf("%s\n", "hello");
    printf("%s\n", "world");

    /* %.Ns string precision: truncate to at most N characters, stopping
     * at a NUL first if the string is shorter than N. */
    printf("[%.4s]\n", "hello");       /* [hell] */
    printf("[%.0s]\n", "hello");       /* [] */
    printf("[%.20s]\n", "hi");         /* [hi] */
    printf("[%10.3s]\n", "hello");     /* [       hel] */
    printf("[%-10.3s]\n", "hello");    /* [hel       ] */

    /* %% literal percent */
    printf("100%%\n");

    /* field width: right-justified, space-padded */
    printf("[%6d]\n", 0);      /* [     0] */
    printf("[%6d]\n", 42);     /* [    42] */
    printf("[%6d]\n", -42);    /* [   -42] */
    printf("[%6d]\n", 32767);  /* [ 32767] */
    printf("[%6u]\n", 0);      /* [     0] */
    printf("[%6u]\n", 42);     /* [    42] */
    printf("[%6s]\n", "abc");  /* [   abc] */
    printf("[%6s]\n", "hello");/* [ hello] */
    printf("[%6x]\n", 255);    /* [    ff] */
    printf("[%6x]\n", 2748);   /* [   abc] */

    /* multiple arguments */
    printf("%d %d %d\n", 1, 2, 3);
    printf("%s=%d\n", "ans", 42);
    printf("%c%c%c\n", 65, 66, 67);  /* ABC */

    size_t st = 33333;
    printf( "size_t zu: %zu\n", st );
    printf( "size_t zd: %zd\n", st );
    printf( "size_t zx: %zx\n", st );

    /* left justification */
    printf("[%5s]\n", "ab");       /* [   ab] */
    printf("[%-5s]\n", "ab");      /* [ab   ] */
    printf("[%-3s:%3d:%6ld]\n", "x", 7, 12345L); /* [x  :  7: 12345] */

    // no real attempt to make printf conformant on a Z80 cppreference();

    printf("tprintf ok\n");
    return 0;
}
