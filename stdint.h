#ifndef H_STDINT
#define H_STDINT

/** Exact-width signed 8-bit integer type. */
typedef signed char int8_t;
/** Exact-width unsigned 8-bit integer type. */
typedef unsigned char uint8_t;
/** Exact-width signed 16-bit integer type. */
typedef signed int int16_t;
/** Exact-width unsigned 16-bit integer type. */
typedef unsigned int uint16_t;
/** Exact-width signed 32-bit integer type. */
typedef signed long int32_t;
/** Exact-width unsigned 32-bit integer type. */
typedef unsigned long uint32_t;

/** Smallest signed integer type with at least 8 bits. */
typedef signed char int_least8_t;
/** Smallest unsigned integer type with at least 8 bits. */
typedef unsigned char uint_least8_t;
/** Smallest signed integer type with at least 16 bits. */
typedef signed int int_least16_t;
/** Smallest unsigned integer type with at least 16 bits. */
typedef unsigned int uint_least16_t;
/** Smallest signed integer type with at least 32 bits. */
typedef signed long int_least32_t;
/** Smallest unsigned integer type with at least 32 bits. */
typedef unsigned long uint_least32_t;

/** Signed integer type at least 8 bits wide that the Z80 operates on fastest (8-bit is native). */
typedef signed char int_fast8_t;
/** Unsigned integer type at least 8 bits wide that the Z80 operates on fastest (8-bit is native). */
typedef unsigned char uint_fast8_t;
/** Signed integer type at least 16 bits wide that the Z80 operates on fastest (16-bit is the natural word). */
typedef signed int int_fast16_t;
/** Unsigned integer type at least 16 bits wide that the Z80 operates on fastest (16-bit is the natural word). */
typedef unsigned int uint_fast16_t;
/** Signed integer type at least 32 bits wide that the Z80 operates on fastest (long is the widest integer). */
typedef signed long int_fast32_t;
/** Unsigned integer type at least 32 bits wide that the Z80 operates on fastest (long is the widest integer). */
typedef unsigned long uint_fast32_t;

/** Signed integer type capable of holding a pointer (16-bit target). */
typedef int intptr_t;
/** Unsigned integer type capable of holding a pointer (16-bit target). */
typedef unsigned int uintptr_t;

/** Maximum-width signed integer type supported by the runtime. */
typedef signed long intmax_t;
/** Maximum-width unsigned integer type supported by the runtime. */
typedef unsigned long uintmax_t;

#ifndef INT8_MIN
/** Minimum value of int8_t. */
#define INT8_MIN (-128)
#endif
#ifndef INT8_MAX
/** Maximum value of int8_t. */
#define INT8_MAX 127
#endif
#ifndef UINT8_MAX
/** Maximum value of uint8_t (promotes to int). */
#define UINT8_MAX 255
#endif
#ifndef INT16_MIN
/** Minimum value of int16_t. */
#define INT16_MIN (-32767 - 1)
#endif
#ifndef INT16_MAX
/** Maximum value of int16_t. */
#define INT16_MAX 32767
#endif
#ifndef UINT16_MAX
/** Maximum value of uint16_t. */
#define UINT16_MAX 65535U
#endif
#ifndef INT32_MIN
/** Minimum value of int32_t. */
#define INT32_MIN (-2147483647L - 1L)
#endif
#ifndef INT32_MAX
/** Maximum value of int32_t. */
#define INT32_MAX 2147483647L
#endif
#ifndef UINT32_MAX
/** Maximum value of uint32_t. */
#define UINT32_MAX 4294967295UL
#endif

#ifndef INT_LEAST8_MIN
/** Minimum value of int_least8_t. */
#define INT_LEAST8_MIN INT8_MIN
#endif
#ifndef INT_LEAST8_MAX
/** Maximum value of int_least8_t. */
#define INT_LEAST8_MAX INT8_MAX
#endif
#ifndef UINT_LEAST8_MAX
/** Maximum value of uint_least8_t. */
#define UINT_LEAST8_MAX UINT8_MAX
#endif
#ifndef INT_LEAST16_MIN
/** Minimum value of int_least16_t. */
#define INT_LEAST16_MIN INT16_MIN
#endif
#ifndef INT_LEAST16_MAX
/** Maximum value of int_least16_t. */
#define INT_LEAST16_MAX INT16_MAX
#endif
#ifndef UINT_LEAST16_MAX
/** Maximum value of uint_least16_t. */
#define UINT_LEAST16_MAX UINT16_MAX
#endif
#ifndef INT_LEAST32_MIN
/** Minimum value of int_least32_t. */
#define INT_LEAST32_MIN (-2147483647L - 1L)
#endif
#ifndef INT_LEAST32_MAX
/** Maximum value of int_least32_t. */
#define INT_LEAST32_MAX 2147483647L
#endif
#ifndef UINT_LEAST32_MAX
/** Maximum value of uint_least32_t. */
#define UINT_LEAST32_MAX 4294967295UL
#endif

