#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>

#ifndef NULL
/** Null pointer constant. */
#define NULL 0
#endif

/** Successful program termination status. */
#define EXIT_SUCCESS 0
/** Unsuccessful program termination status. */
#define EXIT_FAILURE 1
/** Maximum value returned by rand. */
#define RAND_MAX 32767
/** Maximum number of functions that can be registered with atexit(). */
#define ATEXIT_MAX 32

/** Quotient and remainder pair returned by div. */
typedef struct {
    int quot;
    int rem;
} div_t;

/** Quotient and remainder pair returned by ldiv. */
typedef struct {
    long quot;
    long rem;
} ldiv_t;

/** Terminate the program after flushing runtime output. */
_Noreturn void exit( int code );
/** Terminate the program abnormally; does not call atexit handlers. */
_Noreturn void abort( void );
/** Register func to be called at normal program termination (LIFO order).
 *  Returns 0 on success, nonzero if the ATEXIT_MAX table is full. */
int  atexit( void (*func)(void) );
/** Return the next pseudo-random integer in the range 0 through RAND_MAX. */
int rand(void);
/** Seed the pseudo-random number generator. */
void srand(unsigned int seed);
/** Convert the leading decimal text in nptr to int. */
int atoi(const char *nptr);
/** Convert the leading decimal text in nptr to long. */
long atol(const char *nptr);
/** Convert the leading decimal text in nptr to float.
 *  Note: C89 atof normally returns double; dcc has no double type,
 *  so this returns float (IEEE 754 single precision).  Accepts nan,
 *  inf, and infinity spellings.  Overflow returns signed infinity;
 *  underflow returns signed zero. */
float atof(const char *nptr);
/** Convert text in nptr to long using base 2 through 36, or base 0 for auto-detection. */
long strtol(const char *nptr, char **endptr, int base);
/** Convert text in nptr to unsigned long using base 2 through 36, or base 0 for auto-detection. */
unsigned long strtoul(const char *nptr, char **endptr, int base);
/** Convert leading floating-point text in nptr to float; sets *endptr past consumed input.
 *  Note: C89 strtod returns double; dcc returns float (no double type). */
float strtod(const char *nptr, char **endptr);
/** Absolute value of a signed int. */
int abs(int j);
/** Absolute value of a signed long. */
long labs(long j);
/** Signed int division returning quotient and remainder. */
div_t div(int numer, int denom);
/** Signed long division returning quotient and remainder. */
ldiv_t ldiv(long numer, long denom);
/** Binary-search a sorted array. */
void *bsearch(const void *key, const void *base, size_t num, size_t size, int (*compare)(const void *, const void *));
/** Sort an array in place. */
void qsort(void *base, size_t num, size_t size, int (*compare)(const void *, const void *));

/** Allocate size bytes from the heap. */
void *malloc( size_t size );
/** Allocate and zero num * size bytes from the heap. */
void *calloc( size_t num, size_t size );
/** Resize a heap allocation, preserving contents up to the smaller size. */
void *realloc( void *ptr, size_t size );
/** Release a heap allocation. */
void free( void *ptr );

/** Search the environment for name; always returns NULL on CP/M 2.2. */
char *getenv(const char *name);
/** Execute a shell command. CP/M 2.2 has no command processor: system(NULL)
 * correctly reports that via 0 (false), and any non-NULL command returns -1
 * (unsupported). */
int system(const char *string);

/* Multibyte / wide-character conversion (C89 7.10.7 / 7.10.8).
 * C locale: MB_CUR_MAX=1; byte values 0x00..0xFF map to equal-valued wchar_t
 * values. Wider values are not representable in the execution encoding. */
/** Maximum bytes in a multibyte character in the current locale. */
#define MB_CUR_MAX 1
/** Length of the multibyte character at s, examining at most n bytes. */
int mblen(const char *s, size_t n);
/** Convert the multibyte character at s into *pwc; examine at most n bytes. */
int mbtowc(wchar_t *pwc, const char *s, size_t n);
/** Convert wc into one byte at s, or return -1 if wc is unrepresentable. */
int wctomb(char *s, wchar_t wc);
/** Convert at most n multibyte characters from s into the wchar_t array pwcs. */
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
/** Convert at most n bytes from pwcs into s; return (size_t)-1 on an
 *  unrepresentable wide character. */
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);

