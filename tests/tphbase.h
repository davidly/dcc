#ifndef TPHBASE_H
#define TPHBASE_H

/* Old portability headers commonly hide storage and prototype syntax behind
 * macros selected by a deep platform tree. */
#if defined(_DCC_)
# define TPH_NEAR
# define TPH_PROTO(args) args
#elif defined(__STDC__)
# define TPH_NEAR
# define TPH_PROTO(args) args
#else
# define TPH_NEAR
# define TPH_PROTO(args) ()
#endif

#define TPH_JOIN_RAW(a, b) a##b
#define TPH_JOIN(a, b) TPH_JOIN_RAW(a, b)
#define TPH_TEXT_RAW(x) #x
#define TPH_TEXT(x) TPH_TEXT_RAW(x)

#endif
