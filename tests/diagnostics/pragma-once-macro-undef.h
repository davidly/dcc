/* Macro undefined before the condition: #pragma once must be ignored. */
#define POM_UNDEF 1
#undef POM_UNDEF
#if POM_UNDEF
#pragma once
#endif

#ifdef POM_UNDEF_SEEN
#undef POM_UNDEF_VAL
#define POM_UNDEF_VAL 2
#else
#define POM_UNDEF_SEEN
#define POM_UNDEF_VAL 1
#endif
