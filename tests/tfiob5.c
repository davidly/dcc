#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

static int fails;
static char bigbuf[32768];

static void check(const char *name, int ok)
{
    if (!ok) {
        printf("FAIL %s errno=%d\n", name, errno);
        fails++;
    }
}

static void cleanup(void)
{
    unlink("F5.TMP");
    unlink("M5.TMP");
    unlink("Z5.TMP");
    unlink("C5.TMP");
}

int main(void)
{
    FILE *fp;
    int fd;
    int badfd;
    int n;
    char small[4];
    long pos;

    cleanup();

    errno = 0;
    fd = open("M5.TMP", O_WRONLY | O_TRUNC);
    check("trunc missing", fd == -1 && errno == ENOENT);
    errno = 0;
    check("trunc missing no create",
          open("M5.TMP", O_RDONLY) == -1 && errno == ENOENT);

    errno = 0;
    fd = open("F5.TMP", O_RDONLY | O_TRUNC);
    check("readonly trunc", fd == -1 && errno == EINVAL);
    errno = 0;
    fd = open("F5.TMP", 3);
    check("reserved access", fd == -1 && errno == EINVAL);

    fd = open("F5.TMP", O_WRONLY | O_CREAT | O_TRUNC);
    check("create seed", fd >= 3);
    if (fd >= 3) {
        check("write seed", write(fd, "ABC", 3) == 3);
        check("close seed", close(fd) == 0);
    }

    fd = open("F5.TMP", O_WRONLY);
    check("open write only", fd >= 3);
    if (fd >= 3) {
        errno = 0;
        check("read write-only", read(fd, small, 1) == -1 && errno == EBADF);
        close(fd);
    }

    fd = open("F5.TMP", O_RDONLY);
    check("open read only", fd >= 3);
    if (fd >= 3) {
        errno = 0;
        check("write read-only", write(fd, "X", 1) == -1 && errno == EBADF);
        close(fd);
    }

    fp = fopen("F5.TMP", "r");
    check("fopen read only", fp != NULL);
    if (fp != NULL) {
        errno = 0;
        check("fwrite read-only",
              fwrite("X", 1, 1, fp) == 0 && errno == EBADF && ferror(fp));
        fclose(fp);
    }

    fd = open("F5.TMP", O_WRONLY | O_CREAT);
    check("open creat existing", fd >= 3);
    if (fd >= 3) {
        check("creat starts zero", lseek(fd, 0L, SEEK_CUR) == 0L);
        check("overwrite first", write(fd, "Z", 1) == 1);
        close(fd);
    }
    fd = open("F5.TMP", O_RDONLY);
    if (fd >= 3) {
        n = read(fd, small, 3);
        check("creat preserves file",
              n == 3 && small[0] == 'Z' && small[1] == 'B' && small[2] == 'C');
        close(fd);
    } else {
        check("reopen creat file", 0);
    }

    fd = open("F5.TMP", O_WRONLY | O_TRUNC);
    check("truncate existing", fd >= 3);
    if (fd >= 3)
        close(fd);
    fd = open("F5.TMP", O_RDONLY);
    check("truncated length", fd >= 3 && lseek(fd, 0L, SEEK_END) == 0L);
    if (fd >= 3)
        close(fd);

    fd = open("Z5.TMP", O_RDWR | O_CREAT | O_TRUNC);
    check("zero setup", fd >= 3);
    if (fd >= 3) {
        check("zero seed", write(fd, "Q", 1) == 1);
        check("seek hole", lseek(fd, 256L, SEEK_SET) == 256L);
        check("zero write", write(fd, "X", 0) == 0);
        check("zero no extend", lseek(fd, 0L, SEEK_END) == 1L);

        badfd = fd | 0x0100;
        errno = 0;
        check("wide read", read(badfd, small, 1) == -1 && errno == EBADF);
        errno = 0;
        check("wide write", write(badfd, "X", 1) == -1 && errno == EBADF);
        errno = 0;
        check("wide seek", lseek(badfd, 0L, SEEK_SET) == -1L && errno == EBADF);
        errno = 0;
        check("wide fsync", fsync(badfd) == -1 && errno == EBADF);
        errno = 0;
        check("wide fdatasync", fdatasync(badfd) == -1 && errno == EBADF);
        errno = 0;
        check("wide close", close(badfd) == -1 && errno == EBADF);

        check("wide kept fd", lseek(fd, 0L, SEEK_SET) == 0L);
        check("wide kept data", read(fd, small, 1) == 1 && small[0] == 'Q');

        check("seek reset", lseek(fd, 0L, SEEK_SET) == 0L);
        errno = 0;
        check("seek negative set",
              lseek(fd, -1L, SEEK_SET) == -1L && errno == EINVAL);
        check("seek set unchanged", lseek(fd, 0L, SEEK_CUR) == 0L);
        errno = 0;
        check("seek negative cur",
              lseek(fd, -1L, SEEK_CUR) == -1L && errno == EINVAL);
        check("seek cur unchanged", lseek(fd, 0L, SEEK_CUR) == 0L);
        errno = 0;
        check("seek wide whence",
              lseek(fd, 0L, 0x0100) == -1L && errno == EINVAL);

        check("seek long max",
              lseek(fd, LONG_MAX, SEEK_SET) == LONG_MAX);
        errno = 0;
        check("seek overflow",
              lseek(fd, 1L, SEEK_CUR) == -1L && errno == EINVAL);
        check("seek overflow unchanged",
              lseek(fd, 0L, SEEK_CUR) == LONG_MAX);
        check("seek final reset", lseek(fd, 0L, SEEK_SET) == 0L);

        check("fsync valid", fsync(fd) == 0);
        check("fdatasync valid", fdatasync(fd) == 0);
        close(fd);
        errno = 0;
        check("fsync closed", fsync(fd) == -1 && errno == EBADF);
    }

    fd = open("C5.TMP", O_RDWR | O_CREAT | O_TRUNC);
    check("count setup", fd >= 3);
    if (fd >= 3) {
        n = write(fd, bigbuf, 32768U);
        check("write capped", n == INT_MAX);
        pos = lseek(fd, 0L, SEEK_END);
        check("write cap position", pos == (long)INT_MAX);
        check("count rewind", lseek(fd, 0L, SEEK_SET) == 0L);
        n = read(fd, bigbuf, 32768U);
        check("read capped", n == INT_MAX);
        close(fd);
    }

    cleanup();
    if (fails)
        printf("tfiob5 FAILED %d\n", fails);
    else
        puts("tfiob5 ok");
    return fails ? 1 : 0;
}
