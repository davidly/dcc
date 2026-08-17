#include <stdio.h>
#include <string.h>

extern int _argc;
extern void _build_argv(void);
extern char __bssb;
extern char __bsse;
extern char __hstart;

static int fails;

static unsigned char *tail(void)
{
    return (unsigned char *)0x80;
}

static void reset_tail(unsigned int length, unsigned char fill)
{
    unsigned char *p;
    unsigned int i;

    p = tail();
    p[0] = (unsigned char)length;
    for (i = 1; i <= 127; i++)
        p[i] = fill;
}

static void fail(const char *name)
{
    printf("FAIL %s\n", name);
    fails++;
}

static void check_empty(char *argv[])
{
    unsigned char *p;

    p = tail();
    reset_tail(0, 0x5a);
    _build_argv();
    if (_argc != 1 || argv[0] == NULL || argv[0][0] != '\0' ||
        argv[1] != NULL)
        fail("empty terminator");
    if (p[127] != 0x5a)
        fail("empty sentinel");
}

static void check_tabs(char *argv[])
{
    static const char raw[] = "A\tB \t C\t\tD";
    static const char *want[4] = { "A", "B", "C", "D" };
    unsigned char *p;
    unsigned int i;

    p = tail();
    reset_tail(sizeof(raw) - 1, 0xa5);
    for (i = 0; i < sizeof(raw) - 1; i++)
        p[i + 1] = (unsigned char)raw[i];
    _build_argv();

    if (_argc != 5 || argv[5] != NULL)
        fail("tab argc terminator");
    for (i = 0; i < 4; i++)
        if (argv[i + 1] == NULL || strcmp(argv[i + 1], want[i]) != 0)
            fail("tab fields");
    if (p[127] != 0xa5)
        fail("tab sentinel");
}

static char arg_char(unsigned int index, unsigned int count)
{
    if (index == 0)
        return 'A';
    if (index + 1 == count)
        return 'Z';
    return 'x';
}

static void check_capacity(char *argv[], unsigned int count)
{
    unsigned char *p;
    unsigned int i;
    unsigned int length;

    length = count * 2 - 1;
    reset_tail(length, 0xcc);
    p = tail();
    for (i = 0; i < count; i++) {
        p[1 + i * 2] = (unsigned char)arg_char(i, count);
        if (i + 1 < count)
            p[2 + i * 2] = '\t';
    }

    _build_argv();
    if (_argc != (int)(count + 1) || argv[count + 1] != NULL)
        fail(count == 64 ? "capacity 64 terminator" : "capacity 63 terminator");
    for (i = 0; i < count; i++) {
        if (argv[i + 1] == NULL ||
            argv[i + 1][0] != arg_char(i, count) ||
            argv[i + 1][1] != '\0')
            fail(count == 64 ? "capacity 64 data" : "capacity 63 data");
    }
    if (count == 64 &&
        (argv[1] != (char *)(argv + 66) ||
         argv[64] != (char *)(argv + 66) + 126))
        fail("capacity storage bounds");
}

static void check_layout(char *argv[])
{
    char stack_probe;
    unsigned int argbuf;
    unsigned int argbuf_end;
    unsigned int bss_begin;
    unsigned int bss_end;
    unsigned int failures;

    argbuf = (unsigned int)(argv + 66);
    argbuf_end = argbuf + 128;
    bss_begin = (unsigned int)&__bssb;
    bss_end = (unsigned int)&__bsse;
    failures = (unsigned int)&fails;

    if (sizeof(char *) != 2)
        fail("pointer width");
    if ((unsigned int)argv != bss_end ||
        argbuf_end != (unsigned int)&__hstart)
        fail("argv BSS overlap");
    if (failures < bss_begin || failures + sizeof(fails) > bss_end)
        fail("BSS bounds");
    if (failures < argbuf_end && failures + sizeof(fails) > (unsigned int)argv)
        fail("argv object overlap");
    if ((unsigned int)&stack_probe <= (unsigned int)&__hstart)
        fail("stack BSS overlap");
}

static void check_clamp(char *argv[], unsigned int length)
{
    unsigned int i;

    reset_tail(length, 'Q');
    _build_argv();
    if (_argc != 2 || argv[1] == NULL || argv[2] != NULL)
        fail(length == 128 ? "clamp 128 vector" : "clamp 255 vector");
    if (argv[1] != NULL) {
        for (i = 0; i < 127; i++)
            if (argv[1][i] != 'Q')
                fail(length == 128 ? "clamp 128 data" : "clamp 255 data");
        if (argv[1][127] != '\0')
            fail(length == 128 ? "clamp 128 length" : "clamp 255 length");
    }
}

int main(int argc, char *argv[])
{
    fails = 0;

    if (argc != 1 || argv[1] != NULL)
        fail("initial terminator");
    check_layout(argv);
    check_tabs(argv);
    check_capacity(argv, 63);
    check_capacity(argv, 64);
    check_clamp(argv, 128);
    check_clamp(argv, 255);
    check_empty(argv);

    if (fails) {
        printf("targlim failed: %d\n", fails);
        return 1;
    }
    printf("targlim passed\n");
    return 0;
}
