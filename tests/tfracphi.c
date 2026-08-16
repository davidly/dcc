/*
 * tfracphi.c - PHI fallthrough regression through aggregate operations.
 * Generated archive case: batch9/c9922.
 */
#include <stdio.h>

struct Fraction {
    int numerator;
    int denominator;
};

static int gcd(int first, int second)
{
    while (second) {
        int next = first % second;
        first = second;
        second = next;
    }
    return first;
}

static void reduce(struct Fraction *value)
{
    int divisor = gcd(value->numerator < 0
                      ? -value->numerator : value->numerator,
                      value->denominator);

    value->numerator /= divisor;
    value->denominator /= divisor;
}

static struct Fraction add(const struct Fraction *first,
                           const struct Fraction *second)
{
    struct Fraction result = {
        .numerator = first->numerator * second->denominator +
                     second->numerator * first->denominator,
        .denominator = first->denominator * second->denominator
    };

    reduce(&result);
    return result;
}

int main(void)
{
    struct Fraction value =
        add(&(struct Fraction){ 1, 6 }, &(struct Fraction){ 1, 4 });
    int ok = value.numerator == 5 && value.denominator == 12;

    printf("tfracphi fraction=%d/%d\n",
           value.numerator, value.denominator);
    if (ok)
        printf("tfracphi passed with great success\n");
    return !ok;
}
