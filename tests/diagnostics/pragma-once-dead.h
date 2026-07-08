/* Dead-code #pragma once helper for pragma-once-dead-code.c.
 *
 * The #pragma once below is inside #if 0 (dead code) and must be ignored, so
 * this header is expanded on every include.  It only toggles macros, so it is
 * safe to include more than once.  Each expansion advances PODC_VAL 1 -> 2. */
#if 0
#pragma once
#endif

#ifdef PODC_SEEN
#undef PODC_VAL
#define PODC_VAL 2
#else
#define PODC_SEEN
#define PODC_VAL 1
#endif
