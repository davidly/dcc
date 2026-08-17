#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void fail_int(const char *name, int got, int expected)
{
    printf("FAIL %s got %d expected %d\n", name, got, expected);
    failures++;
}

static void fail_long(const char *name, long got, long expected)
{
    printf("FAIL %s got %ld expected %ld\n", name, got, expected);
    failures++;
}

static void fail_str(const char *name, const char *got, const char *expected)
{
    printf("FAIL %s got '%s' expected '%s'\n", name, got, expected);
    failures++;
}

static void check_int(const char *name, int got, int expected)
{
    if (got != expected)
        fail_int(name, got, expected);
}

static void check_long(const char *name, long got, long expected)
{
    if (got != expected)
        fail_long(name, got, expected);
}

static void check_str(const char *name, const char *got, const char *expected)
{
    if (strcmp(got, expected) != 0)
        fail_str(name, got, expected);
}

static void test_sscanf_numbers(void)
{
    int n;
    int a;
    int b;
    int c;
    int d;
    unsigned int u;
    long l;
    unsigned long ul;
    char word[16];

    a = b = c = d = 0;
    u = 0;
    n = sscanf("  -12 34 0x1f 077 end", "%d %u %i %i %s", &a, &u, &b, &c, word);
    check_int("num count", n, 5);
    check_int("num a", a, -12);
    check_int("num u", u, 34);
    check_int("num hex", b, 31);
    check_int("num oct", c, 63);
    check_str("num word", word, "end");

    l = 0;
    ul = 0;
    n = sscanf("-123456 12345678", "%ld %lx", &l, &ul);
    check_int("long count", n, 2);
    check_long("long signed", l, -123456L);
    check_long("long hex", (long)ul, 0x12345678L);

    d = 0;
    n = sscanf("77", "%o", &d);
    check_int("oct count", n, 1);
    check_int("oct value", d, 63);
}

static void test_sscanf_strings(void)
{
    int n;
    int value;
    char word[16];
    char chars[5];

    word[0] = 0;
    n = sscanf("abcdef", "%3s", word);
    check_int("width string count", n, 1);
    check_str("width string", word, "abc");

    chars[0] = chars[1] = chars[2] = chars[3] = chars[4] = 0;
    n = sscanf("abcd", "%2c%2c", chars, chars + 2);
    check_int("char count", n, 2);
    check_int("char0", chars[0], 'a');
    check_int("char1", chars[1], 'b');
    check_int("char2", chars[2], 'c');
    check_int("char3", chars[3], 'd');

    value = 0;
    n = sscanf("123 456", "%*d%d", &value);
    check_int("suppress count", n, 1);
    check_int("suppress value", value, 456);

    value = 0;
    n = sscanf("A=42%", "A=%d%%", &value);
    check_int("literal count", n, 1);
    check_int("literal value", value, 42);
}

static void test_sscanf_failures(void)
{
    int n;
    int value;
    char ch;

    value = 99;
    n = sscanf("bad", "%d", &value);
    check_int("bad count", n, 0);
    check_int("bad unchanged", value, 99);

    value = 0;
    ch = 0;
    n = sscanf("12x", "%d%c", &value, &ch);
    check_int("unget count", n, 2);
    check_int("unget value", value, 12);
    check_int("unget char", ch, 'x');

    value = 0;
    n = sscanf("", "%d", &value);
    check_int("empty count", n, EOF);

    value = 0;
    n = sscanf("B=42", "A=%d", &value);
    check_int("literal mismatch", n, 0);
}

static void test_fscanf_file(void)
{
    FILE *fp;
    int n;
    int literal_value;
    int value;
    int auto_value;
    char ch;
    char word[16];

    remove("TSCAN.TMP");
    fp = fopen("TSCAN.TMP", "w");
    if (fp == NULL) {
        printf("FAIL fopen write\n");
        failures++;
        return;
    }
    fputs(" -99 hello 0x2a\n", fp);
    fclose(fp);

    fp = fopen("TSCAN.TMP", "r");
    if (fp == NULL) {
        printf("FAIL fopen read\n");
        failures++;
        return;
    }

    value = 0;
    auto_value = 0;
    word[0] = 0;
    n = fscanf(fp, "%d %s %i", &value, word, &auto_value);
    check_int("fscanf count", n, 3);
    check_int("fscanf value", value, -99);
    check_str("fscanf word", word, "hello");
    check_int("fscanf auto", auto_value, 42);

    fclose(fp);

    remove("TSCAN.TMP");
    fp = fopen("TSCAN.TMP", "w");
    if (fp == NULL) {
        printf("FAIL fopen write 2\n");
        failures++;
        return;
    }
    fputs("12x", fp);
    fclose(fp);

    fp = fopen("TSCAN.TMP", "r");
    if (fp == NULL) {
        printf("FAIL fopen read 2\n");
        failures++;
        return;
    }

    value = 0;
    ch = 0;
    n = fscanf(fp, "%d", &value);
    check_int("fscanf split count 1", n, 1);
    check_int("fscanf split value", value, 12);

    n = fscanf(fp, "%c", &ch);
    check_int("fscanf split count 2", n, 1);
    check_int("fscanf split char", ch, 'x');

    fclose(fp);
    remove("TSCAN.TMP");

    fp = fopen("TSCAN.TMP", "w");
    if (fp == NULL) {
        printf("FAIL fopen write literal\n");
        failures++;
        return;
    }
    fputs("A=42", fp);
    fclose(fp);

    fp = fopen("TSCAN.TMP", "r");
    if (fp == NULL) {
        printf("FAIL fopen read literal\n");
        failures++;
        return;
    }

    literal_value = 0;
    n = fscanf(fp, "A=%d", &literal_value);
    check_int("fscanf literal count", n, 1);
    check_int("fscanf literal value", literal_value, 42);

    fclose(fp);
    remove("TSCAN.TMP");
}

int main(void)
{
    test_sscanf_numbers();
    test_sscanf_strings();
    test_sscanf_failures();
    test_fscanf_file();

    if (failures != 0)
        return 1;

    printf("tscanf: all tests passed\n");
    return 0;
}
