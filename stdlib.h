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
void exit( int code );
/** Terminate the program abnormally; does not call atexit handlers. */
void abort( void );
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
const void *bsearch(const void *key, const void *base, size_t num, size_t size, int (*compare)(const void *, const void *));
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
/** Execute a shell command; always returns -1 on CP/M 2.2 (no command processor). */
int system(const char *string);

/* Multibyte / wide-character conversion (C89 7.10.7 / 7.10.8).
 * C/ASCII locale: MB_CUR_MAX=1; every char is its own single-byte sequence. */
/** Maximum bytes in a multibyte character in the current locale. */
#define MB_CUR_MAX 1
/** Length of the multibyte character at s, examining at most n bytes. */
int mblen(const char *s, size_t n);
/** Convert the multibyte character at s into *pwc; examine at most n bytes. */
int mbtowc(wchar_t *pwc, const char *s, size_t n);
/** Convert wide character wc into the multibyte sequence at s. */
int wctomb(char *s, wchar_t wc);
/** Convert at most n multibyte characters from s into the wchar_t array pwcs. */
size_t mbstowcs(wchar_t *pwcs, const char *s, size_t n);
/** Convert at most n wide characters from pwcs into the char array s. */
size_t wcstombs(char *s, const wchar_t *pwcs, size_t n);

/* dcc extensions (not C89) for talking to CP/M and hardware directly:
 *   bdos()      calls the CP/M BDOS entry point (fn -> C, dearg -> DE); the
 *               byte result is returned in the low byte of the int.  Calls
 *               whose useful result is an FCB/DMA region return it through the
 *               memory that dearg points at, not in the return value.
 *   inp()/outp() do direct Z80 8-bit port I/O.  inp() runs IN A,(port) and
 *               returns the byte zero-extended to int (0..255); outp() runs
 *               OUT (port),A.  Only the low 8 bits of port are significant. */
/** Call the CP/M BDOS entry point. */
int  bdos( int fn, int dearg );
/** Read an 8-bit Z80 I/O port. */
int  inp( unsigned port );
/** Write an 8-bit Z80 I/O port. */
void outp( unsigned port, unsigned val );
/** Load and run path, replacing this process.  cmdtail is copied to 0x81
 *  (e.g. " ARG1 ARG2"); the first two whitespace-delimited args also seed
 *  the default FCBs at 0x5C and 0x6C.  Returns -1 if the file is not found;
 *  does not return on success. */
int  exec( const char *path, const char *cmdtail );
/** Like exec() but builds the command tail from argv[1..] (argv[0] is the
 *  conventional program name and is ignored for CP/M purposes).
 *  argv must be a NULL-terminated array of string pointers. */
int  execv( const char *path, char **argv );

#endif
