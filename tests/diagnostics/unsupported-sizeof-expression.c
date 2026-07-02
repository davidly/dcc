struct S {
    int x;
};

int y = sizeof(((struct S*)0)->y);