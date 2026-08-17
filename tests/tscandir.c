#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>

static int failures;

static void check_int(const char *name, int got, int expected)
{
    if (got != expected) {
        printf("FAIL %s got %d expected %d\n", name, got, expected);
        failures++;
    }
}

static void check_ptr(const char *name, const void *got, const void *expected)
{
    if (got != expected) {
        printf("FAIL %s pointer\n", name);
        failures++;
    }
}

static FILE *make_file(const char *name, const char *text)
{
    FILE *fp;

    remove(name);
    fp = fopen(name, "w");
    if (fp == NULL) {
        printf("FAIL create %s\n", name);
        failures++;
        return NULL;
    }
    fputs(text, fp);
    fclose(fp);
    return fopen(name, "r");
}

static void test_widths(void)
{
    int n;
    int value;
    unsigned int hex;
    char ch;

    value = -1;
    ch = 0;
    n = sscanf("07Z", "%2i%c", &value, &ch);
    check_int("width i count", n, 2);
    check_int("width i value", value, 7);
    check_int("width i next", ch, 'Z');

    hex = 0xffff;
    ch = 0;
    n = sscanf("0fZ", "%2x%c", &hex, &ch);
    check_int("width x count", n, 2);
    check_int("width x value", hex, 15);
    check_int("width x next", ch, 'Z');

    value = -1;
    ch = 0;
    n = sscanf("07", "%1i%c", &value, &ch);
    check_int("width one i count", n, 2);
    check_int("width one i value", value, 0);
    check_int("width one i next", ch, '7');

    hex = 0xffff;
    ch = 0;
    n = sscanf("0f", "%1x%c", &hex, &ch);
    check_int("width one x count", n, 2);
    check_int("width one x value", hex, 0);
    check_int("width one x next", ch, 'f');
}

static void test_fscanf_fgetc(void)
{
    FILE *fp;
    int n;
    int value;

    fp = make_file("SCA.TMP", "12x");
    if (fp == NULL)
        return;
    value = 0;
    n = fscanf(fp, "%d", &value);
    check_int("fscanf fgetc count", n, 1);
    check_int("fscanf fgetc value", value, 12);
    check_int("fscanf fgetc next", fgetc(fp), 'x');
    fclose(fp);
    remove("SCA.TMP");
}

static void test_two_fds(void)
{
    FILE *a;
    FILE *b;
    int av;
    int bv;

    a = make_file("SCB.TMP", "12a");
    b = make_file("SCC.TMP", "34b");
    if (a == NULL || b == NULL) {
        if (a != NULL)
            fclose(a);
        if (b != NULL)
            fclose(b);
        remove("SCB.TMP");
        remove("SCC.TMP");
        return;
    }

    av = bv = 0;
    check_int("two fd scan a", fscanf(a, "%d", &av), 1);
    check_int("two fd scan b", fscanf(b, "%d", &bv), 1);
    check_int("two fd value a", av, 12);
    check_int("two fd value b", bv, 34);
    check_int("two fd next a", fgetc(a), 'a');
    check_int("two fd next b", fgetc(b), 'b');

    fclose(a);
    fclose(b);
    remove("SCB.TMP");
    remove("SCC.TMP");
}

static void test_seek_reopen(void)
{
    FILE *fp;
    int value;

    fp = make_file("SCD.TMP", "12x");
    if (fp == NULL)
        return;
    value = 0;
    check_int("seek scan", fscanf(fp, "%d", &value), 1);
    check_int("seek result", fseek(fp, 0L, SEEK_SET), 0);
    check_int("seek clears pushback", fgetc(fp), '1');
    fclose(fp);
    remove("SCD.TMP");

    fp = make_file("SCE.TMP", "56y");
    if (fp == NULL)
        return;
    value = 0;
    check_int("reopen scan", fscanf(fp, "%d", &value), 1);
    fclose(fp);
    remove("SCE.TMP");

    fp = make_file("SCF.TMP", "new");
    if (fp == NULL)
        return;
    check_int("reopen clears pushback", fgetc(fp), 'n');
    fclose(fp);
    remove("SCF.TMP");
}

static void test_directory_state(void)
{
    FILE *fp;
    DIR *first;
    DIR *second;
    struct dirent *entry;

    fp = make_file("B3A.ONE", "1");
    if (fp != NULL)
        fclose(fp);
    fp = make_file("B3B.TWO", "2");
    if (fp != NULL)
        fclose(fp);

    first = opendir("B3?.ONE");
    if (first == NULL) {
        printf("FAIL first opendir\n");
        failures++;
    } else {
        errno = 0;
        second = opendir("B3?.TWO");
        check_ptr("concurrent opendir", second, NULL);
        check_int("concurrent errno", errno, EMFILE);

        entry = readdir(first);
        if (entry == NULL) {
            printf("FAIL first handle preserved\n");
            failures++;
        } else {
            check_int("first handle name", strcmp(entry->d_name, "B3A.ONE"), 0);
        }
        check_ptr("first handle done", readdir(first), NULL);
        check_int("first closedir", closedir(first), 0);
    }

    second = opendir("B3?.TWO");
    if (second == NULL) {
        printf("FAIL second opendir after close\n");
        failures++;
    } else {
        entry = readdir(second);
        if (entry == NULL) {
            printf("FAIL second handle entry\n");
            failures++;
        } else {
            check_int("second handle name", strcmp(entry->d_name, "B3B.TWO"), 0);
        }
        check_int("second closedir", closedir(second), 0);
    }

    errno = EIO;
    first = opendir("Z9N*.QQQ");
    if (first == NULL) {
        printf("FAIL no-match opendir\n");
        failures++;
    } else {
        check_int("no-match errno", errno, 0);
        check_ptr("no-match done", readdir(first), NULL);
        check_int("no-match readdir errno", errno, 0);

        errno = 0;
        second = opendir(".");
        check_ptr("done handle still active", second, NULL);
        check_int("done handle errno", errno, EMFILE);
        check_int("no-match closedir", closedir(first), 0);

        errno = 0;
        check_ptr("readdir closed", readdir(first), NULL);
        check_int("readdir closed errno", errno, EINVAL);
        errno = 0;
        check_int("closedir twice", closedir(first), -1);
        check_int("closedir twice errno", errno, EINVAL);

        second = opendir(".");
        if (second == NULL) {
            printf("FAIL opendir after no-match close\n");
            failures++;
        } else {
            check_int("post no-match closedir", closedir(second), 0);
        }
    }

    errno = 0;
    check_ptr("readdir null", readdir(NULL), NULL);
    check_int("readdir null errno", errno, EINVAL);
    errno = 0;
    check_ptr("readdir bad", readdir((DIR *)2), NULL);
    check_int("readdir bad errno", errno, EINVAL);
    errno = 0;
    check_int("closedir null", closedir(NULL), -1);
    check_int("closedir null errno", errno, EINVAL);
    errno = 0;
    check_int("closedir bad", closedir((DIR *)2), -1);
    check_int("closedir bad errno", errno, EINVAL);

    remove("B3A.ONE");
    remove("B3B.TWO");
}

int main(void)
{
    test_widths();
    test_fscanf_fgetc();
    test_two_fds();
    test_seek_reopen();
    test_directory_state();

    if (failures != 0) {
        printf("tscandir FAILED %d\n", failures);
        return 1;
    }

    printf("tscandir ok\n");
    return 0;
}
