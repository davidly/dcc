/* sizeof(type) inside folded logical conditions.
 * Adapted from SDCC's GCC torture execute test pr44468. */
#include <stddef.h>
#include <stdio.h>

struct Inner { int left; int right; };
struct Outer { int prefix; struct Inner inner; };

int main(void)
{
    int failures = 0;

    if (!(sizeof(char) == 1 && sizeof(int) >= 2))
        ++failures;
    if (sizeof(char) != 1 || offsetof(struct Outer, inner) != sizeof(int))
        ++failures;
    if (!(sizeof(long) >= sizeof(int) &&
          offsetof(struct Inner, right) == sizeof(int)))
        ++failures;
    if ((sizeof(char) == 2 || sizeof(int) == 1) && sizeof(long) == 1)
        ++failures;

    if (failures) {
        printf("tszlogic failed: %d\n", failures);
        return 1;
    }
    printf("tszlogic completed with great success\n");
    return 0;
}
