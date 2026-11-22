/* Accepted with warnings: tokens after a directive's operands, a macro
 * redefined with a different body, and a backslash followed by blanks
 * before the newline. */
#define A 1
#ifdef A B
#endif A
#if 0
#else junk
#endif
#undef A B
#define N 1
#define N 2
#define M (1 + 2)
#define M (1  +  2)
#define C 3 + \  
    4

int x = N + M + C;
