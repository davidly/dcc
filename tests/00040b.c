/*
 * 00040b.c - faster stand-in for tests/extended-tests/tests/single-exec/00040.c
 * (upstream c-testsuite, itself imported from simple-cc's
 * tests/scc/execute/0041-queen.c - see that file's .otags).
 *
 * The original solves 8-queens with a deliberately unpruned, whole-board
 * recheck per candidate cell and uses "&" (not "&&") in its safety checks,
 * so nothing short-circuits. On real dcc-compiled Z80 code under ntvcm that
 * comes to ~14.8 billion emulated cycles (~14s even unthrottled) - by far
 * the single most expensive case in the whole extended suite, and pure
 * algorithmic cost: the "&"/K&R-declaration/recursion-with-globals/calloc
 * constructs it exercises are identical for any board size, so a smaller
 * board is not weaker coverage, just less of the same work. N=7 (40
 * solutions, verified against the known closed-form count) runs in about
 * 1/8th the cycles.
 *
 * tests/_extended_test_overrides.json skips the original "00040" by name
 * (with a comment pointing back here) rather than editing the vendored
 * copy in place - that file lives in the tests/extended-tests git
 * submodule (pinned to an upstream c-testsuite commit), so it needs to
 * stay byte-identical to what a future re-sync would fetch anyway.
 */
#include <stdio.h>
#include <stdlib.h>

#define D 7

int N;
int *t;

int
chk(int x, int y)
{
        int i;
        int r;

        for (r=i=0; i<D; i++) {
                r = r + t[x + D*i];
                r = r + t[i + D*y];
                if (x+i < D & y+i < D)
                        r = r + t[x+i + D*(y+i)];
                if (x+i < D & y-i >= 0)
                        r = r + t[x+i + D*(y-i)];
                if (x-i >= 0 & y+i < D)
                        r = r + t[x-i + D*(y+i)];
                if (x-i >= 0 & y-i >= 0)
                        r = r + t[x-i + D*(y-i)];
        }
        return r;
}

int
go(int n, int x, int y)
{
        if (n == D) {
                N++;
                return 0;
        }
        for (; y<D; y++) {
                for (; x<D; x++)
                        if (chk(x, y) == 0) {
                                t[x + D*y]++;
                                go(n+1, x, y);
                                t[x + D*y]--;
                        }
                x = 0;
        }
	return 0;
}

int
main()
{
        t = calloc(D*D, sizeof(int));
        go(0, 0, 0);
        printf("%d-queens: %d solutions\n", D, N);
        if (N != 40)
        	return 1;
        return 0;
}
