#ifndef TPHMAC_H
#define TPHMAC_H

#include "tphbase.h"

#define TPH_VERSION_MAJOR 3
#define TPH_VERSION_MINOR 7
#define TPH_VERSION ((TPH_VERSION_MAJOR * 10) + TPH_VERSION_MINOR)

#if defined(TPH_VERSION_MAJOR) && \
    ((TPH_VERSION_MAJOR << 4) + TPH_VERSION_MINOR == 55)
# define TPH_CONFIGURED 1
#else
# define TPH_CONFIGURED 0
#endif

/* The inactive arm is deliberately not valid C. A portability preprocessor
 * must discard it without feeding its tokens to the parser. */
#if 0
this is not a declaration +++ {{{
#endif

#define TPH_SECOND(a, b) b
#define TPH_APPLY(fn, args) fn args
#define TPH_DECLARE(name) static int TPH_JOIN(tph_value_, name)

int tph_old_sum TPH_PROTO((int left, int right));

#endif
