#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

/* implemented in dccrtl.mac */

/** Copy n bytes from a non-overlapping source object to a destination object. */
void *   memcpy( void *, const void *, size_t );
/** Fill n bytes of an object with the low byte of c. */
void *   memset( void *, int, size_t );
/** Copy n bytes between objects, safely handling overlap. */
void *   memmove( void *, const void *, size_t );
/** Compare n bytes from two objects. */
int      memcmp( const void *, const void *, size_t );
/** Find the first matching byte within n bytes of an object. */
void *   memchr( const void *, int, size_t );
/** Length of a string excluding the terminating NUL. */
size_t   strlen( const char * );
/** Lexicographic string comparison. */
int      strcmp( const char *, const char * );
/** Case-insensitive lexicographic string comparison (ASCII only). */
int      stricmp( const char *, const char * );
/** Copy a string including its terminating NUL. */
char *   strcpy( char *, const char * );
/** Return text for an error number. */
char *   strerror( int );
/** Append at most n characters from one string to another, then write a NUL. */
char *   strncat( char *, const char *, size_t );
/** Find the last occurrence of a character in a string. */
char *   strrchr( const char *, int );
/** Find the first occurrence of a character in a string. */
char *   strchr( const char *, int );
/** Find the first occurrence of a substring in a string. */
char *   strstr( const char *, const char * );
/** Compare at most n characters from two strings. */
int      strncmp( const char *, const char *, size_t );
/** Append one string to another. */
char *   strcat( char *, const char * );
/** Copy at most n characters from one string to another, padding with NULs. */
char *   strncpy( char *, const char *, size_t );
/** Locale-independent string comparison, equivalent to strcmp in dcc. */
int      strcoll( const char *, const char * );
/** Transform s2 for strcoll comparison into s1, writing at most n chars; returns strlen(s2). */
size_t   strxfrm( char *, const char *, size_t );
/** Length of the leading segment containing none of the reject characters. */
size_t   strcspn( const char *, const char * );
/** Find the first character from a set in a string. */
char *   strpbrk( const char *, const char * );
/** Length of the leading segment containing only accepted characters. */
size_t   strspn( const char *, const char * );
/** Split a string into tokens separated by delimiter characters. */
char *   strtok( char *, const char * );

/* dcc extension; not part of C89 */
/** Allocate and return a heap copy of a string. */
char *   strdup( const char * );

#endif
