/* #pragma once under an inactive #ifdef must be ignored. */
#ifdef PODC_NOT_DEFINED
#pragma once
#endif

#ifdef PODC_IFDEF_SEEN
#undef PODC_IFDEF_VAL
#define PODC_IFDEF_VAL 2
#else
#define PODC_IFDEF_SEEN
#define PODC_IFDEF_VAL 1
#endif