#ifndef INT_FAST8_MIN
/** Minimum value of int_fast8_t. */
#define INT_FAST8_MIN INT8_MIN
#endif
#ifndef INT_FAST8_MAX
/** Maximum value of int_fast8_t. */
#define INT_FAST8_MAX INT8_MAX
#endif
#ifndef UINT_FAST8_MAX
/** Maximum value of uint_fast8_t. */
#define UINT_FAST8_MAX UINT8_MAX
#endif
#ifndef INT_FAST16_MIN
/** Minimum value of int_fast16_t. */
#define INT_FAST16_MIN INT16_MIN
#endif
#ifndef INT_FAST16_MAX
/** Maximum value of int_fast16_t. */
#define INT_FAST16_MAX INT16_MAX
#endif
#ifndef UINT_FAST16_MAX
/** Maximum value of uint_fast16_t. */
#define UINT_FAST16_MAX UINT16_MAX
#endif
#ifndef INT_FAST32_MIN
/** Minimum value of int_fast32_t. */
#define INT_FAST32_MIN (-2147483647L - 1L)
#endif
#ifndef INT_FAST32_MAX
/** Maximum value of int_fast32_t. */
#define INT_FAST32_MAX 2147483647L
#endif
#ifndef UINT_FAST32_MAX
/** Maximum value of uint_fast32_t. */
#define UINT_FAST32_MAX 4294967295UL
#endif

#ifndef INTPTR_MIN
/** Minimum value of intptr_t. */
#define INTPTR_MIN INT16_MIN
#endif
#ifndef INTPTR_MAX
/** Maximum value of intptr_t. */
#define INTPTR_MAX INT16_MAX
#endif
#ifndef UINTPTR_MAX
/** Maximum value of uintptr_t. */
#define UINTPTR_MAX UINT16_MAX
#endif
#ifndef INTMAX_MIN
/** Minimum value of intmax_t. */
#define INTMAX_MIN (-2147483647L - 1L)
#endif
#ifndef INTMAX_MAX
/** Maximum value of intmax_t. */
#define INTMAX_MAX 2147483647L
#endif
#ifndef UINTMAX_MAX
/** Maximum value of uintmax_t. */
#define UINTMAX_MAX 4294967295UL
#endif

#ifndef PTRDIFF_MIN
/** Minimum value of ptrdiff_t. */
#define PTRDIFF_MIN (-32767 - 1)
#endif
#ifndef PTRDIFF_MAX
/** Maximum value of ptrdiff_t. */
#define PTRDIFF_MAX 32767
#endif
#ifndef SIZE_MAX
/** Maximum value of size_t. */
#define SIZE_MAX 65535U
#endif
#ifndef WCHAR_MIN
/** Minimum value of wchar_t (unsigned on this target). */
#define WCHAR_MIN 0U
#endif
#ifndef WCHAR_MAX
/** Maximum value of wchar_t. */
#define WCHAR_MAX 65535U
#endif

#ifndef INT8_C
/** Expand an integer constant to type int_least8_t. */
#define INT8_C(value) value
#endif
#ifndef INT16_C
/** Expand an integer constant to type int_least16_t. */
#define INT16_C(value) value
#endif
#ifndef INT32_C
/** Expand an integer constant to type int_least32_t. */
#define INT32_C(value) value ## L
#endif
#ifndef UINT8_C
/** Expand an integer constant to type uint_least8_t. */
#define UINT8_C(value) value
#endif
#ifndef UINT16_C
/** Expand an integer constant to type uint_least16_t. */
#define UINT16_C(value) value ## U
#endif
#ifndef UINT32_C
/** Expand an integer constant to type uint_least32_t. */
#define UINT32_C(value) value ## UL
#endif
#ifndef INTMAX_C
/** Expand an integer constant to type intmax_t. */
#define INTMAX_C(value) value ## L
#endif
#ifndef UINTMAX_C
/** Expand an integer constant to type uintmax_t. */
#define UINTMAX_C(value) value ## UL
#endif

#ifndef _WCHAR_T
#define _WCHAR_T
/** Unsigned 16-bit wide character type. */
typedef unsigned int wchar_t;
#endif

#endif
