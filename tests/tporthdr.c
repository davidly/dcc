/* Dense old-style portability headers: include guards, nested expansion,
 * token pasting, stringification, empty qualifiers, and K&R compatibility. */
#include <stdio.h>
#include <string.h>
#include "tphmac.h"
#include "tphmac.h"

TPH_DECLARE(alpha) = 11;

int tph_old_sum(left, right)
int left;
int right;
{
    return left + right;
}

static int tph_increment(int value)
{
    return value + 1;
}

int main(void)
{
    int (*function_name)(int);
    int nested;
    const char *version_text;

    function_name = tph_increment;
    nested = TPH_SECOND((tph_old_sum(1, 2), 4),
                        TPH_APPLY(tph_old_sum, (5, 6)));
    version_text = TPH_TEXT(TPH_VERSION_MAJOR);

    if (!TPH_CONFIGURED || TPH_VERSION != 37 ||
        tph_value_alpha != 11 || nested != 11 ||
        function_name(8) != 9 || strcmp(version_text, "3") != 0) {
        printf("tporthdr failed\n");
        return 1;
    }
    printf("tporthdr completed with great success\n");
    return 0;
}
