#ifndef _ASSERT_H
#define _ASSERT_H

/** C11 spelling for the core _Static_assert declaration. */
#define static_assert _Static_assert

#ifdef NDEBUG
/** Check expression unless assertions are disabled by NDEBUG. */
#define assert(expression) ((void)0)
#else

/* Failure handler in DCCRTL.MAC: prints the diagnostic and aborts.
 * C name _asfl -> assembly label __asfl (dcc prepends one '_').
 * Only linked when assert() is used with NDEBUG not defined. */
extern _Noreturn void _asfl(const char *msg);

/* Internal helpers for turning __LINE__ into a string literal. */
#define STR_HELPER(x) #x
#define STR_CONVERT(x) STR_HELPER(x)

/** Verify expression; on failure print a diagnostic and abort(). */
#define assert(expression) \
    ((expression) ? (void)0 \
                  : _asfl(#expression " at " __FILE__ ":" STR_CONVERT(__LINE__)))

#endif /* NDEBUG */

#endif /* _ASSERT_H */
