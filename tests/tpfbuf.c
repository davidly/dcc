/* tpfbuf.c - printf float staging and file-sink boundary checks. */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

static int failures;

/* C's _pf32h name binds to the runtime's public __pf32h block marker.
 * Guard its first 11-byte decimal work buffer against formatter overruns. */
extern unsigned char _pf32h;

struct guarded {
    unsigned char pre[4];
    char buf[48];
    unsigned char post[4];
};

static struct guarded g;
static char expected[40];
static char payload[258];

static void fail_int(const char *name, int got, int want)
{
    printf("FAIL %s: got %d want %d\n", name, got, want);
    failures++;
}

static void check_int(const char *name, int got, int want)
{
    if (got != want)
        fail_int(name, got, want);
}

static void check_text(const char *name, const char *got, const char *want)
{
    if (strcmp(got, want) != 0) {
        printf("FAIL %s: text mismatch\n", name);
        failures++;
    }
}

static void guard_init(struct guarded *g)
{
    int i;

    for (i = 0; i < 4; i++) {
        g->pre[i] = 0xa5;
        g->post[i] = 0x5a;
    }
    memset(g->buf, 0xcc, sizeof(g->buf));
}

static void guard_check(const char *name, const struct guarded *g)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g->pre[i] != 0xa5 || g->post[i] != 0x5a) {
            printf("FAIL %s: canary %d\n", name, i);
            failures++;
            return;
        }
    }
}

static void make_fixed(char *s, int precision)
{
    int i;

    s[0] = '1';
    s[1] = '.';
    for (i = 0; i < precision; i++)
        s[i + 2] = '0';
    s[precision + 2] = 0;
}

static void rtl_canary_init(void)
{
    unsigned char *p;
    int i;

    p = &_pf32h;
    for (i = 0; i < 11; i++)
        p[i] = (unsigned char)(0x80 + i);
}

static void rtl_canary_check(const char *name)
{
    unsigned char *p;
    int i;

    p = &_pf32h;
    for (i = 0; i < 11; i++) {
        if (p[i] != (unsigned char)(0x80 + i)) {
            printf("FAIL %s: runtime canary %d\n", name, i);
            failures++;
            return;
        }
    }
}

static int relay_vfprintf(FILE *fp, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vfprintf(fp, fmt, ap);
    va_end(ap);
    return n;
}

static void check_file(const char *path, int want)
{
    FILE *fp;
    char buf[320];
    int i;
    int n;

    fp = fopen(path, "r");
    if (fp == NULL) {
        printf("FAIL %s: reopen\n", path);
        failures++;
        return;
    }
    n = (int)fread(buf, 1, want, fp);
    fclose(fp);
    check_int(path, n, want);
    for (i = 0; i < n; i++) {
        if (buf[i] != (char)('A' + (i % 26))) {
            printf("FAIL %s: byte %d\n", path, i);
            failures++;
            break;
        }
    }
}

int main(void)
{
    FILE *fp;
    int i;
    int n;

    guard_init(&g);
    n = sprintf(g.buf, "%5.1f", 1.0f);
    check_int("float width return", n, (int)strlen(g.buf));
    check_int("float width length", n, 5);
    check_text("float width text", g.buf, "  1.0");
    guard_check("float width", &g);

    guard_init(&g);
    make_fixed(expected, 30);
    n = sprintf(g.buf, "%1.30f", 1.0f);
    check_int("precision 30 return", n, 32);
    check_int("precision 30 strlen", (int)strlen(g.buf), 32);
    check_text("precision 30 text", g.buf, expected);
    guard_check("precision 30", &g);

    guard_init(&g);
    make_fixed(expected, 31);
    n = sprintf(g.buf, "%1.31f", 1.0f);
    check_int("precision 31 return", n, 33);
    check_int("precision 31 strlen", (int)strlen(g.buf), 33);
    check_text("precision 31 text", g.buf, expected);
    guard_check("precision 31", &g);

    guard_init(&g);
    n = snprintf(g.buf, 8, "%1.31f", 1.0f);
    check_int("snprintf logical return", n, 33);
    check_int("snprintf stored length", (int)strlen(g.buf), 7);
    check_text("snprintf stored text", g.buf, "1.00000");
    guard_check("snprintf", &g);

    for (i = 0; i < 257; i++)
        payload[i] = (char)('A' + (i % 26));
    payload[257] = 0;

    unlink("PFB256.TMP");
    rtl_canary_init();
    fp = fopen("PFB256.TMP", "w");
    if (fp == NULL) {
        printf("FAIL file256: create\n");
        failures++;
    } else {
        payload[256] = 0;
        n = fprintf(fp, "%s", payload);
        check_int("file256 return", n, 256);
        check_int("file256 position", (int)ftell(fp), 256);
        fclose(fp);
        check_file("PFB256.TMP", 256);
        rtl_canary_check("file256");
        unlink("PFB256.TMP");
        payload[256] = (char)('A' + (256 % 26));
    }

    unlink("PFB257.TMP");
    rtl_canary_init();
    fp = fopen("PFB257.TMP", "w");
    if (fp == NULL) {
        printf("FAIL file257: create\n");
        failures++;
    } else {
        n = relay_vfprintf(fp, "%s", payload);
        check_int("file257 return", n, 257);
        check_int("file257 position", (int)ftell(fp), 257);
        fclose(fp);
        check_file("PFB257.TMP", 257);
        rtl_canary_check("file257");
        unlink("PFB257.TMP");
    }

    n = fprintf((FILE *)99, "bad");
    check_int("file write failure", n, -1);

    if (failures != 0)
        printf("tpfbuf FAILED %d\n", failures);
    else
        printf("tpfbuf ok\n");
    return failures != 0;
}
