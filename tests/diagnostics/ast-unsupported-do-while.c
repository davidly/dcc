struct S {
    int x;
};

int f(void)
{
    struct S s;
    do {
        return 1;
    } while (s.y);
    return 0;
}
