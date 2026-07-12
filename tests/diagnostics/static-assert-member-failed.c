struct S {
    _Static_assert(sizeof(int) == 4, "member assertion failed");
    int value;
};