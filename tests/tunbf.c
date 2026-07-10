/* tunbf.c - unnamed (padding) bit-fields.
 *
 * Standard C allows a bit-field with no declarator, e.g. `unsigned : 5;`, to
 * reserve bits for layout/padding without creating an addressable member, and a
 * zero-width `unsigned : 0;` to force the next bit-field into a fresh storage
 * unit.  dcc previously rejected both.  These structs are accepted by
 * clang -std=c89; the values below are dcc's 16-bit int-unit reference output
 * (dcc packs bit-fields into 2-byte units, so sizeof differs from a 32-bit
 * host but the field values and relative layout match).
 */
#include <stdio.h>

/* unnamed padding between two named fields, all in one unit */
struct Access { unsigned read : 1; unsigned : 5; unsigned exec : 1; };

/* unnamed field expressed inside a comma-separated declarator list */
struct Mixed { unsigned low : 1, : 5, high : 1; };

/* unnamed field at the very start of the struct */
struct Lead { unsigned : 3; unsigned tag : 2; };

/* zero-width unnamed field forces `b` into a new storage unit */
struct Split { unsigned a : 4; unsigned : 0; unsigned b : 4; };

/* unnamed padding followed by an ordinary (non bit-field) member */
struct Trail { unsigned flag : 1; unsigned : 5; int value; };

/* signed unnamed padding, signed named fields (sign-extension on read) */
struct Signed { int a : 3; int : 2; int b : 3; };

int main(void)
{
    struct Access access = { 1, 1 };
    struct Mixed mixed = { 1, 1 };
    struct Lead lead = { 3 };
    struct Split split = { 5, 6 };
    struct Trail trail = { 1, 4242 };
    struct Signed sgn = { 1, -1 };

    printf("access=%d,%d size=%d\n", access.read, access.exec,
           (int)sizeof(access));
    printf("mixed=%d,%d size=%d\n", mixed.low, mixed.high,
           (int)sizeof(mixed));
    printf("lead=%d size=%d\n", lead.tag, (int)sizeof(lead));
    printf("split=%d,%d size=%d\n", split.a, split.b, (int)sizeof(split));
    printf("trail=%d,%d size=%d\n", trail.flag, trail.value,
           (int)sizeof(trail));
    printf("signed=%d,%d size=%d\n", sgn.a, sgn.b, (int)sizeof(sgn));
    return 0;
}
