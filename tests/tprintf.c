/* validates some printf behaviors */

#include <stdio.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

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

    /* %o octal -- minimal width */
    printf("%o\n", 0);         /* 0 */
    printf("%o\n", 8);         /* 10 */
    printf("%o\n", 10);        /* 12 */
    printf("%o\n", 511);       /* 777 */
    printf("[%6o]\n", 10);     /* [    12] */

    /* An unsupported flag ('+' force-sign, '#' alternate-form) must not
     * desync the arguments that follow it on the same call - confirmed as
     * a real bug: it used to shift every subsequent %-conversion's
     * argument by one. Neither flag's own cosmetic effect is implemented
     * (no '+' sign, no "0x"/leading-0 prefix), but everything after them
     * must still read the right argument. */
    printf("%+d %d %d\n", 1, 2, 3);        /* 1 2 3 */
    printf("%#x %#o %d\n", 6, 4, 9);       /* 6 4 9 */

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

    /* %.0f: an explicit zero precision must round to the nearest integer
     * and print no decimal point at all - not fall back to the default of
     * 6 decimal places (the previous behavior, since it couldn't tell
     * "no precision given" from "precision explicitly 0"). */
    printf("[%.0f]\n", 1.5);       /* [2] */
    printf("[%.2f]\n", 1.5);       /* [1.50] */

    /* %f field width: right-justified (space), left-justified (space),
     * and the '0' flag (zero-fill, landing after a '-' sign rather than
     * before it) - none of this was implemented at all previously; width
     * was silently ignored for every %f conversion. */
    printf("[%8.2f]\n", 1.5);      /* [    1.50] */
    printf("[%-8.2f]\n", 1.5);     /* [1.50    ] */
    printf("[%08.2f]\n", 1.5);     /* [00001.50] */
    printf("[%08.2f]\n", -1.5);    /* [-0001.50] */
    printf("[%12f]\n", 1.5);       /* [    1.500000] */

    /* long strings: %s length must not truncate to 8 bits. A strlen >= 256
     * used to wrap to 0 in printf's internal length counter, so
     * printf("%s", ac) printed nothing at all for such a string - exactly
     * what surfaced when tests/pihex.c was hand-modified to double its
     * generated string's length. A related bug in the same fix (caught only
     * via sprintf's return value, not by what got printed) had the
     * length-computation helper collide with printf's own running
     * output-char count, corrupting the count for every %s conversion. */
    {
        char buf[300];
        char sbuf[300];
        char vb[8];
        int i, sn;

        for (i = 0; i < 296; i++) buf[i] = 'a';
        buf[296] = 'x';
        buf[297] = 'y';
        buf[298] = 'z';
        buf[299] = 0;

        printf("longstr len: %d\n", (int)strlen(buf));        /* 299 */
        printf("longstr first3: %.3s\n", buf);                 /* aaa */
        printf("longstr last3: %s\n", buf + 296);               /* xyz */
        printf("longstr full:\n%s\n", buf);

        sn = sprintf(sbuf, "%s", buf);
        printf("sprintf longstr n=%d slen=%d\n", sn, (int)strlen(sbuf)); /* 299 299 */

        for (i = 0; i < 255; i++) buf[i] = 'p';
        buf[255] = 0;
        printf("buf255 len: %d\n", (int)strlen(buf));           /* 255 */

        for (i = 0; i < 256; i++) buf[i] = 'p';
        buf[256] = 0;
        printf("buf256 len: %d\n", (int)strlen(buf));           /* 256 */

        /* precision clamp on a long (>255) string */
        for (i = 0; i < 296; i++) buf[i] = 'a';
        buf[296] = 0;
        printf("[%.5s]\n", buf);                                 /* [aaaaa] */

        /* sprintf's return value must equal the number of characters
         * written, not corrupted by the register collision above
         * (previously shipped bug: this returned 4 instead of 2). */
        sn = sprintf(vb, "%s", "hi");
        printf("sprintf short n=%d\n", sn);                      /* 2 */
    }

    // no real attempt to make printf conformant on a Z80 cppreference();

    printf("tprintf ok\n");
    return 0;
}
