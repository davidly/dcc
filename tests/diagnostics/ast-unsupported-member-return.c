struct S {
    int x;
};

int f(void)
{
    struct S s;
    return s.y;
}