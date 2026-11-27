/* Compatibility for z88dk's missing nextafterf and FLT_MAX definitions. */
#ifndef BENCHMARK_NEXTAFTER_H
#define BENCHMARK_NEXTAFTER_H
#ifndef FLT_MAX
#ifdef SDCC
#define FLT_MAX 3.4028234664e38f
#else
/* Direction only: larger than any value used by pihex. */
#define FLT_MAX 1.0e37f
#endif
#endif
#define fmodf fmod
#ifdef SDCC
/* Math32 uses IEEE binary32. Step by exactly one representable value;
 * unlike the former relative-epsilon approximation, this preserves the
 * termination condition used in dcc/tests/pihex.c.
 */
float benchmark_nextafterf(float x, float y)
{
    union { float f; uint32_t bits; } value, target;
    uint32_t magnitude, target_magnitude;
    value.f = x; target.f = y;
    magnitude = value.bits & 0x7fffffffUL;
    target_magnitude = target.bits & 0x7fffffffUL;
    /* Integer comparisons also handle subnormals: math32's floating
     * comparisons can treat the smallest subnormal values as zero. */
    if (magnitude > 0x7f800000UL) {
        value.bits |= 0x00400000UL;
        return value.f;
    }
    if (target_magnitude > 0x7f800000UL) {
        target.bits |= 0x00400000UL;
        return target.f;
    }
    if (value.bits == target.bits || (magnitude == 0 && target_magnitude == 0))
        return y;
    if (magnitude == 0)
        value.bits = (target.bits & 0x80000000UL) | 1UL;
    else if (((value.bits ^ target.bits) & 0x80000000UL) != 0)
        --value.bits;
    else if (value.bits < target.bits)
        ++value.bits;
    else
        --value.bits;
    return value.f;
}
#else
/* sccz80's math48 nextafter entry points are unimplemented in this
 * installation. Retain the existing approximation for this backend only.
 */
float benchmark_nextafterf(float x, float y)
{
    float abs_x, epsilon;
    if (x == y) return y;
    abs_x = x < 0.0f ? -x : x;
    epsilon = abs_x * 1e-6f;
    if (epsilon == 0.0f) epsilon = 1e-37f;
    return x < y ? x + epsilon : x - epsilon;
}
#endif
#define nextafterf benchmark_nextafterf
#endif
