struct S {
    int x;
};

int f(void)
{
    struct S s;
    s = (struct S)1;
    return 0;
}