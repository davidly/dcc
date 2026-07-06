int f(int n)
{
    goto inside;
    {
        int a[n];
inside:
        a[0] = 5;
        return a[0];
    }
}