/* dcc extensions (not C89) for talking to CP/M and hardware directly:
 *   bdos()      calls the CP/M BDOS entry point (fn -> C, dearg -> DE); the
 *               byte result is returned in the low byte of the int.  Calls
 *               whose useful result is an FCB/DMA region return it through the
 *               memory that dearg points at, not in the return value.
 *   bdoshl()    same call as bdos(), but for BDOS functions whose useful
 *               result is a 16-bit value in HL (e.g. BDOS 12 get version)
 *               rather than a byte in A; returns HL exactly as BDOS left it.
 *   bios()      calls the CP/M BIOS jump table directly (fn -> BIOS function
 *               number 0-16; dearg -> loaded into BC, DE, and C, since unlike
 *               BDOS the BIOS has no single argument register convention).
 *               fn 0/1 (boot/wboot) end the program instead of returning.
 *               The byte result is returned in the low byte of the int, for
 *               functions that return a byte in A (const, conin, reader,
 *               read, write, listst).
 *   bioshl()    same call as bios(), but for BIOS functions whose useful
 *               result is an address in HL (seldsk, sectran) rather than a
 *               byte in A; returns HL exactly as the BIOS call left it. A is
 *               not mirrored into L the way BDOS conventionally does, so
 *               bioshl() on a byte-returning function is not meaningful.
 *   biosreg()   calls the BIOS with independent 16-bit BC and DE arguments
 *               and returns HL exactly.  Use it for multi-register forms such
 *               as sectran (sector in BC, translation-table address in DE)
 *               and CP/M 3 seldsk (drive in C, login flag in E).  The existing
 *               bios()/bioshl() two-argument ABI remains the one-register
 *               convenience form.
 *   inp()/outp() do direct Z80 8-bit port I/O.  inp() runs IN A,(port) and
 *               returns the byte zero-extended to int (0..255); outp() runs
 *               OUT (port),A.  Only the low 8 bits of port are significant. */
/** Call the CP/M BDOS entry point. */
int  bdos( int fn, int dearg );
/** Call the CP/M BDOS entry point, returning the full HL result. */
int  bdoshl( int fn, int dearg );
/** Call the CP/M BIOS jump table directly. */
int  bios( int fn, int dearg );
/** Call the CP/M BIOS jump table directly, returning the full HL result. */
int  bioshl( int fn, int dearg );
/** Call the CP/M BIOS with independent BC and DE arguments, returning HL. */
int  biosreg( int fn, int bcarg, int dearg );
/** Read an 8-bit Z80 I/O port. */
int  inp( unsigned port );
/** Write an 8-bit Z80 I/O port. */
void outp( unsigned port, unsigned val );
/** Load and run path, replacing this process. cmdtail is copied through its
 *  first NUL or CR into private staging before loader side effects, then to
 *  0x81 (e.g. " ARG1 ARG2"). At most 127 text bytes are accepted; a trailing
 *  CR is written when space remains. The first two arguments, delimited by
 *  bytes through ASCII space, also seed the default FCBs at 0x5C and 0x6C.
 *  path must be an unambiguous CP/M 8.3
 *  filename with an optional A: through P: drive prefix; ".COM" is appended
 *  when no extension is present. Returns -1 with errno=E2BIG for an oversized
 *  tail, EINVAL for an invalid path, ENOENT when the file cannot be opened, or
 *  EFBIG when its 128-byte-record-rounded image cannot fit safely in the TPA;
 *  does not return on success. */
int  exec( const char *path, const char *cmdtail );
/** Like exec() but builds the command tail from argv[1..] (argv[0] is the
 *  conventional program name and is ignored for CP/M purposes).
 *  argv must be a NULL-terminated array of string pointers. Because DCC's
 *  direct CP/M startup parser deliberately has no quoting/escape syntax, each
 *  argument must be nonempty and contain no byte through ASCII space; quote
 *  and backslash are ordinary bytes. Returns -1 with errno=EINVAL when an
 *  argument cannot round-trip, or errno=E2BIG above the 127-byte tail limit. */
int  execv( const char *path, char **argv );

#endif
