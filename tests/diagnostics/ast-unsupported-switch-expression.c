struct S {
    int x;
};

int f(void)
{
    struct S s;
    switch (s.y) {
    case 1:
        return 1;
    }
    return 0;
}