#include <stdio.h>

struct UnbitAccess {
    unsigned read : 1;
    unsigned : 5;
    unsigned exec : 1;
};

struct UnbitMixed {
    unsigned low : 1, : 5, high : 1;
};

struct UnbitLead {
    unsigned : 3;
    unsigned tag : 2;
};

struct UnbitSplit {
    unsigned a : 4;
    unsigned : 0;
    unsigned b : 4;
};

struct UnbitTrail {
    unsigned flag : 1;
    unsigned : 5;
    int value;
};

struct UnbitSigned {
    int a : 3;
    int : 2;
    int b : 3;
};

#ifdef UNBITFIELD_RENAMED
#define UNBITFIELD_REPORT renamed_unnamed_bitfield_report
#else
#define UNBITFIELD_REPORT unnamed_bitfield_report
#endif

#ifdef UNBITFIELD_VOLATILE
#define UNBITFIELD_LOCAL volatile
#else
#define UNBITFIELD_LOCAL
#endif

static int UNBITFIELD_REPORT(void)
{
    UNBITFIELD_LOCAL struct UnbitAccess access = { 1, 1 };
    UNBITFIELD_LOCAL struct UnbitMixed mixed = { 1, 1 };
    UNBITFIELD_LOCAL struct UnbitLead lead = { 3 };
    UNBITFIELD_LOCAL struct UnbitSplit split = { 5, 6 };
    UNBITFIELD_LOCAL struct UnbitTrail trail = { 1, 4242 };
    UNBITFIELD_LOCAL struct UnbitSigned sgn = { 1, -1 };

    printf("access=%d,%d size=%d\n", access.read, access.exec,
           (int)sizeof(access));
    printf("mixed=%d,%d size=%d\n", mixed.low, mixed.high,
           (int)sizeof(mixed));
    printf("lead=%d size=%d\n", lead.tag, (int)sizeof(lead));
    printf("split=%d,%d size=%d\n", split.a, split.b,
           (int)sizeof(split));
    printf("trail=%d,%d size=%d\n", trail.flag, trail.value,
           (int)sizeof(trail));
    printf("signed=%d,%d size=%d\n", sgn.a, sgn.b,
           (int)sizeof(sgn));
#ifdef UNBITFIELD_EXTRA_CFG
    if (access.read == 9)
        return 1;
#endif
    return 0;
}

int main(void)
{
    struct UnbitAccess access = { 1, 1 };
    struct UnbitMixed mixed = { 1, 1 };
    struct UnbitLead lead = { 3 };
    struct UnbitSplit split = { 5, 6 };
    struct UnbitTrail trail = { 1, 4242 };
    struct UnbitSigned sgn = { 1, -1 };
    int failures = 0;

    failures += access.read != 1 || access.exec != 1 ||
        sizeof(access) != 2;
    failures += mixed.low != 1 || mixed.high != 1 ||
        sizeof(mixed) != 2;
    failures += lead.tag != 3 || sizeof(lead) != 2;
    failures += split.a != 5 || split.b != 6 || sizeof(split) != 4;
    failures += trail.flag != 1 || trail.value != 4242 ||
        sizeof(trail) != 4;
    failures += sgn.a != 1 || sgn.b != -1 || sizeof(sgn) != 2;
    failures += UNBITFIELD_REPORT() != 0;
    printf("unnamed bitfield failures=%d\n", failures);
    return failures != 0;
}
