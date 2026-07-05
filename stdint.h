#ifndef H_STDINT
#define H_STDINT

/** Signed 8-bit integer type. */
typedef char int8_t;
/** Unsigned 8-bit integer type. */
typedef unsigned char uint8_t;
/** Unsigned 16-bit integer type. */
typedef unsigned int uint16_t;
/** Signed 16-bit integer type. */
typedef int int16_t;
/** Signed 32-bit integer type. */
typedef long int32_t;
/** Unsigned 32-bit integer type. */
typedef unsigned long uint32_t;
/** Signed integer type capable of holding a pointer (16-bit target). */
typedef int intptr_t;
/** Unsigned integer type capable of holding a pointer (16-bit target). */
typedef unsigned int uintptr_t;
#ifndef _WCHAR_T
#define _WCHAR_T
/** Unsigned 16-bit wide character type. */
typedef unsigned int wchar_t;
#endif

#endif
