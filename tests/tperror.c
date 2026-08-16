/* tperror.c - dedicated perror() regression coverage. Added alongside a
 * DCCRTL.MAC fix: perror() only checked its argument for NULL before
 * printing the "str: " prefix, but C89/C99 require the prefix to be
 * skipped for an empty string too (perror("") must print just the
 * message, not ": message"). Also covers the refactor that factored the
 * prefix and message print loops into a shared __prstr helper.
 *
 * "host": true in tests/_test_overrides.json - this RTL's strerror()
 * uses short custom message text ("No such file", not glibc's "No such
 * file or directory"), so the baseline can't match a real host libc.
 */
#include <stdio.h>
#include <errno.h>

int main(void)
{
    errno = ENOENT;
    perror("prefix");       /* "prefix: No such file" */

    errno = EBADF;
    perror("");             /* "Bad file descriptor" - empty string, no prefix */

    errno = EINVAL;
    perror(0);               /* "Invalid argument" - NULL, no prefix */

    errno = EMFILE;
    perror("again");        /* "again: Too many open files" */

    printf("tperror ok\n");
    return 0;
}
