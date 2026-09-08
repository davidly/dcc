#include <stdio.h>

int countdown(int n)
{
    while (n > 0)
        --n;
    return n;
}

int accumulate(int n)
{
    int sum = 0;
    while (n > 0) {
        sum += n;
        --n;
    }
    return sum;
}

unsigned int divide7(unsigned int value)
{
    unsigned int quotient = 0;
    while (7 <= value) {
        value -= 7;
        ++quotient;
    }
    return quotient;
}

int repeated(int factor)
{
    int total = 0;
    int index;
    for (index = 0; index < 5; ++index) {
        total += factor;
        total += factor;
    }
    return total;
}

int compare(int left, int right)
{
    if (left < right)
        return 11;
    return 22;
}

int main(void)
{
    printf("%d %d %u %d %d %d\n",
           countdown(5), accumulate(5), divide7(50), repeated(3),
           compare(2, 3), compare(4, 3));
    return 0;
}
