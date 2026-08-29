/* Multi-file linkage: both helper units deliberately reuse private names,
 * while long public function names exercise consistent cross-unit shortening. */
#include <stdio.h>
#include "tmfstat.h"

int main(void)
{
    int alpha;
    int beta;

    mf_tentative_value = 4;
    alpha = alpha_translation_unit_long_name(2);
    beta = beta_translation_unit_long_name(3);

    if (mf_initialized_value != 20 || mf_tentative_value != 4 ||
        alpha != 27 || beta != 16) {
        printf("tmfstat failed: %d %d %d %d\n",
               mf_initialized_value, mf_tentative_value, alpha, beta);
        return 1;
    }
    printf("tmfstat completed with great success\n");
    return 0;
}
