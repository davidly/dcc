struct S {
    int x: 3;
};

int missing_symbol;
struct S s = { .x = missing_symbol };