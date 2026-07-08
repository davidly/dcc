/*
 * Regression guard (compile-fail): conditional tracking in the include splicer
 * must match preprocessing closely enough for #pragma once.
 *
 * - pragma-once-ifdef-dead.h puts #pragma once under an inactive #ifdef; it
 *   must be ignored, so including the header twice makes PODC_IFDEF_VAL == 2.
 * - pragma-once-else-live.h puts #pragma once under the live #else branch after
 *   an undefined #if; it must be honored, so including the header twice leaves
 *   PODC_ELSE_VAL == 1.
 *
 * Correct behavior fires the #error below. A regression in either edge case
 * makes the expression false, dcc exits 0, and the diagnostics harness reports
 * "expected compile failure, got success".
 */
#include "pragma-once-ifdef-dead.h"
#include "pragma-once-ifdef-dead.h"
#include "pragma-once-else-live.h"
#include "pragma-once-else-live.h"

#if PODC_IFDEF_VAL == 2 && PODC_ELSE_VAL == 1
#error conditional pragma once edges were handled correctly
#endif

int main(void)
{
    return 0;
}
