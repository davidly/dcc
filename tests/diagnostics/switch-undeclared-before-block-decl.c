/* An undeclared name in a statement that also holds a declaration is still
 * reported, rather than reaching code generation (syntax-crash seed
 * 1123277). */
int f(int x)
{
    switch (x) {
    case 1:
        foo;
    case 2: {
        int n;
    }
    }
    return x;
}
