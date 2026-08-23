// Minimal compatibility shim for building x80.cxx (the 8080/Z80 core from
// ntvcm) inside this repo. The upstream djl_os.hxx is a large cross-platform
// utility header; the x80 core only needs a handful of helpers from it, which
// are reproduced here so the core compiles without pulling in the rest.

#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#if defined( __clang__ ) || defined( __GNUC__ )
    #define not_inlined __attribute__ ((noinline))
    #define force_inlined inline
#elif defined( _MSC_VER )
    #define not_inlined __declspec(noinline)
    #define force_inlined __forceinline
#else
    #define not_inlined
    #define force_inlined inline
#endif

#ifndef _countof
template < typename T, size_t N > constexpr size_t _countof( T ( & )[ N ] ) { return N; }
#endif

inline uint16_t flip_endian16( uint16_t x )
{
#if defined( __clang__ ) || defined( __GNUC__ )
    return __builtin_bswap16( x );
#else
    return (uint16_t) ( ( x >> 8 ) | ( ( x & 0xff ) << 8 ) );
#endif
}

inline uint8_t bit_count8( uint8_t x )
{
#if defined( __GNUC__ ) || defined( __clang__ )
    return (uint8_t) __builtin_popcount( x );
#else
    uint8_t count = 0;
    while ( 0 != x ) { x &= ( x - 1 ); count++; }
    return count;
#endif
}

inline bool is_parity_even8( uint8_t x )
{
    uint8_t folded = ( x ^ ( x >> 4 ) ) & 0x0f;
    return ( 0 == ( ( 0x6996u >> folded ) & 1u ) );
}
