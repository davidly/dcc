/* Macro-defined true condition: #pragma once must be honored. */
#define POM_ON 1
#if POM_ON
#pragma once
#endif

#ifdef POM_ON_SEEN
#undef POM_ON_VAL
#define POM_ON_VAL 2
#else
#define POM_ON_SEEN
#define POM_ON_VAL 1
#endif
