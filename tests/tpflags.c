/* Shared printf-family numeric flags: signs, prefixes, precision and sinks. */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>

static int failures;

static void expect(const char *want, const char *format, ...)
{
    static char buf[300];
    va_list ap;
    int n, size = (int)strlen(want);
    va_start(ap, format);
    n = vsprintf(buf, format, ap);
    va_end(ap);
    if (n != size || strcmp(buf, want)) {
        printf("FAIL format %s got [%s] expected [%s]\n", format, buf, want);
        failures++;
    }

}

static void bounded(const char *want, int cap, const char *format, ...)
{
    static char b[8];
    int n, size = (int)strlen(want);
    va_list ap;
    memset(b, '!', sizeof(b));
    va_start(ap, format);
    n = vsnprintf(b, cap, format, ap);
    va_end(ap);
    if (n != size || b[cap] != '!' ||
        (cap && (b[cap - 1 < size ? cap - 1 : size] != 0 ||
                 memcmp(b, want, cap - 1 < size ? cap - 1 : size)))) {
        puts("FAIL bounded prefix or count");
        failures++;
    }
}

static int emit(const char *format, ...)
{
    int n;
    va_list ap;
    va_start(ap, format);
    n = vprintf(format, ap);
    va_end(ap);
    return n;
}

static int file_emit(FILE *f, const char *format, ...)
{
    int n;
    va_list ap;
    va_start(ap, format);
    n = vfprintf(f, format, ap);
    va_end(ap);
    return n;
}

static float from_bits(uint32_t u)
{
    union { uint32_t u; float f; } v;
    v.u = u;
    return v.f;
}

int main(void)
{
    char b[80];
    FILE *f;
    int n;
    expect("+1  2 -3", "%+d % d %+d", 1, 2, -3);
    expect("+1 +2", "% +d %+ d", 1, 2);
    expect("+0007| 0007|-0007", "%+05d|% 05d|%+05d", 7, 7, -7);
    expect("+7   | 7   ", "%-+05d|%- 05d", 7, 7);
    expect(" +007|    +", "%+05.3d|%+5.0d", 7, 0);
    expect("-32768 -2147483648 +2147483647", "%+d %+ld %+ld",
           -32767 - 1, (-2147483647L - 1L), 2147483647L);
    expect("0x2a 0X2A 052", "%#x %#X %#o", 42u, 42u, 42u);
    expect("0x00002a|0X00002A|00000052", "%#08x|%#08X|%#08o", 42u, 42u, 42u);
    expect("  0x002a|     052|0x2a    ", "%#08.4x|%#08.3o|%-#08x", 42u, 42u, 42u);
    expect("0|0|0|[]|[0]|[    0]", "%#x|%#X|%#o|[%#.0x]|[%#.0o]|[%#5.0o]",
           0u, 0u, 0u, 0u, 0u, 0u);
    expect("0x12345678 0XFFFFFFFF", "%#lx %#lX", 0x12345678UL, 0xffffffffUL);
    expect("7 7 7", "%+u % u %#u", 7u, 7u, 7u);
    expect(" 1.500000|+1.500000|-1.500000", "% f|%+f|%+f", 1.5, 1.5, -1.5);
    expect("+0001.50| 0001.50|-0001.50", "%+08.2f|% 08.2f|%+08.2f", 1.5, 1.5, -1.5);
    expect("2.|+2.| 2.|2.    ", "%#.0f|%+#.0f|% #.0f|%-#6.0f", 2.0, 2.0, 2.0, 2.0);
    expect("+0 -0", "%+.0f %+.0f", from_bits(0), from_bits(0x80000000UL));
    expect("    +inf|     inf|    -inf", "%+08f|% 08f|%+08f",
           from_bits(0x7f800000UL), from_bits(0x7f800000UL), from_bits(0xff800000UL));
    expect("+1|2|3", "%+d|%u|%d", 1, 2u, 3);
    for (n = 0; n <= 6; n++) {
        bounded("+00007", n, "%+06d", 7);
        bounded("0x00002a", n, "%#08x", 42u);
        bounded("+0001.50", n, "%+08.2f", 1.5);
    }
    n = sprintf(b, "%+d %#x % f", 1, 2u, 3.0);
    if (n != 16 || strcmp(b, "+1 0x2  3.000000")) failures++;
    n = snprintf(b, 5, "%#08x", 42u);
    if (n != 8 || strcmp(b, "0x00")) failures++;
    if (printf("printf:+1=%+d hex=%#x\n", 1, 2u) != 21) failures++;
    if (emit("vprintf:% d\n", 3) != 11) failures++;
    f = fopen("TPFLAG.TMP", "wb");
    if (!f) return puts("FAIL file open");
    n = fprintf(f, "%+d %#x", 1, 2u);
    n += file_emit(f, " % f", 3.0);
    if (n != 16) failures++;
    fclose(f);
    f = fopen("TPFLAG.TMP", "rb");
    if (!f) return puts("FAIL file reopen");
    n = (int)fread(b, 1, 16, f); b[n] = 0;
    if (n != 16 || strcmp(b, "+1 0x2  3.000000")) failures++;
    fclose(f); remove("TPFLAG.TMP");
    if (failures) { printf("tpflags FAILED %d\n", failures); return 1; }
    puts("tpflags passed");
    return 0;
}
