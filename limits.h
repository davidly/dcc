#ifndef _LIMITS_H
#define _LIMITS_H

/** Maximum value of signed int. */
#define INT_MAX 32767
/** Minimum value of signed int. */
#define INT_MIN (-32767 - 1)
/** Maximum value of signed short. */
#define SHRT_MAX 32767
/** Minimum value of signed short. */
#define SHRT_MIN (-32767 - 1)
/** Maximum value of unsigned short. */
#define USHRT_MAX 65535
/** Maximum value of unsigned int. */
#define UINT_MAX 65535

/** Minimum value of signed long. */
#define LONG_MIN (-2147483647L - 1L)
/** Maximum value of signed long. */
#define LONG_MAX 2147483647
/** Maximum value of unsigned long. */
#define ULONG_MAX 4294967295
#ifndef UINT32_MAX
/** Maximum value of uint32_t. */
#define UINT32_MAX 4294967295UL
#endif

/** Number of bits in a char. */
#define CHAR_BIT 8
/** Minimum value of signed char. */
#define SCHAR_MIN -128
/** Maximum value of signed char. */
#define SCHAR_MAX 127
/** Maximum value of unsigned char. */
#define UCHAR_MAX 255
/** Minimum value of plain char. */
#define CHAR_MIN -128
/** Maximum value of plain char. */
#define CHAR_MAX 127

#endif
