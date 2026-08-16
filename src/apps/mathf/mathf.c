/**
 * @file mathf.c
 * @brief Preserves historical C implementations of dcc math routines.
 *
 * @par Role
 * Retains 32-bit-float reference implementations for exponential, logarithmic,
 * hyperbolic, trigonometric, inverse-trigonometric, and decomposition APIs.
 * The file has no main() or standalone I/O; it formerly generated routines
 * merged into DCCRTL.MAC.
 *
 * @par Key entry points
 * expf(), logf(), powf(), sinf(), cosf(), tanf(), atanf(), atan2f(), asinf(),
 * acosf(), frexpf(), ldexpf(), and modff().
 *
 * @par Boundary
 * This is historical reference material, not a runtime build input.
 * DCCRTL.MAC is the hand-maintained source of truth and contains fixes absent
 * here; tests/tmathf.c validates the active runtime implementations.
 */

#include <math.h>

/* Bit view of a float for frexpf/ldexpf.  Both members are 32-bit and stored
 * little-endian, so the union aliases the IEEE-754 single-precision bits. */
typedef union {
    float f;
    unsigned long u;
} fbits_t;

/* ---- exponential and logarithm ------------------------------------ */

/* expf: e^x = 2^n * e^r, with n chosen so r = x - n*ln2 is small, then a
 * 6-term Taylor series for e^r and ldexpf for the 2^n scaling. */
float expf(float x)
{
    float t;
    long n;
    float r;
    float r2;
    float p;

    if (x > 88.0f)  return 3.4e38f;     /* overflow -> ~FLT_MAX */
    if (x < -88.0f) return 0.0f;        /* underflow */

    t = x * 1.44269504f;                /* x / ln2 */
    if (t >= 0.0f) n = (long)(t + 0.5f);
    else           n = (long)(t - 0.5f);

    r = x - (float)n * 0.69314718f;     /* r in [-ln2/2, ln2/2] */
    r2 = r * r;
    /* 1 + r + r2/2 + r3/6 + r4/24 + r5/120 (Horner) */
    p = 1.0f + r + r2 * (0.5f + r * (0.16666667f
        + r * (0.04166667f + r * (0.00833333f + r * 0.00138889f))));

    return ldexpf(p, (int)n);
}

/* logf: natural log.  x = m * 2^e with m in [sqrt(0.5), sqrt(2)); then
 * log(m) = 2*(s + s^3/3 + s^5/5 + ...) with s = (m-1)/(m+1). */
float logf(float x)
{
    int e;
    float m;
    float s;
    float s2;
    float poly;

    if (x <= 0.0f) return -3.4e38f;     /* domain error -> -inf-ish */

    m = frexpf(x, &e);                  /* m in [0.5,1), x = m*2^e */
    /* shift m into [sqrt(0.5), sqrt(2)) to speed series convergence */
    if (m < 0.70710678f) {
        m = m * 2.0f;
        e = e - 1;
    }

    s = (m - 1.0f) / (m + 1.0f);
    s2 = s * s;
    poly = 2.0f * s * (1.0f + s2 * (0.33333333f
        + s2 * (0.2f + s2 * (0.14285714f + s2 * 0.11111111f))));

    return (float)e * 0.69314718f + poly;
}

/* log10f: log10(x) = ln(x) / ln(10) */
float log10f(float x)
{
    return logf(x) * 0.43429448f;
}

/* powf: x^y.  General case via exp(y*log(x)); handles x==0, y==0 and the
 * negative-base/integer-exponent case. */
float powf(float x, float y)
{
    float ay;
    long iy;
    float fy;
    float r;

    if (y == 0.0f) return 1.0f;
    if (x == 0.0f) return 0.0f;

    if (x > 0.0f)
        return expf(y * logf(x));

    /* x < 0: defined only when y is an integer */
    ay = (y < 0.0f) ? -y : y;
    iy = (long)ay;
    fy = ay - (float)iy;
    if (fy != 0.0f) return 0.0f;        /* non-integer exponent of negative base */

    r = expf(y * logf(-x));
    if ((iy & 1L) != 0L) r = -r;        /* odd power keeps the sign */
    return r;
}

/* ---- hyperbolic ---------------------------------------------------- */

float sinhf(float x)
{
    float ex;
    float enx;
    ex = expf(x);
    enx = expf(-x);
    return (ex - enx) * 0.5f;
}

float coshf(float x)
{
    float ex;
    float enx;
    ex = expf(x);
    enx = expf(-x);
    return (ex + enx) * 0.5f;
}

float tanhf(float x)
{
    float e2;
    if (x > 15.0f)  return 1.0f;        /* saturates within float precision */
    if (x < -15.0f) return -1.0f;
    e2 = expf(2.0f * x);
    return (e2 - 1.0f) / (e2 + 1.0f);
}

/* ---- trigonometric (verified polynomials from trig.c) -------------- */

float sinf(float x)
{
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318531f;
    float x2;
    float result;

    x = fmodf(x, TWO_PI);
    if (x > PI)  x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    if (x > 1.57079632f)       x = PI - x;
    else if (x < -1.57079632f) x = -PI - x;

    x2 = x * x;
    result = x + x * x2 * (-0.16665724f + x2 * (0.00831348f + x2 * (-0.00018521f)));
    return result;
}

