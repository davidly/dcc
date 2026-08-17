/* math.h - minimal dcc single-precision math declarations */

#ifndef _MATH_H
#define _MATH_H

/** Value returned on overflow; equals FLT_MAX (dcc has no double). */
#define HUGE_VAL 3.40282347e+38F
/** C99 float-flavored HUGE_VAL; true IEEE-754 infinity, unlike HUGE_VAL. */
#define HUGE_VALF INFINITY
/** Math errors are reported through errno. */
#define MATH_ERRNO 1
/** Floating-point exceptions are not available on CP/M. */
#define MATH_ERREXCEPT 2
/** dcc reports math errors through errno, not floating-point exceptions. */
#define math_errhandling MATH_ERRNO

/**
 * IEEE-754 positive infinity, as a genuine compile-time constant: dcc's
 * float-literal parser hands the source text to the host's atof(), and
 * 1e40 already exceeds FLT_MAX, so the host overflows it to (double)
 * infinity before it's narrowed to float. No runtime code, no RTL linkage;
 * usable anywhere a float constant expression is, including static
 * initializers.
 */
#define INFINITY 1e40F
/**
 * IEEE-754 quiet NaN. Unlike INFINITY, this has no spelling as a numeric
 * literal token, so it is a real extern float object (defined in
 * DCCRTL.MAC) rather than a constant expression: usable in ordinary
 * expressions and local initializers, but NOT in a static/global
 * initializer.
 */
extern const float dcc_nan;
#define NAN dcc_nan

/** True if x is a NaN (quiet or signaling). */
int isnan(float x);
/** True if x is +Inf or -Inf. */
int isinf(float x);
/** True if x is neither NaN nor +/-Inf. */
int isfinite(float x);
/** True if x's sign bit is set (negative, including -0.0 and -NaN). */
int signbit(float x);

/** Absolute value. */
float fabsf(float x);
/** Round toward negative infinity. */
float floorf(float x);
/** Round toward positive infinity. */
float ceilf(float x);
/** Square root. */
float sqrtf(float x);
/** Next representable value after x in the direction of y. */
float nextafterf(float x, float y);
/** Floating-point remainder of x / y. */
float fmodf(float x, float y);

/* exponential and logarithmic */
/** Base-e exponential, e raised to x. */
float expf(float x);
/** Natural logarithm, base e. */
float logf(float x);
/** Base-10 logarithm. */
float log10f(float x);
/** x raised to the power y. */
float powf(float x, float y);

/* trigonometric */
/** Sine of x, in radians. */
float sinf(float x);
/** Cosine of x, in radians. */
float cosf(float x);
/** Tangent of x, in radians. */
float tanf(float x);
/** Arc sine of x. */
float asinf(float x);
/** Arc cosine of x. */
float acosf(float x);
/** Arc tangent of x. */
float atanf(float x);
/** Arc tangent of y / x using the signs of both arguments. */
float atan2f(float y, float x);

/* hyperbolic */
/** Hyperbolic sine. */
float sinhf(float x);
/** Hyperbolic cosine. */
float coshf(float x);
/** Hyperbolic tangent. */
float tanhf(float x);

/* decomposition */
/** Split x into a normalized fraction and exponent. */
float frexpf(float x, int *eptr);
/** Compute x multiplied by 2 raised to n. */
float ldexpf(float x, int n);
/** Split x into integer and fractional parts. */
float modff(float x, float *iptr);

/*
 * C89 (4.5) spells these without the 'f' suffix and defines them on double.
 * dcc has no double (float is the only floating type), so the unsuffixed C89
 * names are provided as single-precision aliases of the 'f' variants above.
 * This lets portable C89 source that calls fabs/floor/ceil/sqrt/fmod (etc.)
 * compile and link against the single-precision runtime.
 */
/** Single-precision alias for fabsf. */
#define fabs(x)     fabsf(x)
/** Single-precision alias for floorf. */
#define floor(x)    floorf(x)
/** Single-precision alias for ceilf. */
#define ceil(x)     ceilf(x)
/** Single-precision alias for sqrtf. */
#define sqrt(x)     sqrtf(x)
/** Single-precision alias for fmodf. */
#define fmod(x, y)  fmodf((x), (y))

/** Single-precision alias for expf. */
#define exp(x)      expf(x)
/** Single-precision alias for logf. */
#define log(x)      logf(x)
/** Single-precision alias for log10f. */
#define log10(x)    log10f(x)
/** Single-precision alias for powf. */
#define pow(x, y)   powf((x), (y))

/** Single-precision alias for sinf. */
#define sin(x)      sinf(x)
/** Single-precision alias for cosf. */
#define cos(x)      cosf(x)
/** Single-precision alias for tanf. */
#define tan(x)      tanf(x)
/** Single-precision alias for asinf. */
#define asin(x)     asinf(x)
/** Single-precision alias for acosf. */
#define acos(x)     acosf(x)
/** Single-precision alias for atanf. */
#define atan(x)     atanf(x)
/** Single-precision alias for atan2f. */
#define atan2(y, x) atan2f((y), (x))

/** Single-precision alias for sinhf. */
#define sinh(x)     sinhf(x)
/** Single-precision alias for coshf. */
#define cosh(x)     coshf(x)
/** Single-precision alias for tanhf. */
#define tanh(x)     tanhf(x)

/** Single-precision alias for frexpf. */
#define frexp(x, e)  frexpf((x), (e))
/** Single-precision alias for ldexpf. */
#define ldexp(x, n)  ldexpf((x), (n))
/** Single-precision alias for modff. */
#define modf(x, ip)  modff((x), (ip))

#endif /* _MATH_H */
