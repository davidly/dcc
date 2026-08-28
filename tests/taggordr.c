/* Aggregate type specifiers followed by storage classes or qualifiers.
 * Adapted from SDCC's GCC torture execute test 960326-1. */
#include <stdio.h>

struct Record {
    char a;
    int b;
    short c;
    int values[3];
    long tail;
};

struct Record static ordered = { .b = 3, .values = { 2 } };
struct Record const qualified = { 1, 4, 5, { 6, 7, 8 }, 9L };
struct { int left; int right; } static anonymous_ordered = { 10, 11 };

int main(void)
{
    int failures = 0;

    if (ordered.a != 0 || ordered.b != 3 || ordered.c != 0 ||
        ordered.values[0] != 2 || ordered.values[1] != 0 ||
        ordered.values[2] != 0 || ordered.tail != 0L)
        ++failures;

    if (qualified.a != 1 || qualified.b != 4 || qualified.c != 5 ||
        qualified.values[0] != 6 || qualified.values[1] != 7 ||
        qualified.values[2] != 8 || qualified.tail != 9L)
        ++failures;

    if (anonymous_ordered.left != 10 || anonymous_ordered.right != 11)
        ++failures;

    if (failures) {
        printf("taggordr failed: %d\n", failures);
        return 1;
    }
    printf("taggordr completed with great success\n");
    return 0;
}
