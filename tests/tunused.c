#include <stdio.h>

struct Big { int a; int b; int c; int d; };

int g;
int side_effect_calls = 0;

static int bump(void)
{
    side_effect_calls++;
    return 42;
}

/* Plain scalar locals: one genuinely unused, others live - unused must not
 * shift the offsets of the ones that are. */
static int scalars(int x)
{
    int used;
    int unused;
    int used2;

    used = x + 1;
    used2 = used * 2;
    g = used2;
    return g;
}

/* Array and struct locals with no initializer, both entirely unused. */
static int aggregates(int x)
{
    int arr_unused[10];
    struct Big struct_unused;
    int keep;

    keep = x + 1;
    return keep;
}

int main(void)
{
    int with_init = 5;      /* has an initializer, unused after - must be kept */
    int side_init = bump(); /* initializer has a side effect - must still run */
    struct Big p;
    int used;

    p.a = 1;
    p.b = 2;              /* member access sharing a plain-local-like name */
    used = p.a;

    {
        int shadow = 99;
        printf("shadow=%d\n", shadow);
    }

    printf("scalars=%d aggregates=%d\n", scalars(5), aggregates(7));
    printf("with_init=%d side_calls=%d p.a=%d used=%d\n",
           with_init, side_effect_calls, p.a, used);
    return 0;
}
