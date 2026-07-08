/*
 * Regression guard (compile-fail): include-splicer #pragma once conditionals
 * must see active #define/#undef directives encountered earlier in the same
 * source/header stream.
 *
 * Correct outcomes after each header is included twice:
 * - macro-on:      #define POM_ON 1 -> #if POM_ON is live -> pragma honored -> 1
 * - macro-off:     #define POM_OFF 0 -> #if POM_OFF is dead -> pragma ignored -> 2
 * - macro-undef:   #undef before #if -> condition false -> pragma ignored -> 2
 * - macro-defined: #if defined(POM_DEFINED) is live -> pragma honored -> 1
 */
#include "pragma-once-macro-on.h"
#include "pragma-once-macro-on.h"
#include "pragma-once-macro-off.h"
#include "pragma-once-macro-off.h"
#include "pragma-once-macro-undef.h"
#include "pragma-once-macro-undef.h"
#include "pragma-once-macro-defined.h"
#include "pragma-once-macro-defined.h"

#if POM_ON_VAL == 1 && POM_OFF_VAL == 2 && POM_UNDEF_VAL == 2 && POM_DEFINED_VAL == 1
#error macro-controlled pragma once conditions were handled correctly
#endif

int main(void)
{
    return 0;
}
