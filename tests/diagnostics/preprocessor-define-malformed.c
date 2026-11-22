/* Malformed #define directives, each diagnosed rather than half-recorded. */
#define
#define 1X 2
#define F(a, a) a
#define G(a 1
#define H(a,) a
#define J(..., a) a
#define defined 1
#define S(x) #y
#define P(a) ## a
#define Q(a) a ##

int x;
