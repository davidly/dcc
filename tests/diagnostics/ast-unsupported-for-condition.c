struct S {
    int x;
};

int f(void)
{
    struct S s;
    for (; s.y; ) {
        return 1;
    }
    return 0;
}