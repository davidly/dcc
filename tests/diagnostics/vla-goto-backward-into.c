int f(int n)
{
    {
        int a[n];
inside:
        a[0] = 1;
    }
    {
        int b[n];
        b[0] = 2;
        goto inside;
    }
    return 0;
}