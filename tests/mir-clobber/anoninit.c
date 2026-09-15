#include <stdio.h>
#include <string.h>

#ifdef ANONINIT_VOLATILE_FAILURES
static volatile int anoninit_failures;
#else
static int anoninit_failures;
#endif

#ifdef ANONINIT_VARIADIC_CHECK
static void anoninit_check(
    const char *name, long got, long want, ...)
#else
static void anoninit_check(
    const char *name, long got, long want)
#endif
{
    if (got != want) {
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
        ++anoninit_failures;
    }
}

static void anoninit_check_string(
    const char *name, const char *got, const char *want)
{
    if (got == NULL || want == NULL || strcmp(got, want) != 0) {
        printf("FAIL %s got=%s want=%s\n", name,
               got ? got : "(null)", want ? want : "(null)");
        ++anoninit_failures;
    }
}

struct AnoninitVersion {
    union {
        struct {
            unsigned major : 4;
            unsigned minor : 4;
            unsigned patch : 8;
        };
    };
};

struct AnoninitDate {
    struct {
        unsigned day : 5;
        unsigned month : 4;
        unsigned year : 7;
    };
};

union AnoninitRegister {
    int all;
    struct {
        unsigned char lo;
        unsigned char hi;
    };
};

union AnoninitSelection {
    int i;
    char c;
};

struct AnoninitEvent {
    char kind;
    union {
        struct {
            int lo;
            int hi;
        };
        long combined;
    };
};

struct AnoninitMessage {
    union {
        const char *text;
        int raw;
    };
    int code;
};

#ifdef ANONINIT_RENAMED
#define ANONINIT_REPORT_FUNCTION renamed_anonymous_initializer_report
#else
#define ANONINIT_REPORT_FUNCTION anonymous_initializer_report
#endif

static int ANONINIT_REPORT_FUNCTION(void)
{
    struct AnoninitVersion version = {
        .major = 2, .minor = 7, .patch = 15
    };
    struct AnoninitDate date = {
        .month = 6, .day = 17, .year = 42
    };
    union AnoninitRegister reg = { .hi = 3 };
    union AnoninitSelection sel = { .c = 'Z' };
    struct AnoninitEvent event = { 'K', { { 4, 9 } } };
    struct AnoninitMessage message = { { .text = "OK" }, 404 };

    printf("version=%u.%u.%u\n",
           version.major, version.minor, version.patch);
    printf("date=%u/%u/%u\n", date.day, date.month, date.year);
    printf("reg=%u/%u/%d\n", reg.lo, reg.hi, reg.all);
    printf("sel=%c\n", sel.c);
    printf("event=%c/%d,%d\n", event.kind, event.lo, event.hi);
    printf("message=%s/%d\n", message.text, message.code);

    anoninit_check("version.major", version.major, 2);
    anoninit_check("version.minor", version.minor, 7);
    anoninit_check("version.patch", version.patch, 15);
    anoninit_check("date.day", date.day, 17);
    anoninit_check("date.month", date.month, 6);
    anoninit_check("date.year", date.year, 42);
    anoninit_check("reg.lo", reg.lo, 0);
    anoninit_check("reg.hi", reg.hi, 3);
    anoninit_check("reg.all", reg.all, 3 << 8);
    anoninit_check("sel.c", sel.c, 'Z');
    anoninit_check("event.kind", event.kind, 'K');
    anoninit_check("event.lo", event.lo, 4);
    anoninit_check("event.hi", event.hi, 9);
    anoninit_check_string("message.text", message.text, "OK");
    anoninit_check("message.code", message.code, 404);

#ifdef ANONINIT_EXTRA_CFG
    if (anoninit_failures == 123)
        anoninit_failures += 2;
#endif
    if (anoninit_failures == 0)
        printf("anonymous init tests passed\n");
    return anoninit_failures != 0;
}

int main(void)
{
    int result = ANONINIT_REPORT_FUNCTION();

    printf("anonymous initializer report failures=%d\n",
           anoninit_failures);
    return result;
}
