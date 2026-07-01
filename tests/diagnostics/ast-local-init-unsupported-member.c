struct S {
    int x;
};

int f(void)
{
    struct S s;
    int x = s.y;
    return 0;
}