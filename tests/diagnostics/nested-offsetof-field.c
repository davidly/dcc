struct S {
    int x;
};

enum E {
    A = __offsetof(struct S, x.y)
};