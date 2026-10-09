/* runall.ps1 stages READONLY.TXT with host read-only permissions.
 * Do not create/chmod it here: the CP/M test must exercise host permissions.
 * BDOS open has no access mode: ntvcm falls back to a read-only host handle.
 * Read/write opens may succeed; actual writes must fail without changing data.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int failures;
static const char expected[] = "Read-only fixture.";

static void check(const char *name, int ok)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        printf("  errno=%d\n", errno);
        failures++;
    }
}

static int write_error(int error)
{
    /* BDOS may lose the host errno. ntvcm returns status 6 when a host
     * write fails, which DCC maps to EFBIG. Accept that detectable error
     * even though its description does not identify the permission failure. */
    return error == EFBIG || error == EIO || error == EACCES ||
           error == EPERM || error == EROFS;
}

int main(void)
{
    int fd, error, result, flush_result, close_result, stream_error;
    int original_size, final_size;
    char original[128], final[128];
    size_t written;
    FILE *file;
    char buffer[sizeof(expected)];

    errno = 0;
    fd = open("READONLY.TXT", O_RDONLY);
    check("open O_RDONLY", fd >= 0);
    if (fd >= 0) {
        check("read contents", read(fd, buffer, sizeof(expected) - 1) ==
              sizeof(expected) - 1 &&
              memcmp(buffer, expected, sizeof(expected) - 1) == 0);
        check("close", close(fd) == 0);
    }

    /* Snapshot the complete CP/M record, including any host newline/padding. */
    fd = open("READONLY.TXT", O_RDONLY);
    original_size = -1;
    if (fd >= 0) {
        original_size = read(fd, original, sizeof(original));
        close(fd);
    }
    check("snapshot fixture", original_size >= sizeof(expected) - 1);

    errno = 0;
    fd = open("READONLY.TXT", O_RDWR);
    check("open O_RDWR", fd >= 0);
    if (fd >= 0) {
        errno = 0;
        result = write(fd, "X", 1);
        error = errno;
        check("write rejected", result == -1);
        errno = error;
        check("write errno", write_error(error));
        check("close after rejected write", close(fd) == 0);
    }

    errno = 0;
    file = fopen("READONLY.TXT", "r");
    check("fopen r", file != NULL);
    if (file != NULL) {
        check("fread contents", fread(buffer, 1, sizeof(expected) - 1, file) ==
              sizeof(expected) - 1 &&
              memcmp(buffer, expected, sizeof(expected) - 1) == 0);
        check("fclose", fclose(file) == 0);
    }

    errno = 0;
    file = fopen("READONLY.TXT", "r+");
    check("fopen r+", file != NULL);
    if (file != NULL) {
        errno = 0;
        written = fwrite("Y", 1, 1, file);
        error = errno;
        errno = 0;
        flush_result = fflush(file);
        if (flush_result == EOF)
            error = errno;
        stream_error = ferror(file);
        errno = 0;
        close_result = fclose(file);
        if (close_result == EOF)
            error = errno;
        /* DCC writes files through immediately; allow buffered implementations
         * to report failure at flush/close instead. Save errno before each
         * subsequent call, and never inspect a stream after fclose. */
        check("stdio write rejected",
              written == 0 || flush_result == EOF || close_result == EOF);
        errno = error;
        check("stdio write errno", write_error(error));
        check("stdio error indicator", stream_error != 0 || close_result == EOF);
    }

    fd = open("READONLY.TXT", O_RDONLY);
    final_size = -1;
    if (fd >= 0) {
        final_size = read(fd, final, sizeof(final));
        close(fd);
    }
    check("fixture unchanged", original_size > 0 && final_size == original_size &&
          memcmp(original, final, original_size) == 0);

    return failures != 0;
}
