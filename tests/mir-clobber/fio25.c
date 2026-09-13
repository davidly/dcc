#include <stdio.h>
#include <string.h>

#ifdef FIO_ALT_STRINGS
#define FIRST_NAME "FIOA2.IN"
#define SECOND_NAME "FIOB2.IN"
#define MISSING_NAME "FIONO2.IN"
#define FIRST_LINE "gamma\n"
#define SECOND_LINE "delta\n"
#define FIRST_CONTENT "gamma"
#define SECOND_CONTENT "delta"
#define SUCCESS_TEXT "fileio25 alternate ok\n"
#else
#define FIRST_NAME "FIOFRA.IN"
#define SECOND_NAME "FIOFRB.IN"
#define MISSING_NAME "FIONONE.IN"
#define FIRST_LINE "alpha\n"
#define SECOND_LINE "beta\n"
#define FIRST_CONTENT "alpha"
#define SECOND_CONTENT "beta"
#define SUCCESS_TEXT "fileio25 ok\n"
#endif

#ifdef FIO_LARGE_BUFFER
#define BUFFER_SIZE 40
#else
#define BUFFER_SIZE 32
#endif

#ifdef FIO_UNSIGNED_BUFFER
typedef unsigned char BufferChar;
#define BUFFER_ARG(value) ((char *)(value))
#else
typedef char BufferChar;
#define BUFFER_ARG(value) (value)
#endif

int main(void)
{
#ifdef FIO_VOLATILE_STREAM
    FILE * volatile fp;
#else
    FILE *fp;
#endif
    BufferChar buf[BUFFER_SIZE];
    int i;

    fp = fopen(FIRST_NAME, "w");
    if (!fp) { printf("FAIL create first\n"); return 1; }
    fputs(FIRST_LINE, fp);
    fclose(fp);

    fp = fopen(SECOND_NAME, "w");
    if (!fp) { printf("FAIL create second\n"); return 1; }
    fputs(SECOND_LINE, fp);
    fclose(fp);

    fp = fopen(FIRST_NAME, "r");
    if (!fp) { printf("FAIL open first\n"); return 1; }
    if (!fgets(BUFFER_ARG(buf), sizeof(buf), fp)) {
        printf("FAIL read first\n");
        return 1;
    }
    for (i = 0;
         buf[i] && buf[i] != '\n' && buf[i] != '\r';
         i++) ;
    buf[i] = '\0';
    if (strcmp(BUFFER_ARG(buf), FIRST_CONTENT) != 0) {
        printf("FAIL first content: %s\n", BUFFER_ARG(buf));
        return 1;
    }

    fp = freopen(SECOND_NAME, "r", fp);
    if (!fp) { printf("FAIL reopen second\n"); return 1; }
    if (!fgets(BUFFER_ARG(buf), sizeof(buf), fp)) {
        printf("FAIL read second\n");
        return 1;
    }
    for (i = 0;
         buf[i] && buf[i] != '\n' && buf[i] != '\r';
         i++) ;
    buf[i] = '\0';
    if (strcmp(BUFFER_ARG(buf), SECOND_CONTENT) != 0) {
        printf("FAIL second content: %s\n", BUFFER_ARG(buf));
        return 1;
    }
    fclose(fp);

    fp = fopen(FIRST_NAME, "r");
    if (fp) {
        FILE *fp2 = freopen(MISSING_NAME, "r", fp);
        if (fp2 != NULL) {
            printf("FAIL missing reopen returned non-NULL\n");
            return 1;
        }
    }

    remove(FIRST_NAME);
    remove(SECOND_NAME);
    printf(SUCCESS_TEXT);
    return 0;
}
