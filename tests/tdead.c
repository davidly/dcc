#include <stdio.h>

int poison(int x)
{
    printf("poison:%d\n", x);
    return x + 1000;
}

int dr_ret(int x)
{
    return x + 1;
    x = poison(x);
    return x + 2;
}

static inline int si_ret(int x)
{
    return x + 4;
    x = poison(x);
    return x + 5;
}

int dd_decl(int x)
{
    return x + 3;
    {
        int y = poison(x);
        return y;
    }
}

int dg_goto(int x)
{
    goto live;
    x = poison(x);
live:
    return x + 2;
}

int di_if(int x)
{
    if (x)
        return 7;
    else
        return 8;
    return poison(x);
}

int ds_sw(int x)
{
    switch (x) {
    case 1:
        return 10;
        x = poison(x);
    case 2:
        return 20;
    default:
        return 30;
    }
}

int main(void)
{
    printf("dead: %d %d %d %d %d %d %d\n",
           dr_ret(4), si_ret(4), dd_decl(4), dg_goto(4), di_if(1),
           ds_sw(1), ds_sw(2));
    return 0;
}