/* computes digits of e to 192 places */

#define DIGITS_TO_FIND 200

#include <stdio.h>

extern int main() {
#if 0 // generates faster code, but the benchmark is in part about a compiler doing this on its own.
  unsigned char N, n;
#else
  int N;
  register int n;
#endif
  int x;
  int a[DIGITS_TO_FIND];

  N = DIGITS_TO_FIND;
  x = 0;

  for (n = N - 1; n > 0; --n)
    a[n] = 1;

  a[1] = 2;
  a[0] = 0;

  while (N > 9) {
    n = N--;
    while (--n) {
      a[n] = x % n;
      x = 10 * a[n-1] + x / n;
    }

    printf("%u", x);
  }

  printf("\ndone\n");
  return 0;
}
