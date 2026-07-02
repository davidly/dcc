struct S {
    int x;
};

int f(void)
{
    struct S s;
    if (s.y)
        return 1;
    return 0;
}