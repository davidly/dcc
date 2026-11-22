/* Two case labels with one value, as two macros of one value give, and two
 * default labels, are errors rather than an internal failure. */
#define OP_LOAD 1
#define OP_STORE 1

int f(int op)
{
    switch (op) {
    case OP_LOAD:
        return 10;
    case OP_STORE:
        return 20;
    default:
        return 0;
    default:
        return 1;
    }
}

int g(unsigned char c)
{
    /* Not duplicates: case values convert to the promoted type, int. */
    switch (c) {
    case 1:
        return 1;
    case 257:
        return 2;
    }
    return 0;
}
