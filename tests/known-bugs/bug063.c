/* BUG-063: a store through a pointer declared in a nested block keeps the
 * 16-bit conversion that was inserted for the assumed assignment target, so
 * only the low word of a 32-bit value reaches the pointee. The same store is
 * correct when the pointer is declared at function scope. */
#include <stdint.h>
#include <stdio.h>

int32_t a[2] = { 257, 3 };

static void block_ptr(int32_t x)
{
    {
        int32_t *q = &a[0];
        *q = x;
    }
}

static void block_const(void)
{
    {
        int32_t *q = &a[1];
        *q = (int32_t)65536;
    }
}

static void func_ptr(int32_t x)
{
    int32_t *q = &a[0];
    *q = x;
}

int main(void)
{
    block_ptr(65536);
    printf("block %ld\n", (long)a[0]);
    block_ptr(-65537);
    printf("block negative %ld\n", (long)a[0]);
    block_const();
    printf("block constant %ld\n", (long)a[1]);
    func_ptr(65536);
    printf("function %ld\n", (long)a[0]);
    return 0;
}
