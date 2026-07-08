/* defined(NAME) after an active #define: #pragma once must be honored. */
#define POM_DEFINED 1
#if defined(POM_DEFINED)
#pragma once
#endif

#ifdef POM_DEFINED_SEEN
#undef POM_DEFINED_VAL
#define POM_DEFINED_VAL 2
#else
#define POM_DEFINED_SEEN
#define POM_DEFINED_VAL 1
#endif
