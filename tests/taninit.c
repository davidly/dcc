/* taninit.c
 *
 * Local (auto) initialization of anonymous unions/structs and promoted
 * anonymous bit-field designators.  Regression coverage for the dcc auto
 * aggregate initializer fixes in emit_init_auto_struct_type:
 *
 *   1. Promoted anonymous bit-field designators (.major/.minor/.patch) are
 *      packed into the correct bit-field storage unit, including when the
 *      designators appear out of declaration order.
 *   2. A leading designator on a union resolves the direct member that owns a
 *      promoted anonymous member (or a named non-first member) instead of
 *      always selecting the first union member.
 *   3. Nested anonymous aggregate initializers consume only the braces they
 *      open, so brace ownership stays balanced through recursion and trailing
 *      members after a nested sub-object still initialize correctly.
 *
 * The programs are accepted by clang -std=c11 -Wall -Wextra -pedantic; the
 * values below are its reference output.  All variables are function-local so
 * the auto (stack) initializer path is exercised.
 */
#include <stdio.h>
#include <string.h>
static int failures;

static void chk(const char *name, long got, long want)
{
    if (got != want) {
        printf("FAIL %s got=%ld want=%ld\n", name, got, want);
        ++failures;
    }
}

static void chk_str(const char *name, const char *got, const char *want)
{
    if (got == NULL || want == NULL || strcmp(got, want) != 0) {
        printf("FAIL %s got=%s want=%s\n", name,
               got ? got : "(null)", want ? want : "(null)");
        ++failures;
    }
}

/* Fix 1: promoted anonymous bit-field designators. */
struct Version {
    union {
        struct {
            unsigned major : 4;
            unsigned minor : 4;
            unsigned patch : 8;
        };
    };
};

/* Fix 1: promoted bit-fields with out-of-declaration-order designators. */
struct Date {
    struct {
        unsigned day : 5;
        unsigned month : 4;
        unsigned year : 7;
    };
};

/* Fix 2: a promoted designator must resolve the anonymous struct member even
 * though a named scalar member is declared first in the union. */
union Reg {
    int all;
    struct {
        unsigned char lo;
        unsigned char hi;
    };
};

/* Fix 2: a designator naming a non-first named union member. */
union Sel {
    int i;
    char c;
};

/* Fix 3: nested anonymous aggregate initialized positionally. */
struct Event {
    char kind;
    union {
        struct {
            int lo;
            int hi;
        };
        long combined;
    };
};

/* Fix 3: struct with a braced anonymous-union sub-object followed by a
 * trailing scalar member. */
struct Message {
    union {
        const char *text;
        int raw;
    };
    int code;
};

int main(void)
{
    struct Version version = { .major = 2, .minor = 7, .patch = 15 };
    struct Date date = { .month = 6, .day = 17, .year = 42 };
    union Reg reg = { .hi = 3 };
    union Sel sel = { .c = 'Z' };
    struct Event event = { 'K', { { 4, 9 } } };
    struct Message message = { { .text = "OK" }, 404 };

    printf("version=%u.%u.%u\n", version.major, version.minor, version.patch);
    printf("date=%u/%u/%u\n", date.day, date.month, date.year);
    printf("reg=%u/%u/%d\n", reg.lo, reg.hi, reg.all);
    printf("sel=%c\n", sel.c);
    printf("event=%c/%d,%d\n", event.kind, event.lo, event.hi);
    printf("message=%s/%d\n", message.text, message.code);

    chk("version.major", version.major, 2);
    chk("version.minor", version.minor, 7);
    chk("version.patch", version.patch, 15);
    chk("date.day", date.day, 17);
    chk("date.month", date.month, 6);
    chk("date.year", date.year, 42);
    chk("reg.lo", reg.lo, 0);
    chk("reg.hi", reg.hi, 3);
    chk("reg.all", reg.all, 3 << 8);
    chk("sel.c", sel.c, 'Z');
    chk("event.kind", event.kind, 'K');
    chk("event.lo", event.lo, 4);
    chk("event.hi", event.hi, 9);
    chk_str("message.text", message.text, "OK");
    chk("message.code", message.code, 404);

    if (failures == 0)
        printf("anonymous init tests passed\n");
    return failures != 0;
}
