/* Macro-defined false condition: #pragma once must be ignored. */
#define POM_OFF 0
#if POM_OFF
#pragma once
#endif

#ifdef POM_OFF_SEEN
#undef POM_OFF_VAL
#define POM_OFF_VAL 2
#else
#define POM_OFF_SEEN
#define POM_OFF_VAL 1
#endif