float cosf(float x)
{
    const float PI = 3.14159265f;
    const float TWO_PI = 6.28318531f;
    float sign = 1.0f;
    float x2;
    float result;

    x = fmodf(x, TWO_PI);
    if (x > PI)  x -= TWO_PI;
    if (x < -PI) x += TWO_PI;

    if (x > 1.57079632f) {
        x = PI - x;
        sign = -1.0f;
    } else if (x < -1.57079632f) {
        x = -PI - x;
        sign = -1.0f;
    }

    x2 = x * x;
    result = 1.0f + x2 * (-0.49999286f + x2 * (0.04163351f + x2 * (-0.00136122f)));
    return sign * result;
}

float tanf(float x)
{
    const float PI = 3.14159265f;
    const float HALF_PI = 1.57079632f;
    const float QUARTER_PI = 0.78539816f;
    float xr;
    int invert = 0;
    float x2;
    float num;
    float den;
    float result;

    xr = fmodf(x, PI);
    if (xr > HALF_PI)       xr -= PI;
    else if (xr < -HALF_PI) xr += PI;

    if (xr > QUARTER_PI) {
        xr = HALF_PI - xr;
        invert = 1;
    } else if (xr < -QUARTER_PI) {
        xr = -HALF_PI - xr;
        invert = 1;
    }

    x2 = xr * xr;
    num = xr * (15.0f - x2);
    den = 15.0f - 6.0f * x2;
    if (den == 0.0f) return 0.0f;
    result = num / den;

    if (invert) {
        if (result == 0.0f) return 0.0f;
        result = 1.0f / result;
    }
    return result;
}

/* ---- inverse trigonometric ----------------------------------------- */

float atanf(float x)
{
    float sign = 1.0f;
    float bias = 0.0f;
    float x2;
    float poly;

    if (x < 0.0f) {
        sign = -1.0f;
        x = -x;
    }

    if (x > 2.41421356f) {
        bias = 1.57079632f;
        x = -1.0f / x;
    } else if (x > 0.41421356f) {
        bias = 0.78539816f;
        x = (x - 1.0f) / (x + 1.0f);
    }

    x2 = x * x;
    poly = x + x * x2 * (-0.33333145f + x2 * (0.19993550f
        + x2 * (-0.14208899f + x2 * 0.10656263f)));

    return sign * (bias + poly);
}

float atan2f(float y, float x)
{
    const float PI = 3.14159265f;
    const float HALF_PI = 1.57079632f;
    float ratio;

    if (x == 0.0f) {
        if (y > 0.0f) return  HALF_PI;
        if (y < 0.0f) return -HALF_PI;
        return 0.0f;
    }

    ratio = y / x;
    if (x > 0.0f)
        return atanf(ratio);
    if (y >= 0.0f)
        return atanf(ratio) + PI;
    return atanf(ratio) - PI;
}

float asinf(float x)
{
    const float HALF_PI = 1.57079632f;
    float sign = 1.0f;
    float x2;
    float poly;

    if (x < 0.0f) {
        sign = -1.0f;
        x = -x;
    }
    if (x > 1.0f) return 0.0f;

    if (x > 0.5f) {
        float transform = sqrtf(0.5f * (1.0f - x));
        float sub_asin = asinf(transform);
        return sign * (HALF_PI - 2.0f * sub_asin);
    }

    x2 = x * x;
    poly = x + x * x2 * (0.16666752f + x2 * (0.07495300f
        + x2 * (0.04547002f + x2 * 0.02417036f)));
    return sign * poly;
}

float acosf(float x)
{
    const float HALF_PI = 1.57079632f;
    return HALF_PI - asinf(x);
}

/* ---- decomposition: frexpf / ldexpf / modff ------------------------ */

/* frexpf: x = m * 2^*eptr with 0.5 <= |m| < 1 (or m == 0). */
float frexpf(float x, int *eptr)
{
    fbits_t v;
    int exp;

    if (x == 0.0f) {
        *eptr = 0;
        return 0.0f;
    }

    v.f = x;
    exp = (int)((v.u >> 23) & 0xFFUL);
    *eptr = exp - 126;
    /* force the biased exponent to 126 so the value lands in [0.5,1) */
    v.u = (v.u & 0x807FFFFFUL) | (126UL << 23);
    return v.f;
}

/* ldexpf: x * 2^n, by adjusting the IEEE biased exponent directly. */
float ldexpf(float x, int n)
{
    fbits_t v;
    int exp;

    if (x == 0.0f) return 0.0f;

    v.f = x;
    exp = (int)((v.u >> 23) & 0xFFUL);
    exp += n;

    if (exp >= 255) return (x < 0.0f) ? -3.4e38f : 3.4e38f;  /* overflow */
    if (exp <= 0)   return 0.0f;                              /* underflow */

    v.u = (v.u & 0x807FFFFFUL) | ((unsigned long)exp << 23);
    return v.f;
}

/* modff: split into integer (truncated toward zero) and fractional parts. */
float modff(float x, float *iptr)
{
    float ip;
    if (x >= 0.0f) ip = floorf(x);
    else           ip = ceilf(x);
    *iptr = ip;
    return x - ip;
}
