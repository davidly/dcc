/* #pragma once under the live #else branch of an undefined #if must be honored. */
#if PODC_NOT_DEFINED
#else
#pragma once
#endif

#ifdef PODC_ELSE_SEEN
#undef PODC_ELSE_VAL
#define PODC_ELSE_VAL 2
#else
#define PODC_ELSE_SEEN
#define PODC_ELSE_VAL 1
#endif
