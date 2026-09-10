/* Dedicated exact-matcher and near-match fixture. */
#include <stdio.h>

#ifdef SCOPE_RENAME_CHECK
#define chk scope_check
#endif
#ifdef SCOPE_RENAME_INCREMENT
#define plus_one scope_plus_one
#endif
#ifdef SCOPE_RENAME_FAILURE
#define fails scope_failures
#endif
#ifdef SCOPE_VOLATILE_REGISTER
#define register volatile
#endif
#ifdef SCOPE_UNSIGNED_LONG
#define long unsigned long
#endif
#ifdef SCOPE_EXTERNAL_STORAGE
#define static
#endif

#include "../tforsco.c"
