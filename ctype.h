#ifndef _CTYPE_H
#define _CTYPE_H

/** Test for an ASCII alphabetic character. */
int isalpha(int c);
/** Test for an ASCII decimal digit. */
int isdigit(int c);
/** Test for an ASCII alphabetic character or decimal digit. */
int isalnum(int c);
/** Test for ASCII whitespace. */
int isspace(int c);
/** Test for an ASCII uppercase letter. */
int isupper(int c);
/** Test for an ASCII lowercase letter. */
int islower(int c);
/** Test for an ASCII hexadecimal digit. */
int isxdigit(int c);
/** Test for an ASCII printable character, including space. */
int isprint(int c);
/** Test for an ASCII printable character other than space. */
int isgraph(int c);
/** Test for an ASCII control character. */
int iscntrl(int c);
/** Test for an ASCII punctuation character. */
int ispunct(int c);
/** Convert an ASCII lowercase letter to uppercase, leaving other characters unchanged. */
int toupper(int c);
/** Convert an ASCII uppercase letter to lowercase, leaving other characters unchanged. */
int tolower(int c);

#endif
