struct S {
    int x;
};

enum E {
    A = __offsetof(struct S, 1)
};