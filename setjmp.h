/* setjmp.h for dcc - Z80/CP-M */

#ifndef _SETJMP_H
#define _SETJMP_H

/** Saved non-local jump context for setjmp/longjmp. */
typedef unsigned int jmp_buf[4];

/* Implemented in dccrtl.mac (_setjmp / _longjmp).  C89 specifies setjmp as a
 * macro, but dcc's runtime entry reads the caller frame via IX, so an ordinary
 * prototype matches how it is actually called and gives the editor type info. */
/** Save the current execution context and return 0 on the direct call. */
int  setjmp(jmp_buf env);
/** Restore a context saved by setjmp and make that setjmp return val, or 1 if val is 0. */
_Noreturn void longjmp(jmp_buf env, int val);

#endif

