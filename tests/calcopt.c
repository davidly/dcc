/**
 * @file calcopt.c
 * @brief Assembly-assisted comparison variant of the CALC regression.
 *
 * @par Scenario
 * Builds the same parser and evaluator as calc.c while enabling the optional
 * Z80 kernels in calc1024.c, providing a behavioral and performance reference
 * for the portable generated-code path.
 *
 * @par Boundary
 * This wrapper owns no calculator behavior; all front-end logic remains in
 * calc.c and all conditional arithmetic kernels remain in calc1024.c.
 */
#include "calc.c"