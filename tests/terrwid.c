#include <errno.h>
#include <stdio.h>
#include <string.h>

struct errcase {
    int value;
    const char *message;
};

static const struct errcase cases[] = {
    { ENOENT, "No such file" },
    { EIO, "I/O error" },
    { EBADF, "Bad file descriptor" },
    { EXDEV, "Cross-device link" },
    { EINVAL, "Invalid argument" },
    { EMFILE, "Too many open files" },
    { EFBIG, "File too large" },
    { ENOSPC, "No space left on device" },
    { EDOM, "Domain error" },
    { ERANGE, "Range error" }
};

static int fails;

static void expect_message(const char *name, int value, const char *want)
{
    const char *got;

    got = strerror(value);
    if (strcmp(got, want) != 0) {
        printf("FAIL %s value=%d got='%s'\n", name, value, got);
        fails++;
    }
}

int main(void)
{
    unsigned int i;

    fails = 0;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        expect_message("canonical", cases[i].value, cases[i].message);
        expect_message("positive wide", cases[i].value + 0x100, "Error");
        expect_message("negative alias", cases[i].value - 0x100, "Error");
    }
    expect_message("zero", 0, "Error");
    expect_message("minus one", -1, "Error");
    expect_message("high positive", 0x7f02, "Error");

    if (fails) {
        printf("terrwid failed: %d\n", fails);
        return 1;
    }
    printf("terrwid passed\n");
    return 0;
}
