/*
 * Regression guard (compile-fail): a #pragma once inside #if 0 is dead code and
 * must be IGNORED, so pragma-once-dead.h is expanded on both includes and
 * PODC_VAL becomes 2.  The #error below then fires - that diagnostic is the
 * expected result recorded in the baseline.
 *
 * If a regression makes dcc honor the dead #pragma once, the second include is
 * skipped, PODC_VAL stays 1, the #error does not fire, dcc compiles cleanly and
 * exits 0, and the diagnostics harness reports "expected compile failure, got
 * success".
 */
#include "pragma-once-dead.h"
#include "pragma-once-dead.h"

#if PODC_VAL == 2
#error dead-code pragma once was correctly ignored
#endif

int main(void)
{
    return 0;
}
